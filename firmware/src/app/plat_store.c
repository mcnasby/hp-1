/* SPDX-License-Identifier: GPL-3.0-only */
/* plat.h's storage calls over storage.c (its st_* hooks and flash_ok come first): the device's
 * (plat_fm1.c) and the host simulator's, which keeps a simulated NOR in memory. */
#ifndef PLAT_STORE_DONE
#define PLAT_STORE_DONE(rc) ((void)(rc))       /* (the host counts the writes) */
#endif
static int plat_store_done(int rc)
{
    PLAT_STORE_DONE(rc);
    return rc;
}

int plat_store_load(uint32_t obj, void *dst, uint32_t max) { return flash_ok && obj < OBJ_NOBJ ? st_load(obj, dst, max) : -1; }
int plat_store_save(uint32_t obj, const void *src, uint32_t len) { return flash_ok ? plat_store_done(st_save(obj, src, len)) : -9; }
int plat_store_savev(uint32_t obj, const plat_part_t *part, int n)
{
    return flash_ok ? plat_store_done(st_savev(obj, (const st_part_t *)part, n)) : -9;
}
int plat_store_info(uint32_t obj, uint32_t *len, uint32_t *crc) { return flash_ok && obj < OBJ_NOBJ ? st_info(obj, len, crc) : -1; }
int plat_store_read(uint32_t obj, uint32_t off, void *dst, uint32_t n)
{
    return flash_ok && obj < OBJ_NOBJ ? st_load_at(obj, off, dst, n) : -1;
}
uint32_t plat_store_cap(uint32_t obj) { return obj < OBJ_NOBJ ? st_cap(obj) : 0u; }
int plat_store_stage_begin(uint32_t obj, uint32_t len, uint32_t crc) { return flash_ok ? st_stage_begin(obj, len, crc) : -9; }
int plat_store_stage_write(uint32_t off, const void *src, uint32_t n) { return st_stage_write(off, src, n); }
int plat_store_stage_read(uint32_t off, void *dst, uint32_t n) { return st_stage_read(off, dst, n); }
int plat_store_stage_commit(void) { return plat_store_done(st_stage_commit()); }
void plat_store_stage_abort(void) { st_stage_abort(); }
int plat_store_clear(uint32_t obj) { return flash_ok ? plat_store_done(st_clear(obj)) : -9; }
uint32_t plat_crc32(uint32_t crc, const void *p, uint32_t n) { return st_crc_add(crc, p, n); }
