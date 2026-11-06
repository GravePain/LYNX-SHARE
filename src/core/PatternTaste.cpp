#include "core/PatternTaste.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace lynx::taste
{
float Taste::score (const std::string& key) const
{
    const auto it = scores.find (key);
    return it == scores.end() ? 0.0f : it->second;
}

void Taste::add (const std::vector<std::string>& keys, float delta)
{
    for (const auto& key : keys)
        if (! key.empty())
            scores[key] = std::clamp (score (key) + delta, scoreFloor, scoreCeiling);
}

float Taste::weight (const std::string& key) const
{
    return std::clamp (1.0f + 0.5f * score (key), weightFloor, weightCeiling);
}

int Taste::pickWeighted (const std::vector<float>& weights, std::uint32_t seed)
{
    if (weights.empty())
        return -1;
    double total = 0.0;
    for (float w : weights)
        total += std::max (0.0001f, w);
    // splitmix32-style hash of the seed: one roll in 0..1
    std::uint32_t z = seed + 0x9E3779B9u;
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    z ^= z >> 16;
    double roll = (double) z / 4294967296.0 * total;
    for (std::size_t i = 0; i < weights.size(); ++i)
    {
        roll -= std::max (0.0001f, weights[i]);
        if (roll <= 0.0)
            return (int) i;
    }
    return (int) weights.size() - 1;
}

std::string Taste::toText() const
{
    std::ostringstream o;
    for (const auto& [key, s] : scores)
        o << key << '\t' << s << '\n';
    return o.str();
}

Taste Taste::fromText (const std::string& text)
{
    Taste t;
    std::istringstream in (text);
    std::string line;
    while (std::getline (in, line))
    {
        const auto tab = line.rfind ('\t');
        if (tab == std::string::npos || tab == 0)
            continue;
        try
        {
            t.scores[line.substr (0, tab)] = std::clamp (std::stof (line.substr (tab + 1)), scoreFloor, scoreCeiling);
        }
        catch (const std::exception&)
        {
        }
    }
    return t;
}

bool Taste::load (const std::string& path)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
    {
        scores.clear();
        return false;
    }
    std::ostringstream all;
    all << in.rdbuf();
    *this = fromText (all.str());
    return true;
}

bool Taste::save (const std::string& path) const
{
    std::error_code ec;
    std::filesystem::create_directories (std::filesystem::path (path).parent_path(), ec);
    std::ofstream out (path, std::ios::binary | std::ios::trunc);
    out << toText();
    return (bool) out;
}
} // namespace lynx::taste
