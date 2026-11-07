# LYNX SHARE user guide

How to use LYNX SHARE's own features, the ones FL Studio does not have. For the FL-style workflow (Channel Rack,
Piano Roll, Playlist, Mixer) see [WORKFLOW_FL.md](WORKFLOW_FL.md); for the built-in plugins see [PLUGINS.md](PLUGINS.md).

## Online sessions: make a song with a friend (phase 1, packages)

Send your project to a friend, who records their part in their own LYNX SHARE with their own plugins and sends it back; you
merge it into your project. Phase 1 works with files only: no network, no account, no server. Send the package however
you like (e-mail, WhatsApp, a shared cloud folder, a USB stick). The live version (both of you in the same project at
the same time) is planned, see [design/ONLINE_SESSIONS.md](design/ONLINE_SESSIONS.md).

### 1. The host sends the project

1. Open your project and choose **File › Online session › Send project...**
2. Type **your name** and **your friend's name**.
3. Choose what your friend keeps when the session ends:
   - **Only their own part** (recommended): after they send their part back, their copy of the project is reduced to
     what they made.
   - **A full copy of the project**: they keep everything.

   This is a courtesy between musicians, not copy protection: the package is a normal zip.
4. Click **Save package...** and choose where to save the `.lynxpack`.
5. LYNX SHARE renders the parts that use a third-party plugin (VST3, VST2, CLAP) into audio **stand-ins**, so your
   friend hears them even without your plugins, and writes the package: the project, every sample and recording it
   uses (each file once, checked with SHA-256), the stand-ins and a manifest (the plugins used, with versions).
6. Send the `.lynxpack` file to your friend.

Up to 4 people take part in one project (you included): each guest gets their own colour, their own mixer inserts (20
of them, after the ones you use) and their own Playlist tracks (16 of them).

### 2. The friend opens it and records their part

1. In LYNX SHARE, **File › Open...** the `.lynxpack` (or double-click it on Windows).
2. A summary shows who sent it, which mixer inserts and Playlist tracks are theirs, and what they keep at the end. Click
   **Open**. The project is unpacked into `Documents/LYNX/Projects/Received/<package name>/`.
3. The song plays the same as on the host's PC. A channel or insert whose plugin is not installed here plays its
   stand-in and has an amber **R** badge in the Channel Rack and the Mixer (the hint bar explains it). Its plugin
   settings stay in the project: the host still has them.
4. If a plugin is installed here in another version, LYNX SHARE asks whether to **play the renders** (sounds as sent)
   or **use my version** (may sound different).
5. Record your part: add your channels, patterns and audio, use your inserts and your Playlist tracks. New channels and
   patterns you make are yours; the host's are left as they are.
6. Save as usual (**File › Save**) while working.

### 3. The friend sends their part back

1. **File › Online session › Send my part back...** and choose where to save the package.
2. LYNX SHARE renders stand-ins for **your** parts that use third-party plugins (so the host hears them even without
   your plugins) and writes a small package with only your area: your channels, your notes, your patterns and
   automations, your Playlist clips, your inserts, your recordings and samples, and your stand-ins.
3. Send it to the host. If the host chose "Only their own part", your copy is now reduced to your part.

### 4. The host merges the part

1. Open the project you sent (the same one: the package checks it) and choose **File › Online session › Merge a
   part...** (or open the part's `.lynxpack` with **File › Open...**).
2. The friend's part is added in their colour: their channels, patterns, notes, clips and inserts. Nothing of yours is
   changed. If you used one of their inserts or Playlist tracks meanwhile, theirs moved to free ones (the message says so).
3. Merging is **one undo step**: **Edit › Undo** takes it out again.
4. Their media are copied next to your project (`<project folder>/Session media/<their name>/`); their stand-ins play
   where you lack their plugins (amber **R** badge).
5. Merging a newer part from the same friend replaces their earlier part (the things they deleted are removed too).

### Good to know

- Stand-ins are renders of the part (instrument channels: per pattern, played at each Playlist clip; inserts or parts
  with song-level automation: the whole song). They follow the clips you move, but not edits to the notes of a plugin
  you do not have: ask its owner for a new package.
- Stand-ins are saved next to the project in `Stand-ins/`. A part that did not change is not rendered again.
- The package keeps every plugin's settings (opaque, as the plugin saved them). Nothing is uploaded anywhere.
- Both sides should use the same LYNX SHARE version (the open summary warns when they differ).

## Dice and patterns: start a part in the key

Four Piano roll tools write notes for you, all in the key chosen in the Piano roll's scale box (Key and Scale can be
changed in each tool). They are under **Tools › Dice and patterns**. Each opens a small window that previews on the
pattern as you change it: **Accept** keeps the notes as one undo step, **Cancel** puts back what was there. With notes
selected, they replace the selection; with none, they write on the selected channel from the start of the bar.

Every tool is **seeded**: the same seed and the same settings give the same notes, on any computer. **Throw dice**
rolls a new seed. The seed and the settings are saved with the pattern, so the roll you kept comes back the next time
you open the tool on that pattern.

### Pattern from the library

LYNX ships with a library of MIDI parts, *GravePain Originals*: 2,659 one-part files in 14 styles (Afro, Ambient, DnB,
Funk, Garage, HipHop, House, LoFi, Pop, Rock, Synthwave, Techno, Trance, Trap), for the Intro, Verse, Chorus, Break,
Fill and Outro, played by drums, bass, chords, pads, leads, plucks and bells. They are original material, free to use
in your songs.

- Choose a **Style**, a **Section** and a **Role** (or leave any of them on *Any*), the **Key**, the **Scale** and the
  **Length**. A pattern that fits is laid in the key: its root moves to the key's home (a minor line in a major key goes
  to the relative minor) and its notes snap into the scale. Drum parts keep their notes.
- **Another pattern** picks another one. LYNX remembers what you keep and what you roll past, and from then on picks
  the patterns you keep more often (never always, never never). This is kept in LYNX's settings folder
  (`patterns/taste.txt`), not in the project.
- The library is also in the **Browser** as **Pattern library**. Drag a file onto the Piano roll and it goes in at the
  scale box's key; onto a channel of the Channel Rack and it goes into that channel as written.
- **Tools › Dice and patterns › Add a MIDI folder to the pattern library...** adds your own MIDI files. Each file is
  cut into patterns of up to four bars, one per part (a drum part one per drum). The role and section are read from the
  file and track names (`House 03 Verse Bass F#m.mid`), else from the instrument the part was written for.

### Chord dice

Rolls a chord progression from the loops songs are built on (the common ones come up more often), or plays one you
choose in **Progression**. **Shape** sets triads, 7ths, sus2 / sus4, 5ths, 6ths, add9, 9ths, 11ths or 13ths, and
**Inversion** the root position or an inversion. Each chord is voiced close to the one before, so the hand barely
moves, and a chord repeated in the next bar is held. Accept also keeps the chords as the pattern's **chord lane**,
which the Melody dice can follow.

### Melody dice

Rolls a one-note-at-a-time tune over four bars, on the key's notes from **Octave** over **Range** octaves, at 1/4, 1/8
or 1/16. It starts on the **Home degree** and comes back to it, moves mostly by step, and its bars 3 and 4 answer bars 1
and 2. With **Follow the chord lane** on, every note on a beat is a note of the chord under it. **Ending** can make the
tune land on a chord note at the end (or at bars 2 and 4) instead of looping round.

### Riff stages

Shapes the selected notes in four stages, run in order: **Mirror** (turn time over, or keep the starts and play the
pitches in reverse order; turn pitch over about the key's home, staying in the key), **Levels** (reset to one velocity
first, then wander louder and quieter, or only quieter, and vary the lengths), **Articulation** (multiply the lengths up
to 4x, take a gap off every note, vary them, chop held notes into repeats on the grid) and **Groove** (straight, swing
8ths, swing 16ths, laid back or pushed, by a timing amount). For an arpeggio, use **Tools › Arpeggiate**.

## GravePain Synth

LYNX's own subtractive and wavetable synthesiser. Add it from the Channel Rack's **+** menu (**GravePain Synth**). It
comes with 51 sounds (leads, keys, basses, pads, plucks, stabs, bells, risers, impacts and a drum set): use the
channel's preset arrows or its preset list, under LYNX.

The channel window has five pages:

- **Dials**: the sound's **Type** and its simple controls, named for what they do on that type of sound (a bass has
  Bright, Squelch, Dirt, Sub, Wobble...; a pad Slow, Bright, Wide, Space, Motion...). A dial moves several settings
  together; it shows where the sound sits, so loading a preset puts every dial in place. Every dial can be automated.
- **Oscillators**: two oscillators (sine, triangle, saw, square, wavetable, stepped and pyramid shapes), each with a
  wavetable position, up to 15 unison copies, sub, noise with a colour, FM, cross modulation and feedback, a sample
  layer, voice mode (poly, mono, legato), glide and the clap retrigger.
- **Filter and envelopes**: low pass (12 or 24 dB), band, high, notch, peak, blend and shelves, with drive and
  saturation; filter, amplitude and pitch envelopes (the pitch envelope falls fast, or builds slowly for a riser).
- **Modulation**: two LFOs, a poly LFO, a free envelope, a 16-step sequencer and an 8-slot matrix (source, target,
  amount, and an optional second source scaling it).
- **Effects**: drive and crush, chorus, delay (synced or in milliseconds, ping-pong), reverb, formant and stutter.

The wavetables are LYNX's own (Basic shapes, PWM, Harmonics). For the sample layer, point the channel's sample at a
folder of WAVs named with their note (`Piano C3.wav`, `F#4.wav`) or at a .wti file over WAVs. Helm presets (.helm) can
be read into the synth's settings (what has no match in it is listed).

## Launchpad

LYNX drives a Novation Launchpad X, Mini MK3 or Pro MK3. Turn it on in **Options › Launchpad › Use a Launchpad**; it is
found when it is plugged in (or use **Look for it again**). While LYNX drives it, its pads do not play notes on their
own: the round buttons along the top choose what they do.

- **1 Patterns**: each pad is a pattern of the project (its colour). Press one to make it the current pattern and play
  it in pattern mode; the playing one pulses. The buttons down the right pick the page of 64.
- **2 Notes**: the pads play the selected channel, in key: each column one note of the scale up, each row three notes
  up, so a chord lies under three fingers in a line, and nothing out of key is on the board. The root notes are lit.
  The top and bottom buttons on the right move an octave up and down. Choose the key in **Options › Launchpad ›
  Notes mode key**.
- **3 Steps**: the Channel Rack's step sequencer for the current pattern: each row a channel, each column a step
  (top buttons 5 and 6 move half a bar). The buttons on the right select a row's channel.
- **4** starts the **light show** (also in the menu); any pad ends it. **7** plays and stops.

## Samples that fit the project

The Browser's view menu (its view button, next to the search box) has two more filters for samples:

- **Fits the key**: only samples in the chosen key or its relative (A minor and C major share their notes) are listed.
- **Fits the project tempo**: only loops at the project's tempo, or half or double it, are listed; a drum loop may be
  up to 6 % off, a pitched loop (a bass, a lead) only 1 %, because speeding a loop up also moves its pitch.

They read what a file's name says: a key like `Am`, `F#m`, `C major` or `in Eb`, a tempo like `128bpm` or `BPM 90`.
A sample whose name says nothing is always listed.
