// SPDX-License-Identifier: GPL-3.0-only
// The page's content manager (web/hp1content.js) against the firmware's own protocol (editor.c): the host
// simulator runs the whole app with its flash and answers SysEx on a pipe (hp1_host --pipe), here behind a
// simulated Web MIDI port pair, as the FM-1 is on a USB cable. Finding the FM-1, a backup, the slots'
// actions, a .hp1 into a slot, a restore (a damaged one refused first), rc 3 while playing, and the MIDI
// file's bytes.
//   node tests/web/content_test.mjs [build/host/hp1_host]     (after host/build_host.sh)
import { spawn } from "node:child_process";
import { existsSync, readFileSync, statSync } from "node:fs";
import { createInterface } from "node:readline";
import * as C from "../../web/hp1content.js";

let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(72)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };
const throwsRc = async (p, rc) => { try { await p; return false; } catch (e) { return e instanceof C.ContentError && (rc === undefined || e.rc === rc); } };

// ---- the simulated FM-1: hp1_host --pipe
const host = spawn(process.argv[2] || "build/host/hp1_host", ["--pipe"], { stdio: ["pipe", "pipe", "inherit"] });
const lines = [], waiters = [];
createInterface({ input: host.stdout }).on("line", (l) => { if (waiters.length) waiters.shift()(l); else lines.push(l); });
const readLine = () => (lines.length ? Promise.resolve(lines.shift()) : new Promise((r) => waiters.push(r)));
let chain = Promise.resolve();                       // one exchange at a time on the pipe
const serial = (fn) => (chain = chain.then(fn, fn));
const toHex = (a) => Array.from(a, (b) => b.toString(16).padStart(2, "0").toUpperCase()).join(" ");
async function frameRaw(bytes) {
  host.stdin.write(toHex(bytes) + "\n");
  const l = await readLine();
  return l === "-" ? null : l.split(" ").map((h) => parseInt(h, 16));
}
// a script command (hp1_host's): its output lines
const run = (cmd) => serial(async () => {
  host.stdin.write(cmd + "\n");
  const out = [];
  for (;;) { const l = await readLine(); if (/^ok \d+$/.test(l)) return out; out.push(l); }
});
const expect = async (what, val) => (await run(`expect ${what} ${val}`)).some((l) => l.includes(`ok ${what} == `));

// ---- Web MIDI, simulated: the FM-1 (answering through the pipe) and another synth that never answers
function port(name, answers) {
  const input = new EventTarget();
  Object.assign(input, { name, state: "connected", open: async () => {}, close: async () => {} });
  const output = { name, state: "connected", open: async () => {}, close: async () => {}, send: (d) => {
    if (!answers) return;
    serial(() => frameRaw(Array.from(d))).then((r) => {
      if (!r) return;
      const ev = new Event("midimessage");
      ev.data = Uint8Array.from(r);
      input.dispatchEvent(ev);
    });
  } };
  return [input, output];
}
const [synthIn, synthOut] = port("Other Synth", false), [fmIn, fmOut] = port("FM-1", true);
const access = { inputs: new Map([["a", synthIn], ["b", fmIn]]), outputs: new Map([["a", synthOut], ["b", fmOut]]) };

const found = await C.findDevice(access);
ok(found && found.name === "FM-1" && found.info.protocol === 1 && found.info.version === "DEV" && found.info.slots === 4,
   "findDevice: the FM-1 answers INFO (the other synth doesn't)");
const dev = found.client;

// a project: Stainless, and a step on pattern 1 (Steps page, a key)
await run("wait 300");
await run("turn SELECT 1");
for (let i = 0; i < 3; i++) await run("tap SEQ");
await run("tapkey w8");
await run("tapkey w0");
ok(await expect("par0", 1) && await expect("patev0", ">0"), "the simulator: Stainless, pattern 1 holds notes");

// ---- the working project, as the page shows it
const info = await dev.info();
ok(info.unsaved, "INFO: changes not saved yet");
await dev.save();
ok(!(await dev.info()).unsaved, "SAVE: saved");
const list = await dev.list();
ok(list.length === 6 && list[0].kind === "project" && list[1].kind === "sequence" && list.slice(2).every((o) => o.kind === "slot" && !o.used),
   "LIST: project, sequence, four empty slots");
const projBytes = await dev.read(list[0]), seqBytes = await dev.read(list[1]);
const proj = C.parseProject(projBytes), seq = C.parseSequence(seqBytes);
ok(proj && proj.metal === "Stainless" && proj.scale === "Kurd" && proj.key === "D" && proj.feel === "Expressive" && proj.tempo === 84,
   `the project: ${proj && `${proj.metal}, ${proj.scale} in ${proj.key}, ${proj.feel}, ${proj.tempo} BPM`}`);
const d = C.describeSequence(seq);
ok(seq && seq.nev >= 1 && d.pats.length === 1 && d.pats[0].n === 1 && d.pats[0].division === "1/16" && d.song.length === 0,
   `the sequence: ${seq && seq.nev} events, pattern 1 used, no song`);

// ---- a backup
const backup = await dev.backup("DEV");
const file = JSON.parse(JSON.stringify(backup));
ok(file.format === "hp1-backup" && file.objects.length === 6 && file.objects[0].size === projBytes.length && file.objects[2].size === 0,
   "backup: one JSON file, six objects, the empty slots empty");

// ---- the slots
await dev.slot(C.SLOT_OP.SAVE, 0);
const r = await dev.slot(C.SLOT_OP.RENAME, 0, "Jam!");
ok(r.used && r.name === "JAM", "slot A: saved, renamed (Jam! shown as JAM)");
const a = C.parseSlot(await dev.readId(C.OBJ.SLOT_A));
ok(a && a.name === "JAM" && a.project.metal === "Stainless" && a.sequence.nev === seq.nev, "slot A read back: its name, sound and sequence");
const hp1 = C.buildSlot("FROM FILE", projBytes, seqBytes);
await dev.put(C.OBJ.SLOT_A + 2, hp1);
ok((await dev.list())[4].used && (await dev.list())[4].name === "FROM FILE", "a .hp1 file into slot C");
ok(await throwsRc(dev.put(C.OBJ.SLOT_A + 3, Uint8Array.from(hp1).fill(7, 32, 36)), 2) && !(await dev.list())[5].used,
   "a .hp1 whose project is not one into slot D: refused (rc 2), D empty");
await run("turn SELECT 1");                         // the working project changes: Ember
await dev.slot(C.SLOT_OP.LOAD, 0);
ok(await expect("par0", 1), "slot A loaded into the working project (Stainless again)");
await dev.slot(C.SLOT_OP.CLEAR, 0);
ok(!(await dev.list())[2].used, "slot A cleared");

// ---- the browser's library
const mem = new Map(), store = { getItem: (k) => mem.get(k) ?? null, setItem: (k, v) => mem.set(k, v) };
const item = C.libraryAdd(store, "JAM", hp1);
ok(C.libraryLoad(store).length === 1 && C.parseSlot(C.fromBase64(C.libraryLoad(store)[0].data)).name === "FROM FILE", "library: a .hp1 kept");
ok(C.libraryRemove(store, item.id).length === 0, "library: removed");

// ---- restore
await run("turn SELECT 1");                         // Ember
await dev.slot(C.SLOT_OP.SAVE, 3);
const bad = JSON.parse(JSON.stringify(file));
{ const x = C.fromBase64(bad.objects[0].data); x[12] ^= 1; bad.objects[0].data = C.toBase64(x); }   // a flipped bit
ok(await throwsRc(dev.restore(bad)) && await expect("par0", 2) && (await dev.list())[5].used, "restore: a damaged backup refused, nothing changed");
await run("tap PLAY");
ok(await throwsRc(dev.restore(file), 3) && await throwsRc(dev.slot(C.SLOT_OP.SAVE, 1), 3) && await expect("par0", 2),
   "while playing: restore and slot save refused (rc 3)");
await run("tap PLAY");
let prog = 0;
await dev.restore(file, (k, n) => { prog = k / n; });
const after = await dev.list();
ok(await expect("par0", 1) && after.slice(2).every((o) => !o.used) && after[0].crc === file.objects[0].crc && prog === 1,
   "restore: the working project back (Stainless), slots C and D emptied as in the backup");

// ---- MIDI files
// a pattern built here: 4 sixteenths, a D3 (6200 cents) at step 1 held a step, a Gu (hit 2) at step 3
const sb = new Uint8Array(236 + 16), sv = new DataView(sb.buffer);
sv.setUint32(0, 0x51455348, true); sv.setUint32(4, 1, true); sv.setUint16(24, 2, true);
for (let p = 0; p < 16; p++) { sb[28 + 4 * p] = 4; sb[29 + 4 * p] = 2; }
for (let s = 0; s < 64; s++) sb[92 + 2 * s] = 16;
sb[92] = 0; sb[93] = 2;                             // the song: pattern 1, twice
sv.setUint16(236, 0, true); sv.setUint16(238, 96, true); sv.setUint16(240, 6200, true); sb[242] = 100; sb[243] = 0;
sv.setUint16(244, 192, true); sv.setUint16(246, 10, true); sv.setUint16(248, (2 << 14) | 2, true); sb[250] = 90; sb[251] = 0;
const ps = C.parseSequence(sb);
const mid = C.midiFile(ps, 0, 120, "P1");
const want = [0x4D, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 0, 0, 1, 1, 0x80, 0x4D, 0x54, 0x72, 0x6B, 0, 0, 0, 42,
  0, 0xFF, 0x03, 2, 0x50, 0x31, 0, 0xFF, 0x51, 3, 0x07, 0xA1, 0x20, 0, 0xFF, 0x58, 4, 4, 2, 24, 8,
  0, 0x90, 62, 100, 0x60, 0x80, 62, 0, 0x60, 0x99, 36, 90, 0x18, 0x89, 36, 0, 0x81, 0x28, 0xFF, 0x2F, 0];
ok(mid.length === want.length && mid.every((b, i) => b === want[i]), "MIDI file: header, tempo, D3 on channel 1, Gu as GM kick on channel 10");
const song = C.midiFile(ps, -1, 120);
ok(song.filter((b, i) => b === 0x99 && song[i + 1] === 36).length === 2, "MIDI file: the song plays pattern 1 twice");
ps.sections.length = 0;
ok(await throwsRc(Promise.resolve().then(() => C.midiFile(ps, -1))), "MIDI file: an empty song: an error that says what to do");
const real = C.midiFile(seq, 0, proj.tempo);
ok(real[0] === 0x4D && real.length > 40, "MIDI file: the simulator's pattern 1");

// the page carries this module (after web/emu/build.sh)
const built = "build/emu/index.html", fresh = existsSync(built) &&
  statSync(built).mtimeMs >= Math.max(statSync("web/emu/index.html").mtimeMs, statSync("web/hp1content.js").mtimeMs);
if (fresh) ok(readFileSync("build/emu/index.html", "utf8").includes("/* ---- web/hp1content.js (inlined) ---- */"), "the page carries the content manager");

host.stdin.end();
console.log(failed ? `CONTENT TESTS FAILED (${failed})` : "content tests passed");
process.exit(failed ? 1 : 0);
