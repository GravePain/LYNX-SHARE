# MIDI library builder

Writes `resources/midi-library/GravePain Originals` and checks it. Node, no packages. Ported from GravePain (the
author's own tool, AGPL-3.0 here like the rest of LYNX); the generated MIDI is original content.

    node build.js [out folder] [sketches per style]   # default: resources/midi-library/GravePain Originals, 16
    node validate.js [library folder]                   # reads every file back; exit 1 on any problem

LYNX's own importer (`src/core/MidiPatterns.cpp`) is checked against the library by the test
`gp_patterns_generated_library` in `tests/GravepainPatternsTests.inc`: it fails if a file gives no pattern or a part
lands in another role than its file name says.

| File | What |
|---|---|
| `styles.js` | The styles: tempo ranges, modes, progressions, drum grooves, and the rhythms of bass, chords, leads and bells. Add a style here. |
| `music.js` | Keys, chords, voice leading, and a generator for each role: drums by section, bass from templates, chords, pads, melody (a motif answered and varied, ending on a chord tone), arpeggios, bells. Seeded, so a build is the same every time. |
| `smf.js` | Standard MIDI files: writes format 1 (a tempo track, a part track), reads them back for the checks. |
| `build.js` | Sketches per style, the parts of each, names, the manifest; drops exact repeats. |
| `validate.js` | Every file: reads back with notes that pair up; one part, drums on channel 10 with GM drum notes; the importer's section and role words; every note in key, in range, within its bars and on its sixteenth after swing; bass, leads, plucks and bells one note at a time; no lead leap over an octave; no two files the same. |

Swing moves the off sixteenths later by less than half a step, so a DAW hears it and the importer still puts every
note on its sixteenth.
