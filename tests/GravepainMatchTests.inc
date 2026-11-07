// GravePain port, step 8: sample-browser matching (core/SampleMatch). Ported from GravePain's RulesTests and
// BrowserTests where they apply, with the file-name readers.

#include "core/SampleMatch.h"
#include "core/BrowserView.h"

#include <filesystem>
#include <fstream>

TEST (gp_match_tempo_rules)
{
    using namespace lynx::samplematch;
    const auto near = [] (float a, float b) { return std::abs (a - b) < 1e-4f; };
    CHECK (near (playbackRate (120, 120), 1) && near (playbackRate (140, 70), 1) && near (playbackRate (70, 140), 1));
    CHECK (tempoCompatible (140, 70, false) && isHalfOrDouble (140, 70));
    CHECK (std::abs (semitones (1 + speedWindow) - 1) < 0.1f);     // a hi-hat may be pulled about a semitone
    CHECK (semitones (1 + pitchedSpeedWindow) < 0.25f);            // a bassline not a quarter of one
    CHECK (tempoCompatible (125, 120, false) && ! tempoCompatible (125, 120, true));
    CHECK (tempoCompatible (120.6f, 120, true));
    CHECK (! tempoCompatible (90, 120, false) && ! tempoCompatible (90, 140, false) && ! tempoCompatible (120, 140, false));
    float bpm = 0;
    int beats = 0;
    CHECK (guessFromLength (6.9f, 120, bpm, beats) && beats == 16 && std::abs (bpm - 139.1f) < 0.1f);
    CHECK (guessFromLength (3.4f, 120, bpm, beats) && beats == 8 && std::abs (bpm - 141.2f) < 0.1f);
    CHECK (guessFromLength (8.0f, 120, bpm, beats) && beats == 16 && std::abs (bpm - 120) < 0.01f);
    CHECK (! guessFromLength (0, 120, bpm, beats) && ! guessFromLength (5, 0, bpm, beats));
}

TEST (gp_match_key_rules)
{
    using namespace lynx::samplematch;
    const Key none {}, aMin { 9, true }, asMin { 10, true }, eMin { 4, true }, cMaj { 0, false }, gMaj { 7, false }, aMaj { 9, false }, cMin { 0, true };
    CHECK (! pitched (none) && compatible (none, aMin) && compatible (aMin, none));
    CHECK (compatible (aMin, aMin) && ! compatible (aMin, asMin) && ! compatible (aMin, eMin));
    CHECK (compatible (aMin, cMaj) && compatible (cMaj, aMin) && compatible (eMin, gMaj));
    CHECK (! compatible (aMaj, cMaj) && ! compatible (aMin, cMin));
    CHECK (keyName ({ 6, true }) == "F# minor" && keyName (none) == "no key");
}

TEST (gp_match_names)
{
    using namespace lynx::samplematch;
    CHECK ((keyFromName ("Bass Loop Am 128bpm.wav") == Key { 9, true }));
    CHECK ((keyFromName ("Pad_F#m_90.wav") == Key { 6, true }));
    CHECK ((keyFromName ("Keys in Eb.wav") == Key { 3, false }));
    CHECK ((keyFromName ("Lead C major 140.wav") == Key { 0, false }));
    CHECK ((keyFromName ("Chords_Bbmin.wav") == Key { 10, true }));
    CHECK ((keyFromName ("GMaj Stab.wav") == Key { 7, false }));
    CHECK (! keyFromName ("Kick 02.wav").has_value());
    CHECK (! keyFromName ("I am a sample.wav").has_value());
    CHECK (! keyFromName ("Bass Drop.wav").has_value());
    CHECK (bpmFromName ("Bass Loop Am 128bpm.wav") == 128);
    CHECK (bpmFromName ("Drums 140 BPM.wav") == 140);
    CHECK (bpmFromName ("BPM90 Groove.wav") == 90);
    CHECK (bpmFromName ("Drum Loop 174.wav") == 174);
    CHECK (bpmFromName ("Kick_02.wav") == 0);
    CHECK (bpmFromName ("Snare.wav") == 0);
    CHECK (guessType ("Kit1 - Afro Snare.wav") == "Snare");
    CHECK (guessType ("Sub Bass 2.wav") == "Sub Bass");
    CHECK (guessType ("Bass Sparse.wav") == "Bass");
    CHECK (guessType ("HiHat Closed.wav") == "Hihat");
    CHECK (guessType ("Lush Vocals.wav") == "Voice");
    CHECK (guessType ("Something.wav").empty());
    CHECK (isDrumType ("Kick") && ! isDrumType ("Bass"));
    CHECK (soundsPitched ("Bass Loop.wav") && ! soundsPitched ("Kick Loop.wav") && soundsPitched ("Thing Am.wav"));
}

TEST (gp_match_browser_filter)
{
    using namespace lynx::samplematch;
    Filter f;
    f.byKey = true;
    f.key = { 9, true }; // A minor
    f.byTempo = true;
    f.bpm = 128;
    CHECK (fits ("Bass Loop Am 128bpm.wav", f));
    CHECK (fits ("Pad C major.wav", f));          // the relative major
    CHECK (! fits ("Bass Loop Bbm 128bpm.wav", f));
    CHECK (! fits ("Bass Loop Am 140bpm.wav", f));
    CHECK (fits ("Drum Loop 64bpm.wav", f));      // half time
    CHECK (fits ("Hats 132bpm.wav", f));          // unpitched: 3 % off is fine
    CHECK (! fits ("Bass 132bpm.wav", f));        // pitched: not
    CHECK (fits ("Kick 02.wav", f));              // states nothing: passes
    f.byKey = false;
    CHECK (fits ("Bass Loop Bbm 128bpm.wav", f));
}

TEST (gp_match_file_lengths)
{
    using namespace lynx::samplematch;
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path() / "lynx-gp-match";
    fs::create_directories (dir);
    // a 16-bit stereo WAV of 2 s at 48 kHz: the data chunk says it
    {
        std::ofstream o (dir / "two.wav", std::ios::binary);
        const auto u32 = [&] (std::uint32_t v) { o.put ((char) (v & 255)).put ((char) ((v >> 8) & 255)).put ((char) ((v >> 16) & 255)).put ((char) (v >> 24)); };
        const auto u16 = [&] (std::uint16_t v) { o.put ((char) (v & 255)).put ((char) (v >> 8)); };
        const std::uint32_t data = 48000 * 2 * 2 * 2;
        o.write ("RIFF", 4);
        u32 (36 + data);
        o.write ("WAVEfmt ", 8);
        u32 (16), u16 (1), u16 (2), u32 (48000), u32 (48000 * 4), u16 (4), u16 (16);
        o.write ("data", 4);
        u32 (data);
        const std::vector<char> zeros (data, 0);
        o.write (zeros.data(), (std::streamsize) zeros.size());
    }
    CHECK (std::abs (lengthSeconds ((dir / "two.wav").string()) - 2.0f) < 1e-4f);
    // an AIFF: COMM with 44100 frames at 44100 Hz (an 80-bit rate)
    {
        std::ofstream o (dir / "one.aif", std::ios::binary);
        const std::uint8_t aiff[] = { 'F', 'O', 'R', 'M', 0, 0, 0, 30, 'A', 'I', 'F', 'F', 'C', 'O', 'M', 'M', 0, 0, 0, 18,
                                      0, 1, 0, 0, 0xAC, 0x44, 0, 16, 0x40, 0x0E, 0xAC, 0x44, 0, 0, 0, 0, 0, 0 };
        o.write ((const char*) aiff, sizeof aiff);
    }
    CHECK (std::abs (lengthSeconds ((dir / "one.aif").string()) - 1.0f) < 1e-4f);
    std::ofstream (dir / "not.mp3") << "ID3";
    CHECK (lengthSeconds ((dir / "not.mp3").string()) == 0.0f);
    CHECK (lengthSeconds ((dir / "missing.wav").string()) == 0.0f);
    fs::remove_all (dir);
}

TEST (gp_match_browser_view_round_trip)
{
    lynx::browserview::View v;
    v.matchKey = true;
    v.matchTempo = true;
    v.keyRoot = 9;
    v.keyMinor = true;
    CHECK (lynx::browserview::View::fromText (v.toText()) == v);
    CHECK (lynx::browserview::View::fromText ("filter=all\n") == lynx::browserview::View {}); // older views: off
}
