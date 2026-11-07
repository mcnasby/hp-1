/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of firmware/src/storage.c against a simulated NOR flash:
 * erase -> 0xFF, program can only clear bits, page writes must not wrap. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint8_t nor[0x100000];
static int fail_after = -1;            /* torn-write injection: stop after N programs */

static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (fail_after == 0)
        return -9;
    if (fail_after > 0)
        fail_after--;
    if ((off & 0xFFu) + n > 256u) {
        printf("page wrap at %#x\n", off);
        exit(1);
    }
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    return 0;
}
#include "../firmware/src/app/plat.h"     /* OMNI: the object map */
#include "../firmware/src/storage.c"
#define OBJ_PROJECT0 OBJ_PROJ                  /* HP-1: the project, the panel, the sequence, the slots */
#define OBJ_SETTINGS OBJ_PANEL

static int check(const char *what, int ok)
{
    printf("%-46s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

int main(void)
{
    char a[600], b[600], got[600];
    int bad = 0, n;
    memset(nor, 0xFF, sizeof nor);
    memset(a, 'A', sizeof a);
    memset(b, 'B', sizeof b);
    bad += check("empty flash loads nothing", st_load(OBJ_PROJECT0, got, sizeof got) < 0);
    bad += check("save A", st_save(OBJ_PROJECT0, a, sizeof a) == 0);
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("load returns A", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    bad += check("save B", st_save(OBJ_PROJECT0, b, sizeof b) == 0);
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("load returns B (newer seq)", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    fail_after = 1;                                  /* payload page 1 written, the rest torn */
    st_save(OBJ_PROJECT0, a, sizeof a);
    fail_after = -1;
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("torn save keeps B", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    fail_after = 3;                                  /* all payload pages, header torn */
    st_save(OBJ_PROJECT0, a, sizeof a);
    fail_after = -1;
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("header not written keeps B", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    bad += check("save A again", st_save(OBJ_PROJECT0, a, sizeof a) == 0);
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("load returns A", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    nor[st_sector(OBJ_PROJECT0, 0) + ST_PAYLOAD_OFF + 10] ^= 0x01;   /* bit rot in one copy */
    nor[st_sector(OBJ_PROJECT0, 1) + ST_PAYLOAD_OFF + 10] ^= 0x01;
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("both copies corrupt -> nothing", n < 0);
    bad += check("other objects untouched", st_load(OBJ_PANEL, got, sizeof got) < 0);
    bad += check("settings save/load",
                 st_save(OBJ_SETTINGS, "hello", 5) == 0 && st_load(OBJ_SETTINGS, got, 5) == 5 && !memcmp(got, "hello", 5));
    bad += check("CRC-32 is zlib's (check value 0xCBF43926)", st_crc32("123456789", 9) == 0xCBF43926u);
    memset(nor, 0xFF, sizeof nor);
    st_save(OBJ_PANEL, a, sizeof a);
    st_save(OBJ_PANEL, b, sizeof b);              /* B is newer, in the other copy */
    {
        st_hdr_t h;
        int cur = st_current(OBJ_PANEL, &h);
        nor[st_sector(OBJ_PANEL, (uint32_t)cur) + ST_PAYLOAD_OFF + 300] ^= 0x10;   /* rot in the newer copy */
    }
    n = st_load(OBJ_PANEL, got, sizeof got);
    bad += check("newer copy rotten -> the older one loads", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    bad += check("save over the rotten copy", st_save(OBJ_PANEL, b, sizeof b) == 0);
    n = st_load(OBJ_PANEL, got, sizeof got);
    bad += check("... loads the new data", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    nor[st_sector(OBJ_PANEL, 0) + 8] ^= 0x01;    /* both headers broken */
    nor[st_sector(OBJ_PANEL, 1) + 8] ^= 0x01;
    bad += check("both headers broken -> nothing", st_load(OBJ_PANEL, got, sizeof got) < 0);
    {   /* HP-1 map: every copy inside Felucca's data area (or its globals), none over the panel's, none
         * reaching the update loader's staging (0xE0000..), no overlaps */
        uint32_t o, p, ok = 1;
        for (o = 0; o < OBJ_NOBJ; o++) {
            uint32_t a0 = st_sector(o, 0), a1 = st_sector(o, 1) + st_nsec(o) * 4096;
            int in_data = (a0 >= 0x97000 && a1 <= 0xE0000) || (a0 >= 0xFC000 && a1 <= 0xFF000);
            if (!in_data || a1 - a0 != 2 * st_nsec(o) * 4096)
                ok = 0;
            if (o != OBJ_PANEL && a0 < 0xDE000 && 0xDC000 < a1)
                ok = 0;
            for (p = 0; p < o; p++)
                if (a0 < st_sector(p, 1) + st_nsec(p) * 4096 && st_sector(p, 0) < a1)
                    ok = 0;
        }
        bad += check("objects in the data area, apart", (int)ok);
        bad += check("the sequence at 0xC0000, four sectors a copy", st_sector(OBJ_SEQ, 0) == 0xC0000 && st_sector(OBJ_SEQ, 1) == 0xC4000);
        bad += check("slot D at 0xB6000, five sectors a copy",
                     st_sector(OBJ_SLOT0 + 3, 0) == 0xB6000 && st_sector(OBJ_SLOT0 + 3, 1) == 0xBB000);
    }
    {   /* objects of several sectors: whole, torn, from pieces, read at an offset */
        static uint8_t big[20224], big2[20224], back[20224];
        uint32_t i, len, crc;
        st_part_t part[3];
        for (i = 0; i < sizeof big; i++) {
            big[i] = (uint8_t)(i * 7u + (i >> 8));
            big2[i] = (uint8_t)(i * 13u + 5u);
        }
        memset(nor, 0xFF, sizeof nor);
        bad += check("sequence: 16128 bytes fit, 16129 don't", st_cap(OBJ_SEQ) == 16128 && st_save(OBJ_SEQ, big, 16129) == -1);
        bad += check("sequence: save 16000 bytes", st_save(OBJ_SEQ, big, 16000) == 0);
        n = st_load(OBJ_SEQ, back, sizeof back);
        bad += check("... loads them all", n == 16000 && !memcmp(back, big, 16000));
        fail_after = 40;                                 /* 40 pages of 63 written, then the power goes */
        st_save(OBJ_SEQ, big2, 16000);
        fail_after = -1;
        n = st_load(OBJ_SEQ, back, sizeof back);
        bad += check("torn save across sectors keeps the old copy", n == 16000 && !memcmp(back, big, 16000));
        fail_after = 63;                                 /* every page, not the header */
        st_save(OBJ_SEQ, big2, 16000);
        fail_after = -1;
        n = st_load(OBJ_SEQ, back, sizeof back);
        bad += check("... and with only its header missing", n == 16000 && !memcmp(back, big, 16000));
        part[0].p = big2;
        part[0].n = 100;
        part[1].p = big2 + 100;
        part[1].n = 9000;
        part[2].p = big2 + 9100;
        part[2].n = 11124;
        bad += check("slot: save 20224 bytes from three pieces", st_savev(OBJ_SLOT0 + 2, part, 3) == 0);
        n = st_load(OBJ_SLOT0 + 2, back, sizeof back);
        bad += check("... loads as one", n == 20224 && !memcmp(back, big2, 20224));
        n = st_load_at(OBJ_SLOT0 + 2, 12345, back, 100);
        bad += check("... read at an offset", n == 100 && !memcmp(back, big2 + 12345, 100));
        bad += check("... read past the end: 0", st_load_at(OBJ_SLOT0 + 2, 20224, back, 10) == 0);
        bad += check("... its size and CRC", st_info(OBJ_SLOT0 + 2, &len, &crc) == 0 && len == 20224 && crc == st_crc32(big2, 20224));
        bad += check("the other slots untouched", st_load(OBJ_SLOT0 + 1, back, 10) < 0 && st_load(OBJ_SLOT0 + 3, back, 10) < 0);
        bad += check("the sequence untouched", st_load(OBJ_SEQ, back, sizeof back) == 16000 && !memcmp(back, big, 16000));
        nor[st_sector(OBJ_SLOT0 + 2, 0) + 4096 * 3 + 77] ^= 0x04;   /* rot in its fourth sector */
        nor[st_sector(OBJ_SLOT0 + 2, 1) + 4096 * 3 + 77] ^= 0x04;
        bad += check("rot past the first sector: caught", st_load(OBJ_SLOT0 + 2, back, sizeof back) < 0);

        /* staged writes (content.h): pieces at any offsets, the old copy in charge until the commit */
        bad += check("stage: begin", st_stage_begin(OBJ_SEQ, 5000, st_crc32(big2, 5000)) == 0);
        bad += check("stage: pieces out of order", st_stage_write(3000, big2 + 3000, 2000) == 0 && st_stage_write(0, big2, 3000) == 0);
        bad += check("stage: past the end refused", st_stage_write(4990, big2, 20) != 0);
        bad += check("stage: a save of it meanwhile refused", st_save(OBJ_SEQ, big, 10) == -10);
        n = st_load(OBJ_SEQ, back, sizeof back);
        bad += check("stage: the old copy still in charge", n == 16000 && !memcmp(back, big, 16000));
        bad += check("stage: commit", st_stage_commit() == 0);
        n = st_load(OBJ_SEQ, back, sizeof back);
        bad += check("stage: the new one loads", n == 5000 && !memcmp(back, big2, 5000));
        bad += check("stage: a wrong CRC refused at commit",
                     st_stage_begin(OBJ_SEQ, 100, 0x12345678u) == 0 && st_stage_write(0, big, 100) == 0 && st_stage_commit() == -11);
        n = st_load(OBJ_SEQ, back, sizeof back);
        bad += check("... and the old one kept", n == 5000 && !memcmp(back, big2, 5000));
        bad += check("stage: a missing piece refused",
                     st_stage_begin(OBJ_SEQ, 600, st_crc32(big, 600)) == 0 && st_stage_write(0, big, 300) == 0 && st_stage_commit() != 0);
        st_stage_begin(OBJ_SEQ, 600, st_crc32(big, 600));
        st_stage_write(0, big, 600);
        st_stage_abort();
        bad += check("stage: aborted, nothing published", st_stage_commit() != 0 && st_load(OBJ_SEQ, back, sizeof back) == 5000);
        bad += check("stage: a byte written twice, differently, refused",
                     st_stage_begin(OBJ_SEQ, 300, 0u) == 0 && st_stage_write(0, big, 300) == 0 && st_stage_write(10, big2 + 10, 1) != 0);
        st_stage_abort();
        bad += check("a save after them all", st_save(OBJ_SEQ, big, 700) == 0 && st_load(OBJ_SEQ, back, sizeof back) == 700);
    }
    printf("%s\n", bad ? "STORAGE TEST FAILED" : "storage test passed");
    return bad != 0;
}
