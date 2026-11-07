#include "ui/LaunchpadController.h"

#include <cmath>

#include "platform/Paths.h"

namespace lynx
{
namespace
{
juce::File settingsFile() { return platform::settingsFile ("launchpad.txt"); }
constexpr int frameMs = 33, lookEveryFrames = 90; // 30 frames a second; look for a device every 3 s
} // namespace

LaunchpadController::LaunchpadController (AudioEngine& e) : engine (e)
{
    enabled = settingsFile().loadFileAsString().trim() == "on";
    started = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    if (enabled)
    {
        connect();
        startTimer (frameMs);
    }
}

LaunchpadController::~LaunchpadController()
{
    stopTimer();
    disconnect();
}

void LaunchpadController::setEnabled (bool on)
{
    enabled = on;
    settingsFile().replaceWithText (on ? "on" : "off");
    if (on)
    {
        connect();
        startTimer (frameMs);
    }
    else
    {
        stopTimer();
        disconnect();
    }
}

juce::String LaunchpadController::deviceName() const
{
    return model != nullptr ? juce::String (model->name) : juce::String();
}

void LaunchpadController::lookAgain()
{
    disconnect();
    if (enabled)
        connect();
}

void LaunchpadController::showLights()
{
    state.lightShow = true;
}

void LaunchpadController::setKey (int root, int scale)
{
    state.key = { root, scale > 0 ? scale : scales::major };
}

void LaunchpadController::connect()
{
    if (output != nullptr)
        return;
    for (const auto& device : juce::MidiOutput::getAvailableDevices())
        if (const auto* m = launchpad::modelOfPort (device.name.toStdString()))
            if (auto out = juce::MidiOutput::openDevice (device.identifier))
            {
                output = std::move (out);
                model = m;
                send (launchpad::programmerMode (model->model, true));
                leds.clear();
                send (leds.flush (model->model));
                engine.setLaunchpadCapture (true);
                return;
            }
}

void LaunchpadController::disconnect()
{
    engine.setLaunchpadCapture (false);
    if (output != nullptr && model != nullptr)
    {
        leds.clear();
        send (leds.flush (model->model));
        send (launchpad::programmerMode (model->model, false)); // handed back the way it was found
    }
    output.reset();
    model = nullptr;
}

void LaunchpadController::send (const std::vector<std::uint8_t>& sysex)
{
    // (juce's sysex messages are without the F0 / F7 framing)
    if (output != nullptr && sysex.size() > 2)
        output->sendMessageNow (juce::MidiMessage::createSysExMessage (sysex.data() + 1, (int) sysex.size() - 2));
}

void LaunchpadController::apply (const launchpad::Action& a)
{
    using A = launchpad::Action;
    switch (a.type)
    {
        case A::playPattern:
            engine.editSong ([index = a.a] (Song& s) {
                s.currentPattern = index;
                s.mode = PlayMode::pattern;
            }, false);
            if (! engine.getTransport().playing.load())
                engine.play();
            break;
        case A::selectPattern:
            engine.editSong ([index = a.a] (Song& s) { s.currentPattern = index; }, false);
            break;
        case A::togglePlay:
            if (engine.getTransport().playing.load())
                engine.stop();
            else
                engine.play();
            break;
        case A::noteOn:
            if (a.a >= 0 && a.a < 128)
            {
                now.held[(std::size_t) a.a] = true;
                engine.injectMidiInput (juce::MidiMessage::noteOn (1, a.a, (juce::uint8) std::clamp (a.b, 1, 127)));
            }
            break;
        case A::noteOff:
            if (a.a >= 0 && a.a < 128)
            {
                now.held[(std::size_t) a.a] = false;
                engine.injectMidiInput (juce::MidiMessage::noteOff (1, a.a));
            }
            break;
        case A::toggleStep:
            engine.editSong ([ch = a.a, step = a.b] (Song& s) {
                if (s.currentPattern >= 0 && s.currentPattern < (int) s.patterns.size())
                    s.patterns[(std::size_t) s.currentPattern].toggleStep (ch, step);
            });
            break;
        case A::selectChannel:
            engine.setSelectedChannel (a.a);
            break;
        case A::showLights:
        case A::setMode:
        case A::page:
        case A::none:
            break;
    }
}

void LaunchpadController::timerCallback()
{
    if (output == nullptr)
    {
        if (--lookCountdown <= 0)
        {
            lookCountdown = lookEveryFrames;
            connect();
        }
        return;
    }
    const auto& song = engine.getSong();
    const int selected = engine.getSelectedChannel();
    AudioEngine::LaunchpadMessage m;
    while (engine.popLaunchpadMessage (m))
    {
        const int kind = m.status & 0xF0;
        const bool down = (kind == 0x90 || kind == 0xB0) && m.data2 > 0;
        const bool up = kind == 0x80 || ((kind == 0x90 || kind == 0xB0) && m.data2 == 0);
        const auto pad = launchpad::decode (m.data1);
        if (down)
            for (const auto& a : launchpad::press (song, state, pad, m.data2, selected))
                apply (a);
        else if (up)
            for (const auto& a : launchpad::release (song, state, pad, selected))
                apply (a);
    }
    repaintPads ((float) (juce::Time::getMillisecondCounterHiRes() / 1000.0 - started));
}

void LaunchpadController::repaintPads (float t)
{
    if (state.lightShow)
    {
        if (! showing)
        {
            showing = true;
            show.reset (t);
        }
        launchpad::LightShow::Music music;
        const auto& transport = engine.getTransport();
        music.playing = transport.playing.load();
        if (music.playing)
        {
            const double beats = transport.positionTicks.load() / (double) ppq;
            music.beat = (int) std::floor (beats);
            music.beatPhase = (float) (beats - std::floor (beats));
            music.levels.fill (0.6f);
        }
        const auto& canvas = show.render (t, music);
        for (int y = 0; y < launchpad::size; ++y)
            for (int x = 0; x <= launchpad::size; ++x)
                leds.set (x, y, canvas[(std::size_t) x][(std::size_t) y + 1]);
        for (int x = 0; x < launchpad::size; ++x)
            leds.setTop (x, canvas[(std::size_t) x][0]);
        send (leds.flush (model->model));
        return;
    }
    if (showing)
    {
        showing = false;
        leds.clear();
    }
    const auto& song = engine.getSong();
    const auto& transport = engine.getTransport();
    now.playing = transport.playing.load();
    now.playingPattern = transport.songMode.load() ? -1 : song.currentPattern;
    now.selectedChannel = engine.getSelectedChannel();
    now.pulse = std::fmod (t * 2.0f, 1.0f);
    now.playStep = now.playing ? (int) (transport.positionTicks.load() / (double) ticksPerStep) : -1;
    if (now.playStep >= 0 && song.currentPattern >= 0 && song.currentPattern < (int) song.patterns.size())
    {
        const auto len = std::max<std::int64_t> (ticksPerStep, song.patterns[(std::size_t) song.currentPattern].effectiveLength (std::max (1, song.beatsPerBar)));
        now.playStep %= (int) std::max<std::int64_t> (1, len / ticksPerStep);
    }
    const auto frame = launchpad::paint (song, state, now);
    for (int y = 0; y < launchpad::size; ++y)
        for (int x = 0; x <= launchpad::size; ++x)
            leds.set (x, y, launchpad::ledColour (frame.c[(std::size_t) x][(std::size_t) y + 1]));
    for (int x = 0; x < launchpad::size; ++x)
        leds.setTop (x, launchpad::ledColour (frame.c[(std::size_t) x][0]));
    send (leds.flush (model->model));
}
} // namespace lynx
