// The GravePain Synth's channel page (GravePain port, step 5): LYNX's own design, not an FL arrangement. Five pages:
// the sound type's dials (GravePain's simple controls: Bright, Squelch, Space...), the oscillators, the filter and
// envelopes, the modulation (LFOs, poly LFO, free envelope, step sequencer, the 8-slot matrix) and the effects. Value
// items name the engine parameters of core/GpSynth.h (EngineParam::name).

#include <memory>
#include <string>

#include "core/GpSynth.h"
#include "core/PluginLayoutSynths.h"

#define TR_NOOP(text) text // marks a literal for translation (tools/i18n_keys.py scans core too)

namespace lynx::plugui
{
namespace
{
using namespace lynx::gpsynth;

const char* nameOf (int param) { return patchParams()[(std::size_t) param].param.name.c_str(); }

// "A|B|C" from a parameter's choices (kept for the session: the layout keeps the pointer)
const char* optionsOf (int param)
{
    static std::vector<std::unique_ptr<std::string>> keep;
    std::string s;
    for (const auto* c : patchParams()[(std::size_t) param].choices)
        s += (s.empty() ? "" : "|") + std::string (c);
    keep.push_back (std::make_unique<std::string> (s));
    return keep.back()->c_str();
}

struct Builder
{
    Layout l;
    int page = -1;

    LayoutItem& add (Item type, float x, float y, float w, float h, const char* param, const char* label, std::uint16_t style = 0)
    {
        LayoutItem i;
        i.type = type;
        i.x = x, i.y = y, i.w = w, i.h = h;
        i.param = param;
        i.label = label;
        i.style = style;
        i.page = page;
        l.items.push_back (i);
        return l.items.back();
    }
    void section (float x, float y, float w, float h, const char* title) { add (Item::section, x, y, w, h, nullptr, title); }
    LayoutItem& knob (float x, float y, int param, const char* caption, std::uint16_t style = 0)
    {
        auto& i = add (Item::knob, x, y, 34, 34 + 14, nameOf (param), caption, style);
        if (patchParams()[(std::size_t) param].param.stepped)
            i.steps = (int) (patchParams()[(std::size_t) param].param.high - patchParams()[(std::size_t) param].param.low);
        return i;
    }
    LayoutItem& menu (float x, float y, float w, int param, const char* caption)
    {
        auto& i = add (Item::menu, x, y, w, 18, nameOf (param), caption, captionLeft);
        i.options = optionsOf (param);
        return i;
    }
    LayoutItem& toggle (float x, float y, float w, int param, const char* caption)
    {
        return add (Item::toggle, x, y, w, 16, nameOf (param), caption, boxed);
    }
    LayoutItem& stepper (float x, float y, int param, const char* caption)
    {
        auto& i = add (Item::stepper, x, y, 44, 18, nameOf (param), caption, captionLeft);
        const auto& p = patchParams()[(std::size_t) param].param;
        i.steps = (int) (p.high - p.low);
        return i;
    }
};

Layout build()
{
    Builder b;
    b.l.fl = "";
    b.l.width = 660;
    b.l.height = 400;
    b.l.pages = { TR_NOOP ("Dials"), TR_NOOP ("Oscillators"), TR_NOOP ("Filter and envelopes"), TR_NOOP ("Modulation"), TR_NOOP ("Effects") };
    auto& tabs = b.add (Item::tabs, 4, 4, 400, 20, nullptr, nullptr, pageTabs); // (the window's Sample set / Helm buttons sit at the right)
    tabs.options = "Dials|Oscillators|Filter and envelopes|Modulation|Effects";

    // --- the dials: the sound type's simple controls, the volume ------------------------------------
    b.page = 0;
    b.section (4, 30, 652, 366, TR_NOOP ("Sound"));
    b.menu (14, 52, 260, soundType, TR_NOOP ("Type"));
    b.knob (600, 46, masterVolume, TR_NOOP ("Volume"), big);
    const auto& types = soundTypes();
    const float span = (float) (types.size() - 1);
    for (int t = 0; t < (int) types.size(); ++t)
    {
        const auto& set = macrosFor (t);
        // Open Hat shows Hihat's dials (they are Hihat's parameters)
        const int owner = soundTypeIndex (set.type);
        for (int d = 0; d < (int) set.macros.size() && d < macroCount; ++d)
        {
            const auto* p = findEngineParam (engineParams(), macroParam0 + owner * macroCount + d);
            if (p == nullptr)
                continue;
            auto& k = b.add (Item::knob, 40.0f + (float) (d % 5) * 120.0f, 120.0f + (float) (d / 5) * 120.0f, 56, 56 + 14, p->name.c_str(),
                             set.macros[(std::size_t) d].name, big);
            k.whenParam = nameOf (soundType); // only the current type's dials
            k.whenMin = ((float) t - 0.5f) / span;
            k.whenMax = ((float) t + 0.5f) / span;
        }
    }

    // --- oscillators ------------------------------------------------------------------------------------
    b.page = 1;
    b.section (4, 30, 324, 150, TR_NOOP ("Osc A"));
    b.menu (14, 52, 150, waveA, TR_NOOP ("Wave"));
    b.menu (174, 52, 146, tableA, TR_NOOP ("Table")).whenParam = nameOf (waveA);
    b.l.items.back().whenMin = 3.5f / 11.0f, b.l.items.back().whenMax = 4.5f / 11.0f;
    b.knob (14, 80, positionA, TR_NOOP ("Position"));
    b.knob (64, 80, pulseWidth, TR_NOOP ("Width"));
    b.knob (114, 80, transpose, TR_NOOP ("Transpose"), bipolar);
    b.knob (164, 80, oscFeedback, TR_NOOP ("Feedback"));
    b.knob (214, 80, oscSaturation, TR_NOOP ("Saturate"));
    b.knob (264, 80, oscLevel, TR_NOOP ("Level"));
    b.section (332, 30, 324, 150, TR_NOOP ("Osc B"));
    b.menu (342, 52, 150, waveB, TR_NOOP ("Wave"));
    b.menu (502, 52, 146, tableB, TR_NOOP ("Table")).whenParam = nameOf (waveB);
    b.l.items.back().whenMin = 3.5f / 11.0f, b.l.items.back().whenMax = 4.5f / 11.0f;
    b.knob (342, 80, positionB, TR_NOOP ("Position"));
    b.knob (392, 80, octaveB, TR_NOOP ("Octave"), bipolar);
    b.knob (442, 80, semitonesB, TR_NOOP ("Semis"), bipolar);
    b.knob (492, 80, detuneCents, TR_NOOP ("Detune"), bipolar);
    b.knob (542, 80, oscMix, TR_NOOP ("Mix"));
    b.knob (592, 80, crossMod, TR_NOOP ("Cross"));
    b.section (4, 184, 324, 100, TR_NOOP ("Unison and FM"));
    b.knob (14, 206, unisonVoices, TR_NOOP ("Voices"));
    b.knob (64, 206, unisonDetune, TR_NOOP ("Detune"));
    b.knob (114, 206, stereoSpread, TR_NOOP ("Spread"));
    b.knob (164, 206, fmDepth, TR_NOOP ("FM"));
    b.knob (214, 206, fmEnv, TR_NOOP ("FM env"));
    b.section (332, 184, 324, 100, TR_NOOP ("Sub, noise, sample"));
    b.knob (342, 206, subLevel, TR_NOOP ("Sub"));
    b.knob (392, 206, noiseLevel, TR_NOOP ("Noise"));
    b.knob (442, 206, noiseColour, TR_NOOP ("Colour"), bipolar);
    b.knob (492, 206, sampleLevel, TR_NOOP ("Sample"));
    b.knob (542, 206, sampleRoot, TR_NOOP ("Root"));
    b.section (4, 288, 652, 108, TR_NOOP ("Voices"));
    b.menu (14, 312, 150, voiceMode, TR_NOOP ("Mode"));
    b.knob (184, 306, glide, TR_NOOP ("Glide"));
    b.knob (234, 306, bendRange, TR_NOOP ("Bend"));
    b.knob (284, 306, clapHits, TR_NOOP ("Hits"));
    b.knob (334, 306, clapSpacingMs, TR_NOOP ("Spacing"));

    // --- filter and envelopes -----------------------------------------------------------------------------
    b.page = 2;
    b.section (4, 30, 652, 110, TR_NOOP ("Filter"));
    b.menu (14, 52, 160, filterMode, TR_NOOP ("Mode"));
    const int filterKnobs[] = { cutoffHz, resonance, keyTrack, filterEnvAmount, filterDrive, filterBlend, shelfGainDb, filterSaturation, velocityToCutoff };
    const char* filterCaptions[] = { TR_NOOP ("Cutoff"), TR_NOOP ("Reso"), TR_NOOP ("Key"), TR_NOOP ("Env"), TR_NOOP ("Drive"),
                                     TR_NOOP ("Blend"), TR_NOOP ("Shelf"), TR_NOOP ("Saturate"), TR_NOOP ("Vel") };
    for (int i = 0; i < 9; ++i)
        b.knob (190.0f + (float) i * 50.0f, 48, filterKnobs[i], filterCaptions[i], filterKnobs[i] == filterEnvAmount || filterKnobs[i] == shelfGainDb ? bipolar : 0);
    b.section (4, 144, 324, 100, TR_NOOP ("Filter envelope"));
    b.knob (14, 166, filterAttack, TR_NOOP ("Att"));
    b.knob (64, 166, filterDecay, TR_NOOP ("Dec"));
    b.knob (114, 166, filterSustain, TR_NOOP ("Sus"));
    b.knob (164, 166, filterRelease, TR_NOOP ("Rel"));
    b.section (332, 144, 324, 100, TR_NOOP ("Amp envelope"));
    b.knob (342, 166, attack, TR_NOOP ("Att"));
    b.knob (392, 166, decay, TR_NOOP ("Dec"));
    b.knob (442, 166, sustain, TR_NOOP ("Sus"));
    b.knob (492, 166, release, TR_NOOP ("Rel"));
    b.knob (542, 166, velocityToAmp, TR_NOOP ("Vel"));
    b.section (4, 248, 652, 100, TR_NOOP ("Pitch envelope"));
    b.knob (14, 270, pitchEnvSemitones, TR_NOOP ("Amount"), bipolar);
    b.knob (64, 270, pitchEnvDecay, TR_NOOP ("Time"));
    b.menu (124, 282, 150, pitchEnvCurve, TR_NOOP ("Curve"));

    // --- modulation ------------------------------------------------------------------------------------------
    b.page = 3;
    const auto lfoRow = [&b] (float y, const char* title, int wave, int rate, int sync, int division, int toPitch, int toCutoff, int toAmp, int toPw) {
        b.section (4, y, 652, 66, title);
        b.menu (14, y + 24, 120, wave, TR_NOOP ("Wave"));
        b.knob (144, y + 16, rate, TR_NOOP ("Rate"));
        b.toggle (190, y + 26, 50, sync, TR_NOOP ("Sync"));
        b.menu (246, y + 24, 90, division, nullptr);
        b.knob (350, y + 16, toPitch, TR_NOOP ("Pitch"));
        b.knob (400, y + 16, toCutoff, TR_NOOP ("Cutoff"));
        b.knob (450, y + 16, toAmp, TR_NOOP ("Level"));
        b.knob (500, y + 16, toPw, TR_NOOP ("Width"));
    };
    lfoRow (30, TR_NOOP ("LFO 1"), lfoWave, lfoRateHz, lfoSync, lfoDivision, lfoToPitch, lfoToCutoff, lfoToAmp, lfoToPulseWidth);
    b.knob (550, 46, lfoToPosition, TR_NOOP ("Position"), bipolar);
    b.knob (600, 46, envToPosition, TR_NOOP ("Env pos"), bipolar);
    lfoRow (98, TR_NOOP ("LFO 2"), lfo2Wave, lfo2RateHz, lfo2Sync, lfo2Division, lfo2ToPitch, lfo2ToCutoff, lfo2ToAmp, lfo2ToPulseWidth);
    b.section (4, 166, 324, 66, TR_NOOP ("Poly LFO and free envelope"));
    b.menu (14, 190, 90, polyLfoWave, nullptr);
    b.knob (110, 182, polyLfoRateHz, TR_NOOP ("Rate"));
    b.knob (160, 182, env3Attack, TR_NOOP ("Att"));
    b.knob (200, 182, env3Decay, TR_NOOP ("Dec"));
    b.knob (240, 182, env3Sustain, TR_NOOP ("Sus"));
    b.knob (280, 182, env3Release, TR_NOOP ("Rel"));
    b.section (332, 166, 324, 66, TR_NOOP ("Step sequencer"));
    for (int s = 0; s < 16; ++s)
        b.add (Item::vslider, 340.0f + (float) s * 14.0f, 186, 10, 40, nameOf (seqStep0 + s), nullptr, noCaption);
    b.stepper (572, 186, seqLength, nullptr);
    b.menu (572, 208, 76, seqDivision, nullptr);
    b.section (4, 236, 652, 160, TR_NOOP ("Modulation matrix"));
    for (int s = 0; s < modSlots; ++s)
    {
        const float x = s < 4 ? 14.0f : 338.0f, y = 258.0f + (float) (s % 4) * 34.0f;
        b.menu (x, y, 100, modSource (s), nullptr);
        b.menu (x + 104, y, 100, modTarget (s), nullptr);
        b.add (Item::hslider, x + 208, y + 2, 60, 14, nameOf (modAmount (s)), nullptr, (std::uint16_t) (noCaption | bipolar));
        b.menu (x + 272, y, 44, modVia (s), nullptr);
    }

    // --- effects --------------------------------------------------------------------------------------------
    b.page = 4;
    b.section (4, 30, 216, 90, TR_NOOP ("Drive and crush"));
    b.knob (14, 52, drive, TR_NOOP ("Drive"));
    b.knob (64, 52, crush, TR_NOOP ("Crush"));
    b.section (224, 30, 216, 90, TR_NOOP ("Chorus"));
    b.knob (234, 52, chorusMix, TR_NOOP ("Mix"));
    b.knob (284, 52, chorusRateHz, TR_NOOP ("Rate"));
    b.knob (334, 52, chorusDepth, TR_NOOP ("Depth"));
    b.section (444, 30, 212, 90, TR_NOOP ("Reverb"));
    b.knob (454, 52, reverbMix, TR_NOOP ("Mix"));
    b.knob (504, 52, reverbSize, TR_NOOP ("Size"));
    b.knob (554, 52, reverbDamp, TR_NOOP ("Damp"));
    b.section (4, 124, 652, 90, TR_NOOP ("Delay"));
    b.knob (14, 146, delayMix, TR_NOOP ("Mix"));
    b.knob (64, 146, delayFeedback, TR_NOOP ("Feedback"));
    b.toggle (114, 156, 50, delaySync, TR_NOOP ("Sync"));
    b.menu (170, 154, 90, delayDivision, nullptr);
    b.knob (270, 146, delayTimeMs, TR_NOOP ("Time"));
    b.toggle (320, 156, 90, delayPingPong, TR_NOOP ("Ping pong"));
    b.section (4, 218, 324, 90, TR_NOOP ("Formant"));
    b.knob (14, 240, formantMix, TR_NOOP ("Mix"));
    b.knob (64, 240, formantVowel, TR_NOOP ("Vowel"));
    b.section (332, 218, 324, 90, TR_NOOP ("Stutter"));
    b.knob (342, 240, stutterMix, TR_NOOP ("Mix"));
    b.menu (392, 252, 90, stutterDivision, nullptr);
    return std::move (b.l);
}
} // namespace

const Layout* gravepainLayout()
{
    static const Layout layout = build();
    return &layout;
}
} // namespace lynx::plugui
