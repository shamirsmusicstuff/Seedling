//! Tree geometry (std only): the branch tiers of the Seedling tree, their growth timing and the free tips where dots sit.

const ROOTS: &str = "M450 334Q468 339 485 350 M450 334Q465 347 470 366 M450 334Q446 353 453 372 M450 334Q445 354 427 364 M450 334Q432 341 416 351 M485 350Q499 349 512 352 M485 350Q498 358 503 373 M453 372Q466 379 475 390 M427 364Q427 379 424 394 M416 351Q410 365 396 372 M416 351Q401 353 387 352";
const TRUNK: &str = "M450 332Q455 297 450 262";
const L1: &str = "M450 262Q444 232 416 219 M450 262Q445 237 443 211 M450 262Q472 244 486 220";
const L2: &str = "M416 219Q406 198 384 193 M416 219Q410 199 407 178 M443 211Q421 204 416 182 M443 211Q435 193 435 173 M443 211Q454 196 461 178 M486 220Q496 201 492 180 M486 220Q505 211 519 195";
const L3: &str = "M384 193Q368 187 352 185 M384 193Q371 183 364 169 M407 178Q399 165 393 151 M407 178Q413 163 408 148 M416 182Q401 180 389 172 M416 182Q410 169 403 156 M435 173Q431 158 419 148 M435 173Q429 157 437 142 M461 178Q467 165 463 151 M461 178Q471 170 483 163 M492 180Q490 164 487 148 M492 180Q502 168 507 154 M519 195Q522 177 539 171 M519 195Q534 187 549 182";
const TW: &str = "M352 185Q339 183 327 189 M352 185Q349 172 335 168 M364 169Q353 167 343 161 M364 169Q353 164 348 154 M364 169Q363 158 366 147 M393 151Q385 141 372 139 M393 151Q396 137 388 126 M408 148Q403 136 394 128 M408 148Q406 136 406 124 M408 148Q419 142 423 131 M389 172Q378 174 367 174 M389 172Q382 164 373 158 M403 156Q392 153 383 146 M403 156Q396 148 393 137 M403 156Q405 146 412 137 M419 148Q410 140 398 142 M419 148Q407 143 405 130 M419 148Q424 138 421 127 M437 142Q424 138 420 124 M437 142Q444 131 439 118 M437 142Q447 136 452 125 M463 151Q464 139 455 132 M463 151Q469 141 470 130 M483 163Q479 152 486 142 M483 163Q487 152 498 149 M483 163Q492 156 502 160 M487 148Q480 137 473 127 M487 148Q483 135 492 124 M507 154Q507 141 499 132 M507 154Q513 144 516 133 M507 154Q520 156 529 147 M539 171Q550 161 547 147 M539 171Q549 161 562 161 M549 182Q556 172 563 162 M549 182Q562 178 573 184";

/// One limb or root: a quadratic bezier with its growth timing.
#[derive(Clone, Copy, Debug)]
pub struct Branch {
    pub p0: (f32, f32),
    pub c: (f32, f32),
    pub p1: (f32, f32),
    pub width: f32,
    pub opacity: f32,
    pub delay: f32,
    pub dur: f32,
    /// 0 roots, 1 trunk, 2..4 limbs, 5 twigs
    pub tier: u8,
    /// the branch whose end this one grows from (None for the trunk and the first roots, which grow from the seed)
    pub parent: Option<usize>,
    /// the knob number whose value sets this limb's length (see `OWN_COUNT`)
    pub own: Option<usize>,
}

/// A dot at the free end of a limb or root (an end no other branch starts from).
#[derive(Clone, Copy, Debug)]
pub struct Tip {
    pub at: (f32, f32),
    /// the branch that ends here (its posed end is where the dot sits when the tree sways)
    pub branch: usize,
    /// time (seconds) when the branch reaching it has finished growing
    pub done: f32,
    pub lit: bool,
    pub twinkle: f32,
}

pub struct Tree {
    pub branches: Vec<Branch>,
    pub tips: Vec<Tip>,
    /// branch index of the limb each knob owns (knob number -> branch)
    pub own_branch: Vec<usize>,
}

/// How many knobs own a limb each. Twigs first, then the third-level limbs, then the second-level ones.
pub const OWN_COUNT: usize = 49;

/// Maps tree space (the original 900-wide drawing) to window space: `x' = ox + sx (x - 450)`, `y' = oy + sy (y - 332)`.
#[derive(Clone, Copy, Debug)]
pub struct Map {
    pub ox: f32,
    pub oy: f32,
    pub sx: f32,
    pub sy: f32,
    /// line/dot size factor
    pub k: f32,
}

/// The tree beside the three page boxes (stretched tall so it runs through all of them). Window coordinates (1200 x 900 design space).
pub const LEFT: Map = Map { ox: 186.0, oy: 614.0, sx: 1.0, sy: 2.2, k: 1.7 };
/// The big tree on the home page (window coordinates).
pub const HERO: Map = Map { ox: 600.0, oy: 565.0, sx: 2.05, sy: 2.05, k: 2.1 };

impl Map {
    pub fn pt(&self, p: (f32, f32)) -> (f32, f32) {
        (self.ox + self.sx * (p.0 - 450.0), self.oy + self.sy * (p.1 - 332.0))
    }
}

fn numbers(s: &str) -> Vec<f32> {
    let mut out = Vec::new();
    let mut cur = String::new();
    for ch in s.chars().chain(std::iter::once(' ')) {
        if ch.is_ascii_digit() || ch == '.' || ch == '-' {
            cur.push(ch);
        } else {
            if !cur.is_empty() {
                if let Ok(v) = cur.parse::<f32>() {
                    out.push(v);
                }
                cur.clear();
            }
        }
    }
    out
}

/// (string, width, opacity, first start, stagger, duration)
const TIERS: [(&str, f32, f32, f32, f32, f32); 6] = [
    (ROOTS, 0.8, 0.55, 0.5, 0.1, 1.4),
    (TRUNK, 2.6, 1.0, 1.2, 0.0, 1.8),
    (L1, 2.0, 1.0, 2.7, 0.15, 1.3),
    (L2, 1.5, 1.0, 3.7, 0.1, 1.3),
    (L3, 1.2, 1.0, 4.7, 0.06, 1.2),
    (TW, 0.9, 0.9, 5.6, 0.04, 1.0),
];

/// Time at which the whole tree (and its dots) is fully grown.
pub const GROWN_AT: f32 = 8.0;

impl Tree {
    pub fn build() -> Tree {
        let mut branches = Vec::new();
        let mut raw: Vec<((f32, f32), (f32, f32), f32)> = Vec::new(); // start, end, done
        let key = |p: (f32, f32)| ((p.0 * 10.0).round() as i32, (p.1 * 10.0).round() as i32);
        let mut ends: std::collections::HashMap<(i32, i32), usize> = std::collections::HashMap::new();
        // (tier, index within tier) of every limb a knob can own, in knob order: twigs, then third-level, then second-level limbs
        let mut pool: Vec<(u8, usize)> = Vec::new();
        for tier in [5u8, 4, 3] {
            let n = TIERS[tier as usize].0.split(" M").count();
            for i in 0..n {
                if tier == 3 && i == n - 1 {
                    continue; // the last second-level limb stays unowned, which makes the pool exactly OWN_COUNT
                }
                pool.push((tier, i));
            }
        }
        let mut own_branch = vec![usize::MAX; OWN_COUNT];
        for (ti, (s, w, op, t0, st, dur)) in TIERS.iter().enumerate() {
            for (i, seg) in s.split(" M").enumerate() {
                let n = numbers(seg);
                if n.len() < 6 {
                    continue;
                }
                let delay = t0 + i as f32 * st;
                let idx = branches.len();
                let parent = ends.get(&key((n[0], n[1]))).copied();
                let own = pool.iter().position(|&(t, j)| t as usize == ti && j == i).filter(|&o| o < OWN_COUNT);
                if let Some(o) = own {
                    own_branch[o] = idx;
                }
                branches.push(Branch { p0: (n[0], n[1]), c: (n[2], n[3]), p1: (n[4], n[5]), width: *w, opacity: *op, delay, dur: *dur, tier: ti as u8, parent, own });
                ends.insert(key((n[4], n[5])), idx);
                raw.push(((n[0], n[1]), (n[4], n[5]), delay + dur));
            }
        }
        let starts: std::collections::HashSet<(i32, i32)> = raw.iter().map(|r| key(r.0)).collect();
        let mut seen = std::collections::HashSet::new();
        let mut tips: Vec<Tip> = Vec::new();
        for (bi, r) in raw.iter().enumerate() {
            let k = key(r.1);
            if !starts.contains(&k) && seen.insert(k) {
                tips.push(Tip { at: r.1, branch: bi, done: r.2, lit: false, twinkle: 0.0 });
            }
        }
        if !tips.is_empty() {
            let pick = |score: &dyn Fn(&Tip) -> f32| -> usize {
                let mut best = 0;
                for (i, t) in tips.iter().enumerate() {
                    if score(t) < score(&tips[best]) {
                        best = i;
                    }
                }
                best
            };
            let picks = [
                pick(&|t| t.at.1),
                pick(&|t| -t.at.0),
                pick(&|t| t.at.0),
                pick(&|t| -t.at.1),
                pick(&|t| (t.at.0 - 450.0).abs() + (t.at.1 - 150.0).abs()),
            ];
            let mut n = 0.0;
            for p in picks {
                if !tips[p].lit {
                    tips[p].lit = true;
                    tips[p].twinkle = n * 1.3;
                    n += 1.0;
                }
            }
        }
        Tree { branches, tips, own_branch }
    }

    /// The tree as it stands right now: every limb scaled by its knob's length factor and bent by the wind.
    pub fn pose(&self, p: &Pose) -> Posed {
        let mut curves: Vec<[(f32, f32); 3]> = Vec::with_capacity(self.branches.len());
        let mut rot: Vec<f32> = Vec::with_capacity(self.branches.len());
        for b in &self.branches {
            let (s0, r0) = match b.parent {
                Some(j) => (curves[j][2], rot[j]),
                None => (b.p0, 0.0),
            };
            let f = b.own.and_then(|o| p.lens.get(o).copied()).filter(|f| f.is_finite() && *f > 0.0).unwrap_or(1.0);
            let theta = r0 + sway(b.tier, s0, p);
            rot.push(theta);
            let (sn, cs) = theta.sin_cos();
            let turn = |v: (f32, f32)| (f * (v.0 * cs - v.1 * sn), f * (v.0 * sn + v.1 * cs));
            let c = turn((b.c.0 - b.p0.0, b.c.1 - b.p0.1));
            let e = turn((b.p1.0 - b.p0.0, b.p1.1 - b.p0.1));
            curves.push([s0, (s0.0 + c.0, s0.1 + c.1), (s0.0 + e.0, s0.1 + e.1)]);
        }
        Posed { curves }
    }
}

/// What the tree is doing: knob length factors (1 = as drawn) and the wind.
pub struct Pose<'a> {
    pub lens: &'a [f32],
    /// 0 = still, 1 = full gale
    pub wind: f32,
    /// radians, advances with time (faster in stronger wind)
    pub phase: f32,
}

/// Branch curves (start, control, end in tree space) after `Tree::pose`.
pub struct Posed {
    pub curves: Vec<[(f32, f32); 3]>,
}

impl Posed {
    pub fn tip(&self, tree: &Tree, i: usize) -> (f32, f32) {
        self.curves[tree.tips[i].branch][2]
    }
}

/// Extra bend (radians) a limb of `tier` adds on top of its parent's, starting at `s0`. It leans with the wind and swings
/// to and fro, with a wave that travels across the canopy, and the twigs flutter a little faster.
fn sway(tier: u8, s0: (f32, f32), p: &Pose) -> f32 {
    if p.wind <= 0.0 {
        return 0.0;
    }
    const STEP: [f32; 6] = [0.0, 0.012, 0.016, 0.022, 0.030, 0.040];
    let wave = p.phase + s0.0 * 0.021 + s0.1 * 0.009;
    let gust = 0.65 + 0.35 * (p.phase * 0.31 + 1.0).sin();
    let flutter = if tier >= 4 { 0.25 * (p.phase * 2.7 + s0.0 * 0.07 + s0.1 * 0.05).sin() } else { 0.0 };
    STEP[tier as usize] * p.wind.min(1.0) * gust * (0.55 + 0.45 * wave.sin() + flutter)
}

impl Branch {
    /// Point at fraction `t` (0..1) along the curve.
    pub fn at(&self, t: f32) -> (f32, f32) {
        let u = 1.0 - t;
        (
            u * u * self.p0.0 + 2.0 * u * t * self.c.0 + t * t * self.p1.0,
            u * u * self.p0.1 + 2.0 * u * t * self.c.1 + t * t * self.p1.1,
        )
    }

    /// How much of the branch has grown at time `time` (0..1, eased out).
    pub fn grown(&self, time: f32) -> f32 {
        let x = ((time - self.delay) / self.dur).clamp(0.0, 1.0);
        1.0 - (1.0 - x).powi(3)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn counts_and_tips() {
        let t = Tree::build();
        assert_eq!(t.branches.len(), 11 + 1 + 3 + 7 + 14 + TW.split(" M").count());
        assert!(t.tips.len() > 20);
        assert_eq!(t.tips.iter().filter(|t| t.lit).count(), 5);
    }

    #[test]
    fn tips_are_free_ends() {
        let t = Tree::build();
        for tip in &t.tips {
            for b in &t.branches {
                assert!((b.p0.0 - tip.at.0).abs() > 0.01 || (b.p0.1 - tip.at.1).abs() > 0.01, "tip {:?} starts a branch", tip.at);
            }
        }
    }

    #[test]
    fn growth_is_monotonic_and_ends_on_tip() {
        let t = Tree::build();
        let b = t.branches[0];
        assert_eq!(b.grown(0.0), 0.0);
        assert!(b.grown(b.delay + b.dur * 0.5) > 0.5);
        assert_eq!(b.grown(100.0), 1.0);
        let e = b.at(1.0);
        assert!((e.0 - b.p1.0).abs() < 1e-3 && (e.1 - b.p1.1).abs() < 1e-3);
        assert!(t.tips.iter().all(|t| t.done <= GROWN_AT));
    }

    fn dist(a: (f32, f32), b: (f32, f32)) -> f32 {
        ((a.0 - b.0).powi(2) + (a.1 - b.1).powi(2)).sqrt()
    }

    #[test]
    fn every_knob_owns_a_limb() {
        let t = Tree::build();
        assert_eq!(t.own_branch.len(), OWN_COUNT);
        let mut seen = std::collections::HashSet::new();
        for &b in &t.own_branch {
            assert!(b < t.branches.len(), "a knob has no limb");
            assert!(seen.insert(b), "two knobs share a limb");
        }
        assert_eq!(t.branches.iter().filter(|b| b.own.is_some()).count(), OWN_COUNT);
    }

    #[test]
    fn rest_pose_is_the_drawn_tree() {
        let t = Tree::build();
        let lens = vec![1.0; OWN_COUNT];
        let p = t.pose(&Pose { lens: &lens, wind: 0.0, phase: 1.3 });
        for (b, c) in t.branches.iter().zip(&p.curves) {
            assert!(dist(c[0], b.p0) < 1e-2 && dist(c[1], b.c) < 1e-2 && dist(c[2], b.p1) < 1e-2, "branch moved at rest");
        }
        // an empty lens list means "as drawn" too
        let q = t.pose(&Pose { lens: &[], wind: 0.0, phase: 0.0 });
        assert!(dist(q.curves[40][2], t.branches[40].p1) < 1e-2);
    }

    #[test]
    fn knob_length_scales_its_limb_and_carries_children() {
        let t = Tree::build();
        let n = 40; // an L3 limb (third-level)
        let bi = t.own_branch[n];
        let b = t.branches[bi];
        let mut lens = vec![1.0; OWN_COUNT];
        lens[n] = 2.0;
        let p = t.pose(&Pose { lens: &lens, wind: 0.0, phase: 0.0 });
        let len0 = dist(b.p0, b.p1);
        assert!((dist(p.curves[bi][0], p.curves[bi][2]) - 2.0 * len0).abs() < 0.05);
        assert!(dist(p.curves[bi][0], b.p0) < 1e-2, "limb must still start where its parent ends");
        // a limb that grows from this one starts at its new end
        let kid = t.branches.iter().position(|k| k.parent == Some(bi));
        if let Some(k) = kid {
            assert!(dist(p.curves[k][0], p.curves[bi][2]) < 1e-3);
        }
    }

    #[test]
    fn wind_sways_the_canopy_not_the_roots() {
        let t = Tree::build();
        let lens = vec![1.0; OWN_COUNT];
        let calm = t.pose(&Pose { lens: &lens, wind: 0.0, phase: 0.7 });
        let gale = t.pose(&Pose { lens: &lens, wind: 1.0, phase: 0.7 });
        for (i, b) in t.branches.iter().enumerate() {
            if b.tier == 0 {
                assert!(dist(calm.curves[i][2], gale.curves[i][2]) < 1e-3, "roots must stay put");
            }
        }
        let mut moved = 0.0f32;
        for i in 0..t.tips.len() {
            moved = moved.max(dist(calm.tip(&t, i), gale.tip(&t, i)));
        }
        assert!(moved > 3.0 && moved < 40.0, "tips moved {moved}");
        // the sway swings over time
        let later = t.pose(&Pose { lens: &lens, wind: 1.0, phase: 0.7 + 1.6 });
        let twig = t.tips.iter().position(|tp| t.branches[tp.branch].tier == 5).unwrap();
        assert!(dist(gale.tip(&t, twig), later.tip(&t, twig)) > 0.5);
        // limb lengths survive the bend
        for (i, b) in t.branches.iter().enumerate() {
            let l0 = dist(b.p0, b.p1);
            assert!((dist(gale.curves[i][0], gale.curves[i][2]) - l0).abs() < 0.05, "limb {i} changed length");
        }
    }

    #[test]
    fn map_places_origin() {
        let p = LEFT.pt((450.0, 332.0));
        assert_eq!(p, (186.0, 614.0));
    }
}
