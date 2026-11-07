/* SPDX-License-Identifier: GPL-3.0-only */
/* The clock, the arpeggiator and the sequencer (firmware/src/app/seq.c) on their own: what they play and
 * when (each note's time is its post time plus its delay, to the millisecond), against a clock stepped a
 * millisecond at a time. The app's flash is a buffer here; the instrument is not run, only listened to.
 *   cc -std=c99 -DOM_HOST -Ifirmware/src -Ifirmware/src/dsp tests/host/seq_test.c firmware/src/dsp/handpan.c */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app/app.h"

static uint32_t now;
uint32_t plat_ms(void) { return now; }

static uint8_t flash[OBJ_NOBJ][PLAT_STORE_MAX];
static int flash_len[OBJ_NOBJ] = {-1, -1, -1, -1, -1, -1, -1};
int plat_store_load(uint32_t obj, void *dst, uint32_t max)
{
    if (obj >= OBJ_NOBJ || flash_len[obj] < 0)
        return -1;
    memcpy(dst, flash[obj], max < (uint32_t)flash_len[obj] ? max : (uint32_t)flash_len[obj]);
    return max < (uint32_t)flash_len[obj] ? (int)max : flash_len[obj];
}
int plat_store_save(uint32_t obj, const void *src, uint32_t len)
{
    if (obj >= OBJ_NOBJ || len > PLAT_STORE_MAX)
        return -1;
    memcpy(flash[obj], src, len);
    flash_len[obj] = (int)len;
    return 0;
}

#include "app/project.c"
#include "app/seq.c"

/* what was played: the note, and when it sounds */
#define NLOG 512
static struct { int kind, val, vel; uint32_t t; } plays[NLOG], ends[NLOG], clicks[NLOG], pulses[NLOG];
static int nplay, nend, nclick, npulse;
static void seq_out_note(int kind, int val, int vel, int d)
{
    if (nplay < NLOG) {
        plays[nplay].kind = kind;
        plays[nplay].val = val;
        plays[nplay].vel = vel;
        plays[nplay++].t = now + (uint32_t)d;
    }
}
static void seq_out_off(int kind, int val, int d)
{
    if (nend < NLOG) {
        ends[nend].kind = kind;
        ends[nend].val = val;
        ends[nend++].t = now + (uint32_t)d;
    }
}
static void seq_out_click(int accent, int d)
{
    if (nclick < NLOG) {
        clicks[nclick].val = accent;
        clicks[nclick++].t = now + (uint32_t)d;
    }
}
static void seq_out_groove(int pulse, int d)
{
    if (npulse < NLOG) {
        pulses[npulse].val = pulse;
        pulses[npulse++].t = now + (uint32_t)d;
    }
}
static void seq_out_say(const char *a, const char *b) { (void)a; (void)b; }

static int fails;
static void check(const char *what, int ok)
{
    printf("%-58s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static void run(uint32_t ms)
{
    while (ms--) {
        now++;
        seq_poll();
    }
}

static uint32_t t0;                                  /* times from here */
#define T(x) ((x).t - t0)

static void fresh(void)
{
    now = 0;
    t0 = 0;
    project_defaults();
    seq_clear_all();
    seq_init();
    nplay = nend = nclick = npulse = 0;
}

/* the arp's first n notes from these held keys (played in this order) */
static void arp_notes(int mode, int oct, int order, const int *keys, int nk, int *got, int n)
{
    int i;
    fresh();
    proj.par[P_AMODE] = (int16_t)mode;
    proj.par[P_AOCT] = (int16_t)oct;
    proj.par[P_AORDER] = (int16_t)order;
    proj.par[P_PTEMPO] = 120;
    for (i = 0; i < nk; i++)
        seq_arp_key(EV_PAN, keys[i], 1);
    seq_poll();                                      /* (as the UI does after its keys) */
    run(125u * (uint32_t)n - 60u);
    for (i = 0; i < n; i++)
        got[i] = i < nplay ? plays[i].val : -1;
    for (i = 0; i < nk; i++)
        seq_arp_key(EV_PAN, keys[i], 0);
}

static int same(const int *a, const int *b, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (a[i] != b[i]) {
            printf("    note %d: %d, wanted %d\n", i, a[i], b[i]);
            return 0;
        }
    return 1;
}

int main(void)
{
    static const int KEYS[3] = {6200, 5000, 5700};       /* D4, then D3, then A3 */
    int got[12], i, ok;

    /* ---- the clock */
    fresh();
    proj.par[P_PTEMPO] = 120;
    run(1000);
    check("clock: 120 BPM, a second: 768 ticks (384 a beat)", seq_ticks() == 768u);
    proj.par[P_PTEMPO] = 97;
    {
        const uint32_t t0 = seq_ticks();
        run(10000);
        check("clock: 97 BPM, ten seconds: 6208 ticks (no drift)", seq_ticks() - t0 == 6208u);
    }
    {
        const uint32_t t0 = seq_ticks();
        for (i = 0; i < 24; i++)
            seq_midi_clock(KM_CLK_TICK);
        check("clock: 24 MIDI clocks, a beat", seq_ticks() - t0 == SEQ_PPQ && seq_ext());
        now += 600;
        seq_poll();
        check("clock: MIDI clock gone half a second, our own tempo again", !seq_ext());
    }

    /* ---- the arpeggiator's order */
    arp_notes(ARP_UP, 1, 0, KEYS, 3, got, 6);
    check("arp Up: low to high", same(got, (const int[]){5000, 5700, 6200, 5000, 5700, 6200}, 6));
    arp_notes(ARP_DOWN, 1, 0, KEYS, 3, got, 6);
    check("arp Down: high to low", same(got, (const int[]){6200, 5700, 5000, 6200, 5700, 5000}, 6));
    arp_notes(ARP_UPDN, 1, 0, KEYS, 3, got, 8);
    check("arp Up-down: there and back, the ends once", same(got, (const int[]){5000, 5700, 6200, 5700, 5000, 5700, 6200, 5700}, 8));
    arp_notes(ARP_UP, 1, 1, KEYS, 3, got, 6);
    check("arp Order Played: as the keys went down", same(got, (const int[]){6200, 5000, 5700, 6200, 5000, 5700}, 6));
    arp_notes(ARP_UP, 2, 0, KEYS, 3, got, 7);
    check("arp Octaves 2: the notes, then an octave up", same(got, (const int[]){5000, 5700, 6200, 6200, 6900, 7400, 5000}, 7));
    arp_notes(ARP_DOWN, 3, 0, KEYS, 3, got, 4);
    check("arp Down, Octaves 3: from the top octave", same(got, (const int[]){8600, 8100, 7400, 7400}, 4));
    arp_notes(ARP_REPEAT, 2, 0, KEYS, 3, got, 4);
    check("arp Repeat: the last played, again", same(got, (const int[]){5700, 5700, 5700, 5700}, 4));
    arp_notes(ARP_RANDOM, 1, 0, KEYS, 3, got, 12);
    for (ok = 1, i = 0; i < 12; i++)
        ok &= got[i] == 5000 || got[i] == 5700 || got[i] == 6200;
    check("arp Random: only the held notes", ok);
    for (ok = 0, i = 1; i < 12; i++)
        ok |= got[i] != got[0];
    check("arp Random: not one note", ok);

    /* ---- its timing: Rate, Swing, Gate, Chance, Hold */
    fresh();
    proj.par[P_AMODE] = ARP_UP;
    proj.par[P_PTEMPO] = 120;
    seq_arp_key(EV_PAN, 5000, 1);
    seq_poll();
    run(1000);
    for (ok = nplay == 9, i = 0; ok && i < 8; i++)
        ok = plays[i].t == plays[0].t + 125u * (uint32_t)i || plays[i].t + 1u == plays[0].t + 125u * (uint32_t)i;
    check("arp 1/16 at 120: a note at once, then every 125 ms", ok && plays[0].t == 0u);
    ok = nend >= 7 && ends[0].t >= plays[0].t + 61u && ends[0].t <= plays[0].t + 63u;
    check("arp Gate 64: each note ends half way", ok);
    seq_arp_key(EV_PAN, 5000, 0);
    nplay = 0;
    run(500);
    check("arp: the key let go, it stops", nplay == 0);
    proj.par[P_ARATE] = DIV_8T;
    seq_arp_key(EV_PAN, 5000, 1);
    run(1000);
    seq_arp_key(EV_PAN, 5000, 0);
    check("arp Rate 1/8T: three a beat (167 ms)", nplay >= 6 && (plays[2].t - plays[1].t == 166u || plays[2].t - plays[1].t == 167u));
    fresh();
    proj.par[P_AMODE] = ARP_UP;
    proj.par[P_PTEMPO] = 120;
    proj.par[P_ASWING] = 50;                         /* the second of each pair late: 125 x 1.2 / 0.8 */
    seq_arp_key(EV_PAN, 5000, 1);
    seq_poll();
    run(600);
    seq_arp_key(EV_PAN, 5000, 0);
    {
        const int a = (int)(plays[1].t - plays[0].t), b = (int)(plays[2].t - plays[1].t);
        check("arp Swing 50: long, short (150 ms, 100 ms)", a >= 149 && a <= 151 && b >= 99 && b <= 101);
    }
    fresh();
    proj.par[P_AMODE] = ARP_UP;
    proj.par[P_ACHANCE] = 0;
    seq_arp_key(EV_PAN, 5000, 1);
    run(1000);
    seq_arp_key(EV_PAN, 5000, 0);
    check("arp Chance 0: silent", nplay == 0);
    fresh();
    proj.par[P_AMODE] = ARP_UP;
    proj.par[P_AHOLD] = 1;
    proj.par[P_PTEMPO] = 120;
    seq_arp_key(EV_PAN, 5000, 1);
    seq_arp_key(EV_PAN, 5700, 1);
    seq_arp_key(EV_PAN, 5000, 0);
    seq_arp_key(EV_PAN, 5700, 0);
    run(500);
    check("arp Hold: the keys let go, it goes on (both notes)", nplay >= 4 && plays[0].val == 5000 && plays[1].val == 5700);
    seq_arp_key(EV_PAN, 6900, 1);
    nplay = 0;
    run(400);
    check("arp Hold: a new key replaces the latched notes", nplay >= 3 && plays[0].val == 6900 && plays[2].val == 6900);
    proj.par[P_AHOLD] = 0;
    seq_arp_key(EV_PAN, 6900, 0);
    nplay = 0;
    run(300);
    check("arp Hold off: it lets go", nplay == 0);

    /* ---- the arp with the transport: on its grid; recording, its notes are what's recorded */
    fresh();
    proj.par[P_AMODE] = ARP_UP;
    proj.par[P_PTEMPO] = 120;
    seq_rec_tap();
    seq_play(1);
    run(60);
    seq_arp_key(EV_PAN, 5000, 1);
    seq_poll();
    run(400);
    seq_arp_key(EV_PAN, 5000, 0);
    seq_play(0);
    ok = nplay == 4 && T(plays[0]) == 60u;
    for (i = 1; ok && i < 4; i++)
        ok = T(plays[i]) + 1u >= 125u * (uint32_t)i && T(plays[i]) <= 125u * (uint32_t)i + 1u;
    check("arp, the transport running: at once, then on its sixteenths", ok);
    ok = seq_used(0) == 4 && seq_step_ev(0, 0) >= 0 && seq_step_ev(0, 1) >= 0 && seq_step_ev(0, 3) >= 0;
    check("...recorded: the arp's notes, at their steps", ok);
    i = seq_step_ev(0, 2);
    check("...with the gate's length (half a step)", i >= 0 && sq.ev[i].len == SEQ_SUB / 2);

    /* ---- recording: armed and stopped, the first note starts it; notes at their steps */
    fresh();
    proj.par[P_PTEMPO] = 120;                        /* a step 125 ms, the pattern 2 s */
    seq_rec_tap();
    check("REC: armed, stopped", seq_armed() && !seq_running());
    seq_note_on(0, EV_PAN, 5000, 100);
    check("...the first note starts the transport and records", seq_running() && seq_recording());
    run(60);
    seq_note_off(0);
    run(253 - 60);                                   /* step 2, 3 ms late */
    seq_note_on(1, EV_PAN, 6200, 90);
    run(10);
    seq_note_off(1);
    run(1250 - 263);                                 /* step 10 exactly: a hit */
    seq_note_on(-1, EV_HIT, HIT_GU, 110);
    run(400);
    check("recording: three notes in pattern 1", seq_used(0) == 3);
    check("...at steps 1, 3 and 11", seq_step_ev(0, 0) >= 0 && seq_step_ev(0, 2) >= 0 && seq_step_ev(0, 10) >= 0);
    i = seq_step_ev(0, 2);
    check("...the late one as played (3 ms late, unquantized)", i >= 0 && sq.ev[i].at > 2 * SEQ_SUB && sq.ev[i].at < 2 * SEQ_SUB + 4);
    check("...its length as held (10 ms: 7 of 96)", i >= 0 && sq.ev[i].len >= 7 && sq.ev[i].len <= 9);
    check("...its strength", i >= 0 && (sq.ev[i].vel & 0x7F) == 90);
    i = seq_step_ev(0, 10);
    check("...the hit, a hit", i >= 0 && sq.ev[i].val == (EV_HIT << 14 | HIT_GU));
    seq_play(0);
    check("stop: the take ends, disarmed", !seq_armed() && !seq_running());
    /* played back: at their steps, from PLAY */
    nplay = 0;
    t0 = now;
    seq_play(1);
    run(2100);
    seq_play(0);
    ok = nplay >= 4 && plays[0].val == 5000 && T(plays[0]) <= 1u && plays[1].val == 6200 && T(plays[1]) >= 252u && T(plays[1]) <= 255u &&
         plays[2].val == HIT_GU && plays[2].kind == EV_HIT && T(plays[2]) >= 1249u && T(plays[2]) <= 1251u && plays[3].val == 5000 &&
         T(plays[3]) >= 1999u && T(plays[3]) <= 2001u;
    check("playback: each at its time (0, 253, 1250 ms), then the loop", ok);

    /* ---- overdub, quantize, undo */
    proj.par[P_QUANT] = 3;                           /* 1/16 */
    nplay = 0;
    seq_play(1);
    seq_rec_tap();
    check("armed while playing: waits for the pattern's start", seq_armed() && seq_waiting());
    run(1000);
    seq_note_on(2, EV_PAN, 7200, 100);               /* not yet: before the loop's end */
    run(10);
    seq_note_off(2);
    check("...a note while waiting isn't recorded", seq_used(0) == 3);
    run(990 + 380);                                  /* the second loop: 380 ms in (step 4, 5 ms late) */
    check("...recording from the loop's start", seq_recording());
    seq_note_on(2, EV_PAN, 7200, 100);
    run(20);
    seq_note_off(2);
    run(2000 - 400 + 5);                             /* the third loop: the same note at 0 (5 ms late) */
    seq_note_on(3, EV_PAN, 5000, 77);
    run(30);
    seq_note_off(3);
    check("overdub: loop after loop; the same note there replaced", seq_used(0) == 4);
    seq_rec_tap();
    check("REC again: the take ends", !seq_armed() && seq_running());
    i = seq_step_ev(0, 3);
    check("Quantize 1/16: the take's note on its step", i >= 0 && sq.ev[i].at == 3 * SEQ_SUB && sq.ev[i].val == 7200);
    i = seq_step_ev(0, 0);
    check("...and the replaced one, its new strength", i >= 0 && sq.ev[i].at == 0 && (sq.ev[i].vel & 0x7F) == 77);
    i = seq_step_ev(0, 2);
    check("...the older notes left where they were", i >= 0 && sq.ev[i].at != 2 * SEQ_SUB);
    seq_play(0);
    check("undo: the take gone", seq_undo() == 0 && seq_used(0) == 3 && seq_step_ev(0, 3) < 0);
    i = seq_step_ev(0, 0);
    check("...the replaced note back as it was", i >= 0 && (sq.ev[i].vel & 0x7F) == 100);
    check("...one level only", seq_undo() != 0);

    /* ---- swing and division in playback */
    sq.h.pat[0].swing = 50;                          /* step 2, the second of its pair, late; step 3 not */
    seq_step_put(0, 1, EV_PAN, 7400, 100, 1);
    nplay = 0;
    t0 = now;
    seq_play(1);
    run(400);
    seq_play(0);
    for (ok = 0, i = 0; i < nplay; i++)
        ok |= plays[i].val == 7400 && T(plays[i]) >= 125u + 23u && T(plays[i]) <= 125u + 26u;
    check("pattern Swing 50: the second step of a pair 25 ms late", ok);
    for (ok = 0, i = 0; i < nplay; i++)
        ok |= plays[i].val == 6200 && T(plays[i]) >= 250u && T(plays[i]) <= 256u;
    check("...the first of the next pair where it was", ok);
    for (i = 0; !ok && i < nplay; i++)
        printf("    %d at %u\n", plays[i].val, T(plays[i]));
    seq_step_rest(0, 1);
    sq.h.pat[0].swing = 0;
    sq.h.pat[0].div = DIV_8;                         /* a step 250 ms: step 3 at 503 */
    nplay = 0;
    t0 = now;
    seq_play(1);
    run(700);
    seq_play(0);
    for (ok = 0, i = 0; i < nplay; i++)
        ok |= plays[i].val == 6200 && T(plays[i]) >= 503u && T(plays[i]) <= 508u;
    check("pattern Division 1/8: the same steps, twice as long", ok);
    sq.h.pat[0].div = DIV_16;

    /* ---- patterns: the change waits for the loop's end; copy; clear */
    seq_step_put(1, 0, EV_PAN, 6900, 100, 1);
    sq.h.pat[1].len = 4;                             /* half a second */
    nplay = 0;
    t0 = now;
    seq_play(1);
    run(500);
    seq_select(1);
    check("a pattern chosen while playing: next", seq_next_pat() == 1 && seq_playing_pat() == 0);
    run(1600);
    check("...from the loop's end", seq_playing_pat() == 1);
    for (ok = 0, i = 0; i < nplay; i++)
        ok |= plays[i].val == 6900 && T(plays[i]) >= 1999u && T(plays[i]) <= 2001u;
    check("...its first note at the loop's end (2 s)", ok);
    seq_play(0);
    seq_select(0);
    i = seq_copy_pat();
    check("copy: into the next empty one, and chosen", i == 2 && sq.h.cur == 2 && seq_used(2) == 3 && seq_used(0) == 3);
    seq_clear_pat(2);
    check("clear: empty", seq_used(2) == 0 && seq_used(0) == 3 && seq_used(1) == 1);
    check("...for good (asked twice first; no undo)", seq_undo() != 0 && seq_used(2) == 0);

    /* ---- the song: pattern 2 once, pattern 1 twice, End: then it stops */
    fresh();
    proj.par[P_PTEMPO] = 120;
    seq_step_put(0, 0, EV_PAN, 5000, 100, 1);
    seq_step_put(1, 0, EV_PAN, 6200, 100, 1);
    sq.h.pat[0].len = sq.h.pat[1].len = 4;           /* half a second each */
    sq.h.sec[0].pat = 1;
    sq.h.sec[0].rep = 1;
    sq.h.sec[1].pat = 0;
    sq.h.sec[1].rep = 2;
    sq.h.song = SONG_ON;
    seq_play(1);
    run(1400);
    check("song: section 2 while its second time plays", seq_section() == 1);
    run(600);
    check("song: stopped at its end", !seq_running());
    ok = nplay == 3 && plays[0].val == 6200 && plays[1].val == 5000 && plays[2].val == 5000 && plays[1].t >= 499u &&
         plays[1].t <= 501u && plays[2].t >= 999u && plays[2].t <= 1001u;
    check("song: pattern 2, then pattern 1 twice, on time", ok);
    sq.h.song = SONG_LOOP;
    nplay = 0;
    seq_play(1);
    run(1600);
    check("song Loop: round again", seq_running() && nplay >= 4 && plays[3].val == 6200);
    seq_play(0);

    /* ---- count-in and click; the groove with the transport */
    fresh();
    proj.par[P_PTEMPO] = 120;
    proj.par[P_COUNTIN] = 1;
    proj.par[P_CLICK] = 5;                           /* Rec, level 5 */
    proj.par[P_GROOVE] = 1;
    seq_rec_play();
    check("REC + PLAY: armed, counting in", seq_armed() && seq_running() && seq_counting() == 4);
    run(1990);
    seq_note_on(0, EV_PAN, 5000, 100);               /* still counting in: not recorded */
    seq_note_off(0);
    check("...a note in the count-in isn't recorded", seq_used(0) == 0);
    run(20);
    check("...recording after a bar", seq_recording() && !seq_counting());
    seq_note_on(0, EV_PAN, 5000, 100);
    run(1000);
    seq_play(0);
    check("...the note after it, at the pattern's start", seq_step_ev(0, 0) >= 0);
    ok = nclick >= 6 && (clicks[0].val & 3) == 3 && (clicks[1].val & 3) == 2 && clicks[1].t >= 499u && clicks[1].t <= 501u &&
         (clicks[4].val & 3) == 1 && clicks[4].t >= 1999u && clicks[4].t <= 2001u;
    check("click: the count-in's four beats, then the bar's first", ok);
    ok = npulse >= 4 && pulses[0].val == 0 && pulses[0].t >= 1999u && pulses[0].t <= 2001u && pulses[1].val == 1 &&
         pulses[1].t >= 2165u && pulses[1].t <= 2168u;
    check("groove: from the transport's start, three pulses a beat", ok);

    /* ---- saved and loaded */
    i = sq.h.nev;
    check("save", seq_save() == 0 && !seq_dirty());
    seq_clear_all();
    check("load: as saved", seq_load() == 0 && sq.h.nev == i && seq_step_ev(0, 0) >= 0);
    flash[OBJ_SEQ][0] ^= 1;
    check("load: not a sequence: empty", seq_load() != 0 && sq.h.nev == 0);

    printf(fails ? "SEQ TEST FAILED (%d)\n" : "seq test passed\n", fails);
    return fails != 0;
}
