// GravePain port, step 5: the GravePain Synth (core/GpSynth). Ported from GravePain's SynthEngineTests,
// SynthExpansionTests and SynthSourcesTests where they apply (the synth's own arpeggiator and sequencer are not
// ported: a LYNX channel has its own arpeggiator and the Piano roll plays the notes).

#include "core/GpSynth.h"
#include "core/ModelParams.h"
#include "core/GpHelm.h"
#include "core/LynxBanks.h"
#include "core/AudioEdit.h"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace gp_synth
{
using namespace lynx::gpsynth;
constexpr double rate = 48000.0;

inline std::vector<float> run (Offline& s, int frames)
{
    std::vector<float> l ((std::size_t) frames), r ((std::size_t) frames), out ((std::size_t) frames * 2);
    s.render (l.data(), r.data(), frames);
    for (int i = 0; i < frames; ++i)
    {
        out[(std::size_t) i * 2] = l[(std::size_t) i];
        out[(std::size_t) i * 2 + 1] = r[(std::size_t) i];
    }
    return out;
}
inline float peak (const std::vector<float>& v, std::size_t from = 0, std::size_t to = SIZE_MAX)
{
    float p = 0.0f;
    for (std::size_t i = from; i < std::min (to, v.size()); ++i)
        p = std::max (p, std::abs (v[i]));
    return p;
}
inline bool allFinite (const std::vector<float>& v)
{
    return std::all_of (v.begin(), v.end(), [] (float x) { return std::isfinite (x); });
}
inline std::vector<float> note (const Patch& p, int key = 60, float velocity = 1.0f, int on = 9600, int after = 9600)
{
    Offline s (rate);
    s.setPatch (p);
    s.noteOn (key, velocity);
    auto a = run (s, on);
    s.noteOff (key);
    auto b = run (s, after);
    a.insert (a.end(), b.begin(), b.end());
    return a;
}
// the distance between rising zero crossings in a stretch (samples a cycle), for pitch
inline double period (const std::vector<float>& stereo, std::size_t from, std::size_t to)
{
    int crossings = 0;
    std::size_t first = 0, last = 0;
    for (std::size_t i = from + 2; i < to && i < stereo.size(); i += 2)
        if (stereo[i - 2] <= 0.0f && stereo[i] > 0.0f)
        {
            if (crossings == 0)
                first = i;
            last = i;
            ++crossings;
        }
    return crossings > 1 ? (double) (last - first) / 2.0 / (crossings - 1) : 0.0;
}
} // namespace gp_synth

TEST (gp_synth_silent_then_sounds_and_ends)
{
    using namespace gp_synth;
    Offline s (rate);
    CHECK (peak (run (s, 2048)) == 0.0f);
    s.noteOn (60, 1.0f);
    const auto on = run (s, 9600);
    CHECK (peak (on) > 0.01f && allFinite (on));
    s.noteOff (60);
    run (s, 48000);
    CHECK (s.activeVoices() == 0);
    CHECK (peak (run (s, 2048)) < 1e-4f);
}

TEST (gp_synth_deterministic)
{
    using namespace gp_synth;
    auto p = defaultPatch();
    p[noiseLevel] = 0.3f;
    p[unisonVoices] = 5;
    CHECK (note (p) == note (p)); // the same patch and notes: the same samples
}

TEST (gp_synth_extremes_stay_finite)
{
    using namespace gp_synth;
    for (int corner = 0; corner < 2; ++corner)
    {
        Patch p {};
        for (const auto& info : patchParams())
            p[(std::size_t) info.param.index] = corner == 0 ? info.param.low : info.param.high;
        Offline s (rate);
        s.setPatch (p);
        s.setTables (table (0), table (1));
        for (int k = 0; k < 12; ++k)
            s.noteOn (36 + k * 5, 1.0f);
        const auto out = run (s, 24000);
        CHECK (allFinite (out));
        CHECK (peak (out) <= 1.0f); // the knee never reaches full scale
    }
}

TEST (gp_synth_nan_never_reaches_the_output)
{
    using namespace gp_synth;
    auto p = defaultPatch();
    p[cutoffHz] = std::numeric_limits<float>::quiet_NaN(); // a hand-edited patch can say anything
    const auto bad = patchOf (lynx::gpsynth::toModelParams (p));
    CHECK (std::isfinite (bad[cutoffHz]));
    CHECK (allFinite (note (bad)));
}

TEST (gp_synth_unison_stereo_and_mono)
{
    using namespace gp_synth;
    auto p = defaultPatch();
    p[unisonVoices] = 1;
    p[noiseLevel] = 0;
    const auto mono = note (p);
    float diff = 0.0f;
    for (std::size_t i = 0; i < mono.size(); i += 2)
        diff = std::max (diff, std::abs (mono[i] - mono[i + 1]));
    CHECK (diff < 1e-6f);
    p[unisonVoices] = 7;
    p[stereoSpread] = 1.0f;
    const auto wide = note (p);
    diff = 0.0f;
    for (std::size_t i = 0; i < wide.size(); i += 2)
        diff = std::max (diff, std::abs (wide[i] - wide[i + 1]));
    CHECK (diff > 0.01f);
}

TEST (gp_synth_voices_and_legato)
{
    using namespace gp_synth;
    Offline poly (rate);
    for (int k = 0; k < 40; ++k)
        poly.noteOn (40 + k, 1.0f);
    run (poly, 256);
    CHECK (poly.activeVoices() <= 32);
    auto p = defaultPatch();
    p[voiceMode] = (float) VoiceMode::legato;
    Offline s (rate);
    s.setPatch (p);
    s.noteOn (60, 1.0f);
    s.noteOn (64, 1.0f);
    run (s, 512);
    CHECK (s.activeVoices() == 1);
    s.noteOff (64); // falls back to the key still held
    run (s, 512);
    CHECK (s.activeVoices() == 1);
}

TEST (gp_synth_soft_hits_are_quieter)
{
    using namespace gp_synth;
    const auto p = defaultPatch();
    CHECK (peak (note (p, 60, 0.2f)) < peak (note (p, 60, 1.0f)) * 0.8f);
}

TEST (gp_synth_pitch_envelope)
{
    using namespace gp_synth;
    auto p = defaultPatch();
    p[waveA] = p[waveB] = (float) Wave::sine;
    p[filterMode] = (float) FilterMode::lowPass;
    p[cutoffHz] = 20000.0f;
    p[filterEnvAmount] = 0.0f;
    p[unisonVoices] = 1;
    const auto flat = note (p, 57);
    p[pitchEnvSemitones] = 0.0f;
    CHECK (note (p, 57) == flat); // at 0 nothing changes
    p[pitchEnvSemitones] = 24.0f;
    p[pitchEnvDecay] = 0.1f; // (settled, to a few cents, by the end of the note)
    const auto bent = note (p, 57);
    // early on the pitch is higher (a shorter period) than at the end
    CHECK (period (bent, 0, 1200) < period (bent, 14000, 19000) * 0.8);
    CHECK (std::abs (period (bent, 14000, 19000) - rate / 220.0) < 3.0);
    // Build climbs slower than Fall at first
    auto build = p;
    build[pitchEnvCurve] = 1.0f;
    const auto built = note (build, 57);
    CHECK (period (built, 0, 1200) < period (bent, 0, 1200));
}

TEST (gp_synth_clap_has_its_hits)
{
    using namespace gp_synth;
    auto p = defaultPatch();
    p[oscLevel] = 0.0f;
    p[noiseLevel] = 1.0f;
    p[attack] = 0.0005f;
    p[decay] = 0.004f;
    p[sustain] = 0.0f;
    p[clapHits] = 3;
    p[clapSpacingMs] = 10.0f;
    const auto out = note (p, 60, 1.0f, 2400, 2400);
    // three bursts: loud near 0, 10 and 20 ms, quiet just before each next one
    const auto ms = [] (double m) { return (std::size_t) (m * rate / 1000.0) * 2; };
    const float first = peak (out, ms (0), ms (2));
    CHECK (first > 0.002f);
    CHECK (peak (out, ms (10), ms (12)) > first * 0.5f);
    CHECK (peak (out, ms (20), ms (22)) > first * 0.5f);
    CHECK (peak (out, ms (8), ms (9.9)) < peak (out, ms (10), ms (12)) * 0.5f);
    CHECK (peak (out, ms (32), ms (40)) < first * 0.5f); // three hits, not four
}

TEST (gp_synth_effects_off_change_nothing_and_on_do)
{
    using namespace gp_synth;
    const auto p = defaultPatch();
    const auto dry = note (p);
    auto q = p;
    q[crush] = 0.0f;
    q[delayMix] = 0.0f;
    q[reverbMix] = 0.0f;
    q[noiseColour] = 0.0f;
    q[lfo2ToAmp] = 0.0f;
    CHECK (note (q) == dry);
    q[delayMix] = 0.5f;
    q[delaySync] = 0.0f;
    q[delayTimeMs] = 100.0f;
    q[release] = 0.005f; // (the dry note gone long before its echo)
    // a short note, then its echo 100 ms later
    const auto echo = note (q, 60, 1.0f, 480, 9600);
    CHECK (peak (echo, (std::size_t) (0.1 * rate) * 2 + 400, (std::size_t) (0.12 * rate) * 2) > 0.005f);
    auto verb = p;
    verb[reverbMix] = 0.6f;
    const auto tail = note (verb, 60, 1.0f, 2400, 48000);
    CHECK (peak (tail, 2 * (2400 + 24000), 2 * (2400 + 30000)) > 1e-4f);
    CHECK (peak (tail, tail.size() - 2000) < peak (tail, 2 * 2400, 2 * 4800));
}

TEST (gp_synth_crush_is_a_frequency)
{
    using namespace gp_synth;
    auto p = defaultPatch();
    p[crush] = 1.0f;
    // at full crush a sample is held for 44100 / 2756.25 = 16 samples at 44.1 kHz, ~17 at 48 kHz: the same time
    for (double sr : { 44100.0, 48000.0 })
    {
        Offline s (sr);
        s.setPatch (p);
        s.noteOn (60, 1.0f);
        std::vector<float> l (8192), r (8192);
        s.render (l.data(), r.data(), 8192);
        // a value is held for round(sr / 2756.25) samples (it may keep its level over a boundary): every change
        // lands a whole number of holds after the one before
        const int hold = (int) std::lround (sr / 2756.25);
        int last = -1, changes = 0;
        bool onTheGrid = true;
        for (int i = 4000; i < 8000; ++i)
            if (l[(std::size_t) i] != l[(std::size_t) i - 1])
            {
                if (last >= 0 && (i - last) % hold != 0)
                    onTheGrid = false;
                last = i;
                ++changes;
            }
        CHECK (changes > 20 && onTheGrid);
    }
}

TEST (gp_synth_wavetables)
{
    using namespace gp_synth;
    const auto* t = table (0);
    CHECK (t != nullptr && t->frames == 64 && t->mips == 11 && t->data.size() == (std::size_t) 64 * 11 * 2048);
    // frame 0 is a sine: a quarter of the way along it is at its top
    CHECK (std::abs (t->read (0, 0.0f, 0, 0.25f) - 1.0f) < 0.02f);
    CHECK (std::abs (t->read (0, 0.0f, 0, 0.75f) + 1.0f) < 0.02f);
    // between two frames, the read lies between theirs
    const float a = t->read (30, 0.0f, 0, 0.1f), b = t->read (31, 0.0f, 0, 0.1f), m = t->read (30, 0.5f, 0, 0.1f);
    CHECK (m >= std::min (a, b) - 1e-5f && m <= std::max (a, b) + 1e-5f);
    // the mip follows the note an octave at a time
    CHECK (mipFor (21.5f, 44100.0f, 11) == 0);
    CHECK (mipFor (440.0f, 44100.0f, 11) == mipFor (220.0f, 44100.0f, 11) + 1);
    CHECK (mipFor (20000.0f, 44100.0f, 11) == 10);
    for (int i : { 0, 1, 2 })
        CHECK (table (i) != nullptr);
    CHECK (table (3) == nullptr);
    // a wavetable patch renders
    auto p = defaultPatch();
    p[waveA] = (float) Wave::wavetable;
    p[tableA] = 1;
    p[positionA] = 0.5f;
    p[lfoToPosition] = 0.5f;
    Offline s (rate);
    s.setPatch (p);
    s.setTables (table (1), nullptr);
    s.noteOn (48, 1.0f);
    const auto out = run (s, 9600);
    CHECK (peak (out) > 0.01f && allFinite (out));
}

TEST (gp_synth_sample_sets)
{
    using namespace gp_synth;
    CHECK (rootFromName ("Piano C4") == 60);
    CHECK (rootFromName ("Bass_F#2") == 42);
    CHECK (rootFromName ("Db3 soft") == 49);
    CHECK (rootFromName ("064") == 64);
    CHECK (rootFromName ("no note here") == -1);
    auto set = std::make_shared<SampleSet>();
    SampleZone low, high, highSoft;
    low.root = 48, low.low = 0, low.high = 54;
    high.root = 60, high.low = 55, high.high = 127, high.velocityFrom = 64;
    highSoft = high;
    highSoft.velocityFrom = 0;
    for (auto* z : { &low, &high, &highSoft })
    {
        z->data.resize (4800);
        for (std::size_t i = 0; i < z->data.size(); ++i)
            z->data[i] = (float) std::sin (2.0 * 3.14159265 * 261.6 * (double) i / 44100.0);
    }
    set->zones = { low, high, highSoft };
    CHECK (set->find (40, 1.0f) == &set->zones[0]);
    CHECK (set->find (70, 1.0f) == &set->zones[1]);  // the loud layer
    CHECK (set->find (70, 0.2f) == &set->zones[2]);  // the soft one
    auto p = defaultPatch();
    p[sampleLevel] = 1.0f;
    Offline s (rate);
    s.setPatch (p);
    s.setSamples (set);
    s.noteOn (60, 1.0f);
    const auto out = run (s, 4800);
    CHECK (peak (out) > 0.01f && allFinite (out));
}

TEST (gp_synth_patch_in_the_channel)
{
    using namespace gp_synth;
    CHECK ((int) patchParams().size() == paramCount);
    for (int i = 0; i < paramCount; ++i)
    {
        const auto& info = patchParams()[(std::size_t) i];
        CHECK (info.param.index == i && ! info.param.name.empty());
        CHECK (info.defaultValue >= info.param.low && info.defaultValue <= info.param.high);
    }
    // the knobs read and write the channel's modelParams, an empty patch reading as the defaults
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::gravepain;
    CHECK (lynx::modelparams::rawValue (c, cutoffHz) == 2200.0f);
    CHECK (lynx::modelparams::setValue01 (c, cutoffHz, 1.0f));
    CHECK ((int) c.modelParams.size() == paramCount && c.modelParams[cutoffHz] == 20000.0f && c.modelParams[resonance] == 0.35f);
    CHECK (patchOf (c.modelParams)[cutoffHz] == 20000.0f);
    // the instrument plays from the channel
    auto inst = lynx::makeInstrument (lynx::InstrumentKind::gravepain);
    CHECK (inst != nullptr);
    inst->prepare (rate);
    inst->configure (c, 120.0);
    inst->noteOn (60, 1.0f);
    std::vector<float> l (4800), r (4800);
    inst->render (l.data(), r.data(), 4800, 1.0f, 1.0f);
    float m = 0.0f;
    for (float v : l)
        m = std::max (m, std::abs (v));
    CHECK (m > 0.01f);
}

TEST (gp_synth_factory_bank_and_dials)
{
    using namespace gp_synth;
    const auto& presets = factoryPresets();
    CHECK (presets.size() == 51);
    // every factory sound plays an eight-note chord within full scale, finite
    for (const auto& f : presets)
    {
        const auto p = patchOf (f);
        Offline s (rate);
        s.setPatch (p);
        s.setTables (table ((int) p[tableA]), table ((int) p[tableB]));
        for (int k = 0; k < 8; ++k)
            s.noteOn (f.renderNote + k * 3 - 12, 1.0f);
        const auto out = run (s, 9600);
        CHECK (allFinite (out) && peak (out) <= 1.0f);
        CHECK (soundTypeIndex (f.type) >= 0 && soundTypes()[(std::size_t) soundTypeIndex (f.type)] == f.type);
    }
    // LYNX's preset bank for the engine is the factory set
    const auto& bank = lynx::banks::bankOf (lynx::InstrumentKind::gravepain);
    CHECK (bank.size() == presets.size() && bank[1].name == "Acid Bass" && bank[1].category == "Bass");
    lynx::ChannelDef c;
    c.instrument = lynx::InstrumentKind::gravepain;
    bank[1].apply (c);
    CHECK (patchOf (c.modelParams)[cutoffHz] == 320.0f && patchOf (c.modelParams)[soundType] == (float) soundTypeIndex ("Bass"));
    // a dial moves its parameters together and reads back where it was put
    auto p = patchOf (presets[1]);
    const auto& dials = macrosFor ((int) p[soundType]);
    CHECK (std::string (dials.type) == "Bass" && std::string (dials.macros[0].name) == "Bright");
    setMacro (p, dials.macros[0], 0.5f);
    CHECK (std::abs (p[cutoffHz] - std::sqrt (60.0f * 3000.0f)) < 1.0f); // half way on a curve
    CHECK (std::abs (macroPosition (p, dials.macros[0]) - 0.5f) < 1e-4f);
    // the dials are automatable parameters of the channel too
    const int bassBright = macroParam0 + soundTypeIndex ("Bass") * macroCount;
    CHECK (lynx::modelparams::find (lynx::InstrumentKind::gravepain, bassBright)->name == "Bass Bright");
    CHECK (macroOfParam (bassBright) == &dials.macros[0] && macroOfParam (macroParam0 - 1) == nullptr);
    CHECK (lynx::modelparams::setRawValue (c, bassBright, 1.0f));
    CHECK (patchOf (c.modelParams)[cutoffHz] == 3000.0f);
    CHECK (std::string (macrosFor (soundTypeIndex ("Open Hat")).type) == "Hihat");
    for (const auto& set : macroSets())
        CHECK (! set.macros.empty() && set.macros.size() <= (std::size_t) macroCount);
}

TEST (gp_synth_helm_import)
{
    using namespace gp_synth;
    const std::string helm = R"({
        "patch_name": "Test Pad", "folder_name": "Mine", "author": "me",
        "settings": {
            "osc_1_waveform": 4, "osc_2_waveform": 2, "osc_1_volume": 0.7071, "osc_2_volume": 0.5,
            "osc_2_transpose": 12, "osc_2_tune": 0.1, "cutoff": 69, "resonance": 0.5, "filter_style": 1,
            "amp_attack": 0.5, "amp_release": 1.0, "polyphony": 1, "legato": 1, "arp_on": 1,
            "reverb_on": 1, "reverb_dry_wet": 0.4, "num_steps": 20,
            "modulations": [ { "source": "mono_lfo_1", "destination": "cutoff", "amount": 36 },
                             { "source": "nowhere", "destination": "cutoff", "amount": 1 } ]
        } })";
    std::string problem;
    const auto r = readHelm (helm, "presets/Mine/Test Pad.helm", problem);
    CHECK (r.has_value());
    CHECK (r->name == "Test Pad" && r->folder == "Mine" && r->author == "me");
    const auto& p = r->patch;
    CHECK (p[waveA] == (float) Wave::saw && p[waveB] == (float) Wave::square);
    CHECK (std::abs (p[cutoffHz] - 440.0f) < 0.5f);                 // MIDI note 69
    CHECK (p[filterMode] == (float) FilterMode::lowPass24);
    CHECK (p[semitonesB] == 12.0f && std::abs (p[detuneCents] - 10.0f) < 0.01f);
    CHECK (std::abs (p[attack] - 0.25f) < 1e-5f);                   // the square root of seconds
    CHECK (p[voiceMode] == (float) VoiceMode::legato);
    CHECK (std::abs (p[reverbMix] - 0.4f) < 1e-5f);
    CHECK (p[modSource (0)] == (float) ModSource::lfo1 && p[modTarget (0)] == (float) ModTarget::cutoff
           && std::abs (p[modAmount (0)] - 0.5f) < 1e-5f);          // 36 semitones of cutoff: half the range
    CHECK (r->unmapped.size() == 3);                                 // the arp, steps past 16, the unknown source
    CHECK (! readHelm ("{ not json", "x.helm", problem).has_value() && ! problem.empty());
    CHECK (! readHelm (R"({ "patch_name": "x" })", "x.helm", problem).has_value());
    CHECK (allFinite (note (p)));
}

TEST (gp_synth_sample_set_files)
{
    using namespace gp_synth;
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / ("lynx-gp-samples-" + std::to_string ((long long) std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories (dir);
    const auto wav = [&] (const std::string& name, double hz) {
        lynx::audioedit::Document doc;
        doc.audio.sampleRate = 44100.0;
        doc.audio.channels.assign (1, std::vector<float> (4410));
        for (std::size_t i = 0; i < 4410; ++i)
            doc.audio.channels[0][i] = 0.5f * (float) std::sin (2.0 * 3.14159265 * hz * (double) i / 44100.0);
        const auto bytes = lynx::audioedit::encodeWav (doc);
        std::ofstream (dir / name, std::ios::binary).write ((const char*) bytes.data(), (std::streamsize) bytes.size());
    };
    wav ("Keys C3.wav", 130.8);
    wav ("Keys C5.wav", 523.3);
    wav ("notes.txt.wav", 440.0); // no note in the name: not a zone
    const auto folder = loadSampleFolder (dir.string());
    CHECK (folder != nullptr && folder->zones.size() == 2);
    CHECK (folder->zones[0].root == 48 && folder->zones[1].root == 72);
    CHECK (folder->zones[0].high == 60 && folder->zones[1].low == 61); // split half way between the roots
    // a .wti over the same files: two layers on the first range
    std::ofstream (dir / "Keys.wti") << "[info]\nfiletype=riff\nsamples=3\n[velocitylayers]\ncount=2\n0=0\n1=80\n"
                                        "[sample0]\nname=Keys C3.wav\nroot=48\nhigh=60\n[sample1]\nname=Keys C5.wav\nroot=48\nhigh=60\n"
                                        "[sample2]\nname=Keys C5.wav\nroot=72\nhigh=127\n";
    const auto wti = loadSampleFolder ((dir / "Keys.wti").string());
    if (wti != nullptr)
    {
        CHECK (wti->zones.size() == 3);
        CHECK (wti->find (50, 0.2f) == &wti->zones[0] && wti->find (50, 1.0f) == &wti->zones[1]);
        CHECK (wti->find (100, 1.0f) == &wti->zones[2]);
    }
    else
        CHECK (false); // the .wti did not load
    CHECK (loadSampleFolder ((dir / "missing").string()) == nullptr);
    fs::remove_all (dir);
}
