// The GravePain Synth's sounds (GravePain port, step 5): its sound types, the dials of each type (GravePain's
// SynthMacros) and its factory presets (GravePain's SynthFactoryPresets), the author's own; the tables are generated
// from GravePain's sources by tools/gravepain/convert_presets.py into core/GpSynthBank.inc. The presets are also
// LYNX's preset bank for the engine (core/LynxBanks.h).
//
// Not carried over: the four piano presets (they play FL Keys multisamples, which LYNX does not ship) and the synth's
// own arpeggiator settings (a LYNX channel has its own arpeggiator). GravePain's "Digital" and "Formant" wavetable
// banks are not shipped either; the presets that read them use LYNX's own generated banks (docs/GRAVEPAIN_PORT.md).

#include <algorithm>
#include <cmath>

#include "core/GpSynth.h"
#include "core/LynxBanks.h"

namespace lynx::gpsynth
{
#include "core/GpSynthBank.inc"

const std::vector<std::string>& soundTypes()
{
    static const std::vector<std::string> types = [] {
        std::vector<std::string> t;
        for (const auto& set : macroSets())
            t.emplace_back (set.type);
        t.emplace_back ("Open Hat");
        return t;
    }();
    return types;
}

int soundTypeIndex (const std::string& type)
{
    const auto& t = soundTypes();
    const auto it = std::find (t.begin(), t.end(), type);
    return it == t.end() ? 0 : (int) (it - t.begin());
}

const MacroSet& macrosFor (int type)
{
    const auto& sets = macroSets();
    const auto& types = soundTypes();
    if (type >= 0 && type < (int) types.size() && types[(std::size_t) type] == "Open Hat")
        type = soundTypeIndex ("Hihat");
    return sets[(std::size_t) std::clamp (type, 0, (int) sets.size() - 1)];
}

const Macro* macroOfParam (int index) noexcept
{
    if (index < macroParam0)
        return nullptr;
    const int type = (index - macroParam0) / macroCount, d = (index - macroParam0) % macroCount;
    if (type >= (int) soundTypes().size())
        return nullptr;
    const auto& set = macrosFor (type);
    return d < (int) set.macros.size() ? &set.macros[(std::size_t) d] : nullptr;
}

namespace
{
float partAt (const MacroPart& part, float t) noexcept
{
    if (part.log && part.from > 0.0f && part.to > 0.0f)
        return part.from * std::pow (part.to / part.from, t);
    return part.from + (part.to - part.from) * t;
}
} // namespace

void setMacro (Patch& p, const Macro& m, float position) noexcept
{
    const float t = position < 0.0f ? 0.0f : (position > 1.0f ? 1.0f : position);
    const auto& table = patchParams();
    for (const auto& part : m.parts)
    {
        const auto& info = table[(std::size_t) part.field].param;
        float v = partAt (part, t);
        if (info.stepped)
            v = std::round (v); // a switch is on from half way, a count rounds
        p[(std::size_t) part.field] = std::clamp (v, info.low, info.high);
    }
}

float macroPosition (const Patch& p, const Macro& m) noexcept
{
    if (m.parts.empty())
        return 0.0f;
    const auto& part = m.parts.front();
    const float v = p[(std::size_t) part.field];
    if (part.from == part.to)
        return 0.0f;
    float t;
    if (part.log && part.from > 0.0f && part.to > 0.0f && v > 0.0f)
        t = std::log (v / part.from) / std::log (part.to / part.from);
    else
        t = (v - part.from) / (part.to - part.from);
    return t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
}

Patch patchOf (const FactoryPreset& preset)
{
    auto p = defaultPatch();
    for (const auto& [field, value] : preset.values)
        p[(std::size_t) field] = value;
    p[soundType] = (float) soundTypeIndex (preset.type);
    return patchOf (toModelParams (p)); // (clamped as any patch is)
}
} // namespace lynx::gpsynth

namespace lynx::banks
{
std::vector<Preset> gravepainBank()
{
    std::vector<Preset> bank;
    for (const auto& f : gpsynth::factoryPresets())
    {
        Preset p;
        p.name = f.name;
        p.category = f.type;
        for (const auto* c : f.character)
        {
            std::string word = c;
            std::transform (word.begin(), word.end(), word.begin(), [] (unsigned char ch) { return (char) std::tolower (ch); });
            p.tags.push_back (word);
        }
        const auto patch = gpsynth::patchOf (f);
        p.apply = [patch] (ChannelDef& c) { c.modelParams = gpsynth::toModelParams (patch); };
        bank.push_back (std::move (p));
    }
    return bank;
}
} // namespace lynx::banks
