#pragma once

// Novation Launchpad support (GravePain port, step 6; our own code, from GravePain's LaunchpadX, LaunchpadLightShow,
// LaunchpadGrid and LedColour). No JUCE: the device protocol, the LED colours, the light show and the surface's modes
// are pure logic over the Song, tested in tests/; src/ui/LaunchpadController opens the ports and applies the actions.
//
// The device is put into Programmer Mode: a pad is row * 10 + column, row 1 at the BOTTOM, column 1 on the left
// (11 bottom-left, 88 top-right), the scene buttons down the right are the nines (19 … 89) and the round buttons along
// the top 91 … 98. Colours go as red, green, blue (0..127) in one system-exclusive message, only the pads that changed.
//
// The surface's modes (the top buttons):
//   1 Patterns: the 64 pads are the project's patterns (page by page, scene buttons pick the page); a press makes the
//     pattern current and plays it in pattern mode; the playing one pulses.
//   2 Notes:    the pads play the selected channel in the key (core/Scales' in-key grid: columns step the scale,
//     rows three degrees apart); the root pads are lit; scene buttons move an octave up / down.
//   3 Steps:    the Channel Rack's step sequencer: rows are channels (scene buttons page them), columns 8 steps
//     (top buttons 5 / 6 move half a bar); a press toggles the step; the playing step is marked.
//   4 Light show (any press ends it); 7 play / stop.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Scales.h"
#include "core/Song.h"

namespace lynx::launchpad
{
// --- the device --------------------------------------------------------------------------------------
enum class Model { x, miniMk3, proMk3 };
struct ModelInfo
{
    Model model;
    const char* name;              // "Launchpad X"
    std::uint8_t id;               // the sysex device byte (Novation 00 20 29, 02, id)
    std::vector<std::string> ports; // words a port name has (any one of them): "lpx", "launchpad x"
};
const std::vector<ModelInfo>& models();
// The model a MIDI port name belongs to (nullptr: not a Launchpad this supports). The DAW port (MIDIIN2 / "DAW") is
// not the one to drive.
const ModelInfo* modelOfPort (const std::string& portName);

inline constexpr int size = 8;
enum class PadKind { none, grid, scene, top };
struct Pad
{
    PadKind kind = PadKind::none;
    int x = 0, y = 0; // grid: column 0 left, row 0 at the TOP; scene: y; top: x
};

int padNumber (int column, int row) noexcept; // row 0 = top; column 8 = the scene button
int topNumber (int column) noexcept;          // 91 … 98
Pad decode (int number) noexcept;

std::vector<std::uint8_t> programmerMode (Model model, bool on);

struct Rgb
{
    std::uint8_t r = 0, g = 0, b = 0; // 0..127 (the device's range)
    bool operator== (const Rgb&) const = default;
};
struct Colour
{
    float r = 0, g = 0, b = 0; // 0..1, as a screen shows it
};
Rgb toDevice (Colour c) noexcept;

// The pads as the device shows them: what changed since the last flush goes out as one message.
class Leds
{
public:
    void set (int column, int row, Colour c);   // column 8 = the scene button; row 0 = top
    void setTop (int column, Colour c);
    void clear();                                // every LED dark, sent whatever was shown
    std::vector<std::uint8_t> flush (Model model); // empty when nothing changed
private:
    std::array<std::array<Rgb, size>, size + 1> shown {};
    std::array<Rgb, size> topShown {};
    std::vector<std::uint8_t> pending;
    void queue (int number, Rgb c);
};

// A screen colour redrawn for an LED: what is dark on screen is off, colours go deep, dim ones dimmer.
Colour ledColour (Colour screen) noexcept;

// --- the light show -------------------------------------------------------------------------------------
// Seven effects (plasma, ripples, spiral, rain, fire, scrolling text, equaliser) crossfading into one another, each
// in one palette at a time, over a 9 x 9 canvas: row 0 the top buttons, rows 1-8 the grid top to bottom, column 8
// the scene buttons. With music it follows the beat and the channels' levels; idle it keeps its own 120 BPM pulse.
// Seeded: the same seed and the same frames draw the same show.
class LightShow
{
public:
    static constexpr int width = 9, height = 9;
    enum class Effect { plasma, ripples, spiral, rain, fire, text, equaliser };
    struct Music
    {
        bool playing = false;
        float beatPhase = 0;   // 0..1 through the beat
        int beat = 0;          // changes once a beat
        std::array<float, 8> levels {}; // 0..1 per meter
    };
    using Canvas = std::array<std::array<Colour, height>, width>; // [x][y]

    explicit LightShow (std::uint32_t seed = 1);
    ~LightShow();
    LightShow (const LightShow&) = delete;
    LightShow& operator= (const LightShow&) = delete;

    float effectSeconds = 24.0f, fadeSeconds = 2.5f, gamma = 1.8f;
    std::string message = "LYNX";

    void reset (float now);
    const Canvas& render (float now, const Music& music);
    Effect current() const noexcept;

private:
    struct Impl;
    Impl* impl;
};

// --- the surface ----------------------------------------------------------------------------------------
enum class Mode { patterns, notes, steps };

struct State
{
    Mode mode = Mode::patterns;
    int patternPage = 0;      // patterns 64 * page …
    int octave = 3;           // notes: the bottom-left pad is the key's root in this octave (C3 = 48)
    scales::Key key { 0, scales::major };
    int channelPage = 0;      // steps: channels 8 * page …
    int stepPage = 0;         // steps: steps 8 * page …
    bool lightShow = false;
};

// What the device shows now: the 9 x 9 canvas of the light show's layout (row 0 = top buttons).
struct Frame
{
    std::array<std::array<Colour, 9>, 9> c {}; // [x][y]
};

struct Now
{
    int playingPattern = -1; // the pattern sounding in pattern mode (−1: none / song mode)
    int playStep = -1;       // the step the pattern is at (steps mode marker), −1 when stopped
    bool playing = false;
    float pulse = 0.0f;      // 0..1, for the playing pad's pulse
    int selectedChannel = 0;
    std::array<bool, 128> held {}; // notes mode: keys held on the pads
};

Frame paint (const Song& song, const State& state, const Now& now);

// What a press asks for; the controller applies it to the engine (one undo step for an edit).
struct Action
{
    enum Type { none, setMode, showLights, selectPattern, playPattern, togglePlay, noteOn, noteOff, toggleStep, page, selectChannel };
    Type type = none;
    int a = 0, b = 0;   // selectPattern / playPattern: the pattern; noteOn / noteOff: key, velocity (1..127);
                        // toggleStep: channel, step; setMode: the mode; selectChannel: the channel
};
std::vector<Action> press (const Song& song, State& state, Pad pad, int velocity, int selectedChannel);
std::vector<Action> release (const Song& song, State& state, Pad pad, int selectedChannel);
// The note a notes-mode pad plays.
int noteOfPad (const State& state, int x, int y) noexcept;
} // namespace lynx::launchpad
