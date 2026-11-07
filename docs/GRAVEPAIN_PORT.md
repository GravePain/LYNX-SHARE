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
- Flaky upstream test: `vfx_script_late_answers_play_at_the_next_block` (`tests/VfxScript165Tests.inc:399-400`)
  depends on a script thread answering within its 5 ms sleeps; on this machine it fails about 1 run in 5 (run alone,
  5 runs: 4 pass). Not touched by the port; counted as pre-existing.
- Timer-dependent upstream test: `browser162_search_is_served_before_a_background_pass` (`tests/Browser162Tests.inc:220,
  244-245`) lists 2551 folders on a fake disk that sleeps 1 ms per listing, within 30 s. Windows sleeps at least one
  timer tick (15.4 ms measured here at the default resolution), so it needs ~39 s and fails whenever no program has
  raised the system timer resolution; it passed in the baseline run, when one had. Not touched by the port.

## Test counts

| After | Port checks (`LYNX_TEST=gp_`) | Full run |
|---|---|---|
| Steps 1-4 | 7679, 0 failures | the baseline's 24 failed checks (+ the flaky VfxScript pair once), nothing new |
| UI for 2-4 | 7683, 0 failures | the baseline's 24 failed checks, nothing new |

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

## UI for steps 2-4

- New: `src/ui/PatternLibraryUi.*` (the bundled library's place, the dataset cached in the settings folder
  `patterns/patterns.txt`, the taste in `patterns/taste.txt`, user folders in `pattern-folders.txt`, insertion in key);
  four Piano roll tools in `src/ui/NoteToolDialogs.cpp` under **Tools › Dice and patterns** (Pattern from the library,
  Chord dice, Melody dice, Riff stages) using LYNX's note-tool window (live preview, Accept = one undo step); the
  Browser root **Pattern library** (`BrowserView::RootKind::patternLibrary`); library drops on the Piano roll (in the
  scale box's key) and on Channel Rack channels (as written).
- Core: `patterns::rootNoteInKey` (a minor part in a major key lands on the relative minor), `LayOptions::keyScale` (any
  notetools scale). Tests in `tests/GravepainPatternsTests.inc`.
- The builds copy `resources/midi-library` next to the executable (`build-win/midi-library`, CMake POST_BUILD).
- Strings: pt-BR lines for all 61 new strings; the other 13 tables wait in `resources/i18n/pending.keys`, as the
  project's i18n rule allows.
- Decisions: the project has no key of its own, so the tools start in the Piano roll's scale box key (no scale: C
  major / as written). A chord lane is one progression with one shape and inversion for all bars; per-bar editing
  (GravePain's ChordBar in full) is supported by the core and its text format but has no editor yet.
- Checks: `LYNX.exe --selftest` result=OK; port checks 7683, 0 failures; full run = the baseline's failures only.

## Step 5: the GravePain Synth as a native instrument

- Sources: `Assets/Scripts/Engine/Synth/` SynthEngine, SynthPatch, SynthEffects, WavetableBank, SampleSet, SynthMacros,
  SynthFactoryPresets, HelmPatch; `Docs/synth-plan.md`, `Docs/synth-plan-review.md`; tests from SynthEngineTests,
  SynthExpansionTests, SynthSourcesTests, SynthMacroTests, SynthPresetTests where they apply.
- New: `src/core/GpSynth.*` (patch table, engine, effects, wavetables, sample sets, the instrument),
  `src/core/GpSynthBank.cpp` + generated `src/core/GpSynthBank.inc` (sound types, dials, factory presets, LYNX bank),
  `src/core/GpHelm.*` (.helm import), `src/core/GpSynthLayout.cpp` (the channel page), `tools/gravepain/convert_presets.py`
  (generates the .inc from GravePain's sources), `tests/GravepainSynthTests.inc`.
- Registered like LYNX's other native synths: `InstrumentKind::gravepain` (appended), `natives::instrumentEngines()`
  ("GravePain Synth", own engine, no FL data), `makeInstrument`, the project file id `gravepain`, the automatable
  parameters (`ModelParamList.inc`), the preset bank (`LynxBankList.inc`), the designed page (`instrumentLayout`).
- Decisions:
  - The patch is 150 numbered parameters in `ChannelDef::modelParams` (names, ranges, defaults as GravePain's), so the
    editor, automation clips, MIDI links and the preset bank reach all of them; the dials are extra automatable
    parameters ("Bass Squelch", index 1000 + type * 10 + dial) whose value is read back from their first part.
  - Audio thread: no allocation, no lock (a double-buffered patch published by `configure`, wavetables built on the UI
    thread, sample sets swapped by pointer); a NaN / infinite voice is cleared and the output is finite (as GravePain's
    ba9a600). The synth's own arpeggiator and sequence player are not ported: a LYNX channel has its arpeggiator and
    the Piano roll plays the notes. Synced LFOs / delay follow the project tempo from a free-running beat.
  - **Not shipped**: GravePain's five `.wtb` banks (their origin is not recorded, so they cannot be shown to be the
    author's) and the FL Keys multisamples (FL Studio content, clean-room rule). LYNX makes its own band-limited banks
    instead (Basic shapes: sine > triangle > saw > square; PWM; Harmonics: 1 to 64 partials), 64 frames x 11 mips x 2048
    by inverse FFT. Presets that read GravePain's Digital / Formant banks use Harmonics / Basic shapes; the four piano
    presets (FL Keys samples) are left out: 51 factory presets remain.
  - Sample sets come from the user's own files: a folder of WAVs named with their root, or a .wti over WAVs (read with
    LYNX's FL Keys `parseWti`, extended rather than duplicated). Raw `.dat` sets are not read.
  - .helm import is our own reading of Helm's JSON (no Helm code); Helm's arpeggiator is reported as unmapped.

## Step 6: Launchpad support

- Sources: `Engine/Midi/LaunchpadX.cs`, `LaunchpadLightShow.cs`, `App/LaunchpadGrid.cs`, `Engine/Grid/LedColour.cs`;
  tests from LedColourTests.
- New: `src/core/Launchpad.*` (the device protocol, LED colours, the light show, the surface's modes; no JUCE),
  `src/ui/LaunchpadController.*` (the ports, the timer, applying the actions), `tests/GravepainLaunchpadTests.inc`;
  `AudioEngine::setLaunchpadCapture` / `popLaunchpadMessage` (a Launchpad's input goes to the controller, not to notes);
  Options > Launchpad in the main menu.
- Models: Launchpad X, Mini MK3 and Pro MK3 (the same Programmer Mode protocol; only the sysex device byte differs).
  The DAW port (MIDIIN2 / "DAW") is never the one driven.
- Decisions: GravePain's Launchpad drove its own loop grid (GridSurface), which LYNX does not have; the surface is
  mapped onto LYNX's own model instead: Patterns (the pads launch patterns, page by page), Notes (step 1's in-key
  layout on the selected channel; the notes are injected as MIDI so they go where a keyboard's would), Steps (the
  Channel Rack's step sequencer of the current pattern). The light show is GravePain's seven effects, seeded (Unity's
  Random became a seeded generator, Perlin noise a value noise). GravePain's "to Ableton" forwarding is not ported (a
  LYNX feature would be MIDI Out). The light show starts from the menu; the idle timer was not carried over.
- Not verified on hardware here (no Launchpad on this machine): the protocol bytes are tested, the device was not.

## Step 7: phone pad

- Skipped at the user's request.

## Step 8: sample-browser matching

- Sources: `Engine/Content/KeyRules.cs`, `TempoRules.cs`, `SoundTags.cs`, `AudioTags.cs`; tests from RulesTests and
  BrowserTests where they apply.
- New: `src/core/SampleMatch.*`, `tests/GravepainMatchTests.inc`; `browserview::View` gains the match settings; the
  Browser's view menu gains "Fits the key" and "Fits the project tempo".
- LYNX had nothing of this (its browser filters by file kind and searches names), so nothing was duplicated.
- Decisions: GravePain read keys and tempos from its library's tags; LYNX's samples have none, so they are read from
  the file names (`Am`, `F#m`, `C major`, `in Eb`, `128bpm`, `BPM 90`, a number in a loop's name). A file that states
  nothing passes the filter. The length-from-header reader (WAV / AIFF / Ogg) and the tempo guess from a loop's length
  are in core but not used by the filter (reading every file while the tree paints would be slow).

## Skipped (LYNX covers them)

_To be filled as compared: VoiceEffects vs Tune, TimeStretch, ColumnEffects vs Sentry/Ember/Crest, FlScore,
PluginLink / GravepainHost. Not ported: Unity UI, Free Play, SongPlayer, LoopLayerMixer._
