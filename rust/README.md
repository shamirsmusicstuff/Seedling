# Seedling (Rust)

The Rust rebuild of Seedling: the same random-patch synth, written with [nih-plug](https://github.com/robbert-vdh/nih-plug)
and egui. It builds **VST3** and **CLAP** on Windows, macOS and Linux. (nih-plug has no Audio Unit; the JUCE version on
`main` still ships the AU.)

## Layout

| Path | What |
| --- | --- |
| `dsp/` | `seedling-dsp`, dependency-free engine: parameters, 8-voice engine, six sound sources (Oscillator, Wavetable, Pluck, Vowel, Struck, Unison), wavetable importer with pitch-detect auto-tune, 12 reverbs (DuskVerb-based, GPL-3.0), seed generator with 22 archetypes and Correct Tune |
| `plugin/` | nih-plug shell, egui editor, presets and wavetable storage |
| `tools/gen_params.py` | regenerates `plugin/src/params_gen.rs` from `dsp/src/params.rs` |

The generator is ported value for value: seeds grow the same patches as the C++ version (checked against the C++ generator
on 60 patches in `dsp/src/randomizer.rs` tests).

## Interface

The window is drawn in a 1200 x 900 design space and scales with the host window. Home is just the big centred tree; the three
boxes on the left (edit pages only) (Envelope & LFO at the top, Oscillators, Filter & effects at the bottom) open the edit pages, and
**Edit sound** / **Home** in the bottom row switch between them. Knobs are circles that fill with their value: drag up
and down, hold Shift for fine steps, double-click for the default. The seed box, Randomize, Correct tune, Save and Load
sit in the bottom row. The waves, orbit and waveform previews follow the host tempo.

### Look: Autumn tree, wind, skins

The tree is the **Autumn** pixel-block tree: stamped from square blocks, bark brown at the roots turning to gold at the twigs,
orange-red tips, a few twinkling pale beacons, and the seed at the base that the roots and trunk sprout from.

* **Knobs change limb lengths.** Each of the 49 knobs on the three edit pages owns one limb (twigs first, then the third- and
  second-level limbs). Turn a knob up from its default and its limb grows, turn it down and it shrinks; everything growing from
  its end is carried along. The limb of the knob you are dragging lights up. Home shows the same tree for the current patch.
* **Wind.** While sound is playing the branches sway, the harder the louder: they lean with the wind, swing in a wave across
  the canopy and the twigs flutter. When the sound stops the tree settles again. The roots stay put.
* **Skins.** *SKIN* on Home steps through the skins. Built in: **Indigo** (the original look), **Dusk**
  (`151e3f 030027 f2f3d9 dc9e82 c16e70`), **Brass** (`363636 242f40 cca43b e5e5e5 ffffff`), **Oxblood**
  (`0a0908 49111c f2f4f3 a9927d 5e503f`), Seedling, Skin 1-3, and *Seed colours* (a palette grown from the patch seed).
  Your own are `.seedskin` files in `Documents/Seedling/Skins` (five hex colours, any order; see the README written there).
  The choice is saved with the session. The tree keeps its Autumn colours in every skin.

### Design rule

Only one tree is on screen at a time. The edit pages show the tree on the left, running through the three page boxes.
Home shows nothing but the big centred tree (plus the bottom row of buttons).

## Build

```
cd rust/dsp && cargo test --release          # engine, importer, reverbs, generator
cd rust && cargo xtask bundle seedling --release   # target/bundled/Seedling.vst3 and .clap
```

Copy `Seedling.vst3` to your VST3 folder (Windows `C:\Program Files\Common Files\VST3`, macOS `~/Library/Audio/Plug-Ins/VST3`).

## Custom wavetables

Pick the **Wavetable** source, open *Wavetable...* and choose a WAV from `Documents/Seedling/Wavetables`. Table files
(frames of 2048/1024/512/256 samples, or a Serum `clm` marker) play frame by frame; sampled sounds are pitch-detected (YIN),
cut into single cycles and tuned so they play in tune on every key. The table is stored in presets and in the host session.

Presets are plain text files in `Documents/Seedling/Presets`.

## Not yet in the Rust version

The 3D tree. The window scales freely; the growing tree and the wave background run on the host tempo.
