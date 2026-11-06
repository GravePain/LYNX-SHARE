#include "core/ChordDice.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

namespace lynx::prtools::dice
{
namespace
{
constexpr int majorScale[] = { 0, 2, 4, 5, 7, 9, 11 };
constexpr int minorScale[] = { 0, 2, 3, 5, 7, 8, 10 };
constexpr const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
constexpr int stepsPerBar = 16, stepsPerBeat = 4, steps = 64;

int pc (int n) noexcept { return ((n % 12) + 12) % 12; }

// GravePain's dice generator: a seed means the same roll on every machine
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

Progression P (const char* name, int weight, bool minor, std::vector<int> degrees)
{
    return { name, std::move (degrees), weight, minor };
}

void keyOf (const ChordBar& bar, int laneTonic, bool laneMinor, int& tonic, bool& minor) noexcept
{
    tonic = bar.tonic >= 0 ? bar.tonic % 12 : pc (laneTonic);
    minor = bar.minor >= 0 ? bar.minor == 1 : laneMinor;
}

std::vector<int> classes (int degree, int tonic, bool minor, Shape shape)
{
    const int* scale = minor ? minorScale : majorScale;
    const int d = ((degree % 7) + 7) % 7;
    const auto at = [&] (int step) { return pc (tonic + scale[(d + step) % 7]); };
    switch (shape)
    {
        case Shape::seventh: return { at (0), at (2), at (4), at (6) };
        case Shape::sus2: return { at (0), at (1), at (4) };
        case Shape::sus4: return { at (0), at (3), at (4) };
        case Shape::fifth: return { at (0), at (4) };
        case Shape::sixth: return { at (0), at (2), at (4), at (5) };
        case Shape::add9: return { at (0), at (2), at (4), at (1) };
        case Shape::ninth: return { at (0), at (2), at (4), at (6), at (1) };
        case Shape::eleventh: return { at (0), at (4), at (6), at (1), at (3) };   // the third left out, as usually voiced
        case Shape::thirteenth: return { at (0), at (2), at (6), at (1), at (5) }; // the fifth and the 11th left out
        case Shape::triad: break;
    }
    return { at (0), at (2), at (4) };
}

// mostly a step, sometimes two, now and then the same note or a leap; it turns round at the ends of the ladder
int move (int at, int count, Rng& rng)
{
    if (count <= 1)
        return 0;
    const float r = rng.next();
    const int size = r < 0.15f ? 0 : r < 0.65f ? 1 : r < 0.90f ? 2 : 3 + (rng.next() < 0.5f ? 0 : 1);
    int next = at + (rng.next() < 0.5f ? -size : size);
    if (next < 0)
        next = -next;
    if (next > count - 1)
        next = 2 * (count - 1) - next;
    return std::clamp (next, 0, count - 1);
}

int nearest (const std::vector<int>& tones, int at, int otherwise)
{
    if (tones.empty())
        return otherwise;
    int best = tones[0];
    for (int t : tones)
        if (std::abs (t - at) < std::abs (best - at))
            best = t;
    return best;
}

int onward (const std::vector<int>& tones, int at, int direction)
{
    int best = -1;
    for (int t : tones)
    {
        if (t == at || (direction > 0 && t < at) || (direction < 0 && t > at))
            continue;
        if (best < 0 || std::abs (t - at) < std::abs (best - at))
            best = t;
    }
    return best >= 0 ? best : nearest (tones, at, at);
}

std::vector<bool> rhythmOf (int lines, Rng& rng, int resolution, std::vector<int>* notes, int home, int count)
{
    std::vector<bool> rhythm ((std::size_t) (2 * lines));
    int at = home;
    for (int i = 0; i < 2 * lines; ++i)
    {
        const int step = i * resolution;
        const float chance = i == 0 ? 1.0f : step % stepsPerBeat == 0 ? 0.72f : step % 2 == 0 ? 0.42f : 0.22f;
        if (notes == nullptr)
        {
            rhythm[(std::size_t) i] = rng.next() < chance;
            continue;
        }
        if (rng.next() >= chance)
            continue;
        rhythm[(std::size_t) i] = true;
        if (i > 0)
            at = move (at, count, rng);
        (*notes)[(std::size_t) i] = at;
    }
    return rhythm;
}
} // namespace

const char* shapeLabel (Shape shape) noexcept
{
    static constexpr const char* names[] = { "Triad", "7th", "Sus2", "Sus4", "Fifth", "6th", "Add9", "9th", "11th", "13th" };
    return names[std::clamp ((int) shape, 0, shapeCount - 1)];
}

const char* inversionLabel (int inversion) noexcept
{
    static constexpr const char* names[] = { "Root", "1st inv", "2nd inv" };
    return names[std::clamp (inversion, 0, 2)];
}

std::vector<int> Progression::bars() const
{
    if (degrees.size() == 2)
        return { degrees[0], degrees[0], degrees[1], degrees[1] };
    std::vector<int> b (4, 0);
    for (std::size_t i = 0; i < 4 && i < degrees.size(); ++i)
        b[i] = degrees[i];
    return b;
}

const std::vector<Progression>& progressions (bool minor)
{
    // the loops songs are built on (GravePain's table, written by hand from a public set of real songs' chords): a
    // handful cover a great deal of music, two-chord loops are as common as four-chord ones
    static const std::vector<Progression> majorTable {
        P ("I IV I V", 3, false, { 0, 3, 0, 4 }), P ("I V vi IV", 3, false, { 0, 4, 5, 3 }), P ("vi IV I V", 3, false, { 5, 3, 0, 4 }),
        P ("IV I V vi", 2, false, { 3, 0, 4, 5 }), P ("I vi IV V", 3, false, { 0, 5, 3, 4 }), P ("I V IV V", 2, false, { 0, 4, 3, 4 }),
        P ("ii IV I V", 2, false, { 1, 3, 0, 4 }), P ("IV I vi V", 2, false, { 3, 0, 5, 4 }), P ("I IV vi V", 2, false, { 0, 3, 5, 4 }),
        P ("I vi ii V", 1, false, { 0, 5, 1, 4 }), P ("I IV V IV", 1, false, { 0, 3, 4, 3 }), P ("I iii IV V", 1, false, { 0, 2, 3, 4 }),
        P ("I IV", 3, false, { 0, 3 }), P ("I V", 2, false, { 0, 4 }), P ("IV V", 1, false, { 3, 4 }), P ("I vi", 1, false, { 0, 5 }),
    };
    static const std::vector<Progression> minorTable {
        P ("i VI III VII", 3, true, { 0, 5, 2, 6 }), P ("III VII i VI", 2, true, { 2, 6, 0, 5 }), P ("VI III VII i", 2, true, { 5, 2, 6, 0 }),
        P ("i VII VI VII", 3, true, { 0, 6, 5, 6 }), P ("III i VI VII", 2, true, { 2, 0, 5, 6 }), P ("VI III i VII", 2, true, { 5, 2, 0, 6 }),
        P ("VI VII i III", 2, true, { 5, 6, 0, 2 }), P ("i III VII VI", 2, true, { 0, 2, 6, 5 }), P ("i VII III VI", 1, true, { 0, 6, 2, 5 }),
        P ("i iv VII III", 1, true, { 0, 3, 6, 2 }), P ("iv VI i VII", 1, true, { 3, 5, 0, 6 }), P ("i v VI VII", 1, true, { 0, 4, 5, 6 }),
        P ("i VII", 3, true, { 0, 6 }), P ("i VI", 3, true, { 0, 5 }), P ("VI VII", 2, true, { 5, 6 }), P ("i iv", 1, true, { 0, 3 }),
    };
    return minor ? minorTable : majorTable;
}

const Progression& pickProgression (std::uint32_t seed, bool minor)
{
    const auto& table = progressions (minor);
    int total = 0;
    for (const auto& p : table)
        total += p.weight;
    Rng rng (seed);
    float at = rng.next() * (float) total;
    for (const auto& p : table)
    {
        at -= (float) p.weight;
        if (at < 0.0f)
            return p;
    }
    return table.back();
}

std::vector<ChordBar> barsOf (const Progression& p)
{
    std::vector<ChordBar> bars;
    for (int d : p.bars())
        bars.push_back (ChordBar::of (d));
    return bars;
}

std::vector<int> pitchClasses (const ChordBar& bar, int laneTonic, bool laneMinor)
{
    int tonic = 0;
    bool minor = false;
    keyOf (bar, laneTonic, laneMinor, tonic, minor);
    return classes (bar.degree, tonic, minor, bar.shape);
}

std::string chordName (const ChordBar& bar, int laneTonic, bool laneMinor)
{
    int tonic = 0;
    bool minor = false;
    keyOf (bar, laneTonic, laneMinor, tonic, minor);
    const auto pcs = classes (bar.degree, tonic, minor, bar.shape);
    const std::string root = noteNames[pcs[0]];
    switch (bar.shape)
    {
        case Shape::sus2: return root + "sus2";
        case Shape::sus4: return root + "sus4";
        case Shape::fifth: return root + "5";
        default: break;
    }
    const auto triad = classes (bar.degree, tonic, minor, Shape::triad);
    const int t3 = pc (triad[1] - triad[0]), t5 = pc (triad[2] - triad[0]);
    const std::string q = t5 == 6 ? "dim" : t3 == 3 ? "m" : "";
    const auto sev = classes (bar.degree, tonic, minor, Shape::seventh);
    const int seventh = pc (sev[3] - sev[0]);
    switch (bar.shape)
    {
        case Shape::sixth: return root + q + "6";
        case Shape::add9: return root + q + "add9";
        case Shape::ninth: return root + q + (seventh == 11 && q != "m" ? "maj9" : "9");
        case Shape::eleventh: return root + q + "11";
        case Shape::thirteenth: return root + q + (seventh == 11 && q != "m" ? "maj13" : "13");
        case Shape::seventh:
            if (t5 == 6)
                return root + (seventh == 10 ? "m7b5" : "dim7");
            if (t3 == 3)
                return root + (seventh == 11 ? "m(maj7)" : "m7");
            return root + (seventh == 11 ? "maj7" : "7");
        default: break;
    }
    return root + q;
}

std::string chordNames (const std::vector<ChordBar>& bars, int laneTonic, bool laneMinor)
{
    std::string out;
    for (const auto& bar : bars)
    {
        if (! out.empty())
            out += "   ";
        out += chordName (bar, laneTonic, laneMinor);
        if (bar.ownKey())
        {
            int t = 0;
            bool m = false;
            keyOf (bar, laneTonic, laneMinor, t, m);
            out += std::string (" (") + noteNames[t] + (m ? "m" : "") + ")";
        }
    }
    return out;
}

std::vector<int> ladder (int low, int high, int tonic, bool minor)
{
    std::vector<int> keys;
    const int* scale = minor ? minorScale : majorScale;
    for (int k = std::max (0, low); k <= std::min (127, high); ++k)
    {
        if (tonic >= 0 && std::find (scale, scale + 7, pc (k - tonic)) == scale + 7)
            continue;
        keys.push_back (k);
    }
    return keys;
}

std::vector<std::vector<int>> voice (const std::vector<ChordBar>& bars, const std::vector<int>& keys, int laneTonic, bool laneMinor)
{
    std::vector<std::vector<int>> voiced;
    float centre = -1.0f;
    for (std::size_t b = 0; b < bars.size(); ++b)
    {
        if (b > 0 && bars[b] == bars[b - 1])
        {
            voiced.push_back (voiced.back()); // the same chord, held
            continue;
        }
        const auto pcs = pitchClasses (bars[b], laneTonic, laneMinor);
        std::vector<int> rungs;
        const auto has = [&] (int i) { return std::find (rungs.begin(), rungs.end(), i) != rungs.end(); };
        if (centre < 0.0f)
        {
            int root = -1;
            for (int i = 0; i < (int) keys.size() && root < 0; ++i)
                if (pc (keys[(std::size_t) i]) == pcs[0])
                    root = i;
            if (root >= 0)
                rungs.push_back (root);
            for (std::size_t n = 1; n < pcs.size(); ++n)
                for (int i = std::max (0, root); i < (int) keys.size(); ++i)
                    if (pc (keys[(std::size_t) i]) == pcs[n])
                    {
                        rungs.push_back (i);
                        break;
                    }
        }
        else
        {
            for (int p : pcs)
            {
                int best = -1;
                float bestGap = 1.0e9f;
                for (int i = 0; i < (int) keys.size(); ++i)
                {
                    if (pc (keys[(std::size_t) i]) != p || has (i))
                        continue;
                    const float gap = std::abs ((float) keys[(std::size_t) i] - centre);
                    if (gap < bestGap)
                    {
                        bestGap = gap;
                        best = i;
                    }
                }
                if (best >= 0)
                    rungs.push_back (best);
            }
        }
        std::sort (rungs.begin(), rungs.end());
        for (int lift = 0; lift < bars[b].inversion && rungs.size() > 1; ++lift)
        {
            // an inversion: the lowest note goes up to the first rung above the chord that plays the same note
            const int low = rungs.front(), top = rungs.back();
            int up = -1;
            for (int i = top + 1; i < (int) keys.size() && up < 0; ++i)
                if (pc (keys[(std::size_t) i]) == pc (keys[(std::size_t) low]) && ! has (i))
                    up = i;
            if (up < 0)
                break;
            rungs.erase (rungs.begin());
            rungs.push_back (up);
        }
        if (! rungs.empty())
        {
            float sum = 0.0f;
            for (int i : rungs)
                sum += (float) keys[(std::size_t) i];
            centre = sum / (float) rungs.size();
        }
        voiced.push_back (std::move (rungs));
    }
    return voiced;
}

std::vector<std::vector<int>> chordTones (const std::vector<ChordBar>& bars, const std::vector<int>& keys, int laneTonic, bool laneMinor)
{
    std::vector<std::vector<int>> tones;
    for (const auto& bar : bars)
    {
        const auto pcs = pitchClasses (bar, laneTonic, laneMinor);
        auto& list = tones.emplace_back();
        for (int i = 0; i < (int) keys.size(); ++i)
            if (std::find (pcs.begin(), pcs.end(), pc (keys[(std::size_t) i])) != pcs.end())
                list.push_back (i);
    }
    return tones;
}

std::vector<Note> placeChords (const std::vector<ChordBar>& bars, const std::vector<int>& keys, int laneTonic, bool laneMinor, const PlaceOptions& o)
{
    std::vector<Note> notes;
    if (bars.empty() || keys.empty())
        return notes;
    const auto voiced = voice (bars, keys, laneTonic, laneMinor);
    for (std::size_t b = 0; b < bars.size(); ++b)
    {
        if (b > 0 && bars[b] == bars[b - 1])
            continue; // still held from the bar before
        std::int64_t length = o.barLength;
        for (std::size_t next = b + 1; next < bars.size() && bars[next] == bars[b]; ++next)
            length += o.barLength;
        for (int rung : voiced[b])
        {
            Note n;
            n.start = o.start + (std::int64_t) b * o.barLength;
            n.length = length;
            n.channel = o.channel;
            n.key = keys[(std::size_t) rung];
            n.velocity = o.velocity;
            notes.push_back (n);
        }
    }
    std::stable_sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b) { return a.start != b.start ? a.start < b.start : a.key < b.key; });
    return notes;
}

std::vector<Note> rollMelody (const std::vector<int>& keys, const MelodyOptions& o, const std::vector<std::vector<int>>* tones)
{
    std::map<int, int> grid; // step -> rung: one note at a time
    const int count = (int) keys.size();
    if (count == 0)
        return {};
    const int resolution = o.resolution == 1 || o.resolution == 2 ? o.resolution : 4;
    const int home = std::clamp (o.home, 0, count - 1);
    const int lines = stepsPerBar / resolution;
    Rng rng (o.seed);
    if (tones != nullptr && tones->size() >= 4)
    {
        const auto& t = *tones;
        const auto rhythm = rhythmOf (lines, rng, resolution, nullptr, home, count);
        int lastStep = -1, lastBar = 0;
        int at = nearest (t[0], home, home), before = at;
        const int landOn = 2 * stepsPerBeat; // the third beat of a bar
        for (int line = 0; line < 4 * lines; ++line)
        {
            const int step = line * resolution, inBar = step / stepsPerBar;
            const bool lands = o.landing != Landing::none && (inBar == 3 || (o.landing == Landing::twice && inBar == 1));
            const int past = step - inBar * stepsPerBar - landOn;
            if (lands && past > 0)
                continue;
            if (lands && past == 0)
            {
                const int target = onward (t[(std::size_t) inBar], at, at - before);
                before = at;
                at = target;
                grid[step] = at;
                lastStep = step;
                lastBar = inBar;
                continue;
            }
            const bool pickup = lands && past == -resolution; // the landing is arrived at, not jumped to
            if (! rhythm[(std::size_t) (line % (int) rhythm.size())] && ! pickup)
                continue;
            before = at;
            if (line != 0)
                at = move (at, count, rng);
            if (step % stepsPerBeat == 0)
                at = nearest (t[(std::size_t) inBar], at, at); // on a beat the tune stands on the chord
            grid[step] = at;
            lastStep = step;
            lastBar = inBar;
        }
        if (o.landing == Landing::none && lastStep >= steps - stepsPerBar / 2)
            grid[lastStep] = nearest (t[(std::size_t) lastBar], home, home); // it ends on the chord, near home
    }
    else
    {
        std::vector<int> notes ((std::size_t) (2 * lines));
        const auto rhythm = rhythmOf (lines, rng, resolution, &notes, home, count);
        for (int half = 0; half < 2; ++half)
            for (int i = 0; i < 2 * lines; ++i)
            {
                if (! rhythm[(std::size_t) i])
                    continue;
                int note = notes[(std::size_t) i];
                // the second time round, the last half bar is new: the tune answers itself
                if (half == 1 && i >= 2 * lines - lines / 2)
                    note = std::clamp (home + (int) std::lround ((rng.next() - 0.5f) * 2.0f), 0, count - 1);
                grid[half * 2 * stepsPerBar + i * resolution] = note;
            }
        // it ends where it began: the last note of the four bars is the home note
        for (int step = steps - resolution; step >= steps - stepsPerBar / 2; step -= resolution)
            if (auto it = grid.find (step); it != grid.end())
            {
                it->second = home;
                break;
            }
    }
    std::vector<Note> out;
    const std::int64_t stepTicks = o.place.barLength / stepsPerBar;
    for (auto it = grid.begin(); it != grid.end(); ++it)
    {
        const auto next = std::next (it);
        const int end = next == grid.end() ? steps : next->first;
        Note n;
        n.start = o.place.start + it->first * stepTicks;
        n.length = std::max<std::int64_t> (1, std::min (resolution, end - it->first) * stepTicks);
        n.channel = o.place.channel;
        n.key = keys[(std::size_t) it->second];
        n.velocity = o.place.velocity;
        out.push_back (n);
    }
    return out;
}

std::string toText (const std::vector<ChordBar>& bars)
{
    std::ostringstream o;
    for (std::size_t i = 0; i < bars.size(); ++i)
        o << (i ? " " : "") << bars[i].degree << ':' << bars[i].tonic << ':' << bars[i].minor << ':' << (int) bars[i].shape << ':' << bars[i].inversion;
    return o.str();
}

std::vector<ChordBar> barsFromText (const std::string& text)
{
    std::vector<ChordBar> bars;
    std::istringstream in (text);
    std::string token;
    while (in >> token)
    {
        ChordBar b;
        int shape = 0;
        char c1 = 0, c2 = 0, c3 = 0, c4 = 0;
        std::istringstream t (token);
        if (! (t >> b.degree >> c1 >> b.tonic >> c2 >> b.minor >> c3 >> shape >> c4 >> b.inversion))
            continue;
        b.degree = ((b.degree % 7) + 7) % 7;
        b.tonic = b.tonic < 0 ? -1 : b.tonic % 12;
        b.minor = std::clamp (b.minor, -1, 1);
        b.shape = (Shape) std::clamp (shape, 0, shapeCount - 1);
        b.inversion = std::clamp (b.inversion, 0, 2);
        bars.push_back (b);
    }
    return bars;
}
} // namespace lynx::prtools::dice
