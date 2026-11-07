/* SPDX-License-Identifier: GPL-3.0-only */
/* The content manager's SysEx protocol (firmware/src/app/editor.c, docs/EDITOR_PROTOCOL.md) on the host
 * simulator: frames in, replies out, over the device's own storage on the simulated flash. INFO, LIST, GET
 * read back to the byte and its CRC, staged PUTs (a good one taken; a wrong CRC, a wrong kind, a stray
 * piece refused with the old one kept), the slots' save / load / rename / clear, and rc 3 while playing. */
#define OM_HOST_LIB 1
#include "../../host/hp1_host.c"

static int failed;
static void ok(int c, const char *what)
{
    printf("%-66s %s\n", what, c ? "ok" : "FAIL");
    failed += !c;
}

static uint8_t fr[2048];
static uint32_t frn;
static void f_begin(uint32_t cmd)
{
    fr[0] = 0xF0;
    fr[1] = 0x7D;
    fr[2] = 0x48;
    fr[3] = 0x50;
    fr[4] = (uint8_t)cmd;
    frn = 5;
}
static void f_b(uint32_t v) { fr[frn++] = (uint8_t)(v & 0x7F); }
static void f_u32(uint32_t v)
{
    int i;
    for (i = 0; i < 5; i++)
        f_b(v >> (7 * i));
}
static void f_pack(const uint8_t *p, uint32_t n)
{
    uint32_t i, j;
    for (i = 0; i < n; i += 7) {
        uint32_t k = n - i < 7 ? n - i : 7, m = 0;
        for (j = 0; j < k; j++)
            m |= (uint32_t)(p[i + j] >> 7) << j;
        f_b(m);
        for (j = 0; j < k; j++)
            f_b(p[i + j]);
    }
}
/* send it: the reply's args (after the cmd), their count; -1 no reply */
static const uint8_t *rp;
static int send(void)
{
    fr[frn++] = 0xF7;
    if (!host_sysex(fr, frn))
        return -1;
    if (ed_rx_n < 6 || ed_rx[0] != 0xF0 || ed_rx[1] != 0x7D || ed_rx[2] != 0x48 || ed_rx[3] != 0x50 ||
        ed_rx[4] != fr[4] || ed_rx[ed_rx_n - 1] != 0xF7)
        return -2;
    rp = ed_rx + 5;
    return (int)ed_rx_n - 6;
}
static uint32_t r32(const uint8_t *a) { return a[0] | a[1] << 7 | a[2] << 14 | (uint32_t)a[3] << 21 | (uint32_t)a[4] << 28; }

typedef struct { int used; uint32_t size, crc, cap; char name[16]; } obj_t;
static obj_t objs[CT_N];
static int list(void)
{
    int n, i, p = 1;
    f_begin(2);
    n = send();
    if (n < 1 || rp[0] != CT_N)
        return -1;
    for (i = 0; i < CT_N; i++) {
        int k = 0;
        if (rp[p] != i)
            return -1;
        objs[i].used = rp[p + 2];
        objs[i].size = r32(rp + p + 3);
        objs[i].crc = r32(rp + p + 8);
        objs[i].cap = r32(rp + p + 13);
        p += 18;
        while (rp[p] && k < 15)
            objs[i].name[k++] = (char)rp[p++];
        objs[i].name[k] = 0;
        p++;
    }
    return p == n ? 0 : -1;
}

static uint8_t got[PLAT_STORE_MAX];
static int get_all(int id, uint32_t size)            /* the object read in chunks: 0 */
{
    uint32_t off;
    for (off = 0; off < size; off += 256) {
        uint32_t cnt = size - off < 256 ? size - off : 256, i, j, k = 0;
        int n;
        f_begin(3);
        f_b((uint32_t)id);
        f_u32(off);
        f_b(cnt);
        f_b(cnt >> 7);
        n = send();
        if (n < 9 || rp[0] != id || rp[1] != 0 || r32(rp + 2) != off || (rp[7] | rp[8] << 7) != (int)cnt)
            return -1;
        for (i = 9; i < (uint32_t)n; i += 8) {           /* pack7 back */
            for (j = 0; j < 7 && i + 1 + j < (uint32_t)n; j++)
                got[off + k++] = (uint8_t)(rp[i + 1 + j] | ((rp[i] >> j) & 1) << 7);
        }
        if (k != cnt)
            return -1;
    }
    return 0;
}
static int put_rc(void) { return send() == 3 ? rp[2] : -1; }
static int put(int id, const uint8_t *p, uint32_t size, uint32_t crc)   /* begin, data, commit: the commit's rc */
{
    uint32_t off;
    int rc;
    f_begin(4);
    f_b(0);
    f_b((uint32_t)id);
    f_u32(size);
    f_u32(crc);
    if ((rc = put_rc()) != 0)
        return 100 + rc;
    for (off = 0; off < size; off += 256) {
        uint32_t cnt = size - off < 256 ? size - off : 256;
        f_begin(4);
        f_b(1);
        f_b((uint32_t)id);
        f_u32(off);
        f_pack(p + off, cnt);
        if ((rc = put_rc()) != 0)
            return 200 + rc;
    }
    f_begin(4);
    f_b(2);
    f_b((uint32_t)id);
    return put_rc();
}
static int slot(int op, int s, const char *name)     /* rc */
{
    f_begin(5);
    f_b((uint32_t)op);
    f_b((uint32_t)s);
    if (name) {
        while (*name)
            f_b((uint8_t)*name++);
        f_b(0);
    }
    return send() >= 5 ? rp[2] : -1;
}
static int save(void)
{
    f_begin(6);
    return send() == 1 ? rp[0] : -1;
}

int main(void)
{
    static uint8_t a[PLAT_STORE_MAX], b[PLAT_STORE_MAX];
    int n;
    boot();
    run_ms(200);

    f_begin(1);
    n = send();
    ok(n >= 7 && rp[0] == 1 && rp[1] == CT_N && rp[2] == 4 && (rp[3] | rp[4] << 7) == 256 && !strcmp((const char *)rp + 6, OM_VERSION),
       "INFO: protocol 1, 6 objects, 4 slots, 256-byte chunks, the version");

    /* a project with a sequence: pattern 1 holds three notes */
    proj.par[P_METAL] = MET_EMBER;
    mark_dirty();
    seq_step_put(0, 0, EV_PAN, 6200, 100, 1);
    seq_step_put(0, 4, EV_HIT, HIT_GU, 90, 1);
    seq_step_put(0, 8, EV_PAN, 6900, 80, 2);
    s_cpy(sq.h.name, "MY JAM");
    ok(save() == 0 && !ui.dirty && !seq_dirty(), "SAVE: the working project and sequence into flash");
    ok(list() == 0 && objs[CT_PROJECT].used && objs[CT_PROJECT].size == sizeof proj &&
       objs[CT_PROJECT].crc == plat_crc32(0, &proj, sizeof proj) && !objs[CT_SLOT_A].used && !strcmp(objs[CT_SEQUENCE].name, "MY JAM"),
       "LIST: the project's size and CRC, the sequence's name, empty slots");
    ok(get_all(CT_PROJECT, objs[CT_PROJECT].size) == 0 && !memcmp(got, &proj, sizeof proj), "GET: the project, to the byte");
    ok(get_all(CT_SEQUENCE, objs[CT_SEQUENCE].size) == 0 && objs[CT_SEQUENCE].size == SEQ_BYTES(3) &&
       plat_crc32(0, got, objs[CT_SEQUENCE].size) == objs[CT_SEQUENCE].crc && !memcmp(got, &sq, SEQ_BYTES(3)),
       "GET: the sequence (three events) in chunks, its CRC the listed one");
    f_begin(3); f_b(CT_PROJECT); f_u32(0); f_b(0); f_b(2);       /* 256: more than it holds */
    ok(send() == 9 && rp[1] == 1, "GET: past the end: rc 1");
    f_begin(3); f_b(CT_SLOT_C); f_u32(0); f_b(8); f_b(0);
    ok(send() == 9 && rp[1] == 1, "GET: an empty slot: rc 1");

    /* the slots */
    ok(slot(0, 0, 0) == 0 && rp[3] == 1 && !strcmp((const char *)rp + 4, "MY JAM"), "SLOT save: the working project into A, its name");
    ok(slot(2, 0, "JAM 2") == 0 && !strcmp(ct_slot_name(0), "JAM 2"), "SLOT rename: A is JAM 2");
    ok(slot(2, 0, "jam!") == 1 && !strcmp(ct_slot_name(0), "JAM 2"), "SLOT rename: a name it can't show: rc 1, unchanged");
    ok(slot(1, 2, 0) == 1, "SLOT load: an empty slot: rc 1");
    proj.par[P_METAL] = MET_RAW;
    ok(slot(1, 0, 0) == 0 && proj.par[P_METAL] == MET_EMBER && !strcmp(sq.h.name, "JAM 2") && sq.h.nev == 3,
       "SLOT load: A back into the working project");

    /* staged writes: slot A copied into B through PUT */
    list();
    ok(get_all(CT_SLOT_A, objs[CT_SLOT_A].size) == 0, "GET: slot A");
    memcpy(a, got, objs[CT_SLOT_A].size);
    ok(put(CT_SLOT_B, a, objs[CT_SLOT_A].size, objs[CT_SLOT_A].crc) == 0 && ct_slot_used(1) && !strcmp(ct_slot_name(1), "JAM 2"),
       "PUT: slot A's bytes into B (begin, pieces, commit)");
    list();
    ok(objs[CT_SLOT_B].crc == objs[CT_SLOT_A].crc, "PUT: B's CRC is A's");
    memcpy(b, a, objs[CT_SLOT_A].size);
    memcpy(b + 8, "OTHER\0\0\0", 8);
    ok(put(CT_SLOT_B, b, objs[CT_SLOT_A].size, objs[CT_SLOT_A].crc) == 2, "PUT: bytes that aren't their CRC: refused, rc 2");
    ok(put(CT_SLOT_B, b, objs[CT_SLOT_A].size, plat_crc32(0, b, objs[CT_SLOT_A].size) ^ 1) == 2, "PUT: a wrong CRC: rc 2");
    memset(b, 0x55, 64);
    ok(put(CT_SLOT_B, b, 64, plat_crc32(0, b, 64)) == 2, "PUT: right CRC, not a slot: rc 2");
    list();
    ok(objs[CT_SLOT_B].crc == objs[CT_SLOT_A].crc && !strcmp(ct_slot_name(1), "JAM 2"), "PUT: after three refusals B is as it was");
    f_begin(4); f_b(1); f_b(CT_SLOT_B); f_u32(0); f_pack(a, 16);
    ok(put_rc() == 5, "PUT: a piece with no begin: rc 5 (stale)");
    f_begin(4); f_b(0); f_b(CT_SLOT_B); f_u32(40000); f_u32(0);
    ok(put_rc() == 1, "PUT: bigger than the slot holds: rc 1");
    f_begin(4); f_b(0); f_b(CT_SLOT_B); f_u32(16); f_u32(0);
    put_rc();
    f_begin(4); f_b(1); f_b(CT_SLOT_B); f_u32(0); f_b(0x7F); f_b(1); f_b(2);   /* mask bits past its group */
    ok(put_rc() == 1, "PUT: malformed pack7: rc 1");
    f_begin(4); f_b(2); f_b(CT_SLOT_C);
    ok(put_rc() == 5, "PUT: a commit of another object: rc 5");
    f_begin(4); f_b(0); f_b(CT_SLOT_B); f_u32(16); f_u32(0);
    put_rc();
    run_ms(16000);
    f_begin(4); f_b(1); f_b(CT_SLOT_B); f_u32(0); f_pack(a, 16);
    ok(put_rc() == 5, "PUT: a piece 16 s after its begin: rc 5 (stale)");

    /* the working project, replaced: taken up at once */
    memcpy(b, &proj, sizeof proj);
    ((project_t *)b)->par[P_METAL] = MET_STAINLESS;
    ok(put(CT_PROJECT, b, sizeof proj, plat_crc32(0, b, sizeof proj)) == 0 && proj.par[P_METAL] == MET_STAINLESS,
       "PUT: a working project, taken up (Stainless)");

    /* clear */
    ok(slot(3, 1, 0) == 0 && rp[3] == 0 && !ct_slot_used(1), "SLOT clear: B is empty");
    list();
    ok(!objs[CT_SLOT_B].used && objs[CT_SLOT_A].used, "LIST: B empty, A kept");
    boot();
    ok(!ct_slot_used(1) && ct_slot_used(0), "...and after a reboot");

    /* playing: writes refused */
    seq_play(1);
    run_ms(50);
    f_begin(1);
    ok(send() >= 7 && (rp[5] & 1), "INFO: playing (flags bit 0)");
    ok(save() == 3, "SAVE while playing: rc 3");
    ok(slot(0, 2, 0) == 3 && !ct_slot_used(2), "SLOT save while playing: rc 3, nothing written");
    ok(slot(3, 0, 0) == 3 && ct_slot_used(0), "SLOT clear while playing: rc 3, A kept");
    f_begin(4); f_b(0); f_b(CT_SLOT_C); f_u32(16); f_u32(0);
    ok(put_rc() == 3, "PUT begin while playing: rc 3");
    list();
    ok(get_all(CT_SLOT_A, objs[CT_SLOT_A].size) == 0, "GET while playing: allowed");
    ok(seq_running(), "...and the transport still runs");
    seq_play(0);
    ok(save() == 0, "SAVE once stopped: rc 0");

    /* not ours, unknown */
    f_begin(40);
    ok(send() == -1, "an unknown command: no reply");
    fr[3] = 0x4C;
    fr[4] = 1;
    frn = 5;
    ok(send() == -1, "another header (7D 48 4C): not answered");

    printf(failed ? "EDITOR TESTS FAILED (%d)\n" : "editor tests passed\n", failed);
    return failed ? 1 : 0;
}
