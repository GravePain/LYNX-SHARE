#include "core/GpSynth.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>
#include <regex>

#include "core/AudioEdit.h"
#include "core/AudioEditTools.h"
#include "core/FlKeys.h"
#include "core/Instruments.h"
#include "core/ModelParams.h"
#include "core/Song.h"

namespace lynx::gpsynth
{
namespace
{
constexpr double pi = 3.14159265358979323846;
inline float clamp01 (float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float clampf (float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
inline bool finite (float v) noexcept { return std::isfinite (v); }
} // namespace

double divisionBeats (Division d) noexcept
{
    switch (d)
    {
        case Division::whole: return 4.0;
        case Division::half: return 2.0;
        case Division::quarter: return 1.0;
        case Division::dottedEighth: return 0.75;
        case Division::eighth: return 0.5;
        case Division::eighthTriplet: return 1.0 / 3.0;
        case Division::sixteenth: return 0.25;
        case Division::sixteenthTriplet: return 1.0 / 6.0;
        case Division::thirtySecond: break;
    }
    return 0.125;
}

// --- the parameter table ---------------------------------------------------------------------------
const std::vector<ParamInfo>& patchParams()
{
    static const std::vector<ParamInfo> table = [] {
        std::vector<ParamInfo> t ((std::size_t) paramCount);
        const auto set = [&t] (int i, const char* name, float lo, float hi, float def, bool stepped = false, std::vector<const char*> choices = {}) {
            auto& info = t[(std::size_t) i];
            info.param.index = i;
            info.param.name = name;
            info.param.low = lo;
            info.param.high = hi;
            info.param.stepped = stepped || ! choices.empty();
            info.defaultValue = def;
            info.choices = std::move (choices);
        };
        const std::vector<const char*> waves { "Sine", "Triangle", "Saw", "Square", "Wavetable", "Saw down", "3 steps", "4 steps", "8 steps",
                                               "3 pyramid", "5 pyramid", "9 pyramid" };
        const std::vector<const char*> lfoWaves { "Sine", "Triangle", "Saw", "Square", "Random" };
        const std::vector<const char*> divisions { "1", "1/2", "1/4", "1/8.", "1/8", "1/8T", "1/16", "1/16T", "1/32" };
        const std::vector<const char*> sources { "None", "Amp envelope", "Filter envelope", "Free envelope", "LFO 1", "LFO 2", "Poly LFO",
                                                 "Velocity", "Note", "Mod wheel", "Aftertouch", "Random", "Step sequencer" };
        const std::vector<const char*> targets { "None", "Pitch", "Pitch A", "Pitch B", "Osc mix", "Pulse width", "Position A", "Position B",
                                                 "Sub level", "Noise level", "Cutoff", "Resonance", "Filter blend", "Filter drive", "FM depth",
                                                 "Cross mod", "Amp", "Pan" };
        const std::vector<const char*> onOff { "Off", "On" };
        set (waveA, "Osc A wave", 0, 11, 2, true, waves);
        set (waveB, "Osc B wave", 0, 11, 3, true, waves);
        set (octaveB, "Osc B octave", -2, 2, 0, true);
        set (detuneCents, "Osc B detune", -100, 100, 7);
        set (oscMix, "Osc mix", 0, 1, 0.4f);
        set (pulseWidth, "Pulse width", 0.05f, 0.95f, 0.5f);
        set (subLevel, "Sub", 0, 1, 0);
        set (noiseLevel, "Noise", 0, 1, 0);
        set (oscLevel, "Osc level", 0, 1, 1);
        set (semitonesB, "Osc B semitones", -48, 48, 0, true);
        set (transpose, "Transpose", -48, 48, 0, true);
        set (unisonVoices, "Unison voices", 1, 15, 1, true);
        set (unisonDetune, "Unison detune", 0, 100, 18);
        set (stereoSpread, "Stereo spread", 0, 1, 0.7f);
        set (filterMode, "Filter mode", 0, 8, 0, true, { "Low pass", "Band pass", "High pass", "Notch", "Peak", "Low pass 24", "Blend", "Low shelf", "High shelf" });
        set (cutoffHz, "Cutoff", 30, 20000, 2200);
        set (resonance, "Resonance", 0, 0.97f, 0.35f);
        set (keyTrack, "Key track", 0, 1, 0.3f);
        set (filterEnvAmount, "Filter envelope", -1, 1, 0.45f);
        set (filterAttack, "Filter attack", 0.0005f, 10, 0.005f);
        set (filterDecay, "Filter decay", 0.001f, 10, 0.25f);
        set (filterSustain, "Filter sustain", 0, 1, 0.2f);
        set (filterRelease, "Filter release", 0.001f, 10, 0.3f);
        set (filterDrive, "Filter drive", 0, 1, 0);
        set (filterBlend, "Filter blend", 0, 1, 0);
        set (shelfGainDb, "Shelf gain", -24, 24, 0);
        set (filterSaturation, "Filter saturation", 0, 1, 0);
        set (attack, "Attack", 0.0005f, 10, 0.01f);
        set (decay, "Decay", 0.001f, 10, 0.18f);
        set (sustain, "Sustain", 0, 1, 0.65f);
        set (release, "Release", 0.001f, 10, 0.25f);
        set (velocityToAmp, "Velocity to level", 0, 1, 0.6f);
        set (velocityToCutoff, "Velocity to cutoff", 0, 1, 0.3f);
        set (masterVolume, "Volume", 0, 1.5f, 0.5f);
        set (pitchEnvSemitones, "Pitch envelope", -48, 48, 0);
        set (pitchEnvDecay, "Pitch envelope time", 0.005f, 16, 0.05f);
        set (pitchEnvCurve, "Pitch envelope curve", 0, 1, 0, true, { "Fall", "Build" });
        set (lfoWave, "LFO wave", 0, 4, 0, true, lfoWaves);
        set (lfoRateHz, "LFO rate", 0.01f, 40, 5);
        set (lfoSync, "LFO sync", 0, 1, 0, true, onOff);
        set (lfoDivision, "LFO division", 0, 8, 4, true, divisions);
        set (lfoToPitch, "LFO to pitch", 0, 100, 0);
        set (lfoToCutoff, "LFO to cutoff", 0, 4, 0);
        set (lfoToAmp, "LFO to level", 0, 1, 0);
        set (lfoToPulseWidth, "LFO to pulse width", 0, 0.45f, 0);
        set (lfo2Wave, "LFO 2 wave", 0, 4, 0, true, lfoWaves);
        set (lfo2RateHz, "LFO 2 rate", 0.01f, 40, 2);
        set (lfo2Sync, "LFO 2 sync", 0, 1, 0, true, onOff);
        set (lfo2Division, "LFO 2 division", 0, 8, 4, true, divisions);
        set (lfo2ToPitch, "LFO 2 to pitch", 0, 100, 0);
        set (lfo2ToCutoff, "LFO 2 to cutoff", 0, 4, 0);
        set (lfo2ToAmp, "LFO 2 to level", 0, 1, 0);
        set (lfo2ToPulseWidth, "LFO 2 to pulse width", 0, 0.45f, 0);
        set (voiceMode, "Voices", 0, 2, 0, true, { "Poly", "Mono", "Legato" });
        set (glide, "Glide", 0, 2, 0);
        set (bendRange, "Bend range", 0, 24, 2, true);
        set (fmDepth, "FM depth", 0, 1, 0);
        set (fmEnv, "FM envelope", 0, 1, 0);
        set (oscSaturation, "Osc saturation", 0, 1, 0);
        set (oscFeedback, "Osc feedback", 0, 1, 0);
        set (crossMod, "Cross mod", 0, 1, 0);
        set (noiseColour, "Noise colour", -1, 1, 0);
        set (clapHits, "Clap hits", 1, 8, 1, true);
        set (clapSpacingMs, "Clap spacing", 1, 100, 8);
        set (drive, "Drive", 0, 1, 0);
        set (crush, "Crush", 0, 1, 0);
        set (chorusMix, "Chorus", 0, 1, 0);
        set (chorusRateHz, "Chorus rate", 0.01f, 10, 0.6f);
        set (chorusDepth, "Chorus depth", 0, 1, 0.5f);
        set (delayMix, "Delay", 0, 1, 0);
        set (delayFeedback, "Delay feedback", 0, 0.9f, 0.35f);
        set (delaySync, "Delay sync", 0, 1, 1, true, onOff);
        set (delayDivision, "Delay division", 0, 8, 3, true, divisions);
        set (delayTimeMs, "Delay time", 1, 3000, 350);
        set (delayPingPong, "Ping pong", 0, 1, 1, true, onOff);
        set (reverbMix, "Reverb", 0, 1, 0);
        set (reverbSize, "Reverb size", 0, 1, 0.6f);
        set (reverbDamp, "Reverb damp", 0, 1, 0.4f);
        set (formantMix, "Formant", 0, 1, 0);
        set (formantVowel, "Vowel", 0, 1, 0);
        set (stutterMix, "Stutter", 0, 1, 0);
        set (stutterDivision, "Stutter division", 0, 8, 6, true, divisions);
        set (tableA, "Osc A table", 0, 2, 0, true, { "Basic shapes", "PWM", "Harmonics" });
        set (tableB, "Osc B table", 0, 2, 0, true, { "Basic shapes", "PWM", "Harmonics" });
        set (positionA, "Osc A position", 0, 1, 0);
        set (positionB, "Osc B position", 0, 1, 0);
        set (lfoToPosition, "LFO to position", -1, 1, 0);
        set (envToPosition, "Envelope to position", -1, 1, 0);
        set (sampleLevel, "Sample level", 0, 1, 0);
        set (sampleRoot, "Sample root", 0, 127, 60, true);
        static const char* slotNames[modSlots][4] = {
            { "Mod 1 source", "Mod 1 target", "Mod 1 amount", "Mod 1 via" }, { "Mod 2 source", "Mod 2 target", "Mod 2 amount", "Mod 2 via" },
            { "Mod 3 source", "Mod 3 target", "Mod 3 amount", "Mod 3 via" }, { "Mod 4 source", "Mod 4 target", "Mod 4 amount", "Mod 4 via" },
            { "Mod 5 source", "Mod 5 target", "Mod 5 amount", "Mod 5 via" }, { "Mod 6 source", "Mod 6 target", "Mod 6 amount", "Mod 6 via" },
            { "Mod 7 source", "Mod 7 target", "Mod 7 amount", "Mod 7 via" }, { "Mod 8 source", "Mod 8 target", "Mod 8 amount", "Mod 8 via" },
        };
        for (int s = 0; s < modSlots; ++s)
        {
            set (modSource (s), slotNames[s][0], 0, 12, 0, true, sources);
            set (modTarget (s), slotNames[s][1], 0, 17, 0, true, targets);
            set (modAmount (s), slotNames[s][2], -1, 1, 0);
            set (modVia (s), slotNames[s][3], 0, 12, 0, true, sources);
        }
        set (env3Attack, "Free envelope attack", 0.0005f, 10, 0.01f);
        set (env3Decay, "Free envelope decay", 0.001f, 10, 0.3f);
        set (env3Sustain, "Free envelope sustain", 0, 1, 0);
        set (env3Release, "Free envelope release", 0.001f, 10, 0.3f);
        set (polyLfoWave, "Poly LFO wave", 0, 4, 0, true, lfoWaves);
        set (polyLfoRateHz, "Poly LFO rate", 0.01f, 40, 4);
        set (polyLfoSync, "Poly LFO sync", 0, 1, 0, true, onOff);
        set (polyLfoDivision, "Poly LFO division", 0, 8, 2, true, divisions);
        static const char* stepNames[16] = { "Step 1", "Step 2", "Step 3", "Step 4", "Step 5", "Step 6", "Step 7", "Step 8",
                                             "Step 9", "Step 10", "Step 11", "Step 12", "Step 13", "Step 14", "Step 15", "Step 16" };
        for (int s = 0; s < 16; ++s)
            set (seqStep0 + s, stepNames[s], 0, 1, 0);
        set (seqLength, "Steps", 1, 16, 16, true);
        set (seqDivision, "Step division", 0, 8, 6, true, divisions);
        set (seqSmooth, "Smooth steps", 0, 1, 0, true, onOff);
        {
            static std::vector<const char*> typeNames;
            for (const auto& name : soundTypes())
                typeNames.push_back (name.c_str());
            set (soundType, "Sound type", 0, (float) soundTypes().size() - 1, 0, true, typeNames);
        }
        return t;
    }();
    return table;
}

const std::vector<EngineParam>& engineParams()
{
    static const std::vector<EngineParam> params = [] {
        std::vector<EngineParam> v;
        for (const auto& i : patchParams())
            v.push_back (i.param);
        const auto& types = soundTypes();
        for (int t = 0; t < (int) types.size(); ++t)
        {
            const auto& set = macrosFor (t);
            if (types[(std::size_t) t] != set.type)
                continue; // (Open Hat shares Hihat's dials)
            for (int d = 0; d < (int) set.macros.size() && d < macroCount; ++d)
            {
                EngineParam e;
                e.index = macroParam0 + t * macroCount + d;
                e.name = types[(std::size_t) t] + " " + set.macros[(std::size_t) d].name;
                v.push_back (e);
            }
        }
        return v;
    }();
    return params;
}

Patch defaultPatch()
{
    Patch p {};
    for (const auto& i : patchParams())
        p[(std::size_t) i.param.index] = i.defaultValue;
    return p;
}

Patch patchOf (const std::vector<float>& modelParams)
{
    auto p = defaultPatch();
    const auto& table = patchParams();
    for (std::size_t i = 0; i < p.size() && i < modelParams.size(); ++i)
    {
        const auto v = modelParams[i];
        if (! finite (v))
            continue;
        const auto& info = table[i].param;
        p[i] = clampf (info.stepped ? std::round (v) : v, info.low, info.high);
    }
    return p;
}

std::vector<float> toModelParams (const Patch& p)
{
    return { p.begin(), p.end() };
}

// --- wavetables -------------------------------------------------------------------------------------
float Wavetable::read (int frame, float frac, int mip, float phase) const noexcept
{
    mip = std::clamp (mip, 0, mips - 1);
    frame = std::clamp (frame, 0, frames - 1);
    const float x = phase * (float) tableFrameLength;
    int i = (int) x;
    i = std::clamp (i, 0, tableFrameLength - 1);
    const float t = x - (float) i;
    const int j = i + 1 == tableFrameLength ? 0 : i + 1;
    std::size_t at = ((std::size_t) mip * (std::size_t) frames + (std::size_t) frame) * tableFrameLength;
    const float a = data[at + (std::size_t) i] + (data[at + (std::size_t) j] - data[at + (std::size_t) i]) * t;
    if (frac <= 0.0f || frame + 1 >= frames)
        return a;
    at += tableFrameLength;
    const float b = data[at + (std::size_t) i] + (data[at + (std::size_t) j] - data[at + (std::size_t) i]) * t;
    return a + (b - a) * frac;
}

void Wavetable::place (float position, int& frame, float& frac) const noexcept
{
    position = clamp01 (position);
    const float x = position * (float) (frames - 1);
    frame = (int) x;
    if (frame >= frames - 1)
    {
        frame = frames - 1;
        frac = 0.0f;
        return;
    }
    frac = x - (float) frame;
}

int mipFor (float frequencyHz, float sampleRate, int mips) noexcept
{
    if (mips <= 1 || frequencyHz <= 0.0f || sampleRate <= 0.0f)
        return 0;
    const double level = std::ceil (std::log2 ((double) frequencyHz * tableFrameLength / sampleRate) - 1e-6);
    if (level < 0)
        return 0;
    return level >= mips ? mips - 1 : (int) level;
}

const std::vector<const char*>& tableNames()
{
    static const std::vector<const char*> names { "Basic shapes", "PWM", "Harmonics" };
    return names;
}

namespace
{
// harmonic amplitudes (sine terms, harmonic h at index h) of a frame at position t (0..1) of a bank
void spectrum (int bank, float t, std::vector<double>& amp)
{
    constexpr int harmonics = tableFrameLength / 2;
    std::fill (amp.begin(), amp.end(), 0.0);
    const auto sawAmp = [] (int h) { return 2.0 / (pi * h); };
    const auto squareAmp = [] (int h) { return h % 2 == 1 ? 4.0 / (pi * h) : 0.0; };
    const auto triangleAmp = [] (int h) { return h % 2 == 1 ? 8.0 / (pi * pi * h * h) * (((h - 1) / 2) % 2 == 0 ? 1.0 : -1.0) : 0.0; };
    for (int h = 1; h < harmonics; ++h)
    {
        double a = 0.0;
        if (bank == 0)
        {
            // sine -> triangle -> saw -> square across the bank
            const double x = t * 3.0;
            const double sine = h == 1 ? 1.0 : 0.0;
            if (x < 1.0)
                a = sine + (triangleAmp (h) - sine) * x;
            else if (x < 2.0)
                a = triangleAmp (h) + (sawAmp (h) - triangleAmp (h)) * (x - 1.0);
            else
                a = sawAmp (h) + (squareAmp (h) - sawAmp (h)) * (x - 2.0);
        }
        else if (bank == 1)
        {
            // a pulse narrowing from square (width 0.5) to 0.05: harmonic amplitude 2 sin(pi h w) / (pi h)
            const double w = 0.5 - 0.45 * t;
            a = 2.0 * std::sin (pi * h * w) / (pi * h);
        }
        else
        {
            // harmonics added one by one, 1 to 64, each at 1/h
            const double count = 1.0 + t * 63.0;
            if (h <= (int) count)
                a = 1.0 / h;
            else if (h == (int) count + 1)
                a = (count - (int) count) / h;
        }
        amp[(std::size_t) h] = a;
    }
}

std::unique_ptr<Wavetable> buildTable (int bank)
{
    auto t = std::make_unique<Wavetable>();
    t->name = tableNames()[(std::size_t) bank];
    t->data.assign ((std::size_t) tableMips * tableFrames * tableFrameLength, 0.0f);
    std::vector<double> amp (tableFrameLength / 2), re (tableFrameLength), im (tableFrameLength);
    for (int f = 0; f < tableFrames; ++f)
    {
        spectrum (bank, (float) f / (float) (tableFrames - 1), amp);
        // the level of the full-band frame, so every mip of a frame plays at one level
        double peak = 0.0;
        for (int mip = 0; mip < tableMips; ++mip)
        {
            const int keep = (tableFrameLength / 2) >> mip;
            std::fill (re.begin(), re.end(), 0.0);
            std::fill (im.begin(), im.end(), 0.0);
            for (int h = 1; h < keep && h < tableFrameLength / 2; ++h)
            {
                // a sine term a·sin(hθ): X[h] = -i a/2, X[N-h] = +i a/2
                im[(std::size_t) h] = -amp[(std::size_t) h] * 0.5;
                im[(std::size_t) (tableFrameLength - h)] = amp[(std::size_t) h] * 0.5;
            }
            audioedit::fftInPlace (re, im, true);
            if (mip == 0)
                for (double v : re)
                    peak = std::max (peak, std::abs (v));
            const double scale = peak > 1e-9 ? 1.0 / peak : 1.0;
            auto* out = t->data.data() + ((std::size_t) mip * tableFrames + (std::size_t) f) * tableFrameLength;
            for (int i = 0; i < tableFrameLength; ++i)
                out[i] = (float) (re[(std::size_t) i] * scale);
        }
    }
    return t;
}
} // namespace

const Wavetable* table (int index)
{
    static std::mutex gate;
    static std::array<std::unique_ptr<Wavetable>, 3> built;
    if (index < 0 || index >= (int) built.size())
        return nullptr;
    const std::lock_guard<std::mutex> lock (gate);
    if (built[(std::size_t) index] == nullptr)
        built[(std::size_t) index] = buildTable (index);
    return built[(std::size_t) index].get();
}

// --- sample sets ----------------------------------------------------------------------------------
const SampleZone* SampleSet::find (int note, float velocity) const noexcept
{
    if (zones.empty())
        return nullptr;
    const int v = (int) std::lround (clamp01 (velocity) * 127.0f);
    const SampleZone* best = nullptr;
    int bestDistance = INT32_MAX;
    for (const auto& z : zones)
    {
        const int distance = note < z.low ? z.low - note : (note > z.high ? note - z.high : 0);
        if (distance > bestDistance)
            continue;
        if (distance < bestDistance)
        {
            best = &z;
            bestDistance = distance;
            continue;
        }
        // the same range: the layer closest below the velocity wins; one above it only when nothing is below
        const bool bestBelow = best->velocityFrom <= v, thisBelow = z.velocityFrom <= v;
        if (thisBelow && (! bestBelow || z.velocityFrom > best->velocityFrom))
            best = &z;
        else if (! thisBelow && ! bestBelow && z.velocityFrom < best->velocityFrom)
            best = &z;
    }
    return best;
}

int rootFromName (const std::string& fileName)
{
    static const std::regex noteInName (R"((?:^|[^A-Za-z])([A-Ga-g])(#|b)?(-?\d)(?!\d))");
    static const std::regex numberInName (R"((?:^|\D)(\d{1,3})(?!\d))");
    std::smatch m;
    if (std::regex_search (fileName, m, noteInName))
    {
        static const int base[] = { 9, 11, 0, 2, 4, 5, 7 }; // A B C D E F G
        const int letter = std::toupper ((unsigned char) m.str (1)[0]) - 'A';
        int pc = base[letter];
        if (m.str (2) == "#")
            ++pc;
        else if (m.str (2) == "b")
            --pc;
        const int note = (std::stoi (m.str (3)) + 1) * 12 + pc; // C4 = 60
        if (note >= 0 && note <= 127)
            return note;
    }
    if (std::regex_search (fileName, m, numberInName))
        if (const int n = std::stoi (m.str (1)); n >= 0 && n <= 127)
            return n;
    return -1;
}

// --- effects -----------------------------------------------------------------------------------------
namespace
{
float driveSample (float x, float amount) noexcept
{
    if (amount <= 0.0f)
        return x;
    const float gain = 1.0f + amount * 9.0f;
    float y = clampf (x * gain, -3.0f, 3.0f);
    y = y * (27.0f + y * y) / (27.0f + 9.0f * y * y);
    return y / (1.0f + amount * 1.5f);
}

// a soft knee: straight to 0.5, then a curve that leaves at the same slope and never reaches 1
float knee (float x) noexcept
{
    constexpr float start = 0.5f;
    const float a = std::abs (x);
    if (a <= start)
        return x;
    const float over = a - start, room = 1.0f - start;
    const float y = start + over / (1.0f + over / room);
    return x < 0.0f ? -y : y;
}

float readFractional (const std::vector<float>& buffer, int writeAt, float delay) noexcept
{
    const int length = (int) buffer.size();
    float position = (float) writeAt - delay;
    while (position < 0.0f)
        position += (float) length;
    int i0 = (int) position;
    const float frac = position - (float) i0;
    if (i0 >= length)
        i0 -= length;
    int i1 = i0 + 1;
    if (i1 >= length)
        i1 = 0;
    return buffer[(std::size_t) i0] + (buffer[(std::size_t) i1] - buffer[(std::size_t) i0]) * frac;
}

class Effects
{
public:
    explicit Effects (float sr) : sampleRate (sr)
    {
        chorusL.assign ((std::size_t) (sr * 0.05f) + 4, 0.0f);
        chorusR = chorusL;
        delayL.assign ((std::size_t) (sr * 3.0f) + 4, 0.0f);
        delayR = delayL;
        stutterL.assign ((std::size_t) (sr * 2.0f) + 64, 0.0f);
        stutterR = stutterL;
        const float scale = sr / 44100.0f;
        for (std::size_t i = 0; i < 8; ++i)
        {
            combL[i].assign ((std::size_t) std::max (1, (int) ((float) combTunings[i] * scale)), 0.0f);
            combR[i].assign ((std::size_t) std::max (1, (int) ((float) (combTunings[i] + stereoSpreadSamples) * scale)), 0.0f);
        }
        for (std::size_t i = 0; i < 4; ++i)
        {
            allL[i].assign ((std::size_t) std::max (1, (int) ((float) allpassTunings[i] * scale)), 0.0f);
            allR[i].assign ((std::size_t) std::max (1, (int) ((float) (allpassTunings[i] + stereoSpreadSamples) * scale)), 0.0f);
        }
    }

    void process (float* left, float* right, int frames, const Patch& p, double secondsPerBeat) noexcept
    {
        crushStage (left, right, frames, p);
        const float d = clamp01 (p[drive]);
        if (d > 0.0f)
            for (int i = 0; i < frames; ++i)
            {
                left[i] = driveSample (left[i], d);
                right[i] = driveSample (right[i], d);
            }
        formantStage (left, right, frames, p);
        stutterStage (left, right, frames, p, secondsPerBeat);
        chorusStage (left, right, frames, p);
        delayStage (left, right, frames, p, secondsPerBeat);
        reverbStage (left, right, frames, p);
    }

private:
    static constexpr int combTunings[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
    static constexpr int allpassTunings[4] = { 556, 441, 341, 225 };
    static constexpr int stereoSpreadSamples = 23;

    void crushStage (float* left, float* right, int frames, const Patch& p) noexcept
    {
        const float amount = clamp01 (p[crush]);
        if (amount <= 0.0001f)
        {
            crushWasOn = false;
            return;
        }
        if (! crushWasOn)
        {
            crushHold = 0;
            crushHeldL = crushHeldR = 0.0f;
            crushWasOn = true;
        }
        // a frequency, not a sample count, so it crushes the same at any sample rate
        constexpr float topHz = 44100.0f, bottomHz = 2756.25f, topBits = 16.0f, bottomBits = 4.0f;
        const float holdHz = topHz * std::pow (bottomHz / topHz, amount);
        const int holdSamples = std::max (1, (int) std::lround (sampleRate / holdHz));
        const float bits = topBits - (topBits - bottomBits) * amount;
        const float levels = std::pow (2.0f, bits - 1.0f);
        for (int i = 0; i < frames; ++i)
        {
            if (--crushHold <= 0)
            {
                crushHold = holdSamples;
                crushHeldL = std::round (left[i] * levels) / levels;
                crushHeldR = std::round (right[i] * levels) / levels;
            }
            left[i] = crushHeldL;
            right[i] = crushHeldR;
        }
    }

    void formantStage (float* left, float* right, int frames, const Patch& p) noexcept
    {
        static constexpr float vowels[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 350, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
        static constexpr float levels[3] = { 1.0f, 0.5f, 0.25f };
        const float mix = clamp01 (p[formantMix]);
        if (mix <= 0.0001f)
        {
            formantWasOn = false;
            return;
        }
        if (! formantWasOn)
        {
            ic1.fill (0.0f);
            ic2.fill (0.0f);
            formantWasOn = true;
        }
        const float where = clamp01 (p[formantVowel]) * 4.0f;
        const int v = std::min (3, (int) where);
        const float t = where - (float) v;
        for (int start = 0; start < frames; start += scratchLength)
        {
            const int n = std::min (scratchLength, frames - start);
            for (int f = 0; f < 3; ++f)
            {
                const float hz = vowels[v][f] + (vowels[v + 1][f] - vowels[v][f]) * t;
                const float g = (float) std::tan (pi * std::min (hz, sampleRate * 0.45f) / sampleRate);
                constexpr float k = 0.1f; // a narrow band: a formant is a resonance
                const float c1 = 1.0f / (1.0f + g * (g + k)), c2 = g * c1, c3 = g * c2;
                for (int i = 0; i < n; ++i)
                    for (int side = 0; side < 2; ++side)
                    {
                        const std::size_t m = (std::size_t) (f * 2 + side);
                        const float input = side == 0 ? left[start + i] : right[start + i];
                        const float v3 = input - ic2[m];
                        const float v1 = c1 * ic1[m] + c2 * v3;
                        const float v2 = ic2[m] + c2 * ic1[m] + c3 * v3;
                        ic1[m] = 2.0f * v1 - ic1[m];
                        ic2[m] = 2.0f * v2 - ic2[m];
                        const float band = v1 * k * levels[f] * 4.0f;
                        auto& out = side == 0 ? formantL[(std::size_t) i] : formantR[(std::size_t) i];
                        out = (f == 0 ? 0.0f : out) + band;
                    }
            }
            for (int i = 0; i < n; ++i)
            {
                left[start + i] += (formantL[(std::size_t) i] - left[start + i]) * mix;
                right[start + i] += (formantR[(std::size_t) i] - right[start + i]) * mix;
            }
        }
    }

    void stutterStage (float* left, float* right, int frames, const Patch& p, double secondsPerBeat) noexcept
    {
        const float mix = clamp01 (p[stutterMix]);
        if (mix <= 0.0001f)
        {
            stutterWasOn = false;
            return;
        }
        const int slice = (int) std::max (64.0, std::min ((double) sampleRate * 2.0, divisionBeats ((Division) (int) p[stutterDivision]) * secondsPerBeat * sampleRate));
        if (! stutterWasOn)
        {
            stutterAt = 0;
            stutterWasOn = true;
        }
        for (int i = 0; i < frames; ++i)
        {
            const auto into = stutterAt % ((std::int64_t) slice * 4);
            const auto at = (std::size_t) (into % slice);
            if (into < slice)
            {
                stutterL[at] = left[i];
                stutterR[at] = right[i];
            }
            else
            {
                left[i] += (stutterL[at] - left[i]) * mix;
                right[i] += (stutterR[at] - right[i]) * mix;
            }
            ++stutterAt;
        }
    }

    void chorusStage (float* left, float* right, int frames, const Patch& p) noexcept
    {
        const float mix = clamp01 (p[chorusMix]);
        if (mix <= 0.0001f)
        {
            chorusWasOn = false;
            return;
        }
        if (! chorusWasOn)
        {
            std::fill (chorusL.begin(), chorusL.end(), 0.0f);
            std::fill (chorusR.begin(), chorusR.end(), 0.0f);
            chorusWasOn = true;
        }
        const double step = std::clamp ((double) p[chorusRateHz], 0.01, 10.0) / sampleRate;
        const float baseDelay = 0.012f * sampleRate, depth = 0.006f * sampleRate * clamp01 (p[chorusDepth]);
        const int length = (int) chorusL.size();
        for (int i = 0; i < frames; ++i)
        {
            chorusL[(std::size_t) chorusAt] = left[i];
            chorusR[(std::size_t) chorusAt] = right[i];
            // the two sides a quarter-cycle apart: the width
            const float modL = (float) std::sin (chorusPhase * 2.0 * pi), modR = (float) std::cos (chorusPhase * 2.0 * pi);
            const float wetL = readFractional (chorusL, chorusAt, baseDelay + depth * (0.5f + 0.5f * modL));
            const float wetR = readFractional (chorusR, chorusAt, baseDelay + depth * (0.5f + 0.5f * modR));
            left[i] = left[i] * (1.0f - mix * 0.5f) + wetL * mix;
            right[i] = right[i] * (1.0f - mix * 0.5f) + wetR * mix;
            chorusPhase += step;
            if (chorusPhase >= 1.0)
                chorusPhase -= 1.0;
            if (++chorusAt >= length)
                chorusAt = 0;
        }
    }

    void delayStage (float* left, float* right, int frames, const Patch& p, double secondsPerBeat) noexcept
    {
        const float mix = clamp01 (p[delayMix]);
        if (mix <= 0.0001f)
        {
            delayWasOn = false;
            return;
        }
        const int length = (int) delayL.size();
        if (! delayWasOn)
        {
            std::fill (delayL.begin(), delayL.end(), 0.0f);
            std::fill (delayR.begin(), delayR.end(), 0.0f);
            feedbackLowL = feedbackLowR = 0.0f;
            delaySamples = -1.0f;
            delayWasOn = true;
        }
        const double seconds = p[delaySync] >= 0.5f ? divisionBeats ((Division) (int) p[delayDivision]) * secondsPerBeat
                                                     : std::max (1.0, (double) p[delayTimeMs]) / 1000.0;
        const float target = (float) std::max (1.0, std::min ((double) length - 2, seconds * sampleRate));
        if (delaySamples < 0.0f)
            delaySamples = target;
        const float feedback = clampf (p[delayFeedback], 0.0f, 0.9f);
        const bool pingPong = p[delayPingPong] >= 0.5f;
        for (int i = 0; i < frames; ++i)
        {
            delaySamples += (target - delaySamples) * 0.0005f;
            const float echoL = readFractional (delayL, delayAt, delaySamples), echoR = readFractional (delayR, delayAt, delaySamples);
            // a gentle low-pass in the loop: each repeat darker than the last
            feedbackLowL += (echoL - feedbackLowL) * 0.45f;
            feedbackLowR += (echoR - feedbackLowR) * 0.45f;
            const float inL = left[i], inR = right[i];
            if (pingPong)
            {
                delayL[(std::size_t) delayAt] = (inL + inR) * 0.5f + feedbackLowR * feedback;
                delayR[(std::size_t) delayAt] = feedbackLowL * feedback;
            }
            else
            {
                delayL[(std::size_t) delayAt] = inL + feedbackLowL * feedback;
                delayR[(std::size_t) delayAt] = inR + feedbackLowR * feedback;
            }
            left[i] = inL + echoL * mix;
            right[i] = inR + echoR * mix;
            if (++delayAt >= length)
                delayAt = 0;
        }
    }

    void reverbStage (float* left, float* right, int frames, const Patch& p) noexcept
    {
        // Freeverb's topology (eight combs and four allpasses a side; Jezar placed it in the public domain)
        const float mix = clamp01 (p[reverbMix]);
        if (mix <= 0.0001f)
        {
            reverbWasOn = false;
            return;
        }
        if (! reverbWasOn)
        {
            for (std::size_t i = 0; i < 8; ++i)
            {
                std::fill (combL[i].begin(), combL[i].end(), 0.0f);
                std::fill (combR[i].begin(), combR[i].end(), 0.0f);
                combStoreL[i] = combStoreR[i] = 0.0f;
            }
            for (std::size_t i = 0; i < 4; ++i)
            {
                std::fill (allL[i].begin(), allL[i].end(), 0.0f);
                std::fill (allR[i].begin(), allR[i].end(), 0.0f);
            }
            reverbWasOn = true;
        }
        const float room = clamp01 (p[reverbSize]) * 0.28f + 0.7f, damp = clamp01 (p[reverbDamp]) * 0.4f;
        constexpr float gain = 0.015f;
        const auto comb = [damp, room] (std::vector<float>& buffer, int& at, float& store, float input) {
            const float output = buffer[(std::size_t) at];
            store = output * (1.0f - damp) + store * damp;
            buffer[(std::size_t) at] = input + store * room;
            if (++at >= (int) buffer.size())
                at = 0;
            return output;
        };
        const auto allpass = [] (std::vector<float>& buffer, int& at, float input) {
            const float held = buffer[(std::size_t) at];
            buffer[(std::size_t) at] = input + held * 0.5f;
            if (++at >= (int) buffer.size())
                at = 0;
            return held - input;
        };
        for (int i = 0; i < frames; ++i)
        {
            const float input = (left[i] + right[i]) * gain + 1e-18f; // keeps the tails out of denormals
            float outL = 0.0f, outR = 0.0f;
            for (std::size_t c = 0; c < 8; ++c)
            {
                outL += comb (combL[c], combAtL[c], combStoreL[c], input);
                outR += comb (combR[c], combAtR[c], combStoreR[c], input);
            }
            for (std::size_t a = 0; a < 4; ++a)
            {
                outL = allpass (allL[a], allAtL[a], outL);
                outR = allpass (allR[a], allAtR[a], outR);
            }
            left[i] = left[i] * (1.0f - mix * 0.5f) + outL * mix;
            right[i] = right[i] * (1.0f - mix * 0.5f) + outR * mix;
        }
    }

    float sampleRate;
    static constexpr int scratchLength = 1024;
    int crushHold = 0;
    float crushHeldL = 0, crushHeldR = 0;
    bool crushWasOn = false, formantWasOn = false, stutterWasOn = false, chorusWasOn = false, delayWasOn = false, reverbWasOn = false;
    std::array<float, 6> ic1 {}, ic2 {};
    std::array<float, scratchLength> formantL {}, formantR {};
    std::vector<float> stutterL, stutterR, chorusL, chorusR, delayL, delayR;
    std::int64_t stutterAt = 0;
    int chorusAt = 0, delayAt = 0;
    double chorusPhase = 0.0;
    float delaySamples = -1.0f, feedbackLowL = 0, feedbackLowR = 0;
    std::array<std::vector<float>, 8> combL, combR;
    std::array<std::vector<float>, 4> allL, allR;
    std::array<int, 8> combAtL {}, combAtR {};
    std::array<int, 4> allAtL {}, allAtR {};
    std::array<float, 8> combStoreL {}, combStoreR {};
};
} // namespace

// --- the engine ----------------------------------------------------------------------------------------
namespace
{
constexpr int maxVoices = 32, maxUnison = 15, oldVoices = 8, oldUnison = 7, maxHeld = 16, chunk = 1024, controlRate = 8;
constexpr float fmCycles = 0.8f, flatK = 1.41421356f;
enum class Stage { off, attack, decay, sustain, release };

struct Envelope
{
    Stage stage = Stage::off;
    float value = 0.0f;
    void tick (float attackStep, float decayC, float sustainLevel, float releaseC) noexcept
    {
        switch (stage)
        {
            case Stage::attack:
                value += attackStep;
                if (value >= 1.0f)
                {
                    value = 1.0f;
                    stage = Stage::decay;
                }
                break;
            case Stage::decay:
                value = sustainLevel + (value - sustainLevel) * decayC;
                if (std::abs (value - sustainLevel) < 0.0005f)
                {
                    value = sustainLevel;
                    stage = Stage::sustain;
                }
                break;
            case Stage::sustain: value = sustainLevel; break;
            case Stage::release:
                value *= releaseC;
                if (value < 0.0002f)
                {
                    value = 0.0f;
                    stage = Stage::off;
                }
                break;
            case Stage::off: break;
        }
    }
};

float blep (float t, float dt) noexcept
{
    if (dt <= 0.0f)
        return 0.0f;
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt)
    {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

float stairs (float phase, float step, int levels, bool pyramid) noexcept
{
    const int segments = pyramid ? levels * 2 - 2 : levels;
    const float rise = 2.0f / (float) (levels - 1);
    int at = (int) (phase * (float) segments);
    if (at >= segments)
        at = segments - 1;
    const int level = pyramid && at >= levels ? segments - at : at;
    float value = -1.0f + (float) level * rise;
    for (int j = 0; j < segments; ++j)
    {
        const float height = j == 0 ? (pyramid ? -rise : -2.0f) : (pyramid && j >= levels ? -rise : rise);
        float t = phase - (float) j / (float) segments;
        if (t < 0.0f)
            t += 1.0f;
        value += height * 0.5f * blep (t, step);
    }
    return value;
}

// one band-limited oscillator sample (PolyBLEP on the jumps)
float shape (Wave wave, float phase, float step, float width) noexcept
{
    switch (wave)
    {
        case Wave::sine: return (float) std::sin (phase * 2.0 * pi);
        case Wave::triangle: return 4.0f * std::abs (phase - 0.5f) - 1.0f;
        case Wave::saw: return (2.0f * phase - 1.0f) - blep (phase, step);
        case Wave::sawDown: return -((2.0f * phase - 1.0f) - blep (phase, step));
        case Wave::threeStep: return stairs (phase, step, 3, false);
        case Wave::fourStep: return stairs (phase, step, 4, false);
        case Wave::eightStep: return stairs (phase, step, 8, false);
        case Wave::threePyramid: return stairs (phase, step, 3, true);
        case Wave::fivePyramid: return stairs (phase, step, 5, true);
        case Wave::ninePyramid: return stairs (phase, step, 9, true);
        default:
        {
            const float sq = phase < width ? 1.0f : -1.0f;
            float other = phase + (1.0f - width);
            if (other >= 1.0f)
                other -= 1.0f;
            // re-centred, or a narrow pulse thumps as its width moves
            return sq + blep (phase, step) - blep (other, step) - (2.0f * width - 1.0f);
        }
    }
}

float svf (float input, float& ic1, float& ic2, float a1, float a2, float a3, float k, FilterMode mode) noexcept
{
    // Simper's trapezoidal state-variable filter
    const float v3 = input - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    switch (mode)
    {
        case FilterMode::highPass: return input - k * v1 - v2;
        case FilterMode::bandPass: return v1 * k;
        case FilterMode::notch: return input - k * v1;
        case FilterMode::peak: return input - k * v1 - 2.0f * v2;
        default: return v2;
    }
}

float bands (float input, float& ic1, float& ic2, float a1, float a2, float a3, float k, FilterMode mode, float blend, float gain) noexcept
{
    const float v3 = input - ic2;
    const float v1 = a1 * ic1 + a2 * v3;
    const float v2 = ic2 + a2 * ic1 + a3 * v3;
    ic1 = 2.0f * v1 - ic1;
    ic2 = 2.0f * v2 - ic2;
    const float low = v2, band = v1 * k, high = input - k * v1 - v2;
    switch (mode)
    {
        case FilterMode::lowShelf: return input + (gain - 1.0f) * low;
        case FilterMode::highShelf: return input + (gain - 1.0f) * high;
        default: return blend < 0.5f ? low + (band - low) * blend * 2.0f : band + (high - band) * (blend - 0.5f) * 2.0f;
    }
}

float lfoShape (LfoWave wave, float phase, bool wrapped, float& held, std::uint32_t& rng) noexcept
{
    switch (wave)
    {
        case LfoWave::sine: return (float) std::sin (phase * 2.0 * pi);
        case LfoWave::triangle: return 1.0f - 4.0f * std::abs (phase - 0.5f);
        case LfoWave::saw: return 1.0f - 2.0f * phase; // falling: a wobble opening and dying
        case LfoWave::square: return phase < 0.5f ? 1.0f : -1.0f;
        case LfoWave::random: break;
    }
    if (wrapped)
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        held = (float) (rng >> 8) / 16777216.0f * 2.0f - 1.0f;
    }
    return held;
}
} // namespace

class Engine
{
public:
    explicit Engine (double sr)
        : sampleRate ((float) (sr > 0.0 ? sr : 48000.0)), effects (sampleRate),
          smoothCoef ((float) (1.0 - std::exp (-1.0 / (sampleRate * 0.005)))),
          stealCoef ((float) std::exp (-1.0 / (sampleRate * 0.002)))
    {
        for (int i = 0; i < maxVoices; ++i)
            voices[(std::size_t) i].scatter (*this, i < oldVoices);
        patch = defaultPatch();
    }

    // --- set up (between blocks) -------------------------------------------------------------------
    void setPatch (const Patch& p) noexcept { patch = p; }
    void setTables (const Wavetable* a, const Wavetable* b) noexcept { tableAPtr = a, tableBPtr = b; }
    void setSamples (const SampleSet* s) noexcept { samples = s; }
    void setBpm (double b) noexcept { bpm = std::clamp (b, 20.0, 400.0); }
    void setPitchOffset (float cents) noexcept { pitchOffset = finite (cents) ? cents / 100.0f : 0.0f; }
    void setModWheel (float v) noexcept { modWheel = clamp01 (v); }
    void setAftertouch (float v) noexcept { aftertouch = clamp01 (v); }
    void setSustain (bool down) noexcept
    {
        sustainDown = down;
        if (! down)
            for (auto& v : voices)
                if (v.sustained)
                {
                    v.sustained = false;
                    v.release();
                }
    }

    // --- keys ----------------------------------------------------------------------------------------
    void noteOn (int note, float velocity) noexcept
    {
        if (note < 0 || note > 127)
            return;
        velocity = clamp01 (velocity);
        if ((VoiceMode) (int) patch[voiceMode] == VoiceMode::poly)
            return polyOn (note, velocity);
        // mono / legato: one voice and a stack of held notes
        removeFromStack (note);
        if (monoCount == maxHeld)
            removeAtStack (0);
        monoStack[(std::size_t) monoCount] = note;
        monoVelocity[(std::size_t) monoCount] = velocity;
        ++monoCount;
        auto& v = voices[0];
        for (std::size_t i = 1; i < voices.size(); ++i)
            if (voices[i].amp.stage != Stage::off)
                voices[i].release();
        const bool sounding = v.amp.stage != Stage::off && v.amp.stage != Stage::release;
        const bool retrigger = ! ((VoiceMode) (int) patch[voiceMode] == VoiceMode::legato && sounding);
        v.begin (note, velocity, ++age, retrigger, glideFrom (v, sounding));
        lastPitch = (float) note;
    }

    void noteOff (int note) noexcept
    {
        if ((VoiceMode) (int) patch[voiceMode] == VoiceMode::poly || monoCount == 0)
        {
            for (auto& v : voices)
            {
                if (v.stealing)
                {
                    if (v.pendingNote == note)
                    {
                        if (sustainDown)
                            v.pendingKeyUp = true;
                        else
                            v.pendingNote = -1;
                    }
                    continue;
                }
                if (v.note == note && v.gate)
                {
                    if (sustainDown)
                    {
                        v.gate = false;
                        v.sustained = true;
                    }
                    else
                        v.release();
                }
            }
            return;
        }
        const bool wasTop = monoStack[(std::size_t) monoCount - 1] == note;
        removeFromStack (note);
        if (! wasTop)
            return;
        auto& voice = voices[0];
        if (monoCount > 0)
        {
            const int back = monoStack[(std::size_t) monoCount - 1];
            voice.begin (back, monoVelocity[(std::size_t) monoCount - 1], ++age, (VoiceMode) (int) patch[voiceMode] == VoiceMode::mono, glideFrom (voice, true));
            lastPitch = (float) back;
        }
        else if (sustainDown)
        {
            voice.gate = false;
            voice.sustained = true;
        }
        else
            voice.release();
    }

    void allNotesOff() noexcept
    {
        monoCount = 0;
        sustainDown = false;
        for (auto& v : voices)
        {
            v.sustained = false;
            if (v.amp.stage != Stage::off)
                v.release();
        }
    }

    void kill() noexcept
    {
        for (auto& v : voices)
            v.silence();
        monoCount = 0;
    }

    int activeVoices() const noexcept
    {
        int n = 0;
        for (const auto& v : voices)
            n += v.amp.stage != Stage::off ? 1 : 0;
        return n;
    }

    // --- rendering -----------------------------------------------------------------------------------
    // adds n frames, times gainL / gainR, into left / right
    void render (float* left, float* right, int n, float gainL, float gainR) noexcept
    {
        int done = 0;
        while (done < n)
        {
            const int frames = std::min (chunk, n - done);
            renderChunk (frames);
            for (int i = 0; i < frames; ++i)
            {
                float l = knee (mixL[(std::size_t) i]), r = knee (mixR[(std::size_t) i]);
                // a bad sample out of the synth is silence, never a NaN into the mix
                if (! finite (l))
                    l = 0.0f;
                if (! finite (r))
                    r = 0.0f;
                left[done + i] += l * gainL;
                right[done + i] += r * gainR;
            }
            done += frames;
        }
    }

private:
    friend struct Voice;

    struct Voice
    {
        int note = -1;
        float pitch = 0, velocity = 1;
        std::int64_t startedAt = 0;
        bool gate = false, sustained = false;
        Envelope amp, filter, env3;
        std::array<float, maxUnison> phaseA {}, phaseB {}, lastA {}, lastB {};
        float phaseSub = 0;
        float polyPhase = 0, polyValue = 0, noteRandom = -2;
        std::array<float, 18> modSum {};
        bool modOn = false;
        float modMix = 0, modSub = 0, modNoise = 0, modDrive = 0, modFm = 0, modCross = 0, modAmp = 0, modPan = 0, modBlend = 0;
        float ic1L = 0, ic2L = 0, ic1R = 0, ic2R = 0, ic3L = 0, ic4L = 0, ic3R = 0, ic4R = 0;
        bool stealing = false;
        int pendingNote = -1;
        float pendingVelocity = 0, pendingGlide = -1;
        bool pendingKeyUp = false;
        int controlCountdown = 0;
        float stepA = 0, stepB = 0, width = 0.5f, a1 = 0, a2 = 0, a3 = 0, k2 = 0, b1 = 0, b2 = 0, b3 = 0;
        float pitchEnv = 0, pitchRamp = 0, noiseState = 0;
        int hitsLeft = 0, hitCountdown = 0;
        bool armHits = false, releaseAfterHit = false;
        double samplePos = 0;
        float sampleStep = 0;
        const SampleZone* zone = nullptr;
        bool zonePending = false;
        int frameA = 0, frameB = 0, mipA = 0, mipB = 0;
        float fracA = 0, fracB = 0;
        const Wavetable* bankA = nullptr;
        const Wavetable* bankB = nullptr;
        std::uint32_t* rng = nullptr;

        void scatter (Engine& e, bool original)
        {
            rng = &e.rng;
            // unison copies start at scattered phases, or every supersaw note starts as one loud saw
            for (int k = 0; k < maxUnison; ++k)
            {
                const bool fromMain = original && k < oldUnison;
                phaseA[(std::size_t) k] = (float) ((fromMain ? e.nextRandom() : e.nextExtra()) >> 8) / 16777216.0f;
                phaseB[(std::size_t) k] = (float) ((fromMain ? e.nextRandom() : e.nextExtra()) >> 8) / 16777216.0f;
            }
        }

        void clearFilter() noexcept { ic1L = ic2L = ic1R = ic2R = ic3L = ic4L = ic3R = ic4R = 0.0f; }

        void silence() noexcept
        {
            amp = filter = env3 = {};
            note = -1;
            gate = sustained = stealing = false;
            pendingNote = -1;
            clearFilter();
        }

        // starts (or re-aims) the voice; phases are not reset (a restart mid-cycle would click)
        void begin (int midiNote, float noteVelocity, std::int64_t when, bool retrigger, float glide) noexcept
        {
            stealing = false;
            pendingNote = -1;
            note = midiNote;
            velocity = noteVelocity;
            startedAt = when;
            gate = true;
            sustained = false;
            pitch = glide >= 0.0f ? glide : (float) midiNote;
            controlCountdown = 0;
            if (! retrigger && amp.stage != Stage::off && amp.stage != Stage::release)
                return;
            amp.stage = filter.stage = env3.stage = Stage::attack;
            polyPhase = 0.0f;
            noteRandom = -2.0f;
            pitchEnv = 1.0f;
            pitchRamp = 1.0f;
            armHits = true;
            samplePos = 0.0;
            zonePending = true;
            if (amp.value <= 0.0f)
                clearFilter(); // a silent voice starts with a clean filter
        }

        void stealFor (int midiNote, float noteVelocity, std::int64_t when, float glide) noexcept
        {
            stealing = true;
            pendingNote = midiNote;
            pendingVelocity = noteVelocity;
            pendingGlide = glide;
            pendingKeyUp = false;
            startedAt = when;
            gate = false;
            sustained = false;
        }

        void release() noexcept
        {
            if (stealing)
            {
                pendingNote = -1;
                gate = sustained = false;
                return;
            }
            gate = sustained = false;
            if (amp.stage != Stage::off)
                amp.stage = Stage::release;
            if (filter.stage != Stage::off)
                filter.stage = Stage::release;
            if (env3.stage != Stage::off)
                env3.stage = Stage::release;
        }

        float source (int src, const Engine& s, float lfo, float lfo2) noexcept
        {
            switch ((ModSource) src)
            {
                case ModSource::ampEnv: return amp.value;
                case ModSource::filterEnv: return filter.value;
                case ModSource::freeEnv: return env3.value;
                case ModSource::lfo1: return lfo;
                case ModSource::lfo2: return lfo2;
                case ModSource::polyLfo: return polyValue;
                case ModSource::velocity: return velocity;
                case ModSource::note: return clampf (((float) note - 60.0f) / 60.0f, -1.0f, 1.0f);
                case ModSource::modWheel: return s.modWheel;
                case ModSource::aftertouch: return s.aftertouch;
                case ModSource::random:
                    if (noteRandom < -1.0f)
                    {
                        auto& x = *rng;
                        x ^= x << 13;
                        x ^= x >> 17;
                        x ^= x << 5;
                        noteRandom = (float) (x >> 8) / 16777216.0f * 2.0f - 1.0f;
                    }
                    return noteRandom;
                case ModSource::stepSeq: return s.seqValue;
                default: return 1.0f;
            }
        }

        void modulate (const Engine& s, const Patch& p, float lfo, float lfo2) noexcept
        {
            if (s.modPolyLfo)
            {
                if (p[polyLfoSync] >= 0.5f)
                {
                    const double x = s.chunkBeat / s.polyLfoDivisionBeats;
                    polyPhase = (float) (x - std::floor (x));
                }
                else
                {
                    polyPhase += s.polyLfoStep * (float) controlRate;
                    if (polyPhase >= 1.0f)
                        polyPhase -= 1.0f;
                }
                const auto w = (LfoWave) (int) p[polyLfoWave];
                polyValue = w == LfoWave::sine ? (float) std::sin (polyPhase * 2.0 * pi)
                            : w == LfoWave::triangle ? 1.0f - 4.0f * std::abs (polyPhase - 0.5f)
                            : w == LfoWave::saw ? 1.0f - 2.0f * polyPhase
                            : w == LfoWave::square ? (polyPhase < 0.5f ? 1.0f : -1.0f)
                            : (float) std::sin (polyPhase * 13.7) * 0.5f + (float) std::sin (polyPhase * 29.3) * 0.5f;
            }
            modSum.fill (0.0f);
            for (int i = 0; i < s.modCount; ++i)
            {
                float value = source (s.modSourceOf[(std::size_t) i], s, lfo, lfo2) * s.modAmountOf[(std::size_t) i];
                if (s.modViaOf[(std::size_t) i] != (int) ModSource::none)
                    value *= source (s.modViaOf[(std::size_t) i], s, lfo, lfo2);
                modSum[(std::size_t) s.modTargetOf[(std::size_t) i]] += value;
            }
            modMix = modSum[(std::size_t) ModTarget::oscMix];
            modSub = modSum[(std::size_t) ModTarget::subLevel];
            modNoise = modSum[(std::size_t) ModTarget::noiseLevel];
            modDrive = modSum[(std::size_t) ModTarget::filterDrive];
            modFm = modSum[(std::size_t) ModTarget::fmDepth];
            modCross = modSum[(std::size_t) ModTarget::crossMod];
            modAmp = modSum[(std::size_t) ModTarget::amp];
            modPan = modSum[(std::size_t) ModTarget::pan];
            modBlend = modSum[(std::size_t) ModTarget::filterBlend];
        }

        float osc (Wave wave, const Wavetable* bank, int frame, float frac, int mip, float phase, float step) const noexcept
        {
            if (wave != Wave::wavetable)
                return shape (wave, phase, step, width);
            if (bank == nullptr)
                return shape (Wave::saw, phase, step, width);
            return bank->read (frame, frac, mip, phase);
        }

        float readSample (const SampleZone& z) noexcept
        {
            int i = (int) samplePos;
            if (z.loop && i + 1 >= z.loopEnd)
            {
                samplePos -= z.loopEnd - z.loopStart;
                if (samplePos < 0.0)
                    samplePos = 0.0;
                i = (int) samplePos;
            }
            if (i < 0 || i + 1 >= (int) z.data.size())
                return 0.0f;
            const float t = (float) (samplePos - i);
            samplePos += sampleStep;
            return z.data[(std::size_t) i] + (z.data[(std::size_t) i + 1] - z.data[(std::size_t) i]) * t;
        }

        void control (const Engine& s, const Patch& p, float lfo, float lfo2, float bendSemis) noexcept
        {
            modOn = s.modCount > 0;
            if (modOn)
                modulate (s, p, lfo, lfo2);
            else
                modBlend = 0.0f;
            float vibrato = lfo * (std::max (0.0f, p[lfoToPitch]) / 100.0f + s.modWheel * 0.5f);
            if (p[lfo2ToPitch] != 0.0f)
                vibrato += lfo2 * (std::max (0.0f, p[lfo2ToPitch]) / 100.0f);
            float semis = pitch + bendSemis + vibrato + s.pitchOffset;
            if (p[transpose] != 0.0f)
                semis += clampf (p[transpose], -48.0f, 48.0f);
            if (p[pitchEnvSemitones] != 0.0f)
            {
                // Build is 1 - (1 - ramp)^2: no slope at the strike, the steepest at the end
                const float env = p[pitchEnvCurve] >= 0.5f ? 1.0f - (1.0f - pitchRamp) * (1.0f - pitchRamp) : pitchEnv;
                semis += clampf (p[pitchEnvSemitones], -48.0f, 48.0f) * env;
            }
            if (modOn)
            {
                semis += modSum[(std::size_t) ModTarget::pitch] * 24.0f;
                stepA = (float) (440.0 * std::pow (2.0, (semis + modSum[(std::size_t) ModTarget::pitchA] * 24.0f - 69.0f) / 12.0) / s.sampleRate);
                stepB = (float) (440.0 * std::pow (2.0, (semis + modSum[(std::size_t) ModTarget::pitchB] * 24.0f - 69.0f) / 12.0) / s.sampleRate) * s.oscBRatio;
            }
            else
            {
                stepA = (float) (440.0 * std::pow (2.0, (semis - 69.0f) / 12.0) / s.sampleRate);
                stepB = stepA * s.oscBRatio;
            }
            width = clampf (p[pulseWidth] + lfo * clampf (p[lfoToPulseWidth], 0.0f, 0.45f), 0.05f, 0.95f);
            if (modOn)
                width = clampf (width + modSum[(std::size_t) ModTarget::pulseWidth] * 0.45f, 0.05f, 0.95f);
            if (p[lfo2ToPulseWidth] != 0.0f)
                width = clampf (width + lfo2 * clampf (p[lfo2ToPulseWidth], 0.0f, 0.45f), 0.05f, 0.95f);

            float octaves = clamp01 (p[keyTrack]) * (pitch - 60.0f) / 12.0f + clampf (p[filterEnvAmount], -1.0f, 1.0f) * filter.value * 6.0f
                            + lfo * clampf (p[lfoToCutoff], 0.0f, 4.0f) + clamp01 (p[velocityToCutoff]) * (velocity - 1.0f) * 3.0f;
            if (p[lfo2ToCutoff] != 0.0f)
                octaves += lfo2 * clampf (p[lfo2ToCutoff], 0.0f, 4.0f);
            if (modOn)
                octaves += modSum[(std::size_t) ModTarget::cutoff] * 6.0f;
            float cutoff = std::pow (2.0f, s.logCutoff + octaves);
            const float top = std::min (18000.0f, s.sampleRate * 0.45f);
            cutoff = clampf (cutoff, 20.0f, top);

            // the sampler's zone for this note, picked once at the strike
            const auto* set = s.samples;
            if (set == nullptr)
                zone = nullptr;
            else if (zonePending)
            {
                zone = set->find (note, velocity);
                zonePending = false;
                samplePos = 0.0;
            }
            sampleStep = zone == nullptr ? 0.0f : (float) (zone->rate / (double) s.sampleRate * std::pow (2.0, (semis - (float) zone->root) / 12.0));

            bankA = s.chunkBankA;
            bankB = s.chunkBankB;
            if (bankA != nullptr || bankB != nullptr)
            {
                const float move = lfo * clampf (p[lfoToPosition], -1.0f, 1.0f) + filter.value * clampf (p[envToPosition], -1.0f, 1.0f);
                const float moveA = modOn ? modSum[(std::size_t) ModTarget::positionA] : 0.0f;
                const float moveB = modOn ? modSum[(std::size_t) ModTarget::positionB] : 0.0f;
                if (bankA != nullptr)
                {
                    bankA->place (clamp01 (p[positionA] + move + moveA), frameA, fracA);
                    mipA = mipFor (stepA * s.sampleRate, s.sampleRate, bankA->mips);
                }
                if (bankB != nullptr)
                {
                    bankB->place (clamp01 (p[positionB] + move + moveB), frameB, fracB);
                    mipB = mipFor (stepB * s.sampleRate, s.sampleRate, bankB->mips);
                }
            }

            const float g = (float) std::tan (pi * cutoff / s.sampleRate);
            k2 = modOn ? 2.0f - 2.0f * clampf (s.smoothResonance + modSum[(std::size_t) ModTarget::resonance], 0.0f, 0.97f) : 2.0f - 2.0f * s.smoothResonance;
            a1 = 1.0f / (1.0f + g * (g + k2));
            a2 = g * a1;
            a3 = g * a2;
            b1 = 1.0f / (1.0f + g * (g + flatK));
            b2 = g * b1;
            b3 = g * b2;
        }

        void next (Engine& s, const Patch& p, float lfo, float lfo2, float bendSemis, float& outL, float& outR) noexcept
        {
            const float sustainLevel = clamp01 (p[sustain]);
            if (stealing)
            {
                amp.value *= s.stealCoef;
                if (amp.value < 0.0005f)
                {
                    amp.value = 0.0f;
                    if (pendingNote < 0)
                    {
                        stealing = false;
                        amp.stage = filter.stage = Stage::off;
                        note = -1;
                        return;
                    }
                    const bool keyUp = pendingKeyUp;
                    begin (pendingNote, pendingVelocity, startedAt, true, pendingGlide);
                    if (keyUp)
                    {
                        gate = false;
                        sustained = true;
                    }
                }
            }
            if (! stealing)
            {
                // the clap retrigger: each further hit cuts the level and attacks again
                if (armHits)
                {
                    armHits = false;
                    hitsLeft = s.clapHitCount - 1;
                    hitCountdown = s.clapSpacing;
                    releaseAfterHit = false;
                }
                if (hitsLeft > 0 && --hitCountdown <= 0)
                {
                    --hitsLeft;
                    hitCountdown = s.clapSpacing;
                    amp.value = 0.0f;
                    amp.stage = Stage::attack;
                    filter.stage = Stage::attack;
                    if (! gate && ! sustained)
                        releaseAfterHit = true;
                }
                amp.tick (s.ampAttackStep, s.ampDecayCoef, sustainLevel, s.ampReleaseCoef);
                filter.tick (s.filterAttackStep, s.filterDecayCoef, clamp01 (p[filterSustain]), s.filterReleaseCoef);
                if (s.modFreeEnv)
                    env3.tick (s.env3AttackStep, s.env3DecayCoef, clamp01 (p[env3Sustain]), s.env3ReleaseCoef);
                if (hitsLeft > 0 && amp.stage == Stage::off)
                {
                    amp.stage = Stage::release;
                    amp.value = 0.0f;
                }
                if (releaseAfterHit && amp.stage != Stage::attack)
                {
                    releaseAfterHit = false;
                    amp.stage = Stage::release;
                    filter.stage = Stage::release;
                }
            }
            if (amp.stage == Stage::off)
            {
                note = -1;
                gate = sustained = false;
                return;
            }

            pitch += ((float) note - pitch) * s.glideCoefficient;
            pitchEnv *= s.pitchEnvCoef;
            if (pitchEnv < 1e-4f)
                pitchEnv = 0.0f;
            pitchRamp -= s.pitchRampStep;
            if (pitchRamp < 0.0f)
                pitchRamp = 0.0f;
            if (--controlCountdown <= 0)
            {
                controlCountdown = controlRate;
                control (s, p, lfo, lfo2, bendSemis);
            }
            const float mix = modOn ? clamp01 (p[oscMix] + modMix) : clamp01 (p[oscMix]);
            const float fDrive = modOn ? clamp01 (p[filterDrive] + modDrive) : clamp01 (p[filterDrive]);
            float fm = modOn ? clamp01 (s.smoothFm + modFm) : s.smoothFm;
            if (fm > 0.0f)
            {
                const float follow = clamp01 (p[fmEnv]);
                fm *= fmCycles * (1.0f - follow + follow * filter.value);
            }
            const float cross = modOn ? clamp01 (p[crossMod] + modCross) : clamp01 (p[crossMod]);
            const float feedback = clamp01 (p[oscFeedback]);
            const bool extended = cross > 0.0f || feedback > 0.0f;
            const auto wA = (Wave) (int) p[waveA], wB = (Wave) (int) p[waveB];

            float l = 0.0f, r = 0.0f;
            for (int k = 0; k < s.unisonCount; ++k)
            {
                const float ratio = s.unisonRatio[(std::size_t) k];
                float a = stepA * ratio, b = stepB * ratio;
                if (a >= 0.5f)
                    a = 0.49f;
                if (b >= 0.5f)
                    b = 0.49f;
                float pa = phaseA[(std::size_t) k] + a;
                if (pa >= 1.0f)
                    pa -= 1.0f;
                phaseA[(std::size_t) k] = pa;
                float pb = phaseB[(std::size_t) k] + b;
                if (pb >= 1.0f)
                    pb -= 1.0f;
                phaseB[(std::size_t) k] = pb;

                float sample;
                if (extended)
                {
                    // feedback and A moving B before B moves A (FM)
                    float fa = pa + feedback * 0.25f * lastA[(std::size_t) k];
                    fa -= std::floor (fa);
                    float shapeA = osc (wA, bankA, frameA, fracA, mipA, fa, a);
                    float fb = pb + feedback * 0.25f * lastB[(std::size_t) k] + cross * fmCycles * shapeA;
                    fb -= std::floor (fb);
                    const float shapeB = osc (wB, bankB, frameB, fracB, mipB, fb, b);
                    if (fm > 0.0f)
                    {
                        float bent = fa + fm * shapeB;
                        bent -= std::floor (bent);
                        shapeA = osc (wA, bankA, frameA, fracA, mipA, bent, a);
                    }
                    lastA[(std::size_t) k] = shapeA;
                    lastB[(std::size_t) k] = shapeB;
                    sample = shapeA * (1.0f - mix) + shapeB * mix;
                }
                else if (fm > 0.0f)
                {
                    const float shapeB = osc (wB, bankB, frameB, fracB, mipB, pb, b);
                    float bent = pa + fm * shapeB;
                    bent -= std::floor (bent);
                    sample = osc (wA, bankA, frameA, fracA, mipA, bent, a) * (1.0f - mix) + shapeB * mix;
                }
                else
                    sample = osc (wA, bankA, frameA, fracA, mipA, pa, a) * (1.0f - mix) + osc (wB, bankB, frameB, fracB, mipB, pb, b) * mix;
                l += sample * s.unisonLeft[(std::size_t) k];
                r += sample * s.unisonRight[(std::size_t) k];
            }
            const float oscGain = s.unisonGain * clamp01 (p[oscLevel]) * (s.samples != nullptr ? 1.0f - clamp01 (p[sampleLevel]) : 1.0f);
            l *= oscGain;
            r *= oscGain;
            const float oscSat = clamp01 (p[oscSaturation]);
            if (oscSat > 0.0f)
            {
                l = driveSample (l, oscSat);
                r = driveSample (r, oscSat);
            }

            // sub and noise in the middle: spreading the bass makes a mix fall apart
            float subStep = stepA * 0.5f;
            if (subStep >= 0.5f)
                subStep = 0.49f;
            phaseSub += subStep;
            if (phaseSub >= 1.0f)
                phaseSub -= 1.0f;
            float centre = 0.0f;
            const float sub = modOn ? clamp01 (p[subLevel] + modSub) : p[subLevel];
            const float noise = modOn ? clamp01 (p[noiseLevel] + modNoise) : p[noiseLevel];
            if (sub > 0.0f)
                centre += shape (Wave::square, phaseSub, subStep, 0.5f) * clamp01 (sub);
            if (noise > 0.0f)
            {
                float white = s.nextRandom01() * 2.0f - 1.0f;
                if (s.noiseMode != 0)
                {
                    noiseState += s.noiseCoef * (white - noiseState);
                    white = s.noiseMode < 0 ? noiseState : white - noiseState;
                }
                centre += white * clamp01 (noise);
            }
            if (p[sampleLevel] > 0.0f && zone != nullptr)
                centre += readSample (*zone) * clamp01 (p[sampleLevel]);
            l += centre;
            r += centre;

            // driven INTO the filter, so a resonant sweep has harmonics to find
            if (fDrive > 0.0f)
            {
                l = driveSample (l, fDrive);
                r = driveSample (r, fDrive);
            }
            const auto mode = (FilterMode) (int) p[filterMode];
            if (mode == FilterMode::lowPass24)
            {
                l = svf (svf (l, ic1L, ic2L, a1, a2, a3, k2, FilterMode::lowPass), ic3L, ic4L, b1, b2, b3, flatK, FilterMode::lowPass);
                r = svf (svf (r, ic1R, ic2R, a1, a2, a3, k2, FilterMode::lowPass), ic3R, ic4R, b1, b2, b3, flatK, FilterMode::lowPass);
            }
            else if (mode == FilterMode::blend || mode == FilterMode::lowShelf || mode == FilterMode::highShelf)
            {
                const float blend = clamp01 (p[filterBlend] + modBlend);
                const float shelf = std::pow (10.0f, clampf (p[shelfGainDb], -24.0f, 24.0f) / 20.0f);
                l = bands (l, ic1L, ic2L, a1, a2, a3, k2, mode, blend, shelf);
                r = bands (r, ic1R, ic2R, a1, a2, a3, k2, mode, blend, shelf);
            }
            else
            {
                l = svf (l, ic1L, ic2L, a1, a2, a3, k2, mode);
                r = svf (r, ic1R, ic2R, a1, a2, a3, k2, mode);
            }
            const float filterSat = clamp01 (p[filterSaturation]);
            if (filterSat > 0.0f)
            {
                const float push = filterSat * 2.0f;
                ic1L /= 1.0f + push * std::abs (ic1L);
                ic1R /= 1.0f + push * std::abs (ic1R);
            }

            const float velocityGain = 1.0f - clamp01 (p[velocityToAmp]) * (1.0f - velocity);
            float tremolo = 1.0f - clamp01 (p[lfoToAmp]) * (0.5f - 0.5f * lfo);
            if (p[lfo2ToAmp] != 0.0f)
                tremolo *= 1.0f - clamp01 (p[lfo2ToAmp]) * (0.5f - 0.5f * lfo2);
            float gain = amp.value * velocityGain * tremolo * 0.25f;
            if (modOn)
            {
                gain *= clampf (1.0f + modAmp, 0.0f, 2.0f);
                if (modPan != 0.0f)
                {
                    const float pan = clampf (modPan, -1.0f, 1.0f);
                    l *= 1.0f - std::max (0.0f, pan);
                    r *= 1.0f + std::min (0.0f, pan);
                }
            }
            // a voice that went bad (a NaN in its filter) is cleared and goes silent rather than poisoning the mix
            if (! finite (l) || ! finite (r))
            {
                clearFilter();
                lastA.fill (0.0f);
                lastB.fill (0.0f);
                noiseState = 0.0f;
                return;
            }
            outL += l * gain;
            outR += r * gain;
        }
    };

    float glideFrom (const Voice& v, bool sounding) const noexcept
    {
        if (patch[glide] <= 0.0005f || lastPitch < 0.0f)
            return -1.0f;
        return sounding ? v.pitch : lastPitch;
    }

    void polyOn (int note, float velocity) noexcept
    {
        monoCount = 0;
        for (auto& v : voices)
            if (v.stealing && v.pendingNote == note)
            {
                v.pendingVelocity = velocity;
                v.pendingKeyUp = false;
                return;
            }
        Voice* chosen = nullptr;
        for (auto& v : voices)
            if (v.note == note && v.amp.stage != Stage::off && ! v.stealing)
            {
                chosen = &v;
                break;
            }
        if (chosen == nullptr)
            for (auto& v : voices)
                if (v.amp.stage == Stage::off)
                {
                    chosen = &v;
                    break;
                }
        if (chosen == nullptr)
        {
            for (auto& v : voices)
                if (v.amp.stage == Stage::release && (chosen == nullptr || v.amp.value < chosen->amp.value))
                    chosen = &v;
            if (chosen == nullptr)
            {
                chosen = &voices[0];
                for (auto& v : voices)
                    if (v.startedAt < chosen->startedAt)
                        chosen = &v;
            }
        }
        const float from = glideFrom (*chosen, false);
        if (chosen->amp.stage != Stage::off && chosen->note != note && chosen->amp.value > 0.001f)
            chosen->stealFor (note, velocity, ++age, from);
        else
            chosen->begin (note, velocity, ++age, true, from);
        lastPitch = (float) note;
    }

    void removeFromStack (int note) noexcept
    {
        for (int i = monoCount - 1; i >= 0; --i)
            if (monoStack[(std::size_t) i] == note)
                removeAtStack (i);
    }

    void removeAtStack (int index) noexcept
    {
        for (int i = index; i < monoCount - 1; ++i)
        {
            monoStack[(std::size_t) i] = monoStack[(std::size_t) i + 1];
            monoVelocity[(std::size_t) i] = monoVelocity[(std::size_t) i + 1];
        }
        --monoCount;
    }

    float coefficient (float seconds) const noexcept { return (float) std::exp (-4.6 / (std::max (0.001, (double) seconds) * sampleRate)); }

    void prepareChunk (const Patch& p) noexcept
    {
        const int n = std::clamp ((int) p[unisonVoices], 1, maxUnison);
        unisonCount = n;
        unisonGain = 1.0f / std::sqrt ((float) n);
        const float spread = clamp01 (p[stereoSpread]);
        for (int k = 0; k < n; ++k)
        {
            const float position = n == 1 ? 0.0f : (float) k / (float) (n - 1) * 2.0f - 1.0f;
            unisonRatio[(std::size_t) k] = (float) std::pow (2.0, position * clampf (p[unisonDetune], 0.0f, 100.0f) / 1200.0);
            // constant power, a centred copy at full level in both
            const double angle = (position * spread + 1.0) * pi / 4.0;
            unisonLeft[(std::size_t) k] = (float) (std::cos (angle) * std::sqrt (2.0));
            unisonRight[(std::size_t) k] = (float) (std::sin (angle) * std::sqrt (2.0));
        }
        ampAttackStep = 1.0f / (std::max (0.0005f, p[attack]) * sampleRate);
        ampDecayCoef = coefficient (p[decay]);
        ampReleaseCoef = coefficient (p[release]);
        filterAttackStep = 1.0f / (std::max (0.0005f, p[filterAttack]) * sampleRate);
        filterDecayCoef = coefficient (p[filterDecay]);
        filterReleaseCoef = coefficient (p[filterRelease]);
        glideCoefficient = p[glide] <= 0.0005f ? 1.0f : 1.0f - coefficient (p[glide]);
        const float pitchSeconds = clampf (p[pitchEnvDecay], 0.005f, 16.0f);
        pitchEnvCoef = coefficient (pitchSeconds);
        pitchRampStep = 1.0f / (pitchSeconds * sampleRate);
        const float colour = clampf (p[noiseColour], -1.0f, 1.0f);
        if (colour == 0.0f)
            noiseMode = 0;
        else
        {
            // below 0 a one-pole low-pass closes from 15 kHz to 230 Hz; above, a high-pass opens from 100 Hz to 9 kHz
            noiseMode = colour < 0.0f ? -1 : 1;
            const double fc = colour < 0.0f ? 15000.0 * std::pow (2.0, colour * 6.0) : 100.0 * std::pow (2.0, colour * 6.5);
            noiseCoef = 1.0f - (float) std::exp (-2.0 * pi * fc / sampleRate);
        }
        clapHitCount = std::clamp ((int) p[clapHits], 1, 8);
        clapSpacing = std::max (1, (int) (clampf (p[clapSpacingMs], 1.0f, 100.0f) * 0.001f * sampleRate));
        oscBRatio = (float) std::pow (2.0, std::clamp ((int) p[octaveB], -2, 2) + clampf (p[detuneCents], -100.0f, 100.0f) / 1200.0);
        if (p[semitonesB] != 0.0f)
            oscBRatio *= (float) std::pow (2.0, std::clamp ((int) p[semitonesB], -48, 48) / 12.0);

        modCount = 0;
        modFreeEnv = modPolyLfo = modSeq = modLfo2 = false;
        for (int i = 0; i < modSlots; ++i)
        {
            const int src = (int) p[(std::size_t) modSource (i)], tgt = (int) p[(std::size_t) modTarget (i)], via = (int) p[(std::size_t) modVia (i)];
            const float amount = clampf (p[(std::size_t) modAmount (i)], -1.0f, 1.0f);
            if (src <= 0 || tgt <= 0 || tgt >= 18 || amount == 0.0f)
                continue;
            modSourceOf[(std::size_t) modCount] = src;
            modTargetOf[(std::size_t) modCount] = tgt;
            modViaOf[(std::size_t) modCount] = via;
            modAmountOf[(std::size_t) modCount] = amount;
            for (int used : { src, via })
            {
                modFreeEnv = modFreeEnv || used == (int) ModSource::freeEnv;
                modPolyLfo = modPolyLfo || used == (int) ModSource::polyLfo;
                modSeq = modSeq || used == (int) ModSource::stepSeq;
                modLfo2 = modLfo2 || used == (int) ModSource::lfo2;
            }
            ++modCount;
        }
        if (modFreeEnv)
        {
            env3AttackStep = 1.0f / (std::max (0.0005f, p[env3Attack]) * sampleRate);
            env3DecayCoef = coefficient (p[env3Decay]);
            env3ReleaseCoef = coefficient (p[env3Release]);
        }
        if (modPolyLfo)
        {
            polyLfoStep = (float) (std::clamp ((double) p[polyLfoRateHz], 0.01, 40.0) / sampleRate);
            polyLfoDivisionBeats = divisionBeats ((Division) (int) p[polyLfoDivision]);
        }
    }

    float seqAt (const Patch& p, double when) const noexcept
    {
        const int length = std::clamp ((int) p[seqLength], 1, 16);
        const double x = when / divisionBeats ((Division) (int) p[seqDivision]);
        const double fl = std::floor (x);
        const int at = (int) ((((std::int64_t) fl) % length + length) % length);
        const float value = clamp01 (p[(std::size_t) (seqStep0 + at)]);
        if (p[seqSmooth] < 0.5f)
            return value;
        const float nextValue = clamp01 (p[(std::size_t) (seqStep0 + (at + 1) % length)]);
        return value + (nextValue - value) * (float) (x - fl);
    }

    void renderChunk (int frames) noexcept
    {
        const auto& p = patch;
        prepareChunk (p);
        const double beatStep = bpm / 60.0 / sampleRate;
        chunkBeat = beat;
        std::fill (mixL.begin(), mixL.begin() + frames, 0.0f);
        std::fill (mixR.begin(), mixR.begin() + frames, 0.0f);

        const double lfoBeats = divisionBeats ((Division) (int) p[lfoDivision]);
        const float lfoStep = (float) (std::clamp ((double) p[lfoRateHz], 0.01, 40.0) / sampleRate);
        const bool lfo2On = p[lfo2ToPitch] != 0.0f || p[lfo2ToCutoff] != 0.0f || p[lfo2ToAmp] != 0.0f || p[lfo2ToPulseWidth] != 0.0f || modLfo2;
        const double lfo2Beats = divisionBeats ((Division) (int) p[lfo2Division]);
        const float lfo2Step = (float) (std::clamp ((double) p[lfo2RateHz], 0.01, 40.0) / sampleRate);
        if (! lfo2On)
            lfo2Value = 0.0f;
        const float masterTarget = clampf (p[masterVolume], 0.0f, 1.5f);
        const float cutoffTarget = (float) std::log2 (std::clamp ((double) p[cutoffHz], 30.0, 20000.0));
        const float resonanceTarget = clampf (p[resonance], 0.0f, 0.97f);
        const float fmTarget = clamp01 (p[fmDepth]);
        if (std::isnan (logCutoff))
        {
            logCutoff = cutoffTarget;
            smoothResonance = resonanceTarget;
            smoothMaster = masterTarget;
            smoothFm = fmTarget;
        }
        chunkBankA = (Wave) (int) p[waveA] == Wave::wavetable ? tableAPtr : nullptr;
        chunkBankB = (Wave) (int) p[waveB] == Wave::wavetable ? tableBPtr : nullptr;
        const float bendSemis = 0.0f; // the wheel arrives as the channel's pitch (setPitchOffset)

        for (int i = 0; i < frames; ++i)
        {
            logCutoff += (cutoffTarget - logCutoff) * smoothCoef;
            smoothResonance += (resonanceTarget - smoothResonance) * smoothCoef;
            smoothMaster += (masterTarget - smoothMaster) * smoothCoef;
            smoothFm += (fmTarget - smoothFm) * smoothCoef;
            const double b = beat + i * beatStep;

            // the LFO, shared by every voice so a wobble moves the whole chord together
            const float previous = lfoPhase;
            if (p[lfoSync] >= 0.5f)
            {
                const double x = b / lfoBeats;
                lfoPhase = (float) (x - std::floor (x));
            }
            else
            {
                lfoPhase += lfoStep;
                if (lfoPhase >= 1.0f)
                    lfoPhase -= 1.0f;
            }
            lfoValue = lfoShape ((LfoWave) (int) p[lfoWave], lfoPhase, lfoPhase < previous, lfoHeld, rng);
            if (lfo2On)
            {
                const float previous2 = lfo2Phase;
                if (p[lfo2Sync] >= 0.5f)
                {
                    const double x = b / lfo2Beats;
                    lfo2Phase = (float) (x - std::floor (x));
                }
                else
                {
                    lfo2Phase += lfo2Step;
                    if (lfo2Phase >= 1.0f)
                        lfo2Phase -= 1.0f;
                }
                lfo2Value = lfoShape ((LfoWave) (int) p[lfo2Wave], lfo2Phase, lfo2Phase < previous2, lfo2Held, rng);
            }
            if (modSeq)
                seqValue = seqAt (p, b);

            float l = 0.0f, r = 0.0f;
            for (auto& v : voices)
                if (v.amp.stage != Stage::off)
                    v.next (*this, p, lfoValue, lfo2Value, bendSemis, l, r);
            mixL[(std::size_t) i] = l * smoothMaster;
            mixR[(std::size_t) i] = r * smoothMaster;
        }
        beat += frames * beatStep;
        effects.process (mixL.data(), mixR.data(), frames, p, 60.0 / bpm);
    }

    std::uint32_t nextRandom() noexcept
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    }
    std::uint32_t nextExtra() noexcept
    {
        extraRng ^= extraRng << 13;
        extraRng ^= extraRng >> 17;
        extraRng ^= extraRng << 5;
        return extraRng;
    }
    float nextRandom01() noexcept { return (float) (nextRandom() >> 8) / 16777216.0f; }

    float sampleRate;
    Effects effects;
    float smoothCoef, stealCoef;
    Patch patch {};
    std::array<Voice, maxVoices> voices {};
    std::array<float, chunk> mixL {}, mixR {};
    std::int64_t age = 0;
    std::uint32_t rng = 0x9E3779B9u, extraRng = 0x85EBCA6Bu;
    const Wavetable* tableAPtr = nullptr;
    const Wavetable* tableBPtr = nullptr;
    const Wavetable* chunkBankA = nullptr;
    const Wavetable* chunkBankB = nullptr;
    const SampleSet* samples = nullptr;
    double bpm = 120.0, beat = 0.0, chunkBeat = 0.0, polyLfoDivisionBeats = 1.0;
    float pitchOffset = 0, modWheel = 0, aftertouch = 0, lastPitch = -1;
    bool sustainDown = false;
    std::array<int, maxHeld> monoStack {};
    std::array<float, maxHeld> monoVelocity {};
    int monoCount = 0;
    float logCutoff = NAN, smoothResonance = 0, smoothMaster = 0, smoothFm = 0;
    std::array<float, maxUnison> unisonRatio {}, unisonLeft {}, unisonRight {};
    int unisonCount = 1;
    float unisonGain = 1, ampAttackStep = 0, ampDecayCoef = 0, ampReleaseCoef = 0, filterAttackStep = 0, filterDecayCoef = 0, filterReleaseCoef = 0;
    float glideCoefficient = 1, oscBRatio = 1, pitchEnvCoef = 0, pitchRampStep = 0, noiseCoef = 0;
    int noiseMode = 0, clapHitCount = 1, clapSpacing = 1;
    float lfoPhase = 0, lfoValue = 0, lfoHeld = 0, lfo2Phase = 0, lfo2Value = 0, lfo2Held = 0;
    std::array<int, modSlots> modSourceOf {}, modTargetOf {}, modViaOf {};
    std::array<float, modSlots> modAmountOf {};
    int modCount = 0;
    bool modFreeEnv = false, modPolyLfo = false, modSeq = false, modLfo2 = false;
    float env3AttackStep = 0, env3DecayCoef = 0, env3ReleaseCoef = 0, polyLfoStep = 0, seqValue = 0;
};

std::unique_ptr<Engine> makeEngine (double sampleRate) { return std::make_unique<Engine> (sampleRate); }
void destroy (Engine* e) noexcept { delete e; }

Offline::Offline (double sampleRate) : engine (new Engine (sampleRate)) {}
Offline::~Offline() = default;
void Offline::setPatch (const Patch& p) noexcept { engine->setPatch (p); }
void Offline::setTables (const Wavetable* a, const Wavetable* b) noexcept { engine->setTables (a, b); }
void Offline::setSamples (std::shared_ptr<const SampleSet> set) noexcept
{
    samples = std::move (set);
    engine->setSamples (samples.get());
}
void Offline::setBpm (double bpm) noexcept { engine->setBpm (bpm); }
void Offline::noteOn (int key, float velocity) noexcept { engine->noteOn (key, velocity); }
void Offline::noteOff (int key) noexcept { engine->noteOff (key); }
void Offline::render (float* left, float* right, int n) noexcept { engine->render (left, right, n, 1.0f, 1.0f); }
int Offline::activeVoices() const noexcept { return engine->activeVoices(); }

// --- sample set loading (UI thread) -----------------------------------------------------------------------
namespace
{
std::vector<std::uint8_t> readAll (const std::filesystem::path& path)
{
    std::ifstream in (path, std::ios::binary);
    return { std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>() };
}

bool zoneFromWav (const std::filesystem::path& path, SampleZone& zone)
{
    const auto bytes = readAll (path);
    audioedit::Document doc;
    if (bytes.empty() || ! audioedit::decodeWav (bytes.data(), bytes.size(), doc) || doc.audio.frames() < 2)
        return false;
    const auto frames = doc.audio.frames();
    const auto channels = std::max (1, doc.audio.numChannels());
    zone.data.assign ((std::size_t) frames, 0.0f);
    for (int c = 0; c < channels; ++c)
        for (std::int64_t i = 0; i < frames; ++i)
            zone.data[(std::size_t) i] += doc.audio.channels[(std::size_t) c][(std::size_t) i] / (float) channels;
    zone.rate = (int) std::lround (doc.audio.sampleRate);
    if (doc.loopStart >= 0 && doc.loopEnd > doc.loopStart + 8 && doc.loopEnd <= frames)
    {
        zone.loopStart = (int) doc.loopStart;
        zone.loopEnd = (int) doc.loopEnd;
        zone.loop = true;
    }
    return true;
}
} // namespace

namespace
{
// a .wti (FL Keys' multisample description; the user's own files): flkeys::parseWti reads it, a zone per keygroup,
// a keygroup with the same range as the one before is its next velocity layer. WAV-based sets only (not raw .dat).
std::shared_ptr<const SampleSet> loadWti (const std::filesystem::path& path)
{
    const auto bytes = readAll (path);
    std::vector<std::string> files;
    const auto table = flkeys::parseWti (std::string (bytes.begin(), bytes.end()), files);
    if (! table || table->raw)
        return nullptr;
    auto set = std::make_shared<SampleSet>();
    set->name = path.stem().string();
    int file = -1, lastRoot = -1, lastHigh = -1, layer = 0, low = 0;
    for (const auto& kg : table->keygroups)
    {
        if (kg.file >= 0)
            file = kg.file;
        else if (kg.duplicateOf >= 0 && kg.duplicateOf < (int) table->keygroups.size())
            file = table->keygroups[(std::size_t) kg.duplicateOf].file; // the same file as an earlier keygroup
        if (file < 0 || file >= (int) files.size())
            continue;
        const bool sameRange = kg.root == lastRoot && kg.high == lastHigh;
        layer = sameRange ? layer + 1 : 0;
        if (! sameRange && lastHigh >= 0)
            low = std::min (127, lastHigh + 1);
        SampleZone z;
        if (! zoneFromWav (path.parent_path() / files[(std::size_t) file], z))
            continue;
        z.root = std::clamp (kg.root, 0, 127);
        z.low = std::clamp (std::min (low, z.root), 0, 127);
        z.high = std::clamp (kg.high, z.root, 127);
        z.velocityFrom = layer < (int) table->thresholds.size() ? table->thresholds[(std::size_t) layer] : 127;
        if (! kg.canLoop)
            z.loop = false;
        set->zones.push_back (std::move (z));
        lastRoot = kg.root;
        lastHigh = kg.high;
    }
    return set->zones.empty() ? nullptr : set;
}
} // namespace

std::shared_ptr<const SampleSet> loadSampleFolder (const std::string& folder)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::is_regular_file (folder, ec))
    {
        auto ext = fs::path (folder).extension().string();
        std::transform (ext.begin(), ext.end(), ext.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
        return ext == ".wti" ? loadWti (folder) : nullptr;
    }
    if (! fs::is_directory (folder, ec))
        return nullptr;
    auto set = std::make_shared<SampleSet>();
    set->name = fs::path (folder).filename().string();
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator (folder, ec))
    {
        auto ext = e.path().extension().string();
        std::transform (ext.begin(), ext.end(), ext.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
        if (e.is_regular_file() && ext == ".wav")
            files.push_back (e.path());
    }
    std::sort (files.begin(), files.end());
    for (const auto& f : files)
    {
        const int root = rootFromName (f.stem().string());
        if (root < 0)
            continue;
        SampleZone z;
        if (! zoneFromWav (f, z))
            continue;
        z.root = root;
        set->zones.push_back (std::move (z));
    }
    if (set->zones.empty())
        return nullptr;
    // split the keyboard half way between neighbouring roots
    std::sort (set->zones.begin(), set->zones.end(), [] (const SampleZone& a, const SampleZone& b) { return a.root < b.root; });
    for (std::size_t i = 0; i < set->zones.size(); ++i)
    {
        set->zones[i].low = i == 0 ? 0 : (set->zones[i - 1].root + set->zones[i].root) / 2 + 1;
        set->zones[i].high = i + 1 == set->zones.size() ? 127 : (set->zones[i].root + set->zones[i + 1].root) / 2;
    }
    return set;
}

// --- the instrument --------------------------------------------------------------------------------------
namespace
{
class GpInstrument final : public Instrument
{
public:
    GpInstrument() { buffers[0] = buffers[1] = defaultPatch(); }

    void prepare (double sr) noexcept override
    {
        engine.reset (new Engine (sr));
        engine->setPatch (buffers[(std::size_t) published.load()]);
        engine->setTables (tableAPtr.load(), tableBPtr.load());
        engine->setSamples (samplesPtr.load());
    }

    void configure (const ChannelDef& c, double bpmNow) noexcept override
    {
        bpm.store (bpmNow);
        knobCents.store (c.sampler.pitch * 100.0f);
        // the patch: written to the buffer the audio thread is not reading, then published
        if (c.modelParams != lastParams)
        {
            lastParams = c.modelParams;
            const auto p = patchOf (c.modelParams);
            const int spare = 1 - published.load();
            buffers[(std::size_t) spare] = p;
            published.store (spare);
            changed.store (true);
            // the wavetables (built on first use, here, never on the audio thread)
            tableAPtr.store ((Wave) (int) p[waveA] == Wave::wavetable ? table ((int) p[tableA]) : nullptr);
            tableBPtr.store ((Wave) (int) p[waveB] == Wave::wavetable ? table ((int) p[tableB]) : nullptr);
        }
        if (c.samplePath != lastSamplePath)
        {
            lastSamplePath = c.samplePath;
            auto set = loadSampleFolder (c.samplePath);
            // the old set stays alive while the audio thread may still read it: kept until the next change
            retiredSamples = std::move (currentSamples);
            currentSamples = std::move (set);
            samplesPtr.store (currentSamples.get());
        }
    }

    void setNextNoteExpression (float, float cents, bool) noexcept override { nextCents = cents; }
    void setChannelPitchNow (float knob, float rest) noexcept override { livePitch.store (knob + rest); }

    void noteOn (int key, float velocity) noexcept override
    {
        sync();
        if (engine == nullptr)
            return;
        (void) nextCents; // (per-note fine tune: the engine's pitch is per channel; the knob moves every voice)
        engine->noteOn (key, velocity);
        nextCents = 0.0f;
    }
    void noteOff (int key) noexcept override
    {
        if (engine != nullptr)
            engine->noteOff (key);
    }
    void allNotesOff() noexcept override
    {
        if (engine != nullptr)
            engine->allNotesOff();
    }
    void panic() noexcept override
    {
        if (engine != nullptr)
            engine->kill();
    }
    void controlChange (int cc, float value) noexcept override
    {
        if (engine == nullptr)
            return;
        if (cc == 1)
            engine->setModWheel (value);
        else if (cc == 64)
            engine->setSustain (value >= 0.5f);
    }
    // automation: one patch parameter, 0..1 over its range
    void setParameter (int index, float value) noexcept override
    {
        if (! finite (value))
            return;
        if (const auto* m = macroOfParam (index))
        {
            // a dial: its parameters move together (the dial sets are a static table: nothing allocates)
            setMacro (buffers[(std::size_t) published.load()], *m, value);
            changed.store (true);
            return;
        }
        if (index < 0 || index >= paramCount)
            return;
        const auto& info = patchParams()[(std::size_t) index].param;
        auto& live = buffers[(std::size_t) published.load()];
        live[(std::size_t) index] = info.raw (value);
        changed.store (true);
    }
    bool idle() const noexcept override { return engine == nullptr || engine->activeVoices() == 0; }

    void render (float* left, float* right, int n, float gainL, float gainR) noexcept override
    {
        sync();
        if (engine != nullptr)
            engine->render (left, right, n, gainL, gainR);
    }

private:
    void sync() noexcept
    {
        if (engine == nullptr)
            return;
        if (changed.exchange (false))
            engine->setPatch (buffers[(std::size_t) published.load()]);
        engine->setTables (tableAPtr.load(), tableBPtr.load());
        engine->setSamples (samplesPtr.load());
        engine->setBpm (bpm.load());
        engine->setPitchOffset (livePitch.load() + wheelCents());
    }

    std::unique_ptr<Engine> engine;
    std::array<Patch, 2> buffers;
    std::atomic<int> published { 0 };
    std::atomic<bool> changed { true };
    std::atomic<const Wavetable*> tableAPtr { nullptr }, tableBPtr { nullptr };
    std::atomic<const SampleSet*> samplesPtr { nullptr };
    std::shared_ptr<const SampleSet> currentSamples, retiredSamples;
    std::atomic<double> bpm { 120.0 };
    std::atomic<float> knobCents { 0.0f }, livePitch { 0.0f };
    std::vector<float> lastParams { -1.0f }; // (never a real patch: the first configure publishes)
    std::string lastSamplePath = "\x01";
    float nextCents = 0.0f;
};
} // namespace

std::unique_ptr<Instrument> makeInstrument() { return std::make_unique<GpInstrument>(); }
} // namespace lynx::gpsynth

// --- automatable parameters (core/ModelParams.h) ---------------------------------------------------------
namespace lynx::modelparams
{
namespace
{
std::optional<float> gpValue (const ChannelDef& c, const EngineParam& p)
{
    if (const auto* m = gpsynth::macroOfParam (p.index))
        return gpsynth::macroPosition (gpsynth::patchOf (c.modelParams), *m); // where the patch's own parameters put it
    const auto i = (std::size_t) p.index;
    if (i < c.modelParams.size() && std::isfinite (c.modelParams[i]))
        return c.modelParams[i];
    return gpsynth::patchParams()[i].defaultValue;
}

bool setGpValue (ChannelDef& c, const EngineParam& p, float raw)
{
    if (const auto* m = gpsynth::macroOfParam (p.index))
    {
        auto patch = gpsynth::patchOf (c.modelParams);
        gpsynth::setMacro (patch, *m, raw);
        c.modelParams = gpsynth::toModelParams (patch);
        return true;
    }
    const auto i = (std::size_t) p.index;
    if (i >= (std::size_t) gpsynth::paramCount)
        return false;
    if (c.modelParams.size() < (std::size_t) gpsynth::paramCount)
    {
        // an empty (new) patch becomes the defaults, then the one value
        const auto defaults = gpsynth::toModelParams (gpsynth::patchOf (c.modelParams));
        c.modelParams = defaults;
    }
    c.modelParams[i] = raw;
    return true;
}
} // namespace

Table gravepainModelTable() { return { &gpsynth::engineParams(), &gpValue, &setGpValue }; }
} // namespace lynx::modelparams
