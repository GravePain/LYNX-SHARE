#pragma once

// GravePain Synth: LYNX's own subtractive / wavetable synthesiser (GravePain port, step 5; our own code, from
// GravePain's SynthEngine, SynthPatch and SynthEffects). Per note: two oscillators (12 waves incl. band-limited
// wavetables) with up to 15 unison copies, sub and coloured noise, FM / cross mod / feedback, a multimode filter
// (12 / 24 dB low-pass, band, high, notch, peak, blend, shelves) with its envelope, an amplitude envelope, a pitch
// envelope (fall or build), clap retrigger; two shared LFOs, a poly LFO, a free envelope, a step sequencer and an
// 8-slot modulation matrix; then crush, drive, formant, stutter, chorus, delay and reverb.
//
// The patch is a fixed table of numbered parameters (patchParams()) kept in ChannelDef::modelParams, so the channel
// editor's knobs, automation clips and LYNX's preset banks all reach every one of them. The audio thread allocates
// nothing and takes no lock; a voice whose output goes NaN / infinite is cleared and the output stays finite.

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/EngineParams.h"

namespace lynx
{
struct ChannelDef;
class Instrument;
} // namespace lynx

namespace lynx::gpsynth
{
// --- the patch -----------------------------------------------------------------------------------
enum Param : int
{
    waveA, waveB, octaveB, detuneCents, oscMix, pulseWidth, subLevel, noiseLevel, oscLevel, semitonesB, transpose,
    unisonVoices, unisonDetune, stereoSpread,
    filterMode, cutoffHz, resonance, keyTrack, filterEnvAmount, filterAttack, filterDecay, filterSustain, filterRelease,
    filterDrive, filterBlend, shelfGainDb, filterSaturation,
    attack, decay, sustain, release, velocityToAmp, velocityToCutoff, masterVolume,
    pitchEnvSemitones, pitchEnvDecay, pitchEnvCurve,
    lfoWave, lfoRateHz, lfoSync, lfoDivision, lfoToPitch, lfoToCutoff, lfoToAmp, lfoToPulseWidth,
    lfo2Wave, lfo2RateHz, lfo2Sync, lfo2Division, lfo2ToPitch, lfo2ToCutoff, lfo2ToAmp, lfo2ToPulseWidth,
    voiceMode, glide, bendRange,
    fmDepth, fmEnv, oscSaturation, oscFeedback, crossMod,
    noiseColour, clapHits, clapSpacingMs,
    drive, crush, chorusMix, chorusRateHz, chorusDepth, delayMix, delayFeedback, delaySync, delayDivision, delayTimeMs,
    delayPingPong, reverbMix, reverbSize, reverbDamp, formantMix, formantVowel, stutterMix, stutterDivision,
    tableA, tableB, positionA, positionB, lfoToPosition, envToPosition,
    sampleLevel, sampleRoot,
    mod0Source, // 8 slots of 4: source, target, amount, via
    env3Attack = mod0Source + 32, env3Decay, env3Sustain, env3Release,
    polyLfoWave, polyLfoRateHz, polyLfoSync, polyLfoDivision,
    seqStep0, // 16 steps
    seqLength = seqStep0 + 16, seqDivision, seqSmooth,
    soundType, // which dial set (soundTypes()) the editor's first page shows
    paramCount
};

inline constexpr int modSlots = 8;
inline constexpr int modSource (int slot) noexcept { return mod0Source + slot * 4; }
inline constexpr int modTarget (int slot) noexcept { return mod0Source + slot * 4 + 1; }
inline constexpr int modAmount (int slot) noexcept { return mod0Source + slot * 4 + 2; }
inline constexpr int modVia (int slot) noexcept { return mod0Source + slot * 4 + 3; }

enum class Wave { sine, triangle, saw, square, wavetable, sawDown, threeStep, fourStep, eightStep, threePyramid, fivePyramid, ninePyramid };
enum class FilterMode { lowPass, bandPass, highPass, notch, peak, lowPass24, blend, lowShelf, highShelf };
enum class LfoWave { sine, triangle, saw, square, random };
enum class VoiceMode { poly, mono, legato };
enum class Division { whole, half, quarter, dottedEighth, eighth, eighthTriplet, sixteenth, sixteenthTriplet, thirtySecond };
enum class ModSource { none, ampEnv, filterEnv, freeEnv, lfo1, lfo2, polyLfo, velocity, note, modWheel, aftertouch, random, stepSeq };
enum class ModTarget { none, pitch, pitchA, pitchB, oscMix, pulseWidth, positionA, positionB, subLevel, noiseLevel, cutoff, resonance,
                       filterBlend, filterDrive, fmDepth, crossMod, amp, pan };

double divisionBeats (Division d) noexcept;

struct ParamInfo
{
    EngineParam param;               // index, English name, range, stepped
    float defaultValue = 0.0f;
    std::vector<const char*> choices; // the menu items of a stepped choice (empty: a number)
};
const std::vector<ParamInfo>& patchParams();       // sorted by index, paramCount entries
const std::vector<EngineParam>& engineParams();    // the same as EngineParams (core/ModelParams tables)

using Patch = std::array<float, paramCount>;
Patch defaultPatch();
// The patch of a channel: its modelParams where they hold one (missing values default), each value clamped.
Patch patchOf (const std::vector<float>& modelParams);
std::vector<float> toModelParams (const Patch& p);

// --- sounds: types, dials (macros) and the factory bank (core/GpSynthBank.cpp) -------------------------------
// The sound types (GravePain's Type words): Lead, Keys, Mallets, Riser, Impact, Bass, Sub Bass, Pad, Pluck, Arp, Stab,
// Brass, Bell, Kick, Snare, Clap, Hihat, Cymbal, Tom, Percussion, Open Hat.
const std::vector<std::string>& soundTypes();
int soundTypeIndex (const std::string& type); // 0 (Lead) for an unknown one

// A dial: one simple control moving several patch parameters together, each from `from` to `to` (on a curve for
// times and frequencies). It has no value of its own: where it sits is read back from its first parameter.
struct MacroPart
{
    Param field;
    float from, to;
    bool log;
};
struct Macro
{
    const char* name;
    std::vector<MacroPart> parts;
};
struct MacroSet
{
    const char* type;
    std::vector<Macro> macros;
};
const std::vector<MacroSet>& macroSets();
const MacroSet& macrosFor (int soundType); // Open Hat shares Hihat's
void setMacro (Patch& p, const Macro& m, float position) noexcept;
float macroPosition (const Patch& p, const Macro& m) noexcept;
// The dials as automatable parameters, named "<type> <dial>" ("Bass Squelch"): index macroParam0 + type * macroCount
// + i is dial i of sound type `type` (10 at most a type). The editor shows the current type's.
inline constexpr int macroParam0 = 1000, macroCount = 10;
// The dial a parameter index stands for (nullptr = not a dial)
const Macro* macroOfParam (int index) noexcept;

struct FactoryPreset
{
    const char* name;
    const char* type;
    std::vector<const char*> character; // GravePain's Character words: "Warm", "Bright"...
    int renderNote;
    float renderHold;
    std::vector<std::pair<Param, float>> values; // the rest stay at their defaults
};
const std::vector<FactoryPreset>& factoryPresets();
Patch patchOf (const FactoryPreset& preset);

// --- wavetables ----------------------------------------------------------------------------------
// LYNX's own banks, made by additive synthesis: 64 frames morphing across the bank, each in 11 band-limited copies
// (mip k keeps 1024 >> k harmonics). Built once, on first use (not on the audio thread), and kept for the session.
inline constexpr int tableFrameLength = 2048, tableFrames = 64, tableMips = 11;
struct Wavetable
{
    std::string name;
    int frames = tableFrames, mips = tableMips;
    std::vector<float> data; // [mip][frame][tableFrameLength]
    float read (int frame, float frac, int mip, float phase) const noexcept;
    void place (float position, int& frame, float& frac) const noexcept;
};
int mipFor (float frequencyHz, float sampleRate, int mips) noexcept;
const std::vector<const char*>& tableNames(); // "Basic shapes", "PWM", "Harmonics"
const Wavetable* table (int index);           // builds it the first time (UI thread)

// --- sample sets ---------------------------------------------------------------------------------
// A multisample: zones by note range and velocity layer (GravePain's SampleSet). Loaded by the UI thread from a
// folder of root-named WAVs or a .wti; the audio thread only reads it.
struct SampleZone
{
    std::vector<float> data; // mono
    int rate = 44100, root = 60, low = 0, high = 127, velocityFrom = 0;
    int loopStart = -1, loopEnd = -1;
    bool loop = false;
};
struct SampleSet
{
    std::string name;
    std::vector<SampleZone> zones;
    const SampleZone* find (int note, float velocity) const noexcept;
};
int rootFromName (const std::string& fileName); // "Piano C3", "F#4", "60": the note (C4 = 60), −1 when none
// A folder of WAVs named with their root (a zone each, the keyboard split half way between roots), or a .wti over WAV
// files (the user's own multisamples; zones by key range and velocity layer). UI thread; nullptr when nothing loads.
std::shared_ptr<const SampleSet> loadSampleFolder (const std::string& folder);

// --- the engine ----------------------------------------------------------------------------------
class Engine;
std::unique_ptr<Engine> makeEngine (double sampleRate);
void destroy (Engine*) noexcept;
struct EngineDeleter { void operator() (Engine* e) const noexcept { destroy (e); } };

// For tests and offline renders: plays the patch, adds `n` frames into left / right.
struct Offline
{
    explicit Offline (double sampleRate);
    ~Offline();
    void setPatch (const Patch& p) noexcept;
    void setTables (const Wavetable* a, const Wavetable* b) noexcept;
    void setSamples (std::shared_ptr<const SampleSet> set) noexcept;
    void setBpm (double bpm) noexcept;
    void noteOn (int key, float velocity) noexcept;
    void noteOff (int key) noexcept;
    void render (float* left, float* right, int n) noexcept;
    int activeVoices() const noexcept;
    std::unique_ptr<Engine, EngineDeleter> engine;
    std::shared_ptr<const SampleSet> samples;
};

// The LYNX instrument (InstrumentKind::gravepain): the patch from ChannelDef::modelParams, the sample set named by
// ChannelDef::samplePath (a folder of WAVs or a .wti).
std::unique_ptr<Instrument> makeInstrument();
} // namespace lynx::gpsynth
