// GravePain port, step 6: Launchpad support (core/Launchpad). Ported from GravePain's LedColourTests, with the
// device's numbering, the LED messages, the light show and the surface's modes.

#include "core/Launchpad.h"

namespace gp_lp
{
using namespace lynx::launchpad;
inline float value (Colour c) { return std::max ({ c.r, c.g, c.b }); }
inline float saturation (Colour c)
{
    const float mx = value (c), mn = std::min ({ c.r, c.g, c.b });
    return mx > 0 ? (mx - mn) / mx : 0;
}
inline lynx::Song songWith (int patterns, int channels)
{
    lynx::Song s;
    s.patterns.resize ((std::size_t) patterns);
    s.channels.resize ((std::size_t) channels);
    s.currentPattern = 0;
    return s;
}
} // namespace gp_lp

TEST (gp_launchpad_numbering)
{
    using namespace gp_lp;
    CHECK (padNumber (0, 7) == 11 && padNumber (7, 0) == 88); // bottom-left, top-right
    CHECK (padNumber (8, 0) == 89 && topNumber (0) == 91 && topNumber (7) == 98);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x <= 8; ++x)
        {
            const auto p = decode (padNumber (x, y));
            CHECK (p.kind == (x == 8 ? PadKind::scene : PadKind::grid) && p.y == y && (x == 8 || p.x == x));
        }
    CHECK (decode (95).kind == PadKind::top && decode (95).x == 4);
    CHECK (decode (99).kind == PadKind::none && decode (10).kind == PadKind::none && decode (5).kind == PadKind::none);
    CHECK (modelOfPort ("LPX MIDI") != nullptr && modelOfPort ("LPX MIDI")->model == Model::x);
    CHECK (modelOfPort ("MIDIIN2 (LPX MIDI)") == nullptr);  // the DAW port is not the one to drive
    CHECK (modelOfPort ("Launchpad Mini MK3 LPMiniMK3 MIDI")->model == Model::miniMk3);
    CHECK (modelOfPort ("Some Keyboard") == nullptr);
    const auto mode = programmerMode (Model::x, true);
    CHECK ((mode == std::vector<std::uint8_t> { 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x0E, 0x01, 0xF7 }));
}

TEST (gp_launchpad_leds_send_only_changes)
{
    using namespace gp_lp;
    Leds leds;
    leds.clear();
    const auto all = leds.flush (Model::x);
    CHECK (all.size() == 6 + 1 + (72 + 8) * 5 + 1); // every pad, scene and top button, dark
    CHECK (leds.flush (Model::x).empty());          // nothing changed: nothing sent
    leds.set (0, 7, { 1.0f, 0.0f, 0.5f });
    const auto one = leds.flush (Model::x);
    CHECK ((one == std::vector<std::uint8_t> { 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x03, 0x03, 11, 127, 0, 64, 0xF7 }));
    leds.set (0, 7, { 1.0f, 0.0f, 0.5f });
    CHECK (leds.flush (Model::x).empty());
    leds.setTop (2, { 0, 1, 0 });
    CHECK (leds.flush (Model::miniMk3)[5] == 0x0D);
}

TEST (gp_launchpad_led_colours)
{
    using namespace gp_lp;
    CHECK (value (ledColour ({ 0.11f, 0.11f, 0.14f })) == 0.0f); // an empty pad is off
    CHECK (value (ledColour ({ 0.13f, 0.14f, 0.18f })) == 0.0f);
    CHECK (value (ledColour ({ 0.07f, 0.07f, 0.08f })) == 0.0f);
    const Colour red { 0.92f, 0.36f, 0.36f };
    const auto led = ledColour (red);
    CHECK (saturation (led) > 0.9f && value (led) > 0.85f); // red, not pink, and bright
    CHECK (led.r > led.g * 5 && std::abs (led.g - led.b) < 0.02f); // the same hue
    const Colour lit { 0.36f, 0.55f, 0.95f }, dim { lit.r * 0.34f, lit.g * 0.34f, lit.b * 0.34f };
    const float loaded = value (ledColour (dim)), playing = value (ledColour (lit));
    CHECK (loaded > 0.03f && loaded < playing * 0.25f);
    const auto white = ledColour ({ 0.86f, 0.87f, 0.92f });
    CHECK (saturation (white) < 0.1f && value (white) > 0.8f); // white stays white
    CHECK (toDevice ({ 1.0f, 0.5f, 2.0f }) == (Rgb { 127, 64, 127 }));
}

TEST (gp_launchpad_light_show)
{
    using namespace gp_lp;
    LightShow a (7), b (7);
    a.effectSeconds = b.effectSeconds = 2.0f;
    a.fadeSeconds = b.fadeSeconds = 0.5f;
    a.reset (0.0f);
    b.reset (0.0f);
    LightShow::Music music;
    bool lit = false, same = true, finite = true;
    std::set<int> effects;
    for (int frame = 0; frame < 40 * 20; ++frame) // 20 s at 40 frames a second
    {
        const float t = (float) frame / 40.0f;
        music.playing = frame > 400;
        music.beat = frame / 20;
        music.beatPhase = (float) (frame % 20) / 20.0f;
        music.levels.fill (0.7f);
        const auto& ca = a.render (t, music);
        const auto& cb = b.render (t, music);
        effects.insert ((int) a.current());
        for (int x = 0; x < 9; ++x)
            for (int y = 0; y < 9; ++y)
            {
                const auto& c = ca[(std::size_t) x][(std::size_t) y];
                lit = lit || value (c) > 0.05f;
                finite = finite && std::isfinite (c.r) && std::isfinite (c.g) && std::isfinite (c.b) && value (c) <= 1.0f && c.r >= 0;
                same = same && c.r == cb[(std::size_t) x][(std::size_t) y].r && c.g == cb[(std::size_t) x][(std::size_t) y].g;
            }
    }
    CHECK (lit && finite);
    CHECK (same);                // seeded: the same seed, the same show
    CHECK (effects.size() >= 5); // the effects take their turns
}

TEST (gp_launchpad_patterns_mode)
{
    using namespace gp_lp;
    auto song = songWith (70, 4);
    song.currentPattern = 3;
    State state;
    Now now;
    auto f = paint (song, state, now);
    CHECK (value (f.c[3][1]) > value (f.c[2][1]));        // the current pattern is brighter
    CHECK (value (f.c[0][1]) > 0.0f && value (f.c[0][2]) > 0.0f);
    CHECK (value (f.c[8][1]) > 0.0f && value (f.c[8][2]) > 0.0f && value (f.c[8][3]) == 0.0f); // two pages
    auto actions = press (song, state, decode (padNumber (5, 1)), 100, 0);
    CHECK (actions.size() == 1 && actions[0].type == Action::playPattern && actions[0].a == 13);
    press (song, state, decode (padNumber (8, 1)), 100, 0); // the second page
    CHECK (state.patternPage == 1);
    actions = press (song, state, decode (padNumber (5, 0)), 100, 0);
    CHECK (actions.size() == 1 && actions[0].a == 69);
    CHECK (press (song, state, decode (padNumber (6, 0)), 100, 0).empty()); // past the last pattern
    actions = press (song, state, decode (topNumber (6)), 100, 0);
    CHECK (actions.size() == 1 && actions[0].type == Action::togglePlay);
}

TEST (gp_launchpad_notes_mode_in_key)
{
    using namespace gp_lp;
    using namespace lynx::scales;
    auto song = songWith (1, 2);
    State state;
    state.mode = Mode::notes;
    state.key = { 9, naturalMinor }; // A minor
    state.octave = 2;
    CHECK (noteOfPad (state, 0, 7) == 45);     // bottom-left: the root, A2
    CHECK (noteOfPad (state, 1, 7) == 47);     // one degree up
    CHECK (noteOfPad (state, 0, 6) == 50);     // a row up: three degrees
    for (int x = 0; x < 8; ++x)
        for (int y = 0; y < 8; ++y)
            CHECK (inScale (noteOfPad (state, x, y), state.key));
    auto on = press (song, state, decode (padNumber (0, 7)), 90, 0);
    CHECK (on.size() == 1 && on[0].type == Action::noteOn && on[0].a == 45 && on[0].b == 90);
    auto off = release (song, state, decode (padNumber (0, 7)), 0);
    CHECK (off.size() == 1 && off[0].type == Action::noteOff && off[0].a == 45);
    press (song, state, decode (padNumber (8, 0)), 100, 0); // octave up
    CHECK (state.octave == 3 && noteOfPad (state, 0, 7) == 57);
    Now now;
    now.held[57] = true;
    const auto f = paint (song, state, now);
    CHECK (value (f.c[0][8]) > 0.9f && saturation (f.c[0][8]) < 0.05f); // a held pad is white
    CHECK (value (f.c[0][7]) < value (f.c[1][8]) || value (f.c[1][8]) > 0.0f);
}

TEST (gp_launchpad_steps_mode)
{
    using namespace gp_lp;
    auto song = songWith (1, 3);
    song.patterns[0].toggleStep (1, 2);
    State state;
    state.mode = Mode::steps;
    Now now;
    const auto f = paint (song, state, now);
    CHECK (value (f.c[2][2]) > value (f.c[3][2]));  // channel 1 (row 2 of the canvas), step 2 is on
    CHECK (value (f.c[0][4]) == 0.0f);              // no fourth channel
    auto actions = press (song, state, decode (padNumber (4, 0)), 100, 0);
    CHECK (actions.size() == 1 && actions[0].type == Action::toggleStep && actions[0].a == 0 && actions[0].b == 4);
    press (song, state, decode (topNumber (5)), 100, 0); // the next half bar
    CHECK (state.stepPage == 1);
    actions = press (song, state, decode (padNumber (0, 0)), 100, 0);
    CHECK (actions[0].b == 8);
    actions = press (song, state, decode (padNumber (8, 2)), 100, 0);
    CHECK (actions.size() == 1 && actions[0].type == Action::selectChannel && actions[0].a == 2);
    // the light show: a press only wakes the device
    state.lightShow = true;
    CHECK (press (song, state, decode (padNumber (0, 0)), 100, 0).empty() && ! state.lightShow);
}
