//! The egui editor. Everything is drawn with the painter in a 1200 x 900 design space that scales with the window:
//! animated indigo waves behind, the growing tree on the left running through the three page boxes, a big tree on
//! the home page, level-fill circle knobs, selector pills and a row of buttons along the bottom.

use crate::{scope, skins, storage, tree, Shared};
use nih_plug::prelude::*;
use nih_plug_egui::{create_egui_editor, egui, resizable_window::ResizableWindow, EguiState};
use seedling_dsp::archetypes::{name_for_seed, FIRST_ARCHETYPE_SEED, SEED_RANGE};
use seedling_dsp::params::*;
use seedling_dsp::randomizer::{correct_tune, random_values, SplitMix};
use seedling_dsp::wavetable::import_wav_bytes;
use std::f32::consts::{FRAC_PI_2, PI, TAU};
use std::sync::atomic::Ordering;
use std::sync::Arc;

use egui::epaint::{Mesh, TextShape};
use egui::{Align, Align2, Color32, FontFamily, FontId, Pos2, Rect, Sense, Shape, Stroke, StrokeKind, Vec2};

const W: f32 = 1200.0;
const H: f32 = 900.0;

// Autumn tree: blocks from bark brown at the roots to gold at the twigs, orange-red tips, pale beacons.
/// block size in tree-scale units (times the map's `k`)
const AUTUMN_GAP: f32 = 1.5;
/// block width per tier (roots, trunk, three limb tiers, twigs), in cells
const AUTUMN_SZ: [f32; 6] = [0.8, 2.0, 1.5, 1.2, 1.0, 1.0];
const AUTUMN_COL: [[u8; 3]; 6] = [[0x8d, 0x5a, 0x3b], [0xb5, 0x73, 0x3c], [0xcf, 0x8a, 0x43], [0xe5, 0xa8, 0x4a], [0xf2, 0xc1, 0x4e], [0xff, 0xdd, 0x7a]];
const AUTUMN_TIP: [u8; 3] = [0xff, 0x7b, 0x54];
const AUTUMN_BEACON: [u8; 3] = [0xff, 0xf1, 0xa8];
const AUTUMN_HALO: [u8; 3] = [0xff, 0xb3, 0x47];
const AUTUMN_SEED: [u8; 3] = [0xb5, 0x73, 0x3c];
/// the limb of the knob being dragged
const HOT_LIMB: [u8; 3] = [0x7f, 0xe7, 0xd4];

fn rgb(c: [u8; 3]) -> Color32 { Color32::from_rgb(c[0], c[1], c[2]) }

/// The editor's colours (from the chosen skin).
#[derive(Clone, Copy)]
struct Pal {
    ink: Color32,
    paper: Color32,
    mist: Color32,
    back: [f32; 3],
    mid: [f32; 3],
    front: [f32; 3],
    glow: Color32,
    window: Color32,
    widget: Color32,
    widget_hover: Color32,
    widget_active: Color32,
    ink_rgb: [u8; 3],
    mist_rgb: [u8; 3],
    hover_rgb: [u8; 3],
    glow_rgb: [u8; 3],
}

impl Pal {
    fn from_theme(t: &skins::Theme) -> Pal {
        let f = |c: [u8; 3]| [c[0] as f32, c[1] as f32, c[2] as f32];
        Pal {
            ink: rgb(t.ink),
            paper: rgb(t.paper),
            mist: rgb(t.mist),
            back: f(t.back),
            mid: f(t.mid),
            front: f(t.ink),
            glow: rgb(t.glow),
            window: rgb(t.window),
            widget: rgb(t.widget),
            widget_hover: rgb(t.widget_hover),
            widget_active: rgb(t.widget_active),
            ink_rgb: t.ink,
            mist_rgb: t.mist,
            hover_rgb: t.hover,
            glow_rgb: t.glow,
        }
    }
    fn ink_a(&self, a: u8) -> Color32 { Color32::from_rgba_unmultiplied(self.ink_rgb[0], self.ink_rgb[1], self.ink_rgb[2], a) }
    fn mist_a(&self, a: u8) -> Color32 { Color32::from_rgba_unmultiplied(self.mist_rgb[0], self.mist_rgb[1], self.mist_rgb[2], a) }
    fn hover_a(&self, a: u8) -> Color32 { Color32::from_rgba_unmultiplied(self.hover_rgb[0], self.hover_rgb[1], self.hover_rgb[2], a) }
    fn glow_a(&self, a: u8) -> Color32 { Color32::from_rgba_unmultiplied(self.glow_rgb[0], self.glow_rgb[1], self.glow_rgb[2], a) }
}

fn semi() -> FontFamily { FontFamily::Name("semi".into()) }
fn ital() -> FontFamily { FontFamily::Name("ital".into()) }

#[derive(Clone, Copy, PartialEq)]
enum Popup {
    None,
    Presets,
    Wavetables,
}

pub struct UiState {
    /// 0 envelope & LFO, 1 oscillators, 2 filter & effects, 3 home
    view: usize,
    last_edit: usize,
    seed_text: String,
    preset_name: String,
    status: String,
    rng: SplitMix,
    popup: Popup,
    tree: tree::Tree,
    /// seconds since the tree started growing
    grow_t: f32,
    /// seconds since the editor opened (twinkles)
    clock: f32,
    /// host-tempo clock in beats (waves, orbit and waveforms move with it)
    beats: f32,
    /// every skin on offer; one more entry after them is "Seed colours" (a palette grown from the patch seed)
    skins: Vec<skins::Skin>,
    skin_ix: usize,
    pal: Pal,
    /// what `pal` was made from (skin index, seed when the skin is the seed palette), so it is only rebuilt on a change
    pal_key: (usize, u32),
    /// parameter behind each knob that owns a limb, in limb order
    knobs: Vec<usize>,
    /// length factor of each knob's limb, easing towards its target
    lens: Vec<f32>,
    lens_ready: bool,
    /// the parameter whose knob is being dragged (its limb lights up)
    hot: Option<usize>,
    /// smoothed wind strength 0..1 (follows the loudness of the output) and the sway clock
    wind: f32,
    wind_phase: f32,
    /// last loudness value seen and how long it has stayed exactly the same (a host that stops processing leaves it stuck)
    level_bits: u32,
    level_age: f32,
}

impl UiState {
    fn skin_name(&self) -> String {
        match self.skins.get(self.skin_ix) {
            Some(s) => s.name.clone(),
            None => skins::SEED_COLOURS.to_string(),
        }
    }

    fn update_theme(&mut self, seed: u32) {
        let key = (self.skin_ix, if self.skin_ix >= self.skins.len() { seed } else { 0 });
        if key == self.pal_key {
            return;
        }
        let skin = match self.skins.get(self.skin_ix) {
            Some(s) => s.clone(),
            None => skins::from_seed(seed),
        };
        self.pal = Pal::from_theme(&skins::Theme::from_skin(&skin));
        self.pal_key = key;
    }
}

/// The parameters of every knob on the three edit pages, in page order. Knob number n owns limb n of the tree.
fn knob_order() -> Vec<usize> {
    let mut out = Vec::new();
    for view in 0..3 {
        for pn in panels(view) {
            let slots = match pn.kind {
                Kind::L => L_SLOTS.len(),
                Kind::S => S_SLOTS.len(),
                Kind::W(_) => W_SLOTS.len(),
                Kind::T => T_SLOTS.len(),
                Kind::V => 0,
            };
            out.extend(pn.knobs.iter().take(slots).copied());
            if matches!(pn.kind, Kind::V) {
                out.push(idx::GLIDE);
            }
        }
    }
    out
}

pub fn create(shared: Arc<Shared>) -> Option<Box<dyn Editor>> {
    let egui_state: Arc<EguiState> = shared.params.editor_state.clone();
    let nanos = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_nanos() as u64).unwrap_or(1);
    let (all_skins, _problems) = skins::load_all();
    let wanted = shared.params.skin.lock().map(|g| g.clone()).unwrap_or_default();
    let skin_ix = if wanted == skins::SEED_COLOURS { all_skins.len() } else { all_skins.iter().position(|s| s.name == wanted).unwrap_or(0) };
    let first = all_skins.get(skin_ix).cloned().unwrap_or_else(skins::original);
    let knobs = knob_order();
    debug_assert_eq!(knobs.len(), tree::OWN_COUNT);
    let state = UiState {
        view: 3,
        last_edit: 0,
        seed_text: String::new(),
        preset_name: String::from("My patch"),
        status: String::from("Press Randomize to grow a new sound."),
        rng: SplitMix::new(nanos ^ 0x9E37_79B9_7F4A_7C15),
        popup: Popup::None,
        tree: tree::Tree::build(),
        grow_t: 0.0,
        clock: 0.0,
        beats: 0.0,
        lens: vec![1.0; knobs.len()],
        lens_ready: false,
        knobs,
        skins: all_skins,
        skin_ix,
        pal: Pal::from_theme(&skins::Theme::from_skin(&first)),
        pal_key: (usize::MAX, 0),
        hot: None,
        wind: 0.0,
        wind_phase: 0.0,
        level_bits: 0,
        level_age: 0.0,
    };
    let window_state = egui_state.clone();
    create_egui_editor(
        egui_state,
        state,
        |ctx, _| install_fonts(ctx),
        move |ctx, setter, state| {
            state.update_theme(shared.params.seed.load(Ordering::Relaxed));
            ctx.set_visuals(visuals(&state.pal));
            ResizableWindow::new("seedling-window").min_size(Vec2::new(600.0, 450.0)).show(ctx, window_state.as_ref(), |ui| {
                draw(ui, &shared, setter, state);
            });
        },
    )
}

fn install_fonts(ctx: &egui::Context) {
    let mut fonts = egui::FontDefinitions::default();
    let data = |b: &'static [u8]| Arc::new(egui::FontData::from_static(b));
    fonts.font_data.insert("bodoni".into(), data(include_bytes!("../fonts/BodoniModa-Medium.ttf")));
    fonts.font_data.insert("bodoni_semi".into(), data(include_bytes!("../fonts/BodoniModa-SemiBold.ttf")));
    fonts.font_data.insert("bodoni_ital".into(), data(include_bytes!("../fonts/BodoniModa-MediumItalic.ttf")));
    fonts.families.entry(FontFamily::Proportional).or_default().insert(0, "bodoni".into());
    let fallback = fonts.families.get(&FontFamily::Proportional).cloned().unwrap_or_default();
    let mut with = |first: &str| {
        let mut list: Vec<String> = vec![first.to_string()];
        list.extend(fallback.iter().filter(|n| n.as_str() != first).cloned());
        list
    };
    let semi_list = with("bodoni_semi");
    let ital_list = with("bodoni_ital");
    fonts.families.insert(semi(), semi_list);
    fonts.families.insert(ital(), ital_list);
    ctx.set_fonts(fonts);
}

fn visuals(p: &Pal) -> egui::Visuals {
    let mut v = egui::Visuals::dark();
    v.panel_fill = p.ink;
    v.window_fill = p.window;
    v.window_stroke = Stroke::new(2.0, p.paper);
    v.override_text_color = Some(p.paper);
    v.widgets.inactive.weak_bg_fill = p.widget;
    v.widgets.inactive.bg_fill = p.widget;
    v.widgets.hovered.weak_bg_fill = p.widget_hover;
    v.widgets.hovered.bg_fill = p.widget_hover;
    v.widgets.active.weak_bg_fill = p.widget_active;
    v.widgets.active.bg_fill = p.widget_active;
    v.selection.bg_fill = p.widget_active;
    v
}

// ---- parameter helpers ------------------------------------------------------------------------------------------

fn to_norm(i: usize, v: f32) -> f32 {
    let d = &PARAMS[i];
    ((v - d.lo) / (d.hi - d.lo)).clamp(0.0, 1.0)
}

fn set_norm(setter: &ParamSetter, ptr: ParamPtr, norm: f32) {
    unsafe {
        setter.raw_context.raw_begin_set_parameter(ptr);
        setter.raw_context.raw_set_parameter_normalized(ptr, norm);
        setter.raw_context.raw_end_set_parameter(ptr);
    }
}

fn apply_values(setter: &ParamSetter, shared: &Shared, vals: &[f32]) {
    let ptrs = shared.params.ptrs();
    for i in 0..NUM_PARAMS {
        if PARAMS[i].flags & NO_RANDOM != 0 {
            continue;
        }
        set_norm(setter, ptrs[i], to_norm(i, vals[i]));
    }
}

fn grow(setter: &ParamSetter, shared: &Shared, seed: u32) -> String {
    let mut vals = random_values(seed, shared.params.chaos.value());
    if shared.params.auto_tune.value() {
        correct_tune(&mut vals);
    }
    apply_values(setter, shared, &vals);
    shared.params.seed.store(seed, Ordering::Relaxed);
    seed_title(seed)
}

fn seed_title(seed: u32) -> String {
    match name_for_seed(seed) {
        Some(n) => format!("seed {seed} - {n}"),
        None => format!("seed {seed}"),
    }
}

fn load_wavetable(shared: &Shared, path: &std::path::Path) -> String {
    let Ok(bytes) = std::fs::read(path) else { return "Could not read that file.".into() };
    let name = path.file_stem().map(|s| s.to_string_lossy().to_string()).unwrap_or_default();
    let r = import_wav_bytes(&bytes, &name);
    match r.table {
        Some(t) => {
            let text = storage::table_to_text(&t);
            if let Ok(mut g) = shared.params.wavetable.lock() { *g = text; }
            if let Ok(mut g) = shared.params.wavetable_info.lock() { *g = r.message.clone(); }
            shared.slot.set(Some(Arc::new(t)));
            r.message
        }
        None => r.message,
    }
}

fn clear_wavetable(shared: &Shared) {
    if let Ok(mut g) = shared.params.wavetable.lock() { g.clear(); }
    if let Ok(mut g) = shared.params.wavetable_info.lock() { g.clear(); }
    shared.slot.set(None);
}

fn load_preset(setter: &ParamSetter, shared: &Shared, path: &std::path::Path) -> String {
    let params = &shared.params;
    let ptrs = params.ptrs();
    let name = path.file_name().map(|s| s.to_string_lossy().to_string()).unwrap_or_default();
    match std::fs::read_to_string(path).ok().and_then(|t| storage::preset_from_text(&t)) {
        Some(p) => {
            apply_values(setter, shared, &p.values);
            for i in 0..NUM_PARAMS {
                if PARAMS[i].flags & NO_RANDOM != 0 { set_norm(setter, ptrs[i], to_norm(i, p.values[i])); }
            }
            params.seed.store(p.seed, Ordering::Relaxed);
            match storage::table_from_text(&p.wavetable) {
                Some(t) => {
                    if let Ok(mut g) = params.wavetable.lock() { *g = p.wavetable.clone(); }
                    if let Ok(mut g) = params.wavetable_info.lock() { *g = p.wavetable_info.clone(); }
                    shared.slot.set(Some(Arc::new(t)));
                }
                None => clear_wavetable(shared),
            }
            format!("Loaded {name}")
        }
        None => "That preset could not be read.".into(),
    }
}

// ---- drawing helpers --------------------------------------------------------------------------------------------

/// The painter plus the transform from the 1200 x 900 design space to the screen.
struct View {
    p: egui::Painter,
    o: Pos2,
    s: f32,
    pal: Pal,
}

impl View {
    fn pt(&self, x: f32, y: f32) -> Pos2 { Pos2::new(self.o.x + x * self.s, self.o.y + y * self.s) }
    fn rect(&self, x: f32, y: f32, w: f32, h: f32) -> Rect { Rect::from_min_size(self.pt(x, y), Vec2::new(w * self.s, h * self.s)) }
    fn reg(&self, size: f32) -> FontId { FontId::new(size * self.s, FontFamily::Proportional) }
    fn bold(&self, size: f32) -> FontId { FontId::new(size * self.s, semi()) }
    fn it(&self, size: f32) -> FontId { FontId::new(size * self.s, ital()) }
    fn text(&self, x: f32, y: f32, anchor: Align2, text: &str, font: FontId, color: Color32) {
        self.p.text(self.pt(x, y), anchor, text, font, color);
    }
    /// Text running top to bottom, its right edge at design x `right`, centred on design y `cy`.
    fn vtext(&self, right: f32, cy: f32, text: &str, size: f32) {
        let galley = self.p.layout_no_wrap(text.to_string(), self.it(size), self.pal.paper);
        let w = galley.size().x;
        let pos = Pos2::new(self.o.x + right * self.s, self.o.y + cy * self.s - w / 2.0);
        self.p.add(TextShape::new(pos, galley, self.pal.paper).with_angle(FRAC_PI_2));
    }
    fn outline(&self, r: Rect, radius: f32, fill: Color32, width: f32, color: Color32) {
        self.p.rect(r, radius * self.s, fill, Stroke::new(width * self.s, color), StrokeKind::Inside);
    }
}

fn lerp_c(a: [f32; 3], b: [f32; 3], t: f32) -> Color32 {
    let t = t.clamp(0.0, 1.0);
    Color32::from_rgb(
        (a[0] + (b[0] - a[0]) * t) as u8,
        (a[1] + (b[1] - a[1]) * t) as u8,
        (a[2] + (b[2] - a[2]) * t) as u8,
    )
}

/// Wave fill colour: the skin's back ridge colour at the back, its middle colour half way, ink at the front.
fn wave_grad(p: &Pal, f: f32) -> Color32 {
    if f < 0.5 { lerp_c(p.back, p.mid, f * 2.0) } else { lerp_c(p.mid, p.front, (f - 0.5) * 2.0) }
}

fn rotate_about(x: f32, y: f32) -> (f32, f32) {
    let a = -8.0f32.to_radians();
    let (dx, dy) = (x - 600.0, y - 450.0);
    (600.0 + dx * a.cos() - dy * a.sin(), 450.0 + dx * a.sin() + dy * a.cos())
}

fn background(v: &View, full: Rect, st: &UiState) {
    v.p.rect_filled(full, 0.0, v.pal.ink);
    let p = v.p.with_clip_rect(full);

    // ridges: back to front, reaching well past every edge of the window however it is resized
    let dmin_x = (full.left() - v.o.x) / v.s;
    let dmax_x = (full.right() - v.o.x) / v.s;
    let dmax_y = (full.bottom() - v.o.y) / v.s;
    for i in 0..10usize {
        let fi = i as f32;
        let base = 250.0 + fi * 62.0;
        let amp = 16.0 + fi * 4.5;
        let n = if i % 3 == 0 { 1.0 } else { 2.0 };
        let period = 1200.0 / n;
        let px_per_beat = 1200.0 / (140.0 + 18.0 * fi);
        let off = TAU * (st.beats * px_per_beat) / period;
        let phase = fi * 1.7;
        let top_c = wave_grad(&v.pal, fi / 9.0);
        let bot_c = wave_grad(&v.pal, ((fi + 1.0) / 9.0).min(1.0));
        let mut mesh = Mesh::default();
        let mut rim: Vec<Pos2> = Vec::new();
        let (x0, x1) = (dmin_x - 500.0, dmax_x + 500.0);
        let steps = (((x1 - x0) / 26.0) as usize).max(64);
        for k in 0..=steps {
            let x = x0 + (x1 - x0) * k as f32 / steps as f32;
            let a = TAU * x / period + phase - off;
            let y = base + amp * a.sin() + 0.3 * amp * (3.0 * a + 0.7).sin();
            let (tx, ty) = rotate_about(x, y);
            let (bx, by) = rotate_about(x, (base + amp + 420.0).max(dmax_y + 700.0));
            let top = v.pt(tx, ty);
            mesh.colored_vertex(top, top_c);
            mesh.colored_vertex(v.pt(bx, by), bot_c);
            rim.push(top);
        }
        for k in 0..steps as u32 {
            let a = 2 * k;
            mesh.add_triangle(a, a + 1, a + 2);
            mesh.add_triangle(a + 2, a + 1, a + 3);
        }
        p.add(Shape::mesh(mesh));
        let alpha = ((0.55 - 0.04 * fi).max(0.05) * 255.0) as u8;
        p.add(Shape::line(rim, Stroke::new(1.6 * v.s, v.pal.mist_a(alpha))));
    }

    // glow at the upper right
    let mut glow = Mesh::default();
    let centre = Pos2::new(full.left() + full.width() * 0.86, full.top() + full.height() * 0.10);
    glow.colored_vertex(centre, v.pal.glow_a(90));
    let ring = 28u32;
    for k in 0..ring {
        let a = TAU * k as f32 / ring as f32;
        glow.colored_vertex(centre + Vec2::new(a.cos(), a.sin()) * full.width().max(full.height()) * 0.45, Color32::TRANSPARENT);
    }
    for k in 0..ring {
        glow.add_triangle(0, 1 + k, 1 + (k + 1) % ring);
    }
    p.add(Shape::mesh(glow));

    // shade on the left, behind the tree and boxes
    let mut shade = Mesh::default();
    let (l, r) = (full.left(), full.left() + full.width() * 0.38);
    shade.colored_vertex(Pos2::new(l, full.top()), Color32::from_black_alpha(150));
    shade.colored_vertex(Pos2::new(r, full.top()), Color32::TRANSPARENT);
    shade.colored_vertex(Pos2::new(r, full.bottom()), Color32::TRANSPARENT);
    shade.colored_vertex(Pos2::new(l, full.bottom()), Color32::from_black_alpha(150));
    shade.add_triangle(0, 1, 2);
    shade.add_triangle(0, 2, 3);
    p.add(Shape::mesh(shade));
}

fn ellipse_pts(cx: f32, cy: f32, rx: f32, ry: f32, n: usize, v: &View) -> Vec<Pos2> {
    (0..=n).map(|k| { let a = TAU * k as f32 / n as f32; v.pt(cx + rx * a.cos(), cy + ry * a.sin()) }).collect()
}

fn bez(a: &[Pos2; 3], t: f32) -> Pos2 {
    let u = 1.0 - t;
    Pos2::new(u * u * a[0].x + 2.0 * u * t * a[1].x + t * t * a[2].x, u * u * a[0].y + 2.0 * u * t * a[1].y + t * t * a[2].y)
}

/// The Autumn tree: every limb is stamped from square blocks on a grid (a whole number of screen pixels per block), tips and
/// the seed are whole blocks. `posed` holds the limbs after the knob lengths and the wind; `hot` is a limb to light up.
fn draw_tree(v: &View, tr: &tree::Tree, posed: &tree::Posed, m: tree::Map, grow_t: f32, clock: f32, hot: Option<usize>) {
    let ppp = v.p.pixels_per_point().max(0.5);
    let snap = |x: f32| (x * ppp).round() / ppp;
    let cell = ((AUTUMN_GAP * m.k * v.s * ppp).round() / ppp).max(1.0 / ppp);
    let to_screen = |p: (f32, f32)| {
        let q = m.pt(p);
        v.pt(q.0, q.1)
    };
    // the grid cell (column, row) holding a screen point
    let cell_of = |p: Pos2| (((p.x - v.o.x) / cell - 0.5).round(), ((p.y - v.o.y) / cell - 0.5).round());
    // a block `size` cells wide centred on a grid cell
    let block = |mesh: &mut Mesh, c: (f32, f32), size: f32, col: Color32| {
        let (cx, cy) = (v.o.x + (c.0 + 0.5) * cell, v.o.y + (c.1 + 0.5) * cell);
        let h = cell * size * 0.5;
        mesh.add_colored_rect(Rect::from_min_max(Pos2::new(snap(cx - h), snap(cy - h)), Pos2::new(snap(cx + h), snap(cy + h))), col);
    };
    let mut mesh = Mesh::default();

    for (i, b) in tr.branches.iter().enumerate() {
        let frac = b.grown(grow_t);
        if frac <= 0.0 {
            continue;
        }
        let cv = posed.curves[i];
        let a = [to_screen(cv[0]), to_screen(cv[1]), to_screen(cv[2])];
        let tier = (b.tier as usize).min(5);
        let col = if hot == Some(i) { rgb(HOT_LIMB) } else { rgb(AUTUMN_COL[tier]) };
        // a block every 0.6 cell along the part of the limb that has grown, skipping repeats of the same cell
        let steps = 96usize;
        let n = (((steps as f32) * frac).ceil() as usize).max(1);
        let (mut prev, mut acc, mut last) = (a[0], 0.0f32, (f32::NAN, f32::NAN));
        for k in 0..=n {
            let q = bez(&a, frac * k as f32 / n as f32);
            acc += (q - prev).length();
            prev = q;
            if k == 0 || acc >= cell * 0.6 {
                acc = 0.0;
                let c = cell_of(q);
                if c != last {
                    last = c;
                    block(&mut mesh, c, AUTUMN_SZ[tier], col);
                }
            }
        }
    }
    // the seed at the base: roots and trunk both start in it, so the whole tree sprouts out of it
    block(&mut mesh, cell_of(to_screen((450.0, 333.0))), 2.0, rgb(AUTUMN_SEED));
    // tips on the free ends of limbs and roots, appearing as their limb finishes; a few are lit beacons that twinkle
    for (ti, tip) in tr.tips.iter().enumerate() {
        let a = ((grow_t - tip.done - 0.05) / 0.35).clamp(0.0, 1.0);
        if a <= 0.0 {
            continue;
        }
        let p = to_screen(posed.tip(tr, ti));
        let c = cell_of(p);
        if tip.lit {
            let tw = 0.75 + 0.25 * (clock * 2.0 + tip.twinkle).sin();
            let centre = Pos2::new(v.o.x + (c.0 + 0.5) * cell, v.o.y + (c.1 + 0.5) * cell);
            v.p.circle_filled(centre, 3.3 * m.k * 2.1 * v.s, rgb(AUTUMN_HALO).gamma_multiply(a * tw * 0.3));
            block(&mut mesh, c, 1.5, rgb(AUTUMN_BEACON).gamma_multiply(a));
        } else {
            block(&mut mesh, c, 1.0, rgb(AUTUMN_TIP).gamma_multiply(a));
        }
    }
    v.p.add(Shape::mesh(mesh));
}

// ---- widgets ----------------------------------------------------------------------------------------------------

fn short_label(id: &str) -> String {
    match id {
        "sub_am" => "SUB".into(),
        "b_sh" => "S&H".into(),
        "a_freq" | "b_freq" => "FREQ".into(),
        "flt_kf" | "env_kf" => "KEY".into(),
        "adj_clip" => "CLIP".into(),
        "adj_bass" => "BASS".into(),
        "adj_treb" => "TREBLE".into(),
        "adj_pan" => "PAN".into(),
        "saturate" => "DRIVE".into(),
        "osc_mix" => "A-B".into(),
        _ => match id.split_once('_') {
            Some((_, rest)) => rest.to_uppercase(),
            None => id.to_uppercase(),
        },
    }
}

/// A circle that fills from the bottom with the value. Drag up and down, shift for fine, double-click for the default.
fn knob(v: &View, ui: &mut egui::Ui, setter: &ParamSetter, ptr: ParamPtr, key: usize, def_norm: f32, label: &str, cx: f32, top: f32, d: f32) -> bool {
    let rect = v.rect(cx - d / 2.0 - 6.0, top, d + 12.0, d + 44.0);
    let id = ui.id().with(("knob", key));
    let resp = ui.interact(rect, id, Sense::click_and_drag());
    let drag_id = id.with("drag");
    let mut norm = unsafe { ptr.unmodulated_normalized_value() };
    if resp.drag_started() {
        unsafe { setter.raw_context.raw_begin_set_parameter(ptr) };
        ui.data_mut(|d| d.insert_temp(drag_id, norm));
    }
    if resp.dragged() {
        // the value is accumulated here in full precision, not read back from the host each frame
        let fine = ui.input(|i| i.modifiers.shift);
        let rate = if fine { 0.0007 } else { 0.006 };
        let cur: f32 = ui.data(|d| d.get_temp::<f32>(drag_id)).unwrap_or(norm);
        let cur = (cur - resp.drag_delta().y / v.s * rate).clamp(0.0, 1.0);
        ui.data_mut(|d| d.insert_temp(drag_id, cur));
        norm = cur;
        unsafe { setter.raw_context.raw_set_parameter_normalized(ptr, norm) };
    }
    if resp.drag_stopped() { unsafe { setter.raw_context.raw_end_set_parameter(ptr) }; }
    if resp.double_clicked() {
        norm = def_norm;
        set_norm(setter, ptr, norm);
    }
    let active = resp.hovered() || resp.dragged();
    let c = v.pt(cx, top + d / 2.0);
    let r = d / 2.0 * v.s;
    v.p.circle_filled(c, r, v.pal.ink_a(90));
    if norm > 0.004 {
        let th0 = (1.0 - 2.0 * norm).clamp(-1.0, 1.0).asin();
        let n = 40usize;
        let a0 = th0;
        let a1 = PI - th0;
        let pts: Vec<Pos2> = (0..=n).map(|k| { let a = a0 + (a1 - a0) * k as f32 / n as f32; c + Vec2::new(a.cos(), a.sin()) * r }).collect();
        v.p.add(Shape::convex_polygon(pts, v.pal.mist_a(if active { 150 } else { 115 }), Stroke::NONE));
    }
    v.p.circle_stroke(c, r, Stroke::new(if active { 5.0 } else { 4.0 } * v.s, v.pal.paper));
    v.text(cx, top + d + 10.0, Align2::CENTER_TOP, label, v.bold(12.0), v.pal.paper);
    let shown = unsafe { ptr.normalized_value_to_string(norm, false) };
    v.text(cx, top + d + 26.0, Align2::CENTER_TOP, &shown, v.it(13.0), v.pal.paper);
    resp.dragged()
}

/// Round on/off switch drawn like a knob.
fn toggle(v: &View, ui: &mut egui::Ui, setter: &ParamSetter, ptr: ParamPtr, key: usize, label: &str, on: bool, cx: f32, top: f32, d: f32) {
    let rect = v.rect(cx - d / 2.0 - 6.0, top, d + 12.0, d + 40.0);
    let resp = ui.interact(rect, ui.id().with(("toggle", key)), Sense::click());
    if resp.clicked() { set_norm(setter, ptr, if on { 0.0 } else { 1.0 }); }
    let c = v.pt(cx, top + d / 2.0);
    let r = d / 2.0 * v.s;
    v.p.circle_filled(c, r, if on { v.pal.mist_a(150) } else { v.pal.ink_a(90) });
    v.p.circle_stroke(c, r, Stroke::new(if resp.hovered() { 5.0 } else { 4.0 } * v.s, v.pal.paper));
    v.text(cx, top + d / 2.0 - 8.0, Align2::CENTER_TOP, if on { "ON" } else { "OFF" }, v.bold(15.0), v.pal.paper);
    v.text(cx, top + d + 10.0, Align2::CENTER_TOP, label, v.bold(12.0), v.pal.paper);
}

fn pill_fill(v: &View, hover: bool) -> Color32 {
    if hover { v.pal.hover_a(120) } else { v.pal.ink_a(102) }
}

/// Selector pill with `<` `>` ends. Returns -1 or +1 when the left or right half is clicked.
fn pill_select(v: &View, ui: &mut egui::Ui, key: usize, x: f32, y: f32, w: f32, label: &str, value: &str) -> i32 {
    let rect = v.rect(x, y, w, 44.0);
    let resp = ui.interact(rect, ui.id().with(("pill", key)), Sense::click());
    v.outline(rect, 22.0, pill_fill(v, resp.hovered()), 3.0, v.pal.paper);
    v.text(x + 38.0, y + 22.0, Align2::LEFT_CENTER, label, v.bold(11.0), v.pal.paper);
    v.text(x + w / 2.0 + 10.0, y + 22.0, Align2::CENTER_CENTER, value, v.it(17.0), v.pal.paper);
    let st = Stroke::new(2.0 * v.s, v.pal.paper);
    let cy = y + 22.0;
    let chev = |cx: f32, dir: f32| {
        v.p.add(Shape::line(vec![v.pt(cx - 4.0 * dir, cy - 7.0), v.pt(cx + 4.0 * dir, cy), v.pt(cx - 4.0 * dir, cy + 7.0)], st));
    };
    chev(x + 20.0, -1.0);
    chev(x + w - 20.0, 1.0);
    if resp.clicked() {
        if let Some(pos) = resp.interact_pointer_pos() {
            return if pos.x < rect.center().x { -1 } else { 1 };
        }
    }
    0
}

fn button(v: &View, ui: &mut egui::Ui, key: usize, x: f32, y: f32, w: f32, text: &str) -> bool {
    let rect = v.rect(x, y, w, 44.0);
    let resp = ui.interact(rect, ui.id().with(("button", key)), Sense::click());
    v.outline(rect, 22.0, pill_fill(v, resp.hovered()), 3.0, v.pal.paper);
    v.text(x + w / 2.0, y + 22.0, Align2::CENTER_CENTER, text, v.bold(15.0), v.pal.paper);
    resp.clicked()
}

// ---- scopes -----------------------------------------------------------------------------------------------------

#[derive(Clone, Copy)]
enum Scope {
    OscA,
    OscB,
    Sat,
    Rvb,
    Env,
    Vol,
    Mod,
    Lfo,
    Filter,
    FmMix,
}

fn draw_scope(v: &View, params: &crate::params_gen::SeedParams, kind: Scope, fx: f32, fy: f32, beats: f32) {
    let (x, y, w, h) = (fx + 24.0, fy + 20.0, 268.0, 64.0);
    v.outline(v.rect(x, y, w, h), 12.0, v.pal.ink_a(80), 1.5, Color32::from_white_alpha(110));
    let ptrs = params.ptrs();
    let val = |i: usize| unsafe { ptrs[i].unmodulated_normalized_value() };
    let mid = y + h / 2.0;
    let amp = h / 2.0 - 8.0;
    let base = y + h - 8.0;
    let n = 134usize;
    let sx = |k: usize| x + 8.0 + (w - 16.0) * k as f32 / n as f32;
    let frac = |k: usize| k as f32 / n as f32;
    let strong = Stroke::new(2.0 * v.s, v.pal.paper);
    let faint = Stroke::new(1.0 * v.s, Color32::from_white_alpha(70));
    let scroll = beats / 8.0;
    // a curve over 0..1 drawn from the baseline up, with a marker sweeping along it in time with the tempo
    let unipolar = |f: &dyn Fn(f32) -> f32, marker: bool| {
        let pts: Vec<Pos2> = (0..=n).map(|k| v.pt(sx(k), base - 2.0 * amp * f(frac(k)))).collect();
        v.p.add(Shape::line(pts, strong));
        if marker {
            let m = scroll.fract();
            v.p.circle_filled(v.pt(x + 8.0 + (w - 16.0) * m, base - 2.0 * amp * f(m)), 3.5 * v.s, v.pal.paper);
        }
    };
    match kind {
        Scope::OscA | Scope::OscB => {
            let form = val(if matches!(kind, Scope::OscA) { idx::A_FORM } else { idx::B_FORM });
            let pts: Vec<Pos2> = (0..=n).map(|k| v.pt(sx(k), mid - amp * scope::osc_y(2.0 * frac(k) - scroll * 2.0, form))).collect();
            v.p.add(Shape::line(pts, strong));
        }
        Scope::Sat => {
            // left and right channels: the pan control shifts level between them
            let (drive, clip) = (val(idx::SATURATE), val(idx::ADJ_CLIP));
            let pan = val(idx::ADJ_PAN) * 2.0 - 1.0;
            let (gl, gr) = ((1.0 - pan).min(1.0), (1.0 + pan).min(1.0));
            let (mut inp, mut left, mut right) = (Vec::new(), Vec::new(), Vec::new());
            for k in 0..=n {
                let (out, i) = scope::sat_y(2.0 * frac(k) - scroll * 2.0, drive, clip);
                inp.push(v.pt(sx(k), mid - amp * i));
                left.push(v.pt(sx(k), mid - amp * out * gl));
                right.push(v.pt(sx(k), mid - amp * out * gr));
            }
            v.p.add(Shape::line(inp, faint));
            v.p.add(Shape::line(left, strong));
            v.p.extend(Shape::dashed_line(&right, Stroke::new(1.8 * v.s, v.pal.mist), 5.0 * v.s, 3.0 * v.s));
            v.text(x + 14.0, y + 5.0, Align2::LEFT_TOP, "L", v.bold(10.0), v.pal.paper);
            v.text(x + w - 14.0, y + 5.0, Align2::RIGHT_TOP, "R", v.bold(10.0), v.pal.mist);
        }
        Scope::Rvb => {
            let (len, size, atk, damp) = (val(idx::RVB_LEN), val(idx::RVB_SIZE), val(idx::RVB_ATK), val(idx::RVB_DAMP));
            let env: Vec<Pos2> = (0..=n).map(|k| v.pt(sx(k), mid - amp * scope::rvb_y(k as u32, len, size, atk, damp).1)).collect();
            v.p.add(Shape::line(env, faint));
            for k in 0..=n {
                let (tail, _) = scope::rvb_y(k as u32, len, size, atk, damp);
                v.p.add(Shape::line_segment([v.pt(sx(k), mid - amp * tail), v.pt(sx(k), mid + amp * tail)], Stroke::new(1.6 * v.s, v.pal.paper)));
            }
        }
        Scope::Env => {
            let (t, l, ti) = (val(idx::ENV_TIME), val(idx::ENV_LOOP), val(idx::ENV_TILT));
            unipolar(&|u| scope::env_y(u, t, l, ti), true);
        }
        Scope::Vol => {
            let (a, d, s, f) = (val(idx::VOL_ATK), val(idx::VOL_DCY), val(idx::VOL_SUS), val(idx::VOL_FADE));
            unipolar(&|u| scope::adsr_y(u, a, d, s, f), true);
        }
        Scope::Mod => {
            let (a, d, sh, ve) = (val(idx::MOD_ATK), val(idx::MOD_DCY), val(idx::MOD_SH), val(idx::MOD_VEL));
            unipolar(&|u| scope::mod_y(u, a, d, sh, ve), true);
        }
        Scope::Lfo => {
            let (r, a, d) = (val(idx::LFO_RATE), val(idx::LFO_AMT), val(idx::LFO_DLY));
            v.p.add(Shape::line_segment([v.pt(x + 8.0, mid), v.pt(x + w - 8.0, mid)], faint));
            let pts: Vec<Pos2> = (0..=n).map(|k| v.pt(sx(k), mid - amp * scope::lfo_y(frac(k), r, a, d))).collect();
            v.p.add(Shape::line(pts, strong));
        }
        Scope::Filter => {
            let (t, c, q) = (val(idx::FLT_TYPE), val(idx::FLT_FREQ), val(idx::FLT_Q));
            let fill: Vec<Pos2> = (0..=n).map(|k| v.pt(sx(k), base - 2.0 * amp * scope::filter_y(frac(k), t, c, q))).collect();
            v.p.add(Shape::line(fill, strong));
            v.p.add(Shape::line_segment([v.pt(x + 8.0 + (w - 16.0) * c, y + 6.0), v.pt(x + 8.0 + (w - 16.0) * c, y + h - 6.0)], faint));
        }
        Scope::FmMix => {
            // oscillator A phase-modulated by B, crossfaded with B by the mix control
            let (fa, fb, mix, fm) = (val(idx::A_FORM), val(idx::B_FORM), val(idx::OSC_MIX), val(idx::FM_AMT));
            let pts: Vec<Pos2> = (0..=n)
                .map(|k| {
                    let t = 2.0 * frac(k) - scroll * 2.0;
                    let b = scope::osc_y(t * 3.0, fb);
                    let a = scope::osc_y(t + 0.25 * fm * b, fa);
                    v.pt(sx(k), mid - amp * (a * (1.0 - mix) + b * mix))
                })
                .collect();
            v.p.add(Shape::line(pts, strong));
        }
    }
}

// ---- layout -----------------------------------------------------------------------------------------------------

#[allow(dead_code)]
#[derive(Clone, Copy)]
enum Kind {
    /// four big knobs
    L,
    /// six medium knobs
    S,
    /// scope on top, up to six small knobs
    W(Scope),
    /// two big knobs and a reverb type pill
    T,
    /// glide, mono, source pill (and wavetable pill)
    V,
}

struct Panel {
    x: f32,
    y: f32,
    label: &'static str,
    kind: Kind,
    knobs: &'static [usize],
}

fn panels(view: usize) -> Vec<Panel> {
    let (tl, tr, bl, br) = ((360.0, 40.0), (764.0, 40.0), (360.0, 422.0), (764.0, 422.0));
    fn p(pos: (f32, f32), label: &'static str, kind: Kind, knobs: &'static [usize]) -> Panel {
        Panel { x: pos.0, y: pos.1, label, kind, knobs }
    }
    match view {
        0 => vec![
            p(tl, "Envelope", Kind::W(Scope::Env), &[idx::ENV_TIME, idx::ENV_LOOP, idx::ENV_TILT, idx::ENV_KF]),
            p(tr, "Volume", Kind::W(Scope::Vol), &[idx::VOL_ATK, idx::VOL_DCY, idx::VOL_SUS, idx::VOL_FADE]),
            p(bl, "Modulation", Kind::W(Scope::Mod), &[idx::MOD_ATK, idx::MOD_DCY, idx::MOD_SH, idx::MOD_VEL]),
            p(br, "LFO", Kind::W(Scope::Lfo), &[idx::LFO_RATE, idx::LFO_AMT, idx::LFO_BAL, idx::LFO_DLY]),
        ],
        1 => vec![
            p(tl, "Oscillator A", Kind::W(Scope::OscA), &[idx::A_FORM, idx::A_NOISE, idx::A_MOD, idx::A_COLOR, idx::A_FREQ]),
            p(tr, "Oscillator B", Kind::W(Scope::OscB), &[idx::B_FORM, idx::B_NOISE, idx::B_MOD, idx::SUB_AM, idx::B_FREQ, idx::B_SH]),
            p(bl, "FM & Mix", Kind::W(Scope::FmMix), &[idx::FM_MOD, idx::FM_AMT, idx::MIX_MOD, idx::OSC_MIX]),
            p(br, "Voice", Kind::V, &[]),
        ],
        _ => vec![
            p(tl, "Filter", Kind::W(Scope::Filter), &[idx::FLT_TYPE, idx::FLT_FREQ, idx::FLT_Q, idx::FLT_MOD, idx::FLT_SEP, idx::FLT_KF]),
            p(tr, "Drive & Tone", Kind::W(Scope::Sat), &[idx::SATURATE, idx::ADJ_CLIP, idx::ADJ_BASS, idx::ADJ_TREB, idx::ADJ_PAN]),
            p(bl, "Reverb", Kind::W(Scope::Rvb), &[idx::RVB_MIX, idx::RVB_ATK, idx::RVB_LEN, idx::RVB_SIZE]),
            p(br, "Tail", Kind::T, &[idx::RVB_DAMP, idx::RVB_CHOR]),
        ],
    }
}

#[allow(dead_code)]
const L_SLOTS: [(f32, f32); 4] = [(90.0, 28.0), (234.0, 28.0), (90.0, 190.0), (234.0, 190.0)];
#[allow(dead_code)]
const S_SLOTS: [(f32, f32); 6] = [(62.0, 26.0), (156.0, 26.0), (250.0, 26.0), (62.0, 150.0), (156.0, 150.0), (250.0, 150.0)];
const W_SLOTS: [(f32, f32); 6] = [(62.0, 96.0), (156.0, 96.0), (250.0, 96.0), (62.0, 220.0), (156.0, 220.0), (250.0, 220.0)];
const T_SLOTS: [(f32, f32); 2] = [(90.0, 40.0), (234.0, 40.0)];

/// Draws one panel. Returns the parameter whose knob is being dragged, if any.
fn draw_panel(v: &View, ui: &mut egui::Ui, setter: &ParamSetter, shared: &Shared, st: &mut UiState, pn: &Panel, key0: usize) -> Option<usize> {
    let mut dragged: Option<usize> = None;
    let params = &shared.params;
    let ptrs = params.ptrs();
    let frame = v.rect(pn.x, pn.y, 380.0, 358.0);
    v.outline(frame, 28.0, v.pal.ink_a(102), 4.0, v.pal.paper);
    v.vtext(pn.x + 380.0 - 20.0, pn.y + 179.0, pn.label, 30.0);

    let (slots, d): (&[(f32, f32)], f32) = match pn.kind {
        Kind::L => (&L_SLOTS, 118.0),
        Kind::S => (&S_SLOTS, 84.0),
        Kind::W(_) => (&W_SLOTS, 72.0),
        Kind::T => (&T_SLOTS, 118.0),
        Kind::V => (&[], 118.0),
    };
    if let Kind::W(sc) = pn.kind {
        draw_scope(v, params, sc, pn.x, pn.y, st.beats);
    }
    for (n, &i) in pn.knobs.iter().enumerate() {
        let Some(&(cx, top)) = slots.get(n) else { break };
        if knob(v, ui, setter, ptrs[i], key0 + n, to_norm(i, PARAMS[i].def), &short_label(PARAMS[i].id), pn.x + cx, pn.y + top, d) {
            dragged = Some(i);
        }
    }
    match pn.kind {
        Kind::T => {
            let cur = (unsafe { ptrs[idx::RVB_TYPE].unmodulated_normalized_value() } * (PARAMS[idx::RVB_TYPE].hi - PARAMS[idx::RVB_TYPE].lo)).round() as i32;
            let n = REVERB_NAMES.len() as i32;
            let step = pill_select(v, ui, key0 + 20, pn.x + 24.0, pn.y + 240.0, 268.0, "REVERB", REVERB_NAMES[cur.clamp(0, n - 1) as usize]);
            if step != 0 {
                set_norm(setter, ptrs[idx::RVB_TYPE], to_norm(idx::RVB_TYPE, ((cur + step + n) % n) as f32));
            }
        }
        Kind::V => {
            if knob(v, ui, setter, ptrs[idx::GLIDE], key0, to_norm(idx::GLIDE, PARAMS[idx::GLIDE].def), "GLIDE", pn.x + 90.0, pn.y + 40.0, 118.0) {
                dragged = Some(idx::GLIDE);
            }
            toggle(v, ui, setter, ptrs[idx::MONO], key0 + 1, "MONO", params.mono.value(), pn.x + 234.0, pn.y + 40.0, 118.0);
            let cur = (unsafe { ptrs[idx::SRC_TYPE].unmodulated_normalized_value() } * (PARAMS[idx::SRC_TYPE].hi - PARAMS[idx::SRC_TYPE].lo)).round() as i32;
            let n = SOURCE_NAMES.len() as i32;
            let step = pill_select(v, ui, key0 + 20, pn.x + 24.0, pn.y + 240.0, 268.0, "SOURCE", SOURCE_NAMES[cur.clamp(0, n - 1) as usize]);
            if step != 0 {
                set_norm(setter, ptrs[idx::SRC_TYPE], to_norm(idx::SRC_TYPE, ((cur + step + n) % n) as f32));
            }
            if params.src_type.value() == 1 {
                let r = v.rect(pn.x + 24.0, pn.y + 296.0, 268.0, 44.0);
                let resp = ui.interact(r, ui.id().with(("wt", key0)), Sense::click());
                v.outline(r, 22.0, pill_fill(v, resp.hovered()), 3.0, v.pal.paper);
                v.text(pn.x + 158.0, pn.y + 318.0, Align2::CENTER_CENTER, "Wavetable...", v.it(17.0), v.pal.paper);
                if resp.clicked() { st.popup = Popup::Wavetables; }
            }
        }
        _ => {}
    }
    dragged
}

// ---- the window -------------------------------------------------------------------------------------------------

fn draw(ui: &mut egui::Ui, shared: &Shared, setter: &ParamSetter, st: &mut UiState) {
    let params = &shared.params;
    let seed = params.seed.load(Ordering::Relaxed);

    // clocks
    let dt = ui.input(|i| i.stable_dt).clamp(0.0, 0.1);
    let bpm = f32::from_bits(shared.tempo.load(Ordering::Relaxed)).clamp(40.0, 300.0);
    st.clock += dt;
    st.grow_t += dt;
    st.beats += dt * bpm / 60.0;
    ui.ctx().request_repaint_after(std::time::Duration::from_millis(16));

    // wind: the tree sways with the loudness of what is being played, rising quickly and settling slowly when it stops
    let bits = shared.level.load(Ordering::Relaxed);
    if bits != st.level_bits {
        st.level_bits = bits;
        st.level_age = 0.0;
    } else {
        st.level_age += dt;
    }
    let level = if st.level_age > 1.0 { 0.0 } else { f32::from_bits(bits) };
    let want_wind = if level.is_finite() { (level * 3.0).clamp(0.0, 1.0).powf(0.6) } else { 0.0 };
    let wind_rate = if want_wind > st.wind { 6.0 } else { 1.2 };
    st.wind += (want_wind - st.wind) * (1.0 - (-dt * wind_rate).exp());
    if st.wind < 0.003 && want_wind == 0.0 {
        st.wind = 0.0;
    }
    st.wind_phase = (st.wind_phase + dt * (2.5 + 2.5 * st.wind)) % (TAU * 1000.0);

    // knob lengths: a knob's limb grows when the knob is turned up from its default and shrinks when turned down
    let ptrs = params.ptrs();
    let ease = if st.lens_ready { 1.0 - (-dt * 14.0).exp() } else { 1.0 };
    for n in 0..st.knobs.len() {
        let i = st.knobs[n];
        let norm = unsafe { ptrs[i].unmodulated_normalized_value() };
        let want = (1.0 + 1.4 * (norm - to_norm(i, PARAMS[i].def))).clamp(0.35, 1.9);
        st.lens[n] += (want - st.lens[n]) * ease;
    }
    st.lens_ready = true;
    let posed = st.tree.pose(&tree::Pose { lens: &st.lens, wind: st.wind, phase: st.wind_phase });
    let hot = st.hot.and_then(|p| st.knobs.iter().position(|&i| i == p)).and_then(|n| st.tree.own_branch.get(n).copied());

    let full = ui.max_rect();
    let s = (full.width() / W).min(full.height() / H).max(0.2);
    let o = full.center() - Vec2::new(W, H) * s / 2.0;
    let v = View { p: ui.painter().clone(), o, s, pal: st.pal };

    background(&v, full, st);

    // Rule: only one tree on screen at a time. Edit pages show the tree on the left running through the three page
    // boxes; Home shows nothing but the big centred tree.
    if st.view != 3 {
        let titles = ["I · Envelope & LFO", "II · Oscillators", "III · Filter & effects"];
        for (n, title) in titles.iter().enumerate() {
            let y = 40.0 + 249.0 * n as f32;
            let r = v.rect(36.0, y, 300.0, 241.0);
            let resp = ui.interact(r, ui.id().with(("page", n)), Sense::click());
            let active = st.view == n;
            let fill = if active { Color32::from_white_alpha(22) } else if resp.hovered() { Color32::from_white_alpha(16) } else { Color32::TRANSPARENT };
            v.outline(r, 28.0, fill, 4.0, v.pal.paper);
            let (num, name) = title.split_once(" · ").unwrap_or(("", title));
            v.text(320.0, y + 16.0, Align2::RIGHT_TOP, num, v.bold(14.0), v.pal.paper);
            v.text(320.0, y + 36.0, Align2::RIGHT_TOP, name, v.it(20.0), v.pal.paper);
            if resp.clicked() {
                st.view = n;
                st.last_edit = n;
            }
        }
        draw_tree(&v, &st.tree, &posed, tree::LEFT, st.grow_t, st.clock, hot);
    }

    if st.view == 3 {
        // home: the big tree inside an orbit that beats with the host tempo
        let (cx, cy) = (600.0, 565.0);
        let ring = ellipse_pts(cx, cy, 300.0, 46.0, 96, &v);
        v.p.add(Shape::line(ring.clone(), Stroke::new(1.0 * v.s, Color32::from_white_alpha(76))));
        v.p.extend(Shape::dashed_line(&ring, Stroke::new(3.0 * v.s, Color32::from_white_alpha(180)), 1.0 * v.s, 30.0 * v.s));
        let ang = st.beats / 32.0 * TAU;
        v.p.circle_filled(v.pt(cx + 300.0 * ang.cos(), cy + 46.0 * ang.sin()), 5.0 * v.s, v.pal.paper);
        draw_tree(&v, &st.tree, &posed, tree::HERO, st.grow_t, st.clock, hot);

        // patch name, bands, status
        let name_rect = v.rect(300.0, 56.0, 600.0, 40.0);
        ui.put(
            name_rect,
            egui::TextEdit::singleline(&mut st.preset_name)
                .frame(false)
                .font(v.it(30.0))
                .text_color(v.pal.paper)
                .horizontal_align(Align::Center)
                .desired_width(600.0 * v.s),
        );
        let status = if st.status.is_empty() { seed_title(seed) } else { st.status.clone() };
        v.text(600.0, 744.0, Align2::CENTER_TOP, &status, v.it(14.0), v.pal.paper);

        // randomness and tuning
        let chaos_def = 0.5;
        knob(&v, ui, setter, params.chaos.as_ptr(), 900, chaos_def, "SOFT · WILD", 1040.0, 90.0, 84.0);
        toggle(&v, ui, setter, params.auto_tune.as_ptr(), 901, "AUTO TUNE", params.auto_tune.value(), 1040.0, 250.0, 84.0);

        // skin: step through the built-in and folder skins, then the palette grown from the patch seed
        let skin_label = st.skin_name();
        let step = pill_select(&v, ui, 30, 36.0, 40.0, 250.0, "SKIN", &skin_label);
        if step != 0 {
            let count = st.skins.len() as i32 + 1;
            st.skin_ix = (st.skin_ix as i32 + step).rem_euclid(count) as usize;
            let name = st.skin_name();
            if let Ok(mut g) = params.skin.lock() {
                *g = name;
            }
        }
        st.hot = None;
    } else {
        let list = panels(st.view);
        let mut dragged = None;
        for (n, pn) in list.iter().enumerate() {
            if let Some(i) = draw_panel(&v, ui, setter, shared, st, pn, 100 * (n + 1)) {
                dragged = Some(i);
            }
        }
        st.hot = dragged;
    }

    // bottom row: seed, buttons, navigation
    let seed_rect = v.rect(36.0, 806.0, 190.0, 44.0);
    v.outline(seed_rect, 22.0, v.pal.ink_a(102), 3.0, v.pal.paper);
    v.text(58.0, 828.0, Align2::LEFT_CENTER, "SEED", v.bold(11.0), v.pal.paper);
    let seed_edit = ui.put(
        v.rect(104.0, 812.0, 104.0, 32.0),
        egui::TextEdit::singleline(&mut st.seed_text)
            .frame(false)
            .font(v.it(18.0))
            .text_color(v.pal.paper)
            .hint_text(egui::RichText::new(seed.to_string()).color(Color32::from_white_alpha(170)))
            .horizontal_align(Align::Center)
            .desired_width(104.0 * v.s),
    );
    if seed_edit.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter)) {
        match st.seed_text.trim().parse::<u32>() {
            Ok(n) => {
                st.status = grow(setter, shared, n);
                st.grow_t = 0.0;
                st.seed_text.clear();
            }
            Err(_) => st.status = "Type a whole number as the seed.".into(),
        }
    }
    if button(&v, ui, 1, 262.0, 806.0, 150.0, "Randomize") {
        let n = FIRST_ARCHETYPE_SEED + (st.rng.next_u64() % SEED_RANGE as u64) as u32;
        st.status = grow(setter, shared, n);
        st.grow_t = 0.0;
    }
    if button(&v, ui, 2, 448.0, 806.0, 150.0, "Correct tune") {
        let mut vals = params.values();
        correct_tune(&mut vals);
        apply_values(setter, shared, &vals);
        st.status = "Tuned the oscillators to the keys.".into();
    }
    if button(&v, ui, 3, 634.0, 806.0, 150.0, "Save") {
        let preset = storage::Preset {
            values: params.values().to_vec(),
            seed,
            wavetable: params.wavetable.lock().map(|s| s.clone()).unwrap_or_default(),
            wavetable_info: params.wavetable_info.lock().map(|s| s.clone()).unwrap_or_default(),
        };
        let path = storage::preset_dir().join(format!("{}.seedling", storage::safe_file_stem(&st.preset_name)));
        st.status = match std::fs::write(&path, storage::preset_to_text(&preset)) {
            Ok(()) => format!("Saved {}", path.display()),
            Err(e) => format!("Could not save: {e}"),
        };
    }
    if button(&v, ui, 4, 820.0, 806.0, 150.0, "Load") {
        st.popup = Popup::Presets;
    }
    let (nav_label, nav_to) = if st.view == 3 { ("Edit sound", st.last_edit) } else { ("Home", 3) };
    if button(&v, ui, 5, 1006.0, 806.0, 150.0, nav_label) {
        st.view = nav_to;
    }

    popups(ui, shared, setter, st);
}

fn popups(ui: &mut egui::Ui, shared: &Shared, setter: &ParamSetter, st: &mut UiState) {
    if st.popup == Popup::None {
        return;
    }
    let ctx = ui.ctx().clone();
    let mut close = false;
    let mut picked: Option<std::path::PathBuf> = None;
    let mut builtin = false;
    let mut open_folder = false;
    let wavetables = st.popup == Popup::Wavetables;
    let (dir, ext, title) = if wavetables { (storage::wavetable_dir(), "wav", "Wavetables") } else { (storage::preset_dir(), "seedling", "Presets") };
    egui::Window::new(title).collapsible(false).resizable(false).anchor(Align2::CENTER_CENTER, Vec2::ZERO).show(&ctx, |ui| {
        let files = storage::list_files(&dir, ext);
        if files.is_empty() {
            ui.label(if wavetables { "Put WAV files in Documents/Seedling/Wavetables" } else { "(no presets saved yet)" });
        }
        egui::ScrollArea::vertical().max_height(320.0).show(ui, |ui| {
            for f in files {
                let name = f.file_stem().map(|s| s.to_string_lossy().to_string()).unwrap_or_default();
                if ui.button(name).clicked() {
                    picked = Some(f);
                }
            }
        });
        ui.separator();
        ui.horizontal(|ui| {
            if ui.button("Open folder").clicked() { open_folder = true; }
            if wavetables && ui.button("Use built-in wavetable").clicked() { builtin = true; }
            if ui.button("Close").clicked() { close = true; }
        });
        if wavetables {
            let info = shared.params.wavetable_info.lock().map(|s| s.clone()).unwrap_or_default();
            ui.label(egui::RichText::new(if info.is_empty() { "Built-in wavetable. Sampled sounds are pitch-detected and tuned to the keys.".to_string() } else { info }).size(12.0));
        }
    });
    if open_folder { storage::open_folder(&dir); }
    if builtin {
        clear_wavetable(shared);
        st.status = "Using the built-in wavetable.".into();
        close = true;
    }
    if let Some(f) = picked {
        st.status = if wavetables { load_wavetable(shared, &f) } else { load_preset(setter, shared, &f) };
        if !wavetables { st.grow_t = 0.0; }
        close = true;
    }
    if close { st.popup = Popup::None; }
}
