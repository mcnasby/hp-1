/* SPDX-License-Identifier: GPL-3.0-only */
/* HP-1 platform interface (FoMni's, from X0X's): everything the app needs from the hardware.
 *
 * The device implementation (plat_fm1.c) wraps Felucca's HAL and panel map; the
 * host implementation (host/hp1_host.c) is a simulator fed by a script, which is
 * what lets the whole app — UI, sequencer, engines — run and be tested on a Mac.
 * Drawing goes through Felucca's lcd_fill / lcd_blit (lcd.c on the device, a
 * framebuffer on the host) and gfx.c on top. */
#pragma once
#include <stdint.h>

/* big zero-initialised buffers go in the .pool RAM region on the device */
#ifdef OM_HOST
#define OM_POOL
#else
#define OM_POOL __attribute__((section(".pool")))
#endif

/* panel controls, by printed label (Felucca's panel.c map turns these into matrix ids; Felucca
 * calls the second button SCL, the panel prints SEL) */
enum { B_FX, B_SEL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC,
       B_OCTDN, B_OCTUP, NB };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };

#define NKEYS 27                       /* F3 .. G5 */

uint32_t plat_ms(void);
uint32_t plat_buttons(void);           /* held: bit B_* */
uint32_t plat_keys(void);              /* held: bit k = key k (0 = F3) */
int32_t plat_enc(int role);            /* detents since the last call, + = clockwise; EN_* */
uint32_t plat_master(void);            /* MASTER pot, 0..4096 gain Q12 */
void plat_leds(uint32_t buttons, uint32_t keys, uint32_t dim_keys);   /* lit: bit B_*, bit key; dim: bit key */

/* USB MIDI, 4-byte USB-MIDI event packets (cable 0) */
int plat_midi_in(uint32_t *pkt);       /* 1 = got one (called from the audio ISR) */
void plat_midi_out(uint32_t pkt);      /* queue one (audio ISR or main loop) */

/* persistent storage (Felucca storage.c objects: A/B copies, CRC, torn-write safe) */
#define PLAT_NSLOT 4                   /* the project slots, A..D */
enum { OBJ_PROJ, OBJ_PANEL, OBJ_SEQ, OBJ_SLOT0, OBJ_NOBJ = OBJ_SLOT0 + PLAT_NSLOT };
/* flash (storage.c), a copy each: OBJ_PROJ 0xFC000 and OBJ_PANEL 0xDC000 (one sector), OBJ_SEQ 0xC0000
 * (four), the slots 0x98000 + 0xA000 x slot (five), all inside Felucca's data area */
#define PLAT_STORE_MAX (5u * 4096u - 256u)   /* the biggest object's payload (a slot's) */
int plat_store_load(uint32_t obj, void *dst, uint32_t max);   /* bytes loaded, < 0 = none / bad */
int plat_store_save(uint32_t obj, const void *src, uint32_t len);   /* 0 = ok */
typedef struct { const void *p; uint32_t n; } plat_part_t;
int plat_store_savev(uint32_t obj, const plat_part_t *part, int n);   /* one payload from pieces: 0 = ok */
/* the content manager's (content.h): the current payload's size and CRC (0, < 0 none), n bytes of it from
 * off (bytes read, < 0 none), its room; and a staged write: begin (erases the spare copy), pieces at
 * offsets, read back, commit (the CRC checked, then published), abort */
int plat_store_info(uint32_t obj, uint32_t *len, uint32_t *crc);
int plat_store_read(uint32_t obj, uint32_t off, void *dst, uint32_t n);
uint32_t plat_store_cap(uint32_t obj);
int plat_store_stage_begin(uint32_t obj, uint32_t len, uint32_t crc);
int plat_store_stage_write(uint32_t off, const void *src, uint32_t n);
int plat_store_stage_read(uint32_t off, void *dst, uint32_t n);
int plat_store_stage_commit(void);
void plat_store_stage_abort(void);
uint32_t plat_crc32(uint32_t crc, const void *p, uint32_t n);   /* zlib's, continued from crc (0 to start) */

/* audio load in % of the render budget, and xruns since boot (device: from the ISR) */
uint32_t plat_cpu_pct(void);
uint32_t plat_xruns(void);
