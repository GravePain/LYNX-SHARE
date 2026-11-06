// Builds the library: for every style, a number of sketches (a key, a tempo, a verse and a chorus progression), and
// for each sketch the parts of its sections, one part a file. `node build.js [out folder] [sketches per style]`
//
// Files are named the way the app's pattern importer reads them (src/core/MidiPatterns.cpp): the section and role words in
// the file name and track name, the style as the folder (the dataset's set), drums on channel 10 with GM notes.
'use strict';
var fs = require('fs'), path = require('path');
var M = require('./music.js'), SMF = require('./smf.js'), S = require('./styles.js');

var out = process.argv[2] || path.join(__dirname, '..', '..', 'resources', 'midi-library', 'GravePain Originals');
var perStyle = +(process.argv[3] || 16);
var STEP = SMF.TPB / 4;

var seen = {}, manifest = [], counts = {};

// a fresh build: the last build's files go first (only .mid files in the styles' own folders)
S.STYLES.forEach(function (st) {
  var dir = path.join(out, st.name);
  if (fs.existsSync(dir)) fs.readdirSync(dir).forEach(function (f) { if (/\.mid$/.test(f)) fs.unlinkSync(path.join(dir, f)); });
});

function ticks(notes, swing) {
  return notes.map(function (n) {
    var t = n.step * STEP + (n.step % 2 === 1 ? Math.round(swing * STEP) : 0);
    var end = (n.step + n.length) * STEP + ((n.step + n.length) % 2 === 1 ? Math.round(swing * STEP) : 0);
    return { tick: t, length: Math.max(1, end - t - 1), note: n.note, velocity: n.velocity };
  });
}
function signature(role, notes) {
  return role + '|' + notes.slice().sort(function (a, b) { return a.step - b.step || a.note - b.note; })
    .map(function (n) { return n.step + ':' + n.note + ':' + n.length; }).join(' ');
}

/** Same pitch overlapping itself (a stuck or doubled note): shortens the first to end where the next starts. */
function untangle(notes) {
  var byNote = {};
  notes.forEach(function (n) { (byNote[n.note] = byNote[n.note] || []).push(n); });
  var keep = [];
  Object.keys(byNote).forEach(function (k) {
    var list = byNote[k].sort(function (a, b) { return a.step - b.step; });
    for (var i = 0; i < list.length; i++) {
      if (i + 1 < list.length && list[i + 1].step === list[i].step) continue;   // the same note twice at once: one
      if (i + 1 < list.length && list[i].step + list[i].length > list[i + 1].step) list[i].length = list[i + 1].step - list[i].step;
      keep.push(list[i]);
    }
  });
  return keep.sort(function (a, b) { return a.step - b.step || a.note - b.note; });
}

function emit(style, sketch, section, role, notes, bars, info, program) {
  notes = untangle(notes);
  if (notes.length === 0) return false;
  var sig = signature(role, notes);
  if (seen[sig]) return false;
  seen[sig] = true;
  var isDrums = role === 'Drums';
  var name = style.name + ' ' + String(sketch.n).padStart(2, '0') + ' ' + section + ' ' + role + (isDrums ? '' : ' ' + sketch.key.name);
  var file = path.join(out, style.name, name + '.mid');
  var buf = SMF.write({ bpm: sketch.bpm, title: 'GravePain Originals', bars: bars },
    { name: role, channel: isDrums ? 9 : role === 'Bass' ? 1 : role === 'Chords' ? 2 : role === 'Pad' ? 3 : role === 'Lead' ? 4 : role === 'Pluck' ? 5 : 6,
      program: isDrums ? -1 : program, notes: ticks(notes, style.swing || 0) });
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, buf);
  manifest.push(Object.assign({ file: path.relative(out, file).replace(/\\/g, '/'), style: style.name, sketch: sketch.n, section: section,
    role: role, key: isDrums ? '' : sketch.key.name, bpm: sketch.bpm, bars: bars, notes: notes.length }, info || {}));
  counts[role] = (counts[role] || 0) + 1;
  return true;
}

function progName(key, prog) {
  return prog.map(function (b) { return Array.isArray(b) ? b.map(function (d) { return M.roman(key, d); }).join('-') : M.roman(key, b); }).join(' ');
}

for (var si = 0; si < S.STYLES.length; si++) {
  var style = S.STYLES[si];
  for (var k = 1; k <= perStyle; k++) {
    var rng = new M.Rng(style.name + '#' + k);
    var mode = rng.weighted(style.modes), tonic = rng.int(12);
    var key = new M.Key(tonic, mode);
    var bpm = style.bpm[0] + rng.int(style.bpm[1] - style.bpm[0] + 1);
    var pool = S.PROGS[mode], verseProg = rng.pick(pool), chorusProg = rng.pick(pool);
    for (var tries = 0; tries < 5 && JSON.stringify(chorusProg) === JSON.stringify(verseProg); tries++) chorusProg = rng.pick(pool);
    var sketch = { n: k, key: key, bpm: bpm };
    var verse = M.slotsOf(verseProg), chorus = M.slotsOf(chorusProg);
    var vInfo = { progression: progName(key, verseProg) }, cInfo = { progression: progName(key, chorusProg) };
    var gm = function (name) { return S.GM[name]; };

    // drums: one groove for the sketch, every section built from it
    // (two sketches a groove: past that the drums would be the same grooves again with small changes)
    var groove = style.drums.grooves[(k - 1) % style.drums.grooves.length];
    if (k <= style.drums.grooves.length * 2) ['Intro', 'Verse', 'Chorus', 'Break', 'Fill', 'Outro'].forEach(function (section) {
      var d = M.drums(style, groove, section, new M.Rng(style.name + k + section));
      emit(style, sketch, section, 'Drums', d.notes, d.bars, null, -1);
    });

    // bass: a template for the verse, another for the chorus
    var bt = style.bass.slice(), bVerse = rng.pick(bt), bChorus = rng.pick(bt.filter(function (t) { return t !== bVerse; }));
    emit(style, sketch, 'Verse', 'Bass', M.bass(key, verse, bVerse, style.bassRange, rng, { gate: style.bassGate }), 4, vInfo, gm(style.bassProgram));
    emit(style, sketch, 'Chorus', 'Bass', M.bass(key, chorus, bChorus, style.bassRange, rng, { gate: style.bassGate }), 4, cInfo, gm(style.bassProgram));

    // chords
    var cr = style.chordRhythms, cVerse = rng.pick(cr), cChorus = rng.pick(cr.length > 1 ? cr.filter(function (t) { return t !== cVerse; }) : cr);
    var size = rng.chance(0.7) ? style.chordSize : 3;
    emit(style, sketch, 'Verse', 'Chords', M.chords(key, verse, cVerse, style.chordRange, size, style.add9 && rng.chance(0.5), rng), 4, vInfo, gm(style.chordProgram));
    emit(style, sketch, 'Chorus', 'Chords', M.chords(key, chorus, cChorus, style.chordRange, size, style.add9 && rng.chance(0.5), rng), 4, cInfo, gm(style.chordProgram));
    if (rng.chance(0.4)) emit(style, sketch, 'Intro', 'Chords', M.chords(key, verse, 'X---------------', style.chordRange, size, false, rng), 4, vInfo, gm(style.chordProgram));

    // pads: the intro and the break
    var breakSlots = rng.chance(0.5) ? verse : chorus, breakInfo = breakSlots === verse ? vInfo : cInfo;
    emit(style, sketch, 'Intro', 'Pad', M.pad(key, verse, [45, 84], rng.chance(0.5) ? 4 : 3, rng), 4, vInfo, gm(style.padProgram));
    emit(style, sketch, 'Break', 'Pad', M.pad(key, breakSlots, [45, 84], rng.chance(0.5) ? 4 : 3, rng), 4, breakInfo, gm(style.padProgram));

    // leads: verse and chorus; the chorus sits higher and busier
    var lr = style.leadRange;
    emit(style, sketch, 'Verse', 'Lead', M.melody(key, verse, style.lead, [lr[0], lr[1] - 3], rng, { gate: style.leadGate, sevenths: size === 4 }), 4, vInfo, gm(style.leadProgram));
    emit(style, sketch, 'Chorus', 'Lead', M.melody(key, chorus, style.leadChorus, [lr[0] + 3, lr[1]], rng, { gate: style.leadGate, lift: 3, sevenths: size === 4 }), 4, cInfo, gm(style.leadProgram));

    // a pluck arpeggio and bells
    var a = style.arp, order = rng.pick(a.orders), rate = rng.pick(a.rates);
    var arpSection = rng.chance(0.5) ? 'Chorus' : 'Verse', arpSlots = arpSection === 'Chorus' ? chorus : verse;
    emit(style, sketch, arpSection, 'Pluck', M.arp(key, arpSlots, [55, 88], order, rate, rate > 1 ? 0.5 : 0, rng.chance(0.5) ? 4 : 3, rng), 4,
      Object.assign({ order: order }, arpSection === 'Chorus' ? cInfo : vInfo), gm(a.program));
    emit(style, sketch, 'Break', 'Bell', M.bells(key, breakSlots, style.bellRhythms, [72, 96], rng), 4, breakInfo, gm(style.bellProgram));
  }
}

fs.writeFileSync(path.join(out, 'manifest.json'), JSON.stringify({ built: 'GravePain Originals', files: manifest.length, parts: counts, list: manifest }, null, 1));
console.log(manifest.length + ' files in ' + out);
console.log(JSON.stringify(counts));
