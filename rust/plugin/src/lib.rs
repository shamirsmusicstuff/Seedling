//! Seedling: a random-patch synth that grows sounds from a seed. nih-plug shell around the `seedling-dsp` engine.

mod editor;
mod params_gen;
mod storage;

use nih_plug::prelude::*;
use params_gen::SeedParams;
use seedling_dsp::engine::{Engine, WavetableSlot};
use std::num::NonZeroU32;
use std::sync::Arc;

/// State shared between the audio thread and the editor.
pub struct Shared {
    pub params: Arc<SeedParams>,
    pub slot: WavetableSlot,
}

pub struct Seedling {
    shared: Arc<Shared>,
    engine: Engine,
    /// Length of the wavetable text we last decoded, so a restored session is picked up in `initialize`.
    loaded_wavetable_len: usize,
}

impl Default for Seedling {
    fn default() -> Self {
        let params = Arc::new(SeedParams::default());
        let slot = WavetableSlot::new();
        let mut engine = Engine::new(44100.0);
        engine.set_wavetable_slot(slot.clone());
        Self { shared: Arc::new(Shared { params, slot }), engine, loaded_wavetable_len: 0 }
    }
}

impl Seedling {
    fn sync_wavetable(&mut self) {
        let text = self.shared.params.wavetable.lock().map(|s| s.clone()).unwrap_or_default();
        if text.len() == self.loaded_wavetable_len {
            return;
        }
        self.loaded_wavetable_len = text.len();
        let table = storage::table_from_text(&text);
        self.shared.slot.set(table.map(Arc::new));
    }
}

impl Plugin for Seedling {
    const NAME: &'static str = "Seedling";
    const VENDOR: &'static str = "Seedling";
    const URL: &'static str = "https://github.com/shamirsmusicstuff/Seedling";
    const EMAIL: &'static str = "";
    const VERSION: &'static str = env!("CARGO_PKG_VERSION");

    const AUDIO_IO_LAYOUTS: &'static [AudioIOLayout] = &[AudioIOLayout {
        main_input_channels: None,
        main_output_channels: NonZeroU32::new(2),
        ..AudioIOLayout::const_default()
    }];

    const MIDI_INPUT: MidiConfig = MidiConfig::Basic;
    const SAMPLE_ACCURATE_AUTOMATION: bool = true;

    type SysExMessage = ();
    type BackgroundTask = ();

    fn params(&self) -> Arc<dyn Params> {
        self.shared.params.clone()
    }

    fn editor(&mut self, _async_executor: AsyncExecutor<Self>) -> Option<Box<dyn Editor>> {
        editor::create(self.shared.clone())
    }

    fn initialize(
        &mut self,
        _layout: &AudioIOLayout,
        config: &BufferConfig,
        _context: &mut impl InitContext<Self>,
    ) -> bool {
        self.engine.prepare(config.sample_rate);
        self.loaded_wavetable_len = usize::MAX;
        self.sync_wavetable();
        true
    }

    fn reset(&mut self) {
        self.engine.reset();
    }

    fn process(
        &mut self,
        buffer: &mut Buffer,
        _aux: &mut AuxiliaryBuffers,
        context: &mut impl ProcessContext<Self>,
    ) -> ProcessStatus {
        self.engine.set_params(&self.shared.params.values());

        let total = buffer.samples();
        let channels = buffer.as_slice();
        if channels.len() < 2 {
            return ProcessStatus::Normal;
        }
        let (left, rest) = channels.split_at_mut(1);
        let (left, right) = (&mut *left[0], &mut *rest[0]);

        let mut pos = 0usize;
        let mut next_event = context.next_event();
        while pos < total {
            // apply every event that is due at or before `pos`
            while let Some(event) = next_event {
                if event.timing() as usize > pos {
                    break;
                }
                match event {
                    NoteEvent::NoteOn { note, velocity, .. } => self.engine.note_on(note as i32, velocity),
                    NoteEvent::NoteOff { note, .. } => self.engine.note_off(note as i32),
                    NoteEvent::Choke { note, .. } => self.engine.note_off(note as i32),
                    NoteEvent::MidiPitchBend { value, .. } => self.engine.set_pitch_bend((value - 0.5) * 4.0),
                    NoteEvent::MidiCC { cc, value, .. } => match cc {
                        64 => self.engine.set_sustain(value >= 0.5),
                        120 | 123 => self.engine.all_notes_off(),
                        _ => {}
                    },
                    _ => {}
                }
                next_event = context.next_event();
            }
            let end = match next_event {
                Some(e) => (e.timing() as usize).clamp(pos + 1, total),
                None => total,
            };
            self.engine.render(&mut left[pos..end], &mut right[pos..end]);
            pos = end;
        }
        ProcessStatus::KeepAlive
    }
}

impl ClapPlugin for Seedling {
    const CLAP_ID: &'static str = "com.shamirsmusicstuff.seedling";
    const CLAP_DESCRIPTION: Option<&'static str> = Some("A random-patch synth that grows sounds from a seed");
    const CLAP_MANUAL_URL: Option<&'static str> = Some(Self::URL);
    const CLAP_SUPPORT_URL: Option<&'static str> = None;
    const CLAP_FEATURES: &'static [ClapFeature] = &[ClapFeature::Instrument, ClapFeature::Synthesizer, ClapFeature::Stereo];
}

impl Vst3Plugin for Seedling {
    const VST3_CLASS_ID: [u8; 16] = *b"SeedlingSynthRs1";
    const VST3_SUBCATEGORIES: &'static [Vst3SubCategory] = &[Vst3SubCategory::Instrument, Vst3SubCategory::Synth];
}

nih_export_vst3!(Seedling);
nih_export_clap!(Seedling);
