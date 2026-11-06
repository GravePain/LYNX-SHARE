#include "core/Scales.h"

#include "core/NoteTools.h"

namespace lynx::scales
{
namespace
{
constexpr int chromaticDegrees[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

constexpr int mod (int a, int m) noexcept { return ((a % m) + m) % m; }
constexpr int floorDiv (int a, int b) noexcept { return (a - mod (a, b)) / b; }
constexpr int clampNote (int n) noexcept { return n < 0 ? 0 : (n > 127 ? 127 : n); }
int rootOf (Key key) noexcept { return key.root < 0 ? 0 : key.root % 12; }
bool chromatic (Key key) noexcept { return key.scale <= 0 || key.scale >= (int) notetools::scales().size(); }
} // namespace

std::span<const int> degrees (Key key) noexcept
{
    if (chromatic (key))
        return chromaticDegrees;
    return notetools::scales()[(std::size_t) key.scale].intervals;
}

bool inScale (int note, Key key) noexcept
{
    return chromatic (key) || notetools::inScale (note, rootOf (key), key.scale);
}

int snap (int note, Key key) noexcept
{
    return chromatic (key) ? note : notetools::snapToScale (note, rootOf (key), key.scale);
}

int stepInScale (int from, int steps, Key key) noexcept
{
    const auto deg = degrees (key);
    const int n = (int) deg.size();
    const int root = rootOf (key);
    const int relative = from - root;
    const int pc = mod (relative, 12);
    int index = 0;
    for (int i = 0; i < n; ++i)
        if (deg[(std::size_t) i] <= pc)
            index = i;
    const int target = index + steps;
    return root + (floorDiv (relative, 12) + floorDiv (target, n)) * 12 + deg[(std::size_t) mod (target, n)];
}

ChordNotes buildChord (int note, ChordShape shape, Key key) noexcept
{
    int raw[4] {};
    int count = 0;
    const auto add = [&] (int n) { raw[count++] = n; };
    const bool diatonic = ! chromatic (key) && degrees (key).size() == 7 && inScale (note, key);
    switch (shape)
    {
        case ChordShape::single: add (note); break;
        case ChordShape::power: add (note); add (note + 7); add (note + 12); break;
        case ChordShape::octave: add (note); add (note + 12); break;
        case ChordShape::sus2: add (note); add (diatonic ? stepInScale (note, 1, key) : note + 2); add (note + 7); break;
        case ChordShape::sus4: add (note); add (diatonic ? stepInScale (note, 3, key) : note + 5); add (note + 7); break;
        case ChordShape::triad:
        case ChordShape::seventh:
            add (note);
            add (diatonic ? stepInScale (note, 2, key) : note + 4);
            add (diatonic ? stepInScale (note, 4, key) : note + 7);
            if (shape == ChordShape::seventh)
                add (diatonic ? stepInScale (note, 6, key) : note + 10);
            break;
    }
    ChordNotes c;
    for (int i = 0; i < count; ++i)
        if (raw[i] >= 0 && raw[i] <= 127)
            c.notes[(std::size_t) c.count++] = raw[i];
    return c;
}

const char* chordShapeLabel (ChordShape shape) noexcept
{
    switch (shape)
    {
        case ChordShape::single: return "Single";
        case ChordShape::triad: return "Triad";
        case ChordShape::seventh: return "7th";
        case ChordShape::power: return "Power";
        case ChordShape::octave: return "Octave";
        case ChordShape::sus2: return "Sus2";
        case ChordShape::sus4: break;
    }
    return "Sus4";
}

int scalePad (int column, int rowFromBottom, int baseNote, Key key) noexcept
{
    const int start = snap (baseNote, key);
    if (chromatic (key))
        return clampNote (start + column + rowFromBottom * 5);
    return clampNote (stepInScale (start, column + rowFromBottom * 3, key));
}
} // namespace lynx::scales
