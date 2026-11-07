#pragma once

// The MIDI pattern library in the app (GravePain port, step 2): where the bundled library is, the dataset built from
// it (and from the folders the user adds), cached in the settings folder, the user's taste, and inserting a pattern
// into a channel of the current pattern in the Piano Roll's key. The pattern maths is core/MidiPatterns.

#include <vector>

#include <juce_core/juce_core.h>

#include "core/MidiPatterns.h"
#include "core/PatternTaste.h"
#include "core/Song.h"

namespace lynx::patternlib
{
// The bundled "GravePain Originals" folder: next to LYNX (midi-library/...) or, in a source tree, resources/; an
// invalid File when it is not there.
juce::File bundledFolder();
// The folders the dataset is built from: the bundled one, then the user's (settings: pattern-folders.txt).
juce::Array<juce::File> sourceFolders();
void addSourceFolder (const juce::File& folder);
// Whether a file is inside one of the source folders (a drop from there is inserted as a pattern, in key).
bool isLibraryFile (const juce::File& file);

// The dataset, built on first use (a few seconds for the bundled library) and cached in the settings folder
// (patterns/patterns.txt) until the folders change; rebuild() forces it.
const patterns::PatternSet& dataset();
void rebuild();
// Styles (the sets' genres), sections and roles present, sorted, for the choosers.
juce::StringArray styles();
juce::StringArray sections();
juce::StringArray roles();

// The user's taste (settings: patterns/taste.txt), saved after every change.
taste::Taste& taste();
void keep (const patterns::PatternRecord& r);     // the user kept a pick
void passOver (const patterns::PatternRecord& r); // the user rolled past it

// A pick from the pool for style / section / role ("" = any), weighted by taste, from the seed.
const patterns::PatternRecord* pickFor (const juce::String& style, const juce::String& section, const juce::String& role, std::uint32_t seed);

// Writes a record's notes into `channel` of `pattern` from `at`, laid in the key (keyRoot -1: as written) over
// `bars` bars. Returns the indices of the new notes (after sorting).
std::vector<int> insert (Pattern& pattern, int channel, const patterns::PatternRecord& r, int keyRoot, int keyScale, std::int64_t at, int bars);
// The same for a dropped library .mid file: its first pattern of each part, all parts merged. Empty when the file
// gave none.
std::vector<int> insertFile (Pattern& pattern, int channel, const juce::File& file, int keyRoot, int keyScale, std::int64_t at);
} // namespace lynx::patternlib
