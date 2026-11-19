//! The egui editor: three pages of knobs (I. Envelope and LFO, II. Oscillators, III. Filter and effects), Randomize, seed entry,
//! Correct Tune, presets and the wavetable menu.

use crate::skins::{self, Skin, SEED_COLOURS};
use crate::{storage, Shared};
use nih_plug::prelude::*;
use nih_plug_egui::{create_egui_editor, egui, resizable_window::ResizableWindow, EguiState};
use seedling_dsp::archetypes::{name_for_seed, FIRST_ARCHETYPE_SEED, SEED_RANGE};
use seedling_dsp::params::*;
use seedling_dsp::randomizer::{correct_tune, random_values, SplitMix};
use seedling_dsp::wavetable::import_wav_bytes;
use std::sync::atomic::Ordering;
use std::sync::Arc;

use egui::{Align2, Color32, FontId, Pos2, Rect, Sense, Shape, Stroke, Vec2};

/// Colours of the current skin, worked out once per frame.
#[derive(Clone, Copy)]
struct Theme {
    bg: Color32,
    panel: Color32,
    track: Color32,
    accent: Color32,
    /// body text (mist)
    text: Color32,
    /// secondary text
    dim: Color32,
    /// headings and highlights (paper)
    bright: Color32,
}

fn c32(c: skins::Rgb) -> Color32 { Color32::from_rgb(c[0], c[1], c[2]) }

impl Theme {
    fn from_skin(s: &Skin) -> Self {
        Theme {
            bg: c32(s.ink),
            panel: c32(skins::mix(s.ink, s.slate, 0.28)),
            track: c32(skins::mix(s.ink, s.slate, 0.55)),
            accent: c32(s.accent),
            text: c32(s.mist),
            dim: c32(skins::mix(s.mist, s.ink, 0.35)),
            bright: c32(s.paper),
        }
    }

    fn visuals(&self) -> egui::Visuals {
        let mut v = egui::Visuals::dark();
        v.panel_fill = self.bg;
        v.window_fill = self.panel;
        v.override_text_color = Some(self.text);
        v.widgets.inactive.bg_fill = self.panel;
        v.widgets.inactive.weak_bg_fill = self.panel;
        v.widgets.hovered.weak_bg_fill = self.track;
        v.widgets.hovered.bg_fill = self.track;
        v.widgets.active.weak_bg_fill = self.accent;
        v.widgets.active.bg_fill = self.accent;
        v.selection.bg_fill = self.accent;
        v
    }
}

pub struct UiState {
    page: usize,
    seed_text: String,
    preset_name: String,
    status: String,
    rng: SplitMix,
    skins: Vec<Skin>,
    /// the colours on screen right now (glides towards the chosen skin)
    shown: Skin,
}

pub fn create(shared: Arc<Shared>) -> Option<Box<dyn Editor>> {
    let egui_state: Arc<EguiState> = shared.params.editor_state.clone();
    let nanos = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_nanos() as u64).unwrap_or(1);
    let (skin_list, _) = skins::load_all();
    let state = UiState {
        shown: skins::from_seed(shared.params.seed.load(Ordering::Relaxed)),
        skins: skin_list,
        page: 0,
        seed_text: String::new(),
        preset_name: String::from("My patch"),
        status: String::from("Press Randomize to grow a new sound."),
        rng: SplitMix::new(nanos ^ 0x9E37_79B9_7F4A_7C15),
    };
    let window_state = egui_state.clone();
    create_egui_editor(
        egui_state,
        state,
        |_ctx, _| {},
        move |ctx, setter, state| {
            let th = advance_theme(ctx, &shared, state);
            ctx.set_visuals(th.visuals());
            ResizableWindow::new("seedling-window").min_size(Vec2::new(640.0, 480.0)).show(ctx, window_state.as_ref(), |ui| {
                draw(ui, &shared, setter, state, &th);
            });
        },
    )
}

fn chosen_skin(shared: &Shared, st: &UiState) -> Skin {
    let seed = shared.params.seed.load(Ordering::Relaxed);
    let name = shared.params.skin.lock().map(|s| s.clone()).unwrap_or_default();
    if name.is_empty() || name == SEED_COLOURS {
        return skins::from_seed(seed);
    }
    st.skins.iter().find(|s| s.name == name).cloned().unwrap_or_else(|| skins::from_seed(seed))
}

/// Glides the colours on screen towards the chosen skin (a new seed's colours arrive over about half a second).
fn advance_theme(ctx: &egui::Context, shared: &Shared, st: &mut UiState) -> Theme {
    let target = chosen_skin(shared, st);
    if st.shown != target {
        let next = skins::mix_skin(&st.shown, &target, 0.14);
        let near = |a: skins::Rgb, b: skins::Rgb| (0..3).all(|i| (a[i] as i32 - b[i] as i32).abs() <= 1);
        let done = near(next.ink, target.ink) && near(next.accent, target.accent) && near(next.slate, target.slate) && near(next.mist, target.mist) && near(next.paper, target.paper);
        st.shown = if done { target } else { next };
        ctx.request_repaint();
    }
    Theme::from_skin(&st.shown)
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

// ---- widgets ----------------------------------------------------------------------------------------------------

fn arc_points(c: Pos2, r: f32, a0: f32, a1: f32) -> Vec<Pos2> {
    let n = (((a1 - a0).abs() / 0.12) as usize).max(2);
    (0..=n).map(|k| { let a = a0 + (a1 - a0) * k as f32 / n as f32; c + Vec2::new(a.cos(), a.sin()) * r }).collect()
}

fn knob(ui: &mut egui::Ui, setter: &ParamSetter, ptr: ParamPtr, i: usize, th: &Theme) {
    let def = &PARAMS[i];
    let (rect, _) = ui.allocate_exact_size(Vec2::new(66.0, 92.0), Sense::hover());
    let resp = ui.interact(rect, ui.id().with(("knob", i)), Sense::click_and_drag());
    let mut norm = unsafe { ptr.unmodulated_normalized_value() };

    if resp.drag_started() { unsafe { setter.raw_context.raw_begin_set_parameter(ptr) }; }
    if resp.dragged() {
        let fine = ui.input(|inp| inp.modifiers.shift);
        let dy = -resp.drag_delta().y * if fine { 0.0007 } else { 0.006 };
        norm = (norm + dy).clamp(0.0, 1.0);
        unsafe { setter.raw_context.raw_set_parameter_normalized(ptr, norm) };
    }
    if resp.drag_stopped() { unsafe { setter.raw_context.raw_end_set_parameter(ptr) }; }
    if resp.double_clicked() {
        norm = to_norm(i, def.def);
        set_norm(setter, ptr, norm);
    }

    let p = ui.painter();
    let c = Pos2::new(rect.center().x, rect.top() + 32.0);
    let (a0, a1) = (std::f32::consts::PI * 0.75, std::f32::consts::PI * 2.25);
    p.add(Shape::line(arc_points(c, 25.0, a0, a1), Stroke::new(3.0, th.track)));
    let val_a = a0 + (a1 - a0) * norm;
    let from = if def.flags & BIPOLAR != 0 { a0 + (a1 - a0) * 0.5 } else { a0 };
    let (lo_a, hi_a) = if val_a >= from { (from, val_a) } else { (val_a, from) };
    if hi_a - lo_a > 0.01 {
        p.add(Shape::line(arc_points(c, 25.0, lo_a, hi_a), Stroke::new(3.5, if resp.hovered() || resp.dragged() { th.bright } else { th.accent })));
    }
    p.circle_filled(c, 17.0, th.panel);
    p.circle_stroke(c, 17.0, Stroke::new(1.0, th.dim));
    p.line_segment([c + Vec2::new(val_a.cos(), val_a.sin()) * 6.0, c + Vec2::new(val_a.cos(), val_a.sin()) * 15.0], Stroke::new(2.0, th.bright));

    let shown = if resp.hovered() || resp.dragged() {
        unsafe { ptr.normalized_value_to_string(norm, false) }
    } else {
        String::new()
    };
    p.text(Pos2::new(rect.center().x, rect.top() + 66.0), Align2::CENTER_TOP, def.id, FontId::proportional(11.0), th.text);
    if !shown.is_empty() {
        p.text(Pos2::new(rect.center().x, rect.top() + 79.0), Align2::CENTER_TOP, shown, FontId::proportional(11.0), th.bright);
    }
    resp.on_hover_text(def.name);
}

/// `< name >` selector for a whole-number parameter.
fn step_selector(ui: &mut egui::Ui, setter: &ParamSetter, ptr: ParamPtr, i: usize, names: &[&str], th: &Theme) {
    let cur = (unsafe { ptr.unmodulated_normalized_value() } * (PARAMS[i].hi - PARAMS[i].lo)).round() as i32;
    let n = names.len() as i32;
    ui.horizontal(|ui| {
        if ui.button("<").clicked() { set_norm(setter, ptr, to_norm(i, ((cur - 1 + n) % n) as f32)); }
        ui.add_sized([130.0, 22.0], egui::Label::new(egui::RichText::new(names[cur.clamp(0, n - 1) as usize]).color(th.bright).strong()));
        if ui.button(">").clicked() { set_norm(setter, ptr, to_norm(i, ((cur + 1) % n) as f32)); }
    });
}

fn group_title(g: Group) -> &'static str {
    match g {
        Group::Env => "Envelope", Group::Vol => "Volume", Group::Mod => "Modulation", Group::Lfo => "LFO",
        Group::OscA => "Oscillator A", Group::Fm => "FM", Group::Mix => "Mix", Group::OscB => "Oscillator B",
        Group::Flt => "Filter", Group::Sat => "Saturation", Group::Rvb => "Reverb", Group::Adj => "Adjust", Group::Play => "Play",
    }
}

fn page_groups(page: usize) -> &'static [Group] {
    match page {
        0 => &[Group::Env, Group::Vol, Group::Mod, Group::Lfo],
        1 => &[Group::OscA, Group::Fm, Group::Mix, Group::OscB, Group::Play],
        _ => &[Group::Flt, Group::Sat, Group::Rvb, Group::Adj],
    }
}

// ---- the window -------------------------------------------------------------------------------------------------

fn draw(ui: &mut egui::Ui, shared: &Shared, setter: &ParamSetter, st: &mut UiState, th: &Theme) {
    let params = &shared.params;
    let ptrs = params.ptrs();
    let seed = params.seed.load(Ordering::Relaxed);

    // header
    ui.horizontal(|ui| {
        ui.label(egui::RichText::new("Seedling").size(24.0).color(th.bright).strong());
        ui.label(egui::RichText::new(seed_title(seed)).color(th.dim));
    });
    ui.horizontal_wrapped(|ui| {
        if ui.add(egui::Button::new(egui::RichText::new("Randomize").color(th.bright).strong()).fill(th.accent)).clicked() {
            let s = FIRST_ARCHETYPE_SEED + (st.rng.next_u64() % SEED_RANGE as u64) as u32;
            st.status = grow(setter, shared, s);
        }
        ui.label("seed");
        let r = ui.add(egui::TextEdit::singleline(&mut st.seed_text).desired_width(80.0).hint_text(seed.to_string()));
        let enter = r.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter));
        if ui.button("Grow").clicked() || enter {
            match st.seed_text.trim().parse::<u32>() {
                Ok(s) => st.status = grow(setter, shared, s),
                Err(_) => st.status = "Type a whole number as the seed.".into(),
            }
        }
        ui.separator();
        ui.label("soft");
        let mut chaos = params.chaos.value();
        if ui.add(egui::Slider::new(&mut chaos, 0.0..=1.0).show_value(false)).changed() {
            set_norm(setter, params.chaos.as_ptr(), chaos);
        }
        ui.label("wild");
        let mut tune = params.auto_tune.value();
        if ui.checkbox(&mut tune, "Auto tune").changed() {
            set_norm(setter, params.auto_tune.as_ptr(), if tune { 1.0 } else { 0.0 });
        }
        if ui.button("Correct Tune").on_hover_text("Re-tune the oscillators so every key plays in tune").clicked() {
            let mut v = params.values();
            correct_tune(&mut v);
            apply_values(setter, shared, &v);
            st.status = "Tuned the oscillators to the keys.".into();
        }
    });

    // presets
    ui.horizontal_wrapped(|ui| {
        ui.menu_button("Presets", |ui| {
            let files = storage::list_files(&storage::preset_dir(), "seedling");
            if files.is_empty() { ui.label("(no presets saved yet)"); }
            for f in files {
                let name = f.file_stem().map(|s| s.to_string_lossy().to_string()).unwrap_or_default();
                if ui.button(name).clicked() {
                    match std::fs::read_to_string(&f).ok().and_then(|t| storage::preset_from_text(&t)) {
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
                            st.status = format!("Loaded {}", f.file_name().map(|s| s.to_string_lossy().to_string()).unwrap_or_default());
                        }
                        None => st.status = "That preset could not be read.".into(),
                    }
                }
            }
            ui.separator();
            if ui.button("Open presets folder").clicked() { storage::open_folder(&storage::preset_dir()); }
        });
        ui.menu_button("Skin", |ui| {
            let current = params.skin.lock().map(|s| s.clone()).unwrap_or_default();
            let current = if current.is_empty() { SEED_COLOURS.to_string() } else { current };
            let mut pick: Option<String> = None;
            if ui.selectable_label(current == SEED_COLOURS, SEED_COLOURS).on_hover_text("Colours that follow the patch seed").clicked() { pick = Some(SEED_COLOURS.into()); }
            for s in &st.skins {
                if ui.selectable_label(current == s.name, &s.name).clicked() { pick = Some(s.name.clone()); }
            }
            ui.separator();
            if ui.button("Reload skins").clicked() {
                let (list, problems) = skins::load_all();
                st.skins = list;
                st.status = if problems.is_empty() { format!("{} skins loaded.", st.skins.len()) } else { format!("Skin problem: {}", problems.join("; ")) };
            }
            if ui.button("Open skins folder").clicked() { storage::open_folder(&skins::skins_dir()); }
            if let Some(name) = pick {
                if let Ok(mut g) = params.skin.lock() { *g = name; }
            }
        });
        ui.add(egui::TextEdit::singleline(&mut st.preset_name).desired_width(140.0));
        if ui.button("Save").clicked() {
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
    });

    ui.separator();
    ui.horizontal(|ui| {
        for (p, title) in PAGE_TITLES.iter().enumerate() {
            let label = format!("{}  {}", ["I", "II", "III"][p], title);
            if ui.selectable_label(st.page == p, egui::RichText::new(label).size(15.0)).clicked() {
                st.page = p;
            }
        }
    });
    ui.add_space(4.0);

    egui::ScrollArea::vertical().auto_shrink([false, false]).show(ui, |ui| {
        for &g in page_groups(st.page) {
            ui.group(|ui| {
                ui.label(egui::RichText::new(group_title(g)).color(th.dim).size(12.0));
                ui.horizontal_wrapped(|ui| {
                    for (i, def) in PARAMS.iter().enumerate() {
                        if def.group == g && def.page as usize == st.page && def.flags & (SWITCH | STEP) == 0 {
                            knob(ui, setter, ptrs[i], i, th);
                        }
                    }
                });
                match g {
                    Group::OscA => {
                        ui.horizontal(|ui| {
                            ui.label("Source");
                            step_selector(ui, setter, ptrs[idx::SRC_TYPE], idx::SRC_TYPE, &SOURCE_NAMES, th);
                        });
                        if params.src_type.value() == 1 {
                            wavetable_row(ui, shared, st, th);
                        }
                    }
                    Group::Play => {
                        let mut mono = params.mono.value();
                        if ui.checkbox(&mut mono, "Mono (legato glide)").changed() {
                            set_norm(setter, ptrs[idx::MONO], if mono { 1.0 } else { 0.0 });
                        }
                    }
                    Group::Rvb => {
                        ui.horizontal(|ui| {
                            ui.label("Type");
                            step_selector(ui, setter, ptrs[idx::RVB_TYPE], idx::RVB_TYPE, &REVERB_NAMES, th);
                        });
                    }
                    _ => {}
                }
            });
        }
    });

    ui.add_space(2.0);
    ui.label(egui::RichText::new(&st.status).color(th.dim).size(12.0));
}

fn wavetable_row(ui: &mut egui::Ui, shared: &Shared, st: &mut UiState, th: &Theme) {
    let params = &shared.params;
    ui.horizontal_wrapped(|ui| {
        ui.menu_button("Wavetable...", |ui| {
            let files = storage::list_files(&storage::wavetable_dir(), "wav");
            if files.is_empty() { ui.label("Put WAV files in Documents/Seedling/Wavetables"); }
            for f in files {
                let name = f.file_stem().map(|s| s.to_string_lossy().to_string()).unwrap_or_default();
                if ui.button(name).clicked() {
                    st.status = load_wavetable(shared, &f);
                }
            }
            ui.separator();
            if ui.button("Open wavetable folder").clicked() { storage::open_folder(&storage::wavetable_dir()); }
            if ui.button("Use built-in wavetable").clicked() {
                clear_wavetable(shared);
                st.status = "Using the built-in wavetable.".into();
            }
        });
        let info = params.wavetable_info.lock().map(|s| s.clone()).unwrap_or_default();
        ui.label(egui::RichText::new(if info.is_empty() { "Built-in wavetable. Sampled sounds are pitch-detected and tuned to the keys.".to_string() } else { info }).color(th.dim).size(12.0));
    });
}
