/* SPDX-License-Identifier: GPL-3.0-only */
/* HP-1 on the host: the whole app (UI, project, engine) against a simulated FM-1 driven by a script.
 * Audio goes to a WAV, the screen to PNGs, the lights to text, MIDI out to a log. The device-only
 * parts (HAL, USB, OTA, flash driver) are replaced by plat.h implemented here. (From FoMni's omni_host,
 * itself from X0X's x0x_host.)
 *
 *   hp1_host SCRIPT [OUTDIR]
 *
 * Script, one command per line (# comments):
 *   wait MS                      run the device for MS milliseconds
 *   press BTN | release BTN | tap BTN     (BTN: FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+)
 *   key K down|up | tapkey K     (K: 0..26 = F3..G5, or w0..w15 white keys, b0..b10 black keys)
 *   strum A B [MS]               white keys A..B pressed in turn (down then up), MS apart (default 25)
 *   (the keys are not velocity sensitive: a key plucks at 100)
 *   turn ENC N [MS]              (ENC: SELECT ALGO PRESET K1 K2 K3 K4; N detents, MS apart, default 80)
 *   spin ENC N                   N detents at once, exactly N (no acceleration)
 *   master N                     MASTER pot 0..4096
 *   midi B0 B1 B2                incoming USB MIDI message (hex bytes)
 *   wav FILE | wavstop           start / stop recording the output
 *   peakreset                    restart the output peak (expect peak_db_min / peak_db_max)
 *   shot FILE.png                save the screen
 *   leds                         print the lit buttons and keys
 *   expect WHAT VALUE            check state: exit 1 on mismatch; VALUE "<N" / ">N" is a bound
 *                                (WHAT: view dirty frozen store_writes midi_out voices grains
 *                                 dropped pat pulse ext shellL shellR cavity duocount parN
 *                                 whiteW (cents of white key W) duoI (cents of the second pan's note I)
 *                                 duokeyB (cents black key B plays as the second pan, -1 not)
 *                                 noteC (level x100 of the note at C cents) levelC (the same, x1000)
 *                                 lumaY (brightness 0..255 of the screen at x 2, row Y: the theme)
 *                                 dimkey (the key whose LED is dim, -1 none) litkeys (bits)
 *                                 touchK (the pressure Feel gives held key K, 0..127: K as for key)
 *                                 peak_db_max peak_db_min)
 *   reboot                       re-run boot from the simulated flash (persistence test)
 *   oldproject                   put a FiMba-1 project in the simulated flash (it must not load)
 *   v1project                    put a format 1 project (before Feel) in the flash, Feel's place not 0
 *   torn N                       the next save stops after N flash programs (a power cut)
 *   rename SLOT NAME             rename slot 0..3 (content.h, as the web manager would; _ for a space)
 *   ctcopy FROM TO               copy content object FROM into TO through content.h's staged write
 *   ctbad TO                     a staged write of TO with a wrong CRC (refused: the old one stays)
 *   sysex HEX..                  a SysEx frame for the content manager (editor.c), F0..F7 as hex bytes
 *                                (several arguments or one run); its reply is printed (expect edrc: its rc)
 *
 *   hp1_host --pipe              no script: frames in on stdin, one per line as hex (F0 .. F7), each reply out on
 *                                stdout as one hex line ("-" for none), 20 ms of the device between; a line not
 *                                starting with F0 is a script command (its output goes to stderr)
 * expect, the sequencer: run armed rec wait count seqpat playpat nev patevP (events in pattern P)
 *   stepS (cents or hit of step S of the chosen pattern, -1 a rest) steplenS (its length, sub-steps)
 *   outn (notes the sequencer and the arp played) outlast (the last one's cents or hit) arpheld bpm
 *   slotK (slot K holds a project) slotnameK (the first letter of its name, 0 none) undo section
 *   seqdirty click (clicks posted) edrc (the content manager's last reply's rc)
 */
#define OM_HOST 1
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L          /* clock_gettime under -std=c99 (glibc hides it otherwise) */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#define __attribute__(x)

/* ------------------------------------------------------------ screen --- */
static uint16_t fb[240 * 240];          /* RGB565, byte-swapped as the panel takes it (gfx.c) */
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    uint16_t sc = (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8));
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = sc;
}
static uint32_t blits;
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    blits++;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u)
                fb[(y + j) * 240u + x + i] = px[j * w + i];
}
#include "../firmware/src/gfx.c"


/* ---------------------------------------------------------- platform --- */
#include "../firmware/src/app/plat.h"

static uint32_t now_ms, held_btn, held_keys, master = 2048;
static int32_t enc_acc[NE];
static uint32_t lit_btn, lit_keys, dim_keys;

uint32_t plat_ms(void) { return now_ms; }
uint32_t plat_buttons(void) { return held_btn; }
uint32_t plat_keys(void) { return held_keys; }
int32_t plat_enc(int role)
{
    int32_t v = enc_acc[role];
    enc_acc[role] = 0;
    return v;
}
uint32_t plat_master(void) { return master; }
void plat_leds(uint32_t b, uint32_t k, uint32_t d)
{
    lit_btn = b;
    lit_keys = k;
    dim_keys = d & ~k;                           /* (a lit key is lit, not dim: plat_fm1.c) */
}

#define MQ 256
static uint32_t min_q[MQ], mi_w, mi_r;
static FILE *midi_log;
static uint32_t midi_out_count, clock_out_count;
int plat_midi_in(uint32_t *pkt)
{
    if (mi_r == mi_w)
        return 0;
    *pkt = min_q[mi_r++ % MQ];
    return 1;
}
void plat_midi_out(uint32_t pkt)
{
    midi_out_count++;
    if (((pkt >> 8) & 0xFF) == 0xF8) {
        clock_out_count++;
        return;
    }
    if (midi_log)
        fprintf(midi_log, "%u ms: %02X %02X %02X\n", now_ms, (pkt >> 8) & 0xFF, (pkt >> 16) & 0xFF, (pkt >> 24) & 0xFF);
}

/* flash: a simulated NOR (erase -> 0xFF, programming only clears bits), kept across a simulated reboot,
 * with the device's own storage.c over it. "torn N" makes the next save stop after N programs. */
static uint8_t nor[0x100000];
static int nor_init_done, torn_after = -1;
static uint32_t store_writes;
static const uint8_t flash_ok = 1;
static void nor_init(void)
{
    if (!nor_init_done) {
        memset(nor, 0xFF, sizeof nor);
        nor_init_done = 1;
    }
}
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    nor_init();
    if (off >= sizeof nor || n > sizeof nor - off)
        return -1;
    memcpy(dst, nor + off, n);
    return 0;
}
static int st_erase(uint32_t off)
{
    nor_init();
    if (!((off >= 0x97000u && off + 4096u <= 0xE0000u) || (off >= 0xFC000u && off + 4096u <= 0xFF000u)) || (off & 0xFFFu))
        return -8;                                    /* (as FL_STORE_OK on the device) */
    memset(nor + off, 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    nor_init();
    if (torn_after == 0)
        return -9;                                    /* (the power cut) */
    if (torn_after > 0)
        torn_after--;
    if ((off & 0xFFu) + n > 256u || off + n > sizeof nor)
        return -1;
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    return 0;
}
#define PLAT_STORE_DONE(rc) (store_writes += (rc) == 0, torn_after = -1)
#include "../firmware/src/storage.c"
#include "../firmware/src/app/plat_store.c"

static uint32_t cpu_pct;
uint32_t plat_cpu_pct(void) { return cpu_pct; }
uint32_t plat_xruns(void) { return 0; }

/* -------------------------------------------------------------- app --- */
#include "../firmware/src/app/app.h"
#include "../firmware/src/app/project.c"
#include "../firmware/src/app/seq.c"
#include "../firmware/src/app/click.c"
#include "../firmware/src/app/content.c"
#include "../firmware/src/app/ui.c"
#include "../firmware/src/app/editor.c"

/* the content manager's SysEx (editor.c), as the device's main loop takes it: a whole frame (F0..F7) in,
 * its reply (if any) out. The script's "sysex", the pipe (--pipe) and the browser (web_sysex) use it. */
static uint8_t ed_rx[4096];
static uint32_t ed_rx_n;
static void ed_reply(const uint8_t *p, uint32_t n)
{
    if (ed_rx_n + n <= sizeof ed_rx) {
        memcpy(ed_rx + ed_rx_n, p, n);
        ed_rx_n += n;
    }
}
static uint32_t host_sysex(const uint8_t *f, uint32_t n)   /* the reply's bytes in ed_rx (0: none) */
{
    ed_rx_n = 0;
    if (n >= 2u && f[0] == 0xF0 && f[n - 1u] == 0xF7)
        ed_handle(f + 1, n - 2u);
    return ed_rx_n;
}

/* ---------------------------------------------------------- outputs --- */
static void png_write(const char *path)
{
    /* RGB PNG with stored (uncompressed) deflate blocks: no zlib needed */
    static uint8_t raw[240 * (1 + 240 * 3)];
    FILE *f = fopen(path, "wb");
    uint32_t crc_t[256], i, j, k, n = 0, a = 1, b = 0, pos, len = sizeof raw;
    uint8_t hdr[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return;
    }
    for (i = 0; i < 256; i++) {
        uint32_t c = i;
        for (k = 0; k < 8; k++)
            c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_t[i] = c;
    }
    for (j = 0; j < 240; j++) {
        raw[n++] = 0;
        for (i = 0; i < 240; i++) {
            uint16_t p = fb[j * 240 + i];
            p = (uint16_t)((p >> 8) | (p << 8));
            raw[n++] = (uint8_t)(((p >> 11) & 31) * 255 / 31);
            raw[n++] = (uint8_t)(((p >> 5) & 63) * 255 / 63);
            raw[n++] = (uint8_t)((p & 31) * 255 / 31);
        }
    }
    for (i = 0; i < len; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
#define PUT32(v) do { uint8_t q_[4] = {(uint8_t)((v) >> 24), (uint8_t)((v) >> 16), (uint8_t)((v) >> 8), (uint8_t)(v)}; fwrite(q_, 1, 4, f); } while (0)
    fwrite(hdr, 1, 8, f);
    {
        uint8_t ih[17] = {'I', 'H', 'D', 'R', 0, 0, 0, 240, 0, 0, 0, 240, 8, 2, 0, 0, 0};
        uint32_t c = 0xFFFFFFFFu;
        PUT32(13u);
        fwrite(ih, 1, 17, f);
        for (i = 0; i < 17; i++)
            c = crc_t[(c ^ ih[i]) & 255] ^ (c >> 8);
        PUT32(~c);
    }
    {
        uint32_t nblk = (len + 65534) / 65535, zlen = 2 + len + nblk * 5 + 4, c = 0xFFFFFFFFu;
        uint8_t *z = malloc(zlen + 4), *q = z;
        memcpy(q, "IDAT", 4);
        q += 4;
        *q++ = 0x78;
        *q++ = 0x01;
        for (pos = 0; pos < len;) {
            uint32_t m = len - pos > 65535 ? 65535 : len - pos;
            *q++ = pos + m >= len ? 1 : 0;
            *q++ = (uint8_t)m;
            *q++ = (uint8_t)(m >> 8);
            *q++ = (uint8_t)~m;
            *q++ = (uint8_t)(~m >> 8);
            memcpy(q, raw + pos, m);
            q += m;
            pos += m;
        }
        *q++ = (uint8_t)(b >> 8);
        *q++ = (uint8_t)b;
        *q++ = (uint8_t)(a >> 8);
        *q++ = (uint8_t)a;
        PUT32(zlen);
        fwrite(z, 1, zlen + 4, f);
        for (i = 0; i < zlen + 4; i++)
            c = crc_t[(c ^ z[i]) & 255] ^ (c >> 8);
        PUT32(~c);
        free(z);
    }
    {
        uint8_t ie[4] = {'I', 'E', 'N', 'D'};
        uint32_t c = 0xFFFFFFFFu;
        PUT32(0u);
        fwrite(ie, 1, 4, f);
        for (i = 0; i < 4; i++)
            c = crc_t[(c ^ ie[i]) & 255] ^ (c >> 8);
        PUT32(~c);
    }
    fclose(f);
}

static FILE *wav;
static uint32_t wav_frames;
static float peak_out;
static void wav_open(const char *path)
{
    static const uint8_t h[44] = {0};
    wav = fopen(path, "wb");
    if (wav)
        fwrite(h, 1, 44, wav);
    wav_frames = 0;
}
static void wav_close(void)
{
    uint32_t bytes = wav_frames * 4, v;
    if (!wav)
        return;
    fseek(wav, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, wav);
    v = 36 + bytes;
    fwrite(&v, 4, 1, wav);
    fwrite("WAVEfmt ", 1, 8, wav);
    v = 16;
    fwrite(&v, 4, 1, wav);
    {
        uint16_t fmt[2] = {1, 2};
        uint32_t r[2] = {44100, 44100 * 4};
        uint16_t al[2] = {4, 16};
        fwrite(fmt, 2, 2, wav);
        fwrite(r, 4, 2, wav);
        fwrite(al, 2, 2, wav);
    }
    fwrite("data", 1, 4, wav);
    fwrite(&bytes, 4, 1, wav);
    fclose(wav);
    wav = 0;
}

/* --------------------------------------------------------- the device --- */
static double audio_due_ms, render_ns_total, render_budget_ns_total;
static uint32_t ui_due;
static double now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1e9 + (double)t.tv_nsec;
}

static void boot(void)
{
    km_init();
    project_load();
    project_apply();
    seq_load();
    ct_refresh();
    lcd_fill(0, 0, 240, 240, 0);
    ui_init();
}

static void run_ms(uint32_t ms)
{
    static int32_t blk[512];
    uint32_t i, k;
    for (i = 0; i < ms; i++) {
        now_ms++;
        while (audio_due_ms <= (double)now_ms) {      /* 256-frame blocks, as the device renders them */
            double t0 = now_ns(), dt;
            km_render(blk, 256, master);
            click_mix(blk, 256, master);
#ifdef OM_WEB
            web_audio(blk, 256);
#endif
            dt = now_ns() - t0;
            render_ns_total += dt;
            render_budget_ns_total += 256.0 * 1e9 / 44100.0;
            cpu_pct = (uint32_t)(100.0 * dt / (256.0 * 1e9 / 44100.0));
            for (k = 0; k < 256; k++) {
                float l = (float)blk[2 * k] / 8388608.0f, r = (float)blk[2 * k + 1] / 8388608.0f;
                int16_t s[2];
                if (fabsf(l) > peak_out)
                    peak_out = fabsf(l);
                if (fabsf(r) > peak_out)
                    peak_out = fabsf(r);
                s[0] = (int16_t)(l * 32767.0f);
                s[1] = (int16_t)(r * 32767.0f);
                if (wav) {
                    fwrite(s, 2, 2, wav);
                    wav_frames++;
                }
            }
            audio_due_ms += 256.0 * 1000.0 / 44100.0;
        }
        if (now_ms >= ui_due) {
            ui_due = now_ms + 16;
            ui_frame();
        } else {
            ui_input_only();
        }
    }
}

/* ---------------------------------------------------------- script --- */
static const char *const BTN_N[NB] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
                                      "PLAY", "REC", "OCT-", "OCT+"};
static const char *const ENC_N[NE] = {"SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4"};

static int find_name(const char *const *t, int n, const char *s)
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(t[i], s))
            return i;
    return -1;
}

static int key_of(const char *s)
{
    if (s[0] == 'w')
        return WHITE_K[atoi(s + 1) & 15];
    if (s[0] == 'b')
        return BLACK_K[atoi(s + 1) % 11];
    return atoi(s);
}

static int note_lvl(int cents, float x)
{
    int v;
    float l = 0.0f;
    for (v = 0; v < KM_NVOICE; v++)
        if (km_voice_cents[v] == cents && km_voice_level[v] > l)
            l = km_voice_level[v];
    return (int)(l * x);
}

static int expect(const char *what, const char *val)
{
    int got;
    if (!strcmp(what, "view"))
        got = ui.view;
    else if (!strcmp(what, "dirty"))
        got = ui.dirty;
    else if (!strcmp(what, "frozen"))
        got = ui.frozen + 2 * km_frozen;           /* 3: the UI and the engine agree it is frozen */
    else if (!strcmp(what, "store_writes"))
        got = (int)store_writes;
    else if (!strcmp(what, "midi_out"))
        got = (int)midi_out_count;
    else if (!strcmp(what, "voices")) {
        int v;
        for (got = 0, v = 0; v < KM_NVOICE; v++)
            got += km_voice_cents[v] >= 0;
    } else if (!strcmp(what, "grains"))
        got = km_grains_on;
    else if (!strcmp(what, "shellL") || !strcmp(what, "shellR"))   /* the shell ringing, x 1000 */
        got = (int)(km_shell_level[what[5] == 'R'] * 1000.0f);
    else if (!strcmp(what, "cavity"))             /* the air inside (the Gu), x 1000 */
        got = (int)(km_cavity_level * 1000.0f);
    else if (!strcmp(what, "dropped"))
        got = (int)km_dropped;
    else if (!strcmp(what, "pat"))                /* the groove: 3 = it plays with the transport */
        got = groove_on() ? 3 : 0;
    else if (!strcmp(what, "run"))
        got = seq_running();
    else if (!strcmp(what, "armed"))
        got = seq_armed();
    else if (!strcmp(what, "rec"))
        got = seq_recording();
    else if (!strcmp(what, "wait"))
        got = seq_waiting();
    else if (!strcmp(what, "count"))
        got = seq_counting();
    else if (!strcmp(what, "seqpat"))
        got = sq.h.cur;
    else if (!strcmp(what, "playpat"))
        got = seq_playing_pat();
    else if (!strcmp(what, "section"))
        got = seq_section();
    else if (!strcmp(what, "nev"))
        got = sq.h.nev;
    else if (!strncmp(what, "patev", 5))
        got = seq_used(atoi(what + 5));
    else if (!strncmp(what, "steplen", 7)) {
        int e = seq_step_ev(sq.h.cur, atoi(what + 7));
        got = e < 0 ? -1 : sq.ev[e].len;
    } else if (!strncmp(what, "step", 4)) {
        int e = seq_step_ev(sq.h.cur, atoi(what + 4));
        got = e < 0 ? -1 : sq.ev[e].val & 0x3FFF;
    } else if (!strcmp(what, "outn"))
        got = (int)ui.out_n;
    else if (!strcmp(what, "outlast"))
        got = ui.out_last;
    else if (!strcmp(what, "arpheld"))
        got = seq_arp_held();
    else if (!strcmp(what, "bpm"))
        got = seq_bpm();
    else if (!strcmp(what, "seqdirty"))
        got = seq_dirty();
    else if (!strcmp(what, "edrc"))               /* the content manager's last reply's rc */
        got = ed.rc;
    else if (!strcmp(what, "click"))
        got = (int)click_w;
    else if (!strncmp(what, "slotname", 8))
        got = ct_slot_name(atoi(what + 8))[0];
    else if (!strncmp(what, "slot", 4))
        got = ct_slot_used(atoi(what + 4));
    else if (!strcmp(what, "duocount"))           /* how many second pans go with this one */
        got = km_duo_count(proj.par[P_SCALE], proj.par[P_KEY], proj.par[P_BLACK] == BLK_DUOKEY);
    else if (!strcmp(what, "pulse"))
        got = km_pat_pulse;
    else if (!strcmp(what, "ext"))
        got = km_ext;
    else if (!strncmp(what, "par", 3))            /* parN: parameter N (handpan.h P_*) */
        got = proj.par[atoi(what + 3)];
    else if (!strncmp(what, "white", 5))          /* whiteW: the cents white key W plays now */
        got = white_cents(atoi(what + 5));
    else if (!strncmp(what, "duokey", 6)) {       /* duokeyB: the cents black key B plays as the second pan (-1: not) */
        int b = atoi(what + 6);
        got = is_duo_key(b) ? duo_cents(duo_index(b)) : -1;
    } else if (!strncmp(what, "duo", 3))          /* duoI: the cents of the second pan's note I (0: its ding) */
        got = duo_cents(atoi(what + 3));
    else if (!strncmp(what, "touch", 5))          /* touchK: the pressure on held key K (Feel) */
        got = tch.p[key_of(what + 5)];
    else if (!strncmp(what, "note", 4))           /* noteC: the level x 100 of the note at C cents */
        got = note_lvl(atoi(what + 4), 100.0f);
    else if (!strncmp(what, "level", 5))          /* levelC: the same, x 1000 */
        got = note_lvl(atoi(what + 5), 1000.0f);
    else if (!strcmp(what, "dimkey")) {            /* the key whose LED is dim (-1: none) */
        int q;
        for (got = -1, q = 0; q < NKEYS; q++)
            if (dim_keys >> q & 1u)
                got = q;
    } else if (!strcmp(what, "litkeys"))         /* the keys lit (bit per key) */
        got = (int)lit_keys;
    else if (!strncmp(what, "luma", 4)) {         /* lumaY: the screen's brightness at (2, Y) */
        uint16_t p = fb[(atoi(what + 4) % 240) * 240 + 2];
        p = (uint16_t)((p >> 8) | (p << 8));
        got = (int)((((p >> 11) & 31) * 255 / 31 * 3 + ((p >> 5) & 63) * 255 / 63 * 6 + (p & 31) * 255 / 31) / 10);
    }
    else if (!strcmp(what, "peak_db_max") || !strcmp(what, "peak_db_min")) {
        double db = 20.0 * log10(peak_out > 1e-9f ? peak_out : 1e-9f);
        int bad = what[8] == 'm' && what[9] == 'a' ? db > atof(val) : db < atof(val);
        printf("%s peak %.1f dBFS (%s %s)\n", bad ? "FAIL" : "  ok", db, what + 8, val);
        return bad;
    } else {
        printf("FAIL unknown expect %s\n", what);
        return 1;
    }
    if (val[0] == '<' || val[0] == '>') {
        int lim = (int)strtol(val + 1, 0, 0);
        if (val[0] == '<' ? got >= lim : got <= lim) {
            printf("FAIL expect %s %s, got %d\n", what, val, got);
            return 1;
        }
        printf("  ok %s == %d (%s)\n", what, got, val);
        return 0;
    }
    if (got != (int)strtol(val, 0, 0)) {
        printf("FAIL expect %s == %s, got %d\n", what, val, got);
        return 1;
    }
    printf("  ok %s == %d\n", what, got);
    return 0;
}

#if !defined(OM_WEB) && !defined(OM_HOST_LIB)
static const char *dir = ".";
static char out[400];

static uint32_t hex_bytes(const char *s, uint8_t *b, uint32_t max)   /* "F0 7D .." or "F07D..": the bytes */
{
    uint32_t n = 0;
    while (*s && n < max) {
        char h[3] = {0, 0, 0};
        while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
            s++;
        if (!s[0] || !s[1])
            break;
        h[0] = s[0];
        h[1] = s[1];
        b[n++] = (uint8_t)strtoul(h, 0, 16);
        s += 2;
    }
    return n;
}

static void hex_print(FILE *f, const uint8_t *b, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        fprintf(f, i ? " %02X" : "%02X", b[i]);
    fprintf(f, "\n");
}

/* one line of a script: 0, or 2 (a line it can't run) */
static int script_line(char *line, int lineno, int *fails)
{
    char cmd[32] = {0}, a[256] = {0}, b[64] = {0}, c[64] = {0};
    if (line[0] == '#' || sscanf(line, "%31s %255s %63s %63s", cmd, a, b, c) < 1)
        return 0;
    if (!strcmp(cmd, "wait"))
        run_ms((uint32_t)atoi(a));
    else if (!strcmp(cmd, "press") || !strcmp(cmd, "release") || !strcmp(cmd, "tap")) {
        int i = find_name(BTN_N, NB, a);
        if (i < 0) {
            printf("line %d: no button %s\n", lineno, a);
            return 2;
        }
        if (cmd[0] != 'r')
            held_btn |= 1u << i;
        if (cmd[0] == 't')
            run_ms(40);
        if (cmd[0] != 'p')
            held_btn &= ~(1u << i);
        run_ms(cmd[0] == 't' ? 40 : 2);
    } else if (!strcmp(cmd, "key") || !strcmp(cmd, "tapkey")) {
        int k = key_of(a);
        if (cmd[0] == 't') {
            held_keys |= 1u << k;
            run_ms(60);
            held_keys &= ~(1u << k);
            run_ms(30);
        } else {
            if (!strcmp(b, "down"))
                held_keys |= 1u << k;
            else
                held_keys &= ~(1u << k);
            run_ms(2);
        }
    } else if (!strcmp(cmd, "strum")) {          /* strum A B [MS]: a finger across the white keys */
        int x = atoi(a), y = atoi(b), gap = c[0] ? atoi(c) : 25, s = y >= x ? 1 : -1;
        for (;; x += s) {
            held_keys |= 1u << WHITE_K[x & 15];
            run_ms((uint32_t)gap);
            held_keys &= ~(1u << WHITE_K[x & 15]);
            if (x == y)
                break;
        }
        run_ms(2);
    } else if (!strcmp(cmd, "turn")) {
        int e = find_name(ENC_N, NE, a), n = atoi(b), s = n > 0 ? 1 : -1;
        uint32_t gap = c[0] ? (uint32_t)atoi(c) : 80u;
        if (e < 0) {
            printf("line %d: no encoder %s\n", lineno, a);
            return 2;
        }
        for (; n; n -= s) {
            enc_acc[e] += s;
            run_ms(gap);
        }
    } else if (!strcmp(cmd, "spin")) {
        int e = find_name(ENC_N, NE, a);
        if (e < 0) {
            printf("line %d: no encoder %s\n", lineno, a);
            return 2;
        }
        enc_acc[e] += atoi(b);
        accel_off = 1;
        run_ms(1);
        accel_off = 0;
    } else if (!strcmp(cmd, "master"))
        master = (uint32_t)atoi(a);
    else if (!strcmp(cmd, "midi")) {
        uint32_t s = (uint32_t)strtoul(a, 0, 16), d1 = (uint32_t)strtoul(b, 0, 16), d2 = (uint32_t)strtoul(c, 0, 16);
        uint32_t cin = s >= 0xF0 ? 0x0F : s >> 4;
        min_q[mi_w++ % MQ] = cin | s << 8 | d1 << 16 | d2 << 24;
    } else if (!strcmp(cmd, "wav")) {
        snprintf(out, sizeof out, "%s/%s", dir, a);
        wav_open(out);
    } else if (!strcmp(cmd, "wavstop"))
        wav_close();
    else if (!strcmp(cmd, "peakreset"))
        peak_out = 0.0f;
    else if (!strcmp(cmd, "shot")) {
        snprintf(out, sizeof out, "%s/%s", dir, a);
        png_write(out);
    } else if (!strcmp(cmd, "leds")) {
        int i;
        printf("  leds: buttons");
        for (i = 0; i < NB; i++)
            if (lit_btn >> i & 1u)
                printf(" %s", BTN_N[i]);
        printf(" | white ");
        for (i = 0; i < 16; i++)
            printf("%c", (lit_keys >> WHITE_K[i] & 1u) ? '#' : (dim_keys >> WHITE_K[i] & 1u) ? '+' : '.');
        printf(" black ");
        for (i = 0; i < 11; i++)
            printf("%c", (lit_keys >> BLACK_K[i] & 1u) ? '#' : (dim_keys >> BLACK_K[i] & 1u) ? '+' : '.');
        printf("\n");
    } else if (!strcmp(cmd, "expect"))
        *fails += expect(a, b);
    else if (!strcmp(cmd, "oldproject")) {   /* a FiMba-1 project in the store (another magic): ignored */
        project_defaults();
        proj.magic = 0x41424C4Bu;
        proj.par[P_METAL] = 2;
        plat_store_save(OBJ_PROJ, &proj, sizeof proj);
    } else if (!strcmp(cmd, "v1project")) {  /* saved before Feel: it takes its default, whatever is there */
        project_defaults();
        proj.format = 1u;
        proj.par[P_FEEL] = FEEL_CLASSIC;
        plat_store_save(OBJ_PROJ, &proj, sizeof proj);
    } else if (!strcmp(cmd, "torn"))
        torn_after = atoi(a);
    else if (!strcmp(cmd, "rename")) {
        char *q;
        for (q = b; *q; q++)
            if (*q == '_')
                *q = ' ';
        if (ct_slot_rename(atoi(a), b))
            printf("  rename %s %s: refused\n", a, b);
    } else if (!strcmp(cmd, "ctcopy") || !strcmp(cmd, "ctbad")) {   /* through content.h, as the web would */
        static uint8_t buf[PLAT_STORE_MAX];
        int from = atoi(a), to = cmd[2] == 'b' ? from : atoi(b), n = ct_read(from, 0, buf, sizeof buf), rc = -1;
        uint32_t off;
        if (n > 0 && ct_write_begin(to, (uint32_t)n, plat_crc32(0u, buf, (uint32_t)n) ^ (cmd[2] == 'b')) == 0) {
            for (off = 0, rc = 0; off < (uint32_t)n && !rc; off += 200u)   /* in pieces, as over a cable */
                rc = ct_write_data(off, buf + off, (uint32_t)n - off < 200u ? (uint32_t)n - off : 200u);
            rc = rc ? rc : ct_write_commit();
        }
        printf("  %s %s %s: %d (%d bytes)\n", cmd, a, b, rc, n);
    } else if (!strcmp(cmd, "reboot")) {
        memset(&proj, 0, sizeof proj);
        boot();
    } else if (!strcmp(cmd, "sysex")) {               /* a frame for the content manager; its reply printed */
        static uint8_t f[2048];
        if (host_sysex(f, hex_bytes(line + 5, f, sizeof f))) {
            printf("  reply: ");
            hex_print(stdout, ed_rx, ed_rx_n);
        } else {
            printf("  no reply\n");
        }
    } else if (!strcmp(cmd, "echo"))
        printf("%s", line + 5);
    else {
        printf("line %d: unknown command %s\n", lineno, cmd);
        return 2;
    }
    return 0;
}

int main(int argc, char **argv)
{
    FILE *sc;
    static char line[4096];
    int lineno = 0, fails = 0;
    if (argc < 2) {
        fprintf(stderr, "usage: hp1_host SCRIPT [OUTDIR] | hp1_host --pipe\n");
        return 2;
    }
    if (!strcmp(argv[1], "--pipe")) {                  /* the content manager's frames, for a test's page client */
        boot();
        run_ms(100);
        while (fgets(line, sizeof line, stdin)) {
            if ((line[0] == 'F' || line[0] == 'f') && line[1] == '0') {
                static uint8_t f[2048];
                if (host_sysex(f, hex_bytes(line, f, sizeof f)))
                    hex_print(stdout, ed_rx, ed_rx_n);
                else
                    printf("-\n");
                run_ms(20);
            } else {
                if (line[0] != '\n' && script_line(line, ++lineno, &fails))
                    fprintf(stderr, "pipe: cannot run %s", line);
                printf("ok %d\n", fails);
            }
            fflush(stdout);
        }
        return fails ? 1 : 0;
    }
    if (argc > 2)
        dir = argv[2];
    sc = fopen(argv[1], "r");
    if (!sc) {
        perror(argv[1]);
        return 2;
    }
    snprintf(out, sizeof out, "%s/midi_out.txt", dir);
    midi_log = fopen(out, "w");
    boot();
    while (fgets(line, sizeof line, sc))
        if (script_line(line, ++lineno, &fails))
            return 2;
    wav_close();
    if (midi_log)
        fclose(midi_log);
    printf("host: %u ms simulated, %u blits, render %.1f%% of real time (host CPU)%s\n", now_ms, blits,
           render_budget_ns_total > 0 ? 100.0 * render_ns_total / render_budget_ns_total : 0.0,
           fails ? ", EXPECTATIONS FAILED" : "");
    return fails ? 1 : 0;
}
#endif /* OM_WEB, OM_HOST_LIB */
