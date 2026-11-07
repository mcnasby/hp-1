// SPDX-License-Identifier: GPL-3.0-only
// The 7-bit encodings, the backup file's shape and its checks, after Melodee's web/fm1backup.js,
// Copyright (C) 2026 Kerem Kilic (Ellic Studio), Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic),
// Hügelton Instruments.
//
// HP-1's content manager, the page's side: the SysEx protocol of firmware/src/app/editor.c
// (docs/EDITOR_PROTOCOL.md) to an FM-1 over Web MIDI, or to the emulator on the page (the same C code in
// WebAssembly); the saved objects taken apart for the page (the project's sound, the sequence's patterns
// and song); .hp1 files (a project slot's bytes); backups; a pattern or the song as a Standard MIDI File.
// No imports: web/emu/inline_lib.py puts it in the page as it is.

export const OBJ = { PROJECT: 0, SEQUENCE: 1, SLOT_A: 2 };
export const NSLOT = 4;
const HDR = [0xF0, 0x7D, 0x48, 0x50];
const CMD = { INFO: 1, LIST: 2, GET: 3, PUT: 4, SLOT: 5, SAVE: 6 };
export const SLOT_OP = { SAVE: 0, LOAD: 1, RENAME: 2, CLEAR: 3 };
const CHUNK = 256;
const MAX_OBJ = 5 * 4096 - 256;                     // the biggest object (a slot's), plat.h PLAT_STORE_MAX

// errors carry the device's rc and say what to do
const RC_TEXT = {
  1: "The FM-1 didn't accept that request. Reconnect and try again.",
  2: "The FM-1 checked the data and refused it: the file may be damaged or made by another firmware. Nothing was changed.",
  3: "The FM-1 is playing or recording. Press PLAY (and REC) to stop it, then try again.",
  4: "Writing to the FM-1's memory failed. Try again; if it keeps failing, restart the FM-1.",
  5: "The transfer was interrupted. Try again.",
};
export class ContentError extends Error {
  constructor(message, rc = 0) { super(message); this.rc = rc; }
}
const check = (rc) => { if (rc) throw new ContentError(RC_TEXT[rc] || `The FM-1 answered with error ${rc}.`, rc); };

// ------------------------------------------------------------- encodings
export const u32 = (n) => Array.from({ length: 5 }, (_, i) => (n >>> (i * 7)) & (i === 4 ? 15 : 127));
export const r32 = (a, o = 0) => (a[o] | a[o + 1] << 7 | a[o + 2] << 14 | a[o + 3] << 21 | a[o + 4] << 28) >>> 0;
export function pack7(bytes) {                      // groups of up to 7, each after a byte of their top bits
  const a = [];
  for (let off = 0; off < bytes.length; off += 7) {
    const c = bytes.subarray(off, off + 7);
    a.push(c.reduce((m, b, i) => m | (b >>> 7) << i, 0), ...Array.from(c, (b) => b & 127));
  }
  return a;
}
export function unpack7(a, size) {
  const out = new Uint8Array(size); let i = 0, j = 0;
  while (j < size) {
    const n = Math.min(7, size - j), mask = a[i++];
    if (mask == null || mask >>> n) throw new ContentError("The FM-1 sent bytes that don't decode. Try again.");
    for (let k = 0; k < n; k++) {
      if (a[i] == null || a[i] > 127) throw new ContentError("The FM-1 sent a short chunk. Try again.");
      out[j++] = a[i++] | (mask >>> k & 1) << 7;
    }
  }
  if (i !== a.length) throw new ContentError("The FM-1 sent more than it said. Try again.");
  return out;
}
export function crc32(bytes) {                     // zlib's, as the firmware's storage
  let c = 0xFFFFFFFF;
  for (const b of bytes) { c ^= b; for (let k = 0; k < 8; k++) c = c >>> 1 ^ (c & 1 ? 0xEDB88320 : 0); }
  return (~c) >>> 0;
}
const str = (a, o) => { let s = "", i = o; while (i < a.length && a[i]) s += String.fromCharCode(a[i++]); return [s, i + 1]; };
export const toBase64 = (bytes) => { let s = ""; for (const b of bytes) s += String.fromCharCode(b); return btoa(s); };
export const fromBase64 = (s) => Uint8Array.from(atob(s), (c) => c.charCodeAt(0));

// ------------------------------------------------------------- transports
// An exchange sends one frame (F0..F7) and resolves with the device's reply frame (or null on a timeout).

// Web MIDI: one input/output pair; listens without taking the port's onmidimessage (the page's MIDI-in
// keeps playing the emulator)
export function midiExchange(input, output) {
  let pending = null;
  const on = (e) => {
    const d = e.data;
    if (!pending || d.length < 6 || d[0] !== 0xF0 || d[1] !== 0x7D || d[2] !== 0x48 || d[3] !== 0x50 || d[4] !== pending.cmd) return;
    const p = pending; pending = null; clearTimeout(p.timer); p.resolve(Array.from(d));
  };
  input.addEventListener("midimessage", on);
  const ex = (frame, timeout) => new Promise((resolve, reject) => {
    if (pending) { reject(new ContentError("Busy with another request. Wait for it to finish.")); return; }
    if (input.state === "disconnected" || output.state === "disconnected") {
      reject(new ContentError("The FM-1 was disconnected. Plug it back in and press Connect.")); return;
    }
    pending = { cmd: frame[4], resolve, timer: setTimeout(() => { pending = null; resolve(null); }, timeout) };
    try { output.send(frame); } catch (err) { clearTimeout(pending.timer); pending = null; reject(new ContentError("Sending to the FM-1 failed: " + err.message)); }
  });
  ex.close = () => input.removeEventListener("midimessage", on);
  ex.port = input.name;
  return ex;
}

// the FM-1 among the MIDI ports: the first that answers INFO (FM-1-like names asked first)
export async function findDevice(access) {
  const outs = [...access.outputs.values()], ins = [...access.inputs.values()].filter((i) => i.state !== "disconnected");
  const likely = (p) => /fm-1|hp-1|felucca|composite|sinco|usb-midi/i.test(p.name || "");
  ins.sort((a, b) => likely(b) - likely(a));
  for (const input of ins) {
    const output = outs.find((o) => o.name === input.name) || (outs.length === 1 ? outs[0] : null);
    if (!output) continue;
    try { await input.open(); await output.open(); } catch (_) { continue; }
    const ex = midiExchange(input, output);
    try {
      const c = new ContentClient(ex);
      const info = await c.info({ timeout: 800 });
      return { client: c, info, name: input.name };
    } catch (_) { ex.close(); }
  }
  return null;
}

// ------------------------------------------------------------- the client
export class ContentClient {
  constructor(exchange) { this.ex = exchange; }
  close() { if (this.ex.close) this.ex.close(); }

  async request(cmd, args = [], { timeout = 2000, retries = 1 } = {}) {
    const frame = [...HDR, cmd, ...args, 0xF7];
    if (frame.some((b, i) => i > 0 && i < frame.length - 1 && (b < 0 || b > 127))) throw new ContentError("A request byte is out of range (a bug).");
    for (let t = 0; t <= retries; t++) {
      const r = await this.ex(frame, timeout);
      if (r) return r.slice(5, -1);
    }
    throw new ContentError("The FM-1 didn't answer. Check the USB cable, close other apps that use MIDI, and connect again. " +
                           "If it still doesn't answer, it may need a newer HP-1 (Install, below).");
  }

  async info(opt) {
    const a = await this.request(CMD.INFO, [], opt);
    const [version] = str(a, 6);
    return { protocol: a[0], objects: a[1], slots: a[2], chunk: a[3] | a[4] << 7, playing: !!(a[5] & 1), unsaved: !!(a[5] & 2), version };
  }

  async list() {
    const a = await this.request(CMD.LIST);
    const out = []; let p = 1;
    for (let i = 0; i < a[0]; i++) {
      const o = { id: a[p], kind: ["project", "sequence", "slot"][a[p + 1]] || "other", used: !!a[p + 2],
                  size: r32(a, p + 3), crc: r32(a, p + 8), cap: r32(a, p + 13) };
      [o.name, p] = str(a, p + 18);
      out.push(o);
    }
    return out;
  }

  async save() { check((await this.request(CMD.SAVE, [], { timeout: 8000, retries: 0 }))[0]); }

  // the whole object, its bytes checked against the listed CRC
  async read(o, onProgress = () => {}) {
    const bytes = new Uint8Array(o.size);
    for (let off = 0; off < o.size; off += CHUNK) {
      const n = Math.min(CHUNK, o.size - off);
      const a = await this.request(CMD.GET, [o.id, ...u32(off), n & 127, n >> 7]);
      check(a[1]);
      if (a[0] !== o.id || r32(a, 2) !== off || (a[7] | a[8] << 7) !== n) throw new ContentError("The FM-1 answered for another part. Try again.");
      bytes.set(unpack7(a.slice(9), n), off);
      onProgress(off + n, o.size);
    }
    if (crc32(bytes) !== o.crc) throw new ContentError("It changed while it was read (the FM-1 saved meanwhile). Try again.");
    return bytes;
  }

  async readId(id) {
    const o = (await this.list()).find((x) => x.id === id);
    if (!o || !o.used) return null;
    return this.read(o);
  }

  async put(id, bytes, onProgress = () => {}) {
    const ask = async (args, timeout = 4000) => { const a = await this.request(CMD.PUT, args, { timeout, retries: 0 }); return a[2]; };
    check(await ask([0, id, ...u32(bytes.length), ...u32(crc32(bytes))], 8000));
    try {
      for (let off = 0; off < bytes.length; off += CHUNK) {
        check(await ask([1, id, ...u32(off), ...pack7(bytes.subarray(off, off + CHUNK))]));
        onProgress(Math.min(off + CHUNK, bytes.length), bytes.length);
      }
      check(await ask([2, id], 8000));
    } catch (e) {
      await ask([3, id]).catch(() => {});
      throw e;
    }
  }

  // does it fit (and is the FM-1 free to take it)? begin, then abort: nothing replaced
  async probe(id, size, crc) {
    const a = await this.request(CMD.PUT, [0, id, ...u32(size), ...u32(crc)], { timeout: 8000, retries: 0 });
    await this.request(CMD.PUT, [3, id], { retries: 0 });
    return a[2];
  }

  async slot(op, s, name) {
    const args = [op, s];
    if (op === SLOT_OP.RENAME) args.push(...Array.from(cleanName(name), (c) => c.charCodeAt(0)), 0);
    const a = await this.request(CMD.SLOT, args, { timeout: 8000, retries: 0 });
    check(a[2]);
    return { used: !!a[3], name: str(a, 4)[0] };
  }

  // everything, in one file
  async backup(firmware, onProgress = () => {}) {
    const info = await this.info();
    if (!info.playing) await this.save();         // (the working copies as they are now)
    const list = await this.list();
    const total = list.reduce((n, o) => n + (o.used ? o.size : 0), 0); let done = 0;
    const objects = [];
    for (const o of list) {
      if (!o.used) { objects.push({ id: o.id, size: 0, crc: 0, data: "" }); continue; }
      const bytes = await this.read(o, (k) => onProgress(done + k, total));
      done += o.size;
      objects.push({ id: o.id, size: o.size, crc: o.crc, data: toBase64(bytes) });
    }
    return { format: "hp1-backup", version: 1, firmware: firmware || info.version, created: new Date().toISOString(), objects };
  }

  // a backup back: every object checked here, then each one's size by the FM-1 (begin + abort), before
  // anything is replaced; the slots first, the working project last
  async restore(file, onProgress = () => {}) {
    const b = readBackup(file);
    const info = await this.info();
    if (info.playing) check(3);
    for (const o of b.objects) if (o.size) {
      const rc = await this.probe(o.id, o.size, o.crc);
      if (rc === 1) throw new ContentError(`This backup's ${objName(o.id)} doesn't fit this FM-1's firmware. Nothing was changed.`);
      check(rc);
    }
    const order = [...b.objects.filter((o) => o.id >= OBJ.SLOT_A), ...b.objects.filter((o) => o.id === OBJ.SEQUENCE),
                   ...b.objects.filter((o) => o.id === OBJ.PROJECT)];
    const total = order.reduce((n, o) => n + o.size, 0); let done = 0;
    const list = await this.list();
    for (const o of order) {
      if (o.size) await this.put(o.id, o.bytes, (k) => onProgress(done + k, total));
      else if (o.id >= OBJ.SLOT_A && list.find((x) => x.id === o.id)?.used) await this.slot(SLOT_OP.CLEAR, o.id - OBJ.SLOT_A);
      done += o.size;
    }
    return b;
  }
}

export const objName = (id) => id === OBJ.PROJECT ? "working project" : id === OBJ.SEQUENCE ? "sequence" : `slot ${"ABCD"[id - OBJ.SLOT_A]}`;
// what the screen can show: A-Z 0-9 - and spaces, up to 15
export const cleanName = (s) => String(s || "").toUpperCase().replace(/[^A-Z0-9 -]/g, "").slice(0, 15);

// a backup file, every byte checked before anything is sent
export function readBackup(file) {
  if (typeof file === "string") {
    try { file = JSON.parse(file); } catch (_) { throw new ContentError("That file isn't an HP-1 backup (not JSON)."); }
  }
  if (!file || file.format !== "hp1-backup" || file.version !== 1 || !Array.isArray(file.objects))
    throw new ContentError("That file isn't an HP-1 backup. Choose a file saved with Back up everything.");
  const seen = new Set();
  const objects = file.objects.map((o) => {
    if (!o || !Number.isInteger(o.id) || o.id < 0 || o.id >= OBJ.SLOT_A + NSLOT || seen.has(o.id) || !Number.isInteger(o.size) ||
        o.size < 0 || o.size > MAX_OBJ || !Number.isInteger(o.crc) || o.crc < 0 || o.crc > 0xFFFFFFFF || typeof o.data !== "string")
      throw new ContentError("The backup is damaged (an object's description). Nothing was changed.");
    seen.add(o.id);
    let bytes;
    try { bytes = fromBase64(o.data); } catch (_) { throw new ContentError("The backup is damaged (its data). Nothing was changed."); }
    if (bytes.length !== o.size || (o.size && crc32(bytes) !== o.crc))
      throw new ContentError(`The backup is damaged: the ${objName(o.id)}'s checksum doesn't match. Nothing was changed.`);
    if (o.size) {
      const ok = o.id === OBJ.PROJECT ? !!parseProject(bytes) : o.id === OBJ.SEQUENCE ? !!parseSequence(bytes) : !!parseSlot(bytes);
      if (!ok) throw new ContentError(`The backup's ${objName(o.id)} isn't one HP-1 can read. Nothing was changed.`);
    }
    return { ...o, bytes };
  });
  return { ...file, objects };
}

// ------------------------------------------------------------- the objects
const PROJ_MAGIC = 0x4E415048, SEQ_MAGIC = 0x51455348, SLOT_MAGIC = 0x4C535048;
const P = { METAL: 0, LAYOUT: 23, SCALE: 24, KEY: 25, BLACK: 26, TEMPO: 36, FEEL: 41, NPARAMS: 54 };   // handpan.h P_*
export const METALS = ["Nitrided", "Stainless", "Ember", "Raw"];
export const SCALES = ["Kurd", "Celtic", "Integral", "Pygmy", "Hijaz", "Aegean", "Equinox", "Akebono", "Major", "Mixolydian", "SaByeD"];
export const KEYS = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"];
export const DIVS = ["1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T", "1/2", "1/1", "2 bars", "4 bars"];
const DIVT = [384, 192, 96, 48, 128, 64, 768, 1536, 3072, 6144];      // seq.c: a step's clock ticks (384 a beat)
const HIT_NAMES = ["Thwap L", "Tak L", "Gu", "Ghost", "Slap", "Tak", "Harmonic", "Rim", "Mute", "Tak R", "Thwap R", "Splash", "Shaker", "Shaker back", "Castanets"];
// the hits on General MIDI drum notes: ui.c's drum_hit() read backwards (Mute has none there: a muted conga)
export const HIT_GM = [38, 54, 36, 51, 39, 42, 53, 37, 62, 46, 41, 49, 69, 70, 85];

const dv = (b) => new DataView(b.buffer, b.byteOffset, b.byteLength);
const cstr = (b, o, n) => { let s = ""; for (let i = 0; i < n && b[o + i]; i++) s += String.fromCharCode(b[o + i]); return s; };

export function parseProject(b) {
  if (!b || b.length < 8) return null;
  const v = dv(b);
  if (v.getUint32(0, true) !== PROJ_MAGIC || v.getUint32(4, true) < 1 || v.getUint32(4, true) > 9) return null;
  const n = Math.min(P.NPARAMS, (b.length - 8) >> 1), par = [];
  for (let i = 0; i < n; i++) par.push(v.getInt16(8 + 2 * i, true));
  const at = (i, d) => (i < n ? par[i] : d);
  return { format: v.getUint32(4, true), par, metal: METALS[at(P.METAL, 0)] || "?", scale: SCALES[at(P.SCALE, 0)] || "?",
           key: KEYS[((at(P.KEY, 2) % 12) + 12) % 12], feel: v.getUint32(4, true) >= 2 && at(P.FEEL, 0) ? "Classic" : "Expressive",
           tempo: at(P.TEMPO, 84), black: at(P.BLACK, 0) ? "Duo" : "Kit" };
}

const SEQ_HEAD = 236;
export function parseSequence(b) {
  if (!b || b.length < SEQ_HEAD) return null;
  const v = dv(b);
  if (v.getUint32(0, true) !== SEQ_MAGIC) return null;
  const nev = v.getUint16(24, true);
  if (b.length !== SEQ_HEAD + nev * 8) return null;
  const patterns = [];
  for (let p = 0; p < 16; p++) patterns.push({ len: b[28 + 4 * p], div: b[29 + 4 * p], swing: b[30 + 4 * p], events: 0 });
  const events = [];
  for (let i = 0; i < nev; i++) {
    const o = SEQ_HEAD + 8 * i, val = v.getUint16(o + 4, true), pat = b[o + 7];
    const e = { at: v.getUint16(o, true), len: v.getUint16(o + 2, true), kind: val >> 14, val: val & 0x3FFF, vel: b[o + 6] & 127, pat };
    if (pat < 16) { events.push(e); patterns[pat].events++; }
  }
  const sections = [];
  for (let s = 0; s < 64 && b[92 + 2 * s] < 16; s++) sections.push({ pat: b[92 + 2 * s], rep: Math.max(1, b[93 + 2 * s]) });
  return { name: cstr(b, 8, 16), nev, cur: b[26], songMode: ["Off", "On", "Loop"][b[27]] || "Off", patterns, sections, events };
}

const SLOT_HEAD = 32;
export function parseSlot(b) {
  if (!b || b.length < SLOT_HEAD) return null;
  const v = dv(b);
  if (v.getUint32(0, true) !== SLOT_MAGIC || v.getUint32(4, true) !== 1) return null;
  const pl = v.getUint16(24, true), sl = v.getUint32(28, true);
  if (SLOT_HEAD + pl + sl !== b.length) return null;
  const project = parseProject(b.subarray(SLOT_HEAD, SLOT_HEAD + pl)), sequence = parseSequence(b.subarray(SLOT_HEAD + pl));
  if (!project || !sequence) return null;
  return { name: cstr(b, 8, 16), project, sequence, projectBytes: b.subarray(SLOT_HEAD, SLOT_HEAD + pl), sequenceBytes: b.subarray(SLOT_HEAD + pl) };
}

// a slot's bytes from a project and a sequence (the working ones, to download as a .hp1)
export function buildSlot(name, proj, seq) {
  const b = new Uint8Array(SLOT_HEAD + proj.length + seq.length), v = dv(b), nm = cleanName(name);
  v.setUint32(0, SLOT_MAGIC, true); v.setUint32(4, 1, true);
  for (let i = 0; i < nm.length; i++) b[8 + i] = nm.charCodeAt(i);
  v.setUint16(24, proj.length, true); v.setUint32(28, seq.length, true);
  b.set(proj, SLOT_HEAD); b.set(seq, SLOT_HEAD + proj.length);
  return b;
}

// a few words about a sequence's patterns and its song
export function describeSequence(s) {
  const pats = s.patterns.map((p, i) => ({ n: i + 1, ...p, division: DIVS[p.div] || "?" })).filter((p) => p.events);
  const song = s.sections.map((x) => `P${x.pat + 1}${x.rep > 1 ? ` x${x.rep}` : ""}`);
  return { pats, song };
}
export const hitName = (h) => HIT_NAMES[h] || "Hit";

// ------------------------------------------------------------- MIDI files
// A pattern (0..15) or the song (pat < 0) as a Standard MIDI File, format 0, 384 ticks a beat (the
// sequencer's clock): the pan's notes on channel 1, the second pan's on 2, the hits on 10 as GM drums.
// Swing is left out (the notes on their steps).
export function midiFile(seq, pat, bpm = 120, name = "HP-1") {
  const parts = [];                                   // [pattern, start tick]
  let t = 0;
  const span = (p) => seq.patterns[p].len * (DIVT[seq.patterns[p].div] || 96);
  if (pat >= 0) { parts.push([pat, 0]); t = span(pat); }
  else for (const s of seq.sections) for (let r = 0; r < s.rep; r++) { parts.push([s.pat, t]); t += span(s.pat); }
  if (!parts.length) throw new ContentError("The song has no sections yet. Build one on the FM-1 (SEQ, Song), or export a pattern.");
  const ev = [];                                      // [tick, order (offs first), bytes]
  for (const [p, start] of parts) {
    const st = DIVT[seq.patterns[p].div] || 96;
    for (const e of seq.events) {
      if (e.pat !== p) continue;
      const on = start + Math.floor(e.at * st / 96), len = Math.max(e.kind === 2 ? 24 : 1, Math.floor(e.len * st / 96));
      const ch = e.kind === 2 ? 9 : e.kind === 1 ? 1 : 0;
      const note = e.kind === 2 ? HIT_GM[e.val] ?? 62 : Math.max(0, Math.min(127, Math.round(e.val / 100)));
      ev.push([on, 1, [0x90 | ch, note, Math.max(1, e.vel)]], [on + len, 0, [0x80 | ch, note, 0]]);
    }
  }
  ev.sort((a, b) => a[0] - b[0] || a[1] - b[1]);
  const vlq = (n) => { const o = [n & 127]; while ((n >>>= 7)) o.unshift((n & 127) | 128); return o; };
  const us = Math.round(60000000 / Math.max(20, bpm)), title = Array.from(name.slice(0, 60), (c) => c.charCodeAt(0) & 127);
  const trk = [0, 0xFF, 0x03, title.length, ...title, 0, 0xFF, 0x51, 3, us >> 16 & 255, us >> 8 & 255, us & 255, 0, 0xFF, 0x58, 4, 4, 2, 24, 8];
  let last = 0;
  for (const [tick, , bytes] of ev) { trk.push(...vlq(tick - last), ...bytes); last = tick; }
  trk.push(...vlq(Math.max(0, t - last)), 0xFF, 0x2F, 0);
  const be32 = (n) => [n >>> 24 & 255, n >>> 16 & 255, n >>> 8 & 255, n & 255];
  return Uint8Array.from([0x4D, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 0, 0, 1, 384 >> 8, 384 & 255,
                          0x4D, 0x54, 0x72, 0x6B, ...be32(trk.length), ...trk]);
}

// ------------------------------------------------------------- the browser's library
// .hp1 files kept in this browser (localStorage), to send to any slot later
const LIB_KEY = "hp1-library";
export function libraryLoad(store) {
  try { const l = JSON.parse(store.getItem(LIB_KEY) || "[]"); return Array.isArray(l) ? l : []; } catch (_) { return []; }
}
export function libraryAdd(store, name, bytes) {
  if (!parseSlot(bytes)) throw new ContentError("That isn't an HP-1 project (.hp1).");
  const l = libraryLoad(store);
  const item = { id: Date.now().toString(36) + Math.random().toString(36).slice(2, 6), name: name || "Untitled", size: bytes.length,
                 created: new Date().toISOString(), data: toBase64(bytes) };
  l.unshift(item);
  try { store.setItem(LIB_KEY, JSON.stringify(l)); }
  catch (_) { throw new ContentError("The browser's storage is full. Delete something from the library first."); }
  return item;
}
export function libraryRemove(store, id) {
  const l = libraryLoad(store).filter((x) => x.id !== id);
  try { store.setItem(LIB_KEY, JSON.stringify(l)); } catch (_) {}
  return l;
}
