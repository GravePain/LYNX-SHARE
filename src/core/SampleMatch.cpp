#include "core/SampleMatch.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <regex>
#include <vector>

namespace lynx::samplematch
{
namespace
{
std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

// the words of a name: letters / digits runs, '#' kept with its letter ("F#m"), separators _ - . ( ) [ ] and spaces
std::vector<std::string> words (const std::string& name)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : name)
    {
        if (std::isalnum ((unsigned char) c) || c == '#')
            cur += c;
        else
        {
            if (! cur.empty())
                out.push_back (cur);
            cur.clear();
        }
    }
    if (! cur.empty())
        out.push_back (cur);
    return out;
}

bool containsWord (const std::string& lowerName, const std::string& lowerWord)
{
    for (auto at = lowerName.find (lowerWord); at != std::string::npos; at = lowerName.find (lowerWord, at + 1))
    {
        const bool startOk = at == 0 || ! std::isalnum ((unsigned char) lowerName[at - 1]);
        const auto end = at + lowerWord.size();
        const bool endOk = end >= lowerName.size() || ! std::isalnum ((unsigned char) lowerName[end]);
        if (startOk && endOk)
            return true;
    }
    return false;
}

int pitchClassOf (char letter, char accidental)
{
    static constexpr int base[] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
    const int l = std::toupper ((unsigned char) letter) - 'A';
    if (l < 0 || l > 6)
        return -1;
    int pc = base[l];
    if (accidental == '#')
        ++pc;
    else if (accidental == 'b')
        --pc;
    return (pc + 12) % 12;
}
} // namespace

bool pitched (Key k) noexcept { return k.root >= 0; }

bool compatible (Key a, Key b) noexcept
{
    if (! pitched (a) || ! pitched (b))
        return true;
    if (a.root == b.root && a.minor == b.minor)
        return true;
    // a minor key and the major three semitones above share their seven notes
    const auto relative = [] (Key minorKey, Key majorKey) { return minorKey.minor && ! majorKey.minor && (minorKey.root + 3) % 12 == majorKey.root; };
    return relative (a, b) || relative (b, a);
}

std::string keyName (Key k)
{
    static constexpr const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (! pitched (k))
        return "no key";
    return std::string (names[k.root % 12]) + (k.minor ? " minor" : " major");
}

std::optional<Key> keyFromName (const std::string& name)
{
    const auto w = words (name);
    for (std::size_t i = 0; i < w.size(); ++i)
    {
        const auto& word = w[i];
        // a letter, an optional accidental, then m / min / minor / maj / major (any case, but a lone M is major and
        // a lone m minor), or nothing when the next word is the quality
        const char letter = word.empty() ? '\0' : word[0];
        if (std::toupper ((unsigned char) letter) < 'A' || std::toupper ((unsigned char) letter) > 'G')
            continue;
        std::size_t at = 1;
        char accidental = '\0';
        if (at < word.size() && (word[at] == '#' || word[at] == 'b'))
            accidental = word[at++];
        const auto rest = word.substr (at), restLower = lower (rest);
        std::string q; // "", "m" (minor) or "maj"
        bool spelled = false; // the quality written out (min, minor, maj, major)
        if (rest == "M")
            q = "maj";
        else if (rest == "m")
            q = "m";
        else if (restLower == "min" || restLower == "minor")
            q = "m", spelled = true;
        else if (restLower == "maj" || restLower == "major")
            q = "maj", spelled = true;
        else if (! rest.empty())
            continue; // another word ("Bass", "Drop")
        const auto next = i + 1 < w.size() ? lower (w[i + 1]) : std::string();
        if (q.empty() && (next == "minor" || next == "min"))
            q = "m", spelled = true;
        else if (q.empty() && (next == "major" || next == "maj"))
            q = "maj", spelled = true;
        const bool upper = std::isupper ((unsigned char) letter) != 0;
        // a lone letter is a key only in a clear context: capital, after "in" / "key"
        const auto before = i > 0 ? lower (w[i - 1]) : std::string();
        if (q.empty() && ! (upper && (before == "in" || before == "key")))
            continue;
        if (! upper && ! spelled) // ("am", "em": words, not keys; a lower-case key spells its quality out)
            continue;
        Key k;
        k.root = pitchClassOf (letter, accidental);
        k.minor = q == "m";
        if (k.root >= 0)
            return k;
    }
    return std::nullopt;
}

float playbackRate (float sampleBpm, float projectBpm) noexcept
{
    if (sampleBpm <= 0.0f || projectBpm <= 0.0f)
        return 1.0f;
    float best = projectBpm / sampleBpm;
    for (float k : { 0.5f, 2.0f })
    {
        const float r = projectBpm / (sampleBpm * k);
        if (std::abs (r - 1.0f) < std::abs (best - 1.0f))
            best = r;
    }
    return best;
}

bool tempoCompatible (float sampleBpm, float projectBpm, bool pitchedSample) noexcept
{
    return std::abs (playbackRate (sampleBpm, projectBpm) - 1.0f) <= (pitchedSample ? pitchedSpeedWindow : speedWindow);
}

bool isHalfOrDouble (float sampleBpm, float projectBpm) noexcept
{
    if (sampleBpm <= 0.0f || projectBpm <= 0.0f)
        return false;
    return std::abs (projectBpm / sampleBpm - 1.0f) > speedWindow && tempoCompatible (sampleBpm, projectBpm, false);
}

float semitones (float rate) noexcept { return rate <= 0.0f ? 0.0f : 12.0f * std::log2 (rate); }

float bpmFromName (const std::string& name)
{
    const auto l = lower (name);
    std::smatch m;
    // "128bpm", "128 bpm", "128-bpm", "bpm128", "bpm 128"
    static const std::regex withWord (R"((\d{2,3}(?:\.\d+)?)\s*[-_ ]?\s*bpm|bpm\s*[-_ ]?\s*(\d{2,3}(?:\.\d+)?))");
    if (std::regex_search (l, m, withWord))
    {
        const float v = std::stof (m[1].matched ? m.str (1) : m.str (2));
        if (v >= 40.0f && v <= 300.0f)
            return v;
    }
    // a lone number 60..200 in a loop's name ("Drum Loop 140", "_128_")
    if (l.find ("loop") != std::string::npos || l.find ('_') != std::string::npos)
        for (const auto& w : words (l))
            if (! w.empty() && std::all_of (w.begin(), w.end(), [] (char c) { return std::isdigit ((unsigned char) c); }) && w.size() <= 3)
                if (const int v = std::stoi (w); v >= 60 && v <= 200)
                    return (float) v;
    return 0.0f;
}

bool guessFromLength (float seconds, float projectBpm, float& bpm, int& beats) noexcept
{
    bpm = 0.0f;
    beats = 0;
    if (seconds <= 0.0f || projectBpm <= 0.0f)
        return false;
    float bestOff = 1e9f;
    for (int bars : { 1, 2, 4, 8, 16 })
    {
        const int b = bars * 4;
        const float candidate = (float) b * 60.0f / seconds;
        if (candidate < 40.0f || candidate > 300.0f)
            continue;
        const float off = std::abs (std::log2 (playbackRate (candidate, projectBpm)));
        // among equals (one bar at 280 is two at 140 is four at 70), the tempo nearest the project's own
        const float tie = std::abs (std::log2 (candidate / projectBpm));
        if (off < bestOff - 1e-4f || (std::abs (off - bestOff) <= 1e-4f && tie < std::abs (std::log2 (bpm / projectBpm))))
        {
            bestOff = off;
            bpm = candidate;
            beats = b;
        }
    }
    return beats > 0;
}

namespace
{
const std::vector<std::string>& drumTypes()
{
    static const std::vector<std::string> t { "Kick", "Snare", "Clap", "Hihat", "Open Hat", "Cymbal", "Tom", "Percussion", "Drum Loop", "Drum Kit" };
    return t;
}
const std::vector<std::string>& soundTypes()
{
    static const std::vector<std::string> t { "Bass", "Sub Bass", "Lead", "Pluck", "Arp", "Stab", "Chord", "Pad", "Keys", "Bell", "Mallets",
                                              "Strings", "Brass", "Guitar", "Voice", "Riser", "Impact", "Texture" };
    return t;
}
const std::vector<std::pair<std::string, std::string>>& aliases()
{
    static const std::vector<std::pair<std::string, std::string>> a {
        { "Hi-Hat", "Hihat" }, { "HiHat", "Hihat" }, { "Hats", "Hihat" }, { "Hat", "Hihat" }, { "OpenHat", "Open Hat" },
        { "Perc", "Percussion" }, { "Percs", "Percussion" }, { "Drums", "Drum Loop" }, { "Kicks", "Kick" }, { "Snares", "Snare" },
        { "Claps", "Clap" }, { "Pads", "Pad" }, { "Vocal", "Voice" }, { "Vocals", "Voice" }, { "Vox", "Voice" }, { "Chords", "Chord" },
        { "Plucks", "Pluck" }, { "Leads", "Lead" }, { "Sub", "Sub Bass" }, { "Piano", "Keys" }, { "Fx", "Riser" },
    };
    return a;
}
} // namespace

std::string guessType (const std::string& name)
{
    const auto l = lower (name);
    std::string best;
    std::size_t bestLength = 0;
    for (const auto* list : { &drumTypes(), &soundTypes() })
        for (const auto& t : *list)
            if (t.size() > bestLength && containsWord (l, lower (t)))
                best = t, bestLength = t.size();
    for (const auto& [alias, type] : aliases())
        if (alias.size() > bestLength && containsWord (l, lower (alias)))
            best = type, bestLength = alias.size();
    return best;
}

bool isDrumType (const std::string& type)
{
    return std::find (drumTypes().begin(), drumTypes().end(), type) != drumTypes().end();
}

bool soundsPitched (const std::string& name)
{
    if (keyFromName (name).has_value())
        return true;
    const auto type = guessType (name);
    return ! type.empty() && ! isDrumType (type) && type != "Riser" && type != "Impact" && type != "Texture";
}

namespace
{
std::uint32_t le32 (const std::uint8_t* p) { return (std::uint32_t) p[0] | ((std::uint32_t) p[1] << 8) | ((std::uint32_t) p[2] << 16) | ((std::uint32_t) p[3] << 24); }
std::uint32_t be32 (const std::uint8_t* p) { return ((std::uint32_t) p[0] << 24) | ((std::uint32_t) p[1] << 16) | ((std::uint32_t) p[2] << 8) | (std::uint32_t) p[3]; }

std::vector<std::uint8_t> readRange (std::ifstream& in, std::int64_t from, std::size_t count)
{
    std::vector<std::uint8_t> b (count);
    in.clear();
    in.seekg (from);
    in.read ((char*) b.data(), (std::streamsize) count);
    b.resize ((std::size_t) std::max<std::streamsize> (0, in.gcount()));
    return b;
}

// an 80-bit IEEE extended float (AIFF's sample rate)
double extended (const std::uint8_t* b)
{
    const int exponent = ((b[0] & 0x7F) << 8) | b[1];
    std::uint64_t mantissa = 0;
    for (int i = 0; i < 8; ++i)
        mantissa = (mantissa << 8) | b[2 + i];
    if (exponent == 0 && mantissa == 0)
        return 0.0;
    const double v = (double) mantissa * std::pow (2.0, exponent - 16383 - 63);
    return (b[0] & 0x80) != 0 ? -v : v;
}
} // namespace

float lengthSeconds (const std::string& path)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
        return 0.0f;
    in.seekg (0, std::ios::end);
    const std::int64_t size = in.tellg();
    if (size < 12)
        return 0.0f;
    const auto head = readRange (in, 0, 12);
    if (head.size() < 12)
        return 0.0f;
    if (std::memcmp (head.data(), "RIFF", 4) == 0 && std::memcmp (head.data() + 8, "WAVE", 4) == 0)
    {
        std::uint32_t byteRate = 0;
        std::int64_t at = 12;
        while (at + 8 <= size)
        {
            const auto chunk = readRange (in, at, 8);
            if (chunk.size() < 8)
                break;
            const auto length = le32 (chunk.data() + 4);
            if (std::memcmp (chunk.data(), "fmt ", 4) == 0)
            {
                const auto fmt = readRange (in, at + 8, 16);
                if (fmt.size() >= 12)
                    byteRate = le32 (fmt.data() + 8);
            }
            else if (std::memcmp (chunk.data(), "data", 4) == 0)
            {
                const auto bytes = std::min<std::int64_t> (length, size - (at + 8));
                return byteRate > 0 ? (float) ((double) bytes / byteRate) : 0.0f;
            }
            at += 8 + length + (length & 1);
        }
        return 0.0f;
    }
    if (std::memcmp (head.data(), "FORM", 4) == 0 && (std::memcmp (head.data() + 8, "AIFF", 4) == 0 || std::memcmp (head.data() + 8, "AIFC", 4) == 0))
    {
        std::int64_t at = 12;
        while (at + 8 <= size)
        {
            const auto chunk = readRange (in, at, 8);
            if (chunk.size() < 8)
                break;
            const auto length = be32 (chunk.data() + 4);
            if (std::memcmp (chunk.data(), "COMM", 4) == 0 && length >= 18)
            {
                const auto comm = readRange (in, at + 8, 18);
                if (comm.size() < 18)
                    return 0.0f;
                const auto frames = be32 (comm.data() + 2);
                const double rate = extended (comm.data() + 8);
                return rate > 0 ? (float) (frames / rate) : 0.0f;
            }
            at += 8 + length + (length & 1);
        }
        return 0.0f;
    }
    if (std::memcmp (head.data(), "OggS", 4) == 0)
    {
        // the rate in the identification header; the length the last page's granule position
        const auto first = readRange (in, 0, (std::size_t) std::min<std::int64_t> (4096, size));
        int rate = 0;
        for (std::size_t i = 0; i + 16 < first.size(); ++i)
            if (first[i] == 0x01 && std::memcmp (first.data() + i + 1, "vorbis", 6) == 0)
            {
                rate = (int) le32 (first.data() + i + 12); // after "\x01vorbis": version (4), channels (1), rate (4)
                break;
            }
        if (rate <= 0)
            return 0.0f;
        const auto tailSize = (std::size_t) std::min<std::int64_t> (65536, size);
        const auto tail = readRange (in, size - (std::int64_t) tailSize, tailSize);
        std::int64_t granule = -1;
        for (std::size_t i = 0; i + 14 <= tail.size(); ++i)
            if (std::memcmp (tail.data() + i, "OggS", 4) == 0)
            {
                std::uint64_t g = 0;
                for (int k = 7; k >= 0; --k)
                    g = (g << 8) | tail[i + 6 + (std::size_t) k];
                granule = (std::int64_t) g;
            }
        return granule > 0 ? (float) ((double) granule / rate) : 0.0f;
    }
    return 0.0f;
}

bool fits (const std::string& fileName, const Filter& f)
{
    const auto key = keyFromName (fileName);
    if (f.byKey && key.has_value() && pitched (f.key) && ! compatible (*key, f.key))
        return false;
    if (f.byTempo)
    {
        const float bpm = bpmFromName (fileName);
        if (bpm > 0.0f && ! tempoCompatible (bpm, f.bpm, key.has_value() || soundsPitched (fileName)))
            return false;
    }
    return true;
}
} // namespace lynx::samplematch
