// GravePain port, step 1: scale degrees, chords and the pad grid (core/Scales, on NoteTools' scales). Ported from
// GravePain's SynthScalesAndMidiTests.

#include "core/Scales.h"

TEST (gp_scales_snap_nearest_tie_down)
{
    using namespace lynx::scales;
    const Key cMajor { 0, major };
    CHECK (snap (61, cMajor) == 60);   // C# -> C
    CHECK (snap (66, cMajor) == 65);   // F# -> F (a tie goes down)
    CHECK (snap (61, Key {}) == 61);   // no scale leaves every note alone
    CHECK (snap (60, Key { 1, major }) == 60); // C (B#) is in C# major
}

TEST (gp_scales_in_scale_and_steps)
{
    using namespace lynx::scales;
    const Key aMinor { 9, naturalMinor }, cMajor { 0, major };
    CHECK (inScale (72, aMinor));
    CHECK (! inScale (73, aMinor));
    CHECK (stepInScale (60, 2, cMajor) == 64);
    CHECK (stepInScale (60, -1, cMajor) == 59);
    CHECK (stepInScale (60, 7, cMajor) == 72);
    CHECK (stepInScale (60, -8, cMajor) == 47);
    CHECK (stepInScale (60, 1, Key { -1, majorPentatonic }) == 62); // no root = C
    CHECK (stepInScale (64, 1, Key { 0, majorPentatonic }) == 67);
    CHECK (degrees (Key {}).size() == 12);
}

TEST (gp_scales_chords_from_the_key)
{
    using namespace lynx::scales;
    const Key cMajor { 0, major };
    const auto dm = buildChord (62, ChordShape::triad, cMajor);
    CHECK (dm.count == 3 && dm.notes[0] == 62 && dm.notes[1] == 65 && dm.notes[2] == 69);
    const auto cmaj7 = buildChord (60, ChordShape::seventh, cMajor);
    CHECK (cmaj7.count == 4 && cmaj7.notes[3] == 71);
    const auto plain = buildChord (60, ChordShape::triad, Key {});
    CHECK (plain.count == 3 && plain.notes[1] == 64 && plain.notes[2] == 67);
    const auto bdim = buildChord (71, ChordShape::triad, cMajor);
    CHECK (bdim.notes[1] == 74 && bdim.notes[2] == 77);
    const auto pent = buildChord (62, ChordShape::triad, Key { 0, majorPentatonic });
    CHECK (pent.notes[1] == 66); // a pentatonic key is not stacked: plain major
    CHECK (buildChord (62, ChordShape::sus4, cMajor).notes[1] == 67);
    CHECK (buildChord (125, ChordShape::power, cMajor).count == 1); // 132 and 137 dropped
    CHECK (std::string (chordShapeLabel (ChordShape::seventh)) == "7th");
}

TEST (gp_scales_grid_layout)
{
    using namespace lynx::scales;
    const Key cMajor { 0, major }, aMinor { 9, naturalMinor };
    CHECK (scalePad (0, 0, 36, cMajor) == 36);
    CHECK (scalePad (1, 0, 36, cMajor) == 38);
    CHECK (scalePad (0, 1, 36, cMajor) == 41);
    for (int x = 0; x < 8; ++x)
        for (int y = 0; y < 8; ++y)
            CHECK (inScale (scalePad (x, y, 45, aMinor), aMinor));
    CHECK (scalePad (2, 1, 48, Key {}) == 55); // no scale: rows a fourth apart
    CHECK (scalePad (0, 0, 37, cMajor) == 36);  // the base note snaps into the key
}
