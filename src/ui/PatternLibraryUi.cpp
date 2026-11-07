#include "ui/PatternLibraryUi.h"

#include <algorithm>

#include "core/SmfRead.h"
#include "platform/Paths.h"

namespace lynx::patternlib
{
namespace
{
juce::File cacheFile() { return platform::settingsFile ("patterns").getChildFile ("patterns.txt"); }
juce::File tasteFile() { return platform::settingsFile ("patterns").getChildFile ("taste.txt"); }
juce::File foldersFile() { return platform::settingsFile ("pattern-folders.txt"); }

// what the cache was built from: each folder and its .mid files' count and newest time
juce::String signature()
{
    juce::String s;
    for (const auto& f : sourceFolders())
    {
        int count = 0;
        juce::int64 newest = 0;
        for (const auto& e : juce::RangedDirectoryIterator (f, true, "*.mid;*.midi"))
        {
            ++count;
            newest = juce::jmax (newest, e.getModificationTime().toMilliseconds());
        }
        s << f.getFullPathName() << "|" << count << "|" << newest << ";";
    }
    return s;
}

patterns::PatternSet& cached()
{
    static patterns::PatternSet set;
    return set;
}
bool& loaded()
{
    static bool b = false;
    return b;
}

void build()
{
    const auto sig = signature();
    const auto file = cacheFile();
    if (file.existsAsFile())
    {
        const auto text = file.loadFileAsString();
        const auto firstLine = text.upToFirstOccurrenceOf ("\n", false, false);
        if (firstLine == "#" + sig)
            if (auto set = patterns::fromText (text.fromFirstOccurrenceOf ("\n", false, false).toStdString()))
            {
                cached() = std::move (*set);
                loaded() = true;
                return;
            }
    }
    std::vector<patterns::SourceGroup> groups;
    for (const auto& f : sourceFolders())
        for (auto& g : patterns::groupsOfFolder (f.getFullPathName().toStdString()))
            groups.push_back (std::move (g));
    cached() = patterns::build (groups);
    cached().name = "Patterns";
    file.getParentDirectory().createDirectory();
    file.replaceWithText ("#" + sig + "\n" + juce::String::fromUTF8 (patterns::toText (cached()).c_str()));
    loaded() = true;
}

juce::StringArray sortedUnique (const std::vector<std::string>& values)
{
    juce::StringArray list;
    for (const auto& v : values)
        if (! v.empty())
            list.addIfNotAlreadyThere (juce::String::fromUTF8 (v.c_str()));
    list.sortNatural();
    return list;
}

std::string key (const patterns::PatternRecord& r) { return "pattern:" + r.id; }
} // namespace

juce::File bundledFolder()
{
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
    for (const auto& candidate : { exe.getChildFile ("midi-library/GravePain Originals"),
                                   exe.getParentDirectory().getChildFile ("resources/midi-library/GravePain Originals"),
                                   exe.getParentDirectory().getChildFile ("Resources/midi-library/GravePain Originals") })
        if (candidate.isDirectory())
            return candidate;
    return {};
}

juce::Array<juce::File> sourceFolders()
{
    juce::Array<juce::File> folders;
    if (const auto b = bundledFolder(); b.isDirectory())
        folders.add (b);
    for (const auto& line : juce::StringArray::fromLines (foldersFile().loadFileAsString()))
        if (const juce::File f (line.trim()); line.trim().isNotEmpty() && f.isDirectory())
            folders.addIfNotAlreadyThere (f);
    return folders;
}

void addSourceFolder (const juce::File& folder)
{
    if (! folder.isDirectory() || sourceFolders().contains (folder))
        return;
    foldersFile().appendText (folder.getFullPathName() + "\n");
    loaded() = false;
}

bool isLibraryFile (const juce::File& file)
{
    for (const auto& f : sourceFolders())
        if (file.isAChildOf (f))
            return true;
    return false;
}

const patterns::PatternSet& dataset()
{
    if (! loaded())
        build();
    return cached();
}

void rebuild()
{
    cacheFile().deleteFile();
    loaded() = false;
    build();
}

juce::StringArray styles()
{
    std::vector<std::string> v;
    for (const auto& r : dataset().records)
        v.push_back (patterns::genreOf (r.set));
    return sortedUnique (v);
}

juce::StringArray sections()
{
    std::vector<std::string> v;
    for (const auto& r : dataset().records)
        v.push_back (r.section);
    return sortedUnique (v);
}

juce::StringArray roles()
{
    std::vector<std::string> v;
    for (const auto& r : dataset().records)
        v.push_back (r.role);
    return sortedUnique (v);
}

taste::Taste& taste()
{
    static taste::Taste t;
    static bool read = false;
    if (! read)
    {
        t.load (tasteFile().getFullPathName().toStdString());
        read = true;
    }
    return t;
}

void keep (const patterns::PatternRecord& r)
{
    taste().add ({ key (r) }, taste::kept);
    taste().save (tasteFile().getFullPathName().toStdString());
}

void passOver (const patterns::PatternRecord& r)
{
    taste().add ({ key (r) }, taste::passedOver);
    taste().save (tasteFile().getFullPathName().toStdString());
}

const patterns::PatternRecord* pickFor (const juce::String& style, const juce::String& section, const juce::String& role, std::uint32_t seed)
{
    const auto& set = dataset();
    // the style is a genre: a set whose genre matches
    std::string setName;
    if (style.isNotEmpty())
        for (const auto& r : set.records)
            if (patterns::genreOf (r.set) == style.toStdString())
            {
                setName = r.set;
                break;
            }
    auto pool = patterns::poolFor (set, role.toStdString(), setName, section.toStdString());
    if (pool.empty())
        return nullptr;
    // patterns that fill every bar first, as patterns::pick does; then by taste
    std::vector<const patterns::PatternRecord*> full;
    for (const auto* r : pool)
        if (r->fill >= 0.99f)
            full.push_back (r);
    if (! full.empty())
        pool = std::move (full);
    std::vector<float> weights;
    for (const auto* r : pool)
        weights.push_back (taste().weight (key (*r)));
    const auto i = taste::Taste::pickWeighted (weights, seed);
    return i >= 0 ? pool[(std::size_t) i] : nullptr;
}

namespace
{
// appends notes, sorts, and returns where they ended up (each matched by value to one note not matched yet)
std::vector<int> addNotes (Pattern& pattern, const std::vector<Note>& notes)
{
    pattern.notes.insert (pattern.notes.end(), notes.begin(), notes.end());
    pattern.sortNotes();
    std::vector<int> indices;
    std::vector<char> used (pattern.notes.size(), 0);
    for (const auto& n : notes)
        for (int i = (int) pattern.notes.size() - 1; i >= 0; --i) // the sort is stable: added notes are the last of equals
            if (! used[(std::size_t) i] && pattern.notes[(std::size_t) i] == n)
            {
                used[(std::size_t) i] = 1;
                indices.push_back (i);
                break;
            }
    std::sort (indices.begin(), indices.end());
    return indices;
}

std::vector<Note> laid (const patterns::PatternRecord& r, int channel, int keyRoot, int keyScale, std::int64_t at, int bars)
{
    patterns::LayOptions o;
    o.rootNote = patterns::rootNoteInKey (r, keyRoot, keyScale);
    o.keyRoot = keyRoot;
    o.keyScale = keyScale;
    o.channel = channel;
    o.at = at;
    o.bars = bars;
    return patterns::lay (r, o);
}
} // namespace

std::vector<int> insert (Pattern& pattern, int channel, const patterns::PatternRecord& r, int keyRoot, int keyScale, std::int64_t at, int bars)
{
    return addNotes (pattern, laid (r, channel, keyRoot, keyScale, at, bars));
}

std::vector<int> insertFile (Pattern& pattern, int channel, const juce::File& file, int keyRoot, int keyScale, std::int64_t at)
{
    std::string error;
    const auto midi = smf::readFile (file.getFullPathName().toStdString(), error);
    if (! midi)
        return {};
    patterns::PatternSet one;
    patterns::addFile (*midi, file.getFullPathName().toStdString(), file.getParentDirectory().getFileName().toStdString(), one, true);
    std::vector<Note> notes;
    for (const auto& r : one.records) // every part's first pattern (a drum file gives one per drum)
        for (const auto& n : laid (r, channel, keyRoot, keyScale, at, 4))
            notes.push_back (n);
    return notes.empty() ? std::vector<int> {} : addNotes (pattern, notes);
}
} // namespace lynx::patternlib
