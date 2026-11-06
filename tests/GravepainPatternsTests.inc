// GravePain port, step 2: the MIDI pattern dataset (core/SmfRead, core/MidiPatterns). Ported from GravePain's
// MidiPatternsTests and SynthScalesAndMidiTests; the last test replaces GravePain's importer-check tool.

#include <filesystem>

#include "core/MidiPatterns.h"
#include "core/PatternTaste.h"
#include "core/SmfRead.h"

namespace gp_patterns
{
// a format-1 file at 96 ticks a beat: 120 BPM and 3/4, then C and E held a beat (E by running status, its off a
// zero-velocity on) and a GM kick on channel 10
inline std::vector<std::uint8_t> testFile()
{
    std::vector<std::uint8_t> b;
    const auto chunk = [&] (const char* id, const std::vector<std::uint8_t>& body) {
        b.insert (b.end(), id, id + 4);
        const auto n = body.size();
        b.insert (b.end(), { (std::uint8_t) (n >> 24), (std::uint8_t) (n >> 16), (std::uint8_t) (n >> 8), (std::uint8_t) n });
        b.insert (b.end(), body.begin(), body.end());
    };
    chunk ("MThd", { 0, 1, 0, 2, 0, 96 });
    chunk ("MTrk", { 0x00, 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20, 0x00, 0xFF, 0x58, 0x04, 0x03, 0x02, 0x18, 0x08, 0x00, 0xFF, 0x2F, 0x00 });
    chunk ("MTrk", { 0x00, 0x90, 60, 100, 0x00, 64, 90, 0x00, 0x99, 36, 120, 0x30, 0x89, 36, 0, 0x30, 0x90, 60, 0, 0x00, 0x80, 64, 0,
                     0x00, 0xFF, 0x2F, 0x00 });
    return b;
}

inline lynx::patterns::PatternRecord lineOf (const std::string& id, int bars, std::vector<int> rels)
{
    lynx::patterns::PatternRecord r;
    r.id = r.name = id;
    r.shape = lynx::patterns::line;
    r.bars = bars;
    r.rootNote = 60;
    r.role = "Lead";
    for (int step = 0, i = 0; step < bars * 16; step += 4, ++i)
    {
        const int rel = rels[(std::size_t) i % rels.size()];
        r.notes.push_back ({ step, 2, 60 + rel, rel, 100 });
    }
    return r;
}

inline int relAt (const lynx::patterns::PatternRecord& r, int step)
{
    for (const auto& n : r.notes)
        if (n.step == step)
            return n.rel;
    return -999;
}
} // namespace gp_patterns

TEST (gp_smf_reads_tempo_meter_notes)
{
    std::string error;
    const auto midi = lynx::smf::parse (gp_patterns::testFile(), error);
    CHECK (midi.has_value());
    CHECK (std::abs (midi->bpm - 120.0) < 0.01);
    CHECK (midi->numerator == 3 && midi->denominator == 4);
    int ons = 0;
    for (const auto& e : midi->events)
        ons += (e.status & 0xF0) == 0x90 && e.data2 > 0 ? 1 : 0;
    CHECK (ons == 3);
    CHECK (midi->events.back().tick == lynx::ppq); // 96 ticks = one beat at LYNX's PPQ
    const std::uint8_t junk[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    CHECK (! lynx::smf::parse (junk, error).has_value() && ! error.empty());
}

TEST (gp_patterns_file_becomes_a_pattern_per_part)
{
    using namespace lynx::patterns;
    std::string error;
    const auto midi = lynx::smf::parse (gp_patterns::testFile(), error);
    PatternSet set;
    CHECK (addFile (*midi, "X:/Stack/Test Verse Keys 01.mid", "Stack / Test", set) == 2);
    const PatternRecord* keys = nullptr;
    const PatternRecord* kick = nullptr;
    for (const auto& r : set.records)
        (r.part == 0 ? keys : kick) = &r;
    CHECK (keys != nullptr && kick != nullptr);
    CHECK (keys->role == "Chords" && keys->section == "Verse" && keys->bars == 1 && keys->notes.size() == 2);
    CHECK (keys->shape == mixed);
    CHECK (kick->role == "Kick" && kick->shape == rhythm && kick->notes[0].note == 36);
    CHECK (keys->notes[0].rel == 0 && keys->notes[1].rel == 4);

    const auto back = fromText (toText (set));
    CHECK (back.has_value() && back->records.size() == 2 && back->records[0].id == set.records[0].id);
    CHECK (back->records[0].notes == set.records[0].notes);
}

TEST (gp_patterns_roles_and_sections_from_names)
{
    using namespace lynx::patterns;
    CHECK (roleOf ("GTO Verse Bass 01", -1, false) == "Bass");
    CHECK (roleOf ("A Electric Snare 01", -1, false) == "Snare");
    CHECK (roleOf ("W Intro Kick 01", -1, false) == "Kick");
    CHECK (roleOf ("IB Bass Drum Crash 01", -1, false) == "Kick"); // a bass drum is a kick first
    CHECK (roleOf ("PJ Intro EP Stage", -1, false) == "Chords");
    CHECK (roleOf ("GTO Chorus Hats 01", -1, false) == "Hat");
    CHECK (roleOf ("", 33, false) == "Bass");
    CHECK (roleOf ("", -1, false) == "Other");
    CHECK (drumRole (42) == "Hat" && drumRole (38) == "Snare");
    CHECK (sectionOf ("GTO Chorus Hats 01") == "Chorus");
    CHECK (sectionOf ("W Breakdown Pad") == "Break");
    CHECK (sectionOf ("A Low Conga 05").empty());
    CHECK (isDrumRole ("Perc") && ! isDrumRole ("Bass"));
    CHECK (genreOf ("GravePain Originals / House") == "House");
    CHECK (loopBars ("AAAA") == 1 && loopBars ("ABAB") == 2 && loopBars ("AAAB") == 4 && loopBars ("A-A-") == 4);
}

TEST (gp_patterns_seeded_pick_and_lay)
{
    using namespace lynx::patterns;
    PatternSet set;
    auto a = gp_patterns::lineOf ("a", 4, { 0 });
    a.set = "A";
    a.section = "Verse";
    auto b = gp_patterns::lineOf ("b", 2, { 0, 7, 3 });
    b.set = "A";
    b.section = "Verse";
    b.fill = 1.0f;
    set.records = { a, b };
    const auto pool = poolFor (set, "Lead", "A", "Verse");
    CHECK (pool.size() == 2);
    CHECK (pick (pool, 7) == pick (pool, 7));
    CHECK (pick (pool, 7)->bars == 4); // four bars preferred
    CHECK (poolFor (set, "Lead", "B", "Chorus").size() == 2); // nothing in B: the role anywhere
    CHECK (poolFor (set, "Snare", "A", "Verse").empty());     // no drums at all: never a lead

    // a two-bar line laid on A (57) twice over four bars, snapped into A minor
    LayOptions o;
    o.rootNote = 57;
    o.keyRoot = 9;
    o.keyMinor = true;
    const auto notes = lay (b, o);
    CHECK (notes.size() == 16);
    CHECK (notes[0].key == 57 && notes[1].key == 64 && notes[2].key == 60); // A, E, C
    CHECK (notes[8].start == 2 * 4 * lynx::ppq && notes[8].key == 57);      // round again in bar three
    CHECK (notes[0].length == 2 * lynx::ticksPerStep);
}

TEST (gp_patterns_mix)
{
    using namespace lynx::patterns;
    using gp_patterns::lineOf;
    using gp_patterns::relAt;
    const auto a = lineOf ("a", 4, { 0 }), b = lineOf ("b", 4, { 7 });
    const auto halves = mix (a, &b, Mix::halves);
    CHECK (halves.notes.size() == 16 && relAt (halves, 28) == 0 && relAt (halves, 32) == 7 && halves.bars == 4);
    const auto bars = mix (a, &b, Mix::bars);
    CHECK (relAt (bars, 0) == 0 && relAt (bars, 16) == 7 && relAt (bars, 32) == 0 && relAt (bars, 48) == 7);
    const auto turn = mix (a, &b, Mix::turnaround);
    CHECK (relAt (turn, 32) == 0 && relAt (turn, 48) == 7);

    auto shortOne = lineOf ("s", 1, { 0 });
    shortOne.notes.back().length = 12;
    const auto joined = mix (shortOne, &b, Mix::halves);
    CHECK (relAt (joined, 16) == 0);
    for (const auto& n : joined.notes)
        if (n.step == 28)
            CHECK (n.length == 4); // stops at the end of its repeat

    auto sparse = lineOf ("p", 4, { 0 });
    std::erase_if (sparse.notes, [] (const auto& n) { return n.step % 8 != 0; });
    const auto c = lineOf ("c", 4, { 0, 2, 4 });
    const auto swapped = mix (sparse, &c, Mix::swapNotes);
    CHECK (swapped.notes.size() == 8 && relAt (swapped, 8) == 2 && relAt (swapped, 16) == 4 && relAt (swapped, 24) == 0);
    CHECK (mix (a, nullptr, Mix::halves).id == a.id); // a mix missing its second gives the first back
}

// Replaces GravePain's importer-check: a dataset built from the generated library (resources/midi-library) has a
// pattern from every file, and every pitched part in the role its file name says.
TEST (gp_patterns_generated_library)
{
    using namespace lynx::patterns;
    const std::string root = "resources/midi-library/GravePain Originals";
    if (! std::filesystem::is_directory (root))
    {
        std::printf ("  (skipped: %s not found)\n", root.c_str());
        return;
    }
    const auto groups = groupsOfFolder (root);
    CHECK (groups.size() == 14); // the styles
    const auto set = build (groups);
    std::set<std::string> used;
    int wrong = 0, files = 0;
    for (const auto& g : groups)
        files += (int) g.files.size();
    static const char* named[] = { "Bass", "Chords", "Pad", "Lead", "Pluck", "Bell" };
    for (const auto& r : set.records)
    {
        used.insert (r.source);
        const auto name = std::filesystem::path (r.source).stem().string();
        if (name.find (" Drums") != std::string::npos)
        {
            wrong += isDrumRole (r.role) ? 0 : 1;
            continue;
        }
        for (const char* w : named)
            if (name.find (std::string (" ") + w + " ") != std::string::npos)
            {
                if (r.role != w && wrong++ < 5)
                    std::printf ("  wrong role: %s -> %s\n", name.c_str(), r.role.c_str());
                break;
            }
    }
    CHECK (files == 2659);
    CHECK (wrong == 0);
    CHECK ((int) used.size() == files); // no file without a pattern
}

TEST (gp_taste_weights_and_store)
{
    using namespace lynx::taste;
    Taste t;
    CHECK (t.weight ("pattern:x") == 1.0f);
    t.add ({ "pattern:x", "recipe:Walk" }, kept);
    CHECK (t.score ("pattern:x") == 1.0f && t.weight ("pattern:x") == 1.5f);
    for (int i = 0; i < 30; ++i)
        t.add ({ "pattern:x" }, kept);
    CHECK (t.score ("pattern:x") == scoreCeiling && t.weight ("pattern:x") == weightCeiling);
    for (int i = 0; i < 30; ++i)
        t.add ({ "pattern:y" }, backed);
    CHECK (t.score ("pattern:y") == scoreFloor && t.weight ("pattern:y") == weightFloor);

    CHECK (Taste::fromText (t.toText()) == t);
    const auto dir = std::filesystem::temp_directory_path() / "lynx-gp-taste";
    const auto file = (dir / "taste.txt").string();
    CHECK (t.save (file));
    Taste back;
    CHECK (back.load (file) && back == t);
    std::filesystem::remove_all (dir);

    // weighted picks: deterministic, and a heavy item wins most rolls
    const std::vector<float> w { 0.1f, 4.0f, 1.0f };
    CHECK (Taste::pickWeighted (w, 42) == Taste::pickWeighted (w, 42));
    int heavy = 0;
    for (std::uint32_t s = 0; s < 1000; ++s)
        heavy += Taste::pickWeighted (w, s) == 1 ? 1 : 0;
    CHECK (heavy > 650 && heavy < 800); // 4 / 5.1 = 78 %
    CHECK (Taste::pickWeighted ({}, 1) == -1);
}
