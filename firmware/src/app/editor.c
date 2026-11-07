/* SPDX-License-Identifier: GPL-3.0-only
 * The 7-bit encodings (pack7, u32 in five 7-bit bytes) and the staged transfer's shape after Melodee's editor
 * protocol (firmware/src/editor.c, editor_backup.c), Copyright (C) 2026 Kerem Kilic (Ellic Studio),
 * Portions Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments. */
/* HP-1's content manager over SysEx (docs/EDITOR_PROTOCOL.md): the web page reads and writes the working
 * project, its sequence and the four project slots (content.h) through this.
 *   F0 7D 48 50 cmd args.. F7     (7D: non-commercial; "HP", never Felucca's / Melodee's 7D 46 4C)
 * Every request with a known cmd gets exactly one reply, with the same header and cmd. Every byte is 7 bit:
 * u14 two bytes and u32 five, LSB first; a string ASCII, ended by a 0; data pack7 (groups of up to seven
 * bytes, each after a byte of their top bits, bit j for byte j). Anything that writes is refused (rc 3) while
 * the transport runs or recording is armed: a flash erase silences the audio for a moment.
 * The platform hands in a frame (the bytes between F0 and F7) with ed_handle() from the main loop and
 * supplies ed_reply(); the device's frames come through usb.c (main_fm1.c), the simulator's from its script,
 * a pipe or the browser (hp1_host.c). Part of the unity build, after ui.c (its say() and dirty flag). */
#define ED_H0 0x7Du
#define ED_H1 0x48u
#define ED_H2 0x50u
#define ED_PROTO 1u
#define ED_CHUNK 256u                       /* a GET's or PUT's most bytes */
#define ED_STALE_MS 15000u                  /* a staged write left this long is dropped */
enum { ED_INFO = 1, ED_LIST, ED_GET, ED_PUT, ED_SLOT, ED_SAVE };
enum { ED_OK, ED_INVALID, ED_BAD, ED_BUSY, ED_FLASH, ED_STALE };   /* rc */
enum { ED_BEGIN, ED_DATA, ED_COMMIT, ED_ABORT };                    /* PUT's op */
enum { ED_SL_SAVE, ED_SL_LOAD, ED_SL_RENAME, ED_SL_CLEAR };          /* SLOT's op */

static void ed_reply(const uint8_t *p, uint32_t n);   /* the platform's: one F0..F7 frame to the host */

static uint8_t ed_out[16u + ED_CHUNK + (ED_CHUNK + 6u) / 7u];   /* the longest reply: a GET's */
static uint8_t ed_buf[ED_CHUNK];                                 /* a chunk's bytes */
static uint32_t ed_n;
static struct {
    uint8_t put, id, rc;                    /* a staged write (its id), the last reply's rc (the host's tests) */
    uint32_t size, ms;                      /* its size, its last request */
} ed;

static void ed_b(uint32_t v)
{
    if (ed_n < sizeof ed_out - 1u)
        ed_out[ed_n++] = (uint8_t)(v & 0x7Fu);
}
static void ed_u32(uint32_t v)
{
    int i;
    for (i = 0; i < 5; i++)
        ed_b(v >> (7 * i));
}
static void ed_str(const char *s)
{
    int i;
    for (i = 0; s[i] && i < CT_NAME - 1; i++)
        ed_b((uint8_t)s[i]);
    ed_b(0);
}
static void ed_pack7(const uint8_t *p, uint32_t n)
{
    uint32_t i, j;
    for (i = 0; i < n; i += 7u) {
        const uint32_t k = n - i < 7u ? n - i : 7u;
        uint32_t m = 0;
        for (j = 0; j < k; j++)
            m |= (uint32_t)(p[i + j] >> 7) << j;
        ed_b(m);
        for (j = 0; j < k; j++)
            ed_b(p[i + j]);
    }
}
/* pack7 back: the bytes (<= max), 0 for a malformed run (an empty group, a mask bit past its group) */
static uint32_t ed_unpack7(const uint8_t *a, uint32_t na, uint8_t *out, uint32_t max)
{
    uint32_t n = 0;
    while (na) {
        const uint32_t m = *a++, k = --na > 7u ? 7u : na;
        uint32_t j;
        if (!k || n + k > max || (m >> k))
            return 0;
        for (j = 0; j < k; j++, na--)
            out[n++] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
    }
    return n;
}
static uint32_t ed_r32(const uint8_t *a)
{
    return (uint32_t)a[0] | (uint32_t)a[1] << 7 | (uint32_t)a[2] << 14 | (uint32_t)a[3] << 21 | (uint32_t)a[4] << 28;
}

static int ed_busy(void) { return seq_running() || seq_armed(); }
static int ed_unsaved(void) { return ui.dirty || seq_dirty(); }

static void ed_drop(void)                   /* the staged write, given up */
{
    if (ed.put)
        ct_write_abort();
    ed.put = 0;
}

/* ---------------------------------------------------------- commands --- */
static void ed_info(void)
{
    ed_b(ED_PROTO);
    ed_b(CT_N);
    ed_b(PLAT_NSLOT);
    ed_b(ED_CHUNK);
    ed_b(ED_CHUNK >> 7);
    ed_b((uint32_t)ed_busy() | (uint32_t)ed_unsaved() << 1);   /* bit 0: playing, bit 1: changes not saved yet */
    ed_str(OM_VERSION);
}

static void ed_list(void)
{
    int i;
    ed_b(CT_N);
    for (i = 0; i < CT_N; i++) {
        ct_info_t o;
        ct_info(i, &o);
        ed_b((uint32_t)i);
        ed_b(i == CT_PROJECT ? 0u : i == CT_SEQUENCE ? 1u : 2u);   /* kind: the project, its sequence, a slot */
        ed_b(o.used);
        ed_u32(o.size);
        ed_u32(o.crc);
        ed_u32(o.cap);
        ed_str(o.name);
    }
}

/* id, off u32, count u14 (1..256) -> id, rc, off u32, count u14, pack7 data */
static uint32_t ed_get(const uint8_t *a, uint32_t n)
{
    ct_info_t o;
    uint32_t off = 0, cnt = 0, rc = ED_OK;
    if (n != 8u || a[5] > 15u || ct_info(a[0], &o) || !o.used)
        rc = ED_INVALID;
    else {
        off = ed_r32(a + 1);
        cnt = (uint32_t)a[6] | (uint32_t)a[7] << 7;
        if (!cnt || cnt > ED_CHUNK || off > o.size || cnt > o.size - off)
            rc = ED_INVALID;
        else if (ct_read(a[0], off, ed_buf, cnt) != (int)cnt)
            rc = ED_FLASH;
    }
    ed_b(n ? a[0] : 127u);
    ed_b(rc);
    ed_u32(off);
    ed_b(rc ? 0u : cnt);
    ed_b(rc ? 0u : cnt >> 7);
    if (!rc)
        ed_pack7(ed_buf, cnt);
    return rc;
}

/* op 0 begin: id, size u32, crc u32; 1 data: id, off u32, pack7; 2 commit: id; 3 abort: id -> op, id, rc */
static uint32_t ed_put_(const uint8_t *a, uint32_t n)
{
    const uint32_t now = plat_ms();
    ct_info_t o;
    uint32_t cnt, off;
    if (n < 2u || a[0] > ED_ABORT || a[1] >= CT_N)
        return ED_INVALID;
    if (a[0] == ED_ABORT) {
        ed_drop();
        return n == 2u ? ED_OK : ED_INVALID;
    }
    if (a[0] == ED_BEGIN) {
        uint32_t size;
        if (n != 12u || a[6] > 15u || a[11] > 15u)
            return ED_INVALID;
        size = ed_r32(a + 2);
        ct_info(a[1], &o);
        if (!size || size > o.cap)
            return ED_INVALID;
        if (ed_busy())
            return ED_BUSY;
        ed_drop();
        if (ct_write_begin(a[1], size, ed_r32(a + 7)))
            return ED_FLASH;
        ed.put = 1;
        ed.id = a[1];
        ed.size = size;
        ed.ms = now;
        return ED_OK;
    }
    if (!ed.put || ed.id != a[1] || now - ed.ms > ED_STALE_MS) {   /* no begin, another object's, or abandoned */
        ed_drop();
        return ED_STALE;
    }
    ed.ms = now;
    if (a[0] == ED_COMMIT) {
        int rc;
        if (n != 2u)
            return ED_INVALID;
        if (ed_busy()) {
            ed_drop();
            return ED_BUSY;
        }
        ed.put = 0;
        rc = ct_write_commit();
        if (rc == -3)
            return ED_BAD;
        if (rc)
            return ED_FLASH;
        if (a[1] == CT_PROJECT)
            ui.dirty = 0;                   /* (what's in RAM is what was saved) */
        if (a[1] <= CT_SEQUENCE)
            say("RECEIVED", a[1] == CT_PROJECT ? "PROJECT" : "SEQUENCE");
        return ED_OK;
    }
    if (n < 9u || a[6] > 15u)
        return ED_INVALID;
    off = ed_r32(a + 2);
    cnt = ed_unpack7(a + 7, n - 7u, ed_buf, ED_CHUNK);
    if (!cnt || off > ed.size || cnt > ed.size - off)
        return ED_INVALID;
    if (ct_write_data(off, ed_buf, cnt)) {
        ed_drop();
        return ED_FLASH;
    }
    return ED_OK;
}

/* op, slot (0..3) [, name string (rename)] -> op, slot, rc, used, name */
static uint32_t ed_slot(const uint8_t *a, uint32_t n)
{
    uint32_t rc = ED_OK;
    const int s = n >= 2u ? a[1] : 0;
    char name[CT_NAME], t[2] = {0, 0};
    if (n < 2u || a[0] > ED_SL_CLEAR || a[1] >= PLAT_NSLOT)
        rc = ED_INVALID;
    else if (a[0] == ED_SL_RENAME) {
        uint32_t i;
        for (i = 0; i < CT_NAME && 2u + i < n && a[2u + i]; i++)
            name[i] = (char)a[2u + i];
        if (i == CT_NAME || 2u + i != n - 1u || a[2u + i])
            rc = ED_INVALID;                /* (one string, ended by its 0, as the last thing) */
        else
            name[i] = 0;
    } else if (n != 2u)
        rc = ED_INVALID;
    if (!rc && ed_busy())
        rc = ED_BUSY;
    if (!rc) {
        ed_drop();                          /* (a slot's own writes use the staging too) */
        t[0] = (char)('A' + s);
        switch (a[0]) {
        case ED_SL_SAVE:
            rc = ct_slot_save(s) ? ED_FLASH : ED_OK;
            if (!rc)
                say("SAVED TO", t);
            break;
        case ED_SL_LOAD:
            if (!ct_slot_used(s)) {
                rc = ED_INVALID;
                break;
            }
            set_freeze(0);
            if (ct_slot_load(s)) {
                rc = ED_BAD;
                break;
            }
            ui.step = 0;
            say("LOADED", sq.h.name[0] ? sq.h.name : "A PROJECT");
            break;
        case ED_SL_RENAME: {
            const int r = ct_slot_rename(s, name);   /* -1: a name it can't show, or an empty slot */
            rc = r == -1 ? ED_INVALID : r ? ED_FLASH : ED_OK;
            break;
        }
        default:
            rc = ct_slot_clear(s) ? ED_FLASH : ED_OK;
            if (!rc)
                say("CLEARED", t);
        }
    }
    ed_b(n ? a[0] : 127u);
    ed_b(n >= 2u ? a[1] : 127u);
    ed_b(rc);
    ed_b((uint32_t)ct_slot_used(s));
    ed_str(ct_slot_name(s));
    return rc;
}

/* the working project and sequence into flash now, if changed (as SAVE does) -> rc */
static uint32_t ed_save(void)
{
    ct_info_t o;
    int rc = 0;
    if (ed_busy())
        return ED_BUSY;
    ed_drop();
    if (ui.dirty || (ct_info(CT_PROJECT, &o) == 0 && !o.used)) {   /* (never saved yet: saved now, to be read) */
        rc = project_save();
        if (!rc)
            ui.dirty = 0;
    }
    if (seq_dirty() || (ct_info(CT_SEQUENCE, &o) == 0 && !o.used))
        rc |= seq_save();
    return rc ? ED_FLASH : ED_OK;
}

/* one frame (the bytes between F0 and F7): 0 = not this protocol's (left for the updater) */
static int ed_handle(const uint8_t *p, uint32_t n)
{
    uint32_t cmd, rc = ED_OK;
    if (n < 4u || p[0] != ED_H0 || p[1] != ED_H1 || p[2] != ED_H2)
        return 0;
    cmd = p[3];
    if (cmd < ED_INFO || cmd > ED_SAVE)
        return 1;                           /* (a newer page's: no reply, it reads INFO's version) */
    ed_out[0] = 0xF0;
    ed_out[1] = ED_H0;
    ed_out[2] = ED_H1;
    ed_out[3] = ED_H2;
    ed_out[4] = (uint8_t)cmd;
    ed_n = 5;
    switch (cmd) {
    case ED_INFO: ed_info(); break;
    case ED_LIST: ed_list(); break;
    case ED_GET: rc = ed_get(p + 4, n - 4u); break;
    case ED_PUT:
        rc = ed_put_(p + 4, n - 4u);
        ed_b(n > 4u ? p[4] : 127u);
        ed_b(n > 5u ? p[5] : 127u);
        ed_b(rc);
        break;
    case ED_SLOT: rc = ed_slot(p + 4, n - 4u); break;
    default:
        rc = ed_save();
        ed_b(rc);
    }
    ed.rc = (uint8_t)rc;
    ed_out[ed_n++] = 0xF7;
    ed_reply(ed_out, ed_n);
    return 1;
}
