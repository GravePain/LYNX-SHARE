# GravePain port

Features ported from GravePain (the author's own Unity 6 / C# project, released here under AGPL-3.0 by its author)
into LYNX as idiomatic C++20. A port, not a copy: nothing from Unity crosses over, LYNX code is extended where it
already does the job. Branch `gravepain-features`.

## Build environment and baseline

- Windows 10 without WSL distros or g++/clang: tests built with zig 0.13.0 (Windows build, `~/opt`), from Git Bash:
  `CXX="zig c++ -D_USE_MATH_DEFINES" CC="zig cc" LYNX_TESTS_SPLIT=0 scripts/build-local.sh`.
  - `LYNX_TESTS_SPLIT=0`: on Windows, `scripts/split-tests.py` writes `#line "tests\X.inc"` (a backslash escape error).
  - `_USE_MATH_DEFINES`: `M_PI` on Windows' C runtime.
  - No FL Studio installed: the checks that read FL's data folder skip (see the baseline line below).
- Baseline (unmodified LYNX, upstream `main` at the fork, Windows-native zig build): about 1060 tests run, **24
  failed checks at 5 sites**, all Windows / no-FL-install artefacts:
  `tests.cpp:3352`, `:3358` (FL path resolution with POSIX paths), `:6640` (a pt-BR line missing upstream for a
  PluginManager string), `:6680` x14, `:11583` x7 (render goldens out of tolerance on this toolchain). The run then
  **aborts** in `patcher164f_envelope_controller_…`: Windows refuses to delete a temp WAV still open
  (`filesystem_error: remove: Permission denied`), so later tests do not run here. Skipped checks: those needing an FL
  Studio data folder ("no FL Studio data: skipped", Sawer DLL fingerprints, the real-.flp folder).
  The port's tests are included before that point (in `tests/tests.cpp`, ahead of `WorkflowP2Tests.inc`) so they run;
  `LYNX_TEST=gp_ build-local/lynx_tests` runs them alone. The tools step fails to link `renderwav` (winmm's
  `timeBeginPeriod` on Windows) after the test binary is built; also an environment artefact.
  The rule kept for every commit: the same 24 failed checks, nothing new.

## Step 1: scales and chords core

- Source: `Assets/Scripts/Engine/Synth/SynthScales.cs`; tests from `Assets/Tests/EditMode/SynthScalesAndMidiTests.cs`.
- New: `src/core/Scales.h/.cpp`, `tests/GravepainScalesTests.inc`.
- **Extended LYNX instead of duplicating:** `core/NoteTools` already has the scale list, `inScale` and `snapToScale`
  (tie goes down, as GravePain's). `scales::Key` is a root plus a `notetools::scales()` index; Scales adds only what
  LYNX lacked: stepping by scale degrees, chords stacked from the key, the 8x8 in-key pad layout.
- Decisions: GravePain's `NoteName` not ported (LYNX's `core/NoteNames` keeps FL's octave numbering); "pentatonic"
  is NoteTools' major / minor pentatonic (indices 4 / 5). No allocation: audio-thread safe.

## Step 2: MIDI dataset

- Sources: `Tools/MidiLibrary/*`, `Assets/Scripts/Engine/Clip/MidiPatterns.cs`, `PatternTaste.cs`, the reading half of
  `Synth/MidiFile.cs`; tests from `MidiPatternsTests.cs`, `SynthScalesAndMidiTests.cs`.
- New: `tools/midi-library/` (Node, no packages; default paths now `resources/midi-library/GravePain Originals`),
  `resources/midi-library/GravePain Originals/` (2659 files + manifest), `src/core/SmfRead.*`, `src/core/MidiPatterns.*`,
  `src/core/PatternTaste.*`, `tests/GravepainPatternsTests.inc`.
- **Extended LYNX:** LYNX's MIDI parsing (`engine/MidiFileIo`) needs JUCE, so `SmfRead` is a JUCE-free reader that
  fills LYNX's own `midiimport::ParsedMidi` (same struct, ticks at LYNX's PPQ).
- `importer-check/` (a .NET tool) replaced by the test `gp_patterns_generated_library`.
- Decisions:
  - The library is **shipped**, not generated at build time: the generator is deterministic (a rebuild is byte-for-byte
    the GravePain output, checked with `diff -r`), 4.6 MB, and the tests and the app need it without Node.
  - Dataset and taste stored as small text formats (`toText` / `fromText`), not JSON: core has no JSON library and
    stays JUCE-free. The taste file goes in LYNX's settings folder (the UI passes the path), never in a project.
  - `PatternTaste.pickWeighted` uses its own seeded hash instead of .NET's `System.Random`, so a seed is portable.
  - Lane-specific parts (`RoleForLane`, `RoleIsNamed`, the clip grid's `Lay`) are replaced by `lay`, which writes LYNX
    `Note`s transposed to a key root, optionally snapped into the project key.

## Step 3: Chord Dice, chord lanes, Melody Dice

- Sources: `Clip/ChordDice.cs`, `Clip/ClipPattern.cs` (ChordBar), `Clip/MelodyDice.cs`, `Docs/dice-examples.md`;
  tests from `ChordTests.cs`, `MelodyDiceTests.cs`, `MidiPatternsTests.cs`.
- New: `src/core/ChordDice.*`, `tests/GravepainDiceTests.inc`.
- Relation to LYNX: prtools already has a progression parser / generator and `writeProgression`; the dice add the
  weighted table of real-song loops, per-bar key / shape / inversion and GravePain's ladder voicing. Kept separate
  (`prtools::dice`) because the voicing model differs; both write plain notes.
- Decisions: the melody's notes last until the next note or one grid step, whichever is first (GravePain's were
  struck hits). The landing option is kept off by default (dice-examples.md: the user preferred the loop).

## Step 4: riff chain stages

- Source: `Clip/RiffChain.cs`; tests from `RiffChainTests.cs` where they apply.
- New: `src/core/RiffStages.*`, `tests/GravepainRiffTests.inc`; `Pattern::toolSettings` (saved in `.lynx`).
- **LYNX already had most of it**: the riff machine's flip (mirror time / pitch), randomise (levels), articulator and
  quantize-to-groove. Added only: mirror keeping starts, pitch mirror into the key, levels reset, lengths past 100 %
  (to 4x), chop-to-grid, five built-in grooves. Arp: LYNX's Arpeggiator, not GravePain's.
- Settings: `RiffSettings` and chord lanes kept as `[section]` blocks in `Pattern::toolSettings`.

## Skipped (LYNX covers them)

_To be filled as compared: VoiceEffects vs Tune, TimeStretch, ColumnEffects vs Sentry/Ember/Crest, FlScore,
PluginLink / GravepainHost. Not ported: Unity UI, Free Play, SongPlayer, LoopLayerMixer._
