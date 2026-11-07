/* SPDX-License-Identifier: GPL-3.0-only */
/* HP-1's saved content (content.h): the project slots and the content manager's reads and staged
 * writes, over plat.h's storage. Part of the unity build, after project.c and seq.c (the host's too). */
static struct {
    char name[PLAT_NSLOT][CT_NAME];
    uint8_t used[PLAT_NSLOT];
    uint8_t staged;                     /* a write in progress: its id + 1 */
    uint32_t len;                       /* ...and its size */
    uint8_t loaded;                     /* a slot was loaded: the working project to be saved (ct_loaded) */
} ct;

/* the UI asks: a slot loaded since (from here or the web): its project is the working one now, unsaved */
static int ct_loaded(void)
{
    const int l = ct.loaded;
    ct.loaded = 0;
    return l;
}

static uint32_t ct_obj(int id) { return id == CT_PROJECT ? OBJ_PROJ : id == CT_SEQUENCE ? OBJ_SEQ : OBJ_SLOT0 + (uint32_t)(id - CT_SLOT_A); }

static void ct_name_copy(char *d, const char *s)
{
    int i;
    for (i = 0; i < CT_NAME - 1 && s[i]; i++)
        d[i] = s[i];
    for (; i < CT_NAME; i++)
        d[i] = 0;
}

static int ct_name_ok(const char *s)
{
    int i;
    for (i = 0; i < CT_NAME && s[i]; i++)
        if (!((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= '0' && s[i] <= '9') || s[i] == '-' || s[i] == ' '))
            return 0;
    return i < CT_NAME;
}

void ct_refresh(void)
{
    int k;
    for (k = 0; k < PLAT_NSLOT; k++) {
        ct_slot_head_t h;
        ct.used[k] = plat_store_read(OBJ_SLOT0 + (uint32_t)k, 0u, &h, sizeof h) == (int)sizeof h && h.magic == CT_SLOT_MAGIC;
        ct_name_copy(ct.name[k], ct.used[k] && ct_name_ok(h.name) ? h.name : "");
    }
}

int ct_slot_used(int slot) { return slot >= 0 && slot < PLAT_NSLOT && ct.used[slot]; }
const char *ct_slot_name(int slot) { return slot >= 0 && slot < PLAT_NSLOT ? ct.name[slot] : ""; }

/* ------------------------------------------------------------- checks --- */
/* a payload of kind id, read through rd: a valid one (the format's own checks; the CRC is storage's) */
typedef int (*ct_reader_t)(uint32_t obj, uint32_t off, void *dst, uint32_t n);
static int ct_check_proj(ct_reader_t rd, uint32_t obj, uint32_t off, uint32_t len)
{
    uint32_t mf[2];
    if (len < 8u || len > sizeof(project_t) + 512u || rd(obj, off, mf, 8u))
        return -1;
    return mf[0] == PROJ_MAGIC && mf[1] >= 1u && mf[1] <= PROJ_FORMAT ? 0 : -1;
}
static int ct_check_seq(ct_reader_t rd, uint32_t obj, uint32_t off, uint32_t len)
{
    seq_head_t h;
    if (len < sizeof h || rd(obj, off, &h, sizeof h))
        return -1;
    return h.magic == SEQ_MAGIC && h.format >= 1u && h.format <= SEQ_FORMAT && h.nev <= SEQ_NEV && len == SEQ_BYTES(h.nev)
               ? 0 : -1;
}
static int ct_check(int id, ct_reader_t rd, uint32_t obj, uint32_t len)
{
    ct_slot_head_t h;
    if (id == CT_PROJECT)
        return ct_check_proj(rd, obj, 0u, len);
    if (id == CT_SEQUENCE)
        return ct_check_seq(rd, obj, 0u, len);
    if (len < sizeof h || rd(obj, 0u, &h, sizeof h) || h.magic != CT_SLOT_MAGIC || h.format < 1u || h.format > CT_SLOT_FORMAT ||
        !ct_name_ok(h.name) || (uint32_t)sizeof h + h.proj_len + h.seq_len != len)
        return -1;
    if (ct_check_proj(rd, obj, sizeof h, h.proj_len) || ct_check_seq(rd, obj, sizeof h + h.proj_len, h.seq_len))
        return -1;
    return 0;
}
static int ct_rd_stage(uint32_t obj, uint32_t off, void *dst, uint32_t n) { (void)obj; return plat_store_stage_read(off, dst, n); }
static int ct_rd_cur(uint32_t obj, uint32_t off, void *dst, uint32_t n) { return plat_store_read(obj, off, dst, n) == (int)n ? 0 : -1; }

/* ------------------------------------------------------------- listing --- */
int ct_info(int id, ct_info_t *o)
{
    uint32_t len, crc;
    if (id < 0 || id >= CT_N)
        return -1;
    memset(o, 0, sizeof *o);
    o->id = (uint8_t)id;
    o->cap = plat_store_cap(ct_obj(id));
    if (plat_store_info(ct_obj(id), &len, &crc) == 0) {
        o->used = 1;
        o->size = len;
        o->crc = crc;
    }
    ct_name_copy(o->name, id >= CT_SLOT_A ? ct.name[id - CT_SLOT_A] : sq.h.name);
    return 0;
}

int ct_list(ct_info_t *out, int max)
{
    int i;
    for (i = 0; i < max && i < CT_N; i++)
        ct_info(i, &out[i]);
    return i;
}

int ct_read(int id, uint32_t off, void *dst, uint32_t n)
{
    if (id < 0 || id >= CT_N)
        return -1;
    return plat_store_read(ct_obj(id), off, dst, n);
}

/* ------------------------------------------------------------- writing --- */
int ct_write_begin(int id, uint32_t size, uint32_t crc)
{
    ct.staged = 0;
    if (id < 0 || id >= CT_N)
        return -1;
    if (id == CT_SEQUENCE && seq_running())
        seq_play(0);                                     /* (the sequence is about to be replaced) */
    if (plat_store_stage_begin(ct_obj(id), size, crc))
        return -2;
    ct.staged = (uint8_t)(id + 1);
    ct.len = size;
    return 0;
}

int ct_write_data(uint32_t off, const void *src, uint32_t n) { return ct.staged && plat_store_stage_write(off, src, n) == 0 ? 0 : -1; }

void ct_write_abort(void)
{
    ct.staged = 0;
    plat_store_stage_abort();
}

int ct_write_commit(void)
{
    const int id = ct.staged - 1;
    if (!ct.staged)
        return -1;
    ct.staged = 0;
    if (ct_check(id, ct_rd_stage, 0u, ct.len) != 0) {
        plat_store_stage_abort();
        return -3;                                       /* not one of its kind: the old one stays */
    }
    if (plat_store_stage_commit())
        return -4;
    if (id == CT_PROJECT) {                              /* taken up at once */
        project_load();
        project_apply();
    } else if (id == CT_SEQUENCE) {
        seq_play(0);
        seq_load();
        seq_init();
    } else {
        ct_refresh();
    }
    return 0;
}

/* --------------------------------------------------------------- slots --- */
int ct_slot_save(int slot)
{
    ct_slot_head_t h;
    plat_part_t part[3];
    int rc;
    if (slot < 0 || slot >= PLAT_NSLOT)
        return -1;
    memset(&h, 0, sizeof h);
    h.magic = CT_SLOT_MAGIC;
    h.format = CT_SLOT_FORMAT;
    ct_name_copy(h.name, sq.h.name);
    h.proj_len = (uint16_t)sizeof proj;
    h.seq_len = SEQ_BYTES(sq.h.nev);
    part[0].p = &h;
    part[0].n = sizeof h;
    part[1].p = &proj;
    part[1].n = sizeof proj;
    part[2].p = &sq;
    part[2].n = h.seq_len;
    rc = plat_store_savev(OBJ_SLOT0 + (uint32_t)slot, part, 3);
    ct_refresh();
    return rc;
}

int ct_slot_load(int slot)
{
    static const uint8_t KEEP[4] = {P_TUNE, P_MIDICH, P_MIDIOUT, P_THEME};   /* the room and the rig: kept */
    const uint32_t obj = OBJ_SLOT0 + (uint32_t)slot;
    ct_slot_head_t h;
    project_t was = proj;
    uint32_t len, crc;
    int i, n;
    if (slot < 0 || slot >= PLAT_NSLOT || plat_store_info(obj, &len, &crc) || ct_check(CT_SLOT_A + slot, ct_rd_cur, obj, len) ||
        ct_rd_cur(obj, 0u, &h, sizeof h))
        return -1;
    seq_play(0);
    memset(&proj, 0, sizeof proj);
    n = plat_store_read(obj, sizeof h, &proj, h.proj_len < sizeof proj ? h.proj_len : sizeof proj);
    if (n < 8 || !project_valid(n)) {
        proj = was;
        return -2;
    }
    for (i = 0; i < 4; i++)
        proj.par[KEEP[i]] = was.par[KEEP[i]];
    memset(&sq, 0, sizeof sq);
    n = plat_store_read(obj, sizeof h + h.proj_len, &sq, h.seq_len < sizeof sq ? h.seq_len : sizeof sq);
    if (n < (int)sizeof(seq_head_t) || seq_fix())
        seq_clear_all();
    ct_name_copy(sq.h.name, h.name);
    seq_init();
    project_apply();
    seq_set_dirty();                                     /* the working copies follow (autosave) */
    ct.loaded = 1;
    return 0;
}

int ct_slot_rename(int slot, const char *name)
{
    const uint32_t obj = OBJ_SLOT0 + (uint32_t)slot;
    ct_slot_head_t h;
    uint8_t b[256];
    uint32_t len, crc, off, c;
    if (slot < 0 || slot >= PLAT_NSLOT || !ct_name_ok(name) || plat_store_info(obj, &len, &crc) ||
        ct_rd_cur(obj, 0u, &h, sizeof h) || h.magic != CT_SLOT_MAGIC)
        return -1;
    ct_name_copy(h.name, name);
    c = plat_crc32(0u, &h, sizeof h);                     /* the new payload's CRC first, then the copy */
    for (off = sizeof h; off < len; off += sizeof b) {
        uint32_t n = len - off > sizeof b ? (uint32_t)sizeof b : len - off;
        if (ct_rd_cur(obj, off, b, n))
            return -1;
        c = plat_crc32(c, b, n);
    }
    if (plat_store_stage_begin(obj, len, c) || plat_store_stage_write(0u, &h, sizeof h))
        return -2;
    for (off = sizeof h; off < len; off += sizeof b) {
        uint32_t n = len - off > sizeof b ? (uint32_t)sizeof b : len - off;
        if (ct_rd_cur(obj, off, b, n) || plat_store_stage_write(off, b, n)) {
            plat_store_stage_abort();
            return -2;
        }
    }
    if (plat_store_stage_commit())
        return -3;
    ct_refresh();
    return 0;
}
