/* SPDX-License-Identifier: GPL-3.0-only */
/* HP-1 in the browser (FoMni's emulator, from X0X's): the host simulator (host/hp1_host.c: the whole app, UI and instrument,
 * against a simulated FM-1) compiled to WebAssembly and run in an AudioWorklet. The worklet asks
 * for audio; producing it advances the device's clock one millisecond at a time, which runs the
 * engine every 256 samples (the I2S half buffer) and the UI every 16 ms, as on the FM-1. The page
 * sends input (and MIDI from Web MIDI), and reads the screen, the lights and the saved objects back
 * through these exports.
 *
 *   web/emu/build.sh   ->  build/emu/hp1.wasm (+ the page) */
#include <stdint.h>

static void web_audio(const int32_t *blk, uint32_t n);
#include "../../host/hp1_host.c"

#undef __attribute__

/* output ring, stereo float, filled by run_ms, drained by web_render */
#define RING 4096u
static float ring[RING * 2];
static uint32_t ring_w, ring_r;
static void web_audio(const int32_t *blk, uint32_t n)
{
    uint32_t k;
    for (k = 0; k < n; k++) {
        uint32_t i = (ring_w++ % RING) * 2u;
        ring[i] = (float)blk[2 * k] / 8388608.0f;
        ring[i + 1] = (float)blk[2 * k + 1] / 8388608.0f;
    }
}

static float out_l[1024], out_r[1024];
__attribute__((used, visibility("default"))) float *web_out_l(void) { return out_l; }
__attribute__((used, visibility("default"))) float *web_out_r(void) { return out_r; }

/* n frames (<= 1024) into out_l / out_r: the device runs until they exist */
__attribute__((used, visibility("default"))) void web_render(uint32_t n)
{
    uint32_t k;
    if (n > 1024u)
        n = 1024u;
    while (ring_w - ring_r < n)
        run_ms(1);
    for (k = 0; k < n; k++) {
        uint32_t i = (ring_r++ % RING) * 2u;
        out_l[k] = ring[i];
        out_r[k] = ring[i + 1];
    }
}

__attribute__((used, visibility("default"))) void web_boot(void) { boot(); }
__attribute__((used, visibility("default"))) void web_buttons(uint32_t m) { held_btn = m; }
__attribute__((used, visibility("default"))) void web_keys(uint32_t m) { held_keys = m; }
__attribute__((used, visibility("default"))) void web_enc(int role, int32_t n)
{
    if (role >= 0 && role < NE)
        enc_acc[role] += n;
}
/* one MIDI message from the page (Web MIDI), as a USB-MIDI packet into the same queue the device's USB
 * and TRS jack fill */
__attribute__((used, visibility("default"))) void web_midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    uint32_t cin = st >= 0xF0u ? 0x0Fu : st >> 4;
    if (mi_w - mi_r < MQ)
        min_q[mi_w++ % MQ] = cin | (st & 0xFFu) << 8 | (d1 & 0x7Fu) << 16 | (d2 & 0x7Fu) << 24;
}
__attribute__((used, visibility("default"))) void web_master(uint32_t v) { master = v > 4096u ? 4096u : v; }
__attribute__((used, visibility("default"))) uint32_t web_lit_buttons(void) { return lit_btn; }
__attribute__((used, visibility("default"))) uint32_t web_lit_keys(void) { return lit_keys; }
__attribute__((used, visibility("default"))) uint32_t web_dim_keys(void) { return dim_keys; }
__attribute__((used, visibility("default"))) uint16_t *web_fb(void) { return fb; }
__attribute__((used, visibility("default"))) uint32_t web_blits(void) { return blits; }
__attribute__((used, visibility("default"))) uint32_t web_now(void) { return now_ms; }
__attribute__((used, visibility("default"))) uint32_t web_cpu(void) { return cpu_pct; }

/* saved objects: the page keeps them (localStorage) and gives them back before boot. The simulated flash
 * runs the device's own storage (host/hp1_host.c); an object passes through this buffer: web_store_len
 * reads the object into it (its length, -1 none), web_store_set_len saves what the page put there. */
static uint8_t web_obj[PLAT_STORE_MAX];
__attribute__((used, visibility("default"))) uint8_t *web_store(uint32_t obj) { return obj < OBJ_NOBJ ? web_obj : 0; }
__attribute__((used, visibility("default"))) int web_store_len(uint32_t obj)
{
    return obj < OBJ_NOBJ ? plat_store_load(obj, web_obj, sizeof web_obj) : -1;
}
__attribute__((used, visibility("default"))) void web_store_set_len(uint32_t obj, int n)
{
    if (obj < OBJ_NOBJ && n >= 0)
        plat_store_save(obj, web_obj, n > (int)PLAT_STORE_MAX ? PLAT_STORE_MAX : (uint32_t)n);
}
__attribute__((used, visibility("default"))) uint32_t web_store_writes(void) { return store_writes; }
__attribute__((used, visibility("default"))) uint32_t web_nobj(void) { return OBJ_NOBJ; }
__attribute__((used, visibility("default"))) uint32_t web_store_max(void) { return PLAT_STORE_MAX; }

/* the content manager's calls (firmware/src/app/content.h), for a page: bytes pass through web_ct_buf */
static uint8_t ct_buf[4096];
static ct_info_t ct_one;
__attribute__((used, visibility("default"))) uint8_t *web_ct_buf(void) { return ct_buf; }
__attribute__((used, visibility("default"))) uint32_t web_ct_count(void) { return CT_N; }
__attribute__((used, visibility("default"))) ct_info_t *web_ct_info(int id) { return ct_info(id, &ct_one) == 0 ? &ct_one : 0; }
__attribute__((used, visibility("default"))) int web_ct_read(int id, uint32_t off, uint32_t n)
{
    return ct_read(id, off, ct_buf, n > sizeof ct_buf ? (uint32_t)sizeof ct_buf : n);
}
__attribute__((used, visibility("default"))) int web_ct_begin(int id, uint32_t size, uint32_t crc) { return ct_write_begin(id, size, crc); }
__attribute__((used, visibility("default"))) int web_ct_write(uint32_t off, uint32_t n)
{
    return ct_write_data(off, ct_buf, n > sizeof ct_buf ? (uint32_t)sizeof ct_buf : n);
}
__attribute__((used, visibility("default"))) int web_ct_commit(void) { return ct_write_commit(); }
__attribute__((used, visibility("default"))) void web_ct_abort(void) { ct_write_abort(); }
__attribute__((used, visibility("default"))) int web_ct_slot_save(int slot) { return ct_slot_save(slot); }
__attribute__((used, visibility("default"))) int web_ct_slot_load(int slot) { return ct_slot_load(slot); }
__attribute__((used, visibility("default"))) int web_ct_slot_rename(int slot)   /* the name: a C string in web_ct_buf */
{
    ct_buf[CT_NAME - 1] = 0;
    return ct_slot_rename(slot, (const char *)ct_buf);
}

/* how the pan spells its accidentals (km_sharps: bit p, pitch class p a sharp), for the page's labels */
__attribute__((used, visibility("default"))) uint32_t web_sharps(void) { return km_sharps(proj.par[P_SCALE], proj.par[P_KEY]); }

/* what each key plays now, for the page's labels: a note's key its note (cents), a key of the steel
 * or the kit -(1 + its HIT_*) */
__attribute__((used, visibility("default"))) int32_t web_key_label(uint32_t k)
{
    int i;
    for (i = 0; i < NWHITE; i++)
        if (WHITE_K[i] == k)
            return km_white_hit(i) >= 0 ? -(1 + km_white_hit(i)) : white_cents(i);
    for (i = 0; i < NBLACK; i++)
        if (BLACK_K[i] == k)
            return is_duo_key(i) ? duo_cents(duo_index(i)) : -(1 + (int32_t)black_hit(i));
    return 0;
}
