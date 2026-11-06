// The styles: tempo, keys, progressions, and the rhythms each part is built on. All written here for this library.
//
// Drum lanes (16 or 32 steps, repeated to four bars): K kick, S snare, C clap, R rim (side stick), H closed hat,
// O open hat, P pedal hat, Y ride, X crash, T M F high/mid/floor tom, Z shaker, B tambourine, W cowbell, G L high/low
// conga, V claves, N high bongo. Marks: X accent, x hit, o softer, g ghost, '.' rest.
// Progressions are scale degrees from 0 (the tonic), a bar each, or [a, b] for two chords in a bar.
'use strict';

var minorProgs = [
  [0, 5, 2, 6], [0, 6, 5, 6], [0, 3, 6, 2], [0, 5, 3, 4], [0, 3, 0, 6], [5, 6, 0, 0], [0, 2, 5, 6],
  [0, 6, 3, 5], [0, 3, 5, 6], [5, 3, 0, 6], [0, 0, 3, 5], [3, 5, 0, 6], [0, 4, 5, 3], [0, [5, 6], 0, [3, 6]]
];
var majorProgs = [
  [0, 4, 5, 3], [0, 5, 3, 4], [5, 3, 0, 4], [0, 3, 5, 4], [3, 4, 0, 5], [0, 3, 4, 3], [0, 2, 3, 4],
  [3, 0, 4, 5], [0, 5, 1, 4], [1, 4, 0, 5], [0, [3, 4], 5, [3, 4]], [3, 4, 2, 5], [0, 4, 3, 3], [5, 4, 3, 4]
];
var dorianProgs = [[0, 3, 0, 3], [0, 6, 3, 0], [0, 1, 0, 3], [0, 3, 6, 3], [1, 4, 0, 0], [0, [3, 6], 0, [3, 4]]];
var phrygianProgs = [[0, 1, 0, 1], [0, 6, 1, 0], [0, 0, 1, 6], [0, 5, 1, 0]];
var mixoProgs = [[0, 6, 3, 0], [0, 3, 6, 0], [0, 4, 6, 3], [6, 3, 0, 0]];

var PROGS = { minor: minorProgs, major: majorProgs, dorian: dorianProgs, phrygian: phrygianProgs, mixolydian: mixoProgs };

// GM programs
var GM = { piano: 0, ep: 4, clav: 7, glock: 9, vibes: 11, marimba: 12, organ: 16, nylon: 24, cleanGtr: 27, muteGtr: 28,
  overdrive: 29, fingerBass: 33, pickBass: 34, fretless: 35, synthBass1: 38, synthBass2: 39, strings: 48, synthStrings: 50,
  sqLead: 80, sawLead: 81, padWarm: 89, padPoly: 90, padHalo: 94, padSweep: 95, pluckHarp: 46, celesta: 8, kalimba: 108,
  brass: 61, synBrass: 62, tubular: 14 };

var STYLES = [
  {
    name: 'House', bpm: [122, 126], modes: { minor: 3, dorian: 2, major: 1 }, swing: 0.08,
    drums: {
      human: 4, hat16: ['x.o.x.o.x.o.x.o.', 'xoxoxoxoxoxoxoxo'.replace(/o/g, 'g')],
      chorusAdd: [{ lane: 'Z', pattern: 'oxoooxoooxoooxoo' }, { lane: 'B', pattern: '....x.......x...' }],
      grooves: [
        { K: 'x...x...x...x...', C: '....x.......x...', O: '..x...x...x...x.', H: 'o...o...o...o...' },
        { K: 'x...x...x...x...', C: '....x.......x...', O: '..x...x...x...x.', Z: 'g.o.g.o.g.o.g.og' },
        { K: 'x...x...x...x...', S: '....o.......o..g', C: '....x.......x...', H: 'g.x.g.x.g.x.g.xg' },
        { K: 'x...x...x...x...', C: '....x.......x...', O: '..x...x...x...x.', G: '......o.....o.o.', L: 'o.........o.....' }
      ]
    },
    bass: ['..R...R...R...R.', '..R.-.R...O...R.', 'R..R..R...R.R..A', '..RR..R...R..R..', '..R...5...R...O.', '..R-..R-..O-..R-'],
    bassRange: [36, 55], bassProgram: 'synthBass1',
    chordRhythms: ['..X...X...X..X..', '..X-..X-..X-..X-', 'X.....X.....X...', '...X..X....X..X.', 'X--.X--.X--.X--.'],
    chordSize: 4, add9: true, chordRange: [55, 76], chordProgram: 'ep',
    lead: ['x.x...x...x.x...', '..x.x...x.x..x..', 'x--.x.x.x--.....', '..x-x-..x...x...'],
    leadChorus: ['x.x.x.x.x.x.x...', 'x.xx.x.xx.x.x...', '..x.xx.x..x.xx..'],
    leadRange: [62, 84], leadProgram: 'sawLead',
    arp: { orders: ['up', 'pedal', 'updown'], rates: [2, 1], program: 'pluckHarp' },
    bellRhythms: ['x-------x-------', 'x-----x-----x---', '..x-------x-----'], bellProgram: 'marimba',
    padProgram: 'padWarm'
  },
  {
    name: 'Techno', bpm: [128, 134], modes: { minor: 3, phrygian: 2, dorian: 1 }, swing: 0,
    drums: {
      human: 3, hat16: ['xoxoxoxoxoxoxoxo', 'x.xxx.xxx.xxx.xx'],
      chorusAdd: [{ lane: 'Y', pattern: '..x...x...x...x.' }, { lane: 'R', pattern: '...x..x....x..x.' }],
      grooves: [
        { K: 'x...x...x...x...', H: 'g.x.g.x.g.x.g.x.', C: '....x.......x...' },
        { K: 'x...x...x...x...', O: '..x...x...x...x.', R: '...x..x..x....x.' },
        { K: 'x...x...x...x..o', H: 'xoxoxoxoxoxoxoxo', V: '..x....x..x.....' },
        { K: 'x...x...x...x...', Y: '..x...x...x...x.', C: '....x.......x...', W: 'x..x..x...x..x..' }
      ]
    },
    bass: ['..R...R...R...R.', '.RR..RR..RR..RR.', '..RR.R..R.RR.R..', 'RRRRRRRRRRRRRRRR'.replace(/R/g, 'r'), '..r.r..r..r.r..r', 'R.......R..R....'],
    bassRange: [33, 50], bassProgram: 'synthBass2',
    chordRhythms: ['X-..............', '..X.....X.....X.', 'X.....X.....X...', '...X.......X....'],
    chordSize: 3, add9: false, chordRange: [52, 72], chordProgram: 'synthStrings',
    lead: ['x.x.x.x.x.x.x.x.', 'x..x..x..x..x...', 'x.xx.xx.x.xx.xx.', '..x..x..x..x..x.'],
    leadChorus: ['xxxxxxxxxxxxxxxx', 'x.xxx.xxx.xxx.xx', 'xx.xx.xx.xx.xx.x'],
    leadRange: [57, 79], leadProgram: 'sqLead', leadGate: 1,
    arp: { orders: ['up', 'down', 'pedal'], rates: [1, 2], program: 'sqLead' },
    bellRhythms: ['x-------........', 'x-----------....', '....x-------....'], bellProgram: 'tubular',
    padProgram: 'padSweep'
  },
  {
    name: 'HipHop', bpm: [86, 94], modes: { minor: 3, dorian: 2, major: 1 }, swing: 0.3,
    drums: {
      human: 7, hat16: ['x.o.x.o.x.o.x.oo', 'x.xox.x.x.xox.x.'],
      chorusAdd: [{ lane: 'B', pattern: '....x.......x...' }],
      intro: ['H', 'R', 'Z'],
      grooves: [
        { K: 'x......x..x.....', S: '....x.......x...', H: 'x.o.x.o.x.o.x.o.' },
        { K: 'x.......x.x..... x......x.x.x....'.replace(' ', ''), S: '....x.......x...', H: 'x.x.x.x.x.x.x.x.' },
        { K: 'x..x......x..x..', S: '....x.......x..g', H: 'x.o.x.o.xoo.x.o.' },
        { K: 'x.....x...x.....', S: '....x..g....x...', H: 'x.x.x.xxx.x.x.x.', O: '..............x.' }
      ]
    },
    bass: ['R-----..R---5-..', 'R--.....R-.R....', 'R---..R-....A...', 'R-..R-....R-..O.', 'R-------R---b---'],
    bassRange: [33, 52], bassProgram: 'fingerBass',
    chordRhythms: ['X---------------', 'X-------X-------', 'X-----X-------..', 'X--.....X--.X--.'],
    chordSize: 4, add9: true, chordRange: [52, 74], chordProgram: 'ep',
    lead: ['x-..x.x.x---....', '..x.x.x-x-......', 'x.x-..x.x-..x...', 'x---x-x-........'],
    leadChorus: ['x.x.x.x-x.x.x---', 'x.xx..x.x.xx..x.', '..x.x.x.x.x.x-..'],
    leadRange: [60, 82], leadProgram: 'vibes',
    arp: { orders: ['up', 'updown'], rates: [2, 4], program: 'pluckHarp' },
    bellRhythms: ['x-------........', 'x---x-------....', '..x-----x-------'], bellProgram: 'glock',
    padProgram: 'strings'
  },
  {
    name: 'Trap', bpm: [136, 146], modes: { minor: 4, phrygian: 2 }, swing: 0,
    drums: {
      human: 4, hat16: ['xxxxxxxxxxxxxxxx', 'x.xxx.x.xxxxx.x.', 'x.x.xxx.x.x.xxxx'],
      chorusAdd: [{ lane: 'O', pattern: '..............x.' }],
      intro: ['H', 'O'],
      grooves: [
        { K: 'x......x..x.....', C: '........x.......', H: 'x.x.x.x.x.x.x.x.' },
        { K: 'x.......x.....x. x.....x...x.....'.replace(' ', ''), C: '........x.......', H: 'x.x.x.xxx.x.x.x.' },
        { K: 'x..x......x.....', S: '........x.......', C: '........x.......', H: 'x.x.x.x.x.xxx.x.' },
        { K: 'x.....x.......x.', C: '........x......g', H: 'x.x.x.x.xxx.x.x.', O: '......x.........' }
      ]
    },
    bass: ['R-------..R-----', 'R------.R-.R----', 'R-----R-------O-', 'R---------R-----', 'R-----..R---A---'],
    bassRange: [28, 46], bassProgram: 'synthBass1',
    chordRhythms: ['X---------------', 'X-------X-------', 'X.......X.......'],
    chordSize: 3, add9: false, chordRange: [55, 76], chordProgram: 'piano',
    lead: ['x.x.x-..x.x.x-..', 'x--x--x-x-......', '..x.x.x---......', 'x.xx.x..x.......'],
    leadChorus: ['x.x.xx.x.x.xx.x.', 'x.x.x.x.x.x.x-x-', 'xx.x.xx.x.xx.x..'],
    leadRange: [65, 86], leadProgram: 'celesta',
    arp: { orders: ['updown', 'down'], rates: [2], program: 'pluckHarp' },
    bellRhythms: ['x-------........', 'x-----x-----....', 'x---x---x-------'], bellProgram: 'glock',
    padProgram: 'padHalo'
  },
  {
    name: 'LoFi', bpm: [70, 86], modes: { major: 2, dorian: 2, minor: 2 }, swing: 0.33,
    drums: {
      human: 9, hat16: ['x.g.x.g.x.g.x.g.'],
      chorusAdd: [{ lane: 'Z', pattern: 'o.o.o.o.o.o.o.o.' }],
      intro: ['H', 'R', 'Z'],
      grooves: [
        { K: 'x.......x.x.....', R: '....x.......x...', H: 'x.o.x.o.x.o.x.o.' },
        { K: 'x......x..x.....', S: '....o.......o...', H: 'o.o.o.o.o.o.o.o.' },
        { K: 'x..x......x.....', S: '....x..g....x...', H: 'x.g.x.g.x.g.x.g.' },
        { K: 'x.....x...x..x..', R: '....x.......x...', Z: 'o.g.o.g.o.g.o.g.' }
      ]
    },
    bass: ['R-----..R---....', 'R--.....5--.....', 'R-...R-.R-..A...', 'R-------3---5---', 'R---..R-R---....'],
    bassRange: [33, 52], bassProgram: 'fingerBass',
    chordRhythms: ['X---------------', 'X-------X-------', '.X--------X-----', 'X--------.X-----'],
    chordSize: 4, add9: true, chordRange: [52, 74], chordProgram: 'ep',
    lead: ['x-..x---........', '..x-x-x-x---....', 'x.x.x---..x.x---', 'x---..x-x-......'],
    leadChorus: ['x.x.x-x.x---....', 'x-x-x-..x.x.x---', '..x.x.x.x-x-....'],
    leadRange: [62, 81], leadProgram: 'vibes',
    arp: { orders: ['up', 'updown'], rates: [2, 4], program: 'nylon' },
    bellRhythms: ['x-------x-------', 'x-----x---------', '....x-------....'], bellProgram: 'kalimba',
    padProgram: 'padWarm'
  },
  {
    name: 'DnB', bpm: [170, 176], modes: { minor: 4, dorian: 1 }, swing: 0,
    drums: {
      human: 5, hat16: ['x.xxx.xxx.xxx.xx', 'xoxoxoxoxoxoxoxo'],
      chorusAdd: [{ lane: 'Y', pattern: 'x.x.x.x.x.x.x.x.' }],
      grooves: [
        { K: 'x.........x.....', S: '....x.......x...', H: 'x.x.x.x.x.x.x.x.' },
        { K: 'x.........x..x..', S: '....x..g....x...', H: 'x.x.x.x.x.x.x.x.' },
        { K: 'x.x.......x.....', S: '....x.......x..g', H: 'x.xxx.x.x.xxx.x.' },
        { K: 'x.........x.....x.......x.x.....', S: '....x.......x.......x..g....x...', Z: 'xoxoxoxoxoxoxoxo' }
      ]
    },
    bass: ['R---------------', 'R-------R-----5-', 'R-----R---R-----', 'R---------O---R-', 'R-------..R-A---'],
    bassRange: [28, 46], bassProgram: 'synthBass2',
    chordRhythms: ['X---------------', 'X-------X-------', '..X.....X.....X.'],
    chordSize: 4, add9: true, chordRange: [55, 76], chordProgram: 'ep',
    lead: ['x---x---x-x-....', 'x.x.x---x.x.x---', '..x-x---..x-x---', 'x-------x-x-x-..'],
    leadChorus: ['x.x.x.x.x-x-x---', 'xx.xx.x.x.xx.x..', 'x-x-x-x-x.x.x---'],
    leadRange: [62, 86], leadProgram: 'sawLead',
    arp: { orders: ['up', 'pedal'], rates: [1, 2], program: 'pluckHarp' },
    bellRhythms: ['x-------........', 'x-----------....'], bellProgram: 'tubular',
    padProgram: 'padSweep'
  },
  {
    name: 'Synthwave', bpm: [96, 112], modes: { minor: 3, major: 2 }, swing: 0,
    drums: {
      human: 3, hat16: ['x.x.x.x.x.x.x.x.'.replace(/\./g, 'o')], crash: true,
      chorusAdd: [{ lane: 'B', pattern: '....x.......x...' }],
      grooves: [
        { K: 'x.......x.......', S: '....X.......X...', H: 'x.x.x.x.x.x.x.x.' },
        { K: 'x.......x.x.....', S: '....X.......X...', H: 'x.o.x.o.x.o.x.o.' },
        { K: 'x...x...x...x...', S: '....X.......X...', H: '..x...x...x...x.' }
      ]
    },
    bass: ['R.R.R.R.R.R.R.R.', 'RRRRRRRRRRRRRRRR', 'R.R.R.R.R.R.O.R.', 'R.RRR.RRR.RRR.RA', 'R.O.R.O.R.O.R.O.'],
    bassRange: [33, 50], bassProgram: 'synthBass1', bassGate: 0,
    chordRhythms: ['X---------------', 'X-X-X-X-X-X-X-X-', 'X-------X-------', 'X--X--X-X--X--X-'],
    chordSize: 3, add9: true, chordRange: [55, 76], chordProgram: 'synBrass',
    lead: ['x---x-x-x---x---', 'x-x-x---x-x-x---', 'x---x---x-x-x-x-', 'x.x.x-------x-x-'],
    leadChorus: ['x-x-x-x-x---x-x-', 'x---x-x-x-x-x---', 'x-xxx-x-x-x-x---'],
    leadRange: [62, 86], leadProgram: 'sawLead',
    arp: { orders: ['up', 'updown', 'down'], rates: [1, 2], program: 'sqLead' },
    bellRhythms: ['x-------x-------', 'x---------------'], bellProgram: 'vibes',
    padProgram: 'padPoly'
  },
  {
    name: 'Pop', bpm: [100, 120], modes: { major: 4, minor: 2 }, swing: 0,
    drums: {
      human: 4, hat16: ['x.o.x.o.x.o.x.o.', 'xoxoxoxoxoxoxoxo'], crash: true,
      chorusAdd: [{ lane: 'B', pattern: '..x...x...x...x.' }],
      grooves: [
        { K: 'x.......x.x.....', S: '....x.......x...', C: '....x.......x...', H: 'x.x.x.x.x.x.x.x.' },
        { K: 'x..x....x.......', S: '....x.......x...', H: 'x.x.x.x.x.x.x.x.' },
        { K: 'x...x...x...x...', C: '....x.......x...', H: '..x...x...x...x.' },
        { K: 'x.....x.x.......', S: '....x.......x...', H: 'x.o.x.o.x.o.x.o.', Z: 'g.o.g.o.g.o.g.o.' }
      ]
    },
    bass: ['R.......R.R.....', 'R...R...R...R...', 'R.R.R.R.R.R.R.R.', 'R-....R.R-..A...', 'R-..R-..5-..R-..'],
    bassRange: [36, 55], bassProgram: 'pickBass',
    chordRhythms: ['X---------------', 'X-.X-.X-X-.X-.X-', 'X---X---X---X---', 'X-------X-------', 'X-.X--.XX-.X--.X'],
    chordSize: 3, add9: true, chordRange: [55, 76], chordProgram: 'piano',
    lead: ['..x.x.x-x-x-....', 'x.x.x.x-........', '..x.x.x.x---....', 'x-x-x.x.x---....'],
    leadChorus: ['x-x-x-x-x---....', 'x.x.x.x.x-x-x---', '..x-x-x-x-x-x---'],
    leadRange: [62, 81], leadProgram: 'sqLead',
    arp: { orders: ['up', 'updown'], rates: [2], program: 'pluckHarp' },
    bellRhythms: ['x-------x-------', 'x---x---x-------'], bellProgram: 'glock',
    padProgram: 'padWarm'
  },
  {
    name: 'Rock', bpm: [110, 140], modes: { major: 2, minor: 2, mixolydian: 2 }, swing: 0,
    drums: {
      human: 6, crash: true,
      chorusAdd: [{ lane: 'Y', pattern: 'x.x.x.x.x.x.x.x.' }],
      intro: ['H', 'Y'],
      grooves: [
        { K: 'x.......x.x.....', S: '....x.......x...', H: 'x.x.x.x.x.x.x.x.' },
        { K: 'x.x.....x.......', S: '....x.......x...', H: 'x.x.x.x.x.x.x.x.' },
        { K: 'x.......x.......', S: '....x.......x...', H: 'x.x.x.x.x.x.x.x.' },
        { K: 'x...x...x...x...', S: '....x.......x...', H: 'x.x.x.x.x.x.x.x.' }
      ]
    },
    bass: ['R.R.R.R.R.R.R.R.', 'R.......R.R.....', 'R.R.R.R.R.R.5.A.', 'R-R-R-R-5-5-R-R-'],
    bassRange: [28, 50], bassProgram: 'pickBass',
    chordRhythms: ['X-X-X-X-X-X-X-X-', 'X-------X-------', 'X--X--X-X--X--X-', 'X---------------'],
    chordSize: 3, add9: false, chordRange: [52, 72], chordProgram: 'overdrive',
    lead: ['x-x-x---x-x-x---', 'x---x-x-x-------', 'x.x.x-x-x---....', '..x-x-x-x-------'],
    leadChorus: ['x-x-x-x-x-x-x---', 'x---x---x-x-x---', 'x-x-x---x-x-x-x-'],
    leadRange: [59, 81], leadProgram: 'overdrive',
    arp: { orders: ['up', 'pedal'], rates: [2], program: 'cleanGtr' },
    bellRhythms: ['x-------x-------'], bellProgram: 'glock',
    padProgram: 'organ'
  },
  {
    name: 'Ambient', bpm: [64, 80], modes: { major: 2, minor: 2, dorian: 1 }, swing: 0,
    drums: {
      human: 8,
      chorusAdd: [{ lane: 'Z', pattern: 'o.g.o.g.o.g.o.g.' }],
      intro: ['Z', 'R'],
      grooves: [
        { K: 'x...............', R: '........x.......', Z: 'o.g.o.g.o.g.o.g.' },
        { K: 'x.........x.....', R: '....o.......o...' },
        { K: 'x.......x.......', V: '......o.......o.', Z: 'g.g.g.g.g.g.g.g.' }
      ]
    },
    bass: ['R---------------', 'R-------R-------', 'R-------5-------'],
    bassRange: [33, 52], bassProgram: 'fretless',
    chordRhythms: ['X---------------', 'X-------X-------'],
    chordSize: 4, add9: true, chordRange: [55, 79], chordProgram: 'ep',
    lead: ['x-------x---....', 'x---x-------....', '....x-------x---', 'x-----x-----....'],
    leadChorus: ['x---x---x-------', 'x---x-x-x-------'],
    leadRange: [64, 86], leadProgram: 'padHalo',
    arp: { orders: ['up', 'updown'], rates: [2, 4], program: 'celesta' },
    bellRhythms: ['x---------------', 'x-------x-------', '....x-----------', 'x-----x---------'], bellProgram: 'vibes',
    padProgram: 'padHalo'
  },
  {
    name: 'Afro', bpm: [100, 112], modes: { major: 2, minor: 2, dorian: 1 }, swing: 0.12,
    drums: {
      human: 6, hat16: ['x.xox.xox.xox.xo'],
      chorusAdd: [{ lane: 'W', pattern: 'x...x.x...x.x...' }],
      intro: ['Z', 'G', 'L', 'V'],
      grooves: [
        { K: 'x..x..x.x..x..x.'.replace(/./g, function (c, i) { return [0, 6, 8, 14].indexOf(i) >= 0 ? 'x' : '.'; }), R: '...x..x....x..x.', Z: 'oxoxoxoxoxoxoxox', G: '......x.......x.' },
        { K: 'x.....x.x.......', R: '...x..x....x....', H: 'x.xox.xox.xox.xo', L: 'o..o......o..o..' },
        { K: 'x..x....x..x....', C: '....x.......x...', Z: 'oxoxoxoxoxoxoxox', V: 'x..x..x...x.x...' }
      ]
    },
    bass: ['R..R..R...R.....', 'R.....R.R...5...', 'R-..R-..R-...A..', 'R...R..R..R.R...'],
    bassRange: [33, 52], bassProgram: 'fingerBass',
    chordRhythms: ['..X...X...X...X.', 'X..X..X...X..X..', '.X.X...X.X.X....', 'X-------X-------'],
    chordSize: 3, add9: true, chordRange: [57, 77], chordProgram: 'cleanGtr',
    lead: ['x.x..x.x..x.....', '..x.x..x.x..x...', 'x..x..x.x.......', 'x.x.x..x..x.x...'],
    leadChorus: ['x.x.x..x.x.x.x..', 'x..x..x.x..x..x.', '..x.x.x..x.x.x..'],
    leadRange: [62, 84], leadProgram: 'marimba',
    arp: { orders: ['updown', 'pedal'], rates: [2], program: 'marimba' },
    bellRhythms: ['x-----x---------', 'x---x---x-------'], bellProgram: 'kalimba',
    padProgram: 'padWarm'
  },
  {
    name: 'Garage', bpm: [128, 136], modes: { minor: 3, dorian: 2 }, swing: 0.25,
    drums: {
      human: 5, hat16: ['x.xox.xox.xox.xo'],
      chorusAdd: [{ lane: 'Z', pattern: 'g.o.g.o.g.o.g.o.' }],
      grooves: [
        { K: 'x.........x.....', S: '....x.......x...', H: '..x...x...x...x.' },
        { K: 'x......x..x.....', S: '....x.......x...', C: '....x.......x...', H: '..x...xx..x...x.' },
        { K: 'x.........x..x..', R: '....x..x....x...', H: '..x...x...x.x.x.' }
      ]
    },
    bass: ['R-....R-..R-....', 'R..R..R...R..R..', 'R-......R-..A...', '..R-..R-..R-..O-'],
    bassRange: [33, 52], bassProgram: 'synthBass1',
    chordRhythms: ['..X...X...X..X..', 'X..X..X.....X...', '...X..X....X..X.', 'X-..X-..X-..X-..'],
    chordSize: 4, add9: true, chordRange: [55, 76], chordProgram: 'organ',
    lead: ['x.x..x..x.x.....', '..x..x.x..x.....', 'x..x..x...x..x..', 'x.x.x..x........'],
    leadChorus: ['x.x..x.xx.x..x..', 'x..x.x..x.x..x.x', '..x.x..x.x.x..x.'],
    leadRange: [62, 84], leadProgram: 'sqLead',
    arp: { orders: ['up', 'pedal'], rates: [2], program: 'pluckHarp' },
    bellRhythms: ['x-------........', 'x-----x---------'], bellProgram: 'vibes',
    padProgram: 'padWarm'
  },
  {
    name: 'Funk', bpm: [96, 112], modes: { dorian: 3, mixolydian: 2, minor: 1 }, swing: 0.15,
    drums: {
      human: 7, hat16: ['xoxoxoxoxoxoxoxo'], crash: true,
      chorusAdd: [{ lane: 'B', pattern: 'x.x.x.x.x.x.x.x.' }, { lane: 'W', pattern: 'x.....x...x.....' }],
      grooves: [
        { K: 'x.x.......x..x..', S: '....x..g.g..x..g', H: 'xoxoxoxoxoxoxoxo' },
        { K: 'x......x..x.....', S: '....x.g.....x.g.', H: 'x.x.x.x.x.x.x.x.', O: '......x.........' },
        { K: 'x..x..x...x.....', S: '....x..g.g.gx...', H: 'xoxoxoxoxoxoxoxo' }
      ]
    },
    bass: ['R..O..R.R...b.R.', 'R.R..O.R..R..5..', 'R...O.R.5..R.O..', 'R..r.rR..r.R.O.A'],
    bassRange: [28, 52], bassProgram: 'fingerBass', bassGate: 0,
    chordRhythms: ['..X...X...X...X.', '.X.X.X...X.X.X..', 'X..X..X...X..X..', '..X.X...X.X..X..'],
    chordSize: 4, add9: true, chordRange: [55, 76], chordProgram: 'clav',
    lead: ['x.x..x.x..x.....', '..x.x.x..x.x....', 'x..x.x..x..x.x..', 'x.xx.x..x.......'],
    leadChorus: ['x.x.x.xx.x.x.x..', 'xx.x.x.x.x.x.xx.', 'x..x.x.x..x.x.x.'],
    leadRange: [60, 82], leadProgram: 'brass',
    arp: { orders: ['pedal', 'up'], rates: [2], program: 'muteGtr' },
    bellRhythms: ['x-------x-------'], bellProgram: 'vibes',
    padProgram: 'organ'
  },
  {
    name: 'Trance', bpm: [136, 140], modes: { minor: 4, major: 1 }, swing: 0,
    drums: {
      human: 3, hat16: ['xoxoxoxoxoxoxoxo'],
      chorusAdd: [{ lane: 'Y', pattern: '..x...x...x...x.' }],
      grooves: [
        { K: 'x...x...x...x...', C: '....x.......x...', O: '..x...x...x...x.' },
        { K: 'x...x...x...x...', C: '....x.......x...', O: '..x...x...x...x.', H: 'x...x...x...x...' },
        { K: 'x...x...x...x...', S: '....x.......x...', O: '..x...x...x...x.', Z: 'xoxoxoxoxoxoxoxo' }
      ]
    },
    bass: ['..R...R...R...R.', '.RR.RR.RR.RR.RR.'.replace(/^\./, '.'), '..RR..RR..RR..RR', '..R...R...O...R.'],
    bassRange: [33, 50], bassProgram: 'synthBass2',
    chordRhythms: ['X---------------', '..X-..X-..X-..X-', 'X.X.X.X.X.X.X.X.'.replace(/\./g, '-'), 'X--X--X--X--X-X-'],
    chordSize: 3, add9: true, chordRange: [57, 79], chordProgram: 'synthStrings',
    lead: ['x.x.x.x.x.x.x.x.', 'x..x..x.x..x..x.', 'x-x-x-x-x.x.x-x-', 'x--x--x-x---x---'],
    leadChorus: ['x.xx.xx.x.xx.xx.', 'xxxxxxxxxxxxxxxx', 'x..x..x.x..x..x.'],
    leadRange: [64, 88], leadProgram: 'sawLead', leadGate: 1,
    arp: { orders: ['up', 'updown', 'down'], rates: [1], program: 'sawLead' },
    bellRhythms: ['x-------x-------', 'x---x---x---x---'], bellProgram: 'celesta',
    padProgram: 'padPoly'
  }
];

module.exports = { STYLES: STYLES, PROGS: PROGS, GM: GM };
