/* SPDX-License-Identifier: GPL-3.0-only */
/* HP-1's saved content, for the player (the Projects page) and for a content manager (the web page): the
 * working project and sequence, and the four project slots A..D, each an object in flash (storage.c,
 * torn-write safe). content.c; the host simulator runs the same code over its simulated flash.
 *
 * Objects (ct_list):
 *   CT_PROJECT   the working project: the sound and the settings (project_t, project.c)
 *   CT_SEQUENCE  the working sequence: its patterns, its song, the project's name (seq_data_t, seq.h:
 *                a seq_head_t, then seq_head_t.nev events)
 *   CT_SLOT_A..D a whole project in a slot: a ct_slot_head_t, then its project_t (proj_len bytes), then
 *                its sequence (seq_len bytes, as CT_SEQUENCE)
 * Every payload is little-endian, as the device keeps it in RAM. A write is staged: begin (its size and
 * zlib CRC-32), its bytes at any offsets (each byte once), commit; only a commit whose bytes are whole,
 * match the CRC and hold a valid object of that kind replaces the old one, which stays in charge until
 * then (abort, a power cut, a failed check: nothing changes). A committed working project or sequence is
 * taken up at once (the transport stops first). */
#pragma once
#include <stdint.h>

enum { CT_PROJECT, CT_SEQUENCE, CT_SLOT_A, CT_SLOT_B, CT_SLOT_C, CT_SLOT_D, CT_N };
#define CT_NAME 16                       /* 15 characters and the 0 */
#define CT_SLOT_MAGIC 0x4C535048u        /* "HPSL" */
#define CT_SLOT_FORMAT 1u

typedef struct {
    uint8_t id;                          /* CT_* */
    uint8_t used;                        /* holds a valid object */
    char name[CT_NAME];                  /* "Project", "Sequence", or the slot's project's name ("" unnamed) */
    uint32_t size, crc;                  /* its payload's bytes and CRC-32 (0 when unused) */
    uint32_t cap;                        /* the most it can hold */
} ct_info_t;

typedef struct {                         /* a slot's payload starts with this (32 bytes) */
    uint32_t magic, format;
    char name[CT_NAME];
    uint16_t proj_len, rsv;
    uint32_t seq_len;
} ct_slot_head_t;

int ct_list(ct_info_t *out, int max);    /* fills up to max (CT_N), returns how many */
int ct_info(int id, ct_info_t *out);     /* 0, -1: no such object */
int ct_read(int id, uint32_t off, void *dst, uint32_t n);   /* bytes read (0 past the end), < 0 none */

int ct_write_begin(int id, uint32_t size, uint32_t crc);    /* 0: ready (the spare copy erased) */
int ct_write_data(uint32_t off, const void *src, uint32_t n);   /* 0: written and read back */
int ct_write_commit(void);               /* 0: published (and taken up); -3 not whole, not its CRC or not one of
                                          * its kind, -4 flash (< 0: refused, the old one kept) */
void ct_write_abort(void);

int ct_slot_save(int slot);              /* the working project and sequence into slot 0..3: 0 = saved */
int ct_slot_load(int slot);              /* slot 0..3 into the working project (Setup's values kept): 0 */
int ct_slot_rename(int slot, const char *name);   /* A-Z 0-9 - and spaces, up to 15: 0 */
int ct_slot_clear(int slot);             /* slot 0..3 erased (empty): 0 */
int ct_slot_used(int slot);
const char *ct_slot_name(int slot);      /* "" when unnamed or empty */
void ct_refresh(void);                   /* re-read the slots' names (boot) */
