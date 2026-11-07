/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * X0X / OMNI: object map changed (see below). HP-1: objects of several sectors, saves from pieces, staged
 * writes (content.h). */
/* Persistent storage on the SPI NOR.
 *
 * Every object has an A/B pair of copies. A save goes to the copy that is not
 * the current one: erase its sectors, program the payload (from offset 256),
 * then the 32-byte header at offset 0 last. The header is the commit record;
 * on load the valid copy with the highest seq wins, so a torn write leaves
 * the previous copy in charge.
 *
 * Flash access goes through three hooks (also used by the host test):
 *   st_read(off, dst, n)   st_erase(off)   st_prog(off, src, n)
 */
#define ST_MAGIC 0x30583058u                   /* "X0X0": never mistaken for a Felucca object */
#define ST_SECTOR 4096u
#define ST_PAYLOAD_OFF 256u
#define ST_PAGE 256u

/* flash map, HP-1 (plat.h OBJ_*): the project at 0xFC000 (Felucca's FL_GLOB), the panel calibration at
 * 0xDC000 (Felucca's user-preset area), the sequence at 0xC0000 (four sectors a copy) and the four
 * project slots from 0x98000 (five sectors a copy, 0xA000 apart): all inside Felucca's data area
 * 0x97000..0xDFFFF, clear of the panel's. Nothing above 0xDFFFF: 0xE0000.. is the update loader's
 * staging area. The magic is X0X's, so a project is told apart by its own magic (project.c).
 *
 * An object may span several sectors a copy: the header sits at the start of the copy's first sector,
 * the payload from offset 256 on, across the rest. Its CRC is taken a page at a time, so no buffer
 * ever holds a whole object. */
typedef struct {
    uint32_t magic;
    uint16_t type, slot;
    uint32_t seq, len, crc, rsv[2];
    uint32_t hcrc;
} st_hdr_t;

static int st_read(uint32_t off, void *dst, uint32_t n);
static int st_erase(uint32_t off);
static int st_prog(uint32_t off, const void *src, uint32_t n);

static uint32_t st_crc_add(uint32_t c, const void *p, uint32_t n)   /* zlib CRC-32, 4 bits per step */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    c = ~c;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return ~c;
}
static uint32_t st_crc32(const void *p, uint32_t n) { return st_crc_add(0u, p, n); }

/* where an object lives: its sectors a copy, and copy A's first sector */
static uint32_t st_nsec(uint32_t obj)
{
    return obj == OBJ_SEQ ? 4u : obj >= OBJ_SLOT0 && obj < OBJ_SLOT0 + PLAT_NSLOT ? 5u : 1u;
}
static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* flash offset of copy A (0) / B (1) */
{
    uint32_t a;
    if (obj == OBJ_PANEL)
        a = 0xDC000u;
    else if (obj == OBJ_SEQ)
        a = 0xC0000u;
    else if (obj >= OBJ_SLOT0 && obj < OBJ_SLOT0 + PLAT_NSLOT)
        a = 0x98000u + (obj - OBJ_SLOT0) * 0xA000u;
    else
        a = 0xFC000u;
    return a + copy * st_nsec(obj) * ST_SECTOR;
}
static uint32_t st_cap(uint32_t obj) { return st_nsec(obj) * ST_SECTOR - ST_PAYLOAD_OFF; }   /* payload room */

static uint8_t st_buf[ST_PAGE] __attribute__((aligned(4)));   /* a page: CRC steps, the driver's RAM source */

/* the copy whose payload last checked out: a read further into it (content.h reads in pieces) skips the
 * check while its header stands; a read from its start checks it again */
static struct { uint32_t obj, copy, seq, hcrc; uint8_t ok; } st_known;

static void st_forget(uint32_t obj, uint32_t copy)    /* that copy is about to change */
{
    if (st_known.obj == obj && st_known.copy == copy)
        st_known.ok = 0;
}

static int st_head(uint32_t obj, uint32_t copy, st_hdr_t *h)   /* commit record valid: 0 */
{
    if (st_read(st_sector(obj, copy), h, sizeof *h))
        return -1;
    if (h->magic != ST_MAGIC || h->type != obj || h->len > st_cap(obj) ||
        h->hcrc != st_crc32(h, sizeof *h - 4u))
        return -1;
    return 0;
}

static int st_body(uint32_t obj, uint32_t copy, const st_hdr_t *h, int trust)   /* the payload's CRC holds: 0 */
{
    uint32_t off, c = 0u, base = st_sector(obj, copy) + ST_PAYLOAD_OFF;
    if (trust && st_known.ok && st_known.obj == obj && st_known.copy == copy && st_known.seq == h->seq && st_known.hcrc == h->hcrc)
        return 0;
    for (off = 0; off < h->len; off += ST_PAGE) {
        uint32_t n = h->len - off > ST_PAGE ? ST_PAGE : h->len - off;
        if (st_read(base + off, st_buf, n))
            return -1;
        c = st_crc_add(c, st_buf, n);
    }
    if (c != h->crc)
        return -1;
    st_known.obj = obj;
    st_known.copy = copy;
    st_known.seq = h->seq;
    st_known.hcrc = h->hcrc;
    st_known.ok = 1;
    return 0;
}

/* the current copy: the valid one with the highest seq (A on a tie), -1 when neither is valid. Headers
 * first, so only the winner's payload is checked; *h gets its header. */
static int st_current_(uint32_t obj, st_hdr_t *h, int trust)
{
    st_hdr_t a, b;
    int va = st_head(obj, 0, &a) == 0, vb = st_head(obj, 1, &b) == 0;
    if (vb && (!va || b.seq > a.seq)) {
        if (st_body(obj, 1, &b, trust) == 0) {
            *h = b;
            return 1;
        }
        vb = 0;
    }
    if (va && st_body(obj, 0, &a, trust) == 0) {
        *h = a;
        return 0;
    }
    if (vb && st_body(obj, 1, &b, trust) == 0) {
        *h = b;
        return 1;
    }
    return -1;
}
static int st_current(uint32_t obj, st_hdr_t *h) { return st_current_(obj, h, 0); }

/* n bytes of the object's current payload from offset off into dst: the bytes read (0 past its end), -1
 * when it holds nothing */
static int st_load_at(uint32_t obj, uint32_t off, void *dst, uint32_t n)
{
    st_hdr_t h;
    int cur = st_current_(obj, &h, off > 0u);
    if (cur < 0)
        return -1;
    if (off >= h.len)
        return 0;
    if (n > h.len - off)
        n = h.len - off;
    if (n && st_read(st_sector(obj, (uint32_t)cur) + ST_PAYLOAD_OFF + off, dst, n))
        return -1;
    return (int)n;
}

/* load object into dst (up to max bytes); returns the length, or -1 */
static int st_load(uint32_t obj, void *dst, uint32_t max) { return st_load_at(obj, 0u, dst, max); }

/* the current payload's length and CRC: 0, or -1 when the object holds nothing */
static int st_info(uint32_t obj, uint32_t *len, uint32_t *crc)
{
    st_hdr_t h;
    if (st_current(obj, &h) < 0)
        return -1;
    *len = h.len;
    *crc = h.crc;
    return 0;
}

/* program n bytes at a payload offset of the copy at base, a page at a time through st_buf (the driver
 * wants RAM sources), and read them back */
static int st_put(uint32_t base, uint32_t off, const uint8_t *src, uint32_t n)
{
    while (n) {
        uint32_t a = base + ST_PAYLOAD_OFF + off, k = ST_PAGE - (a & (ST_PAGE - 1u)), i;
        int rc;
        if (k > n)
            k = n;
        for (i = 0; i < k; i++)
            st_buf[i] = src[i];
        if ((rc = st_prog(a, st_buf, k)) != 0)
            return rc;
        if (st_read(a, st_buf, k))
            return -7;
        for (i = 0; i < k; i++)
            if (st_buf[i] != src[i])
                return -7;                             /* (a byte programmed twice, or a failing part) */
        off += k;
        src += k;
        n -= k;
    }
    return 0;
}

static int st_erase_copy(uint32_t obj, uint32_t copy)
{
    uint32_t i;
    int rc;
    for (i = 0; i < st_nsec(obj); i++)
        if ((rc = st_erase(st_sector(obj, copy) + i * ST_SECTOR)) != 0)
            return rc;
    return 0;
}

/* the commit record, last; then read back with its payload */
static int st_commit(uint32_t obj, uint32_t copy, uint32_t seq, uint32_t len, uint32_t crc)
{
    st_hdr_t h, chk;
    int rc;
    h.magic = ST_MAGIC;
    h.type = (uint16_t)obj;
    h.slot = (uint16_t)copy;
    h.seq = seq;
    h.len = len;
    h.crc = crc;
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    st_forget(obj, copy);
    if ((rc = st_prog(st_sector(obj, copy), &h, sizeof h)) != 0)       /* the commit record, last */
        return rc;
    /* read back: a write-protected or failing part must not report SAVED */
    if (st_head(obj, copy, &chk) || chk.seq != h.seq || st_body(obj, copy, &chk, 0))
        return -7;
    return 0;
}

/* a staged write (content.h): the payload goes into the copy that is not current, in pieces, and its
 * commit record is written only once it is whole and checked. Until then the current copy stays in
 * charge; abandoned, the staged copy is only an invalid one. */
static struct { uint8_t on; uint32_t obj, copy, len, crc, seq; } st_stage;

/* save from pieces (one payload, several RAM buffers) */
typedef struct { const void *p; uint32_t n; } st_part_t;
static int st_savev(uint32_t obj, const st_part_t *part, int nparts)
{
    uint32_t len = 0, crc = 0u, off = 0, copy;
    int cur, rc, i;
    st_hdr_t h;
    if (obj >= OBJ_NOBJ || (st_stage.on && st_stage.obj == obj))
        return -10;                                    /* (being written from outside, content.h) */
    for (i = 0; i < nparts; i++) {
        len += part[i].n;
        crc = st_crc_add(crc, part[i].p, part[i].n);
    }
    if (len > st_cap(obj))
        return -1;
    cur = st_current(obj, &h);
    copy = cur == 0 ? 1u : 0u;                         /* write the other copy */
    st_forget(obj, copy);
    if ((rc = st_erase_copy(obj, copy)) != 0)
        return rc;
    for (i = 0; i < nparts; i++) {
        if ((rc = st_put(st_sector(obj, copy), off, part[i].p, part[i].n)) != 0)
            return rc;
        off += part[i].n;
    }
    return st_commit(obj, copy, (cur < 0 ? 0u : h.seq) + 1u, len, crc);
}

static int st_save(uint32_t obj, const void *src, uint32_t len)
{
    st_part_t p;
    p.p = src;
    p.n = len;
    return st_savev(obj, &p, 1);
}

static int st_stage_begin(uint32_t obj, uint32_t len, uint32_t crc)
{
    st_hdr_t h;
    int cur, rc;
    st_stage.on = 0;
    if (obj >= OBJ_NOBJ || len > st_cap(obj))
        return -1;
    cur = st_current(obj, &h);
    st_stage.obj = obj;
    st_stage.copy = cur == 0 ? 1u : 0u;
    st_stage.len = len;
    st_stage.crc = crc;
    st_stage.seq = (cur < 0 ? 0u : h.seq) + 1u;
    st_forget(obj, st_stage.copy);
    if ((rc = st_erase_copy(obj, st_stage.copy)) != 0)
        return rc;
    st_stage.on = 1;
    return 0;
}

static int st_stage_write(uint32_t off, const void *src, uint32_t n)
{
    if (!st_stage.on || off > st_stage.len || n > st_stage.len - off)
        return -1;
    return st_put(st_sector(st_stage.obj, st_stage.copy), off, src, n);
}

static int st_stage_read(uint32_t off, void *dst, uint32_t n)
{
    if (!st_stage.on || off > st_stage.len || n > st_stage.len - off)
        return -1;
    return st_read(st_sector(st_stage.obj, st_stage.copy) + ST_PAYLOAD_OFF + off, dst, n);
}

/* the staged payload whole and as meant (its CRC), then published */
static int st_stage_commit(void)
{
    uint32_t off, c = 0u, base;
    if (!st_stage.on)
        return -1;
    st_stage.on = 0;
    base = st_sector(st_stage.obj, st_stage.copy) + ST_PAYLOAD_OFF;
    for (off = 0; off < st_stage.len; off += ST_PAGE) {
        uint32_t n = st_stage.len - off > ST_PAGE ? ST_PAGE : st_stage.len - off;
        if (st_read(base + off, st_buf, n))
            return -1;
        c = st_crc_add(c, st_buf, n);
    }
    if (c != st_stage.crc)
        return -11;
    return st_commit(st_stage.obj, st_stage.copy, st_stage.seq, st_stage.len, st_stage.crc);
}

static void st_stage_abort(void) { st_stage.on = 0; }

/* the object emptied: both copies erased, the one not in charge first (a cut between leaves it as it was) */
static int st_clear(uint32_t obj)
{
    st_hdr_t h;
    uint32_t other;
    int rc;
    if (obj >= OBJ_NOBJ || (st_stage.on && st_stage.obj == obj))
        return -10;
    other = st_current(obj, &h) == 0 ? 1u : 0u;
    st_forget(obj, 0u);
    st_forget(obj, 1u);
    if ((rc = st_erase_copy(obj, other)) != 0)
        return rc;
    return st_erase_copy(obj, 1u - other);
}
