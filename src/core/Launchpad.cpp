#include "core/Launchpad.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

namespace lynx::launchpad
{
namespace
{
constexpr std::uint8_t sysexEnd = 0xF7, modeCommand = 0x0E, ledCommand = 0x03, ledRgb = 0x03;

std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

std::vector<std::uint8_t> header (Model model)
{
    for (const auto& m : models())
        if (m.model == model)
            return { 0xF0, 0x00, 0x20, 0x29, 0x02, m.id };
    return { 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C };
}

Colour rgbColour (std::uint32_t argb, float scale = 1.0f)
{
    return { (float) ((argb >> 16) & 255) / 255.0f * scale, (float) ((argb >> 8) & 255) / 255.0f * scale, (float) (argb & 255) / 255.0f * scale };
}

Colour scaled (Colour c, float k) { return { c.r * k, c.g * k, c.b * k }; }
Colour lerp (Colour a, Colour b, float t) { return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; }
Colour add (Colour a, Colour b) { return { a.r + b.r, a.g + b.g, a.b + b.b }; }
Colour cmax (Colour a, Colour b) { return { std::max (a.r, b.r), std::max (a.g, b.g), std::max (a.b, b.b) }; }
constexpr Colour black {}, white { 1, 1, 1 };

// the colours a pad takes when the project has none of its own (by index)
Colour autoColour (int index)
{
    static constexpr std::uint32_t wheel[] = { 0xFF5A8CFF, 0xFFFF6B6B, 0xFF4CD18A, 0xFFFFC14D, 0xFFB57BFF, 0xFF4DD6E0, 0xFFFF8A3D, 0xFFE05AC8 };
    return rgbColour (wheel[(std::size_t) (((index % 8) + 8) % 8)]);
}
} // namespace

const std::vector<ModelInfo>& models()
{
    static const std::vector<ModelInfo> list {
        { Model::x, "Launchpad X", 0x0C, { "lpx", "launchpad x" } },
        { Model::miniMk3, "Launchpad Mini MK3", 0x0D, { "lpminimk3", "launchpad mini mk3", "lpmini" } },
        { Model::proMk3, "Launchpad Pro MK3", 0x0E, { "lppromk3", "launchpad pro mk3" } },
    };
    return list;
}

const ModelInfo* modelOfPort (const std::string& portName)
{
    const auto name = lower (portName);
    // the second interface is meant for a DAW (Windows calls it MIDIIN2 / MIDIOUT2, macOS "DAW In"): not that one
    if (name.find ("daw") != std::string::npos || name.find ("midiin2") != std::string::npos || name.find ("midiout2") != std::string::npos)
        return nullptr;
    for (const auto& m : models())
        for (const auto& word : m.ports)
            if (name.find (word) != std::string::npos)
                return &m;
    return nullptr;
}

int padNumber (int column, int row) noexcept { return (size - row) * 10 + (column == size ? 9 : column + 1); }
int topNumber (int column) noexcept { return 90 + column + 1; }

Pad decode (int number) noexcept
{
    Pad p;
    const int row = number / 10, col = number % 10;
    if (row == 9)
    {
        if (col >= 1 && col <= size)
            p = { PadKind::top, col - 1, 0 };
        return p;
    }
    if (row < 1 || row > size)
        return p;
    if (col >= 1 && col <= size)
        return { PadKind::grid, col - 1, size - row };
    if (col == 9)
        return { PadKind::scene, size, size - row };
    return p;
}

std::vector<std::uint8_t> programmerMode (Model model, bool on)
{
    auto m = header (model);
    m.push_back (modeCommand);
    m.push_back (on ? 1 : 0);
    m.push_back (sysexEnd);
    return m;
}

Rgb toDevice (Colour c) noexcept
{
    const auto ch = [] (float v) { return (std::uint8_t) std::clamp ((int) std::lround (v * 127.0f), 0, 127); };
    return { ch (c.r), ch (c.g), ch (c.b) };
}

void Leds::queue (int number, Rgb c)
{
    pending.insert (pending.end(), { ledRgb, (std::uint8_t) number, c.r, c.g, c.b });
}

void Leds::set (int column, int row, Colour c)
{
    if (column < 0 || column > size || row < 0 || row >= size)
        return;
    const auto next = toDevice (c);
    auto& was = shown[(std::size_t) column][(std::size_t) row];
    if (was == next)
        return; // only what changed is sent
    was = next;
    queue (padNumber (column, row), next);
}

void Leds::setTop (int column, Colour c)
{
    if (column < 0 || column >= size)
        return;
    const auto next = toDevice (c);
    if (topShown[(std::size_t) column] == next)
        return;
    topShown[(std::size_t) column] = next;
    queue (topNumber (column), next);
}

void Leds::clear()
{
    // sent whatever was shown: a device just opened may still be lit by another program
    pending.clear();
    for (int row = 0; row < size; ++row)
        for (int column = 0; column <= size; ++column)
        {
            shown[(std::size_t) column][(std::size_t) row] = {};
            queue (padNumber (column, row), {});
        }
    for (int column = 0; column < size; ++column)
    {
        topShown[(std::size_t) column] = {};
        queue (topNumber (column), {});
    }
}

std::vector<std::uint8_t> Leds::flush (Model model)
{
    if (pending.empty())
        return {};
    auto m = header (model);
    m.push_back (ledCommand);
    m.insert (m.end(), pending.begin(), pending.end());
    m.push_back (sysexEnd);
    pending.clear();
    return m;
}

Colour ledColour (Colour screen) noexcept
{
    constexpr float floorV = 0.19f, curve = 1.2f, depth = 2.5f, greyBelow = 0.08f, colourAbove = 0.25f;
    // RGB -> HSV
    const float mx = std::max ({ screen.r, screen.g, screen.b }), mn = std::min ({ screen.r, screen.g, screen.b });
    float v = mx, s = mx > 0.0f ? (mx - mn) / mx : 0.0f, h = 0.0f;
    if (mx > mn)
    {
        const float d = mx - mn;
        if (mx == screen.r)
            h = std::fmod ((screen.g - screen.b) / d, 6.0f);
        else if (mx == screen.g)
            h = (screen.b - screen.r) / d + 2.0f;
        else
            h = (screen.r - screen.g) / d + 4.0f;
        h /= 6.0f;
        if (h < 0.0f)
            h += 1.0f;
    }
    if (v <= floorV)
        return black;
    v = std::pow ((v - floorV) / (1.0f - floorV), curve);
    const float deep = 1.0f - std::pow (1.0f - s, depth);
    float t = std::clamp ((s - greyBelow) / (colourAbove - greyBelow), 0.0f, 1.0f);
    t = t * t * (3.0f - 2.0f * t);
    s = s + (deep - s) * t;
    // HSV -> RGB
    const float c = v * s, hp = h * 6.0f, x = c * (1.0f - std::abs (std::fmod (hp, 2.0f) - 1.0f)), m = v - c;
    Colour out;
    if (hp < 1) out = { c, x, 0 };
    else if (hp < 2) out = { x, c, 0 };
    else if (hp < 3) out = { 0, c, x };
    else if (hp < 4) out = { 0, x, c };
    else if (hp < 5) out = { x, 0, c };
    else out = { c, 0, x };
    return { out.r + m, out.g + m, out.b + m };
}

// --- the light show ---------------------------------------------------------------------------------------
namespace
{
constexpr int W = LightShow::width, H = LightShow::height;
constexpr float centre = 4.0f, pi = 3.14159265358979f;
using Canvas = LightShow::Canvas;

struct Random
{
    std::uint32_t s;
    explicit Random (std::uint32_t seed) : s (seed * 2654435761u + 1013904223u) { if (s == 0) s = 1; }
    float next() noexcept
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return (float) (s >> 8) / 16777216.0f;
    }
    float range (float lo, float hi) noexcept { return lo + (hi - lo) * next(); }
    int range (int lo, int hiExclusive) noexcept { return lo + std::min (hiExclusive - lo - 1, (int) (next() * (float) (hiExclusive - lo))); }
};

// smooth noise in two dimensions, 0..1 (value noise: hashed lattice, smoothstep between)
float noise (float x, float y)
{
    const auto hash = [] (int i, int j) {
        std::uint32_t h = (std::uint32_t) i * 374761393u + (std::uint32_t) j * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return (float) ((h ^ (h >> 16)) & 0xFFFF) / 65535.0f;
    };
    const int xi = (int) std::floor (x), yi = (int) std::floor (y);
    float fx = x - (float) xi, fy = y - (float) yi;
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    const float a = hash (xi, yi), b = hash (xi + 1, yi), c = hash (xi, yi + 1), d = hash (xi + 1, yi + 1);
    return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

Colour hex (std::uint32_t rgb) { return rgbColour (rgb); }

struct Palette
{
    std::vector<Colour> stops;
    Colour sample (float t) const
    {
        t -= std::floor (t);
        const float scaledT = t * (float) stops.size();
        const int i = (int) scaledT % (int) stops.size();
        float k = scaledT - std::floor (scaledT);
        k = k * k * (3 - 2 * k);
        return lerp (stops[(std::size_t) i], stops[(std::size_t) ((i + 1) % (int) stops.size())], k);
    }
};

const std::vector<Palette>& palettes()
{
    static const std::vector<Palette> p {
        { { hex (0xFF1FD1), hex (0x6A00FF), hex (0x00D5FF), hex (0x00FFA3) } },              // neon
        { { hex (0xFF2A00), hex (0xFF8A00), hex (0xFF2D7A), hex (0x7A1FFF) } },              // sunset
        { { hex (0x0022FF), hex (0x00B3FF), hex (0xB8F4FF), hex (0x3D5AFE) } },              // ice
        { { hex (0x7CFF00), hex (0xFFE600), hex (0x00FFC8), hex (0x00FF55) } },              // acid
        { { hex (0xFF0055), hex (0xFFD000), hex (0x00E5FF), hex (0x8C00FF), hex (0x00FF6A) } }, // club
    };
    return p;
}

struct FrameInfo
{
    float t = 0, dt = 0, kick = 0;
    bool beatHit = false;
    int beatCount = 0;
    LightShow::Music music;
    const Palette* palette = nullptr;
};

float dist (float x, float y) { return std::sqrt (x * x + y * y); }

struct Layer
{
    virtual ~Layer() = default;
    virtual void reset (float) {}
    virtual void draw (FrameInfo& f, Canvas& c, Random& rng) = 0;
};

struct Plasma final : Layer
{
    void draw (FrameInfo& f, Canvas& c, Random&) override
    {
        for (int x = 0; x < W; ++x)
            for (int y = 0; y < H; ++y)
            {
                const float dx = (float) x - centre, dy = (float) y - centre;
                const float v = std::sin ((float) x * 0.55f + f.t * 0.9f) + std::sin ((float) y * 0.45f - f.t * 1.1f)
                                + std::sin ((float) (x + y) * 0.35f + f.t * 0.6f) + std::sin (dist (dx, dy) * 0.9f - f.t * 1.6f);
                c[(std::size_t) x][(std::size_t) y] = scaled (f.palette->sample (v * 0.125f + 0.5f + f.t * 0.04f), 0.62f + 0.38f * f.kick);
            }
    }
};

struct Ripples final : Layer
{
    struct Ring { float x, y, born, speed, hue, strength, life; };
    std::vector<Ring> rings;
    float nextSpawn = 0;
    void reset (float t) override
    {
        rings.clear();
        nextSpawn = t;
    }
    void draw (FrameInfo& f, Canvas& c, Random& rng) override
    {
        if (f.t >= nextSpawn)
        {
            rings.push_back ({ rng.range (0.0f, W - 1.0f), rng.range (0.0f, H - 1.0f), f.t, rng.range (3.5f, 6.0f), rng.next(), rng.range (0.6f, 1.0f), rng.range (1.6f, 2.4f) });
            nextSpawn = f.t + rng.range (0.35f, 1.1f);
        }
        if (f.beatHit) // the beat is a stone dropped in the middle; every fourth a bigger one
            rings.push_back ({ centre, centre, f.t, 7.0f, f.t * 0.05f, f.beatCount % 4 == 0 ? 1.3f : 0.8f, 1.5f });
        const auto glow = scaled (f.palette->sample (f.t * 0.03f), 0.05f);
        for (auto& col : c)
            col.fill (glow);
        for (int i = (int) rings.size() - 1; i >= 0; --i)
        {
            const auto& r = rings[(std::size_t) i];
            const float age = f.t - r.born, fade = 1.0f - age / r.life;
            if (fade <= 0.0f)
            {
                rings.erase (rings.begin() + i);
                continue;
            }
            const float radius = age * r.speed;
            for (int x = 0; x < W; ++x)
                for (int y = 0; y < H; ++y)
                {
                    const float d = dist ((float) x - r.x, (float) y - r.y), edge = d - radius;
                    const float ring = std::exp (-edge * edge * 1.6f) * fade * fade * r.strength;
                    if (ring >= 0.01f)
                        c[(std::size_t) x][(std::size_t) y] = add (c[(std::size_t) x][(std::size_t) y], scaled (f.palette->sample (r.hue + d * 0.05f), ring));
                }
        }
    }
};

struct Spiral final : Layer
{
    void draw (FrameInfo& f, Canvas& c, Random&) override
    {
        constexpr int arms = 3;
        for (int x = 0; x < W; ++x)
            for (int y = 0; y < H; ++y)
            {
                const float dx = (float) x - centre, dy = (float) y - centre, r = dist (dx, dy), a = std::atan2 (dy, dx);
                const float s = std::sin (a * arms + r * 1.1f - f.t * 3.2f - f.kick * 0.8f);
                const float band = std::pow (std::clamp (0.5f + 0.5f * s, 0.0f, 1.0f), 1.6f);
                const auto colour = f.palette->sample (r * 0.09f - f.t * 0.07f + a / (2 * pi) * 0.25f);
                c[(std::size_t) x][(std::size_t) y] = scaled (colour, (0.06f + 0.94f * band) * (0.8f + 0.2f * f.kick));
            }
        c[4][4] = lerp (c[4][4], white, f.kick * 0.7f); // the eye flashes with the kick
    }
};

struct Rain final : Layer
{
    struct Drop { int x; float y, speed, hue; };
    std::vector<Drop> drops;
    Canvas trail {};
    float nextSpawn = 0;
    void reset (float t) override
    {
        drops.clear();
        trail = {};
        nextSpawn = t;
    }
    void spawn (Random& rng) { drops.push_back ({ rng.range (0, W), -1.0f, rng.range (6.0f, 13.0f), rng.next() }); }
    void draw (FrameInfo& f, Canvas& c, Random& rng) override
    {
        const float decay = std::exp (-f.dt * 5.5f);
        for (auto& col : trail)
            for (auto& p : col)
                p = scaled (p, decay);
        if (f.t >= nextSpawn)
        {
            spawn (rng);
            nextSpawn = f.t + rng.range (0.05f, 0.18f);
        }
        if (f.beatHit)
            for (int i = 0; i < 3; ++i)
                spawn (rng);
        for (int i = (int) drops.size() - 1; i >= 0; --i)
        {
            auto& d = drops[(std::size_t) i];
            const float from = d.y;
            d.y += d.speed * f.dt;
            const auto head = lerp (f.palette->sample (d.hue + f.t * 0.02f), white, 0.35f);
            // every cell crossed this frame, or a fast drop leaves gaps in its own trail
            for (int y = std::max (0, (int) std::floor (from)); y <= std::min (H - 1, (int) std::floor (d.y)); ++y)
                trail[(std::size_t) d.x][(std::size_t) y] = cmax (trail[(std::size_t) d.x][(std::size_t) y], head);
            if (d.y > (float) H + 1.0f)
                drops.erase (drops.begin() + i);
        }
        c = trail;
    }
};

struct Fire final : Layer
{
    std::array<std::array<float, H + 1>, W> heat {}; // a hidden fuel row below the canvas
    float step = 0;
    static Colour flame (float h)
    {
        static const Colour ramp[] = { black, hex (0x5A0000), hex (0xC81000), hex (0xFF5A00), hex (0xFFB300), hex (0xFFF3B0) };
        h = std::clamp (h, 0.0f, 1.0f) * 5.0f;
        const int i = std::min (4, (int) h);
        return lerp (ramp[i], ramp[i + 1], h - (float) i);
    }
    void reset (float) override
    {
        heat = {};
        step = 0;
    }
    void draw (FrameInfo& f, Canvas& c, Random& rng) override
    {
        step += f.dt;
        while (step >= 1.0f / 30.0f) // a fixed rate: the same flames at any frame rate
        {
            step -= 1.0f / 30.0f;
            for (int x = 0; x < W; ++x)
                heat[(std::size_t) x][H] = rng.range (0.55f, 1.0f) + f.kick * 0.35f;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                {
                    const float below = heat[(std::size_t) x][(std::size_t) y + 1] * 2 + heat[(std::size_t) std::max (0, x - 1)][(std::size_t) y + 1]
                                        + heat[(std::size_t) std::min (W - 1, x + 1)][(std::size_t) y + 1];
                    heat[(std::size_t) x][(std::size_t) y] = std::max (0.0f, below / 4.05f - rng.range (0.05f, 0.13f));
                }
        }
        for (int x = 0; x < W; ++x)
            for (int y = 0; y < H; ++y)
                c[(std::size_t) x][(std::size_t) y] = flame (heat[(std::size_t) x][(std::size_t) y]);
    }
};

const std::map<char, std::array<std::uint8_t, 7>>& font()
{
    // 5 x 7 capitals and digits, a byte a row, top first, the left pixel in bit 4
    static const std::map<char, std::array<std::uint8_t, 7>> f {
        { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } }, { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
        { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } }, { 'D', { 0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E } },
        { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } }, { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
        { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } }, { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
        { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } }, { 'J', { 0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C } },
        { 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } }, { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
        { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } }, { 'N', { 0x11, 0x11, 0x19, 0x15, 0x13, 0x11, 0x11 } },
        { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } }, { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
        { 'Q', { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D } }, { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
        { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } }, { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
        { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } }, { 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
        { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A } }, { 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
        { 'Y', { 0x11, 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04 } }, { 'Z', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F } },
        { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } }, { '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
        { '2', { 0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F } }, { '3', { 0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E } },
        { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } }, { '5', { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E } },
        { '6', { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E } }, { '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
        { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } }, { '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C } },
        { '!', { 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04 } }, { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
        { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04 } }, { '\'', { 0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00 } },
    };
    return f;
}

struct Text final : Layer
{
    const std::string* message = nullptr;
    std::vector<std::uint8_t> columns;
    std::string built;
    float start = 0;
    void build()
    {
        if (message == nullptr || (built == *message && ! columns.empty()))
            return;
        built = *message;
        columns.clear();
        for (char ch : built)
        {
            const auto it = font().find ((char) std::toupper ((unsigned char) ch));
            if (it == font().end())
            {
                columns.insert (columns.end(), { 0, 0, 0 });
                continue;
            }
            for (int col = 0; col < 5; ++col)
            {
                std::uint8_t bits = 0;
                for (int row = 0; row < 7; ++row)
                    if (((it->second[(std::size_t) row] >> (4 - col)) & 1) != 0)
                        bits |= (std::uint8_t) (1 << (6 - row));
                columns.push_back (bits);
            }
            columns.push_back (0); // a gap between letters
        }
    }
    void reset (float t) override
    {
        start = t;
        build();
    }
    void draw (FrameInfo& f, Canvas& c, Random&) override
    {
        build();
        for (int x = 0; x < W; ++x)
            for (int y = 0; y < H; ++y)
                c[(std::size_t) x][(std::size_t) y] = scaled (f.palette->sample ((float) x * 0.04f + (float) y * 0.03f + f.t * 0.05f), 0.07f);
        const int total = (int) columns.size() + W; // the message, a screen of nothing, again
        const float scroll = std::fmod ((f.t - start) * 7.0f, (float) total);
        const int first = (int) std::floor (scroll);
        for (int x = 0; x < W; ++x)
        {
            const int m = first + x - W; // starts off the right-hand edge
            if (m < 0 || m >= (int) columns.size())
                continue;
            const auto bits = columns[(std::size_t) m];
            const auto colour = lerp (f.palette->sample ((float) m * 0.025f + f.t * 0.1f), white, 0.15f + 0.25f * f.kick);
            for (int row = 0; row < 7; ++row)
                if (((bits >> (6 - row)) & 1) != 0)
                    c[(std::size_t) x][(std::size_t) row + 2] = colour; // on the bottom seven pad rows
        }
        // a spark on the top buttons walks the bar, so the beat shows behind the words
        const int spark = ((f.beatCount % 8) + 8) % 8;
        c[(std::size_t) spark][0] = scaled (f.palette->sample (f.t * 0.2f), 0.4f + 0.6f * f.kick);
    }
};

struct Equaliser final : Layer
{
    std::array<float, 8> level {}, peak {}, peakHeld {};
    float master = 0;
    void reset (float) override
    {
        level = peak = peakHeld = {};
        master = 0;
    }
    static void bar (Canvas& c, int x, float value, const Palette& p, float hueBase)
    {
        const float h = value * 8.0f;
        for (int k = 0; k < 8; ++k)
        {
            const float fill = std::clamp (h - (float) k, 0.0f, 1.0f);
            if (fill <= 0.0f)
                break;
            c[(std::size_t) x][(std::size_t) (8 - k)] = scaled (p.sample (hueBase + (float) k / 8.0f * 0.6f), fill); // row 8 = the bottom pad row
        }
    }
    void draw (FrameInfo& f, Canvas& c, Random&) override
    {
        float sum = 0;
        for (int i = 0; i < 8; ++i)
        {
            float target;
            const bool real = f.music.playing;
            if (real && f.music.levels[(std::size_t) i] > 0.0f)
                target = std::clamp (f.music.levels[(std::size_t) i] * (0.5f + 0.5f * f.kick) + (noise ((float) i * 1.7f, f.t * 4.0f) - 0.5f) * 0.25f, 0.0f, 1.0f);
            else if (real)
                target = 0.0f; // a channel that is not playing reads as silent
            else
                target = std::clamp (0.2f + 0.6f * noise ((float) i * 0.7f, f.t * 1.6f) * (0.65f + 0.35f * f.kick), 0.0f, 1.0f);
            const float rate = target > level[(std::size_t) i] ? 25.0f : 5.0f; // up fast, down slow, like a meter
            level[(std::size_t) i] += (target - level[(std::size_t) i]) * (1.0f - std::exp (-f.dt * rate));
            sum += level[(std::size_t) i];
            if (level[(std::size_t) i] >= peak[(std::size_t) i])
            {
                peak[(std::size_t) i] = level[(std::size_t) i];
                peakHeld[(std::size_t) i] = 0.45f;
            }
            else if ((peakHeld[(std::size_t) i] -= f.dt) <= 0.0f)
                peak[(std::size_t) i] = std::max (level[(std::size_t) i], peak[(std::size_t) i] - f.dt * 0.6f);
        }
        master += (sum / 8.0f - master) * (1.0f - std::exp (-f.dt * 12.0f));
        c = {};
        for (int i = 0; i < 8; ++i)
        {
            bar (c, i, level[(std::size_t) i], *f.palette, 0.1f);
            const int top = std::min (7, (int) std::floor (peak[(std::size_t) i] * 8.0f));
            if (peak[(std::size_t) i] > 0.05f)
                c[(std::size_t) i][(std::size_t) (8 - top)] = cmax (c[(std::size_t) i][(std::size_t) (8 - top)], lerp (f.palette->sample (0.7f), white, 0.6f));
            if (level[(std::size_t) i] > 0.9f)
                c[(std::size_t) i][0] = scaled (white, 0.9f);
        }
        bar (c, 8, master, *f.palette, 0.55f); // the scene column is the master meter
    }
};
} // namespace

struct LightShow::Impl
{
    Random rng;
    std::vector<std::unique_ptr<Layer>> layers;
    Canvas a {}, b {}, out {};
    std::vector<int> order;
    int orderPos = 0, current = 0, next = -1, palette = 0, nextPalette = 0;
    float startTime = 0, lastTime = 0, effectStart = 0, fadeStart = 0;
    int lastBeat = INT32_MIN, beatCount = 0;
    Text* text = nullptr;

    explicit Impl (std::uint32_t seed) : rng (seed)
    {
        layers.push_back (std::make_unique<Plasma>());
        layers.push_back (std::make_unique<Ripples>());
        layers.push_back (std::make_unique<Spiral>());
        layers.push_back (std::make_unique<Rain>());
        layers.push_back (std::make_unique<Fire>());
        auto t = std::make_unique<Text>();
        text = t.get();
        layers.push_back (std::move (t));
        layers.push_back (std::make_unique<Equaliser>());
    }
    // a new running order each time round, never the same effect twice across the join
    void shuffle (int avoidFirst)
    {
        order.clear();
        for (int i = 0; i < (int) layers.size(); ++i)
            order.push_back (i);
        for (int i = (int) order.size() - 1; i > 0; --i)
            std::swap (order[(std::size_t) i], order[(std::size_t) rng.range (0, i + 1)]);
        if (order[0] == avoidFirst)
            std::swap (order[0], order.back());
        orderPos = 0;
    }
    int nextEffect()
    {
        if (orderPos >= (int) order.size())
            shuffle (current);
        return order[(std::size_t) orderPos++];
    }
};

LightShow::LightShow (std::uint32_t seed) : impl (new Impl (seed)) { impl->text->message = &message; }
LightShow::~LightShow() { delete impl; }
LightShow::Effect LightShow::current() const noexcept { return (Effect) impl->current; }

void LightShow::reset (float now)
{
    auto& s = *impl;
    s.startTime = s.lastTime = now;
    s.effectStart = 0;
    s.next = -1;
    s.lastBeat = INT32_MIN;
    s.beatCount = 0;
    s.shuffle (-1);
    s.current = s.order[(std::size_t) s.orderPos++];
    s.palette = s.rng.range (0, (int) palettes().size());
    s.layers[(std::size_t) s.current]->reset (0);
}

const LightShow::Canvas& LightShow::render (float now, const Music& music)
{
    auto& s = *impl;
    const float t = now - s.startTime, dt = std::clamp (now - s.lastTime, 0.0f, 0.1f);
    s.lastTime = now;
    // the beat: the music's when it plays, a steady 120 BPM of our own when not
    float phase;
    int beat;
    if (music.playing)
    {
        phase = std::clamp (music.beatPhase, 0.0f, 1.0f);
        beat = music.beat;
    }
    else
    {
        const float beats = t * 2.0f;
        beat = (int) std::floor (beats);
        phase = beats - (float) beat;
    }
    const bool hit = s.lastBeat != INT32_MIN && beat != s.lastBeat;
    s.lastBeat = beat;
    if (hit)
        ++s.beatCount;
    const float kick = std::exp (-phase * 5.0f) * (music.playing ? 1.0f : 0.6f);
    if (s.next < 0 && t - s.effectStart >= effectSeconds)
    {
        s.next = s.nextEffect();
        s.nextPalette = (s.palette + s.rng.range (1, (int) palettes().size())) % (int) palettes().size();
        s.fadeStart = t;
        s.layers[(std::size_t) s.next]->reset (t);
    }
    FrameInfo f;
    f.t = t, f.dt = dt, f.kick = kick, f.beatHit = hit, f.beatCount = s.beatCount, f.music = music;
    f.palette = &palettes()[(std::size_t) s.palette];
    s.a = {};
    s.layers[(std::size_t) s.current]->draw (f, s.a, s.rng);
    if (s.next >= 0)
    {
        float k = std::clamp ((t - s.fadeStart) / std::max (0.01f, fadeSeconds), 0.0f, 1.0f);
        k = k * k * (3 - 2 * k);
        f.palette = &palettes()[(std::size_t) s.nextPalette];
        s.b = {};
        s.layers[(std::size_t) s.next]->draw (f, s.b, s.rng);
        for (int x = 0; x < W; ++x)
            for (int y = 0; y < H; ++y)
                s.a[(std::size_t) x][(std::size_t) y] = lerp (s.a[(std::size_t) x][(std::size_t) y], s.b[(std::size_t) x][(std::size_t) y], k);
        if (k >= 1.0f)
        {
            s.current = s.next;
            s.palette = s.nextPalette;
            s.next = -1;
            s.effectStart = s.fadeStart;
        }
    }
    // LEDs are linear, eyes are not: a power gives deep blacks and trails that trail
    for (int x = 0; x < W; ++x)
        for (int y = 0; y < H; ++y)
        {
            const auto& c = s.a[(std::size_t) x][(std::size_t) y];
            const auto g = [this] (float v) { return std::pow (std::clamp (v, 0.0f, 1.0f), gamma); };
            s.out[(std::size_t) x][(std::size_t) y] = { g (c.r), g (c.g), g (c.b) };
        }
    return s.out;
}

// --- the surface -------------------------------------------------------------------------------------------
namespace
{
// the top buttons
constexpr int topPatterns = 0, topNotes = 1, topSteps = 2, topLights = 3, topLeft = 4, topRight = 5, topPlay = 6;

Colour patternColour (const Song& song, int index)
{
    const auto& p = song.patterns[(std::size_t) index];
    return p.colour != 0 ? rgbColour (p.colour) : autoColour (index);
}

Colour channelColour (const Song& song, int index)
{
    const auto& c = song.channels[(std::size_t) index];
    return c.colour != 0 ? rgbColour (c.colour) : autoColour (index);
}

int channelCount (const Song& song) { return (int) song.channels.size(); }
int stepCount (const Song& song, int pattern)
{
    // the pattern's length in steps, at least a bar
    const auto& p = song.patterns[(std::size_t) pattern];
    const auto len = p.effectiveLength (std::max (1, song.beatsPerBar));
    return std::max (16, (int) (len / ticksPerStep));
}
} // namespace

int noteOfPad (const State& state, int x, int y) noexcept
{
    // the in-key grid of step 1: columns one scale degree, rows (from the bottom) three degrees apart
    const int base = std::clamp (12 * (state.octave + 1) + (state.key.root < 0 ? 0 : state.key.root % 12), 0, 127);
    return scales::scalePad (x, size - 1 - y, base, state.key);
}

Frame paint (const Song& song, const State& state, const Now& now)
{
    Frame f;
    auto& c = f.c; // [x][y], y 0 = top buttons, 1-8 = grid, x 8 = scene column
    const float pulse = 0.55f + 0.45f * std::sin (now.pulse * 2.0f * pi);
    // the top buttons: the mode lit, the light show, play
    const Colour modeOff { 0.25f, 0.25f, 0.3f }, modeOn { 0.3f, 0.7f, 1.0f };
    c[topPatterns][0] = state.mode == Mode::patterns ? modeOn : modeOff;
    c[topNotes][0] = state.mode == Mode::notes ? modeOn : modeOff;
    c[topSteps][0] = state.mode == Mode::steps ? modeOn : modeOff;
    c[topLights][0] = Colour { 0.7f, 0.25f, 0.9f };
    c[topLeft][0] = c[topRight][0] = state.mode == Mode::steps ? Colour { 0.5f, 0.5f, 0.5f } : black;
    c[topPlay][0] = now.playing ? Colour { 0.2f, 1.0f, 0.3f } : Colour { 0.25f, 0.4f, 0.25f };

    const int patterns = (int) song.patterns.size();
    if (state.mode == Mode::patterns)
    {
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const int index = state.patternPage * 64 + y * size + x;
                if (index >= patterns)
                    continue;
                auto colour = scaled (patternColour (song, index), 0.45f);
                if (index == song.currentPattern)
                    colour = patternColour (song, index);
                if (index == now.playingPattern && now.playing)
                    colour = lerp (patternColour (song, index), white, 0.5f * pulse);
                c[(std::size_t) x][(std::size_t) y + 1] = colour;
            }
        const int pages = std::max (1, (patterns + 63) / 64);
        for (int y = 0; y < size; ++y)
            if (y < pages)
                c[8][(std::size_t) y + 1] = y == state.patternPage ? white : Colour { 0.3f, 0.3f, 0.3f };
    }
    else if (state.mode == Mode::notes)
    {
        const auto channel = std::clamp (now.selectedChannel, 0, std::max (0, channelCount (song) - 1));
        const auto colour = channelCount (song) > 0 ? channelColour (song, channel) : Colour { 0.4f, 0.6f, 1.0f };
        const int root = state.key.root < 0 ? 0 : state.key.root % 12;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
            {
                const int note = noteOfPad (state, x, y);
                const bool isRoot = ((note - root) % 12 + 12) % 12 == 0;
                Colour pad = isRoot ? colour : scaled (colour, 0.3f);
                if (note >= 0 && note < 128 && now.held[(std::size_t) note])
                    pad = white;
                c[(std::size_t) x][(std::size_t) y + 1] = pad;
            }
        c[8][1] = Colour { 0.5f, 0.5f, 0.5f }; // octave up
        c[8][8] = Colour { 0.5f, 0.5f, 0.5f }; // octave down
    }
    else
    {
        const int pattern = std::clamp (song.currentPattern, 0, std::max (0, patterns - 1));
        if (patterns > 0)
        {
            const int steps = stepCount (song, pattern);
            for (int y = 0; y < size; ++y)
            {
                const int channel = state.channelPage * size + y;
                if (channel >= channelCount (song))
                    continue;
                const auto colour = channelColour (song, channel);
                for (int x = 0; x < size; ++x)
                {
                    const int step = state.stepPage * size + x;
                    if (step >= steps)
                        continue;
                    const bool on = song.patterns[(std::size_t) pattern].hasStep (channel, step);
                    Colour pad = on ? colour : (step / 4 % 2 == 0 ? Colour { 0.12f, 0.12f, 0.12f } : Colour { 0.06f, 0.06f, 0.06f });
                    if (step == now.playStep && now.playing)
                        pad = lerp (pad, white, 0.6f);
                    c[(std::size_t) x][(std::size_t) y + 1] = pad;
                }
                c[8][(std::size_t) y + 1] = scaled (colour, channel == now.selectedChannel ? 1.0f : 0.35f);
            }
        }
    }
    return f;
}

std::vector<Action> press (const Song& song, State& state, Pad pad, int velocity, int selectedChannel)
{
    std::vector<Action> out;
    if (state.lightShow) // any press ends the show and only wakes the device
    {
        state.lightShow = false;
        return out;
    }
    const int patterns = (int) song.patterns.size();
    if (pad.kind == PadKind::top)
    {
        switch (pad.x)
        {
            case topPatterns: state.mode = Mode::patterns; out.push_back ({ Action::setMode, (int) Mode::patterns }); break;
            case topNotes: state.mode = Mode::notes; out.push_back ({ Action::setMode, (int) Mode::notes }); break;
            case topSteps: state.mode = Mode::steps; out.push_back ({ Action::setMode, (int) Mode::steps }); break;
            case topLights: state.lightShow = true; out.push_back ({ Action::showLights }); break;
            case topLeft:
                if (state.mode == Mode::steps && state.stepPage > 0)
                {
                    --state.stepPage;
                    out.push_back ({ Action::page });
                }
                break;
            case topRight:
                if (state.mode == Mode::steps && patterns > 0
                    && (state.stepPage + 1) * size < stepCount (song, std::clamp (song.currentPattern, 0, patterns - 1)))
                {
                    ++state.stepPage;
                    out.push_back ({ Action::page });
                }
                break;
            case topPlay: out.push_back ({ Action::togglePlay }); break;
            default: break;
        }
        return out;
    }
    if (pad.kind == PadKind::scene)
    {
        if (state.mode == Mode::patterns && pad.y * 64 < std::max (1, patterns))
        {
            state.patternPage = pad.y;
            out.push_back ({ Action::page });
        }
        else if (state.mode == Mode::notes)
        {
            if (pad.y == 0 && state.octave < 8)
                ++state.octave;
            else if (pad.y == size - 1 && state.octave > 0)
                --state.octave;
            out.push_back ({ Action::page });
        }
        else if (state.mode == Mode::steps)
        {
            // a scene button selects that row's channel (its notes and the Piano roll follow)
            const int channel = state.channelPage * size + pad.y;
            if (channel < channelCount (song))
                out.push_back ({ Action::selectChannel, channel });
        }
        return out;
    }
    if (pad.kind != PadKind::grid)
        return out;
    if (state.mode == Mode::patterns)
    {
        const int index = state.patternPage * 64 + pad.y * size + pad.x;
        if (index < patterns)
            out.push_back ({ Action::playPattern, index });
    }
    else if (state.mode == Mode::notes)
        out.push_back ({ Action::noteOn, noteOfPad (state, pad.x, pad.y), std::clamp (velocity, 1, 127) });
    else if (patterns > 0)
    {
        const int channel = state.channelPage * size + pad.y, step = state.stepPage * size + pad.x;
        if (channel < channelCount (song) && step < stepCount (song, std::clamp (song.currentPattern, 0, patterns - 1)))
            out.push_back ({ Action::toggleStep, channel, step });
    }
    (void) selectedChannel;
    return out;
}

std::vector<Action> release (const Song&, State& state, Pad pad, int)
{
    if (pad.kind == PadKind::grid && state.mode == Mode::notes)
        return { { Action::noteOff, noteOfPad (state, pad.x, pad.y), 0 } };
    return {};
}
} // namespace lynx::launchpad
