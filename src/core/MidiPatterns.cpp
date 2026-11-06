#include "core/MidiPatterns.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <unordered_set>

#include "core/Scales.h"
#include "core/SmfRead.h"

namespace lynx::patterns
{
const std::array<const char*, 17> roles { "Kick", "Snare", "Clap", "Hat", "Ride", "Crash", "Tom", "Perc", "Bass",
                                          "Chords", "Lead", "Pad", "Bell", "Pluck", "Drone", "FX", "Other" };
const std::array<const char*, 8> drumRoles { "Kick", "Snare", "Clap", "Hat", "Ride", "Crash", "Tom", "Perc" };
const std::array<const char*, 7> sections { "Intro", "Verse", "Chorus", "Bridge", "Break", "Fill", "Outro" };

namespace
{
constexpr float blockAlike = 0.75f; // two bars / windows sharing this much (step, pitch class) are one

int pitchClass (int n) noexcept { return ((n % 12) + 12) % 12; }

struct RoleWord
{
    std::regex words;
    const char* role;
};

const std::vector<RoleWord>& roleWords()
{
    static const auto icase = std::regex::ECMAScript | std::regex::icase;
    static const std::vector<RoleWord> list {
        { std::regex (R"(\b(kick|bd|bass ?drum|bassdrum)\b)", icase), "Kick" },
        { std::regex (R"(\b(snare|sd|rim|rimshot|side ?stick)\b)", icase), "Snare" },
        { std::regex (R"(\b(clap|claps|handclap)\b)", icase), "Clap" },
        { std::regex (R"(\b(hat|hats|hh|hihat|hi-hat|hi ?hat|open ?hat|closed ?hat)\b)", icase), "Hat" },
        { std::regex (R"(\b(ride|ride ?bell)\b)", icase), "Ride" },
        { std::regex (R"(\b(crash|cymbal|china|splash|gong)\b)", icase), "Crash" },
        { std::regex (R"(\b(tom|toms|floor ?tom|timp\w*|timphany)\b)", icase), "Tom" },
        { std::regex (R"(\b(conga|bongo|shaker|tamb\w*|perc\w*|triangle|agogo|cowbell|maracas|stick|cabasa|guiro|clave|woodblock|cajon|djembe|tabla|insects)\b)", icase), "Perc" },
        { std::regex (R"(\b(bass|sub|808)\b)", icase), "Bass" },
        { std::regex (R"(\b(pad|pads|strings|string ensemble|choir|ensemble|wash)\b)", icase), "Pad" },
        { std::regex (R"(\b(drone|drones)\b)", icase), "Drone" },
        { std::regex (R"(\b(bell|bells|chime|chimes|glock\w*|mallet|marimba|vibes|vibraphone|celesta|kalimba|music ?box)\b)", icase), "Bell" },
        { std::regex (R"(\b(pluck|plucks|comb|harp|pizz\w*)\b)", icase), "Pluck" },
        { std::regex (R"(\b(organ|ep|e-?piano|piano|keys|clavi\w*|chord|chords|stab|stabs|guitar|gtr|rhodes|wurli\w*|mellotron|progression|progressions)\b)", icase), "Chords" },
        { std::regex (R"(\b(fx|sweep|noise|zap|riser|clang|impact|hit|hits|whoosh|sfx|effect)\b)", icase), "FX" },
        { std::regex (R"(\b(lead|flute|melod\w*|synth|arp|arpeggio|tones|solo|brass|horn|horns|sax|trumpet|violin|cello|whistle|voice|vox|theme|hook|line)\b)", icase), "Lead" },
    };
    return list;
}

std::string hashId (const std::string& text)
{
    std::uint32_t h = 2166136261u;
    for (unsigned char c : text)
    {
        h ^= c;
        h *= 16777619u;
    }
    char buf[9];
    std::snprintf (buf, sizeof buf, "%08x", h);
    return buf;
}

// GravePain's xorshift pick generator, so a seed picks the same pattern it did there
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

using Signature = std::unordered_set<int>;

Signature signatureOf (const std::vector<PatternNote>& notes, int from, int to)
{
    Signature s;
    for (const auto& n : notes)
        if (n.step >= from && n.step < to)
            s.insert ((n.step - from) * 12 + pitchClass (n.note));
    return s;
}

float alike (const Signature& a, const Signature& b)
{
    if (a.empty() && b.empty())
        return 1.0f;
    int both = 0;
    for (int x : a)
        both += b.count (x) ? 1 : 0;
    return (float) both / (float) (a.size() + b.size() - (std::size_t) both);
}

struct HeldNote
{
    std::int64_t tick = 0, length = -1;
    int note = 0, velocity = 0;
};

// each note of a MIDI channel with how long it was held (offs before ons at one tick, first on first off)
std::vector<HeldNote> pairNotes (const midiimport::ParsedMidi& midi, int channel)
{
    struct Ev { std::int64_t tick; bool on; int note, velocity; std::size_t order; };
    std::vector<Ev> evs;
    for (std::size_t i = 0; i < midi.events.size(); ++i)
    {
        const auto& e = midi.events[i];
        const int kind = e.status & 0xF0;
        if ((e.status & 0x0F) != channel || (kind != 0x80 && kind != 0x90))
            continue;
        evs.push_back ({ e.tick, kind == 0x90 && e.data2 > 0, e.data1, e.data2, i });
    }
    std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.tick != b.tick ? a.tick < b.tick : (! a.on && b.on); });
    std::map<int, std::vector<std::size_t>> open;
    std::vector<HeldNote> list;
    for (const auto& e : evs)
    {
        if (e.on)
        {
            open[e.note].push_back (list.size());
            list.push_back ({ e.tick, -1, e.note, e.velocity });
        }
        else if (auto it = open.find (e.note); it != open.end() && ! it->second.empty())
        {
            auto& h = list[it->second.front()];
            it->second.erase (it->second.begin());
            h.length = std::max<std::int64_t> (1, e.tick - h.tick);
        }
    }
    for (auto& h : list)
        if (h.length < 0)
            h.length = ppq / 4;
    return list;
}

float pitchSlope (const std::vector<PatternNote>& notes, int from, int to, int over)
{
    int count = 0;
    double mx = 0, my = 0;
    for (const auto& n : notes)
        if (n.step >= from && n.step < to)
        {
            mx += n.step;
            my += n.note;
            ++count;
        }
    if (count < 2)
        return 0.0f;
    mx /= count;
    my /= count;
    double sxy = 0, sxx = 0;
    for (const auto& n : notes)
        if (n.step >= from && n.step < to)
        {
            sxy += (n.step - mx) * (n.note - my);
            sxx += (n.step - mx) * (n.step - mx);
        }
    const double slope = sxx > 0 ? sxy / sxx : 0.0;
    return (float) std::clamp (slope * over / 12.0, -1.0, 1.0);
}

float busierLater (const std::vector<PatternNote>& notes, int from, int to)
{
    const int middle = from + (to - from) / 2;
    int first = 0, second = 0;
    for (const auto& n : notes)
    {
        if (n.step < from || n.step >= to)
            continue;
        (n.step < middle ? first : second)++;
    }
    return (float) (second - first) / (float) std::max (1, first + second);
}

// shape, polyphony, density, fill, root and contour from the notes
void shape (PatternRecord& r)
{
    std::set<int> pitches;
    std::vector<std::pair<int, int>> events;
    for (const auto& n : r.notes)
    {
        pitches.insert (n.note);
        events.emplace_back (n.step, 1);
        events.emplace_back (n.step + n.length, -1);
    }
    std::sort (events.begin(), events.end());
    int now = 0, poly = 0;
    for (const auto& e : events)
        poly = std::max (poly, now += e.second);
    r.poly = poly;
    r.density = (float) r.notes.size() / (float) std::max (1, r.bars);
    r.shape = pitches.size() == 1 ? rhythm : poly >= 3 ? chords : poly == 1 ? line : mixed;

    int lastEnd = 0;
    for (const auto& n : r.notes)
        lastEnd = std::max (lastEnd, n.step + std::max (1, n.length));
    const int covered = std::max (1, (int) std::ceil (lastEnd / (double) stepsPerBar));
    const int tight = covered <= 1 ? 1 : covered <= 2 ? 2 : 4;
    r.bars = std::min (r.bars, tight);
    std::set<int> barsWithNotes;
    for (const auto& n : r.notes)
        barsWithNotes.insert (std::min (r.bars - 1, n.step / stepsPerBar));
    r.fill = (float) barsWithNotes.size() / (float) r.bars;

    float weight[12] {};
    for (std::size_t i = 0; i < r.notes.size(); ++i)
    {
        const auto& n = r.notes[i];
        weight[pitchClass (n.note)] += (float) n.length + (n.step % stepsPerBar == 0 ? 8.0f : 0.0f) + (i == 0 ? 8.0f : 0.0f);
    }
    int root = 0;
    for (int pc = 1; pc < 12; ++pc)
        if (weight[pc] > weight[root])
            root = pc;
    r.root = root;
    r.minor = weight[(root + 3) % 12] > weight[(root + 4) % 12];

    const int span = std::max (1, r.bars * stepsPerBar), half = std::max (1, span / 2);
    if (r.shape == rhythm)
    {
        int first = 0, second = 0;
        for (const auto& n : r.notes)
            (n.step < span / 2 ? first : second)++;
        r.contour = (float) (second - first) / (float) std::max (1, first + second);
        r.firstHalf = busierLater (r.notes, 0, half);
        r.secondHalf = busierLater (r.notes, half, span);
    }
    else
    {
        if (r.notes.size() > 1)
            r.contour = pitchSlope (r.notes, 0, span, span);
        r.firstHalf = pitchSlope (r.notes, 0, half, half);
        r.secondHalf = pitchSlope (r.notes, half, span, half);
    }
    int rootNote = INT32_MAX;
    for (const auto& n : r.notes)
        if (pitchClass (n.note) == root)
            rootNote = std::min (rootNote, n.note);
    r.rootNote = rootNote == INT32_MAX ? r.notes.front().note : rootNote;
    for (auto& n : r.notes)
        n.rel = n.note - r.rootNote;
}

int addPart (const std::vector<HeldNote>& held, const std::string& role, const std::string& setName, const std::string& name,
             const std::string& section, const std::string& path, int channel, PatternSet& into, bool firstWindowOnly)
{
    if (held.empty())
        return 0;
    const double tps = (double) ppq / stepsPerBeat;
    std::int64_t end = 0;
    for (const auto& n : held)
        end = std::max (end, n.tick + n.length);
    const int totalBars = std::max (1, (int) std::ceil (end / (tps * stepsPerBar) - 1e-6));
    const int windowBars = totalBars >= 3 ? 4 : totalBars;
    const int windowSteps = windowBars * stepsPerBar;
    std::vector<Signature> kept;
    int added = 0, take = 0;
    const int lastFrom = firstWindowOnly ? 0 : totalBars * stepsPerBar - 1;
    for (int from = 0; from <= lastFrom; from += windowSteps)
    {
        std::vector<PatternNote> notes;
        for (const auto& n : held)
        {
            const int step = (int) std::lround (n.tick / tps) - from;
            if (step < 0 || step >= windowSteps)
                continue;
            const int length = std::max (1, (int) std::lround (n.length / tps));
            notes.push_back ({ step, std::min (length, windowSteps - step), n.note, 0, n.velocity });
        }
        if (notes.empty())
            continue;
        std::stable_sort (notes.begin(), notes.end(), [] (const auto& a, const auto& b) { return a.step != b.step ? a.step < b.step : a.note < b.note; });
        auto sig = signatureOf (notes, 0, windowSteps);
        if (std::any_of (kept.begin(), kept.end(), [&] (const auto& k) { return alike (k, sig) >= blockAlike; }))
            continue;
        kept.push_back (std::move (sig));

        const auto blocks = blocksOf (notes, windowBars);
        const int loop = loopBars (blocks);
        if (loop < windowBars)
        {
            const int loopSteps = loop * stepsPerBar;
            std::erase_if (notes, [&] (const auto& n) { return n.step >= loopSteps; });
            for (auto& n : notes)
                n.length = std::min (n.length, loopSteps - n.step);
        }
        PatternRecord r;
        r.set = setName;
        r.name = name;
        r.section = section;
        r.role = role;
        r.bars = loop;
        r.source = path;
        r.part = channel;
        r.notes = std::move (notes);
        r.blocks = blocks;
        shape (r);
        if (role == "Other")
            r.role = roleOfShape (r);
        r.id = hashId (setName + "|" + name + "|" + std::to_string (channel) + "|" + r.role + "|" + std::to_string (take));
        into.records.push_back (std::move (r));
        ++added;
        ++take;
    }
    return added;
}

std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

// ---- mixing -------------------------------------------------------------------------------------
using Notes = std::vector<PatternNote>;

Notes unrolled (const PatternRecord& r)
{
    const int window = std::clamp (r.bars, 1, 4) * stepsPerBar;
    Notes list;
    for (int from = 0; from < maxSteps; from += window)
        for (const auto& n : r.notes)
        {
            const int step = from + n.step;
            if (n.step >= window || step >= maxSteps)
                continue;
            list.push_back ({ step, std::max (1, std::min (n.length, from + window - step)), n.note, n.rel, n.velocity });
        }
    std::stable_sort (list.begin(), list.end(), [] (const auto& x, const auto& y) { return x.step != y.step ? x.step < y.step : x.rel < y.rel; });
    return list;
}

void nearInOctaves (const Notes& first, Notes& other)
{
    double meanA = 0, meanB = 0;
    for (const auto& n : first)
        meanA += n.rel;
    for (const auto& n : other)
        meanB += n.rel;
    meanA /= (double) first.size();
    meanB /= (double) other.size();
    int octaves = 0;
    while (meanB + 12 * octaves - meanA > 7)
        --octaves;
    while (meanA - (meanB + 12 * octaves) > 7)
        ++octaves;
    for (auto& n : other)
        n.rel += 12 * octaves;
}

int relInScale (int rel, bool minor)
{
    // nearest note of the root's major / minor scale, down on a tie
    return scales::snap (rel + 60, scales::Key { 0, minor ? scales::naturalMinor : scales::major }) - 60; // (snap works on 0..127 keys)
}

Notes reversed (const Notes& notes)
{
    Notes list;
    for (const auto& n : notes)
    {
        const int start = n.step / stepsPerBar * stepsPerBar, length = std::min (n.length, start + stepsPerBar - n.step);
        list.push_back ({ start + stepsPerBar - (n.step - start) - length, length, n.note, n.rel, n.velocity });
    }
    return list;
}

Notes flipped (const Notes& notes, bool minor)
{
    int low = INT32_MAX, high = INT32_MIN;
    for (const auto& n : notes)
    {
        low = std::min (low, n.rel);
        high = std::max (high, n.rel);
    }
    Notes list;
    for (const auto& n : notes)
        list.push_back ({ n.step, n.length, n.note, relInScale (low + high - n.rel, minor), n.velocity });
    return list;
}

Notes answered (const Notes& first, const Notes& second, bool minor)
{
    Notes list;
    for (int i = 1; i < 4; i += 2)
    {
        const PatternNote* called = nullptr;
        const PatternNote* opens = nullptr;
        for (const auto& n : first)
            if (n.step >= (i - 1) * stepsPerBar && n.step < i * stepsPerBar && (called == nullptr || n.step >= called->step))
                called = &n;
        for (const auto& n : second)
            if (n.step >= i * stepsPerBar && n.step < (i + 1) * stepsPerBar && (opens == nullptr || n.step < opens->step))
                opens = &n;
        const int shift = called != nullptr && opens != nullptr ? called->rel - opens->rel : 0;
        for (const auto& n : second)
            if (n.step >= i * stepsPerBar && n.step < (i + 1) * stepsPerBar)
                list.push_back ({ n.step, n.length, n.note, relInScale (n.rel + shift, minor), n.velocity });
    }
    return list;
}

std::vector<Notes> strikes (const Notes& notes)
{
    std::vector<Notes> s;
    for (const auto& n : notes)
    {
        if (s.empty() || s.back().front().step != n.step)
            s.emplace_back();
        s.back().push_back (n);
    }
    return s;
}

Notes thinned (const Notes& notes, int perBar)
{
    const auto strength = [] (int step) { return step % stepsPerBar == 0 ? 3 : step % stepsPerBeat == 0 ? 2 : step % 2 == 0 ? 1 : 0; };
    const auto loudest = [] (const Notes& s) { int v = 0; for (const auto& n : s) v = std::max (v, n.velocity); return v; };
    Notes kept;
    for (int i = 0; i < 4; ++i)
    {
        Notes bar;
        for (const auto& n : notes)
            if (n.step >= i * stepsPerBar && n.step < (i + 1) * stepsPerBar)
                bar.push_back (n);
        auto s = strikes (bar);
        std::stable_sort (s.begin(), s.end(), [&] (const Notes& x, const Notes& y) {
            if (strength (x[0].step) != strength (y[0].step))
                return strength (x[0].step) > strength (y[0].step);
            if (loudest (x) != loudest (y))
                return loudest (x) > loudest (y);
            return x[0].step < y[0].step;
        });
        for (std::size_t k = 0; k < s.size() && (int) k < perBar; ++k)
            kept.insert (kept.end(), s[k].begin(), s[k].end());
    }
    return kept;
}

Notes notesOf (const Notes& rhythmNotes, const Notes& pitches)
{
    const auto when = strikes (rhythmNotes);
    const auto what = strikes (pitches);
    Notes notes;
    for (std::size_t i = 0; i < when.size(); ++i)
    {
        int length = 0, velocity = 0;
        for (const auto& n : when[i])
        {
            length = std::max (length, n.length);
            velocity = std::max (velocity, n.velocity);
        }
        for (const auto& p : what[i % what.size()])
            notes.push_back ({ when[i][0].step, length, 0, p.rel, velocity });
    }
    return notes;
}

Notes accentsOf (const Notes& hits, const Notes& accents)
{
    Notes notes;
    for (const auto& n : hits)
    {
        int velocity = n.velocity, gap = INT32_MAX;
        for (const auto& m : accents)
        {
            const int d = std::abs (m.step % stepsPerBar - n.step % stepsPerBar);
            if (d < gap)
            {
                gap = d;
                velocity = m.velocity;
            }
        }
        notes.push_back ({ n.step, n.length, n.note, n.rel, velocity });
    }
    return notes;
}
} // namespace

bool isDrumRole (std::string_view role) noexcept
{
    return std::any_of (drumRoles.begin(), drumRoles.end(), [&] (const char* r) { return role == r; });
}

std::string sectionOf (const std::string& words)
{
    static const std::regex sectionWords (R"(\b(intro|verse|chorus|bridge|break|breakdown|fill|outro|ending)\b)", std::regex::ECMAScript | std::regex::icase);
    std::smatch m;
    if (words.empty() || ! std::regex_search (words, m, sectionWords))
        return {};
    auto w = lower (m.str (1));
    if (w == "breakdown")
        return "Break";
    if (w == "ending")
        return "Outro";
    w[0] = (char) std::toupper ((unsigned char) w[0]);
    return w;
}

std::string drumRole (int note)
{
    switch (note)
    {
        case 35: case 36: return "Kick";
        case 37: case 38: case 40: return "Snare";
        case 39: return "Clap";
        case 42: case 44: case 46: return "Hat";
        case 51: case 53: case 59: return "Ride";
        case 49: case 52: case 55: case 57: return "Crash";
        case 41: case 43: case 45: case 47: case 48: case 50: return "Tom";
        default: return "Perc";
    }
}

std::string programRole (int program)
{
    if (program < 0) return {};
    if (program < 8) return "Chords";   // pianos
    if (program < 16) return "Bell";    // chromatic percussion
    if (program < 32) return "Chords";  // organs, guitars
    if (program < 40) return "Bass";
    if (program < 48) return "Lead";    // solo strings
    if (program < 56) return "Pad";     // ensembles
    if (program < 88) return "Lead";    // brass, reeds, pipes, synth leads
    if (program < 96) return "Pad";     // synth pads
    if (program < 104) return "FX";
    if (program < 112) return "Lead";   // ethnic
    if (program < 120) return "Perc";
    return "FX";
}

std::string roleOf (const std::string& words, int program, bool drumChannel)
{
    if (! words.empty())
        for (const auto& rw : roleWords())
            if (std::regex_search (words, rw.words))
                return rw.role;
    if (auto byProgram = programRole (program); ! byProgram.empty())
        return byProgram;
    return drumChannel ? "Perc" : "Other";
}

std::string roleOfShape (const PatternRecord& r)
{
    if (r.notes.empty())
        return "Other";
    if (r.shape == rhythm)
        return "Perc";
    if (r.shape == chords)
        return "Chords";
    double mean = 0;
    for (const auto& n : r.notes)
        mean += n.note;
    return mean / (double) r.notes.size() < 50.0 ? "Bass" : "Lead";
}

std::string blocksOf (const std::vector<PatternNote>& notes, int bars)
{
    std::vector<Signature> reps;
    std::string text;
    for (int b = 0; b < bars; ++b)
    {
        auto sig = signatureOf (notes, b * stepsPerBar, (b + 1) * stepsPerBar);
        if (sig.empty())
        {
            text += '-';
            continue;
        }
        int hit = -1;
        for (std::size_t i = 0; i < reps.size() && hit < 0; ++i)
            if (alike (reps[i], sig) >= blockAlike)
                hit = (int) i;
        if (hit < 0)
        {
            reps.push_back (std::move (sig));
            hit = (int) reps.size() - 1;
        }
        text += hit < 26 ? (char) ('A' + hit) : '*';
    }
    return text;
}

int loopBars (const std::string& blocks)
{
    if (blocks.empty())
        return 1;
    const int bars = (int) blocks.size();
    if (blocks.find ('-') == std::string::npos && std::all_of (blocks.begin(), blocks.end(), [&] (char c) { return c == blocks[0]; }))
        return 1;
    if (bars == 4 && blocks.compare (0, 2, blocks, 2, 2) == 0 && blocks.substr (0, 2).find ('-') == std::string::npos)
        return 2;
    return bars;
}

std::string genreOf (const std::string& setName)
{
    const auto trim = [] (std::string s) {
        while (! s.empty() && s.front() == ' ') s.erase (s.begin());
        while (! s.empty() && s.back() == ' ') s.pop_back();
        return s;
    };
    const auto slash = setName.find ('/');
    if (slash == std::string::npos)
        return trim (setName);
    const auto next = setName.find ('/', slash + 1);
    return trim (setName.substr (slash + 1, next == std::string::npos ? std::string::npos : next - slash - 1));
}

int addFile (const midiimport::ParsedMidi& midi, const std::string& path, const std::string& setName, PatternSet& into, bool firstWindowOnly)
{
    const auto name = std::filesystem::path (path).stem().string();
    std::string words = name;
    for (const auto& t : midi.trackNames)
        if (! t.empty())
            words += " " + t;
    const auto section = sectionOf (words);
    const auto programs = smf::programs (midi);
    int counts[16] {};
    for (const auto& e : midi.events)
        if ((e.status & 0xF0) == 0x90 && e.data2 > 0)
            ++counts[e.status & 0x0F];
    int added = 0;
    for (int channel = 0; channel < 16; ++channel)
    {
        if (counts[channel] == 0)
            continue;
        const auto held = pairNotes (midi, channel);
        if (channel == 9)
        {
            // a drum channel is several instruments on one part: each drum's notes are a pattern of their own
            std::map<std::string, std::vector<HeldNote>> byRole;
            for (const auto& n : held)
                byRole[drumRole (n.note)].push_back (n);
            for (const auto& [role, list] : byRole)
                added += addPart (list, role, setName, name, section, path, channel, into, firstWindowOnly);
        }
        else
        {
            auto role = roleOf (words, programs[(std::size_t) channel], false);
            if (role == "Other")
            {
                auto folderWords = setName;
                std::replace (folderWords.begin(), folderWords.end(), '/', ' ');
                role = roleOf (folderWords, -1, false);
            }
            added += addPart (held, role, setName, name, section, path, channel, into, firstWindowOnly);
        }
    }
    return added;
}

PatternSet build (const std::vector<SourceGroup>& groups, bool firstWindowOnly)
{
    PatternSet set;
    set.firstWindowOnly = firstWindowOnly;
    for (const auto& g : groups)
    {
        for (const auto& file : g.files)
        {
            std::string error;
            if (auto midi = smf::readFile (file, error))
                addFile (*midi, file, g.label, set, firstWindowOnly);
        }
        set.sources.push_back (g.label);
    }
    return set;
}

std::vector<SourceGroup> groupsOfFolder (const std::string& folder)
{
    namespace fs = std::filesystem;
    std::vector<SourceGroup> groups;
    std::error_code ec;
    const fs::path root (folder);
    if (! fs::is_directory (root, ec))
        return groups;
    const auto top = root.filename().string();
    const auto midiIn = [&] (const fs::path& dir) {
        std::vector<std::string> files;
        for (const auto& e : fs::directory_iterator (dir, ec))
        {
            auto ext = lower (e.path().extension().string());
            if (e.is_regular_file() && (ext == ".mid" || ext == ".midi"))
                files.push_back (e.path().string());
        }
        std::sort (files.begin(), files.end());
        return files;
    };
    if (auto own = midiIn (root); ! own.empty())
        groups.push_back ({ top, std::move (own) });
    std::vector<fs::path> dirs;
    for (const auto& e : fs::directory_iterator (root, ec))
        if (e.is_directory())
            dirs.push_back (e.path());
    std::sort (dirs.begin(), dirs.end());
    for (const auto& d : dirs)
        if (auto files = midiIn (d); ! files.empty())
            groups.push_back ({ top + " / " + d.filename().string(), std::move (files) });
    return groups;
}

std::vector<const PatternRecord*> matching (const PatternSet& set, const std::string& role, const std::string& setName, const std::string& section)
{
    std::vector<const PatternRecord*> list;
    for (const auto& r : set.records)
    {
        if (r.notes.empty())
            continue;
        if (! role.empty() && r.role != role)
            continue;
        if (! setName.empty() && lower (r.set) != lower (setName))
            continue;
        if (! section.empty() && r.section != section)
            continue;
        list.push_back (&r);
    }
    std::stable_sort (list.begin(), list.end(), [] (const auto* a, const auto* b) { return a->id < b->id; });
    return list;
}

std::vector<const PatternRecord*> poolFor (const PatternSet& set, const std::string& role, const std::string& setName, const std::string& section)
{
    auto pool = matching (set, role, setName, section);
    if (pool.empty() && ! section.empty())
        pool = matching (set, role, setName, {});
    if (pool.empty() && ! setName.empty())
        pool = matching (set, role, {}, {});
    if (pool.empty() && ! role.empty())
    {
        const bool drums = isDrumRole (role);
        const auto sameFamily = [drums] (const PatternRecord* r) { return isDrumRole (r->role) != drums; };
        pool = matching (set, {}, setName, section);
        std::erase_if (pool, sameFamily);
        if (pool.empty())
        {
            pool = matching (set, {}, {}, {});
            std::erase_if (pool, sameFamily);
        }
    }
    return pool;
}

const PatternRecord* pick (const std::vector<const PatternRecord*>& pool, std::uint32_t seed)
{
    if (pool.empty())
        return nullptr;
    std::vector<const PatternRecord*> from;
    for (const auto* r : pool)
        if (r->fill >= 0.99f)
            from.push_back (r);
    if (from.empty())
        from = pool;
    std::vector<const PatternRecord*> four;
    for (const auto* r : from)
        if (r->bars == 4)
            four.push_back (r);
    if (! four.empty())
        from = std::move (four);
    Rng rng (seed);
    return from[std::min (from.size() - 1, (std::size_t) (rng.next() * (float) from.size()))];
}

bool mixTakesSecond (Mix m) noexcept
{
    return m == Mix::halves || m == Mix::bars || m == Mix::turnaround || m == Mix::swapNotes || m == Mix::answer || m == Mix::aabc;
}

PatternRecord mix (const PatternRecord& a, const PatternRecord* b, Mix how, const PatternRecord* c, float busy)
{
    if (a.notes.empty() || how == Mix::whole)
        return a;
    const bool hasB = b != nullptr && ! b->notes.empty() && b != &a;
    if (mixTakesSecond (how) && ! hasB)
        return a;
    const bool hasC = c != nullptr && ! c->notes.empty() && c != &a && c != b;
    auto first = unrolled (a);
    Notes second = hasB ? unrolled (*b) : Notes {};
    Notes third = hasC ? unrolled (*c) : Notes {};
    if (! a.isRhythm())
    {
        if (hasB) nearInOctaves (first, second);
        if (hasC) nearInOctaves (first, third);
    }
    Notes flip, rev, ans;
    std::array<const Notes*, 4> from { &first, &first, &first, &first };
    switch (how)
    {
        case Mix::halves: from = { &first, &first, &second, &second }; break;
        case Mix::bars: from = { &first, &second, &first, &second }; break;
        case Mix::turnaround: from = { &first, &first, &first, &second }; break;
        case Mix::aabc: from = { &first, &first, &second, hasC ? &third : &second }; break;
        case Mix::reverse: rev = reversed (first); from = { &first, &first, &first, &rev }; break;
        case Mix::flip: flip = a.isRhythm() ? reversed (first) : flipped (first, a.minor); from = { &first, &flip, &first, &flip }; break;
        case Mix::answer: ans = a.isRhythm() ? second : answered (first, second, a.minor); from = { &first, &ans, &first, &ans }; break;
        default: break;
    }
    Notes notes;
    if (how == Mix::swapNotes)
        notes = a.isRhythm() ? accentsOf (first, second) : notesOf (first, second);
    else if (how == Mix::fit)
    {
        const float aim = busy >= 0.0f ? busy : hasB ? (float) strikes (second).size() / 4.0f : -1.0f;
        if (aim < 0.0f)
            return a;
        notes = thinned (first, std::max (1, (int) std::lround (aim)));
    }
    else
    {
        for (int i = 0; i < 4; ++i)
        {
            // a note stops where its stretch of bars does, so one source never holds over into another
            int end = (i + 1) * stepsPerBar;
            while (end < maxSteps && from[(std::size_t) (end / stepsPerBar)] == from[(std::size_t) i])
                end += stepsPerBar;
            for (const auto& n : *from[(std::size_t) i])
                if (n.step >= i * stepsPerBar && n.step < (i + 1) * stepsPerBar)
                    notes.push_back ({ n.step, std::max (1, std::min (n.length, end - n.step)), n.note, n.rel, n.velocity });
        }
    }
    if (notes.empty())
        return a;
    std::stable_sort (notes.begin(), notes.end(), [] (const auto& x, const auto& y) {
        return x.step != y.step ? x.step < y.step : x.rel != y.rel ? x.rel < y.rel : x.note < y.note;
    });
    notes.erase (std::unique (notes.begin(), notes.end(), [] (const auto& x, const auto& y) { return x.step == y.step && x.rel == y.rel && x.note == y.note; }), notes.end());
    if (! a.isRhythm())
        for (auto& n : notes)
            n.note = a.rootNote + n.rel;

    PatternRecord m = a;
    const auto idx = std::to_string ((int) how);
    m.id = a.id + (hasB && (mixTakesSecond (how) || how == Mix::fit) ? "+" + b->id : "") + (hasC && how == Mix::aabc ? "+" + c->id : "") + "/" + idx;
    m.name = a.name + (hasB && mixTakesSecond (how) ? " + " + b->name : "") + (hasC && how == Mix::aabc ? " + " + c->name : "");
    m.bars = 4;
    m.notes = std::move (notes);
    std::vector<std::pair<int, int>> events;
    std::set<int> filled;
    for (const auto& n : m.notes)
    {
        events.emplace_back (n.step, 1);
        events.emplace_back (n.step + n.length, -1);
        filled.insert (n.step / stepsPerBar);
    }
    std::sort (events.begin(), events.end());
    int now = 0;
    m.poly = 0;
    for (const auto& e : events)
        m.poly = std::max (m.poly, now += e.second);
    m.density = (float) m.notes.size() / 4.0f;
    m.fill = (float) filled.size() / 4.0f;
    return m;
}

std::vector<Note> lay (const PatternRecord& r, const LayOptions& o)
{
    std::vector<Note> out;
    const int window = std::clamp (r.bars, 1, 4) * stepsPerBar;
    const int total = std::clamp (o.bars, 1, 64) * stepsPerBar;
    const bool ranged = ! r.isRhythm() && o.highest > o.lowest;
    int shift = 0;
    if (ranged)
    {
        int best = -1;
        for (int octaves = -2; octaves <= 2; ++octaves)
        {
            int inside = 0;
            for (const auto& n : r.notes)
            {
                const int note = o.rootNote + n.rel + octaves * 12;
                inside += note >= o.lowest && note <= o.highest ? 1 : 0;
            }
            // the nearest shift wins a tie, so a pattern that fits where it is stays there
            if (inside > best || (inside == best && std::abs (octaves) < std::abs (shift)))
            {
                best = inside;
                shift = octaves;
            }
        }
    }
    const scales::Key key { o.keyRoot, o.keyMinor ? scales::naturalMinor : scales::major };
    for (int from = 0; from < total; from += window)
        for (const auto& n : r.notes)
        {
            const int step = from + n.step;
            if (n.step >= window || step >= total)
                continue;
            int note = r.isRhythm() ? n.note : o.rootNote + n.rel + shift * 12;
            if (! r.isRhythm() && o.keyRoot >= 0)
                note = scales::snap (note, key);
            if (ranged)
            {
                while (note < o.lowest && note + 12 <= o.highest) note += 12;
                while (note > o.highest && note - 12 >= o.lowest) note -= 12;
            }
            if (note < 0 || note > 127)
                continue;
            Note nn;
            nn.start = o.at + (std::int64_t) step * ticksPerStep;
            nn.length = (std::int64_t) std::max (1, std::min (n.length, total - step)) * ticksPerStep;
            nn.channel = o.channel;
            nn.key = note;
            nn.velocity = std::clamp (n.velocity, 1, 127) / 127.0f;
            out.push_back (nn);
        }
    std::stable_sort (out.begin(), out.end(), [] (const Note& x, const Note& y) { return x.start != y.start ? x.start < y.start : x.key < y.key; });
    return out;
}

// ---- text form ---------------------------------------------------------------------------------
namespace
{
std::string clean (std::string s)
{
    for (auto& ch : s)
        if (ch == '\t' || ch == '\n' || ch == '\r')
            ch = ' ';
    return s;
}

std::vector<std::string> split (const std::string& s, char sep)
{
    std::vector<std::string> parts;
    std::string cur;
    for (char ch : s)
    {
        if (ch == sep)
        {
            parts.push_back (cur);
            cur.clear();
        }
        else
            cur += ch;
    }
    parts.push_back (cur);
    return parts;
}
} // namespace

std::string toText (const PatternSet& set)
{
    std::ostringstream o;
    o << "LYNX patterns 1\n" << "name\t" << clean (set.name) << "\n" << "firstWindowOnly\t" << (set.firstWindowOnly ? 1 : 0) << "\n";
    for (const auto& s : set.sources)
        o << "source\t" << clean (s) << "\n";
    for (const auto& r : set.records)
    {
        o << "record\t" << r.id << '\t' << clean (r.set) << '\t' << clean (r.name) << '\t' << r.section << '\t' << r.role << '\t' << r.shape << '\t'
          << r.bars << '\t' << r.root << '\t' << r.rootNote << '\t' << (r.minor ? 1 : 0) << '\t' << r.poly << '\t' << r.density << '\t' << r.fill << '\t'
          << clean (r.source) << '\t' << r.blocks << '\t' << r.contour << '\t' << r.firstHalf << '\t' << r.secondHalf << '\t' << r.part << '\t';
        for (std::size_t i = 0; i < r.notes.size(); ++i)
        {
            const auto& n = r.notes[i];
            o << (i ? " " : "") << n.step << ':' << n.length << ':' << n.note << ':' << n.velocity;
        }
        o << "\n";
    }
    return o.str();
}

std::optional<PatternSet> fromText (const std::string& text)
{
    std::istringstream in (text);
    std::string line;
    if (! std::getline (in, line) || line.rfind ("LYNX patterns 1", 0) != 0)
        return std::nullopt;
    PatternSet set;
    try
    {
        while (std::getline (in, line))
        {
            if (! line.empty() && line.back() == '\r')
                line.pop_back();
            const auto f = split (line, '\t');
            if (f[0] == "name" && f.size() >= 2)
                set.name = f[1];
            else if (f[0] == "firstWindowOnly" && f.size() >= 2)
                set.firstWindowOnly = f[1] == "1";
            else if (f[0] == "source" && f.size() >= 2)
                set.sources.push_back (f[1]);
            else if (f[0] == "record" && f.size() >= 21)
            {
                PatternRecord r;
                r.id = f[1]; r.set = f[2]; r.name = f[3]; r.section = f[4]; r.role = f[5]; r.shape = f[6];
                r.bars = std::stoi (f[7]); r.root = std::stoi (f[8]); r.rootNote = std::stoi (f[9]); r.minor = f[10] == "1";
                r.poly = std::stoi (f[11]); r.density = std::stof (f[12]); r.fill = std::stof (f[13]); r.source = f[14]; r.blocks = f[15];
                r.contour = std::stof (f[16]); r.firstHalf = std::stof (f[17]); r.secondHalf = std::stof (f[18]); r.part = std::stoi (f[19]);
                for (const auto& token : split (f[20], ' '))
                {
                    const auto p = split (token, ':');
                    if (p.size() != 4)
                        continue;
                    r.notes.push_back ({ std::stoi (p[0]), std::stoi (p[1]), std::stoi (p[2]), 0, std::stoi (p[3]) });
                }
                for (auto& n : r.notes)
                    n.rel = n.note - r.rootNote;
                set.records.push_back (std::move (r));
            }
        }
    }
    catch (const std::exception&)
    {
        return std::nullopt;
    }
    return set;
}
} // namespace lynx::patterns
