// Reads every file of the library back from disk and checks it. `node validate.js [library folder]`
// Fails (exit 1) on any file that breaks a rule; prints the first few of each.
'use strict';
var fs = require('fs'), path = require('path');
var SMF = require('./smf.js'), M = require('./music.js');

var root = process.argv[2] || path.join(__dirname, '..', '..', 'resources', 'midi-library', 'GravePain Originals');
var manifest = JSON.parse(fs.readFileSync(path.join(root, 'manifest.json'), 'utf8'));

// the importer's words (src/core/MidiPatterns.cpp roleWords and sectionOf), in its order
var ROLE_WORDS = [
  [/\b(kick|bd|bass ?drum|bassdrum)\b/i, 'Kick'], [/\b(snare|sd|rim|rimshot|side ?stick)\b/i, 'Snare'],
  [/\b(clap|claps|handclap)\b/i, 'Clap'], [/\b(hat|hats|hh|hihat|hi-hat|hi ?hat|open ?hat|closed ?hat)\b/i, 'Hat'],
  [/\b(ride|ride ?bell)\b/i, 'Ride'], [/\b(crash|cymbal|china|splash|gong)\b/i, 'Crash'],
  [/\b(tom|toms|floor ?tom|timp\w*|timphany)\b/i, 'Tom'],
  [/\b(conga|bongo|shaker|tamb\w*|perc\w*|triangle|agogo|cowbell|maracas|stick|cabasa|guiro|clave|woodblock|cajon|djembe|tabla|insects)\b/i, 'Perc'],
  [/\b(bass|sub|808)\b/i, 'Bass'], [/\b(pad|pads|strings|string ensemble|choir|ensemble|wash)\b/i, 'Pad'],
  [/\b(drone|drones)\b/i, 'Drone'], [/\b(bell|bells|chime|chimes|glock\w*|mallet|marimba|vibes|vibraphone|celesta|kalimba|music ?box)\b/i, 'Bell'],
  [/\b(pluck|plucks|comb|harp|pizz\w*)\b/i, 'Pluck'],
  [/\b(organ|ep|e-?piano|piano|keys|clavi\w*|chord|chords|stab|stabs|guitar|gtr|rhodes|wurli\w*|mellotron|progression|progressions)\b/i, 'Chords'],
  [/\b(fx|sweep|noise|zap|riser|clang|impact|hit|hits|whoosh|sfx|effect)\b/i, 'FX'],
  [/\b(lead|flute|melod\w*|synth|arp|arpeggio|tones|solo|brass|horn|horns|sax|trumpet|violin|cello|whistle|voice|vox|theme|hook|line)\b/i, 'Lead']
];
var SECTION = /\b(intro|verse|chorus|bridge|break|breakdown|fill|outro|ending)\b/i;
function roleOf(words) { for (var i = 0; i < ROLE_WORDS.length; i++) if (ROLE_WORDS[i][0].test(words)) return ROLE_WORDS[i][1]; return ''; }

var GM_DRUMS = {}; Object.keys(M.DRUM).forEach(function (k) { GM_DRUMS[M.DRUM[k]] = true; });
var RANGE = { Bass: [24, 64], Chords: [43, 86], Pad: [38, 90], Lead: [55, 92], Pluck: [48, 94], Bell: [65, 100] };
var MONO = { Bass: true, Lead: true, Bell: true, Pluck: true };

function parseKey(name) {
  var m = /^([A-G][b#]?)(m)?(?: (Dorian|Phrygian|Mixolydian))?$/.exec(name);
  if (!m) return null;
  var tonic = ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B'].indexOf(m[1]);
  return new M.Key(tonic, m[3] ? m[3].toLowerCase() : m[2] ? 'minor' : 'major');
}

var fails = {}, failCount = 0, sigs = {}, checked = 0;
function fail(rule, file, what) {
  failCount++;
  (fails[rule] = fails[rule] || []).push(file + (what ? ': ' + what : ''));
}

manifest.list.forEach(function (entry) {
  var file = entry.file, full = path.join(root, file), f;
  try { f = SMF.read(fs.readFileSync(full)); } catch (e) { fail('reads', file, e.message); return; }
  checked++;
  if (f.format !== 1 || f.tpb !== SMF.TPB) fail('header', file, 'format ' + f.format + ' tpb ' + f.tpb);
  f.problems.forEach(function (p) { fail('notes pair up', file, p); });
  if (f.notes.length === 0) fail('has notes', file);
  var drums = entry.role === 'Drums';
  var channels = {}; f.notes.forEach(function (n) { channels[n.channel] = true; });
  if (Object.keys(channels).length !== 1) fail('one part', file, Object.keys(channels).join(','));
  if (drums && !channels[9]) fail('drums on channel 10', file);
  if (!drums && channels[9]) fail('not on channel 10', file);

  // the importer reads the section and role from the file name and track names
  var words = path.basename(file, '.mid') + ' ' + f.names.join(' ');
  var sec = SECTION.exec(words);
  if (!sec || sec[1].toLowerCase() !== entry.section.toLowerCase()) fail('section word', file, sec ? sec[1] : 'none');
  if (!drums && roleOf(words) !== entry.role) fail('role word', file, 'reads as ' + (roleOf(words) || 'nothing'));
  if (drums && roleOf(path.basename(file, '.mid')) !== '') fail('drum file name says no instrument', file, roleOf(path.basename(file, '.mid')));

  var endTick = entry.bars * 4 * SMF.TPB, step = SMF.TPB / 4;
  var key = drums ? null : parseKey(entry.key);
  if (!drums && !key) fail('key name', file, entry.key);
  f.notes.forEach(function (n) {
    if (n.length <= 0) fail('length', file, 'note ' + n.note + ' at ' + n.tick);
    if (n.tick + n.length > endTick) fail('within its bars', file, 'note ' + n.note + ' ends at ' + (n.tick + n.length));
    if (n.velocity < 1 || n.velocity > 127) fail('velocity', file, n.velocity);
    if (n.tick % step >= step / 2) fail('lands on its sixteenth', file, 'tick ' + n.tick);
    if (drums && !GM_DRUMS[n.note]) fail('GM drum note', file, n.note);
    if (!drums && key && !key.has(n.note)) fail('in key', file, n.note + ' not in ' + key.name);
    if (!drums && RANGE[entry.role] && (n.note < RANGE[entry.role][0] || n.note > RANGE[entry.role][1])) fail('range', file, entry.role + ' ' + n.note);
  });
  if (MONO[entry.role]) {
    for (var i = 1; i < f.notes.length; i++)
      if (f.notes[i].tick < f.notes[i - 1].tick + f.notes[i - 1].length) { fail('one note at a time', file, 'at ' + f.notes[i].tick); break; }
  }
  if (entry.role === 'Lead') {
    for (var j = 1; j < f.notes.length; j++)
      if (Math.abs(f.notes[j].note - f.notes[j - 1].note) > 12) { fail('no leap over an octave', file, f.notes[j - 1].note + '->' + f.notes[j].note); break; }
  }
  if (!drums && (entry.role === 'Chords' || entry.role === 'Pad')) {
    var at = {}; f.notes.forEach(function (n) { at[n.tick] = (at[n.tick] || 0) + 1; });
    Object.keys(at).forEach(function (t) { if (at[t] > 6) fail('six notes at most at once', file, at[t] + ' at ' + t); });
  }
  // the importer's signature: no two files the same part
  var sig = entry.role + '|' + f.notes.map(function (n) { return Math.round(n.tick / step) + ':' + n.note + ':' + Math.max(1, Math.round(n.length / step)); }).join(' ');
  if (sigs[sig]) fail('no duplicates', file, 'same as ' + sigs[sig]); else sigs[sig] = file;
});

var onDisk = 0;
(function walk(d) { fs.readdirSync(d).forEach(function (n) { var p = path.join(d, n); if (fs.statSync(p).isDirectory()) walk(p); else if (/\.mid$/.test(n)) onDisk++; }); })(root);
if (onDisk !== manifest.list.length) fail('manifest lists every file', root, onDisk + ' on disk, ' + manifest.list.length + ' listed');

Object.keys(fails).forEach(function (rule) {
  console.log('FAIL ' + rule + ' (' + fails[rule].length + ')');
  fails[rule].slice(0, 4).forEach(function (l) { console.log('  ' + l); });
});
console.log(checked + ' files checked, ' + failCount + ' problems');
process.exit(failCount ? 1 : 0);
