#include "core/RiffStages.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

#include "core/NoteTools.h"
#include "core/Scales.h"

namespace lynx::prtools::riff
{
namespace
{
std::vector<int> valid (const Pattern& p, std::vector<int> indices)
{
    std::erase_if (indices, [&] (int i) { return i < 0 || i >= (int) p.notes.size(); });
    std::sort (indices.begin(), indices.end());
    indices.erase (std::unique (indices.begin(), indices.end()), indices.end());
    return indices;
}

// GravePain's riff generator (the dice's), so a seed means the same variation on every machine
struct Rng
{
    std::uint32_t state;
    explicit Rng (std::uint32_t seed) noexcept : state (seed * 2654435761u + 1013904223u) { if (state == 0) state = 1; }
    float next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) (state & 0xFFFFFF) / (float) 0x1000000;
    }
};
} // namespace

void mirrorKeepStarts (Pattern& pattern, const std::vector<int>& indices)
{
    const auto idx = valid (pattern, indices);
    std::vector<std::int64_t> starts;
    for (int i : idx)
        starts.push_back (pattern.notes[(std::size_t) i].start);
    std::sort (starts.begin(), starts.end());
    starts.erase (std::unique (starts.begin(), starts.end()), starts.end());
    std::map<std::int64_t, std::int64_t> to; // old start -> new start
    for (std::size_t k = 0; k < starts.size(); ++k)
        to[starts[starts.size() - 1 - k]] = starts[k];
    for (int i : idx)
    {
        auto& n = pattern.notes[(std::size_t) i];
        n.start = to[n.start];
    }
}

void mirrorPitch (Pattern& pattern, const std::vector<int>& indices, int home, int root, int scale)
{
    const scales::Key key { root, scale };
    for (int i : valid (pattern, indices))
    {
        auto& n = pattern.notes[(std::size_t) i];
        n.key = scales::snap (std::clamp (2 * home - n.key, 0, 127), key);
    }
}

void resetLevels (Pattern& pattern, const std::vector<int>& indices, float velocity)
{
    for (int i : valid (pattern, indices))
        pattern.notes[(std::size_t) i].velocity = std::clamp (velocity, 0.0f, 1.0f);
}

void shapeLengths (Pattern& pattern, const std::vector<int>& indices, const LengthOptions& o)
{
    const double multiply = std::clamp (o.multiply, 0.25f, 4.0f);
    const double variation = std::clamp (o.variation, 0.0f, 1.0f);
    Rng rng (o.seed);
    for (int i : valid (pattern, indices))
    {
        auto& n = pattern.notes[(std::size_t) i];
        double length = (double) n.length * multiply;
        const double r = rng.next() * 2.0 - 1.0; // drawn for every note, so the variation doesn't reshuffle
        if (variation > 0.0)
            length *= 1.0 + r * variation * 0.5;
        length -= (double) std::max<std::int64_t> (0, o.gap);
        n.length = std::max<std::int64_t> (1, std::llround (length));
    }
}

int chopToGrid (Pattern& pattern, const std::vector<int>& indices, std::int64_t grid)
{
    if (grid <= 0)
        return 0;
    std::vector<Note> added;
    for (int i : valid (pattern, indices))
    {
        auto& n = pattern.notes[(std::size_t) i];
        if (n.length <= grid)
            continue;
        const auto end = n.start + n.length;
        for (auto at = n.start + grid; at < end; at += grid)
        {
            Note hit = n;
            hit.start = at;
            hit.length = std::min (grid, end - at);
            added.push_back (hit);
        }
        n.length = grid;
    }
    pattern.notes.insert (pattern.notes.end(), added.begin(), added.end());
    pattern.sortNotes();
    return (int) added.size();
}

const char* grooveLabel (Groove g) noexcept
{
    static constexpr const char* names[] = { "Straight", "Swing 8ths", "Swing 16ths", "Laid back", "Pushed" };
    return names[std::clamp ((int) g, 0, grooveCount - 1)];
}

int grooveNudge (Groove g, int step) noexcept
{
    switch (g)
    {
        case Groove::swing8: return step % 4 == 2 ? 8 : 0;
        case Groove::swing16: return step % 2 == 1 ? 4 : 0;
        case Groove::laidBack: return 2;
        case Groove::pushed: return -2;
        case Groove::straight: break;
    }
    return 0;
}

void applyGroove (Pattern& pattern, const std::vector<int>& indices, Groove g, float timing)
{
    const float t = std::clamp (timing, 0.0f, 1.0f);
    for (int i : valid (pattern, indices))
    {
        auto& n = pattern.notes[(std::size_t) i];
        const auto step = (n.start + ticksPerStep / 2) / ticksPerStep;
        const auto shift = (std::int64_t) std::llround (grooveNudge (g, (int) (step % 16)) * t * ticksPerStep / 16.0);
        n.start = std::max<std::int64_t> (0, step * ticksPerStep + shift);
    }
}

std::string toText (const RiffSettings& s)
{
    std::ostringstream o;
    o << "seed=" << s.seed << "\nmirrorOn=" << s.mirrorOn << "\nmirrorTime=" << s.mirrorTime << "\nmirrorKeepStarts=" << s.mirrorKeepStarts
      << "\nmirrorPitch=" << s.mirrorPitch << "\nlevelsOn=" << s.levelsOn << "\nlevelsReset=" << s.levelsReset << "\nlevelsBipolar=" << s.levelsBipolar
      << "\nlevelsAmount=" << s.levelsAmount << "\nlevelsLength=" << s.levelsLength << "\nartOn=" << s.artOn << "\nartChop=" << s.artChop
      << "\nartMultiply=" << s.artMultiply << "\nartGap=" << s.artGap << "\nartVariation=" << s.artVariation << "\ngrooveOn=" << s.grooveOn
      << "\ngroove=" << (int) s.groove << "\ngrooveTiming=" << s.grooveTiming << "\narpOn=" << s.arpOn << "\narpPattern=" << s.arpPattern
      << "\narpRange=" << s.arpRange << "\n";
    return o.str();
}

RiffSettings fromText (const std::string& text)
{
    RiffSettings s;
    std::istringstream in (text);
    std::string line;
    while (std::getline (in, line))
    {
        const auto eq = line.find ('=');
        if (eq == std::string::npos)
            continue;
        const auto k = line.substr (0, eq), v = line.substr (eq + 1);
        try
        {
            const auto b = [&] { return v == "1"; };
            const auto f = [&] { return std::stof (v); };
            if (k == "seed") s.seed = (std::uint32_t) std::stoul (v);
            else if (k == "mirrorOn") s.mirrorOn = b();
            else if (k == "mirrorTime") s.mirrorTime = b();
            else if (k == "mirrorKeepStarts") s.mirrorKeepStarts = b();
            else if (k == "mirrorPitch") s.mirrorPitch = b();
            else if (k == "levelsOn") s.levelsOn = b();
            else if (k == "levelsReset") s.levelsReset = b();
            else if (k == "levelsBipolar") s.levelsBipolar = b();
            else if (k == "levelsAmount") s.levelsAmount = std::clamp (f(), 0.0f, 1.0f);
            else if (k == "levelsLength") s.levelsLength = std::clamp (f(), 0.0f, 1.0f);
            else if (k == "artOn") s.artOn = b();
            else if (k == "artChop") s.artChop = b();
            else if (k == "artMultiply") s.artMultiply = std::clamp (f(), 0.25f, 4.0f);
            else if (k == "artGap") s.artGap = std::clamp (f(), 0.0f, 0.5f);
            else if (k == "artVariation") s.artVariation = std::clamp (f(), 0.0f, 1.0f);
            else if (k == "grooveOn") s.grooveOn = b();
            else if (k == "groove") s.groove = (Groove) std::clamp (std::stoi (v), 0, grooveCount - 1);
            else if (k == "grooveTiming") s.grooveTiming = std::clamp (f(), 0.0f, 1.0f);
            else if (k == "arpOn") s.arpOn = b();
            else if (k == "arpPattern") s.arpPattern = std::max (0, std::stoi (v));
            else if (k == "arpRange") s.arpRange = std::clamp (std::stoi (v), 1, 3);
        }
        catch (const std::exception&)
        {
        }
    }
    return s;
}

void run (Pattern& pattern, const std::vector<int>& indices, const RiffSettings& s, int home, int root, int scale, std::int64_t grid)
{
    // the notes are tracked by identity through the stages (chopping adds notes and re-sorts)
    auto idx = valid (pattern, indices);
    if (s.mirrorOn)
    {
        if (s.mirrorTime)
        {
            if (s.mirrorKeepStarts)
                mirrorKeepStarts (pattern, idx);
            else
                notetools::flip (pattern, idx, true, false);
        }
        if (s.mirrorPitch)
            mirrorPitch (pattern, idx, home, root, scale);
    }
    if (s.levelsOn)
    {
        if (s.levelsReset)
            resetLevels (pattern, idx);
        notetools::RandomiseOptions r;
        r.velocity = s.levelsAmount * 48.0f / 127.0f; // at full amount a level moves up to 48 of 127
        r.length = s.levelsLength * 0.5f;
        r.bipolar = s.levelsBipolar;
        r.seed = s.seed;
        notetools::randomise (pattern, idx, r);
    }
    if (s.artOn)
    {
        LengthOptions l;
        l.multiply = s.artMultiply;
        l.gap = (std::int64_t) std::llround (s.artGap * ticksPerStep);
        l.variation = s.artVariation;
        l.seed = s.seed + 1;
        shapeLengths (pattern, idx, l);
        if (s.artChop)
        {
            const auto before = pattern.notes.size();
            // the chopped notes are the indices' notes and the hits they gave: tracked by their channel
            const int channel = idx.empty() ? 0 : pattern.notes[(std::size_t) idx.front()].channel;
            chopToGrid (pattern, idx, grid);
            if (pattern.notes.size() != before)
            {
                idx.clear();
                for (int i = 0; i < (int) pattern.notes.size(); ++i)
                    if (pattern.notes[(std::size_t) i].channel == channel)
                        idx.push_back (i);
            }
        }
    }
    if (s.grooveOn)
        applyGroove (pattern, idx, s.groove, s.grooveTiming);
    pattern.sortNotes();
}

std::string section (const std::string& toolSettings, const std::string& name)
{
    std::string out, line;
    std::istringstream in (toolSettings);
    bool inside = false;
    while (std::getline (in, line))
    {
        if (line.size() > 2 && line.front() == '[' && line.back() == ']')
        {
            inside = line == "[" + name + "]";
            continue;
        }
        if (inside)
            out += line + "\n";
    }
    return out;
}

std::string withSection (const std::string& toolSettings, const std::string& name, const std::string& body)
{
    // every block but this one, in order, then this one
    std::string out;
    std::istringstream in (toolSettings);
    std::string line, current;
    bool skip = false;
    while (std::getline (in, line))
    {
        if (line.size() > 2 && line.front() == '[' && line.back() == ']')
            skip = line == "[" + name + "]";
        if (! skip)
            out += line + "\n";
    }
    if (body.empty())
        return out;
    out += "[" + name + "]\n" + body;
    if (out.back() != '\n')
        out += '\n';
    return out;
}
} // namespace lynx::prtools::riff
