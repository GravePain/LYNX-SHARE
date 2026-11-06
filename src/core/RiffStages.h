#pragma once

// Riff stages LYNX's riff machine lacks (GravePain port, step 4; our own code, from GravePain's RiffChain). The riff
// machine (prtools::riffMachine) already has arp, mirror (flip time / pitch), levels (randomise), articulate and
// groove (quantize to a score); these add what GravePain's chain does beyond it:
// - Mirror: time turned over keeping the starts (the rhythm stays, the pitches play in the opposite order), and
//   pitch turned over about a home note landing in the key;
// - Levels: every note reset to one velocity before the wander;
// - Articulation: lengths multiplied past 100 % (up to 4x), and held notes chopped into repeated hits on a grid;
// - Groove: built-in templates (straight, swing 8ths, swing 16ths, laid back, pushed) applied with a timing and a
//   level strength, no score file needed.
// All seeded where random. RiffSettings holds every stage's settings as text so the same roll comes back with the
// pattern (Pattern::toolSettings). Pure functions over Pattern, no JUCE.

#include <cstdint>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::prtools::riff
{
// --- mirror ------------------------------------------------------------------------------------
// The k-th distinct start takes the notes of the k-th start from the end (lengths and levels go with them).
void mirrorKeepStarts (Pattern& pattern, const std::vector<int>& indices);
// Each note's key becomes 2 * home - key, snapped into the key (root 0..11, notetools::scales index; 0 = none).
void mirrorPitch (Pattern& pattern, const std::vector<int>& indices, int home, int root, int scale);

// --- levels ------------------------------------------------------------------------------------
void resetLevels (Pattern& pattern, const std::vector<int>& indices, float velocity = 100.0f / 127.0f);

// --- articulation ------------------------------------------------------------------------------
struct LengthOptions
{
    float multiply = 1.0f;       // 0.25..4
    std::int64_t gap = 0;        // ticks taken off every note's end
    float variation = 0.0f;      // 0..1: each length varied by up to half this, both ways
    std::uint32_t seed = 1;
};
void shapeLengths (Pattern& pattern, const std::vector<int>& indices, const LengthOptions& options);
// Notes longer than `grid` ticks become repeated hits every `grid` ticks over their length. Returns how many notes
// were added.
int chopToGrid (Pattern& pattern, const std::vector<int>& indices, std::int64_t grid);

// --- groove ------------------------------------------------------------------------------------
enum class Groove { straight, swing8, swing16, laidBack, pushed };
inline constexpr int grooveCount = 5;
const char* grooveLabel (Groove groove) noexcept; // "Straight", "Swing 8ths", ...
// The template's shift for the sixteenth `step` (0..15), in sixteenths of a step (8 = half a step late).
int grooveNudge (Groove groove, int step) noexcept;
// Moves every note off its sixteenth as the template says, by `timing` (0..1). Notes never start before 0.
void applyGroove (Pattern& pattern, const std::vector<int>& indices, Groove groove, float timing);

// --- settings kept with the pattern ------------------------------------------------------------
struct RiffSettings
{
    std::uint32_t seed = 1;
    bool mirrorOn = false, mirrorTime = true, mirrorKeepStarts = false, mirrorPitch = false;
    bool levelsOn = false, levelsReset = false, levelsBipolar = true;
    float levelsAmount = 0.5f, levelsLength = 0.0f;
    bool artOn = false, artChop = false;
    float artMultiply = 1.0f, artGap = 0.0f, artVariation = 0.0f; // gap in steps (0..0.5)
    bool grooveOn = false;
    Groove groove = Groove::swing8;
    float grooveTiming = 0.5f;
    bool arpOn = false;           // the arp stage is LYNX's Arpeggiator (notetools::arpeggiate)
    int arpPattern = 0, arpRange = 1;
    bool operator== (const RiffSettings&) const = default;
};
std::string toText (const RiffSettings& s);    // "key=value" lines
RiffSettings fromText (const std::string& text); // unknown keys ignored, missing ones default

// Runs the switched-on stages in order (mirror, levels, articulation, groove) on `indices`; the arp stage is left to
// the caller (LYNX's Arpeggiator). `home` is the pitch mirror's centre, root / scale the key, `grid` the chop grid.
void run (Pattern& pattern, const std::vector<int>& indices, const RiffSettings& s, int home, int root, int scale, std::int64_t grid);

// Pattern::toolSettings holds several tools' settings as "[section]" blocks; these read and replace one block.
std::string section (const std::string& toolSettings, const std::string& name);
std::string withSection (const std::string& toolSettings, const std::string& name, const std::string& body);
} // namespace lynx::prtools::riff
