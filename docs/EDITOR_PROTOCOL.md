# HP-1's content manager protocol (SysEx)

The web page's **Projects on your FM-1** section reads and writes the FM-1's saved projects with these
messages, over USB MIDI. The firmware side is `firmware/src/app/editor.c` (over the content API,
`firmware/src/app/content.h`); the page's side is `web/hp1content.js`. The emulator on the page answers
the same messages (the same C code, compiled to WebAssembly), and so does the host simulator
(`hp1_host --pipe`, or `sysex` in a scenario). The encodings and the staged transfer follow Melodee's
editor protocol (its `web/EDITOR_PROTOCOL.md`); the header and the commands are HP-1's own.

## Framing

A request is `F0 7D 48 50 <cmd> <args...> F7`: `7D` is the non-commercial SysEx ID, `48 50` is "HP"
(Felucca's and Melodee's editors use `7D 46 4C`; the updater's frames are `F0 22 24 35 ...` and packed
`00 59 ...`). Every request with a known command gets exactly one reply, with the same header and the
same `<cmd>`; an unknown command gets none (a newer page reads INFO's protocol version). Every data byte
is 7 bit. Send the next request after the reply: the FM-1 holds one frame at a time (up to 640 bytes).

| Item | Encoding |
| --- | --- |
| u14 | 2 bytes, 7 bits each, LSB first |
| u32 | 5 bytes, 7 bits each, LSB first; the last one is 0..15 |
| string | ASCII bytes, ended by a 0 |
| pack7 | groups of up to 7 bytes, each group after one byte holding their top bits (bit j: byte j's bit 7); unused mask bits are 0 |
| rc | 0 ok, 1 invalid request, 2 the data failed its checks (CRC, or not an object of its kind), 3 busy: the transport is running or recording is armed, 4 flash error, 5 stale (a PUT piece or commit with no matching begin, or 15 s after the last piece) |

## Objects

| id | kind | What |
| --- | --- | --- |
| 0 | 0 project | The working project: `project_t` (`app.h`): magic `"HPAN"`, format, then `int16` parameters in `handpan.h`'s `P_*` order |
| 1 | 1 sequence | The working sequence: `seq_head_t` (236 bytes: magic `"HSEQ"`, format, the name, `nev`, the patterns, the song's 64 sections), then `nev` 8-byte events (`seq.h`) |
| 2..5 | 2 slot | Project slots A..D: `ct_slot_head_t` (32 bytes: magic `"HPSL"`, format 1, the name, the project's and the sequence's lengths), the project, the sequence. A `.hp1` file is exactly these bytes |

Everything is little-endian, as the device keeps it. CRCs are zlib's CRC-32.

## Commands

| cmd | Request args | Reply args |
| --- | --- | --- |
| 1 INFO | none | protocol (1), objects (6), slots (4), the most bytes a GET or PUT piece carries (u14: 256), flags (bit 0 playing or armed, bit 1 changes not saved yet), the firmware's version string (`OM_VERSION`: "DEV", or "0.2.0 BETA") |
| 2 LIST | none | count, then per object: id, kind, used (0/1), size u32, crc u32, room u32, name string (a slot: its name, "" when unnamed or empty; the project and sequence: the working project's name) |
| 3 GET | id, offset u32, count u14 (1..256) | id, rc, offset u32, count u14, pack7 data (none when rc isn't 0). Reads the saved object (rc 1 for an empty one or past its end); allowed while playing |
| 4 PUT | op 0 begin: 0, id, size u32, crc u32; op 1 data: 1, id, offset u32, pack7 data (up to 256 bytes); op 2 commit: 2, id; op 3 abort: 3, id | op, id, rc |
| 5 SLOT | op, slot (0..3) [, name string for op 2] | op, slot, rc, used, the slot's name string |
| 6 SAVE | none | rc. The working project and sequence into flash now, when changed (or never saved), as the SAVE button does |

**PUT** is a staged write: begin erases the object's spare copy (the current one stays in charge), the
pieces go there (any order, each byte once), and commit checks them whole (the CRC begun with, and that
they hold a valid object of that kind) before it replaces the old one. A refused commit (rc 2) or an
abort changes nothing. A committed working project or sequence is taken up at once. A begin, then an
abort, checks that an object of that size fits without replacing anything (the page's restore does this
for every object first).

**SLOT** ops: 0 save the working project into the slot, 1 load the slot into the working project (rc 1:
it's empty; the Setup page's values stay), 2 rename (A-Z, 0-9, `-` and spaces, up to 15: anything else
rc 1), 3 clear (both copies erased).

Everything that writes (PUT begin and commit, SLOT, SAVE) is refused with rc 3 while the transport runs
or recording is armed: a flash erase silences the audio for a moment. GET, LIST and INFO always answer.

## Backups

**Back up everything** saves one JSON file:

```json
{ "format": "hp1-backup", "version": 1, "firmware": "0.2.0 BETA", "created": "2026-11-07T12:00:00Z",
  "objects": [ { "id": 0, "size": 148, "crc": 123456789, "data": "<base64>" }, ... ] }
```

with all six objects (an empty slot: size 0, data ""). Restoring checks the whole file first (every
CRC, every object's magic), then each object's size on the FM-1 (begin and abort), then writes the
slots (an empty one cleared), the sequence, and the working project last.
