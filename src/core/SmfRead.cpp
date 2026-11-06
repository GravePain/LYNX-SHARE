#include "core/SmfRead.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <vector>

#include "core/Song.h"

namespace lynx::smf
{
namespace
{
bool chunkIs (std::span<const std::uint8_t> b, std::size_t at, const char* id) noexcept
{
    return at + 4 <= b.size() && b[at] == (std::uint8_t) id[0] && b[at + 1] == (std::uint8_t) id[1]
           && b[at + 2] == (std::uint8_t) id[2] && b[at + 3] == (std::uint8_t) id[3];
}

std::uint32_t readBig (std::span<const std::uint8_t> b, std::size_t at, int count) noexcept
{
    std::uint32_t v = 0;
    for (int i = 0; i < count && at + (std::size_t) i < b.size(); ++i)
        v = (v << 8) | b[at + (std::size_t) i];
    return v;
}

// seven bits a byte, the top bit set on every byte but the last (at most four bytes)
std::uint32_t readVariable (std::span<const std::uint8_t> b, std::size_t& at, std::size_t end) noexcept
{
    std::uint32_t v = 0;
    for (int i = 0; i < 4 && at < end; ++i)
    {
        const auto x = b[at++];
        v = (v << 7) | (x & 0x7Fu);
        if ((x & 0x80) == 0)
            break;
    }
    return v;
}
} // namespace

std::optional<midiimport::ParsedMidi> parse (std::span<const std::uint8_t> b, std::string& error)
{
    if (! chunkIs (b, 0, "MThd"))
    {
        error = "not a MIDI file (no MThd header)";
        return std::nullopt;
    }
    const auto headerLength = readBig (b, 4, 4);
    std::size_t at = 8;
    if (headerLength < 6 || at + headerLength > b.size())
    {
        error = "the header is cut short";
        return std::nullopt;
    }
    const int tracks = (int) readBig (b, at + 2, 2);
    const int division = (int) readBig (b, at + 4, 2);
    if ((division & 0x8000) != 0 || division <= 0)
    {
        error = (division & 0x8000) != 0 ? "timed in SMPTE frames rather than beats" : "a beat of zero ticks";
        return std::nullopt;
    }
    at += headerLength;

    midiimport::ParsedMidi out;
    bool tempoSeen = false, meterSeen = false;
    const auto toLynx = [division] (std::uint64_t tick) { return (std::int64_t) ((tick * (std::uint64_t) ppq + (std::uint64_t) division / 2) / (std::uint64_t) division); };
    for (int t = 0; t < tracks && at + 8 <= b.size(); ++t)
    {
        // chunks that are not tracks are allowed and skipped, as the standard asks
        while (at + 8 <= b.size() && ! chunkIs (b, at, "MTrk"))
            at += 8 + readBig (b, at + 4, 4);
        if (at + 8 > b.size())
            break;
        const auto length = readBig (b, at + 4, 4);
        at += 8;
        const std::size_t end = std::min (b.size(), at + (std::size_t) length);
        const int track = (int) out.trackNames.size();
        std::string name;
        std::uint64_t tick = 0;
        int status = 0;
        while (at < end)
        {
            tick += readVariable (b, at, end);
            if (at >= end)
                break;
            if (b[at] >= 0x80)
                status = b[at++];
            else if (status < 0x80 || status >= 0xF0)
                break; // a data byte with no status to run on: the rest of the track is unreadable
            if (status == 0xFF)
            {
                if (at >= end)
                    break;
                const int type = b[at++];
                std::size_t len = readVariable (b, at, end);
                len = std::min (len, end - at);
                if (type == 0x51 && len >= 3 && ! tempoSeen)
                {
                    const auto mpq = readBig (b, at, 3);
                    if (mpq > 0)
                        out.bpm = 60000000.0 / mpq;
                    tempoSeen = true;
                }
                else if (type == 0x58 && len >= 2 && ! meterSeen)
                {
                    out.numerator = b[at];
                    out.denominator = 1 << std::min (6, (int) b[at + 1]);
                    meterSeen = true;
                }
                else if (type == 0x03 && len > 0 && name.empty())
                {
                    name.assign ((const char*) b.data() + at, len);
                    while (! name.empty() && (unsigned char) name.back() <= ' ')
                        name.pop_back();
                }
                at += len;
                status = 0; // meta events cancel running status
                if (type == 0x2F)
                    break;
                continue;
            }
            if (status == 0xF0 || status == 0xF7)
            {
                const std::size_t len = readVariable (b, at, end);
                at = std::min (end, at + len);
                status = 0;
                continue;
            }
            const int kind = status & 0xF0;
            const int dataBytes = kind == 0xC0 || kind == 0xD0 ? 1 : 2;
            if (at + (std::size_t) dataBytes > end)
                break;
            midiimport::RawEvent e;
            e.track = track;
            e.tick = toLynx (tick);
            e.status = (std::uint8_t) status;
            e.data1 = (std::uint8_t) (b[at] & 0x7F);
            e.data2 = dataBytes > 1 ? (std::uint8_t) (b[at + 1] & 0x7F) : 0;
            at += (std::size_t) dataBytes;
            if (kind != 0xA0 && kind != 0xD0)
                out.events.push_back (e);
        }
        out.trackNames.push_back (name);
        at = end;
    }
    return out;
}

std::optional<midiimport::ParsedMidi> readFile (const std::string& path, std::string& error)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
    {
        error = "cannot open " + path;
        return std::nullopt;
    }
    const std::vector<std::uint8_t> bytes ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
    return parse (bytes, error);
}

std::array<int, 16> programs (const midiimport::ParsedMidi& midi)
{
    std::array<int, 16> p;
    p.fill (-1);
    std::int64_t firstTick[16];
    std::fill (std::begin (firstTick), std::end (firstTick), INT64_MAX);
    for (const auto& e : midi.events)
        if ((e.status & 0xF0) == 0xC0 && e.tick < firstTick[e.status & 0x0F])
        {
            firstTick[e.status & 0x0F] = e.tick;
            p[(std::size_t) (e.status & 0x0F)] = e.data1;
        }
    return p;
}
} // namespace lynx::smf
