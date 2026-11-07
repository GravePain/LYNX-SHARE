#include "ui/NoteToolDialogs.h"
#include "core/NoteNames.h"

#include <limits>
#include <map>
#include <optional>

#include "core/NoteTools.h"
#include "core/PianoRollTools.h"
#include "core/ChordDice.h"
#include "core/RiffStages.h"
#include "ui/PatternLibraryUi.h"
#include "engine/FlStudio.h"
#include "ui/I18n.h"
#include "ui/Theme.h"
#include "ui/ToolWindow.h"

namespace lynx
{
namespace
{
// FL's naming, as the piano roll beside it (round 149): key 60 = C5
juce::String keyName (int key)
{
    return juce::String (notenames::keyName (key)); // Settings › General › Note names (round 165f)
}

juce::StringArray pitchClassNames()
{
    return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

// the Piano Roll's scale box when the tool opened (setNoteToolKey): root 0..11, notetools::scales index (0 = none)
int& keyRoot() { static int r = 0; return r; }
int& keyScale() { static int s = 0; return s; }
bool keyIsMinor() { return keyScale() == 2 || keyScale() == 3 || keyScale() == 5 || keyScale() == 7 || keyScale() == 8; }

juce::String percent (double v)
{
    return juce::String (juce::roundToInt (v * 100.0)) + "%";
}

juce::String signedPercent (double v)
{
    return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v * 100.0)) + "%";
}

juce::String stepsText (double steps)
{
    return juce::String (steps, steps < 10.0 ? 2 : 0) + " " + tr ("steps");
}

// --- the user's FL Studio data (score templates, chord presets), read only -----------------------

const FlStudioInstall& flInstall()
{
    static const FlStudioInstall fl = findFlStudio();
    return fl;
}

juce::File flData (const juce::String& relative)
{
    return flInstall().installDir.getChildFile ("Data").getChildFile (relative);
}

// FL's .fsc files of a folder (recursive), named by their path inside it
struct ScoreFolder
{
    juce::StringArray names;
    std::vector<juce::File> files;

    explicit ScoreFolder (const juce::String& folder)
    {
        const auto dir = flData ("Patches/Scores/" + folder);

        if (! dir.isDirectory())
            return;

        auto found = dir.findChildFiles (juce::File::findFiles, true, "*.fsc");
        std::sort (found.begin(), found.end(), [] (const juce::File& a, const juce::File& b) {
            return a.getFullPathName().compareNatural (b.getFullPathName()) < 0;
        });

        for (const auto& f : found)
        {
            names.add (f.getRelativePathFrom (dir).upToLastOccurrenceOf (".", false, false).replaceCharacter ('\\', '/'));
            files.push_back (f);
        }
    }

    std::optional<prtools::Score> load (int index) const
    {
        if (index < 0 || index >= (int) files.size())
            return std::nullopt;

        juce::MemoryBlock data;
        if (! files[(std::size_t) index].loadFileAsData (data))
            return std::nullopt;

        auto score = prtools::parseScore ({ static_cast<const std::uint8_t*> (data.getData()), data.getSize() });
        if (score)
            score->name = names[index].toStdString();
        return score;
    }
};

const ScoreFolder& scoreFolder (const juce::String& folder)
{
    static std::map<juce::String, std::unique_ptr<ScoreFolder>> cache;
    auto& slot = cache[folder];
    if (slot == nullptr)
        slot = std::make_unique<ScoreFolder> (folder);
    return *slot;
}

juce::String readChordToolFile (const juce::String& name)
{
    const auto f = flInstall().installDir.getChildFile ("System/Tools/Chord progression/Chords").getChildFile (name);
    return f.existsAsFile() ? f.loadFileAsString() : juce::String();
}

const std::vector<prtools::ProgressionPreset>& progressionPresets()
{
    static const std::vector<prtools::ProgressionPreset> list = [] {
        auto installed = prtools::parseProgressionPresets (readChordToolFile ("presets.cpt").toStdString());
        return installed.empty() ? prtools::builtInProgressions() : installed;
    }();
    return list;
}

const std::vector<prtools::RhythmPreset>& rhythmPresets()
{
    static const std::vector<prtools::RhythmPreset> list = [] {
        auto installed = prtools::parseRhythmPresets (readChordToolFile ("rhythm_presets.cpt").toStdString());
        if (installed.empty())
            installed = { { "Half bars", ">------->-------" }, { "Quarter notes", ">--->--->--->---" },
                          { "Offbeat", "-->--->--->--->-" }, { "Push", ">-->---->-->----" } };
        return installed;
    }();
    return list;
}

// --- the panel -------------------------------------------------------------------------------------

struct Control
{
    enum Kind { slider, choice, toggle, text, header };

    Control (Kind k = slider, juce::String n = {}, double min = 0.0, double max = 1.0, double step = 0.01, double init = 0.0,
             std::function<juce::String (double)> f = {}, juce::StringArray items = {}, juce::String initText = {})
        : kind (k), name (std::move (n)), minimum (min), maximum (max), interval (step), initial (init), format (std::move (f)),
          choices (std::move (items)), initialText (std::move (initText))
    {
    }

    Kind kind;
    juce::String name;
    double minimum, maximum, interval, initial;
    std::function<juce::String (double)> format;
    juce::StringArray choices;
    juce::String initialText;
    std::function<void()> onChange; // runs before the preview (presets that set other controls)
};

class NoteToolPanel final : public juce::Component
{
public:
    NoteToolPanel (AudioEngine& e, NoteTool t, std::vector<int> i, std::int64_t g)
        : engine (e), tool (t), indices (std::move (i)), grid (juce::jmax<std::int64_t> (1, g))
    {
        const auto& song = engine.getSong();
        patternIndex = song.currentPattern;
        originals = song.patterns[(std::size_t) patternIndex].notes;
        channel = engine.getSelectedChannel();
        bpm = song.bpm;
        beatsPerBar = song.beatsPerBar;
        buildControls();

        title.setText (tr (noteToolTitle (tool)), juce::dontSendNotification);
        title.setFont (theme::font (theme::type::title, true));
        addAndMakeVisible (title);
        info.setFont (theme::font (theme::type::small));
        info.setColour (juce::Label::textColourId, theme::colour::textSecondary);
        addAndMakeVisible (info);
        updateInfo();

        for (auto* b : { &acceptButton, &resetButton, &cancelButton })
            addAndMakeVisible (b);

        acceptButton.setColour (juce::TextButton::buttonColourId, theme::colour::accent);
        acceptButton.onClick = [this] { accept(); };
        resetButton.onClick = [this] { resetControls(); };
        cancelButton.onClick = [this] { close(); };

        if (hasExtraButton())
        {
            extraButton.setButtonText (tool == NoteTool::riffMachine || tool == NoteTool::chordDice || tool == NoteTool::melodyDice || tool == NoteTool::riffStages ? tr ("Throw dice")
                                       : tool == NoteTool::patternLibrary ? tr ("Another pattern")
                                       : tool == NoteTool::chordProgression ? tr ("Generate")
                                                                            : tr ("New variation"));
            extraButton.onClick = [this] { extraAction(); };
            addAndMakeVisible (extraButton);
        }

        int height = 92 + 44 + (hasExtraButton() ? rowHeight : 0);
        for (const auto& c : controls)
            height += c.kind == Control::header ? headerHeight : rowHeight;

        setSize (440, height);
        preview();
    }

    ~NoteToolPanel() override
    {
        if (! accepted)
            restore();
    }

    std::function<void()> onFinished;

    void paint (juce::Graphics& g) override
    {
        g.fillAll (theme::colour::bg2);

        // section lines under the headers
        g.setColour (theme::colour::border);
        for (std::size_t c = 0; c < controls.size(); ++c)
            if (controls[c].kind == Control::header)
                g.fillRect (labels[c]->getBounds().removeFromBottom (1));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (14, 10);
        title.setBounds (area.removeFromTop (24));
        info.setBounds (area.removeFromTop (36));
        area.removeFromTop (6);

        for (std::size_t c = 0; c < controls.size(); ++c)
        {
            if (controls[c].kind == Control::header)
            {
                labels[c]->setBounds (area.removeFromTop (headerHeight).withTrimmedTop (6));
                continue;
            }

            auto row = area.removeFromTop (rowHeight).reduced (0, 3);
            labels[c]->setBounds (row.removeFromLeft (150));
            if (editors[c] != nullptr)
                editors[c]->setBounds (controls[c].kind == Control::toggle ? row.removeFromLeft (40) : row);
        }

        if (hasExtraButton())
            extraButton.setBounds (area.removeFromTop (rowHeight).reduced (0, 3).withTrimmedLeft (150).withWidth (160));

        auto buttons = area.removeFromBottom (30);
        acceptButton.setBounds (buttons.removeFromRight (100));
        buttons.removeFromRight (8);
        cancelButton.setBounds (buttons.removeFromRight (90));
        buttons.removeFromRight (8);
        resetButton.setBounds (buttons.removeFromRight (90));
    }

private:
    static constexpr int rowHeight = 30, headerHeight = 26;

    bool hasExtraButton() const noexcept
    {
        return tool == NoteTool::randomise || tool == NoteTool::humanize || tool == NoteTool::riffMachine || tool == NoteTool::chordProgression
               || tool == NoteTool::patternLibrary || tool == NoteTool::chordDice || tool == NoteTool::melodyDice || tool == NoteTool::riffStages;
    }

    // --- controls ------------------------------------------------------------------------------

    std::size_t add (Control c)
    {
        auto label = std::make_unique<juce::Label> (juce::String(), c.name);
        label->setFont (c.kind == Control::header ? theme::font (theme::type::small, true) : theme::font (theme::type::body));
        if (c.kind == Control::header)
            label->setColour (juce::Label::textColourId, theme::colour::textSecondary);
        addAndMakeVisible (*label);
        labels.push_back (std::move (label));

        std::unique_ptr<juce::Component> editor;
        const auto index = controls.size();
        const auto changed = [this, index] {
            if (! updating)
            {
                if (controls[index].onChange)
                    controls[index].onChange();
                preview();
            }
        };

        if (c.kind == Control::slider)
        {
            auto s = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
            s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 78, 20);
            s->setRange (c.minimum, c.maximum, c.interval);
            s->setValue (c.initial, juce::dontSendNotification);
            s->setDoubleClickReturnValue (true, c.initial);
            if (c.format)
                s->textFromValueFunction = c.format;
            s->updateText();
            s->onValueChange = changed;
            editor = std::move (s);
        }
        else if (c.kind == Control::choice)
        {
            auto box = std::make_unique<juce::ComboBox>();
            box->addItemList (c.choices, 1);
            box->setSelectedItemIndex ((int) c.initial, juce::dontSendNotification);
            box->onChange = changed;
            editor = std::move (box);
        }
        else if (c.kind == Control::toggle)
        {
            auto toggle = std::make_unique<juce::ToggleButton>();
            toggle->setToggleState (c.initial > 0.5, juce::dontSendNotification);
            toggle->onClick = changed;
            editor = std::move (toggle);
        }
        else if (c.kind == Control::text)
        {
            auto edit = std::make_unique<juce::TextEditor>();
            edit->setText (c.initialText, false);
            edit->setFont (theme::font (theme::type::body));
            edit->onTextChange = changed;
            editor = std::move (edit);
        }

        if (editor != nullptr)
            addAndMakeVisible (*editor);
        editors.push_back (std::move (editor));
        controls.push_back (std::move (c));
        return index;
    }

    void header (const juce::String& name) { add ({ Control::header, name }); }

    double value (std::size_t i) const
    {
        if (auto* s = dynamic_cast<juce::Slider*> (editors[i].get()))
            return s->getValue();
        if (auto* b = dynamic_cast<juce::ComboBox*> (editors[i].get()))
            return juce::jmax (0, b->getSelectedItemIndex());
        if (auto* t = dynamic_cast<juce::ToggleButton*> (editors[i].get()))
            return t->getToggleState() ? 1.0 : 0.0;
        return 0.0;
    }

    int intValue (std::size_t i) const { return juce::roundToInt (value (i)); }
    bool on (std::size_t i) const { return value (i) > 0.5; }

    juce::String textValue (std::size_t i) const
    {
        if (auto* t = dynamic_cast<juce::TextEditor*> (editors[i].get()))
            return t->getText();
        return {};
    }

    void setValue (std::size_t i, double v)
    {
        const juce::ScopedValueSetter<bool> quiet (updating, true);

        if (auto* s = dynamic_cast<juce::Slider*> (editors[i].get()))
            s->setValue (v, juce::dontSendNotification);
        else if (auto* b = dynamic_cast<juce::ComboBox*> (editors[i].get()))
            b->setSelectedItemIndex ((int) v, juce::dontSendNotification);
        else if (auto* t = dynamic_cast<juce::ToggleButton*> (editors[i].get()))
            t->setToggleState (v > 0.5, juce::dontSendNotification);
    }

    void setText (std::size_t i, const juce::String& text)
    {
        const juce::ScopedValueSetter<bool> quiet (updating, true);
        if (auto* t = dynamic_cast<juce::TextEditor*> (editors[i].get()))
            t->setText (text, false);
    }

    void resetControls()
    {
        for (std::size_t i = 0; i < controls.size(); ++i)
        {
            if (controls[i].kind == Control::text)
                setText (i, controls[i].initialText);
            else if (controls[i].kind != Control::header)
                setValue (i, controls[i].initial);
        }

        seed = 1;
        preview();
    }

    juce::StringArray levelNames() const
    {
        juce::StringArray levels;
        for (int l = 0; l < notetools::levelCount; ++l)
            levels.add (tr (notetools::levelName ((notetools::Level) l)));
        return levels;
    }

    // adds the six level-strength sliders (FL's PAN VEL MOD X REL MOD Y PITCH wheels); returns the first index
    std::size_t addLevelStrengths()
    {
        header (tr ("Level strengths"));
        const auto first = add ({ Control::slider, tr ("Velocity"), 0.0, 1.0, 0.01, 1.0, percent });
        add ({ Control::slider, tr ("Pan"), 0.0, 1.0, 0.01, 1.0, percent });
        add ({ Control::slider, tr ("Release"), 0.0, 1.0, 0.01, 1.0, percent });
        add ({ Control::slider, tr ("Mod X"), 0.0, 1.0, 0.01, 1.0, percent });
        add ({ Control::slider, tr ("Mod Y"), 0.0, 1.0, 0.01, 1.0, percent });
        add ({ Control::slider, tr ("Fine pitch"), 0.0, 1.0, 0.01, 1.0, percent });
        return first;
    }

    prtools::LevelStrengths levelStrengths (std::size_t first) const
    {
        prtools::LevelStrengths s;
        s.velocity = (float) value (first);
        s.pan = (float) value (first + 1);
        s.release = (float) value (first + 2);
        s.modX = (float) value (first + 3);
        s.modY = (float) value (first + 4);
        s.pitch = (float) value (first + 5);
        return s;
    }

    juce::StringArray withBuiltIn (const juce::String& builtIn, const juce::StringArray& files) const
    {
        juce::StringArray list { builtIn };
        list.addArray (files);
        return list;
    }

    void buildControls()
    {
        const auto steps = [] (double v) { return juce::String (v, 2) + " " + tr ("steps"); };

        switch (tool)
        {
            case NoteTool::strum:
                add ({ Control::slider, tr ("Spread"), 0.0, 2.0, 0.01, 0.25, steps });
                add ({ Control::slider, tr ("Curve"), -1.0, 1.0, 0.01, 0.0, [] (double v) { return juce::String (v, 2); } });
                add ({ Control::slider, tr ("Velocity per note"), -0.25, 0.25, 0.01, 0.0, signedPercent });
                add ({ Control::choice, tr ("Direction"), 0, 1, 1, 0, {}, { tr ("Low to high"), tr ("High to low") } });
                add ({ Control::toggle, tr ("Alternate"), 0, 1, 1, 0 });
                add ({ Control::toggle, tr ("Keep the ends"), 0, 1, 1, 1 });
                break;
            case NoteTool::arpeggiate:
                add ({ Control::choice, tr ("Step"), 0, 5, 1, 3, {}, { "1/4", "1/8", tr ("1/8 triplet"), "1/16", tr ("1/16 triplet"), "1/32" } });
                add ({ Control::slider, tr ("Gate"), 0.05, 1.0, 0.01, 0.8, percent });
                add ({ Control::choice, tr ("Direction"), 0, 3, 1, 0, {}, { tr ("Up"), tr ("Down"), tr ("Up and down"), tr ("Down and up") } });
                add ({ Control::slider, tr ("Octaves"), 1.0, 4.0, 1.0, 1.0, [] (double v) { return juce::String (juce::roundToInt (v)); } });
                break;
            case NoteTool::flip:
                add ({ Control::toggle, tr ("Flip in time"), 0, 1, 1, 1 });
                add ({ Control::toggle, tr ("Flip in pitch"), 0, 1, 1, 0 });
                break;
            case NoteTool::randomise:
                add ({ Control::slider, tr ("Velocity"), 0.0, 1.0, 0.01, 0.2, percent });
                add ({ Control::slider, tr ("Pan"), 0.0, 1.0, 0.01, 0.0, percent });
                add ({ Control::slider, tr ("Fine pitch"), 0.0, 1.0, 0.01, 0.0, percent });
                add ({ Control::slider, tr ("Timing"), 0.0, 0.5, 0.01, 0.0, steps });
                add ({ Control::slider, tr ("Length"), 0.0, 1.0, 0.01, 0.0, percent });
                add ({ Control::toggle, tr ("Both directions"), 0, 1, 1, 1 });
                break;
            case NoteTool::scaleLevels:
                add ({ Control::choice, tr ("Level"), 0, (double) notetools::levelCount - 1, 1, 0, {}, levelNames() });
                add ({ Control::slider, tr ("Scale at the start"), 0.0, 2.0, 0.01, 1.0, percent });
                add ({ Control::slider, tr ("Scale at the end"), 0.0, 2.0, 0.01, 1.0, percent });
                add ({ Control::slider, tr ("Offset"), -1.0, 1.0, 0.01, 0.0, signedPercent });
                break;
            case NoteTool::limit:
                add ({ Control::slider, tr ("Lowest note"), 0.0, 127.0, 1.0, 36.0, [] (double v) { return keyName (juce::roundToInt (v)); } });
                add ({ Control::slider, tr ("Highest note"), 0.0, 127.0, 1.0, 84.0, [] (double v) { return keyName (juce::roundToInt (v)); } });
                add ({ Control::slider, tr ("Lowest velocity"), 0.0, 1.0, 0.01, 0.0, percent });
                add ({ Control::slider, tr ("Highest velocity"), 0.0, 1.0, 0.01, 1.0, percent });
                break;

            case NoteTool::quantize:
            {
                // FL's quantizer: start/duration strength 64/128, sensitivity 36 (REA_rodada83 §1)
                add ({ Control::choice, tr ("Template"), 0, 0, 1, 0, {}, withBuiltIn (tr ("Piano roll grid"), scoreFolder ("Quantization").names) });
                add ({ Control::slider, tr ("Grid swing"), 0.0, 1.0, 0.01, 0.0, percent });
                add ({ Control::slider, tr ("Start time"), 0.0, 1.0, 1.0 / 128.0, 0.5, percent });
                add ({ Control::choice, tr ("Duration"), 0, 3, 1, 0, {},
                       { tr ("Leave duration"), tr ("Leave end time"), tr ("Quantize duration"), tr ("Quantize end time") } });
                add ({ Control::slider, tr ("Duration strength"), 0.0, 1.0, 1.0 / 128.0, 0.5, percent });
                add ({ Control::slider, tr ("Sensitivity"), 12.0, 192.0, 1.0, 36.0, [] (double v) { return stepsText (v / 48.0); } });
                levelsAt = addLevelStrengths();
                break;
            }
            case NoteTool::chop:
                add ({ Control::choice, tr ("Pattern"), 0, 0, 1, 0, {}, withBuiltIn (tr ("Piano roll grid"), scoreFolder ("Chopping").names) });
                add ({ Control::choice, tr ("Time"), 1, 7, 1, 3, {}, { "x1/8", "x1/4", "x1/2", "x1", "x2", "x3", "x4" } });
                add ({ Control::toggle, tr ("Keep on the song grid"), 0, 1, 1, 1 });
                levelsAt = addLevelStrengths();
                break;
            case NoteTool::flam:
            {
                const auto preset = add ({ Control::choice, tr ("Preset"), 0, 4, 1, 3, {},
                                           { tr ("Custom"), tr ("Flam"), tr ("Early flam"), tr ("Double stroke"), tr ("Early double stroke") } });
                controls[preset].onChange = [this, preset] {
                    const auto p = intValue (preset);
                    if (p == 0)
                        return;
                    const auto o = prtools::flamPreset ((prtools::FlamPreset) (p - 1));
                    setValue (1, o.time);
                    setValue (2, o.velocity / 128.0);
                    setValue (3, o.absolute ? 1 : 0);
                    setValue (4, o.before ? 1 : 0);
                    if (auto* s = dynamic_cast<juce::Slider*> (editors[1].get()))
                        s->updateText();
                };
                add ({ Control::slider, tr ("Time"), -192.0, 192.0, 1.0, 192.0, [this] (double v) {
                           return controls.size() > 3 && on (3) ? juce::String (v * 100.0 / 192.0, 1) + " ms" : stepsText (v / 192.0);
                       } });
                add ({ Control::slider, tr ("Velocity"), 0.0, 1.0, 1.0 / 128.0, 85.0 / 128.0, percent });
                add ({ Control::toggle, tr ("Time in milliseconds"), 0, 1, 1, 0 });
                add ({ Control::toggle, tr ("Before the note"), 0, 1, 1, 0 });
                for (std::size_t c = 1; c <= 4; ++c)
                    controls[c].onChange = [this] {
                        setValue (0, 0);
                        if (auto* s = dynamic_cast<juce::Slider*> (editors[1].get()))
                            s->updateText();
                    };
                break;
            }
            case NoteTool::lfo:
            {
                // FL's speed wheel, shown as the period (right = longer)
                const auto speed = [] (double v) { return stepsText (prtools::lfoPeriod (40960 - juce::roundToInt (v)) / ticksPerStep); };
                add ({ Control::choice, tr ("Level"), 0, (double) notetools::levelCount - 1, 1, 0, {}, levelNames() });
                add ({ Control::choice, tr ("Shape"), 0, 2, 1, 0, {}, { tr ("Sine"), tr ("Triangle"), tr ("Square") } });
                add ({ Control::slider, tr ("Phase"), 0.0, 1.0, 0.01, 0.5, percent });
                header (tr ("Start"));
                add ({ Control::slider, tr ("Value"), 0.0, 1.0, 0.01, 0.5, percent });
                add ({ Control::slider, tr ("Range"), -1.0, 1.0, 0.01, 0.25, signedPercent });
                add ({ Control::slider, tr ("Period"), 0.0, 40960.0, 1.0, 20480.0, speed });
                header (tr ("End"));
                add ({ Control::toggle, tr ("Change toward the end"), 0, 1, 1, 0 });
                add ({ Control::slider, tr ("Value"), 0.0, 1.0, 0.01, 0.5, percent });
                add ({ Control::slider, tr ("Range"), -1.0, 1.0, 0.01, 0.25, signedPercent });
                add ({ Control::slider, tr ("Period"), 0.0, 40960.0, 1.0, 20480.0, speed });
                add ({ Control::toggle, tr ("Logarithmic speed change"), 0, 1, 1, 0 });
                break;
            }
            case NoteTool::claw:
            {
                juce::StringArray trash;
                for (auto* n : prtools::clawTrashNames())
                    trash.add (juce::String (n) == "(none)" ? tr ("(none)") : juce::String (n));
                add ({ Control::choice, tr ("Period"), 0, 4, 1, 0, {}, { tr ("1 beat"), tr ("2 beats"), tr ("4 beats"), tr ("8 beats"), tr ("16 beats") } });
                add ({ Control::choice, tr ("Remove every"), 0, 9, 1, 3, {}, trash });
                add ({ Control::slider, tr ("Time distortion"), -1.0, 1.0, 1.0 / 1024.0, 0.0, signedPercent });
                add ({ Control::toggle, tr ("Stretch to compensate"), 0, 1, 1, 1 });
                add ({ Control::toggle, tr ("Remove short notes"), 0, 1, 1, 1 });
                break;
            }
            case NoteTool::articulate:
            {
                const auto preset = add ({ Control::choice, tr ("Preset"), 0, 5, 1, 1, {},
                                           { tr ("Custom"), tr ("Legato"), tr ("Portato"), tr ("Staccato"), tr ("Small gaps"), tr ("Only chop chords") } });
                controls[preset].onChange = [this, preset] {
                    const auto p = intValue (preset);
                    if (p == 0)
                        return;
                    const auto o = prtools::articulatePreset ((prtools::ArticulatePreset) (p - 1));
                    setValue (1, o.multiply / 100.0);
                    setValue (2, o.variation / 100.0);
                    setValue (3, o.gap);
                    setValue (5, o.useLengths ? 1 : 0);
                    setValue (6, o.chopChords ? 1 : 0);
                };
                add ({ Control::slider, tr ("Length"), 0.1, 1.0, 0.01, 1.0, percent });
                add ({ Control::slider, tr ("Variation"), 0.0, 1.0, 0.01, 0.0, percent });
                add ({ Control::slider, tr ("Gap"), 0.0, 240.0, 1.0, 0.0, [] (double v) { return stepsText (v / 240.0); } });
                add ({ Control::slider, tr ("Variation seed"), 0.0, 999.0, 1.0, 0.0, [] (double v) { return juce::String (juce::roundToInt (v)); } });
                add ({ Control::toggle, tr ("Scale the notes' own lengths"), 0, 1, 1, 0 });
                add ({ Control::toggle, tr ("Chop chords"), 0, 1, 1, 0 });
                for (std::size_t c = 1; c <= 6; ++c)
                    controls[c].onChange = [this] { setValue (0, 0); };
                break;
            }
            case NoteTool::riffMachine:
            {
                juce::StringArray prog, chord;
                for (int v = 0; v < prtools::builtInRiffProgressionCount(); ++v)
                    prog.add (tr ("Built-in") + " " + juce::String (v + 1) + ": " + tr (prtools::builtInRiffProgression (v).name));
                prog.addArray (scoreFolder ("Chopping/Riff machine step 1").names);
                for (int v = 0; v < prtools::builtInRiffChordCount(); ++v)
                    chord.add (tr ("Built-in") + " " + juce::String (v + 1) + ": " + tr (prtools::builtInRiffChord (v).name));
                chord.addArray (scoreFolder ("Chopping/Riff machine step 2").names);

                add ({ Control::slider, tr ("Length"), 1.0, 16.0, 1.0, 4.0, [] (double v) { return juce::String (juce::roundToInt (v)) + " " + tr ("bars"); } });
                add ({ Control::toggle, tr ("Work on the existing notes"), 0, 1, 1, 0 });
                header (tr ("Steps"));
                add ({ Control::toggle, tr ("1. Progression"), 0, 1, 1, 1 });
                add ({ Control::choice, tr ("Rhythm and melody"), 0, 0, 1, 0, {}, prog });
                add ({ Control::toggle, tr ("2. Chords"), 0, 1, 1, 1 });
                add ({ Control::choice, tr ("Chord shape"), 0, 0, 1, 0, {}, chord });
                add ({ Control::toggle, tr ("3. Arpeggio"), 0, 1, 1, 0 });
                add ({ Control::toggle, tr ("4. Humanize levels"), 0, 1, 1, 0 });
                add ({ Control::toggle, tr ("5. Mirror"), 0, 1, 1, 0 });
                add ({ Control::toggle, tr ("6. Articulation"), 0, 1, 1, 1 });
                add ({ Control::toggle, tr ("7. Groove"), 0, 1, 1, 0 });
                add ({ Control::toggle, tr ("8. Fit to the scale"), 0, 1, 1, 1 });
                add ({ Control::choice, tr ("Key"), 0, 11, 1, 0, {}, pitchClassNames() });
                juce::StringArray scaleNames;
                for (std::size_t s = 1; s < notetools::scales().size(); ++s)
                    scaleNames.add (tr (notetools::scales()[s].name));
                add ({ Control::choice, tr ("Scale"), 0, 0, 1, 0, {}, scaleNames });
                break;
            }
            case NoteTool::euclidean:
            {
                const auto count = [] (double v) { return juce::String (juce::roundToInt (v)); };
                add ({ Control::slider, tr ("Steps"), 1.0, 32.0, 1.0, 16.0, count });
                add ({ Control::slider, tr ("Pulses"), 0.0, 32.0, 1.0, 5.0, count });
                add ({ Control::slider, tr ("Rotation"), 0.0, 31.0, 1.0, 0.0, count });
                add ({ Control::choice, tr ("Step"), 0, 3, 1, 2, {}, { "1/4", "1/8", "1/16", "1/32" } });
                add ({ Control::slider, tr ("Length"), 1.0, 16.0, 1.0, 1.0, [] (double v) { return juce::String (juce::roundToInt (v)) + " " + tr ("bars"); } });
                add ({ Control::slider, tr ("Key"), 0.0, 127.0, 1.0, 60.0, [] (double v) { return keyName (juce::roundToInt (v)); } });
                add ({ Control::slider, tr ("Velocity"), 0.0, 1.0, 0.01, 0.78, percent });
                add ({ Control::slider, tr ("Gate"), 0.05, 1.0, 0.01, 0.5, percent });
                add ({ Control::slider, tr ("Accent on the first hit"), 0.0, 0.5, 0.01, 0.0, signedPercent });
                if (! indices.empty() && indices.front() >= 0 && indices.front() < (int) originals.size())
                    setValue (5, originals[(std::size_t) indices.front()].key); // the rhythm on the key that was there
                break;
            }
            case NoteTool::humanize:
                add ({ Control::slider, tr ("Timing"), 0.0, 60.0, 1.0, 12.0, [] (double v) { return stepsText (v / ticksPerStep); } });
                add ({ Control::slider, tr ("Velocity"), 0.0, 0.5, 0.01, 0.08, percent });
                add ({ Control::slider, tr ("Length"), 0.0, 0.5, 0.01, 0.05, percent });
                add ({ Control::toggle, tr ("Softer off-beat notes"), 0, 1, 1, 1 });
                break;
            case NoteTool::noteRepeat:
                add ({ Control::choice, tr ("Rate"), 0, 5, 1, 3, {}, { "1/4", "1/8", tr ("1/8 triplet"), "1/16", tr ("1/16 triplet"), "1/32" } });
                add ({ Control::slider, tr ("Gate"), 0.05, 1.0, 0.01, 0.5, percent });
                add ({ Control::slider, tr ("Velocity per repeat"), 0.5, 1.2, 0.01, 0.9, percent });
                add ({ Control::slider, tr ("Pitch per repeat"), -12.0, 12.0, 1.0, 0.0, [] (double v) {
                           const auto st = juce::roundToInt (v);
                           return (st > 0 ? "+" : "") + juce::String (st) + " " + tr ("semitones");
                       } });
                break;
            case NoteTool::chordProgression:
            {
                juce::StringArray presets { tr ("(typed or generated)") };
                for (const auto& p : progressionPresets())
                    presets.add (juce::String::fromUTF8 (p.name.c_str()) + (p.minor ? " (" + tr ("minor") + ")" : juce::String()));
                juce::StringArray rhythms { tr ("Held chords") };
                for (const auto& r : rhythmPresets())
                    rhythms.add (juce::String::fromUTF8 (r.name.c_str()));

                const auto first = progressionPresets().empty() ? std::string ("I-V-vi-IV") : progressionPresets().front().numerals;
                add ({ Control::choice, tr ("Key"), 0, 11, 1, 0, {}, pitchClassNames() });
                add ({ Control::choice, tr ("Scale"), 0, 1, 1, 0, {}, { tr ("Major"), tr ("Minor") } });
                const auto preset = add ({ Control::choice, tr ("Preset"), 0, 0, 1, 1, {}, presets });
                add ({ Control::text, tr ("Progression"), 0, 0, 0, 0, {}, {}, juce::String::fromUTF8 (first.c_str()) });
                controls[preset].onChange = [this, preset] {
                    const auto p = intValue (preset) - 1;
                    if (p < 0 || p >= (int) progressionPresets().size())
                        return;
                    const auto& pr = progressionPresets()[(std::size_t) p];
                    setValue (1, pr.minor ? 1 : 0);
                    setText (3, juce::String::fromUTF8 (pr.numerals.c_str()));
                };
                controls[3].onChange = [this] { setValue (2, 0); };
                add ({ Control::slider, tr ("Chords to generate"), 2.0, 16.0, 1.0, 4.0, [] (double v) { return juce::String (juce::roundToInt (v)); } });
                add ({ Control::slider, tr ("Adventurous"), 0.0, 1.0, 0.01, 0.3, percent });
                add ({ Control::choice, tr ("Chord length"), 0, 3, 1, 2, {}, { tr ("1 beat"), tr ("2 beats"), tr ("1 bar"), tr ("2 bars") } });
                add ({ Control::slider, tr ("Octave"), 2.0, 7.0, 1.0, 4.0, [] (double v) { return "C" + juce::String (juce::roundToInt (v)); } });
                add ({ Control::choice, tr ("Rhythm"), 0, 0, 1, 0, {}, rhythms });
                add ({ Control::toggle, tr ("Bass note"), 0, 1, 1, 1 });
                add ({ Control::toggle, tr ("Smooth voicing"), 0, 1, 1, 1 });
                break;
            }
            case NoteTool::patternLibrary:
            {
                // (the dataset is built on first use: a few seconds for the bundled library, then cached)
                juce::StringArray anyStyle { tr ("Any") }, anySection { tr ("Any") }, anyRole { tr ("Any") };
                anyStyle.addArray (patternlib::styles());
                for (const auto& section : patternlib::sections())
                    anySection.add (tr (section));
                for (const auto& r : patternlib::roles())
                    anyRole.add (tr (r));
                juce::StringArray keys { tr ("As written") };
                keys.addArray (pitchClassNames());
                gp.push_back (add ({ Control::choice, tr ("Style"), 0, 0, 1, 0, {}, anyStyle }));
                gp.push_back (add ({ Control::choice, tr ("Section"), 0, 0, 1, 0, {}, anySection }));
                gp.push_back (add ({ Control::choice, tr ("Role"), 0, 0, 1, 0, {}, anyRole }));
                gp.push_back (add ({ Control::choice, tr ("Key"), 0, 12, 1, keyScale() > 0 ? keyRoot() + 1.0 : 0.0, {}, keys }));
                gp.push_back (add ({ Control::choice, tr ("Scale"), 0, 1, 1, keyIsMinor() ? 1.0 : 0.0, {}, { tr ("Major"), tr ("Minor") } }));
                gp.push_back (add ({ Control::choice, tr ("Length"), 0, 3, 1, 2, {}, { tr ("1 bar"), tr ("2 bars"), tr ("4 bars"), tr ("8 bars") } }));
                break;
            }
            case NoteTool::chordDice:
            {
                const auto saved = prtools::riff::section (currentToolSettings(), "dice");
                juce::StringArray progs { tr ("Dice (from the seed)") };
                for (bool minor : { false, true })
                    for (const auto& p : prtools::dice::progressions (minor))
                        progs.add (juce::String (p.name) + (minor ? " (" + tr ("minor") + ")" : juce::String()));
                juce::StringArray shapes, inversions;
                for (int i = 0; i < prtools::dice::shapeCount; ++i)
                    shapes.add (tr (prtools::dice::shapeLabel ((prtools::dice::Shape) i)));
                for (int i = 0; i < 3; ++i)
                    inversions.add (tr (prtools::dice::inversionLabel (i)));
                gp.push_back (add ({ Control::choice, tr ("Key"), 0, 11, 1, (double) keyRoot(), {}, pitchClassNames() }));
                gp.push_back (add ({ Control::choice, tr ("Scale"), 0, 1, 1, keyIsMinor() ? 1.0 : 0.0, {}, { tr ("Major"), tr ("Minor") } }));
                gp.push_back (add ({ Control::choice, tr ("Progression"), 0, 0, 1, 0, {}, progs }));
                gp.push_back (add ({ Control::choice, tr ("Shape"), 0, 0, 1, 0, {}, shapes }));
                gp.push_back (add ({ Control::choice, tr ("Inversion"), 0, 0, 1, 0, {}, inversions }));
                gp.push_back (add ({ Control::slider, tr ("Octave"), 2.0, 6.0, 1.0, 3.0, [] (double v) { return "C" + juce::String (juce::roundToInt (v)); } }));
                gp.push_back (add ({ Control::choice, tr ("Chord length"), 0, 1, 1, 0, {}, { tr ("1 bar"), tr ("2 bars") } }));
                seed = (std::uint32_t) juce::jmax (1, savedInt (saved, "seed", 1));
                break;
            }
            case NoteTool::melodyDice:
            {
                const auto saved = prtools::riff::section (currentToolSettings(), "melody");
                gp.push_back (add ({ Control::choice, tr ("Key"), 0, 11, 1, (double) keyRoot(), {}, pitchClassNames() }));
                gp.push_back (add ({ Control::choice, tr ("Scale"), 0, 1, 1, keyIsMinor() ? 1.0 : 0.0, {}, { tr ("Major"), tr ("Minor") } }));
                gp.push_back (add ({ Control::choice, tr ("Notes"), 0, 2, 1, 1, {}, { "1/4", "1/8", "1/16" } }));
                gp.push_back (add ({ Control::slider, tr ("Octave"), 2.0, 7.0, 1.0, 4.0, [] (double v) { return "C" + juce::String (juce::roundToInt (v)); } }));
                gp.push_back (add ({ Control::slider, tr ("Range"), 1.0, 3.0, 1.0, 2.0, [] (double v) { return juce::String (juce::roundToInt (v)) + " " + tr ("octaves"); } }));
                gp.push_back (add ({ Control::slider, tr ("Home degree"), 1.0, 7.0, 1.0, 1.0, [] (double v) { return juce::String (juce::roundToInt (v)); } }));
                gp.push_back (add ({ Control::toggle, tr ("Follow the chord lane"), 0, 1, 1, 1 }));
                gp.push_back (add ({ Control::choice, tr ("Ending"), 0, 2, 1, 0, {}, { tr ("Loops round"), tr ("Lands at the end"), tr ("Lands at bars 2 and 4") } }));
                seed = (std::uint32_t) juce::jmax (1, savedInt (saved, "seed", 1));
                break;
            }
            case NoteTool::riffStages:
            {
                // the pattern's saved settings come back, so the same roll returns
                const auto s = prtools::riff::fromText (prtools::riff::section (currentToolSettings(), "riff"));
                seed = s.seed;
                juce::StringArray grooves;
                for (int g = 0; g < prtools::riff::grooveCount; ++g)
                    grooves.add (tr (prtools::riff::grooveLabel ((prtools::riff::Groove) g)));
                header (tr ("Mirror"));
                gp.push_back (add ({ Control::toggle, tr ("Mirror"), 0, 1, 1, s.mirrorOn ? 1.0 : 0.0 }));
                gp.push_back (add ({ Control::toggle, tr ("Turn time over"), 0, 1, 1, s.mirrorTime ? 1.0 : 0.0 }));
                gp.push_back (add ({ Control::toggle, tr ("Keep the starts"), 0, 1, 1, s.mirrorKeepStarts ? 1.0 : 0.0 }));
                gp.push_back (add ({ Control::toggle, tr ("Turn pitch over"), 0, 1, 1, s.mirrorPitch ? 1.0 : 0.0 }));
                header (tr ("Levels"));
                gp.push_back (add ({ Control::toggle, tr ("Levels"), 0, 1, 1, s.levelsOn ? 1.0 : 0.0 }));
                gp.push_back (add ({ Control::toggle, tr ("Reset first"), 0, 1, 1, s.levelsReset ? 1.0 : 0.0 }));
                gp.push_back (add ({ Control::toggle, tr ("Louder and quieter"), 0, 1, 1, s.levelsBipolar ? 1.0 : 0.0 }));
                gp.push_back (add ({ Control::slider, tr ("Amount"), 0.0, 1.0, 0.01, s.levelsAmount, percent }));
                gp.push_back (add ({ Control::slider, tr ("Lengths"), 0.0, 1.0, 0.01, s.levelsLength, percent }));
                header (tr ("Articulation"));
                gp.push_back (add ({ Control::toggle, tr ("Articulation"), 0, 1, 1, s.artOn ? 1.0 : 0.0 }));
                gp.push_back (add ({ Control::toggle, tr ("Chop to the grid"), 0, 1, 1, s.artChop ? 1.0 : 0.0 }));
                gp.push_back (add ({ Control::slider, tr ("Multiply"), 0.25, 4.0, 0.05, s.artMultiply, [] (double v) { return juce::String (v, 2) + "x"; } }));
                gp.push_back (add ({ Control::slider, tr ("Gap"), 0.0, 0.5, 0.01, s.artGap, stepsText }));
                gp.push_back (add ({ Control::slider, tr ("Variation"), 0.0, 1.0, 0.01, s.artVariation, percent }));
                header (tr ("Groove"));
                gp.push_back (add ({ Control::toggle, tr ("Groove"), 0, 1, 1, s.grooveOn ? 1.0 : 0.0 }));
                gp.push_back (add ({ Control::choice, tr ("Template"), 0, 0, 1, (double) (int) s.groove, {}, grooves }));
                gp.push_back (add ({ Control::slider, tr ("Timing"), 0.0, 1.0, 0.01, s.grooveTiming, percent }));
                break;
            }
        }
    }

    // --- applying ------------------------------------------------------------------------------

    std::optional<prtools::Score> chosenScore (const juce::String& folder, int choice, const prtools::Score& builtIn) const
    {
        if (choice <= 0)
            return builtIn;

        const auto key = folder + "#" + juce::String (choice);
        if (auto found = scoreCache.find (key); found != scoreCache.end())
            return found->second;

        auto score = scoreFolder (folder).load (choice - 1);
        if (score)
            scoreCache[key] = *score;
        return score;
    }

    std::int64_t generatorStart() const
    {
        if (indices.empty())
            return 0;

        std::int64_t start = std::numeric_limits<std::int64_t>::max();
        for (auto i : indices)
            if (i >= 0 && i < (int) originals.size())
                start = std::min (start, originals[(std::size_t) i].start);

        const auto bar = (std::int64_t) ppq * juce::jmax (1, beatsPerBar);
        return start == std::numeric_limits<std::int64_t>::max() ? 0 : start / bar * bar;
    }

    void apply (Pattern& p)
    {
        using namespace notetools;
        status.clear();
        chordNames.clear();

        switch (tool)
        {
            case NoteTool::strum:
            {
                StrumOptions o;
                o.spread = (std::int64_t) std::llround (value (0) * ticksPerStep);
                o.curve = (float) value (1);
                o.velocityStep = (float) value (2);
                o.downward = on (3);
                o.alternate = on (4);
                o.keepEnds = on (5);
                strum (p, indices, o);
                break;
            }
            case NoteTool::arpeggiate:
            {
                static const std::int64_t stepTicks[] = { ppq, ppq / 2, ppq / 3, ppq / 4, ppq / 6, ppq / 8 };
                ArpeggiateOptions o;
                o.step = stepTicks[juce::jlimit (0, 5, intValue (0))];
                o.gate = (float) value (1);
                o.direction = intValue (2);
                o.octaves = intValue (3);
                arpeggiate (p, indices, o);
                break;
            }
            case NoteTool::flip:
                flip (p, indices, on (0), on (1));
                break;
            case NoteTool::randomise:
            {
                RandomiseOptions o;
                o.velocity = (float) value (0);
                o.pan = (float) value (1);
                o.finePitch = (float) value (2);
                o.timing = (std::int64_t) std::llround (value (3) * ticksPerStep);
                o.length = (float) value (4);
                o.bipolar = on (5);
                o.seed = seed;
                randomise (p, indices, o);
                break;
            }
            case NoteTool::scaleLevels:
                scaleLevels (p, indices, (Level) intValue (0), (float) value (1), (float) value (2), (float) value (3));
                break;
            case NoteTool::limit:
            {
                LimitOptions o;
                o.lowKey = intValue (0);
                o.highKey = intValue (1);
                o.minVelocity = (float) value (2);
                o.maxVelocity = (float) value (3);
                limit (p, indices, o);
                break;
            }
            case NoteTool::quantize:
            {
                const auto groove = chosenScore ("Quantization", intValue (0), prtools::gridScore (grid, (float) value (1)));
                if (! groove)
                {
                    status = tr ("This template could not be read.");
                    break;
                }
                prtools::QuantizeOptions o;
                o.start = (float) value (2);
                o.durationMode = intValue (3);
                o.duration = (float) value (4);
                o.sensitivity = intValue (5);
                o.levels = levelStrengths (levelsAt);
                prtools::quantize (p, indices, *groove, o);
                break;
            }
            case NoteTool::chop:
            {
                const auto pattern = chosenScore ("Chopping", intValue (0), prtools::gridScore (grid));
                if (! pattern)
                {
                    status = tr ("This pattern could not be read.");
                    break;
                }
                prtools::ChopOptions o;
                o.timeMul = intValue (1) + 1;
                o.absolute = on (2);
                o.levels = levelStrengths (levelsAt);
                prtools::chop (p, indices, *pattern, o);
                break;
            }
            case NoteTool::flam:
            {
                prtools::FlamOptions o;
                o.time = intValue (1);
                o.velocity = juce::roundToInt (value (2) * 128.0);
                o.absolute = on (3);
                o.before = on (4);
                prtools::flam (p, indices, o, bpm);
                break;
            }
            case NoteTool::lfo:
            {
                prtools::LfoOptions o;
                o.shape = intValue (1);
                o.phase = juce::jlimit (0, 65535, juce::roundToInt (value (2) * 65536.0));
                o.startValue = juce::roundToInt (value (4) * 65536.0);
                o.startRange = juce::roundToInt (value (5) * 65536.0);
                o.startSpeed = 40960 - intValue (6);
                o.useEnd = on (8);
                o.endValue = juce::roundToInt (value (9) * 65536.0);
                o.endRange = juce::roundToInt (value (10) * 65536.0);
                o.endSpeed = 40960 - intValue (11);
                o.logSpeed = on (12);
                prtools::lfo (p, indices, (Level) intValue (0), o);
                break;
            }
            case NoteTool::claw:
            {
                prtools::ClawOptions o;
                o.period = intValue (0);
                o.trash = intValue (1);
                o.timeDistortion = juce::roundToInt (value (2) * 1024.0);
                o.stretch = on (3);
                o.removeShort = on (4);
                prtools::claw (p, indices, o);
                break;
            }
            case NoteTool::articulate:
            {
                prtools::ArticulateOptions o;
                o.multiply = juce::roundToInt (value (1) * 100.0);
                o.variation = juce::roundToInt (value (2) * 100.0);
                o.gap = intValue (3);
                o.seed = (std::uint32_t) intValue (4);
                o.useLengths = on (5);
                o.chopChords = on (6);
                prtools::articulate (p, indices, o, beatsPerBar);
                break;
            }
            case NoteTool::riffMachine:
            {
                prtools::RiffOptions o;
                o.bars = intValue (0);
                o.onExistingNotes = on (1);
                o.channel = channel;
                o.start = generatorStart();
                const int stepToggles[] = { 3, 5, 7, 8, 9, 10, 11, 12 };
                for (int s = 0; s < 8; ++s)
                    o.enabled[s] = on ((std::size_t) stepToggles[s]);

                const auto progChoice = intValue (4), chordChoice = intValue (6);
                const auto builtInProg = prtools::builtInRiffProgressionCount(), builtInChord = prtools::builtInRiffChordCount();
                if (progChoice < builtInProg)
                    o.progression = prtools::builtInRiffProgression (progChoice);
                else if (auto s = chosenScore ("Chopping/Riff machine step 1", progChoice - builtInProg + 1, {}))
                    o.progression = *s;
                if (chordChoice < builtInChord)
                    o.chord = prtools::builtInRiffChord (chordChoice);
                else if (auto s = chosenScore ("Chopping/Riff machine step 2", chordChoice - builtInChord + 1, {}))
                    o.chord = *s;

                o.progressionChop.timeMul = dice.progressionMul;
                o.chordChop.timeMul = 4;
                o.arp.direction = dice.arpDirection;
                o.arp.step = dice.arpStep;
                o.levels.velocity = 0.2f;
                o.levels.seed = seed;
                o.flipTime = dice.flipTime;
                o.flipPitch = ! dice.flipTime;
                o.articulation = prtools::articulatePreset ((prtools::ArticulatePreset) dice.articulation);
                o.grooveOptions.start = 0.5f;
                o.fitRoot = intValue (13);
                o.fitScale = intValue (14) + 1;
                o.fit.lowKey = 36;
                o.fit.highKey = 84;
                prtools::riffMachine (p, indices, o, beatsPerBar);
                break;
            }
            case NoteTool::euclidean:
            {
                static const std::int64_t stepTicks[] = { ppq, ppq / 2, ppq / 4, ppq / 8 };
                EuclideanOptions o;
                o.steps = intValue (0);
                o.pulses = intValue (1);
                o.rotation = intValue (2);
                o.stepTicks = stepTicks[juce::jlimit (0, 3, intValue (3))];
                o.start = generatorStart();
                o.length = (std::int64_t) intValue (4) * ppq * juce::jmax (1, beatsPerBar);
                o.key = intValue (5);
                o.velocity = (float) value (6);
                o.gate = (float) value (7);
                o.accent = (float) value (8);
                o.channel = channel;
                euclidean (p, indices, o);
                break;
            }
            case NoteTool::humanize:
            {
                HumanizeOptions o;
                o.timing = intValue (0);
                o.velocity = (float) value (1);
                o.length = (float) value (2);
                o.softerOffbeats = on (3);
                o.seed = seed;
                humanize (p, indices, o);
                break;
            }
            case NoteTool::noteRepeat:
            {
                static const std::int64_t rates[] = { ppq, ppq / 2, ppq / 3, ppq / 4, ppq / 6, ppq / 8 };
                RepeatOptions o;
                o.rate = rates[juce::jlimit (0, 5, intValue (0))];
                o.gate = (float) value (1);
                o.decay = (float) value (2);
                o.pitchStep = intValue (3);
                repeatNotes (p, indices, o);
                break;
            }
            case NoteTool::chordProgression:
            {
                const auto tonic = intValue (0);
                const auto minor = intValue (1) == 1;
                const auto chords = prtools::parseProgression (textValue (3).toStdString(), tonic, minor);

                if (! chords)
                {
                    status = tr ("Type chords as Roman numerals, e.g. I-V-vi-IV or ii7 V7 Imaj7.");
                    break;
                }

                // remove the notes it replaces, then write the chords
                auto valid = indices;
                std::sort (valid.begin(), valid.end());
                for (auto it = valid.rbegin(); it != valid.rend(); ++it)
                    if (*it >= 0 && *it < (int) p.notes.size())
                        p.notes.erase (p.notes.begin() + *it);

                static const std::int64_t lengths[] = { 1, 2, 4, 8 };
                prtools::ChordWriteOptions o;
                o.chordLength = (std::int64_t) ppq * (intValue (6) < 2 ? lengths[intValue (6)] : (std::int64_t) beatsPerBar * (intValue (6) == 2 ? 1 : 2));
                o.octave = intValue (7);
                const auto r = intValue (8) - 1;
                if (r >= 0 && r < (int) rhythmPresets().size())
                    o.rhythm = rhythmPresets()[(std::size_t) r].cells;
                o.bassNote = on (9);
                o.voiceLead = on (10);
                prtools::writeProgression (p, channel, generatorStart(), *chords, o);

                juce::StringArray names;
                for (const auto& c : *chords)
                    names.add (prtools::chordName (c));
                chordNames = tr ("Chords") + ": " + names.joinIntoString ("  ");
                break;
            }
            case NoteTool::patternLibrary:
            {
                const auto choice = [this] (std::size_t c) { return intValue (gp[c]) == 0 ? juce::String() : getCombo (gp[c])->getText(); };
                const auto sectionIndex = intValue (gp[1]), roleIndex = intValue (gp[2]);
                const auto section = sectionIndex == 0 ? juce::String() : patternlib::sections()[sectionIndex - 1];
                const auto role = roleIndex == 0 ? juce::String() : patternlib::roles()[roleIndex - 1];
                picked = patternlib::pickFor (choice (0), section, role, seed);
                if (picked == nullptr)
                {
                    status = patternlib::bundledFolder().isDirectory() ? tr ("No pattern fits these choices.")
                                                                       : tr ("The pattern library was not found next to LYNX.");
                    break;
                }
                removeIndices (p);
                static const int barCounts[] = { 1, 2, 4, 8 };
                patternlib::insert (p, channel, *picked, intValue (gp[3]) - 1, intValue (gp[4]) == 1 ? 2 : 1, generatorStart(),
                                    barCounts[juce::jlimit (0, 3, intValue (gp[5]))]);
                chordNames = juce::String::fromUTF8 ((picked->set + ": " + picked->name).c_str()) + "  (" + tr (juce::String (picked->role)) + ", "
                             + juce::String (picked->bars) + " " + tr (picked->bars == 1 ? "bar" : "bars") + ")";
                break;
            }
            case NoteTool::chordDice:
            {
                const auto bars = diceBars();
                const auto tonic = intValue (gp[0]);
                const bool minor = intValue (gp[1]) == 1;
                removeIndices (p);
                const auto low = 12 * (intValue (gp[5]) + 1);
                prtools::dice::PlaceOptions o;
                o.channel = channel;
                o.start = generatorStart();
                o.barLength = (std::int64_t) ppq * juce::jmax (1, beatsPerBar) * (intValue (gp[6]) == 1 ? 2 : 1);
                for (const auto& n : prtools::dice::placeChords (bars, prtools::dice::ladder (low, low + 36), tonic, minor, o))
                    p.notes.push_back (n);
                chordNames = tr ("Chords") + ": " + juce::String (prtools::dice::chordNames (bars, tonic, minor));
                if (committing)
                {
                    p.toolSettings = prtools::riff::withSection (p.toolSettings, "chords", prtools::dice::toText (bars));
                    p.toolSettings = prtools::riff::withSection (p.toolSettings, "dice", "seed=" + std::to_string (seed) + "\n");
                }
                break;
            }
            case NoteTool::melodyDice:
            {
                static constexpr std::array<int, 7> majorSteps { 0, 2, 4, 5, 7, 9, 11 }, minorSteps { 0, 2, 3, 5, 7, 8, 10 };
                const auto tonic = intValue (gp[0]);
                const bool minor = intValue (gp[1]) == 1;
                const auto low = 12 * (intValue (gp[3]) + 1);
                const auto keys = prtools::dice::ladder (low, low + 12 * intValue (gp[4]), tonic, minor);
                prtools::dice::MelodyOptions o;
                o.seed = seed;
                o.resolution = intValue (gp[2]) == 0 ? 4 : intValue (gp[2]) == 1 ? 2 : 1;
                o.landing = (prtools::dice::Landing) intValue (gp[7]);
                // the home: the chosen degree of the key, in the ladder's lowest octave
                const auto degree = (std::size_t) juce::jlimit (0, 6, intValue (gp[5]) - 1);
                const auto homeKey = low + (tonic + (minor ? minorSteps : majorSteps)[degree]) % 12;
                o.home = (int) (std::lower_bound (keys.begin(), keys.end(), homeKey) - keys.begin());
                o.place.channel = channel;
                o.place.start = generatorStart();
                o.place.barLength = (std::int64_t) ppq * juce::jmax (1, beatsPerBar);
                const auto lane = prtools::dice::barsFromText (prtools::riff::section (p.toolSettings, "chords"));
                std::vector<std::vector<int>> tones;
                if (on (gp[6]) && lane.size() >= 4)
                    tones = prtools::dice::chordTones (lane, keys, tonic, minor);
                removeIndices (p);
                for (const auto& n : prtools::dice::rollMelody (keys, o, tones.empty() ? nullptr : &tones))
                    p.notes.push_back (n);
                if (on (gp[6]) && lane.size() < 4)
                    status = tr ("No chord lane on this pattern yet: roll Chord dice first, or switch off Follow the chord lane.");
                if (committing)
                    p.toolSettings = prtools::riff::withSection (p.toolSettings, "melody", "seed=" + std::to_string (seed) + "\n");
                break;
            }
            case NoteTool::riffStages:
            {
                const auto s = riffSettings();
                const auto home = 12 * 5 + (keyScale() > 0 ? keyRoot() : 0); // the pitch mirror's centre: the key's home, C4..B4
                prtools::riff::run (p, indices, s, home, keyRoot(), keyScale(), juce::jmax<std::int64_t> (1, grid));
                if (committing)
                    p.toolSettings = prtools::riff::withSection (p.toolSettings, "riff", prtools::riff::toText (s));
                break;
            }
        }

        p.sortNotes();
    }

    // --- the GravePain tools' helpers -----------------------------------------------------------

    std::string currentToolSettings() const
    {
        const auto& song = engine.getSong();
        return patternIndex < (int) song.patterns.size() ? song.patterns[(std::size_t) patternIndex].toolSettings : std::string();
    }

    static int savedInt (const std::string& section, const std::string& key, int otherwise)
    {
        for (const auto& line : juce::StringArray::fromLines (juce::String::fromUTF8 (section.c_str())))
            if (line.startsWith (juce::String (key) + "="))
                return line.fromFirstOccurrenceOf ("=", false, false).getIntValue();
        return otherwise;
    }

    // the notes the tool replaces (the selection, or the channel's notes) come out first
    void removeIndices (Pattern& p) const
    {
        auto valid = indices;
        std::sort (valid.begin(), valid.end());
        valid.erase (std::unique (valid.begin(), valid.end()), valid.end());
        for (auto it = valid.rbegin(); it != valid.rend(); ++it)
            if (*it >= 0 && *it < (int) p.notes.size())
                p.notes.erase (p.notes.begin() + *it);
    }

    std::vector<prtools::dice::ChordBar> diceBars() const
    {
        const bool minor = intValue (gp[1]) == 1;
        const auto choice = intValue (gp[2]) - 1;
        const auto& majors = prtools::dice::progressions (false);
        const auto& minors = prtools::dice::progressions (true);
        const prtools::dice::Progression& prog = choice < 0 ? prtools::dice::pickProgression (seed, minor)
                                                 : choice < (int) majors.size() ? majors[(std::size_t) choice]
                                                                                : minors[(std::size_t) juce::jlimit (0, (int) minors.size() - 1, choice - (int) majors.size())];
        auto bars = prtools::dice::barsOf (prog);
        for (auto& b : bars)
        {
            b.shape = (prtools::dice::Shape) intValue (gp[3]);
            b.inversion = intValue (gp[4]);
            if (prog.minor != minor) // a loop of the other mode: on the relative key, so it stays in the lane's notes
            {
                b.tonic = (intValue (gp[0]) + (prog.minor ? 9 : 3)) % 12;
                b.minor = prog.minor ? 1 : 0;
            }
        }
        return bars;
    }

    prtools::riff::RiffSettings riffSettings() const
    {
        prtools::riff::RiffSettings s;
        s.seed = seed;
        s.mirrorOn = on (gp[0]);
        s.mirrorTime = on (gp[1]);
        s.mirrorKeepStarts = on (gp[2]);
        s.mirrorPitch = on (gp[3]);
        s.levelsOn = on (gp[4]);
        s.levelsReset = on (gp[5]);
        s.levelsBipolar = on (gp[6]);
        s.levelsAmount = (float) value (gp[7]);
        s.levelsLength = (float) value (gp[8]);
        s.artOn = on (gp[9]);
        s.artChop = on (gp[10]);
        s.artMultiply = (float) value (gp[11]);
        s.artGap = (float) value (gp[12]);
        s.artVariation = (float) value (gp[13]);
        s.grooveOn = on (gp[14]);
        s.groove = (prtools::riff::Groove) intValue (gp[15]);
        s.grooveTiming = (float) value (gp[16]);
        return s;
    }

    void extraAction()
    {
        if (tool == NoteTool::patternLibrary && picked != nullptr)
            patternlib::passOver (*picked); // rolled past: picked a little less often from now on
        ++seed;
        if (tool == NoteTool::chordDice)
            setValue (gp[2], 0); // a new roll is the dice's own pick

        if (tool == NoteTool::riffMachine)
        {
            juce::Random r ((juce::int64) seed * 7919);
            const juce::ScopedValueSetter<bool> quiet (updating, true);
            setValue (4, r.nextInt (juce::jmax (1, getCombo (4)->getNumItems())));
            setValue (6, r.nextInt (juce::jmax (1, getCombo (6)->getNumItems())));
            setValue (7, r.nextBool() ? 1 : 0);
            setValue (8, r.nextBool() ? 1 : 0);
            setValue (9, r.nextInt (3) == 0 ? 1 : 0);
            setValue (11, r.nextBool() ? 1 : 0);
            dice.progressionMul = 3 + r.nextInt (3);
            dice.arpDirection = r.nextInt (4);
            static const std::int64_t arpSteps[] = { ppq / 2, ppq / 4, ppq / 4, ppq / 8 };
            dice.arpStep = arpSteps[r.nextInt (4)];
            dice.flipTime = r.nextBool();
            dice.articulation = r.nextInt (3);
        }
        else if (tool == NoteTool::chordProgression)
        {
            const auto chords = prtools::generateProgression (intValue (0), intValue (1) == 1, intValue (4), (float) value (5), seed);
            setText (3, juce::String::fromUTF8 (prtools::toNumerals (chords, intValue (0)).c_str()));
            setValue (2, 0);
        }

        preview();
    }

    juce::ComboBox* getCombo (std::size_t i) const { return dynamic_cast<juce::ComboBox*> (editors[i].get()); }

    void updateInfo()
    {
        juce::String text;

        if (noteToolGenerates (tool))
            text = indices.empty() ? tr ("Writes new notes on the selected channel. Accept keeps them as one undo step.")
                                   : tr ("Replaces {0} notes of the selected channel. Accept keeps the result as one undo step.",
                                         { juce::String ((int) indices.size()) });
        else
            text = tr ("{0} notes. Changes are heard and seen right away; Accept keeps them as one undo step.",
                       { juce::String ((int) indices.size()) });

        if (status.isNotEmpty())
            text = status;
        else if (chordNames.isNotEmpty())
            text << "\n" << chordNames;

        info.setText (text, juce::dontSendNotification);
    }

    bool patternStillThere() const { return patternIndex < (int) engine.getSong().patterns.size(); }

    void restore()
    {
        if (patternStillThere())
            engine.editSong ([this] (Song& s) { s.patterns[(std::size_t) patternIndex].notes = originals; }, false);
    }

    void updateEnablement()
    {
        const auto enable = [this] (std::size_t c, bool yes) {
            editors[c]->setEnabled (yes);
            labels[c]->setEnabled (yes);
        };

        if (tool == NoteTool::lfo)
            for (std::size_t c = 9; c <= 12; ++c)
                enable (c, on (8));
        else if (tool == NoteTool::quantize)
            enable (1, intValue (0) == 0); // the swing applies to the grid template
        else if (tool == NoteTool::riffMachine)
        {
            enable (4, on (3));
            enable (6, on (5));
            enable (13, on (12));
            enable (14, on (12));
        }
    }

    void preview()
    {
        updateEnablement();

        if (patternStillThere())
            engine.editSong ([this] (Song& s) {
                auto& p = s.patterns[(std::size_t) patternIndex];
                p.notes = originals;
                apply (p);
            }, false);

        updateInfo();
    }

    void accept()
    {
        if (patternStillThere())
        {
            restore();
            const juce::ScopedValueSetter<bool> saving (committing, true); // the tools' settings are saved with the pattern
            engine.editSong ([this] (Song& s) { apply (s.patterns[(std::size_t) patternIndex]); });
            if (tool == NoteTool::patternLibrary && picked != nullptr)
                patternlib::keep (*picked); // kept: picked more often
        }

        accepted = true;
        close();
    }

    void close()
    {
        if (! accepted)
            restore();
        accepted = true; // nothing left to undo in the destructor

        if (onFinished)
            onFinished();

        closeToolWindowOf (*this);
    }

    AudioEngine& engine;
    NoteTool tool;
    std::vector<int> indices;
    std::int64_t grid;
    int patternIndex = 0, channel = 0, beatsPerBar = 4;
    double bpm = 130.0;
    std::vector<Note> originals;
    std::uint32_t seed = 1;
    bool accepted = false, updating = false, committing = false;
    std::vector<std::size_t> gp; // the GravePain tools' control indices, in the order they were added
    const patterns::PatternRecord* picked = nullptr; // the pattern library's current pick
    std::size_t levelsAt = 0;
    juce::String status, chordNames;
    mutable std::map<juce::String, prtools::Score> scoreCache;

    struct Dice // the riff machine's hidden choices, changed by "Throw dice"
    {
        int progressionMul = 4, arpDirection = 0, articulation = 0;
        std::int64_t arpStep = ppq / 4;
        bool flipTime = false;
    } dice;

    std::vector<Control> controls;
    std::vector<std::unique_ptr<juce::Label>> labels;
    std::vector<std::unique_ptr<juce::Component>> editors;
    juce::Label title, info;
    juce::TextButton acceptButton { tr ("Accept") }, resetButton { tr ("Reset") }, cancelButton { tr ("Cancel") };
    juce::TextButton extraButton;
};
} // namespace

const char* noteToolTitle (NoteTool tool)
{
    switch (tool)
    {
        case NoteTool::strum: return TR_NOOP ("Strum");
        case NoteTool::arpeggiate: return TR_NOOP ("Arpeggiate");
        case NoteTool::flip: return TR_NOOP ("Flip");
        case NoteTool::randomise: return TR_NOOP ("Randomise");
        case NoteTool::scaleLevels: return TR_NOOP ("Scale levels");
        case NoteTool::limit: return TR_NOOP ("Limit");
        case NoteTool::quantize: return TR_NOOP ("Quantize");
        case NoteTool::chop: return TR_NOOP ("Chop");
        case NoteTool::flam: return TR_NOOP ("Flam");
        case NoteTool::lfo: return TR_NOOP ("LFO");
        case NoteTool::claw: return TR_NOOP ("Claw machine");
        case NoteTool::articulate: return TR_NOOP ("Articulate");
        case NoteTool::riffMachine: return TR_NOOP ("Riff machine");
        case NoteTool::chordProgression: return TR_NOOP ("Chord progression");
        case NoteTool::euclidean: return TR_NOOP ("Euclidean rhythm");
        case NoteTool::humanize: return TR_NOOP ("Humanize");
        case NoteTool::noteRepeat: return TR_NOOP ("Note repeater");
        case NoteTool::patternLibrary: return TR_NOOP ("Pattern from the library");
        case NoteTool::chordDice: return TR_NOOP ("Chord dice");
        case NoteTool::melodyDice: return TR_NOOP ("Melody dice");
        case NoteTool::riffStages: return TR_NOOP ("Riff stages");
    }
    return "";
}

void setNoteToolKey (int root, int scale) noexcept
{
    keyRoot() = ((root % 12) + 12) % 12;
    keyScale() = scale < 0 ? 0 : scale;
}

bool noteToolGenerates (NoteTool tool) noexcept
{
    return tool == NoteTool::riffMachine || tool == NoteTool::chordProgression || tool == NoteTool::euclidean
           || tool == NoteTool::patternLibrary || tool == NoteTool::chordDice || tool == NoteTool::melodyDice;
}

std::unique_ptr<juce::Component> makeNoteToolPanel (AudioEngine& engine, NoteTool tool, std::vector<int> indices, std::int64_t grid)
{
    return std::make_unique<NoteToolPanel> (engine, tool, std::move (indices), grid);
}

void showNoteTool (AudioEngine& engine, NoteTool tool, std::vector<int> indices, std::int64_t grid, std::function<void()> onFinished)
{
    if (indices.empty() && ! noteToolGenerates (tool))
        return;

    auto panel = std::make_unique<NoteToolPanel> (engine, tool, std::move (indices), grid);
    panel->onFinished = std::move (onFinished);
    openToolWindow (std::move (panel), tr (noteToolTitle (tool)), "notetool");
}
} // namespace lynx
