// GravePain port, step 4: the riff stages LYNX's riff machine lacks (core/RiffStages). Ported from GravePain's
// RiffChainTests where they apply (the arp stage is LYNX's own Arpeggiator, tested elsewhere).

#include "core/RiffStages.h"

namespace gp_riff
{
inline lynx::Pattern line (std::vector<int> keys, std::int64_t every = lynx::ppq, std::int64_t length = lynx::ppq / 2)
{
    lynx::Pattern p;
    for (std::size_t i = 0; i < keys.size(); ++i)
    {
        lynx::Note n;
        n.start = (std::int64_t) i * every;
        n.length = length;
        n.key = keys[i];
        n.velocity = 0.5f + 0.1f * (float) (i % 3);
        p.notes.push_back (n);
    }
    return p;
}
inline std::vector<int> all (const lynx::Pattern& p)
{
    std::vector<int> v;
    for (int i = 0; i < (int) p.notes.size(); ++i)
        v.push_back (i);
    return v;
}
} // namespace gp_riff

TEST (gp_riff_mirror_keeps_starts_and_turns_pitch)
{
    using namespace lynx::prtools::riff;
    auto p = gp_riff::line ({ 60, 62, 64, 67 });
    mirrorKeepStarts (p, gp_riff::all (p));
    p.sortNotes();
    CHECK (p.notes[0].start == 0 && p.notes[0].key == 67 && p.notes[3].key == 60); // the rhythm stays, the order turns
    CHECK (p.notes[1].start == lynx::ppq && p.notes[1].key == 64);

    auto q = gp_riff::line ({ 60, 62, 64 });
    mirrorPitch (q, gp_riff::all (q), 62, 0, 1); // about D in C major
    CHECK (q.notes[0].key == 64 && q.notes[1].key == 62 && q.notes[2].key == 60);
    auto r = gp_riff::line ({ 61 });
    mirrorPitch (r, { 0 }, 62, 0, 1); // 63 is not in C major: snaps down to D
    CHECK (r.notes[0].key == 62);
}

TEST (gp_riff_levels_and_lengths)
{
    using namespace lynx::prtools::riff;
    auto p = gp_riff::line ({ 60, 62, 64 });
    resetLevels (p, gp_riff::all (p));
    for (const auto& n : p.notes)
        CHECK (std::abs (n.velocity - 100.0f / 127.0f) < 1e-6f);

    auto l = gp_riff::line ({ 60, 62 }, lynx::ppq, lynx::ppq / 2);
    LengthOptions o;
    o.multiply = 2.0f;
    shapeLengths (l, gp_riff::all (l), o);
    CHECK (l.notes[0].length == lynx::ppq); // past 100 %, which the articulator does not go
    o.multiply = 1.0f;
    o.gap = 100;
    shapeLengths (l, gp_riff::all (l), o);
    CHECK (l.notes[0].length == lynx::ppq - 100);
    o.gap = 0;
    o.variation = 1.0f;
    auto v1 = gp_riff::line ({ 60, 62, 64, 65 }), v2 = v1;
    shapeLengths (v1, gp_riff::all (v1), o);
    shapeLengths (v2, gp_riff::all (v2), o);
    CHECK (v1.notes == v2.notes); // seeded

    auto c = gp_riff::line ({ 60 }, lynx::ppq, 4 * lynx::ppq);
    CHECK (chopToGrid (c, { 0 }, lynx::ppq) == 3);
    CHECK (c.notes.size() == 4 && c.notes[3].start == 3 * lynx::ppq && c.notes[3].length == lynx::ppq);
}

TEST (gp_riff_groove_templates)
{
    using namespace lynx::prtools::riff;
    CHECK (grooveNudge (Groove::swing8, 2) == 8 && grooveNudge (Groove::swing8, 4) == 0);
    CHECK (grooveNudge (Groove::swing16, 3) == 4 && grooveNudge (Groove::pushed, 0) == -2);
    auto p = gp_riff::line ({ 60, 60, 60, 60 }, lynx::ppq / 2);
    applyGroove (p, gp_riff::all (p), Groove::swing8, 1.0f);
    CHECK (p.notes[0].start == 0);
    CHECK (p.notes[1].start == lynx::ppq / 2 + lynx::ticksPerStep / 2); // the off-eighth half a step late
    auto q = gp_riff::line ({ 60 });
    applyGroove (q, { 0 }, Groove::pushed, 1.0f);
    CHECK (q.notes[0].start == 0); // never before the start
}

TEST (gp_riff_settings_round_trip_and_run)
{
    using namespace lynx::prtools::riff;
    RiffSettings s;
    s.seed = 777;
    s.mirrorOn = true;
    s.mirrorKeepStarts = true;
    s.levelsOn = true;
    s.levelsReset = true;
    s.artOn = true;
    s.artMultiply = 1.5f;
    s.grooveOn = true;
    s.groove = Groove::swing16;
    CHECK (fromText (toText (s)) == s);
    CHECK (fromText ("") == RiffSettings {});

    auto a = gp_riff::line ({ 60, 62, 64, 65, 67 }), b = a;
    run (a, gp_riff::all (a), s, 62, 0, 1, lynx::ticksPerStep);
    run (b, gp_riff::all (b), s, 62, 0, 1, lynx::ticksPerStep);
    CHECK (a.notes == b.notes); // the same settings, the same riff

    std::string tools = withSection ("", "riff", toText (s));
    tools = withSection (tools, "chords", "0:-1:-1:0:0 3:-1:-1:0:0");
    CHECK (fromText (section (tools, "riff")) == s);
    CHECK (section (tools, "chords") == "0:-1:-1:0:0 3:-1:-1:0:0\n");
    tools = withSection (tools, "riff", "");
    CHECK (section (tools, "riff").empty() && ! section (tools, "chords").empty());

    lynx::Pattern pattern;
    pattern.toolSettings = tools;
    CHECK (pattern.toolSettings == tools);
}
