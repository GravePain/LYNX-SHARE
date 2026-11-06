#pragma once

// A Standard MIDI File reader without JUCE (GravePain port, step 2: the reading half of GravePain's MidiFile), so
// core code and the tests can read .mid files. Produces the same midiimport::ParsedMidi that engine/MidiFileIo makes
// with JUCE: raw note / controller / program / pitch-bend events per track, ticks at LYNX's PPQ, the track names,
// the first tempo and time signature. Formats 0, 1 and 2 (2 read as 1); SMPTE time division is refused.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "core/MidiImport.h"

namespace lynx::smf
{
std::optional<midiimport::ParsedMidi> parse (std::span<const std::uint8_t> bytes, std::string& error);
std::optional<midiimport::ParsedMidi> readFile (const std::string& path, std::string& error);

// The first program change of each MIDI channel (-1 = never set): what a part was written for.
std::array<int, 16> programs (const midiimport::ParsedMidi& midi);
} // namespace lynx::smf
