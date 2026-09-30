# POLOOYX — Presets

## Factory presets (25)
Pick from the **preset box** at the top, or step through them with **< / >**. They're also exposed to the host as
VST3 programs, so FL Studio's plugin-wrapper preset menu can list them.

| Category | Presets |
|---|---|
| CORE | POLOOYX DEFAULT · POLOOYX CLEAN · POLOOYX DARK · POLOOYX HARD · POLOOYX SPACE |
| GLITCH | DIGITAL TEAR · BUFFER DREAM · GLITCHED · BROKEN SIGNAL · ERROR |
| PSYCHEDELIC | ACID DREAM · VOID · TRIP · FLOAT · OUT OF BODY |
| VOCAL | NIGHT VOCAL · UNDERGROUND · DARK VOCAL · WIDE VOCAL · DISTORTED VOCAL |
| EXTREME | SYSTEM FAILURE · ALIEN · CORRUPTED · MELTDOWN · DEAD SIGNAL |

These are first-pass voicings built on synthetic test material. They'll be retuned once you send
feedback from real vocals.

## Making your own
1. Start from the closest factory preset.
2. Set the **six macros** first. They carry the character.
3. Open **ADVANCED** only for specifics: key/scale, HPF, de-esser threshold for your mic, delay time.
4. Click **SAVE**, type a name, press Enter.
   Saved to `Documents\POLOOYX\Presets\<name>.polooyx`. It shows up under **USER** in the preset box.
5. Saving with an existing name overwrites that preset.

## Comparing
- **A / B**: two complete plugin states. Click B, tweak, click A to hear the original. Whatever you change is
  kept in the slot you're on.
- **UNDO / REDO**: every knob move, button click and preset load is one step.

## Moving / sharing presets
`.polooyx` files are plain XML. Copy them between machines into `Documents\POLOOYX\Presets`.

## FL Studio's own preset system
FL's plugin-wrapper **Save preset as…** (`.fst`) also works and stores the complete state. FL projects save
the full plugin state automatically, so you never lose settings by closing a project.
