#pragma once

// MIDI pattern dataset (GravePain port, step 2; our own code, from GravePain's MidiPatterns). A folder of MIDI files
// is formatted once into a dataset of short patterns the Piano Roll / Channel Rack can insert from:
// - every file is split into its parts (a MIDI channel each; the drum channel 10 further by drum, by GM note);
// - each part into windows of up to four bars, quantised to sixteenths; a window three quarters like one already
//   kept is dropped, and a window that loops inside itself (AAAA, ABAB) is cut to its loop;
// - each pattern gets a role (from the file / track name, then the GM program, then its notes), a section (Intro,
//   Verse...), a shape (rhythm, line, chords, mixed) and a root, and stores its notes as semitones from that root, so
//   it can be laid in any key.
// Pure functions, no JUCE. Building reads files (not for the audio thread).

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/MidiImport.h"
#include "core/Song.h"

namespace lynx::patterns
{
inline constexpr int stepsPerBeat = 4, stepsPerBar = 16, maxSteps = 64; // four bars of sixteenths

inline constexpr const char* rhythm = "rhythm";
inline constexpr const char* line = "line";
inline constexpr const char* chords = "chords";
inline constexpr const char* mixed = "mixed";

// Kick, Snare, Clap, Hat, Ride, Crash, Tom, Perc, Bass, Chords, Lead, Pad, Bell, Pluck, Drone, FX, Other
extern const std::array<const char*, 17> roles;
extern const std::array<const char*, 8> drumRoles;
extern const std::array<const char*, 7> sections; // Intro, Verse, Chorus, Bridge, Break, Fill, Outro

struct PatternNote
{
    int step = 0, length = 1; // sixteenths
    int note = 60;            // as written
    int rel = 0;              // semitones from the record's rootNote
    int velocity = 100;       // 1..127
    bool operator== (const PatternNote&) const = default;
};

struct PatternRecord
{
    std::string id, set, name, section, role, shape;
    int bars = 4;             // 1, 2 or 4
    int root = 0;             // the pitch class the notes lean on
    int rootNote = 60;        // the lowest note of that class
    bool minor = false;
    int poly = 0;
    float density = 0.0f;     // notes a bar
    float fill = 1.0f;        // share of the bars with a note
    std::string source;       // the file
    std::string blocks;       // "AAAB", "A-A-": a letter a bar, alike bars sharing one
    float contour = 0.0f, firstHalf = 0.0f, secondHalf = 0.0f; // -1..1: which way it goes (whole loop, each half)
    int part = 0;             // the MIDI channel it came from
    std::vector<PatternNote> notes;

    bool isRhythm() const noexcept { return shape == rhythm; }
};

struct PatternSet
{
    std::string name;
    bool firstWindowOnly = false;
    std::vector<std::string> sources;
    std::vector<PatternRecord> records;
};

bool isDrumRole (std::string_view role) noexcept;
std::string sectionOf (const std::string& words);               // "" when the words name none
std::string drumRole (int gmNote);
std::string programRole (int program);                          // "" for -1
std::string roleOf (const std::string& words, int program, bool drumChannel); // "Other" when nothing says
std::string roleOfShape (const PatternRecord& shaped);          // a part nothing placed, by its notes
std::string blocksOf (const std::vector<PatternNote>& notes, int bars);
int loopBars (const std::string& blocks);
std::string genreOf (const std::string& setName);               // "GravePain Originals / House" -> "House"

// Formats one parsed file into records of `into`; `path` gives the name words. Returns how many it added.
int addFile (const midiimport::ParsedMidi& midi, const std::string& path, const std::string& setName, PatternSet& into,
             bool firstWindowOnly = false);

struct SourceGroup
{
    std::string label;              // the set: "GravePain Originals / House"
    std::vector<std::string> files; // .mid paths
};
// Every file of every group; files that are not MIDI or are broken are skipped.
PatternSet build (const std::vector<SourceGroup>& groups, bool firstWindowOnly = false);
// The groups of a library folder: each sub-folder (sorted) is a set "<folder name> / <sub-folder>", the .mid files in
// the folder itself a set named after it.
std::vector<SourceGroup> groupsOfFolder (const std::string& folder);

// The records that fit (empty string = any), sorted by id so a seed means the same thing each time.
std::vector<const PatternRecord*> matching (const PatternSet& set, const std::string& role, const std::string& setName,
                                            const std::string& section);
// The pool for a role: in the set and section; else the set; else anywhere; else the same family (drums / notes).
std::vector<const PatternRecord*> poolFor (const PatternSet& set, const std::string& role, const std::string& setName,
                                           const std::string& section);
// The seed's pick from a pool, patterns that fill every bar and four-bar ones first; nullptr for an empty pool.
const PatternRecord* pick (const std::vector<const PatternRecord*>& pool, std::uint32_t seed);

// Two or three patterns as one (GravePain's MIX): whole, halves, bars, turnaround, swap notes, reverse, flip, answer,
// fit, A A B C.
enum class Mix { whole, halves, bars, turnaround, swapNotes, reverse, flip, answer, fit, aabc };
inline constexpr int mixCount = 10;
bool mixTakesSecond (Mix mix) noexcept;
PatternRecord mix (const PatternRecord& a, const PatternRecord* b, Mix how, const PatternRecord* c = nullptr, float busy = -1.0f);

// The record's notes as Piano Roll notes on `channel` from tick `at`, repeated to `bars` (1..4): moved so its root
// lands on `rootNote`, by octaves into lowest..highest (a rhythm keeps its pitches). `keyRoot` / `keyScale` given
// (keyRoot >= 0): pitched notes also snap into that key.
struct LayOptions
{
    int rootNote = 60;
    int lowest = 0, highest = 127;
    int bars = 4;
    int channel = 0;
    std::int64_t at = 0;
    int keyRoot = -1;
    int keyScale = 1;          // notetools::scales() index the pitched notes snap into (1 major, 2 natural minor)
};
std::vector<Note> lay (const PatternRecord& r, const LayOptions& options);
// The note the record's root should land on in a key (root 0..11, notetools::scales index): the key's home, or its
// relative minor / major when the record leans the other way (a minor line in C major is laid on A), in the octave
// nearest the record's own root. A rhythm, or keyRoot < 0, keeps its root as written.
int rootNoteInKey (const PatternRecord& r, int keyRoot, int keyScale);

// The dataset as text (one line a record, tab-separated, notes "step:length:note:velocity"), and back.
std::string toText (const PatternSet& set);
std::optional<PatternSet> fromText (const std::string& text);
} // namespace lynx::patterns
