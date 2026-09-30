# POLOOYX — VOCAL ENGINE

A real-time vocal processor for a dark, futuristic, glitchy, psychedelic underground sound.
JUCE 8 / C++17 · VST3 + Standalone · Windows x64 (built by GitHub Actions).

**Clean aggressive vocal + controlled psychedelic environment + unpredictable digital artifacts.**

## Download & install (Windows / FL Studio)
1. Open this repo on GitHub → **Actions** → latest green *Build POLOOYX* run → download **POLOOYX-Windows-x64**,
   or grab `POLOOYX-Windows-x64.zip` from the **`builds`** branch (updated on every push to `main`).
2. Unzip, double-click **`Install POLOOYX.bat`** (copies `POLOOYX.vst3` to `C:\Program Files\Common Files\VST3`).
3. FL Studio → *Options → Manage plugins → Find installed plugins* → POLOOYX shows under Effects (vendor *Polooyx*).
4. Put it on your vocal's mixer track.

Full steps: [`packaging/INSTALL.txt`](packaging/INSTALL.txt).

## Using it
- **Six macros = the whole POLOOYX sound.** AURA · GLITCH · SPACE · DARK · CHAOS · BODY. Each drives many
  parameters at once. See [docs/MACROS.md](docs/MACROS.md).
- **POLOOYX MODE** switches on the signature voicing: shadow layer, macro cross-links, ducked space, glitch echoes.
- **ADVANCED** opens every module: TUNE · EQ · DYNAMICS · SATURATION · GLITCH · SPACE · STEREO, plus the
  clickable signal chain for bypassing modules.
- **Presets**: 25 factory presets plus your own (SAVE). A/B, UNDO/REDO. See [docs/PRESETS.md](docs/PRESETS.md).
- Set **KEY / SCALE** (ADVANCED → TUNE) to your beat for pitch correction.

## Signal chain
```
IN → TUNE (YIN + PSOLA pitch/formant) → EQ → DE-ESS → COMP → SATURATE (2x OS) → SHADOW layer
   → GLITCH (tempo-synced buffer FX) → SPACE (delay → FDN reverb, ducked) + MICRO-PITCH doubler
   → WIDTH / MOVEMENT (lead stays centred) → OUTPUT / MIX → soft clip → lookahead limiter → OUT
```

## Docs
| | |
|---|---|
| [docs/PARAMETERS.md](docs/PARAMETERS.md) | every parameter, range and default (generated from the code) |
| [docs/MACROS.md](docs/MACROS.md) | exactly what each macro controls, and how they interact |
| [docs/LIMITATIONS.md](docs/LIMITATIONS.md) | known limitations of this milestone |
| [docs/PRESETS.md](docs/PRESETS.md) | presets, saving your own, A/B |
| [docs/TEST_REPORT.md](docs/TEST_REPORT.md) | DSP verification: every parameter measured, pitch accuracy, CPU |

## Building from source (optional — CI does this for you)
```bash
git clone <this repo>   # JUCE 8.0.4 is fetched automatically by CMake
cmake -B build -DCMAKE_BUILD_TYPE=Release          # Windows: -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target POLOOYX_VST3 POLOOYX_Standalone PolooyxTests
./build/PolooyxTests_artefacts/Release/PolooyxTests  # writes test-output/TEST_REPORT.md
```

## Repository layout
```
Source/
  PluginProcessor.*    audio engine: chunked real-time chain, presets, A/B, state
  PluginEditor.*       UI (macros, visualizer, advanced pages, meters)
  MacroMap.h           the macro network (macros → engine targets)
  Params.h             parameter IDs, ranges, defaults
  Presets.h            factory presets
  dsp/                 PitchEngine (YIN + PSOLA), Dynamics, Saturator, GlitchEngine, SpaceStereo
  ui/LookAndFeel.h     visual language
tests/TestMain.cpp     offline verification harness (runs in CI on the Windows build)
.github/workflows/     Windows build → tests → pluginval → zip
```

JUCE 8.0.4 is downloaded by CMake at configure time (AGPLv3 / JUCE licence).
