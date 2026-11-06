#pragma once

// Chord Dice, chord lanes and Melody Dice (GravePain port, step 3; our own code, from GravePain's ChordDice,
// ClipPattern's ChordBar and MelodyDice). Piano Roll tools next to prtools' chord progression:
// - a chord lane is four (or more) bars, each a ChordBar: a degree of the key, optionally its own key, a shape
//   (triad, 7th, sus2, sus4, 5th, 6th, add9, 9th, 11th, 13th) and an inversion;
// - the Chord Dice pick a progression from a hand-written table of the loops songs are built on (common ones more
//   often), from a seed;
// - chords are voiced on a ladder of keys, each chord as close to the last as it can be (then inverted as asked);
// - the Melody Dice roll a one-note-at-a-time tune on a ladder of in-key notes from a seed, optionally standing on the
//   chord under every beat.
// Same seed + same inputs = same notes, on every machine (GravePain's xorshift generator). Pure functions, no JUCE.

#include <cstdint>
#include <string>
#include <vector>

#include "core/Song.h"

namespace lynx::prtools::dice
{
enum class Shape { triad, seventh, sus2, sus4, fifth, sixth, add9, ninth, eleventh, thirteenth };
inline constexpr int shapeCount = 10;
const char* shapeLabel (Shape shape) noexcept;       // "Triad", "7th", ... (English, for tr())
const char* inversionLabel (int inversion) noexcept; // "Root", "1st inv", "2nd inv"

struct ChordBar
{
    int degree = 0;    // 0..6, 0 = the key's home chord
    int tonic = -1;    // the bar's own key (pitch class), -1 = the lane's
    int minor = -1;    // 1 minor, 0 major, -1 = the lane's
    Shape shape = Shape::triad;
    int inversion = 0; // 0 root, 1 first, 2 second

    bool ownKey() const noexcept { return tonic >= 0; }
    bool operator== (const ChordBar&) const = default;
    static ChordBar of (int degree) { ChordBar b; b.degree = degree; return b; }
};

struct Progression
{
    std::string name;          // "I V vi IV"
    std::vector<int> degrees;  // four (a bar each) or two (two bars each)
    int weight = 1;            // 3 everywhere, 2 common, 1 now and then
    bool minor = false;
    std::vector<int> bars() const; // the four bars' degrees
};

const std::vector<Progression>& progressions (bool minor);
// The progression a seed picks, leaning towards the common ones.
const Progression& pickProgression (std::uint32_t seed, bool minor);
std::vector<ChordBar> barsOf (const Progression& p);

// The pitch classes of a bar's chord in its key and shape (2..5 of them, root first).
std::vector<int> pitchClasses (const ChordBar& bar, int laneTonic, bool laneMinor);
// "G7", "Dm7", "Bm7b5", "Csus4", "A5", "Fmaj7", "Am" (sharps); "-" never (a bar is always a chord).
std::string chordName (const ChordBar& bar, int laneTonic, bool laneMinor);
// "C   G   Am (Am)   F": each bar's name, its own key in brackets when it has one.
std::string chordNames (const std::vector<ChordBar>& bars, int laneTonic, bool laneMinor);

// The keys low..high (all twelve pitch classes) or only the lane's key's notes, lowest first.
std::vector<int> ladder (int low, int high, int tonic = -1, bool minor = false);

// For each bar, the rungs (indices into ladderKeys) its chord is played on: the first chord's root on the lowest rung
// that has it, then each chord's notes on the rungs nearest the middle of the chord before; then inverted as the bar
// asks. A bar the same as the one before holds it.
std::vector<std::vector<int>> voice (const std::vector<ChordBar>& bars, const std::vector<int>& ladderKeys, int laneTonic, bool laneMinor);
// For each bar, the rungs of ladderKeys that are notes of its chord: what a melody may stand on.
std::vector<std::vector<int>> chordTones (const std::vector<ChordBar>& bars, const std::vector<int>& ladderKeys, int laneTonic, bool laneMinor);

struct PlaceOptions
{
    int channel = 0;
    std::int64_t start = 0;
    std::int64_t barLength = 4 * ppq;
    float velocity = 100.0f / 128.0f;
};
// The chords as held notes, a bar the same as the one before holding on; sorted by start.
std::vector<Note> placeChords (const std::vector<ChordBar>& bars, const std::vector<int>& ladderKeys, int laneTonic, bool laneMinor,
                               const PlaceOptions& options);

enum class Landing { none, atTheEnd, twice };

struct MelodyOptions
{
    std::uint32_t seed = 1;
    int home = 0;            // the rung of ladderKeys to start and end on
    int resolution = 2;      // 4 quarter notes, 2 eighths, 1 sixteenths
    Landing landing = Landing::none;
    PlaceOptions place;      // channel, start, bar length (four bars are rolled)
};
// A four-bar tune on ladderKeys. With `tones` (four bars of chordTones) every note on a beat stands on the chord.
std::vector<Note> rollMelody (const std::vector<int>& ladderKeys, const MelodyOptions& options,
                              const std::vector<std::vector<int>>* tones = nullptr);

// A chord lane's settings as text ("degree:tonic:minor:shape:inversion" per bar, space-separated), so the same roll
// can be saved with a pattern and come back.
std::string toText (const std::vector<ChordBar>& bars);
std::vector<ChordBar> barsFromText (const std::string& text);
} // namespace lynx::prtools::dice
