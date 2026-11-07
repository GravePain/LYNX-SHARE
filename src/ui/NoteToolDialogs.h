#pragma once

// Piano Roll note tool dialogs (docs/WORKFLOW_FL.md P2, R23; round 83): Strum, Arpeggiate, Flip, Randomise,
// Scale levels, Limit, Quantize, Chop, Flam, LFO, Claw machine, Articulate, Riff machine and Chord progression.
// Each is a small window whose controls preview live on the pattern (not in the history); Accept makes the change
// one undo step, Cancel / closing puts the notes back.
// The note maths lives in core/NoteTools and core/PianoRollTools.

#include <cstdint>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "engine/AudioEngine.h"

namespace lynx
{
enum class NoteTool
{
    strum, arpeggiate, flip, randomise, scaleLevels, limit,
    quantize, chop, flam, lfo, claw, articulate, riffMachine, chordProgression,
    euclidean, humanize, noteRepeat // LYNX's versions of FL's piano roll scripts (REA round 143)
    , patternLibrary, chordDice, melodyDice, riffStages // the GravePain port (docs/GRAVEPAIN_PORT.md)
};

const char* noteToolTitle (NoteTool tool); // English, for tr()
// Tools that write new notes and can open without any (riff machine, chord progression).
bool noteToolGenerates (NoteTool tool) noexcept;

// The panel (for --snapshot and tests); `indices` are notes of the current pattern, `grid` the editor's grid.
std::unique_ptr<juce::Component> makeNoteToolPanel (AudioEngine& engine, NoteTool tool, std::vector<int> indices, std::int64_t grid);

// Opens the tool in its own window. `onFinished` runs after Accept or Revert (the note indices changed).
void showNoteTool (AudioEngine& engine, NoteTool tool, std::vector<int> indices, std::int64_t grid, std::function<void()> onFinished = {});

// The Piano Roll's scale box (root 0..11, notetools::scales index; 0 = none): the key the GravePain tools start in.
void setNoteToolKey (int root, int scale) noexcept;
} // namespace lynx
