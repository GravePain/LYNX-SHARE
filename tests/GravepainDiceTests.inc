// GravePain port, step 3: Chord Dice, chord bars and Melody Dice (core/ChordDice). Ported from GravePain's
// ChordTests, MelodyDiceTests and MidiPatternsTests (chord shapes).

#include "core/ChordDice.h"

TEST (gp_dice_chords_from_the_key)
{
    using namespace lynx::prtools::dice;
    const auto pcs = [] (int degree, int tonic, bool minor, Shape s = Shape::triad) {
        ChordBar b = ChordBar::of (degree);
        b.shape = s;
        return pitchClasses (b, tonic, minor);
    };
    CHECK ((pcs (0, 0, false) == std::vector<int> { 0, 4, 7 }));
    CHECK ((pcs (5, 0, false) == std::vector<int> { 9, 0, 4 }));
    CHECK ((pcs (4, 0, false) == std::vector<int> { 7, 11, 2 }));
    CHECK ((pcs (0, 9, true) == std::vector<int> { 9, 0, 4 }));
    CHECK ((pcs (4, 0, false, Shape::seventh) == std::vector<int> { 7, 11, 2, 5 }));
    CHECK ((pcs (0, 0, false, Shape::sus4) == std::vector<int> { 0, 5, 7 }));
    CHECK ((pcs (0, 0, false, Shape::fifth) == std::vector<int> { 0, 7 }));
    for (bool minor : { false, true })
        for (const auto& p : progressions (minor))
        {
            CHECK (p.degrees.size() == 4 || p.degrees.size() == 2);
            CHECK (p.bars().size() == 4 && p.weight >= 1 && p.weight <= 3 && p.minor == minor);
        }
}

TEST (gp_dice_chord_names)
{
    using namespace lynx::prtools::dice;
    const auto name = [] (int degree, Shape s, int tonic = 0, bool minor = false) {
        ChordBar b = ChordBar::of (degree);
        b.shape = s;
        return chordName (b, tonic, minor);
    };
    CHECK (name (0, Shape::triad) == "C" && name (3, Shape::triad) == "F" && name (5, Shape::triad) == "Am" && name (6, Shape::triad) == "Bdim");
    CHECK (name (4, Shape::seventh) == "G7" && name (1, Shape::seventh) == "Dm7" && name (6, Shape::seventh) == "Bm7b5");
    CHECK (name (0, Shape::seventh) == "Cmaj7" && name (0, Shape::sus4) == "Csus4" && name (0, Shape::fifth) == "C5");
    CHECK (name (0, Shape::ninth) == "Cmaj9" && name (4, Shape::ninth) == "G9" && name (5, Shape::sixth) == "Am6");
    ChordBar own = ChordBar::of (0);
    own.tonic = 9;
    own.minor = 1;
    CHECK (chordName (own, 0, false) == "Am");
    CHECK (chordNames (barsOf (Progression { "x", { 0, 5, 2, 6 }, 1, true }), 5, true) == "Fm   C#   G#   D#");
    CHECK (chordNames ({ ChordBar::of (0), own }, 0, false) == "C   Am (Am)");
}

TEST (gp_dice_progressions_seeded_and_weighted)
{
    using namespace lynx::prtools::dice;
    CHECK (&pickProgression (4821, true) == &pickProgression (4821, true));
    std::map<std::string, int> count;
    for (std::uint32_t seed = 0; seed < 3000; ++seed)
        ++count[pickProgression (seed, true).name];
    CHECK (count["i VI III VII"] > count["i iv VII III"] * 2); // what songs lean on, the dice lean on
    CHECK (count.size() == progressions (true).size());          // everything can come up
}

TEST (gp_dice_chords_voiced_close_and_held)
{
    using namespace lynx::prtools::dice;
    const auto keys = ladder (48, 84);
    for (bool minor : { false, true })
        for (const auto& p : progressions (minor))
            for (int tonic : { 0, 5, 9 })
            {
                const auto bars = barsOf (p);
                const auto notes = placeChords (bars, keys, tonic, minor, {});
                float lastCentre = -1.0f;
                for (int b = 0; b < 4; ++b)
                {
                    std::vector<int> on;
                    for (const auto& n : notes)
                        if (n.start == (std::int64_t) b * 4 * lynx::ppq)
                            on.push_back (n.key);
                    const bool carried = b > 0 && bars[(std::size_t) b] == bars[(std::size_t) b - 1];
                    if (carried)
                    {
                        CHECK (on.empty()); // a chord held over the bar is not struck again
                        continue;
                    }
                    CHECK (on.size() == 3);
                    const auto pcs = pitchClasses (bars[(std::size_t) b], tonic, minor);
                    float centre = 0.0f;
                    for (int k : on)
                    {
                        CHECK (std::find (pcs.begin(), pcs.end(), k % 12) != pcs.end());
                        centre += (float) k / 3.0f;
                    }
                    if (lastCentre >= 0.0f)
                        CHECK (std::abs (centre - lastCentre) <= 5.0f); // the hand barely moves
                    lastCentre = centre;
                }
            }
    // C held two bars, then G7 from bar three in second inversion
    ChordBar g7 = ChordBar::of (4);
    g7.shape = Shape::seventh;
    const std::vector<ChordBar> bars { ChordBar::of (0), ChordBar::of (0), g7, g7 };
    const auto notes = placeChords (bars, keys, 0, false, {});
    CHECK (notes.size() == 7 && notes[0].length == 8 * lynx::ppq && notes[0].key == 48);
    auto inverted = bars;
    inverted[0].inversion = 1;
    const auto inv = placeChords (inverted, keys, 0, false, {});
    CHECK (inv[0].key == 52 && inv[2].key == 60); // E G C
    CHECK (placeChords (bars, {}, 0, false, {}).empty());
    CHECK (barsFromText (toText (inverted)) == inverted);
}

TEST (gp_dice_melody_plain)
{
    using namespace lynx::prtools::dice;
    const auto keys = ladder (60, 84, 0, false);
    const auto roll = [&] (std::uint32_t seed, int resolution = 2, int home = 0) {
        MelodyOptions o;
        o.seed = seed;
        o.resolution = resolution;
        o.home = home;
        return rollMelody (keys, o);
    };
    CHECK (roll (4821) == roll (4821));
    int different = 0;
    for (std::uint32_t seed = 1; seed <= 20; ++seed)
        different += roll (seed) != roll (seed + 100) ? 1 : 0;
    CHECK (different == 20);
    const std::int64_t step = lynx::ppq / 4;
    for (std::uint32_t seed = 1; seed <= 30; ++seed)
        for (int res : { 1, 2, 4 })
        {
            const auto notes = roll (seed, res, 2);
            CHECK (notes.size() > 3);
            for (std::size_t i = 0; i < notes.size(); ++i)
            {
                CHECK (notes[i].start % (res * step) == 0);
                CHECK (std::find (keys.begin(), keys.end(), notes[i].key) != keys.end());
                if (i > 0)
                    CHECK (notes[i].start > notes[i - 1].start); // one note at a time
            }
            CHECK (notes.front().start == 0 && notes.front().key == keys[2]); // starts at home
            if (notes.back().start >= 56 * step)
                CHECK (notes.back().key == keys[2]); // ends at home
        }
    CHECK (rollMelody ({}, MelodyOptions {}).empty());
}

TEST (gp_dice_melody_over_chords)
{
    using namespace lynx::prtools::dice;
    const auto keys = ladder (60, 84, 0, true);
    const std::int64_t step = lynx::ppq / 4;
    for (const auto& p : progressions (true))
        for (std::uint32_t seed : { 57067u, 4821u, 3187u })
        {
            const auto bars = barsOf (p);
            const auto tones = chordTones (bars, keys, 0, true);
            MelodyOptions o;
            o.seed = seed;
            const auto notes = rollMelody (keys, o, &tones);
            CHECK (notes.size() > 2);
            CHECK (notes.front().start == 0);
            for (const auto& n : notes)
                if (n.start % (4 * step) == 0)
                {
                    const int rung = (int) (std::find (keys.begin(), keys.end(), n.key) - keys.begin());
                    const auto& t = tones[(std::size_t) (n.start / (16 * step))];
                    CHECK (std::find (t.begin(), t.end(), rung) != t.end()); // on the beat, on the chord
                }
            CHECK (rollMelody (keys, o, &tones) == notes);
            o.landing = Landing::atTheEnd;
            const auto landed = rollMelody (keys, o, &tones);
            bool onThird = false, roomAfter = true;
            for (const auto& n : landed)
            {
                onThird = onThird || n.start == (48 + 8) * step;
                roomAfter = roomAfter && n.start <= (48 + 8) * step;
            }
            CHECK (onThird && roomAfter);
        }
}
