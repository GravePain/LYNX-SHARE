#pragma once

// A Novation Launchpad (X, Mini MK3, Pro MK3) driven by LYNX (GravePain port, step 6): opens its MIDI output, puts it
// in Programmer Mode, reads its pads from the engine's capture queue, applies the surface's actions (core/Launchpad.h)
// to the project and repaints the LEDs. With no Launchpad plugged in it does nothing, quietly. Message thread.

#include <memory>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_events/juce_events.h>

#include "core/Launchpad.h"
#include "engine/AudioEngine.h"

namespace lynx
{
class LaunchpadController final : private juce::Timer
{
public:
    explicit LaunchpadController (AudioEngine& engine);
    ~LaunchpadController() override;

    bool isEnabled() const noexcept { return enabled; }
    void setEnabled (bool on); // remembered in the settings folder
    bool isConnected() const noexcept { return output != nullptr; }
    juce::String deviceName() const;
    void lookAgain();                    // after plugging one in, or another program let go of it
    void showLights();                   // the light show now (any pad ends it)
    void setKey (int root, int scale);   // the notes mode's key (the Piano roll's scale box)

private:
    void timerCallback() override;
    void connect();
    void disconnect();
    void send (const std::vector<std::uint8_t>& sysex);
    void apply (const launchpad::Action& a);
    void repaintPads (float now);

    AudioEngine& engine;
    bool enabled = false;
    std::unique_ptr<juce::MidiOutput> output;
    const launchpad::ModelInfo* model = nullptr;
    launchpad::Leds leds;
    launchpad::State state;
    launchpad::LightShow show;
    launchpad::Now now;
    bool showing = false;
    int lookCountdown = 0;
    double started = 0.0;
};
} // namespace lynx
