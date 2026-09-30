# POLOOYX — Macro Map

The six macros are the plugin. None of them is a single effect: each one moves several parts of
the engine at once, **on top of** whatever the ADVANCED panel is set to (ADVANCED = the base, macros = the performance layer).
All values below are what the code does at 100%; everything scales smoothly from 0%.
Source of truth: `Source/MacroMap.h`.

## AURA — width · shimmer · air · haze
| Moves | By (at 100%) |
|---|---|
| Stereo width of everything around the lead | +60% |
| Micro-pitch doubler (detuned L/R copies) level | +50% |
| Doubler detune depth | +0.6 (of 1) |
| Lead pitch shimmer (slow vibrato on the lead itself) | ±7 cents |
| High-shelf "air" | +4.5 dB (reduced by DARK in POLOOYX mode) |
| Saturation drive (harmonic sheen) | +12% |
| Saturation tone | brighter (+0.3) |
| Reverb send | +10% |
| Reverb modulation (chorused tail) | +0.4 |
| De-esser | threshold −4 dB, range +3 dB (more air → more control of "s") |

## GLITCH — stutter · repeat · gate · pitch
| Moves | By (at 100%) |
|---|---|
| Event probability per grid step | +85% (curved: gentle at the bottom of the knob) |
| Grid | one step finer above 72% (e.g. 1/16 → 1/32) |
| Rhythmic gate weight | +40% |
| Pitch variation of stutter repeats | +35% (up to ±12 st per repeat) |
| Glitched fragments echo into the delay | +60% × SPACE (POLOOYX mode) |

## SPACE — delay · reverb · movement
| Moves | By (at 100%) |
|---|---|
| Delay level | +28% |
| Delay feedback | +35% |
| Delay filtering | HP +300 Hz, LP −2 kHz (trails get thinner/darker as they build) |
| Reverb size | +35% |
| Reverb decay | ×2.3 |
| Reverb pre-delay | +45 ms |
| Reverb level | +32% |
| Auto-pan movement of the space | +40% depth, slightly faster |
| Ducking (space pulls back while the lead is singing) | +20% (POLOOYX mode) |
| High-mid presence on the lead | up to +1.1 dB (POLOOYX mode "intelligibility guard") |

## DARK — filter · formant · warmth
| Moves | By (at 100%) |
|---|---|
| Low-pass filter | down to 4.8 kHz |
| High shelf | −7 dB |
| Low shelf weight | +1.5 dB |
| Formants | −2.5 semitones (lower, bigger-sounding throat; pitch unchanged) |
| Saturation drive | +22% and darker tone |
| Reverb damping | +45% |
| Delay LP | −2.5 kHz |
| Shadow layer | +15% and more driven (POLOOYX mode) |
| High-mid presence | up to +1.1 dB (POLOOYX mode, keeps words readable) |

## CHAOS — drift · modulation · instability
| Moves | By (at 100%) |
|---|---|
| Random pitch drift of the lead | ±38 cents (quadratic: subtle until ~50%), 0.25 → 2.75 Hz |
| Lead shimmer rate | faster |
| Glitch probability | +25% × GLITCH (amplifies GLITCH, never starts it on its own) |
| Glitch behaviour | more reverse/tape events, finer & off-grid slices, wilder pitch jumps |
| Saturation drive | +25%, mix +15% |
| Delay wow (tape-style time wobble) | full |
| Auto-pan | +50% depth, much faster |
| Doubler / reverb modulation | faster and deeper |

## BODY — weight · compression · density
| Moves | By (at 100%) |
|---|---|
| Low shelf | +3.5 dB |
| Low-mid | +2 dB |
| Compressor threshold | −12 dB |
| Compressor ratio | +3:1 |
| Compressor makeup | +5 dB |
| Saturation mix (density) | +25%, drive +15% |
| Shadow layer level | +40% |
| Ducking | +30% (POLOOYX mode) |

## How they interact (POLOOYX mode)
POLOOYX mode turns on a set of cross-links so the macros behave like one instrument instead of six effects:

- **DARK tames AURA** — the air shelf AURA adds is reduced by up to 60% as DARK rises, so they don't fight.
- **SPACE + BODY → ducking** — the more space and body you add, the harder the space ducks under the lead. Big
  atmosphere, lead still in front.
- **SPACE + DARK → presence** — a small upper-mid lift follows them so words stay intelligible.
- **GLITCH × SPACE** — glitched fragments are thrown into the delay, so stutters leave echoes.
- **GLITCH × CHAOS** — chaos widens glitch pitch jumps and adds drift during glitches.
- **Signature voicing** — HPF at ≥95 Hz, shadow layer, a touch of doubling and width, darker reverb.

With POLOOYX mode off, the same macros still work, but independently (no cross-links), and the shadow layer is lighter.
Switching the mode is smoothed, so it can be automated without clicks.
