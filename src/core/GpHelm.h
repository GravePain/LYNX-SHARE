#pragma once

// .helm presets into the GravePain Synth (GravePain port, step 5; our own importer, from GravePain's HelmPatch, no
// Helm code). A .helm file is JSON: a "settings" object of named values and a "modulations" list. Each value is
// mapped onto the matching part of the patch (times stored as the square root of seconds, volumes as the square root
// of the gain, frequencies as a power of two in hertz, cutoff as a MIDI note, tempos as an index) and each modulation
// onto a slot of the matrix; what has no match is named in `unmapped`. Helm's arpeggiator is not carried over (a LYNX
// channel has its own).

#include <optional>
#include <string>
#include <vector>

#include "core/GpSynth.h"

namespace lynx::gpsynth
{
struct HelmResult
{
    std::string name, folder, author;
    Patch patch {};
    std::vector<std::string> unmapped;
};

std::optional<HelmResult> readHelm (const std::string& jsonText, const std::string& fileName, std::string& problem);
std::optional<HelmResult> readHelmFile (const std::string& path, std::string& problem);
} // namespace lynx::gpsynth
