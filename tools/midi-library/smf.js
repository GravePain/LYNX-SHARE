// Standard MIDI files, written and read back. Format 1: a tempo track, then one track per part. No dependencies.
'use strict';

var TPB = 96;   // ticks per beat: 24 a sixteenth

function vlq(n) {
  var bytes = [n & 0x7f];
  while ((n >>= 7) > 0) bytes.unshift((n & 0x7f) | 0x80);
  return bytes;
}
function text(s) { return Array.from(Buffer.from(s, 'latin1')); }
function u32(n) { return [(n >>> 24) & 255, (n >>> 16) & 255, (n >>> 8) & 255, n & 255]; }
function chunk(id, body) { return Buffer.concat([Buffer.from(id, 'latin1'), Buffer.from(u32(body.length)), Buffer.from(body)]); }

/** events: [{tick, order, bytes}] sorted; ends with end-of-track. */
function trackBytes(events, endTick) {
  events.sort(function (a, b) { return a.tick - b.tick || a.order - b.order; });
  var out = [], last = 0;
  events.forEach(function (e) { out = out.concat(vlq(e.tick - last), e.bytes); last = e.tick; });
  out = out.concat(vlq(Math.max(0, endTick - last)), [0xff, 0x2f, 0x00]);
  return out;
}

/**
 * Writes one part. part: {name, channel (0-15), program (-1 for none), notes: [{tick, length, note, velocity}]},
 * song: {bpm, title, bars}. Returns a Buffer.
 */
function write(song, part) {
  var endTick = song.bars * 4 * TPB;
  var mpq = Math.round(60000000 / song.bpm);
  var tempo = [
    { tick: 0, order: 0, bytes: [0xff, 0x03].concat(vlq(song.title.length), text(song.title)) },
    { tick: 0, order: 1, bytes: [0xff, 0x58, 0x04, 4, 2, 24, 8] },
    { tick: 0, order: 2, bytes: [0xff, 0x51, 0x03, (mpq >> 16) & 255, (mpq >> 8) & 255, mpq & 255] }
  ];
  var ch = part.channel, ev = [{ tick: 0, order: 0, bytes: [0xff, 0x03].concat(vlq(part.name.length), text(part.name)) }];
  if (part.program >= 0) ev.push({ tick: 0, order: 1, bytes: [0xc0 | ch, part.program] });
  part.notes.forEach(function (n) {
    // at one tick: note offs first (order 2), then note ons (order 3), so a repeated note is never cut by its own off
    ev.push({ tick: n.tick, order: 3, bytes: [0x90 | ch, n.note, n.velocity] });
    ev.push({ tick: n.tick + n.length, order: 2, bytes: [0x80 | ch, n.note, 0] });
  });
  var header = chunk('MThd', [0, 1, 0, 2, (TPB >> 8) & 255, TPB & 255]);
  return Buffer.concat([header, chunk('MTrk', trackBytes(tempo, endTick)), chunk('MTrk', trackBytes(ev, endTick))]);
}

/** Reads a file back: {format, tpb, tempo (bpm), names: [], programs: {}, notes: [{tick, length, note, velocity, channel}]}. */
function read(buf) {
  var p = 0;
  function need(id) { if (buf.toString('latin1', p, p + 4) !== id) throw new Error('expected ' + id + ' at ' + p); p += 4; }
  function u32r() { var v = buf.readUInt32BE(p); p += 4; return v; }
  function vlqr() { var v = 0, b; do { b = buf[p++]; v = (v << 7) | (b & 0x7f); } while (b & 0x80); return v; }
  need('MThd'); var hl = u32r();
  var format = buf.readUInt16BE(p), tracks = buf.readUInt16BE(p + 2), tpb = buf.readUInt16BE(p + 4); p += hl;
  var res = { format: format, tpb: tpb, tempo: 120, names: [], programs: {}, notes: [], problems: [] };
  for (var t = 0; t < tracks; t++) {
    need('MTrk'); var len = u32r(), end = p + len, tick = 0, status = 0, open = {};
    while (p < end) {
      tick += vlqr();
      var b = buf[p];
      if (b === 0xff) {
        var type = buf[p + 1]; p += 2; var l = vlqr();
        if (type === 0x03) res.names.push(buf.toString('latin1', p, p + l));
        if (type === 0x51) res.tempo = Math.round(60000000 / ((buf[p] << 16) | (buf[p + 1] << 8) | buf[p + 2]) * 100) / 100;
        p += l; status = 0; continue;
      }
      if (b === 0xf0 || b === 0xf7) { p++; p += vlqr(); continue; }
      if (b & 0x80) { status = b; p++; }
      var kind = status & 0xf0, chn = status & 0x0f;
      var d1 = buf[p++], d2 = (kind === 0xc0 || kind === 0xd0) ? 0 : buf[p++];
      var key = chn + ':' + d1;
      if (kind === 0x90 && d2 > 0) {
        if (open[key]) res.problems.push('note ' + d1 + ' struck again while held at tick ' + tick);
        open[key] = { tick: tick, note: d1, velocity: d2, channel: chn };
      } else if (kind === 0x80 || (kind === 0x90 && d2 === 0)) {
        var o = open[key];
        if (!o) { res.problems.push('note off with no note on: ' + d1 + ' at ' + tick); continue; }
        o.length = tick - o.tick; res.notes.push(o); delete open[key];
      } else if (kind === 0xc0) res.programs[chn] = d1;
    }
    Object.keys(open).forEach(function (k) { res.problems.push('stuck note ' + open[k].note); });
    p = end;
  }
  res.notes.sort(function (a, b) { return a.tick - b.tick || a.note - b.note; });
  return res;
}

module.exports = { TPB: TPB, write: write, read: read };
