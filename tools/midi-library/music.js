// The music: keys, chords, voicing, and a generator for each role. Everything works in sixteenth steps (16 a bar) and
// is decided by a seeded random number, so a build gives the same library every time.
'use strict';

// ---- random -------------------------------------------------------------------------------------------------------

function hash(s) {
  var h = 2166136261 >>> 0;
  for (var i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619) >>> 0; }
  return h;
}
function Rng(seed) {
  var a = (typeof seed === 'string' ? hash(seed) : seed) >>> 0;
  this.next = function () {
    a = (a + 0x6d2b79f5) >>> 0;
    var t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}
Rng.prototype.int = function (n) { return Math.floor(this.next() * n); };
Rng.prototype.pick = function (list) { return list[this.int(list.length)]; };
Rng.prototype.chance = function (p) { return this.next() < p; };
/** Picks from {value: weight} or [[value, weight]]. */
Rng.prototype.weighted = function (pairs) {
  if (!Array.isArray(pairs)) pairs = Object.keys(pairs).map(function (k) { return [k, pairs[k]]; });
  var total = 0; pairs.forEach(function (p) { total += p[1]; });
  var r = this.next() * total;
  for (var i = 0; i < pairs.length; i++) { r -= pairs[i][1]; if (r < 0) return pairs[i][0]; }
  return pairs[pairs.length - 1][0];
};

// ---- keys ---------------------------------------------------------------------------------------------------------

var MODES = {
  major: [0, 2, 4, 5, 7, 9, 11],
  minor: [0, 2, 3, 5, 7, 8, 10],
  dorian: [0, 2, 3, 5, 7, 9, 10],
  phrygian: [0, 1, 3, 5, 7, 8, 10],
  mixolydian: [0, 2, 4, 5, 7, 9, 10]
};
var SHARP = ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B'];

function keyName(tonic, mode) {
  var n = SHARP[tonic];
  if (mode === 'major') return n;
  if (mode === 'minor') return n + 'm';
  return n + ' ' + mode.charAt(0).toUpperCase() + mode.slice(1);
}

function Key(tonic, mode) {
  this.tonic = tonic; this.mode = mode; this.steps = MODES[mode];
  this.pcs = this.steps.map(function (s) { return (s + tonic) % 12; });
  this.name = keyName(tonic, mode);
}
Key.prototype.has = function (note) { return this.pcs.indexOf(((note % 12) + 12) % 12) >= 0; };
/** The pitch class of a scale degree (0-based, any integer). */
Key.prototype.pc = function (degree) { var d = ((degree % 7) + 7) % 7; return this.pcs[d]; };
/** Every note of the key from lo to hi. */
Key.prototype.ladder = function (lo, hi) {
  var out = [];
  for (var n = lo; n <= hi; n++) if (this.has(n)) out.push(n);
  return out;
};
/** A chord on a degree: thirds stacked in the key. size 3 triad, 4 seventh; add9 adds the ninth to a triad. */
Key.prototype.chord = function (degree, size, add9) {
  var pcs = [];
  for (var i = 0; i < size; i++) pcs.push(this.pc(degree + 2 * i));
  if (add9) pcs.push(this.pc(degree + 8));
  return pcs;
};

var ROMAN = ['I', 'II', 'III', 'IV', 'V', 'VI', 'VII'];
/** "vi", "IV", "ii°", "bVII" style name of a degree in a key, by its triad's quality. */
function roman(key, degree) {
  var t = key.chord(degree, 3), third = (t[1] - t[0] + 12) % 12, fifth = (t[2] - t[0] + 12) % 12;
  var flat = (key.pc(degree) - key.tonic + 12) % 12 < MODES.major[degree % 7] ? 'b' : '';
  var r = ROMAN[degree % 7];
  if (third === 3) r = r.toLowerCase();
  if (fifth === 6) r += '°';
  return flat + r;
}

// ---- voicing ------------------------------------------------------------------------------------------------------

/** Close voicings of pcs (root first) lying within lo..hi: every rotation at every octave. */
function voicings(pcs, lo, hi) {
  var out = [], root = pcs[0];
  // the tones in order above the root, so an added ninth sits inside the voicing, not an octave out
  pcs = pcs.slice().sort(function (a, b) { return ((a - root + 12) % 12) - ((b - root + 12) % 12); });
  for (var rot = 0; rot < pcs.length; rot++) {
    var order = pcs.slice(rot).concat(pcs.slice(0, rot));
    for (var base = lo - 12; base <= hi; base++) {
      if (((base % 12) + 12) % 12 !== order[0]) continue;
      var v = [base];
      for (var i = 1; i < order.length; i++) {
        var n = v[i - 1] + 1;
        while (((n % 12) + 12) % 12 !== order[i]) n++;
        v.push(n);
      }
      if (v[0] >= lo && v[v.length - 1] <= hi) out.push(v);
    }
  }
  return out;
}
function distance(a, b) {
  var d = 0;
  for (var i = 0; i < Math.max(a.length, b.length); i++) d += Math.abs((a[Math.min(i, a.length - 1)]) - (b[Math.min(i, b.length - 1)]));
  return d;
}
/** The voicing nearest the last one (or the centre of the range for the first). */
function voiceLead(pcs, lo, hi, prev) {
  var all = voicings(pcs, lo, hi);
  for (var wider = 1; all.length === 0 && wider <= 6; wider++) all = voicings(pcs, lo - wider, hi + wider);
  if (all.length === 0) throw new Error('no voicing of ' + pcs + ' in ' + lo + '..' + hi);
  var centre = (lo + hi) / 2, best = null, bestScore = Infinity;
  all.forEach(function (v) {
    var mean = v.reduce(function (s, n) { return s + n; }, 0) / v.length;
    var score = prev ? distance(v, prev) + Math.abs(mean - centre) * 0.25 : Math.abs(mean - centre);
    if (score < bestScore) { bestScore = score; best = v; }
  });
  return best;
}

// ---- the harmony of a sketch ---------------------------------------------------------------------------------------

/**
 * A progression's four bars as a list of {step, length, degree}: a bar of one chord, or [a, b] for two half bars.
 */
function slotsOf(progression) {
  var out = [];
  progression.forEach(function (bar, i) {
    if (Array.isArray(bar)) {
      out.push({ step: i * 16, length: 8, degree: bar[0] });
      out.push({ step: i * 16 + 8, length: 8, degree: bar[1] });
    } else out.push({ step: i * 16, length: 16, degree: bar });
  });
  return out;
}
function chordAt(slots, step) {
  for (var i = slots.length - 1; i >= 0; i--) if (step >= slots[i].step) return slots[i];
  return slots[0];
}

// ---- patterns as text ----------------------------------------------------------------------------------------------

/** "x--.x..." : onsets with their lengths (a '-' holds the note before it). Repeats to fill `steps`. */
function onsets(pattern, steps) {
  var out = [];
  for (var s = 0; s < steps; s++) {
    var c = pattern[s % pattern.length];
    if (c === '-' || c === '.') continue;
    var len = 1;
    while (s + len < steps && pattern[(s + len) % pattern.length] === '-') len++;
    out.push({ step: s, length: len, mark: c });
  }
  return out;
}

var VEL = { X: 118, x: 100, o: 80, g: 50, R: 100, r: 78 };

// ---- drums ---------------------------------------------------------------------------------------------------------

var DRUM = {
  K: 36, S: 38, C: 39, R: 37, H: 42, O: 46, P: 44, Y: 51, X: 49, T: 50, M: 47, F: 43,
  Z: 70, B: 54, W: 56, G: 63, L: 64, V: 75, N: 60
};

function lanesToNotes(lanes, steps, rng, human) {
  var notes = [];
  Object.keys(lanes).forEach(function (lane) {
    if (!lanes[lane]) return;
    onsets(lanes[lane], steps).forEach(function (o) {
      var v = (VEL[o.mark] || 90) + (human ? rng.int(2 * human + 1) - human : 0);
      // hats and shakers lean on the beat: the off-beat eighths a little softer, the sixteenths between softer still
      if ((lane === 'H' || lane === 'Z' || lane === 'P' || lane === 'Y') && o.mark !== 'X') v -= o.step % 4 === 2 ? 10 : o.step % 2 === 1 ? 16 : 0;
      notes.push({ step: o.step, length: 1, note: DRUM[lane], velocity: clampVel(v) });
    });
  });
  return notes;
}
function clampVel(v) { return Math.max(1, Math.min(127, Math.round(v))); }

function repeatTo(p, steps) { var s = ''; while (s.length < steps) s += p; return s.slice(0, steps); }
function setAt(s, i, c) { return s.slice(0, i) + c + s.slice(i + 1); }
function blank(steps) { return repeatTo('.', steps); }

/** The groove's lanes written out to 64 steps. */
function fullLanes(groove) {
  var out = {};
  Object.keys(groove).forEach(function (k) { out[k] = repeatTo(groove[k], 64); });
  return out;
}

var FILLS = [
  { S: '........x.x.xxxx' },
  { S: '..........xxxxxx', T: '........xx......' },
  { T: '........xx......', M: '..........xx....', F: '............xxxx' },
  { S: '........x..x..x.', F: '..............xx' },
  { S: '........oxoxXxXx' },
  { S: '............xxxx', T: '........x.x.....', M: '.........x.x....' },
  { S: '....x...x.xxx.x.', F: '...............x' },
  { T: '........x..x....', M: '..........x..x..', F: '............x..x', S: '........g.g.g.g.' }
];

/**
 * The drums of a section from a groove. section: Intro | Verse | Chorus | Break | Outro | Fill.
 * style.drums: {grooves, chorusAdd, hat16, intro, crash}.
 */
function drums(style, groove, section, rng) {
  var L = fullLanes(groove), steps = 64, d = style.drums;
  var has = function (k) { return !!L[k]; };
  if (section === 'Fill') {
    var fill = rng.pick(FILLS), bar = {};
    Object.keys(L).forEach(function (k) {
      var keepFirstHalf = k === 'K' || k === 'H' || k === 'P' || k === 'Z' || k === 'Y' || k === 'S' || k === 'C' || k === 'R';
      bar[k] = keepFirstHalf ? L[k].slice(0, 8) + '........' : null;
    });
    Object.keys(fill).forEach(function (k) {
      var first = bar[k] ? bar[k].slice(0, 8) : '........';
      var second = fill[k].slice(8);
      bar[k] = first + second;
    });
    if (rng.chance(0.5) && has('K')) bar.K = setAt(bar.K, 15, '.');
    return { notes: lanesToNotes(bar, 16, rng, d.human), bars: 1 };
  }

  // a variation on the fourth bar: a kick pickup, an open hat, or a snare ghost
  var v = rng.int(3);
  if (section !== 'Intro' && section !== 'Break') {
    if (v === 0 && has('K') && L.K[62] === '.') L.K = setAt(L.K, 62, 'o');
    else if (v === 1 && (has('O') || has('H'))) { L.O = L.O || blank(steps); L.O = setAt(L.O, 62, 'x'); if (has('H')) L.H = setAt(L.H, 62, '.'); }
    else if (v === 2 && has('S') && L.S[63] === '.') L.S = setAt(L.S, 63, 'g');
  }

  if (section === 'Intro') {
    var keep = d.intro || ['H', 'O', 'P', 'Z', 'Y'];
    Object.keys(L).forEach(function (k) { if (keep.indexOf(k) < 0 && k !== 'K') L[k] = null; });
    if (has('K')) L.K = blank(32) + L.K.slice(32);
    if (rng.chance(0.5)) Object.keys(L).forEach(function (k) { if (L[k] && k !== 'K') L[k] = thin(L[k], 0, 16); });
  } else if (section === 'Chorus') {
    if (d.hat16 && has('H')) L.H = repeatTo(rng.pick(d.hat16), 64);
    (d.chorusAdd || []).forEach(function (add) { if (!L[add.lane]) L[add.lane] = repeatTo(add.pattern, 64); });
    L.X = setAt(L.X || blank(steps), 0, 'X');
  } else if (section === 'Break') {
    L.K = null; L.C = null;
    var build = rng.chance(0.6);
    L.S = build ? blank(48) + rng.pick(['x.x.x.x.xxxxxxxx', 'o.o.x.x.xxxxXXXX', '....x...x.x.xxxx']) : null;
    if (L.Y && rng.chance(0.5)) L.Y = null;
    if (rng.chance(0.4) && L.H) L.H = thin(L.H, 0, 64);
  } else if (section === 'Outro') {
    L.X = setAt(L.X || blank(steps), 0, 'X');
    Object.keys(L).forEach(function (k) {
      if (!L[k] || k === 'K' || k === 'H' || k === 'X' || k === 'Y') return;
      L[k] = L[k].slice(0, 32) + blank(32);
    });
    if (L.K && rng.chance(0.5)) L.K = L.K.slice(0, 48) + 'x' + blank(15);
  }
  if (d.crash && section === 'Verse' && rng.chance(0.4)) L.X = setAt(L.X || blank(steps), 0, 'x');
  return { notes: lanesToNotes(L, steps, rng, d.human), bars: 4 };
}
/** Every other hit of a lane from..to taken out (the intro's lighter hats). */
function thin(lane, from, to) {
  var count = 0, s = lane;
  for (var i = from; i < to; i++) if (s[i] !== '.' && s[i] !== '-') { if (count % 2 === 1) s = setAt(s, i, '.'); count++; }
  return s;
}

// ---- bass ----------------------------------------------------------------------------------------------------------

/**
 * Bass from a template: R root, 5 fifth, O octave, 3 third, b seventh, A an approach step to the next chord's root,
 * '-' hold, '.' rest. Upper case at full velocity, the template's lower case ('r', 'o') softer.
 */
function bass(key, slots, template, range, rng, opts) {
  opts = opts || {};
  var notes = [], lo = range[0], hi = range[1];
  function rootOf(slot) {
    var pc = key.pc(slot.degree), n = lo;
    while (((n % 12) + 12) % 12 !== pc) n++;
    if (n > lo + 11) n -= 12;
    return n;
  }
  onsets(repeatTo(template, 64), 64).forEach(function (o) {
    var slot = chordAt(slots, o.step), root = rootOf(slot), m = o.mark.toUpperCase(), n;
    var chord = key.chord(slot.degree, 4);
    if (m === 'R') n = root;
    else if (m === '5') n = nearestPc(root + 7, chord[2], lo, hi);
    else if (m === '3') n = nearestPc(root + 4, chord[1], lo, hi);
    else if (m === 'B') n = nearestPc(root + 10, chord[3], lo, hi);
    else if (m === 'O') n = root + 12 <= hi ? root + 12 : root;
    else if (m === 'A') {
      var next = chordAt(slots, Math.min(63, (Math.floor(o.step / 16) + 1) * 16));
      if (o.step + o.length >= 64) next = slots[0];
      var target = rootOf(next), lad = key.ladder(lo, hi), i = lad.indexOf(target);
      n = i < 0 ? root : (rng.chance(0.5) && i + 1 < lad.length ? lad[i + 1] : (i > 0 ? lad[i - 1] : lad[i + 1]));
    } else n = root;
    // a note never runs over its chord's change
    var len = Math.min(o.length, slot.step + slot.length - o.step);
    if (len < 1) len = 1;
    if (opts.gate && len > 1) len = Math.max(1, len - opts.gate);
    var vel = (o.mark === m ? 104 : 80) + rng.int(9) - 4;
    notes.push({ step: o.step, length: len, note: n, velocity: clampVel(vel) });
  });
  return notes;
}
function nearestPc(want, pc, lo, hi) {
  var best = null;
  for (var n = lo; n <= hi; n++) if (((n % 12) + 12) % 12 === pc && (best === null || Math.abs(n - want) < Math.abs(best - want))) best = n;
  return best;
}

// ---- chords and pads -----------------------------------------------------------------------------------------------

/** Chords struck on a rhythm ('X' strikes, '-' holds, '.' rests), voice-led bar to bar. */
function chords(key, slots, rhythm, range, size, add9, rng, velocity) {
  var notes = [], prev = null, voiced = {};
  slots.forEach(function (s, i) { prev = voiceLead(key.chord(s.degree, size, add9 && size === 3), range[0], range[1], prev); voiced[i] = prev; });
  onsets(repeatTo(rhythm, 64), 64).forEach(function (o) {
    var idx = 0;
    for (var i = slots.length - 1; i >= 0; i--) if (o.step >= slots[i].step) { idx = i; break; }
    var slot = slots[idx], len = Math.min(o.length, slot.step + slot.length - o.step);
    var vel = (VEL[o.mark] || 96) - 8 + rng.int(9) - 4;
    voiced[idx].forEach(function (n) { notes.push({ step: o.step, length: Math.max(1, len), note: n, velocity: clampVel((velocity || vel)) }); });
  });
  return notes;
}

/** A pad: each chord held for its slot, the root below an upper voicing; a note common to the next chord is tied over. */
function pad(key, slots, range, size, rng) {
  var notes = [], prev = null, held = {};
  slots.forEach(function (s, i) {
    var pcs = key.chord(s.degree, size, size === 3 && rng.chance(0.3));
    var upper = voiceLead(pcs.slice(1).concat([pcs[0]]), range[0] + 12, range[1], prev);
    prev = upper;
    var root = nearestPc(upper[0] - 7, pcs[0], range[0], upper[0] - 3);
    var set = upper.slice(); if (root !== null && set.indexOf(root) < 0) set.unshift(root);
    var nextHeld = {};
    set.forEach(function (n) {
      if (held[n]) { held[n].length += s.length; nextHeld[n] = held[n]; }
      else { var note = { step: s.step, length: s.length, note: n, velocity: clampVel(72 + rng.int(9) - 4) }; notes.push(note); nextHeld[n] = note; }
    });
    held = nextHeld;
  });
  return notes;
}

// ---- melody --------------------------------------------------------------------------------------------------------

/**
 * A four-bar line: a one-bar motif, answered and varied over the chords, ending on a chord tone.
 * rhythms: one-bar onset patterns ('x' a note, '-' hold, '.' rest). form: 'AABC', 'ABAC' or 'AAAC' style.
 */
function melody(key, slots, rhythms, range, rng, opts) {
  opts = opts || {};
  var lad = key.ladder(range[0], range[1]), notes = [];
  var centre = Math.round((range[0] + range[1]) / 2);
  function idxNear(note) { var b = 0; lad.forEach(function (n, i) { if (Math.abs(n - note) < Math.abs(lad[b] - note)) b = i; }); return b; }
  function chordTonesAt(step) { var s = chordAt(slots, step); return key.chord(s.degree, opts.sevenths ? 4 : 3); }
  function snapToChord(i, step) {
    var pcs = chordTonesAt(step);
    if (pcs.indexOf(lad[i] % 12) >= 0) return i;
    var up = i + 1 < lad.length && pcs.indexOf(lad[i + 1] % 12) >= 0, down = i > 0 && pcs.indexOf(lad[i - 1] % 12) >= 0;
    if (up && down) return rng.chance(0.5) ? i + 1 : i - 1;
    if (up) return i + 1;
    if (down) return i - 1;
    return i;
  }
  function clampI(i) { return Math.max(0, Math.min(lad.length - 1, i)); }
  /** The chord tone nearest i, looking first the way the line is moving. */
  function towardChord(i, step, dir) {
    var pcs = chordTonesAt(step);
    var order = dir < 0 ? [0, -1, 1, -2, 2] : [0, 1, -1, 2, -2];
    for (var k = 0; k < order.length; k++) { var j = i + order[k]; if (j >= 0 && j < lad.length && pcs.indexOf(lad[j] % 12) >= 0) return j; }
    return i;
  }

  // the motif: its rhythm and its moves in scale steps
  var rhythm = onsets(rng.pick(rhythms), 16);
  var moves = rhythm.map(function (_, k) {
    if (k === 0) return 0;
    return rng.weighted([[1, 3], [-1, 3], [2, 2.5], [-2, 2.5], [0, 0.6], [3, 1.2], [-3, 1.2], [4, 0.6], [-4, 0.6], [5, 0.3], [-5, 0.3]]);
  });
  var answer = onsets(rng.pick(rhythms), 16);
  var answerMoves = answer.map(function (_, k) { return k === 0 ? 0 : rng.weighted([[1, 3], [-1, 3], [2, 2], [-2, 2], [0, 1], [-3, 1], [3, 1]]); });
  var ending = onsets(rng.pick(opts.endings || ['x---------------', 'x.x.x-----------', 'x...x-------....', 'x.x...x---------', '..x.x.x---------']), 16);
  var form = opts.form || rng.pick(['AABC', 'ABAC', 'AA2BC', 'AAAC']);
  var parts = form === 'AA2BC' ? ['A', 'A2', 'B', 'C'] : form.split('');

  var last = idxNear(centre + (opts.lift || 0));
  parts.forEach(function (p, bar) {
    var base = bar * 16, r, mv;
    if (p === 'A' || p === 'A2') { r = rhythm; mv = moves.slice(); }
    else if (p === 'B') { r = answer; mv = answerMoves; }
    else { r = ending; mv = r.map(function (_, k) { return k === 0 ? 0 : rng.pick([-1, 1, -2]); }); }
    if (p === 'A2' && mv.length > 2) { mv[mv.length - 1] = -mv[mv.length - 1] || 1; }
    // the bar starts on the chord tone nearest where the line is
    var i = snapToChord(clampI(last + (bar === 0 ? 0 : rng.pick([-1, 0, 1]))), base);
    r.forEach(function (o, k) {
      var prev = i, dir = mv[k] > 0 ? 1 : mv[k] < 0 ? -1 : 0;
      if (k > 0) i = clampI(i + mv[k]);
      var step = base + o.step, strong = o.step % 4 === 0;
      if (strong || o.length >= 4) i = towardChord(i, step, dir);
      // a move that was meant to go somewhere does not land back where it started
      if (k > 0 && dir !== 0 && i === prev) i = towardChord(clampI(i + dir), step, dir);
      if (k > 0 && dir === 0 && i === prev && notes.length > 1 && notes[notes.length - 2].note === lad[i]) i = towardChord(clampI(i + (rng.chance(0.5) ? 1 : -1)), step, 0);
      if (p === 'C' && k === r.length - 1) {
        // the line comes home: the chord's root or third, held
        var pcs = chordTonesAt(step), best = i;
        for (var d = 0; d < 4; d++) {
          if (i - d >= 0 && (lad[i - d] % 12 === pcs[0] || lad[i - d] % 12 === pcs[1])) { best = i - d; break; }
          if (i + d < lad.length && (lad[i + d] % 12 === pcs[0] || lad[i + d] % 12 === pcs[1])) { best = i + d; break; }
        }
        i = best;
      }
      var len = Math.min(o.length, 64 - step);
      if (opts.gate && len > 1) len -= 1;
      var vel = (strong ? 100 : 86) + rng.int(11) - 5;
      notes.push({ step: step, length: Math.max(1, len), note: lad[i], velocity: clampVel(vel) });
    });
    last = i;
  });
  // a short note followed by a rest carries on into it (up to a beat), unless the style plays its leads clipped
  if (!opts.gate) notes.forEach(function (n, k) {
    var gap = (k + 1 < notes.length ? notes[k + 1].step : 64) - n.step;
    if (n.length < gap) n.length = Math.max(n.length, Math.min(gap, 4));
  });
  return notes;
}

/** An arpeggio of each chord: order 'up', 'down', 'updown', 'pedal' (root between each), at a rate in steps. */
function arp(key, slots, range, order, rate, gate, size, rng) {
  var notes = [], prev = null;
  slots.forEach(function (s) {
    var v = voiceLead(key.chord(s.degree, size), range[0], range[1] - 12, prev); prev = v;
    var tones = v.concat([v[0] + 12]), seq;
    if (order === 'up') seq = tones;
    else if (order === 'down') seq = tones.slice().reverse();
    else if (order === 'updown') seq = tones.concat(tones.slice(1, -1).reverse());
    else if (order === 'pedal') { seq = []; tones.slice(1).forEach(function (t) { seq.push(v[0], t); }); }
    else seq = [tones[0], tones[2], tones[1], tones[3] || tones[0] + 12];
    var k = 0;
    for (var st = s.step; st < s.step + s.length; st += rate) {
      var len = Math.max(1, Math.min(rate - (gate ? 0 : 0), s.step + s.length - st));
      if (gate && rate > 1) len = Math.max(1, Math.round(rate * gate));
      var accent = (st - s.step) % 4 === 0;
      notes.push({ step: st, length: len, note: seq[k % seq.length], velocity: clampVel((accent ? 98 : 80) + rng.int(9) - 4) });
      k++;
    }
  });
  return notes;
}

/** Bells: a few long notes a bar on chord tones high up, moving by the smallest step. */
function bells(key, slots, rhythms, range, rng) {
  var notes = [], last = null, rhythm = rng.pick(rhythms);
  onsets(repeatTo(rhythm, 64), 64).forEach(function (o) {
    var s = chordAt(slots, o.step), pcs = key.chord(s.degree, rng.chance(0.3) ? 4 : 3);
    var cands = [];
    for (var n = range[0]; n <= range[1]; n++) if (pcs.indexOf(n % 12) >= 0) cands.push(n);
    var want = last === null ? Math.round((range[0] + range[1]) / 2) : last + rng.pick([-3, -1, 0, 2, 4]);
    var pick = cands.reduce(function (b, n) { return Math.abs(n - want) < Math.abs(b - want) ? n : b; }, cands[0]);
    if (pick === last && cands.length > 1 && rng.chance(0.6)) pick = cands[(cands.indexOf(pick) + rng.pick([1, cands.length - 1])) % cands.length];
    last = pick;
    notes.push({ step: o.step, length: Math.min(o.length, 64 - o.step), note: pick, velocity: clampVel(84 + rng.int(17) - 8) });
  });
  return notes;
}

module.exports = {
  Rng: Rng, hash: hash, Key: Key, MODES: MODES, roman: roman, keyName: keyName, slotsOf: slotsOf, onsets: onsets,
  voiceLead: voiceLead, drums: drums, bass: bass, chords: chords, pad: pad, melody: melody, arp: arp, bells: bells,
  DRUM: DRUM, repeatTo: repeatTo
};
