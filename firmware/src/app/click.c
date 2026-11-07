/* SPDX-License-Identifier: GPL-3.0-only
 * The click's voice is Melodee's (Copyright (C) 2026 Kerem Kilic, Ellic Studio), itself from Felucca's
 * metronome: a decaying sine on the magic circle. */
/* HP-1's click: a short, quiet tick a beat (the bar's first higher), added to the output after the
 * instrument (never in it: the engine's sound is untouched). The main loop posts it with a delay, as
 * it posts strikes; the audio interrupt drains the queue at the start of a block, as the engine does,
 * so the two land together. Part of the unity build, before ui.c. */
#define CLICK_NQ 8u
#define CLICK_K_DOWN 8195                    /* 2 sin(pi f / fs), Q15: 1760 Hz, the bar's first */
#define CLICK_K_BEAT 6154                    /* 1320 Hz */
#define CLICK_AMP 16384
static struct { int16_t ms; uint8_t accent, level; } click_q[CLICK_NQ];
static volatile uint32_t click_w, click_r;
static struct { int32_t due, k, g; uint8_t wait; } click_p[CLICK_NQ];   /* posted, waiting for their time */
static struct { int32_t s, c, k, env, g; } click_v;

/* main loop: a click delay_ms from now, at level 1..10 */
static void click_post(int accent, int level, int delay_ms)
{
    uint32_t w = click_w;
    if (w - click_r >= CLICK_NQ || level <= 0)
        return;
    click_q[w % CLICK_NQ].ms = (int16_t)(delay_ms < 0 ? 0 : delay_ms > 2000 ? 2000 : delay_ms);
    click_q[w % CLICK_NQ].accent = (uint8_t)accent;
    click_q[w % CLICK_NQ].level = (uint8_t)(level > 10 ? 10 : level);
    __asm__ volatile("" ::: "memory");
    click_w = w + 1u;
}

/* audio interrupt: add the click to n stereo frames (24-bit), after km_render; gain_q12 the MASTER pot */
static void click_mix(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    uint32_t i, j;
    while (click_r != click_w) {                          /* the posted ones start counting now */
        uint32_t r = click_r % CLICK_NQ;
        for (j = 0; j < CLICK_NQ; j++)
            if (!click_p[j].wait) {
                click_p[j].wait = 1;
                click_p[j].due = click_q[r].ms * 44 + click_q[r].ms / 10;
                click_p[j].k = click_q[r].accent ? CLICK_K_DOWN : CLICK_K_BEAT;
                /* level 10: a quarter of full scale (the bar's first; a beat 6 dB under), with MASTER */
                click_p[j].g = (int32_t)((uint32_t)click_q[r].level * 205u * (gain_q12 > 8192u ? 8192u : gain_q12) / 4096u) >>
                               (click_q[r].accent ? 0 : 1);
                break;
            }
        __asm__ volatile("" ::: "memory");
        click_r++;
    }
    for (i = 0; i < n; i++) {
        int32_t v, l, r;
        for (j = 0; j < CLICK_NQ; j++)
            if (click_p[j].wait && click_p[j].due-- <= 0) {
                click_p[j].wait = 0;
                click_v.s = 0;                           /* (from 0: no step at its start) */
                click_v.c = CLICK_AMP;
                click_v.k = click_p[j].k;
                click_v.env = 32767;
                click_v.g = click_p[j].g;
            }
        if (click_v.env <= 0)
            continue;
        click_v.s += (click_v.k * click_v.c) >> 15;
        click_v.c -= (click_v.k * click_v.s) >> 15;
        v = (((click_v.s * click_v.env) >> 15) * click_v.g) >> 4;   /* Q15 x Q12 -> 24-bit */
        click_v.env -= (click_v.env >> 8) + 1;
        l = out[2u * i] + v;
        r = out[2u * i + 1u] + v;
        out[2u * i] = l > 8388607 ? 8388607 : l < -8388607 ? -8388607 : l;
        out[2u * i + 1u] = r > 8388607 ? 8388607 : r < -8388607 ? -8388607 : r;
    }
}
