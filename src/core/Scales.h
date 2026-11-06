#pragma once

// Scale degrees, chords on a note and the in-key pad grid (GravePain port, step 1; our own code, from GravePain's
// SynthScales). Builds on NoteTools' scale list, inScale and snapToScale (the Piano Roll's scale highlight / snap):
// a Key is a root and an index into notetools::scales() (0 = no scale, i.e. chromatic). What this adds: stepping by
// scale degrees, chords stacked from the key, and the layout of an 8x8 grid (Launchpad, phone pad) in the key.
// No allocation: safe on the audio thread.

#include <array>
#include <span>

namespace lynx::scales
{
// notetools::scales() indices used by name here
inline constexpr int noScale = 0, major = 1, naturalMinor = 2, majorPentatonic = 4, minorPentatonic = 5;

struct Key
{
    int root = 0;      // pitch class 0..11 (C = 0); negative = C
    int scale = noScale;
    bool operator== (const Key&) const = default;
};

// The semitones above the root in the key's scale (all twelve without one).
std::span<const int> degrees (Key key) noexcept;
bool inScale (int note, Key key) noexcept;
// The nearest in-key note, a tie going down (notetools::snapToScale).
int snap (int note, Key key) noexcept;
// The note `steps` scale degrees above `from` (negative goes down); `from` should itself be in the key.
int stepInScale (int from, int steps, Key key) noexcept;

enum class ChordShape { single, triad, seventh, power, octave, sus2, sus4 };
inline constexpr int chordShapeCount = 7;

struct ChordNotes
{
    std::array<int, 4> notes {};
    int count = 0;
    std::span<const int> view() const noexcept { return { notes.data(), (std::size_t) count }; }
};

// A chord on `note`. In a seven-note scale, triads, sevenths and sus chords are stacked from the key (C major: C on
// C, Dm on D, B° on B); without a scale, in a pentatonic one or on a note outside the key, a plain major chord. Notes
// outside 0..127 are dropped.
ChordNotes buildChord (int note, ChordShape shape, Key key) noexcept;
const char* chordShapeLabel (ChordShape shape) noexcept; // "Single", "Triad", "7th"... (English, for tr())

// The note under pad (column, rowFromBottom) of the in-key grid: each column one degree up, each row three degrees
// above the one below (a triad lies under three fingers in a line); without a scale, rows are a fourth apart.
int scalePad (int column, int rowFromBottom, int baseNote, Key key) noexcept;
} // namespace lynx::scales
