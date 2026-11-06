#pragma once

// What the user keeps, remembered (GravePain port, step 2; our own code, from GravePain's PatternTaste): a score per
// key ("pattern:<id>", "recipe:<name>"). Keeping a pick adds 1, undoing it straight after -1, re-rolling past it
// -0.25; scores stay in -5..20. A pick's weight is 1 + score / 2 held in 0.1..4, so something undone again and again
// still comes up (a tenth as often) and something kept often never crowds out the rest. The file lives in LYNX's
// user settings folder (the caller gives the path), never in a project. Not for the audio thread.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace lynx::taste
{
inline constexpr float kept = 1.0f, backed = -1.0f, passedOver = -0.25f;
inline constexpr float scoreFloor = -5.0f, scoreCeiling = 20.0f;
inline constexpr float weightFloor = 0.1f, weightCeiling = 4.0f;

class Taste
{
public:
    float score (const std::string& key) const;
    void add (const std::vector<std::string>& keys, float delta);
    float weight (const std::string& key) const;

    // The index the seed picks from `weights` (each at least 0.0001); -1 for none. Deterministic for a seed.
    static int pickWeighted (const std::vector<float>& weights, std::uint32_t seed);

    // "key<TAB>score" lines; unreadable lines are skipped.
    std::string toText() const;
    static Taste fromText (const std::string& text);
    bool load (const std::string& path);       // false (and empty) when the file is missing
    bool save (const std::string& path) const; // creates the folder

    bool operator== (const Taste&) const = default;

private:
    std::map<std::string, float> scores;
};
} // namespace lynx::taste
