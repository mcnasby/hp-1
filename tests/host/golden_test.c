/* SPDX-License-Identifier: GPL-3.0-only */
/* The sound, held still: a fixed performance on every metal (the pan's notes at several strengths, the
 * ding struck again and again, every hit of the steel and the kit, a second pan, a palm mute, the delay
 * and the grains) rendered and measured block by block, against tests/host/golden.txt. Any change to the
 * engine that moves the sound fails here; a change meant to move it regenerates the file on purpose:
 *   golden_test tests/host/golden.txt          compare (0.01 dB a block)
 *   golden_test tests/host/golden.txt write    rewrite it (only when the sound is meant to change) */
#include <stdio.h>
#include <string.h>
#include "handpan.h"
#include "fastmath.h"

#define BLOCK 1024u
#define SECS 7u
#define NBLK (SECS * 44100u / BLOCK)

static int32_t buf[2 * 256];

/* the performance: at frame f (in blocks of 256), what happens */
static void play(uint32_t f, const int *pan, const int *duo)
{
    static const uint8_t VEL[6] = {100, 60, 127, 85, 40, 110};
    if (f < 120 && f % 10 == 0)                            /* the fields, up and down */
        km_pluck(pan[(f / 10) % KM_NPAN], VEL[(f / 10) % 6], (f / 10) & 1 ? 40 : -40, 0);
    if (f >= 120 && f < 168 && f % 12 == 0)                /* the ding, again and again */
        km_pluck(pan[0], 100, 0, 0);
    if (f >= 170 && f < 170 + 3 * HIT_N && (f - 170) % 3 == 0 && (f - 170) / 3 != HIT_MUTE)
        km_hit((int)(f - 170) / 3, 104, 0);                /* every hit (the mute below) */
    if (f >= 220 && f < 250 && f % 5 == 0)                 /* the second pan */
        km_pluck(duo[(f / 5) % KM_NDUO], 90, 55, 0);
    if (f == 262)
        km_pluck(pan[4], 120, 0, 25);                      /* a strike, delayed */
    if (f == 280)
        km_hit(HIT_MUTE, 100, 0);
    if (f == 290)
        km_pluck(pan[2], 100, -30, 0);
    if (f == 300)
        km_damp(pan[2]);
}

int main(int argc, char **argv)
{
    static float got[4][NBLK][2];
    int m, fails = 0;
    const int write = argc > 2 && !strcmp(argv[2], "write");
    for (m = 0; m < 4; m++) {
        int pan[KM_NPAN], duo[KM_NDUO], all[KM_NPAN + KM_NDUO], i;
        uint32_t f = 0, n = 0, b = 0;
        double e[2] = {0, 0};
        km_init();
        km_set(P_METAL, m);
        km_set(P_DELAY, 30);
        km_set(P_GRAIN, 25);
        km_pan_notes(0, 2, 0, pan);
        km_duo_notes(0, 2, 0, 0, 0, duo);
        for (i = 0; i < KM_NPAN; i++)
            all[i] = pan[i];
        for (i = 0; i < KM_NDUO; i++)
            all[KM_NPAN + i] = duo[i];
        km_pan(all, KM_NPAN + KM_NDUO);
        while (b < NBLK) {
            uint32_t k;
            play(f++, pan, duo);
            km_render(buf, 256, 2048);
            for (k = 0; k < 256; k++) {
                const double l = buf[2 * k] / 8388608.0, r = buf[2 * k + 1] / 8388608.0;
                e[0] += l * l;
                e[1] += r * r;
                if (++n == BLOCK) {
                    got[m][b][0] = 10.0f * fm_log2f((float)(e[0] / BLOCK) + 1e-20f) * 0.30103f;
                    got[m][b][1] = 10.0f * fm_log2f((float)(e[1] / BLOCK) + 1e-20f) * 0.30103f;
                    e[0] = e[1] = 0.0;
                    n = 0;
                    if (++b == NBLK)
                        break;
                }
            }
        }
    }
    if (write) {
        FILE *o = fopen(argv[1], "w");
        uint32_t b;
        if (!o)
            return 2;
        fprintf(o, "# tests/host/golden_test.c: dB per %u-sample block, left right, metal by metal\n", BLOCK);
        for (m = 0; m < 4; m++)
            for (b = 0; b < NBLK; b++)
                fprintf(o, "%d %u %.3f %.3f\n", m, b, (double)got[m][b][0], (double)got[m][b][1]);
        fclose(o);
        printf("golden: wrote %s\n", argv[1]);
        return 0;
    }
    {
        FILE *g = fopen(argv[1], "r");
        char line[128];
        int rows = 0;
        if (!g) {
            printf("FAIL golden: no %s\n", argv[1]);
            return 1;
        }
        while (fgets(line, sizeof line, g)) {
            int gm;
            unsigned gb;
            float l, r;
            if (line[0] == '#' || sscanf(line, "%d %u %f %f", &gm, &gb, &l, &r) != 4)
                continue;
            rows++;
            if (gm < 0 || gm > 3 || gb >= NBLK)
                continue;
            if (fm_fabsf(got[gm][gb][0] - l) > 0.01f || fm_fabsf(got[gm][gb][1] - r) > 0.01f) {
                if (fails < 12)
                    printf("FAIL golden: metal %d block %u: %.3f %.3f dB, was %.3f %.3f\n", gm, gb,
                           (double)got[gm][gb][0], (double)got[gm][gb][1], (double)l, (double)r);
                fails++;
            }
        }
        fclose(g);
        if (rows != 4 * (int)NBLK) {
            printf("FAIL golden: %d rows, wanted %d\n", rows, 4 * (int)NBLK);
            fails++;
        }
    }
    printf(fails ? "golden: %d blocks moved\n" : "golden: the sound as it was\n", fails);
    return fails != 0;
}
