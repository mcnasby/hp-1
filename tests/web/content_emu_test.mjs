// SPDX-License-Identifier: GPL-3.0-only
// The content manager against the browser build: the page's client (web/hp1content.js) talking to
// hp1.wasm through web_sysex, as the page does with "This page's HP-1" (the worklet passes the frames). And
// the built page's scripts, each one parsed (a syntax error would leave the page's buttons dead).
//   node tests/web/content_emu_test.mjs build/emu/hp1.wasm     (after web/emu/build.sh)
import { spawnSync } from "node:child_process";
import { readFileSync, writeFileSync, mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import * as C from "../../web/hp1content.js";

let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(72)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };

let mem;
const wasi = new Proxy({ clock_time_get: (id, p, out) => { new DataView(mem.buffer).setBigUint64(out, 0n, true); return 0; },
  fd_write: (fd, iov, n, out) => { new DataView(mem.buffer).setUint32(out, 0, true); return 0; } }, { get: (t, k) => k in t ? t[k] : () => 0 });
const { instance } = await WebAssembly.instantiate(readFileSync(process.argv[2] || "build/emu/hp1.wasm"),
  { wasi_snapshot_preview1: wasi, env: new Proxy({}, { get: () => () => 0 }) });
const ex = instance.exports; mem = ex.memory;
if (ex._initialize) ex._initialize();
ex.web_boot();
for (let i = 0; i < 100; i++) ex.web_render(128);
// as the worklet does: the frame in, the reply out
const exchange = async (frame) => {
  new Uint8Array(mem.buffer, ex.web_sysex_in(), frame.length).set(frame);
  const n = ex.web_sysex(frame.length);
  for (let i = 0; i < 4; i++) ex.web_render(128);
  return n ? Array.from(new Uint8Array(mem.buffer, ex.web_sysex_out(), n)) : null;
};
const dev = new C.ContentClient(exchange);
const info = await dev.info();
ok(info.protocol === 1 && info.slots === 4 && info.version.length > 0, `INFO from the wasm: HP-1 ${info.version}`);
const writes0 = ex.web_store_writes();
await dev.save();
await dev.slot(C.SLOT_OP.SAVE, 1);
await dev.slot(C.SLOT_OP.RENAME, 1, "web jam");
const list = await dev.list();
ok(list[3].used && list[3].name === "WEB JAM" && ex.web_store_writes() > writes0, "slot B saved and renamed (the page's store sees the writes)");
const b = await dev.backup();
ok(b.objects.length === 6 && b.objects[3].size > 0 && C.parseSlot(C.fromBase64(b.objects[3].data)).name === "WEB JAM", "a backup of the emulator");
await dev.slot(C.SLOT_OP.CLEAR, 1);
ok(!(await dev.list())[3].used, "slot B cleared");
await dev.restore(b);
ok((await dev.list())[3].name === "WEB JAM", "restored: slot B back");
ex.web_buttons(1 << 10);                             // PLAY
for (let i = 0; i < 20; i++) ex.web_render(128);
ex.web_buttons(0);
for (let i = 0; i < 20; i++) ex.web_render(128);
const playing = (await dev.info()).playing;
let rc = 0;
try { await dev.slot(C.SLOT_OP.SAVE, 2); } catch (e) { rc = e.rc; }
ok(playing && rc === 3, "playing: a slot save refused (rc 3)");

// the built page's scripts parse
const page = readFileSync("build/emu/HP-1.html", "utf8");
const dir = mkdtempSync(join(tmpdir(), "hp1-page-"));
const scripts = [...page.matchAll(/<script( type="module")?>([\s\S]*?)<\/script>/g)];
let bad = 0;
scripts.forEach((m, i) => {
  const f = join(dir, `s${i}.${m[1] ? "mjs" : "cjs"}`);
  writeFileSync(f, m[2]);
  const r = spawnSync(process.execPath, ["--check", f], { encoding: "utf8" });
  if (r.status) { bad++; console.log(r.stderr.split("\n").slice(0, 6).join("\n")); }
});
ok(scripts.length >= 4 && !bad, `the page's ${scripts.length} scripts parse`);
ok(page.includes("/* ---- web/hp1content.js (inlined) ---- */") && page.includes('id="ctConnect"'), "the page carries the content manager");

console.log(failed ? `CONTENT EMU TESTS FAILED (${failed})` : "content emu tests passed");
process.exit(failed ? 1 : 0);
