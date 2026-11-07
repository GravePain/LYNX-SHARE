#pragma once

// Sample-browser matching (GravePain port, step 8; our own code, from GravePain's KeyRules, TempoRules, SoundTags and
// AudioTags): which samples go with the project's key and tempo, read from what a file says about itself.
// - Key: a pitched sample either shares the key or its relative (A minor and C major are the same seven notes); there
//   is no tolerance, a semitone off is a minor second. Drums and effects have no key and go with everything.
// - Tempo: the same, half or double time fits; a small speed change is allowed, 6 % (about a semitone) for unpitched
//   sounds, 1 % for pitched ones, because speeding a loop up moves its pitch with it.
// - The key, the tempo and the sound type are read from the file name ("Bass Loop Am 128bpm.wav", "F# minor",
//   "140 BPM", "Kick_02"), and a loop's tempo can be guessed from its length (a whole number of bars nearest the
//   project's tempo), read from a WAV / AIFF / Ogg header without decoding.
// LYNX's browser had no key / tempo / type matching (it filters by file kind): nothing here duplicates it.

#include <optional>
#include <string>

namespace lynx::samplematch
{
// --- key ---------------------------------------------------------------------------------------------
struct Key
{
    int root = -1;      // pitch class 0..11; −1 = no key (drums, effects)
    bool minor = false;
    bool operator== (const Key&) const = default;
};
bool pitched (Key k) noexcept;
bool compatible (Key a, Key b) noexcept;   // same key, or relative major / minor; no key goes with any
std::string keyName (Key k);               // "A minor", "C# major", "no key"
// The key a file name states: "Am", "A min", "A minor", "Amaj", "A major", "F#m", "Bbmin", "in E" (major), "_Cm_"…
// nullopt when the name says none.
std::optional<Key> keyFromName (const std::string& name);

// --- tempo -------------------------------------------------------------------------------------------
inline constexpr float speedWindow = 0.06f, pitchedSpeedWindow = 0.01f;
// The playback speed a sample needs to fit the project's tempo (1 = as recorded); half and double time count as the
// same groove.
float playbackRate (float sampleBpm, float projectBpm) noexcept;
bool tempoCompatible (float sampleBpm, float projectBpm, bool pitchedSample) noexcept;
bool isHalfOrDouble (float sampleBpm, float projectBpm) noexcept;
float semitones (float playbackRate) noexcept; // how far a speed moves the pitch
// "128bpm", "128 BPM", "_128_", "bpm128", "Loop 140" (a number 60..200 on its own when the name says loop): 0 = none
float bpmFromName (const std::string& name);
// A loop's tempo from its length: 1, 2, 4, 8 or 16 bars at the tempo nearest the project's (half and double count as
// near). False when the length fits none (40..300 BPM).
bool guessFromLength (float seconds, float projectBpm, float& bpm, int& beats) noexcept;

// --- sound type --------------------------------------------------------------------------------------
// GravePain's agreed Type words: drums (Kick, Snare, Clap, Hihat, Open Hat, Cymbal, Tom, Percussion, Drum Loop,
// Drum Kit) and sounds (Bass, Sub Bass, Lead, Pluck, Arp, Stab, Chord, Pad, Keys, Bell, Mallets, Strings, Brass,
// Guitar, Voice, Riser, Impact, Texture).
std::string guessType (const std::string& name); // the longest type word or alias found whole; "" = none
bool isDrumType (const std::string& type);
// Whether a sample carries pitch: a stated key, or a pitched type (bass, lead, pad…).
bool soundsPitched (const std::string& name);

// --- audio file length -------------------------------------------------------------------------------
// Seconds, from a WAV's fmt / data chunks, an AIFF's COMM chunk or an Ogg Vorbis file's last granule; 0 when the
// header does not say (MP3, unreadable).
float lengthSeconds (const std::string& path);

// --- the browser's filter ----------------------------------------------------------------------------
struct Filter
{
    bool byKey = false;
    Key key {};            // the key to fit (root −1 with byKey: any pitched key is fine, no-key samples pass)
    bool byTempo = false;
    float bpm = 120.0f;    // the project's tempo
};
// Whether a sample (by its file name) fits: a sample that states nothing passes (we cannot tell it does not fit).
bool fits (const std::string& fileName, const Filter& filter);
} // namespace lynx::samplematch
