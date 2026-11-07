#include "core/GpHelm.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <variant>

namespace lynx::gpsynth
{
namespace
{
// --- a small JSON reader: enough for a .helm file ------------------------------------------------------
struct Json;
using Object = std::map<std::string, Json>;
using Array = std::vector<Json>;
struct Json
{
    std::variant<std::nullptr_t, bool, double, std::string, std::shared_ptr<Object>, std::shared_ptr<Array>> v;
    const Object* object() const { auto* p = std::get_if<std::shared_ptr<Object>> (&v); return p ? p->get() : nullptr; }
    const Array* array() const { auto* p = std::get_if<std::shared_ptr<Array>> (&v); return p ? p->get() : nullptr; }
    const double* number() const { return std::get_if<double> (&v); }
    const std::string* string() const { return std::get_if<std::string> (&v); }
};

struct Parser
{
    const std::string& t;
    std::size_t i = 0;
    void skip()
    {
        while (i < t.size() && std::isspace ((unsigned char) t[i]))
            ++i;
    }
    [[noreturn]] void fail (const char* what) { throw std::runtime_error (what); }
    Json value()
    {
        skip();
        if (i >= t.size())
            fail ("the file ends early");
        const char c = t[i];
        if (c == '{')
        {
            auto o = std::make_shared<Object>();
            ++i;
            skip();
            if (i < t.size() && t[i] == '}')
            {
                ++i;
                return { o };
            }
            while (true)
            {
                skip();
                auto key = str();
                skip();
                if (i >= t.size() || t[i] != ':')
                    fail ("expected ':'");
                ++i;
                (*o)[key] = value();
                skip();
                if (i < t.size() && t[i] == ',')
                {
                    ++i;
                    continue;
                }
                if (i < t.size() && t[i] == '}')
                {
                    ++i;
                    return { o };
                }
                fail ("expected ',' or '}'");
            }
        }
        if (c == '[')
        {
            auto a = std::make_shared<Array>();
            ++i;
            skip();
            if (i < t.size() && t[i] == ']')
            {
                ++i;
                return { a };
            }
            while (true)
            {
                a->push_back (value());
                skip();
                if (i < t.size() && t[i] == ',')
                {
                    ++i;
                    continue;
                }
                if (i < t.size() && t[i] == ']')
                {
                    ++i;
                    return { a };
                }
                fail ("expected ',' or ']'");
            }
        }
        if (c == '"')
            return { str() };
        if (t.compare (i, 4, "true") == 0)
        {
            i += 4;
            return { true };
        }
        if (t.compare (i, 5, "false") == 0)
        {
            i += 5;
            return { false };
        }
        if (t.compare (i, 4, "null") == 0)
        {
            i += 4;
            return { nullptr };
        }
        std::size_t used = 0;
        const double d = std::stod (t.substr (i, 64), &used);
        if (used == 0)
            fail ("unreadable value");
        i += used;
        return { d };
    }
    std::string str()
    {
        if (i >= t.size() || t[i] != '"')
            fail ("expected a string");
        ++i;
        std::string out;
        while (i < t.size() && t[i] != '"')
        {
            if (t[i] == '\\' && i + 1 < t.size())
            {
                ++i;
                const char e = t[i];
                out += e == 'n' ? '\n' : e == 't' ? '\t' : e;
                if (e == 'u')
                    i += 4, out.back() = '?';
            }
            else
                out += t[i];
            ++i;
        }
        ++i;
        return out;
    }
};

float V (const Object& s, const char* key, float fallback = 0.0f)
{
    const auto it = s.find (key);
    if (it == s.end())
        return fallback;
    if (const auto* d = it->second.number())
        return (float) *d;
    return fallback;
}

std::string S (const Object& s, const char* key, const std::string& fallback)
{
    const auto it = s.find (key);
    if (it != s.end())
        if (const auto* str = it->second.string(); str != nullptr && ! str->empty())
            return *str;
    return fallback;
}

float sq (float v) { return v * v; }
float c01 (float v) { return std::clamp (v, 0.0f, 1.0f); }
float hz (float exponent) { return std::pow (2.0f, exponent); }

constexpr Wave helmWaves[12] = { Wave::sine, Wave::triangle, Wave::square, Wave::sawDown, Wave::saw, Wave::threeStep,
                                 Wave::fourStep, Wave::eightStep, Wave::threePyramid, Wave::fivePyramid, Wave::ninePyramid, Wave::sine };

// Helm's tempo index (32/1 ... 1/64) and sync kind (1 straight, 2 dotted, 3 triplet)
Division division (float index, float sync)
{
    const int i = (int) std::lround (index), kind = (int) std::lround (sync);
    if (kind == 2)
        return i <= 7 ? Division::quarter : Division::dottedEighth;
    if (kind == 3)
        return i <= 8 ? Division::eighthTriplet : Division::sixteenthTriplet;
    switch (i)
    {
        case 6: return Division::half;
        case 7: return Division::quarter;
        case 8: return Division::eighth;
        case 9: return Division::sixteenth;
        case 10: case 11: return Division::thirtySecond;
        default: return Division::whole;
    }
}

LfoWave lfo (float index)
{
    switch ((int) std::lround (index))
    {
        case 1: return LfoWave::triangle;
        case 2: return LfoWave::square;
        case 3: case 4: return LfoWave::saw;
        case 11: return LfoWave::random;
        default: return LfoWave::sine;
    }
}

ModSource source (const std::string& name)
{
    static const std::map<std::string, ModSource> m {
        { "amp_envelope", ModSource::ampEnv }, { "fil_envelope", ModSource::filterEnv }, { "mod_envelope", ModSource::freeEnv },
        { "mono_lfo_1", ModSource::lfo1 }, { "mono_lfo_2", ModSource::lfo2 }, { "poly_lfo", ModSource::polyLfo },
        { "velocity", ModSource::velocity }, { "note", ModSource::note }, { "mod_wheel", ModSource::modWheel },
        { "aftertouch", ModSource::aftertouch }, { "random", ModSource::random }, { "step_sequencer", ModSource::stepSeq },
    };
    const auto it = m.find (name);
    return it == m.end() ? ModSource::none : it->second;
}

// a Helm destination as a target, the amount in our units (1 = the target's full range)
bool target (const std::string& name, float amount, ModTarget& to, float& ours)
{
    ours = amount;
    if (name == "cutoff") { to = ModTarget::cutoff; ours = amount / 72.0f; return true; }
    if (name == "resonance") { to = ModTarget::resonance; return true; }
    if (name == "osc_1_transpose" || name == "osc_1_tune") { to = ModTarget::pitchA; ours = amount / 24.0f; return true; }
    if (name == "osc_2_transpose" || name == "osc_2_tune") { to = ModTarget::pitchB; ours = amount / 24.0f; return true; }
    if (name == "cross_modulation") { to = ModTarget::crossMod; ours = amount * 2.0f; return true; }
    if (name == "osc_1_volume") { to = ModTarget::oscMix; ours = -amount; return true; }
    if (name == "osc_2_volume") { to = ModTarget::oscMix; return true; }
    if (name == "noise_volume") { to = ModTarget::noiseLevel; return true; }
    if (name == "sub_volume") { to = ModTarget::subLevel; return true; }
    if (name == "filter_blend") { to = ModTarget::filterBlend; ours = amount / 2.0f; return true; }
    if (name == "filter_drive") { to = ModTarget::filterDrive; ours = amount / 20.0f; return true; }
    if (name == "volume") { to = ModTarget::amp; return true; }
    return false;
}

Patch map (const Object& s, const Json* modulations, std::vector<std::string>& unmapped)
{
    auto p = defaultPatch();
    const auto set = [&p] (Param k, float v) { p[(std::size_t) k] = v; };
    // oscillators: A's transpose is the patch's, B's counted from A
    const int wA = (int) std::lround (V (s, "osc_1_waveform", 4)), wB = (int) std::lround (V (s, "osc_2_waveform", 4));
    set (waveA, (float) helmWaves[std::clamp (wA, 0, 11)]);
    set (waveB, (float) helmWaves[std::clamp (wB, 0, 11)]);
    float a = sq (V (s, "osc_1_volume", 0.7071f)), b = sq (V (s, "osc_2_volume", 0.7071f));
    float noise = sq (V (s, "noise_volume"));
    if (wA == 11) // Helm's white-noise wave: our noise
        noise += a, a = 0.0f;
    if (wB == 11)
        noise += b, b = 0.0f;
    set (oscLevel, c01 (a + b));
    set (oscMix, a + b > 0.0f ? b / (a + b) : 0.0f);
    const float t1 = V (s, "osc_1_transpose") + V (s, "osc_1_tune"), t2 = V (s, "osc_2_transpose") + V (s, "osc_2_tune");
    set (transpose, (float) std::lround (V (s, "osc_1_transpose")));
    const float relative = t2 - t1;
    const int semis = std::clamp ((int) std::lround (relative), -48, 48);
    set (octaveB, 0);
    set (semitonesB, (float) semis);
    set (detuneCents, std::clamp ((relative - (float) semis) * 100.0f, -100.0f, 100.0f));
    if (std::abs (relative) > 48.5f)
        unmapped.push_back ("osc 2 " + std::to_string ((int) std::lround (relative)) + " semitones from osc 1 (48 at most here)");
    set (unisonVoices, (float) std::clamp ((int) std::lround (std::max (V (s, "osc_1_unison_voices", 1), V (s, "osc_2_unison_voices", 1))), 1, 15));
    set (unisonDetune, std::clamp (std::max (V (s, "osc_1_unison_detune"), V (s, "osc_2_unison_detune")), 0.0f, 100.0f));
    set (crossMod, c01 (V (s, "cross_modulation") * 2.0f));
    set (oscFeedback, c01 (std::abs (V (s, "osc_feedback_amount"))));
    set (subLevel, c01 (sq (V (s, "sub_volume"))));
    set (noiseLevel, c01 (noise));
    // filter: the cutoff a MIDI note; style 0 is 12 dB, 1 is 24 dB, 2 the shelves
    if (V (s, "filter_on", 1) > 0.5f)
    {
        set (cutoffHz, (float) (440.0 * std::pow (2.0, (V (s, "cutoff", 100) - 69.0) / 12.0)));
        set (resonance, c01 (V (s, "resonance")) * 0.97f);
        const float blend = c01 (V (s, "filter_blend") / 2.0f);
        const int style = (int) std::lround (V (s, "filter_style"));
        if (style == 2)
        {
            const int shelf = (int) std::lround (V (s, "filter_shelf"));
            set (filterMode, (float) (shelf == 0 ? FilterMode::lowShelf : shelf == 2 ? FilterMode::highShelf : FilterMode::blend));
            if (shelf == 1)
                set (filterBlend, 0.5f);
            set (shelfGainDb, (c01 (V (s, "resonance")) * 2.0f - 1.0f) * 24.0f);
            set (resonance, 0.2f);
        }
        else if (blend < 0.01f)
            set (filterMode, (float) (style == 1 ? FilterMode::lowPass24 : FilterMode::lowPass));
        else
        {
            set (filterMode, (float) FilterMode::blend);
            set (filterBlend, blend);
        }
        set (keyTrack, c01 (V (s, "keytrack")));
        set (filterEnvAmount, std::clamp (V (s, "fil_env_depth") / 72.0f, -1.0f, 1.0f));
        set (filterDrive, c01 (V (s, "filter_drive", -12) / 20.0f));
    }
    else
    {
        set (cutoffHz, 16000.0f);
        set (resonance, 0.0f);
        set (filterEnvAmount, 0.0f);
        set (keyTrack, 0.0f);
    }
    // envelopes: the square root of seconds
    set (attack, std::max (0.001f, sq (V (s, "amp_attack"))));
    set (decay, std::max (0.005f, sq (V (s, "amp_decay", 1))));
    set (sustain, c01 (V (s, "amp_sustain", 1)));
    set (release, std::max (0.005f, sq (V (s, "amp_release", 0.3f))));
    set (filterAttack, std::max (0.001f, sq (V (s, "fil_attack"))));
    set (filterDecay, std::max (0.005f, sq (V (s, "fil_decay", 1))));
    set (filterSustain, c01 (V (s, "fil_sustain")));
    set (filterRelease, std::max (0.005f, sq (V (s, "fil_release", 0.3f))));
    set (env3Attack, std::max (0.001f, sq (V (s, "mod_attack"))));
    set (env3Decay, std::max (0.005f, sq (V (s, "mod_decay", 1))));
    set (env3Sustain, c01 (V (s, "mod_sustain")));
    set (env3Release, std::max (0.005f, sq (V (s, "mod_release", 0.3f))));
    set (velocityToAmp, c01 (V (s, "velocity_track")));
    set (velocityToCutoff, 0.0f);
    // the LFOs and the sequencer: a frequency (2^value Hz) or, synced, a tempo index
    set (lfoWave, (float) lfo (V (s, "mono_lfo_1_waveform")));
    set (lfoSync, V (s, "mono_lfo_1_sync") > 0.5f ? 1.0f : 0.0f);
    set (lfoRateHz, hz (V (s, "mono_lfo_1_frequency", 1)));
    set (lfoDivision, (float) division (V (s, "mono_lfo_1_tempo", 7), V (s, "mono_lfo_1_sync")));
    set (lfo2Wave, (float) lfo (V (s, "mono_lfo_2_waveform")));
    set (lfo2Sync, V (s, "mono_lfo_2_sync") > 0.5f ? 1.0f : 0.0f);
    set (lfo2RateHz, hz (V (s, "mono_lfo_2_frequency", 1)));
    set (lfo2Division, (float) division (V (s, "mono_lfo_2_tempo", 7), V (s, "mono_lfo_2_sync")));
    set (polyLfoWave, (float) lfo (V (s, "poly_lfo_waveform")));
    set (polyLfoSync, V (s, "poly_lfo_sync") > 0.5f ? 1.0f : 0.0f);
    set (polyLfoRateHz, hz (V (s, "poly_lfo_frequency", 1)));
    set (polyLfoDivision, (float) division (V (s, "poly_lfo_tempo", 7), V (s, "poly_lfo_sync")));
    for (int i = 0; i < 16; ++i)
    {
        const std::string key = std::string ("step_seq_") + (i < 10 ? "0" : "") + std::to_string (i);
        set ((Param) (seqStep0 + i), c01 ((V (s, key.c_str()) + 1.0f) * 0.5f));
    }
    set (seqLength, (float) std::clamp ((int) std::lround (V (s, "num_steps", 16)), 1, 16));
    set (seqSmooth, V (s, "step_smoothing") > 0.01f ? 1.0f : 0.0f);
    set (seqDivision, (float) division (V (s, "step_sequencer_tempo", 9), std::max (1.0f, V (s, "step_sequencer_sync", 1))));
    if (V (s, "num_steps", 16) > 16)
        unmapped.push_back ("steps 17 to " + std::to_string ((int) V (s, "num_steps")) + " (16 here)");
    // voicing
    const bool mono = V (s, "polyphony", 8) <= 1.5f;
    set (voiceMode, (float) (! mono ? VoiceMode::poly : V (s, "legato") > 0.5f ? VoiceMode::legato : VoiceMode::mono));
    const float glideTime = hz (V (s, "portamento", -9));
    set (glide, V (s, "portamento_type") > 0.5f && glideTime > 0.01f ? std::min (1.0f, glideTime) : 0.0f);
    set (bendRange, (float) std::clamp ((int) std::lround (V (s, "pitch_bend_range", 2)), 0, 12));
    if (V (s, "arp_on") > 0.5f)
        unmapped.push_back ("the arpeggiator (use the channel's)");
    // effects, each only when Helm has it on
    if (V (s, "distortion_on") > 0.5f)
        set (drive, c01 (V (s, "distortion_drive") / 30.0f * c01 (V (s, "distortion_mix", 1))));
    if (V (s, "delay_on") > 0.5f)
    {
        set (delayMix, c01 (V (s, "delay_dry_wet")));
        set (delayFeedback, std::min (0.9f, std::abs (V (s, "delay_feedback"))));
        set (delaySync, V (s, "delay_sync", 1) > 0.5f ? 1.0f : 0.0f);
        set (delayDivision, (float) division (V (s, "delay_tempo", 9), V (s, "delay_sync", 1)));
        set (delayTimeMs, std::clamp (1000.0f / hz (V (s, "delay_frequency", 2)), 20.0f, 2000.0f));
    }
    if (V (s, "reverb_on") > 0.5f)
    {
        set (reverbMix, c01 (V (s, "reverb_dry_wet")));
        set (reverbSize, c01 ((V (s, "reverb_feedback", 0.9f) - 0.8f) / 0.2f));
        set (reverbDamp, c01 (V (s, "reverb_damping")));
    }
    if (V (s, "formant_on") > 0.5f)
    {
        set (formantMix, 1.0f);
        set (formantVowel, c01 (V (s, "formant_x", 0.5f)));
    }
    if (V (s, "stutter_on") > 0.5f)
    {
        set (stutterMix, 0.6f);
        set (stutterDivision, (float) division (V (s, "stutter_tempo", 8), std::max (1.0f, V (s, "stutter_sync", 1))));
    }
    set (masterVolume, std::clamp (sq (V (s, "volume", 0.7071f)), 0.0f, 1.0f));
    // the matrix
    int slot = 0;
    if (modulations != nullptr)
        if (const auto* list = modulations->array())
            for (const auto& m : *list)
            {
                const auto* mod = m.object();
                if (mod == nullptr)
                    continue;
                const auto src = S (*mod, "source", ""), dst = S (*mod, "destination", "");
                const auto from = source (src);
                ModTarget to {};
                float ours = 0.0f;
                if (from == ModSource::none || ! target (dst, V (*mod, "amount"), to, ours))
                {
                    unmapped.push_back (src + " > " + dst);
                    continue;
                }
                if (slot >= modSlots)
                {
                    unmapped.push_back (src + " > " + dst + " (past eight slots)");
                    continue;
                }
                set ((Param) modSource (slot), (float) from);
                set ((Param) modTarget (slot), (float) to);
                set ((Param) modAmount (slot), std::clamp (ours, -1.0f, 1.0f));
                set ((Param) modVia (slot), 0.0f);
                ++slot;
            }
    return patchOf (toModelParams (p)); // (clamped as any patch is)
}
} // namespace

std::optional<HelmResult> readHelm (const std::string& text, const std::string& fileName, std::string& problem)
{
    Json root;
    try
    {
        Parser parser { text };
        root = parser.value();
    }
    catch (const std::exception& e)
    {
        problem = e.what();
        return std::nullopt;
    }
    const auto* o = root.object();
    const Object* settings = nullptr;
    if (o != nullptr)
        if (const auto it = o->find ("settings"); it != o->end())
            settings = it->second.object();
    if (settings == nullptr)
    {
        problem = "no settings";
        return std::nullopt;
    }
    HelmResult r;
    const auto stem = std::filesystem::path (fileName).stem().string();
    r.name = S (*o, "patch_name", stem);
    r.folder = S (*o, "folder_name", std::filesystem::path (fileName).parent_path().filename().string());
    r.author = S (*o, "author", "");
    const Json* modulations = nullptr;
    if (const auto it = settings->find ("modulations"); it != settings->end())
        modulations = &it->second;
    else if (const auto top = o->find ("modulations"); top != o->end())
        modulations = &top->second;
    r.patch = map (*settings, modulations, r.unmapped);
    return r;
}

std::optional<HelmResult> readHelmFile (const std::string& path, std::string& problem)
{
    std::ifstream in (path, std::ios::binary);
    if (! in)
    {
        problem = "cannot open " + path;
        return std::nullopt;
    }
    std::ostringstream all;
    all << in.rdbuf();
    return readHelm (all.str(), path, problem);
}
} // namespace lynx::gpsynth
