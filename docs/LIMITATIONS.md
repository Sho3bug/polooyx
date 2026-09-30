# POLOOYX v1.0 — Known Limitations (first milestone)

Everything listed in the UI is implemented with real DSP. The points below describe what those
implementations can and can't do yet, so nothing is oversold.

## Pitch correction
- **Latency: ~23 ms total at 48 kHz** (20 ms PSOLA look-ahead + 1.5 ms limiter + oversampling + a 32-sample
  processing grid). It's reported to FL Studio, so playback is compensated. Monitoring *through* the plugin while
  recording will feel late. Workaround: record dry, or turn TUNE off while tracking. A low-latency mode
  (~10 ms, slightly grainier on low voices) is a candidate for the next milestone.
- **Monophonic.** One voice at a time: stacked harmonies or chords on one track will confuse the tracker.
- **Pitch range 70 Hz – 1 kHz.** Very low growls or whistle register won't be tracked (they pass through uncorrected).
- **Voices below ~100 Hz** use grains slightly shorter than one period (latency budget), which can add a faint
  roughness at strong corrections. Normal male rap/singing range (100–300 Hz) is unaffected.
- **No MIDI-controlled target notes** yet — key/scale only.
- **"Pentatonic" = minor pentatonic** (relative to the chosen key). Pick the relative minor key for major pentatonic.
- **HUMANIZE / FLEX** from the original spec are not in this milestone. RETUNE SPEED + AMOUNT cover the core behaviour.

## Formant
- Real, pitch-independent formant shifting (grain resampling). Range ±12 st, but beyond about ±6 st it becomes
  obviously synthetic, which suits "alien" presets but not natural tone shaping.
- Formant shifting runs the grain engine even with TUNE off, so it carries the same latency (already compensated).

## Glitch
- Real buffer manipulation: stutter, repeat, reverse, gate, tape-stop, all locked to FL Studio's tempo and
  bar position. With the transport stopped, it free-runs at the project tempo.
- **SMART GLITCH detects events from the audio** (energy envelopes + the pitch tracker): phrase endings, held
  voiced notes, accents and pauses right after a phrase. It **does not understand words**. "Emphasized word" means
  loud onset, not meaning. Adlib detection isn't possible from a single track, so glitch your adlib track separately.
- Randomness is deterministic per session (same result each render), not re-seeded per playback.

## Space / stereo
- One reverb algorithm (8-line modulated FDN) shaped by SIZE/DECAY/DAMPING. The spec's named reverb modes
  (DARK ROOM, VOID, CATHEDRAL, …) are for the next milestone.
- Delay modes: stereo or ping-pong. GLITCH/REVERSE delay modes are next milestone.
- In mono (mono out) all stereo width effects fold down.

## Not yet in this milestone (planned)
GHOST vocal layer · ADLIB mode · PSYCHE engine (chorus/flanger/phaser section) · MUTATE/randomize ·
favourites (★) · settings page (UI scale, oversampling choice, animation toggle) · LAB routing ·
reverb/delay modes · macOS/AU build.
(The SHADOW layer and the micro-pitch doubler *are* in, because the signature sound depends on them.)

## Platform
- Windows x64 VST3 + Standalone. Built and validated by GitHub Actions (pluginval strictness 10).
- The build is **not code-signed**, so Windows SmartScreen may warn on first run ("More info → Run anyway").
- JUCE 8 is used under its AGPLv3/personal terms. That's fine for your own music. If you ever sell or
  distribute the plugin commercially, you'll need a JUCE licence (free tier exists for small revenue).
