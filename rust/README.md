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

The window is drawn in a 1200 x 900 design space and scales with the host window. Home shows the growing tree; the three
boxes on the left (Envelope & LFO at the top, Oscillators, Filter & effects at the bottom) open the edit pages, and
**Edit sound** / **Home** in the bottom row switch between them. Knobs are circles that fill with their value: drag up
and down, hold Shift for fine steps, double-click for the default. The seed box, Randomize, Correct tune, Save and Load
sit in the bottom row. The waves, orbit and waveform previews follow the host tempo.

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

Skins and seed-derived colours (paused while the new look settles), the 3D tree. The window scales freely; the growing tree and the wave background run on the host tempo.
