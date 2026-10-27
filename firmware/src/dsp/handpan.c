/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument (handpan.h). One compilation unit, -O2. The plate reverb is FoMni's (Dattorro's
 * figure-eight), the command queue and the float-only maths follow FoMni / X0X, the modal voice, the
 * effects and the grain cloud are FiMba-1's. */
#include "handpan.h"
#include "fastmath.h"

#ifdef OM_HOST
#define KM_POOL
#else
#define KM_POOL __attribute__((section(".pool")))
#endif
#define BARRIER() __asm__ volatile("" ::: "memory")

const char *const KM_NOTE_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

static const km_param_t PARAMS[P_NPARAMS] = {
    {"Metal", 0, MET_N - 1, MET_NITRIDED}, {"Strike", 0, 100, 40}, {"Sustain", 0, 100, 55}, {"Harmonics", 0, 100, 60},
    {"Size", 44, 62, 53}, {"Thickness", 10, 14, 12}, {"Sympathy", 0, 100, 50}, {"Port", 0, 100, 50},
    {"Reverb", 0, 100, 38}, {"Room", 0, 100, 68}, {"Delay", 0, 100, 0}, {"Time", 40, 740, 375},
    {"Tape", 0, 100, 0}, {"Lo-fi", 0, 100, 0}, {"Chorus", 0, 100, 0}, {"Filter", -100, 100, 0},
    {"Grains", 0, 100, 0}, {"Grain ms", 20, 500, 160}, {"Density", 1, 60, 12}, {"Pitch", 0, GP_N - 1, GP_UP12},
    {"Spray", 0, 100, 30}, {"Feedback", 0, 95, 40}, {"Width", 0, 100, 70},
    {"Layout", 0, LAY_N - 1, LAY_PAN}, {"Scale", 0, KM_NSCALE - 1, 0}, {"Key", 0, 11, 2}, {"Black", 0, BLK_N - 1, BLK_KIT},
    {"Duo", 0, KM_NDUOCHOICE - 1, 0}, {"Duo level", 0, 100, 80}, {"Percussion", 0, 100, 70}, {"Octave", -1, 1, 0},
    {"Worn", 0, 100, 0}, {"Humanize", 0, 100, 35}, {"Tuning", 0, TUNE_N - 1, TUNE_EQUAL}, {"Release", 0, 1, 0},
    {"Groove", 0, PAT_N - 1, PAT_HEART}, {"Tempo", 40, 200, 84},
    {"Tune", -50, 50, 0}, {"MIDI ch", 0, 16, 0}, {"MIDI out", 0, 1, 0}, {"Theme", 0, THEME_N - 1, THEME_DARK},
};
const km_param_t *km_param_info(int i) { return (i >= 0 && i < P_NPARAMS) ? &PARAMS[i] : &PARAMS[0]; }

/* ------------------------------------------------------------- metals --- */
/* What the shell is made of and how it was finished. Nitrided: carbon steel case-hardened in nitrogen
 * (the classic dark pan): warm, the octave strong, a lively shimmer. Stainless: brighter, the longest
 * ring, a slower, wider shimmer. Ember: a softer, nitrided alloy: mellow, the fifth tucked in, a gentle
 * pulse. Raw: cold-rolled DC04 left plain: earthy, drier, the shortest ring, a quicker pulse. Levels and
 * decays after the measured pans (handpan.h); the D3 ding rings ~4-6 s, the high fields 2-3 s. */
static const km_metal_t METALS[MET_N] = {
    {"Nitrided", 3.8f, 0.45f, 0.82f, 0.30f, 0.10f, 1.25f, 0.85f, 4.5f, 0.55f, 1.0f, 0.85f, 0.55f, 0.55f},
    {"Stainless", 5.0f, 0.40f, 0.90f, 0.40f, 0.14f, 1.30f, 0.95f, 4.0f, 0.50f, 1.2f, 1.10f, 0.95f, 0.95f},
    {"Ember", 4.3f, 0.45f, 0.72f, 0.20f, 0.06f, 1.20f, 0.80f, 3.6f, 0.42f, 0.8f, 0.55f, 0.38f, 0.45f},
    {"Raw", 2.8f, 0.50f, 0.76f, 0.32f, 0.10f, 1.10f, 0.75f, 6.0f, 0.60f, 1.3f, 0.95f, 0.65f, 0.35f},
};
const km_metal_t *km_metal(int i) { return &METALS[(unsigned)i < MET_N ? i : 0]; }

/* -------------------------------------------------------------- scales --- */
/* Handpan scales as the makers build them: the ding, then the tone fields in semitones above it, low to
 * high. A pan in another key is the same steps from another ding. Root: the key it is most often made in. */
typedef struct {
    const char *name;
    uint8_t root, n;
    uint8_t s[10];
} scale_t;
static const scale_t SCALES[KM_NSCALE] = {
    {"Kurd", 2, 8, {7, 8, 10, 12, 14, 15, 17, 19}},        /* D3 / A Bb C D E F G A: the best known */
    {"Celtic", 2, 8, {7, 10, 12, 14, 15, 17, 19, 22}},     /* Celtic minor (Amara): D / A C D E F G A C */
    {"Integral", 2, 7, {7, 8, 10, 12, 14, 15, 19}},        /* D / A Bb C D E F A */
    {"Pygmy", 6, 8, {2, 3, 7, 10, 12, 14, 15, 19}},        /* F# / G# A C# E F# G# A C# */
    {"Hijaz", 2, 8, {7, 8, 11, 12, 14, 15, 17, 19}},       /* D / A Bb C# D E F G A */
    {"Aegean", 0, 8, {4, 7, 11, 12, 16, 18, 19, 23}},      /* C / E G B C E F# G B */
    {"Equinox", 5, 8, {3, 7, 8, 10, 12, 14, 15, 19}},      /* F / Ab C Db Eb F G Ab C */
    {"Akebono", 2, 8, {7, 8, 12, 14, 15, 19, 20, 24}},     /* D / A Bb D E F A Bb D */
    {"Major", 0, 8, {7, 9, 11, 12, 14, 16, 17, 19}},       /* C / G A B C D E F G (Sabye-like) */
    {"Mixolyd.", 7, 8, {7, 9, 10, 12, 14, 16, 17, 19}},    /* G / D E F G A B C D */
};

static const scale_t *scale_of(int s) { return &SCALES[(unsigned)s < KM_NSCALE ? s : 0]; }
const char *km_scale_name(int s) { return scale_of(s)->name; }
int km_scale_root(int s) { return scale_of(s)->root; }
int km_scale_tops(int s) { return scale_of(s)->n; }

/* the ding sits between A2 and G#3, where the makers put it (a D pan's ding is D3, an A pan's A2) */
int km_ding_cents(int key, int octave)
{
    key = ((key % 12) + 12) % 12;
    return 100 * (key >= 9 ? 36 + key : 48 + key) + 1200 * octave;
}

static uint16_t scale_pcs(int s)                   /* the pitch classes over the ding (bit 0: the ding) */
{
    const scale_t *sc = scale_of(s);
    uint16_t m = 1u;
    int i;
    for (i = 0; i < sc->n; i++)
        m |= (uint16_t)(1u << (sc->s[i] % 12));
    return m;
}

int km_pan_notes(int s, int key, int octave, int out[KM_NPAN])
{
    const scale_t *sc = scale_of(s);
    const uint16_t pcs = scale_pcs(s);
    int d = km_ding_cents(key, octave), n = 0, i, st;
    out[n++] = d;
    for (i = 0; i < sc->n; i++)
        out[n++] = d + 100 * sc->s[i];
    for (st = sc->s[sc->n - 1] + 1; n < KM_NPAN; st++)   /* beyond its fields: the scale goes on up */
        if (pcs >> (st % 12) & 1u)
            out[n++] = d + 100 * st;
    return sc->n + 1;
}

/* Pan: the ding on both middle keys (either thumb finds it), then the fields climbing outward in turn,
 * left, right, left, right, as they zigzag up around a real pan:
 *   w:      0   1   2   3   4   5   6   7   8   9  10  11  12  13  14  15
 *   note   13  11   9   7   5   3   1   0   0   2   4   6   8  10  12  14
 * Mirror: the same right for left. Linear: the ding on the left, then up the keys. */
int km_white_index(int layout, int w)
{
    w &= 15;
    if (layout == LAY_LINEAR)
        return w;
    if (layout == LAY_MIRROR)
        w = 15 - w;
    if (w == 7 || w == 8)
        return 0;
    return w < 7 ? 2 * (7 - w) - 1 : 2 * (w - 8);
}

int km_white_cents(int layout, int s, int key, int octave, int w)
{
    int n[KM_NPAN];
    km_pan_notes(s, key, octave, n);
    return n[km_white_index(layout, w)];
}

float km_white_pos(int layout, int w)
{
    if (layout == LAY_LINEAR)
        return ((float)(w & 15) - 7.5f) * (1.0f / 7.5f);
    {   /* as it sits on the pan: the ding in the middle, the fields to their side, the high ones wider */
        int i = km_white_index(layout, w), side = (w & 15) < 8 ? -1 : 1;
        return i ? (float)side * (0.25f + 0.05f * (float)(i < 15 ? i : 15)) : 0.0f;
    }
}

int km_just_cents(int cents, int tonic_cents)
{
    static const int16_t JUST[12] = {0, 112, 204, 316, 386, 498, 590, 702, 814, 884, 1018, 1088};
    int d = cents - tonic_cents, pc = (d % 1200 + 1200) % 1200;
    if (pc % 100)
        return cents;
    return cents - pc + JUST[pc / 100];
}

/* ------------------------------------------------------------ the duo --- */
/* A second pan that plays along: a scale and key whose every note is already in the first pan (or, for
 * a pan of few notes, all but one, and that one not a semitone from its ding). Ranked by how its ding
 * sits against the first's: the same, a fifth, a fourth, the relative major or minor, then the rest;
 * then by how many notes they share. The same pan in the same key isn't offered. */
static int duo_list(int s, int key, uint8_t *out_s, uint8_t *out_k)
{
    static const uint8_t RANK[12] = {0, 9, 6, 4, 5, 2, 9, 1, 5, 3, 8, 9};   /* by the dings' interval */
    const uint16_t main_pcs = scale_pcs(s);
    int score[KM_NSCALE * 12], n = 0, cs, ck, i, j;
    uint8_t ls[KM_NSCALE * 12], lk[KM_NSCALE * 12];
    key = ((key % 12) + 12) % 12;
    for (cs = 0; cs < KM_NSCALE; cs++)
        for (ck = 0; ck < 12; ck++) {
            const int iv = (ck - key + 12) % 12;
            const uint16_t p = scale_pcs(cs), rot = (uint16_t)(((p << iv) | (p >> (12 - iv))) & 0xFFFu);
            int out = 0, shared = 0, b;
            if (cs == s && ck == key)
                continue;
            for (b = 0; b < 12; b++)
                if (rot >> b & 1u) {
                    if (main_pcs >> b & 1u)
                        shared++;
                    else
                        out++;
                }
            if (out > 1 || (out == 1 && ((rot >> 1 & 1u && !(main_pcs >> 1 & 1u)) ||
                                         (rot >> 11 & 1u && !(main_pcs >> 11 & 1u)))))
                continue;                                /* (an outside note a semitone from the ding: a clash) */
            if (RANK[iv] >= 9)
                continue;
            ls[n] = (uint8_t)cs;
            lk[n] = (uint8_t)ck;
            score[n++] = out * 1000 + RANK[iv] * 100 - shared * 10 + (cs == s ? 0 : 1);
        }
    for (i = 1; i < n; i++)                              /* best first (insertion sort: stable) */
        for (j = i; j > 0 && score[j] < score[j - 1]; j--) {
            int t = score[j];
            uint8_t a = ls[j], b = lk[j];
            score[j] = score[j - 1];
            ls[j] = ls[j - 1];
            lk[j] = lk[j - 1];
            score[j - 1] = t;
            ls[j - 1] = a;
            lk[j - 1] = b;
        }
    if (!n) {                                            /* (none: the same pan a fifth up) */
        ls[0] = (uint8_t)s;
        lk[0] = (uint8_t)((key + 7) % 12);
        n = 1;
    }
    if (n > KM_NDUOCHOICE)
        n = KM_NDUOCHOICE;
    for (i = 0; i < n; i++) {
        if (out_s)
            out_s[i] = ls[i];
        if (out_k)
            out_k[i] = lk[i];
    }
    return n;
}

int km_duo_count(int s, int key) { return duo_list(s, key, 0, 0); }

void km_duo_choice(int s, int key, int choice, int *ds, int *dk)
{
    uint8_t ls[KM_NDUOCHOICE], lk[KM_NDUOCHOICE];
    int n = duo_list(s, key, ls, lk);
    choice = choice < 0 ? 0 : choice >= n ? n - 1 : choice;
    *ds = ls[choice];
    *dk = lk[choice];
}

/* the second pan stands a register above the first: its ding 3 to 14 semitones over the first's */
int km_duo_notes(int s, int key, int octave, int choice, int out[KM_NDUO])
{
    int ds, dk, n[KM_NPAN], d0 = km_ding_cents(key, octave), i;
    km_duo_choice(s, key, choice, &ds, &dk);
    km_pan_notes(ds, dk, octave, n);
    while (n[0] < d0 + 300)
        for (i = 0; i < KM_NPAN; i++)
            n[i] += 1200;
    while (n[0] > d0 + 1400)
        for (i = 0; i < KM_NPAN; i++)
            n[i] -= 1200;
    for (i = 0; i < KM_NDUO; i++)
        out[i] = n[i];
    return KM_NDUO;
}

int km_duo_index(int layout, int i)
{
    i = i < 0 ? 0 : i >= KM_NDUO ? KM_NDUO - 1 : i;
    if (layout == LAY_LINEAR)
        return i;
    if (layout == LAY_MIRROR)
        i = KM_NDUO - 1 - i;
    return i == 3 ? 0 : i < 3 ? 2 * (3 - i) - 1 : 2 * (i - 3);
}

const char *km_duo_name(int s, int key, int choice, char *b)
{
    int ds, dk, i = 0;
    const char *p;
    km_duo_choice(s, key, choice, &ds, &dk);
    for (p = KM_NOTE_NAME[dk]; *p; p++)
        b[i++] = *p;
    b[i++] = ' ';
    for (p = SCALES[ds].name; *p && i < 15; p++)
        b[i++] = *p;
    b[i] = 0;
    return b;
}

/* ------------------------------------------------------- param text --- */
static void itoa_s(int v, char *b)
{
    char t[8];
    int n = 0, neg = v < 0;
    if (neg)
        v = -v;
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 6);
    if (neg)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static void copy_s(char *b, const char *s)
{
    int k = 0;
    while (s[k] && k < 11) {
        b[k] = s[k];
        k++;
    }
    b[k] = 0;
}

void km_param_text(int i, int v, char *b)
{
    static const char *const ONOFF[2] = {"Off", "On"};
    static const char *const LAY[LAY_N] = {"Pan", "Mirror", "Linear"};
    static const char *const BLK[BLK_N] = {"Kit", "Duo"};
    static const char *const GP[GP_N] = {"-12", "-7", "0", "+7", "+12", "+19", "Shimmer", "Reverse"};
    static const char *const REL[2] = {"Ring", "Damp"};
    static const char *const PAT[PAT_N] = {"Heartbeat", "Cascade", "3 over 2", "Backbeat"};
    static const char *const TUN[TUNE_N] = {"Equal", "Just"};
    static const char *const THM[THEME_N] = {"Dark", "Light"};
    switch (i) {
    case P_METAL: copy_s(b, km_metal(v)->name); return;
    case P_LAYOUT: copy_s(b, LAY[(unsigned)v < LAY_N ? v : 0]); return;
    case P_BLACK: copy_s(b, BLK[(unsigned)v < BLK_N ? v : 0]); return;
    case P_GPITCH: copy_s(b, GP[(unsigned)v < GP_N ? v : 0]); return;
    case P_SCALE: copy_s(b, km_scale_name(v)); return;
    case P_KEY: copy_s(b, KM_NOTE_NAME[(unsigned)v % 12u]); return;
    case P_RELEASE: copy_s(b, REL[v ? 1 : 0]); return;
    case P_PATTERN: copy_s(b, PAT[(unsigned)v < PAT_N ? v : 0]); return;
    case P_TUNING: copy_s(b, TUN[v ? 1 : 0]); return;
    case P_THEME: copy_s(b, THM[v ? 1 : 0]); return;
    case P_MIDIOUT: copy_s(b, ONOFF[v ? 1 : 0]); return;
    case P_DUO:                                    /* (the UI shows the pan's name; this is its rank) */
        if (!v) {
            copy_s(b, "Auto");
            return;
        }
        *b++ = '#';
        v++;
        break;
    case P_THICK:                                  /* tenths of a millimeter: "1.2mm" */
        b[0] = (char)('0' + v / 10);
        b[1] = '.';
        b[2] = (char)('0' + v % 10);
        copy_s(b + 3, "mm");
        return;
    case P_SIZE:
        itoa_s(v, b);
        while (*b)
            b++;
        copy_s(b, "cm");
        return;
    case P_FILTER:                                 /* one knob: left a low-pass, right a high-pass */
        if (!v) {
            copy_s(b, "Off");
            return;
        }
        *b++ = v < 0 ? 'L' : 'H';
        *b++ = 'P';
        *b++ = ' ';
        v = v < 0 ? -v : v;
        break;
    case P_MIDICH:
        if (!v) {
            copy_s(b, "Omni");
            return;
        }
        break;
    case P_TUNE: case P_OCTAVE:
        if (v > 0)
            *b++ = '+';
        break;
    default: break;
    }
    itoa_s(v, b);
}

/* ------------------------------------------------------------ commands --- */
enum { C_SET, C_PLUCK, C_HIT, C_DAMP, C_DAMPALL, C_SUSTAIN, C_BEND, C_FREEZE, C_PANIC, C_PATTERN, C_PANCLR, C_PANADD,
       C_CLOCK };
typedef struct {
    uint8_t c;
    int8_t pan;
    int16_t a, b, d;
} cmd_t;
#define QN 256u                                   /* a MIDI burst, a flam of hits: room for all of it */
static cmd_t q[QN];
static volatile uint32_t q_w, q_r;
volatile uint32_t km_dropped;
static void post(int c, int a, int b, int pan, int d)
{
    uint32_t w = q_w;
    cmd_t *e;
    if (w - q_r >= QN) {
        km_dropped++;
        return;
    }
    e = &q[w % QN];
    e->c = (uint8_t)c;
    e->a = (int16_t)a;
    e->b = (int16_t)b;
    e->pan = (int8_t)pan;
    e->d = (int16_t)d;
    BARRIER();
    q_w = w + 1;
}
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
void km_set(int p, int v) { post(C_SET, p, v, 0, 0); }
void km_pluck(int cents, int vel, int pan, int delay_ms)
{
    post(C_PLUCK, clampi(cents, 0, 12700), clampi(vel, 1, 127), clampi(pan, -100, 100), clampi(delay_ms, 0, 2000));
}
void km_hit(int kind, int vel, int delay_ms) { post(C_HIT, clampi(kind, 0, HIT_N - 1), clampi(vel, 1, 127), 0, clampi(delay_ms, 0, 2000)); }
void km_damp(int cents) { post(C_DAMP, cents, 0, 0, 0); }
void km_damp_all(void) { post(C_DAMPALL, 0, 0, 0, 0); }
void km_sustain(int on) { post(C_SUSTAIN, on, 0, 0, 0); }
void km_bend(int v) { post(C_BEND, clampi(v, -8192, 8191), 0, 0, 0); }
void km_freeze(int on) { post(C_FREEZE, on, 0, 0, 0); }
void km_panic(void) { post(C_PANIC, 0, 0, 0, 0); }
void km_pattern(int on) { post(C_PATTERN, on, 0, 0, 0); }
void km_pan(const int *cents, int n)
{
    int i;
    post(C_PANCLR, 0, 0, 0, 0);
    for (i = 0; i < n && i < KM_NPAN + KM_NDUO; i++)
        post(C_PANADD, clampi(cents[i], 0, 12700), 0, 0, 0);
}
void km_clock(int msg) { post(C_CLOCK, msg, 0, 0, 0); }

/* -------------------------------------------------------------- state --- */
volatile float km_voice_level[KM_NVOICE];
volatile int16_t km_voice_cents[KM_NVOICE];
volatile uint8_t km_frozen, km_grains_on;
volatile float km_shell_level[2], km_cavity_level, km_out_level;

static int16_t par[P_NPARAMS];
static float tunefac = 1.0f, bendfac = 1.0f, width = 0.7f;
static uint32_t rng = 0x12345678u;
static inline uint32_t rnd(void)
{
    uint32_t x = rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return rng = x;
}
static inline float rndf(void) { return (float)(rnd() >> 8) * (1.0f / 16777216.0f); }   /* 0..1 */
static inline float rnds(void) { return rndf() * 2.0f - 1.0f; }                          /* -1..1 */

/* the pan's notes (the ding first), as the UI last gave them: sympathy and the grooves use them */
static int pan_n, pan_c[KM_NPAN + KM_NDUO];
static int air_dirty = 1;                               /* the air's tuning follows the pan (below) */
static float air_mute;                                  /* > 0: a palm on the pan (samples left): the halo dies too */
static void air_quiet_all(void);

/* --------------------------------------------------------------- fields --- */
typedef struct {
    float b1[KM_NMODE], b2[KM_NMODE], g[KM_NMODE];    /* y = b1 y1 + b2 y2 + g x */
    float y1[KM_NMODE], y2[KM_NMODE];
    float pl, pr, level, damp;                        /* damp: < 1 while a hand is on it (folded into b1, b2) */
    float ex_amp, ex_ph, ex_inc;                      /* the strike: a half-sine pulse, ex_ph 0..0.5 turns */
    float click, click_dec, click_a, click_lp, click_hp;   /* the fingertip's skin on the steel: a burst,
                                                       * band-passed (low-pass click_a, high-pass ~300 Hz) */
    int16_t cents;
    float fade, fade_step;                            /* a stolen voice, fading out (fade slots only) */
    float w0[KM_NMODE], rr[KM_NMODE], amp[KM_NMODE];  /* each mode at rest: frequency (rad), radius, level */
    float c0[KM_NMODE], s0[KM_NMODE];                 /* cos and sin of w0: the glide moves from them */
    float cw[KM_NMODE], sw[KM_NMODE];                 /* cos and sin of the frequency now (the silence test) */
    float emph[KM_NMODE];                             /* how much of each mode this strike wakes (a harmonic,
                                                       * a sympathetic ring: some modes more than others) */
    float glide, glide_pend, glide_rise;              /* cents sharp now (a firm strike bends the field); to
                                                       * come, and how much more each chunk */
    uint8_t on, nm, quiet, released, contact, rise_n;
    uint16_t live;                                    /* the modes still sounding (bit k): the rest are skipped */
} voice_t;
/* Two spare slots where a stolen voice fades out over a few milliseconds while its slot takes the new
 * field: cutting it dead was a pop (as loud as the field was) */
#define KM_NFADE 2
#define FADE_S (0.006f * KM_SR)
static voice_t vc[KM_NVOICE + KM_NFADE];
/* a firm strike pushes the field wide and it sounds a little sharp until it settles (~45 ms) */
#define GLIDE_MAX 5.0f
#define GLIDE_K 0.96826f                               /* per chunk of 64: exp(-64 / (0.045 s * SR)) */
/* Re-striking a ringing field: the fingertip lands on it, damping it to ~0.55 over CONTACT_N chunks, then
 * lifts (the new strike). Done through the resonators (their decay), never a jump in the waveform */
#define CONTACT_N 3u                                  /* chunks of BLK (64): ~4.4 ms */
#define CONTACT_D 0.99689f                            /* per sample: 0.55 over 192 samples */
#define FINGER_D 0.99978f                             /* a hand left on it (Release Damp, palm mute) */
#define SLAP_D 0.99870f                               /* the ding slap: the palm stays, ~0.12 s */

/* the modes: the fundamental; the octave and its twin; the compound fifth and its twin; the double
 * octave (the tuner's 1 : 2 : 3 : 4); then the ping: four of the field's higher modes, untuned and
 * inharmonic (the dimple's and the field's edge's), which only a strike reaches and which die in a
 * moment: the bright, metallic "tang" at the front of every note */
enum { M_FUND, M_OCT, M_OCT2, M_FIFTH, M_FIFTH2, M_DBL, M_PING };
static const float PING_RATIO[4] = {5.21f, 6.83f, 8.62f, 11.27f}, PING_AMP[4] = {1.0f, 0.75f, 0.55f, 0.38f},
                   PING_T60[4] = {0.22f, 0.16f, 0.12f, 0.09f};

static float cents_hz(int c) { return 8.17579892f * fm_exp2f((float)c * (1.0f / 1200.0f)) * tunefac * bendfac; }

/* Worn: an old, played-in pan. Each field has its own quirks, the same each time it's struck (they come
 * from its pitch): a little out of tune (up to +-10 cents), its octave a little off true, a sustain
 * longer or shorter than its neighbors', a faster or slower beat, and a little louder or softer. */
static float worn;                                    /* 0..1 */
static float wear(int cents, int i)                   /* the field's quirk i, -1..1 */
{
    uint32_t h = (uint32_t)cents * 2654435761u ^ (uint32_t)i * 0x9E3779B9u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return (float)(h & 0xFFFFu) * (2.0f / 65535.0f) - 1.0f;
}

/* the resonators' settings, from their frequency (with the glide) and radius (FiMba-1's) */
static void voice_freq(voice_t *v)
{
    const float e = v->glide * 0.000577791f;           /* cents -> the fraction the frequency rises */
    int k;
    for (k = 0; k < KM_NMODE; k++) {
        float w = v->w0[k] * (1.0f + e), r = v->rr[k], c, sn;
        if (k >= v->nm || v->amp[k] <= 0.0f || w > 0.45f * FM_TWO_PI) {
            v->b1[k] = v->b2[k] = v->g[k] = 0.0f;
            continue;
        }
        if (e == 0.0f) {
            c = v->c0[k];
            sn = v->s0[k];
        } else {   /* cos and sin of w0 + d from those of w0: the glide is small, to d^3 */
            const float d = v->w0[k] * e, d2 = 0.5f * d * d, d3 = d * d2 * (1.0f / 3.0f);
            c = v->c0[k] * (1.0f - d2) - v->s0[k] * (d - d3);
            sn = v->s0[k] * (1.0f - d2) + v->c0[k] * (d - d3);
        }
        v->b1[k] = 2.0f * r * c;
        v->b2[k] = -r * r;
        v->g[k] = v->amp[k] * v->emph[k] * sn;        /* an impulse of area 1 rings at amplitude amp */
        v->cw[k] = c;
        v->sw[k] = sn;
    }
}

static float thick_f(void) { return (float)par[P_THICK] * (1.0f / 12.0f); }
static float size_f(void) { return 53.0f / (float)par[P_SIZE]; }   /* > 1 smaller, < 1 bigger */

static void voice_coefs(voice_t *v)
{
    const km_metal_t *m = km_metal(par[P_METAL]);
    const float th = thick_f(), harm = (float)par[P_HARMONICS] * (1.0f / 60.0f);
    float f0 = cents_hz(v->cents),
          /* Sustain: a quarter to four times the metal's own; a thicker shell rings on longer, a bigger
           * pan's fields are bigger and ring a little longer too */
          dscale = fm_exp2f((float)(par[P_SUSTAIN] - 50) * 0.04f) * fm_powf(th, 0.8f) * fm_powf(size_f(), -0.5f), t1,
          lvl = 1.0f, spread = 1.0f, ostretch = 0.0f, ratio[KM_NMODE], amp[KM_NMODE], tk[KM_NMODE], bo, bf;
    int k, nm = 0;
    if (worn > 0.0f) {                                  /* this field's quirks */
        f0 *= fm_exp2f(worn * 10.0f * wear(v->cents, 0) * (1.0f / 1200.0f));
        dscale *= 1.0f + 0.35f * worn * wear(v->cents, 1);
        spread = 1.0f + 0.8f * worn;                    /* its neighbors further off: a wider beat */
        ostretch = 0.006f * worn * wear(v->cents, 3);  /* the octave off true by up to ~10 cents */
        lvl = 1.0f + 0.2f * worn * wear(v->cents, 4);
    }
    /* each field's own shimmer, the same every time it's struck (from its pitch): the octave's and the
     * fifth's beats, 0.7 to 1.6 times the metal's, in Hz (as measured: a beat in Hz, not a ratio) */
    bo = m->beat * spread * (1.0f + 0.3f * wear(v->cents, 6));
    bf = m->beat * spread * (1.25f + 0.35f * wear(v->cents, 7));
    t1 = m->t60 * dscale * fm_powf(f0 * (1.0f / 146.83f), -m->pitch_k);
    ratio[M_FUND] = 1.0f;
    amp[M_FUND] = 1.0f;
    tk[M_FUND] = t1;
    ratio[M_OCT] = 2.0f * (1.0f + ostretch);
    amp[M_OCT] = m->oct * harm;
    tk[M_OCT] = t1 * m->oct_t;                          /* the octave often outlasts the fundamental */
    ratio[M_OCT2] = ratio[M_OCT] + bo / f0;
    amp[M_OCT2] = amp[M_OCT] * m->depth;
    tk[M_OCT2] = tk[M_OCT];
    ratio[M_FIFTH] = 3.0f * (1.0f + 0.5f * ostretch);
    amp[M_FIFTH] = m->fifth * harm;
    tk[M_FIFTH] = t1 * m->fifth_t;
    ratio[M_FIFTH2] = ratio[M_FIFTH] - bf / f0;
    amp[M_FIFTH2] = amp[M_FIFTH] * m->depth;
    tk[M_FIFTH2] = tk[M_FIFTH];
    ratio[M_DBL] = 4.0f * (1.0f + ostretch);
    amp[M_DBL] = m->dbl * harm * th;
    tk[M_DBL] = t1 * 0.5f;
    for (k = 0; k < 4; k++) {                           /* (thicker steel: a higher, longer ping) */
        ratio[M_PING + k] = PING_RATIO[k] * fm_powf(th, 0.15f);
        amp[M_PING + k] = m->ping * PING_AMP[k] * th;
        tk[M_PING + k] = PING_T60[k] * th;
    }
    for (k = 0; k < KM_NMODE; k++) {
        float f = f0 * ratio[k], t60 = tk[k], r, a = amp[k] * lvl;
        v->amp[k] = 0.0f;
        if (f > 0.45f * KM_SR || a <= 0.0f)
            continue;
        if (t60 < 0.01f)
            t60 = 0.01f;
        r = fm_expf(-6.9077553f / (t60 * KM_SR)) * v->damp;
        v->w0[k] = FM_TWO_PI * f / KM_SR;
        v->c0[k] = fm_cosf(v->w0[k]);
        v->s0[k] = fm_sinf(v->w0[k]);
        v->rr[k] = r;
        v->amp[k] = a;
        nm = k + 1;
    }
    v->nm = (uint8_t)nm;
    voice_freq(v);
}

static void voice_damp_to(voice_t *v, float d)
{
    if (v->damp == d || !v->on)
        return;
    v->contact = 0;
    v->damp = d;
    voice_coefs(v);
}
static void voice_damp(voice_t *v) { voice_damp_to(v, FINGER_D); }

/* the voice for a field: the one already ringing it (a field is one membrane: struck again, it is the
 * same vibration), else a free one, else the quietest */
static voice_t *voice_ringing(int cents)
{
    int i;
    for (i = 0; i < KM_NVOICE; i++)
        if (vc[i].on && vc[i].cents == cents)
            return &vc[i];
    return 0;
}
static voice_t *voice_free(void)
{
    int i;
    for (i = 0; i < KM_NVOICE; i++)
        if (!vc[i].on)
            return &vc[i];
    return 0;
}
static voice_t *voice_for(int cents)
{
    int i, best = 0;
    float lo = 1e9f;
    voice_t *v = voice_ringing(cents);
    if (v || (v = voice_free()) != 0)
        return v;
    for (i = 0; i < KM_NVOICE; i++) {
        float l = vc[i].level * (vc[i].damp == FINGER_D ? 0.25f : 1.0f);   /* a damped field goes first */
        if (l < lo) {
            lo = l;
            best = i;
        }
    }
    {   /* the quietest goes to a fade slot (the one nearest silence, if both are busy) and fades out */
        voice_t *f = &vc[KM_NVOICE];
        int k;
        v = &vc[best];
        for (i = KM_NVOICE; i < KM_NVOICE + KM_NFADE; i++)
            if (!vc[i].on || vc[i].fade < f->fade)
                f = &vc[i];
        *f = *v;
        f->fade = 1.0f;
        f->fade_step = 1.0f / FADE_S;
        f->ex_ph = 0.5f;                                /* no more strike, no more click */
        f->click = 0.0f;
        for (k = 0; k < KM_NMODE; k++)
            v->y1[k] = v->y2[k] = 0.0f;
        v->on = 0;
        km_voice_level[best] = 0.0f;
        return v;
    }
}

/* the excitation of the shell and the air by this chunk's strikes: the shell a side, the cavity */
#define BLK 64
static float knock[BLK];                              /* the strikes' force: it pushes the air and the shell */
static float knock_amt = 0.5f;                         /* how much of a strike reaches the air (Port) */

/* how a strike wakes the modes */
enum { EMPH_ALL, EMPH_HARMONIC, EMPH_FUND, EMPH_OCT, EMPH_FIFTH };
static void strike(voice_t *v, int cents, int vel, int pan, float hard, int emph)
{
    const km_metal_t *m = km_metal(par[P_METAL]);
    /* a fingertip leaves the steel in tp: soft fingers (1.2 ms) round off the top, a firm flick
     * (0.2 ms) rings everything. The skin on the steel: a short, soft burst (the first ~10 ms of a real
     * strike are broadband), brighter and shorter the firmer the strike */
    float tp = 0.0012f * fm_exp2f(-2.6f * hard), vg = (float)vel * (1.0f / 127.0f), a, n;
    int k, again = v->on;
    if (!again)
        for (k = 0; k < KM_NMODE; k++)
            v->y1[k] = v->y2[k] = 0.0f;
    for (k = 0; k < KM_NMODE; k++) {
        /* (the ping's modes take no part in the pulse: the contact's impulse rings them, below) */
        static const float E[5][KM_NMODE] = {{1, 1, 1, 1, 1, 1, 0, 0, 0, 0},
                                             {0.06f, 1.4f, 1.4f, 1.3f, 1.3f, 0.8f, 0, 0, 0, 0},   /* a finger on the center */
                                             {1, 0.1f, 0.05f, 0.06f, 0.03f, 0, 0, 0, 0, 0},       /* sympathy: one partial */
                                             {0.1f, 1, 1, 0.08f, 0.04f, 0, 0, 0, 0, 0},
                                             {0.08f, 0.08f, 0.04f, 1, 1, 0, 0, 0, 0, 0}};
        v->emph[k] = E[emph][k];
    }
    v->cents = (int16_t)cents;
    v->live = 0xFFFFu;                                  /* every mode, until it falls silent */
    v->damp = again ? CONTACT_D : 1.0f;                 /* ringing: the fingertip lands on it first */
    v->contact = again ? (uint8_t)CONTACT_N : 0u;
    v->released = 0;
    voice_coefs(v);
    n = tp * KM_SR;                                     /* pulse samples */
    if (n < 1.0f)
        n = 1.0f;
    a = vg * vg * 0.55f;
    {
        float gl = GLIDE_MAX * vg * vg * (0.3f + 0.7f * hard) * (emph == EMPH_ALL ? 1.0f : 0.0f);
        v->glide_rise = 0.0f;
        v->rise_n = 0;
        if (again) {
            v->glide_pend = gl;
        } else {
            v->glide = gl;
            v->glide_pend = 0.0f;
            voice_freq(v);
        }
    }
    {   /* the ping: the instant of contact, steel on steel through the skin, too short for the finger's
         * pulse to carry. An impulse into the field's higher modes (a firm strike rings them more) */
        const float ap = a * (0.3f + 0.7f * hard) * (emph == EMPH_ALL ? 1.0f : emph == EMPH_HARMONIC ? 0.6f : 0.0f);
        for (k = M_PING; k < KM_NMODE; k++)
            v->y1[k] += v->amp[k] * v->s0[k] * ap;     /* (an impulse: it rings at amp x ap) */
    }
    v->ex_inc = 0.5f / n;                               /* half a turn over the contact */
    v->ex_ph = again ? -(float)(CONTACT_N * 64u) * v->ex_inc : 0.0f;
    v->ex_amp = a * (FM_PI * 0.5f) / n;                 /* area a: the first mode rings at ~a */
    v->click = emph == EMPH_ALL || emph == EMPH_HARMONIC ? a * m->touch * hard * (0.5f + 0.5f * hard) * 0.9f : 0.0f;
    v->click_dec = fm_expf(-1.0f / ((0.007f - 0.004f * hard) * KM_SR));
    v->click_a = 0.12f + 0.4f * hard;                   /* its low-pass: ~0.9 to ~4 kHz */
    a = ((float)pan * 0.01f * width + 1.0f) * (FM_PI * 0.25f);   /* pan: equal power */
    v->pl = fm_cosf(a);
    v->pr = fm_sinf(a);
    v->on = 1;
    v->quiet = 0;
    v->level = vg;
    km_voice_cents[v - vc] = (int16_t)cents;
}

static float strike_hard(int vel)
{
    return fm_clampf((float)par[P_STRIKE] * 0.01f + (float)(vel - 90) * (0.35f / 127.0f), 0.0f, 1.0f);
}

/* strikes to come (delayed hits, sympathetic rings, the slap's palm) */
enum { PD_STRIKE, PD_SYMP, PD_HIT, PD_SLAPDAMP };
#define NPEND 48
static struct {
    uint32_t due;
    int16_t cents;
    int8_t pan;
    uint8_t vel, on, type, emph;
} pend[NPEND];
static uint32_t now_s;                                  /* samples rendered since km_init */
static void pend_add(int type, int cents, int vel, int pan, int emph, uint32_t samples)
{
    int i;
    for (i = 0; i < NPEND; i++)
        if (!pend[i].on) {
            pend[i].on = 1;
            pend[i].type = (uint8_t)type;
            pend[i].due = now_s + samples;
            pend[i].cents = (int16_t)cents;
            pend[i].vel = (uint8_t)vel;
            pend[i].pan = (int8_t)pan;
            pend[i].emph = (uint8_t)emph;
            return;
        }
}

/* Sympathy: a struck note's partials (1, 2, 3 times it) meet other fields' partials, and those fields
 * start to sing. Each field whose fundamental, octave or fifth lands on one of them (within 12 cents)
 * rings that partial softly, a few milliseconds on. Only on a free voice and a field at rest: sympathy
 * never takes a sounding note's place, and a ringing field isn't struck again. */
static float sym_amt = 0.4f;
static void sympathy(int cents, int vel, float scale)
{
    static const int16_t P[3] = {0, 1200, 1902};     /* a field's partials, in cents over it */
    int i, a, b;
    if (sym_amt <= 0.0f || vel < 20)
        return;
    for (i = 0; i < pan_n; i++) {
        const int p = pan_c[i];
        int best = -1;
        float w = 0.0f;
        if (p == cents)
            continue;
        for (a = 0; a < 3; a++)                         /* the struck note's partial a */
            for (b = 0; b < 3; b++) {                   /* the other field's partial b */
                int d = cents + P[a] - (p + P[b]);
                float ww = (a == 0 || b == 0) ? 1.0f : 0.5f;
                if (d > -12 && d < 12 && ww > w) {
                    w = ww;
                    best = b;
                }
            }
        if (best < 0 && i == 0) {                       /* the ding is coupled to the air inside, which every
                                                         * strike pushes: it sounds faintly under every note */
            best = 0;
            w = 0.35f;
        }
        if (best < 0)
            continue;
        {
            int sv = (int)((float)vel * 0.32f * sym_amt * w * scale);
            if (sv >= 4)
                pend_add(PD_SYMP, p, sv, 0, best == 0 ? EMPH_FUND : best == 1 ? EMPH_OCT : EMPH_FIFTH,
                         (uint32_t)(0.006f * KM_SR) + (rnd() & 255u));
        }
    }
}

static void pluck(int cents, int vel, int pan)
{
    voice_t *v = voice_for(cents);
    float hard = strike_hard(vel);
    strike(v, cents, vel, pan, hard, EMPH_ALL);
    sympathy(cents, vel, 1.0f);
}

static void symp_ring(int cents, int vel, int emph)
{
    voice_t *v;
    if (voice_ringing(cents) || (v = voice_free()) == 0)
        return;
    {
        int i, pan = 0;
        for (i = 1; i < pan_n && i < KM_NPAN; i++)      /* where it sits: odd fields left, even right */
            if (pan_c[i] == cents)
                pan = (i & 1) ? -35 : 35;
        strike(v, cents, vel, pan, 0.0f, emph);
        v->ex_inc *= 0.25f;                             /* it builds up, slower than a strike */
        v->ex_amp *= 0.25f;
    }
}

static void retune_all(void)
{
    int i;
    for (i = 0; i < KM_NVOICE + KM_NFADE; i++)
        if (vc[i].on)
            voice_coefs(&vc[i]);
}

/* ---------------------------------------------------------------- shell --- */
/* The steel around the fields, 1.2 mm thick: two banks of NSHELL resonators, the left shoulder and the
 * right (a slap excites the modes near where it lands more), at its own inharmonic modes. Its lowest
 * mode scales with the thickness and goes as 1 / the size squared (a plate's bending modes). The air
 * inside rings at the Helmholtz frequency of the bottom port, ~92 Hz on a 53 cm pan, lower on a bigger
 * one (the volume grows faster than the port). */
#define NSHELL 10
static const float SH_RATIO[NSHELL] = {1.0f, 1.47f, 2.09f, 2.56f, 3.21f, 3.88f, 4.73f, 5.62f, 6.91f, 8.37f};
static const float SH_L[NSHELL] = {1.0f, 0.55f, 0.80f, 0.35f, 0.62f, 0.48f, 0.30f, 0.40f, 0.22f, 0.18f};
static const float SH_R[NSHELL] = {0.70f, 0.85f, 0.40f, 0.72f, 0.30f, 0.58f, 0.45f, 0.20f, 0.36f, 0.15f};
typedef struct {
    float b1, b2, gl, gr, y1l, y2l, y1r, y2r;
} shmode_t;
static shmode_t sh[NSHELL];
static float cav_b1, cav_b2, cav_g, cav_y1, cav_y2, port_lvl = 0.5f, perc_lvl = 0.7f;
static float sh_mute;                                   /* > 0: a palm on the shell (samples left) */
static float shp_l, shp_r, cavp;                        /* their levels, for the screen */
static uint32_t sh_idle;                                /* chunks of silence in a row */

static void reso(float hz, float t60, float *b1, float *b2, float *sn)
{
    float w = FM_TWO_PI * hz / KM_SR, r = fm_expf(-6.9077553f / (t60 * KM_SR));
    *b1 = 2.0f * r * fm_cosf(w);
    *b2 = -r * r;
    *sn = fm_sinf(w);
}

static void shell_coefs(void)
{
    const km_metal_t *m = km_metal(par[P_METAL]);
    const float sz = size_f(), th = thick_f(), f1 = 430.0f * th * sz * sz;
    int k;
    for (k = 0; k < NSHELL; k++) {
        float hz = f1 * SH_RATIO[k], t60 = m->shell_t60 * fm_powf(th, 0.7f) / (1.0f + 0.22f * (SH_RATIO[k] - 1.0f)),
              lvl = fm_powf(m->shell_bright, (float)k * 0.33f), sn;
        if (hz > 0.42f * KM_SR) {
            sh[k].gl = sh[k].gr = 0.0f;
            continue;
        }
        reso(hz, t60, &sh[k].b1, &sh[k].b2, &sn);
        sh[k].gl = SH_L[k] * lvl * sn;
        sh[k].gr = SH_R[k] * lvl * sn;
    }
    {
        float sn;
        reso(92.0f * fm_powf(sz, 1.5f), 0.38f, &cav_b1, &cav_b2, &sn);
        cav_g = sn;
    }
}

/* the skin on the steel: a burst of noise through a band-pass, a side */
typedef struct {
    float amp, dec, s1, s2, a1, a2, a3, k;
} burst_t;
static burst_t nb[2];
static void burst(burst_t *b, float amp, float hz, float q, float ms)
{
    float g = fm_tanf(FM_PI * hz / KM_SR), k = 1.0f / q;
    b->a1 = 1.0f / (1.0f + g * (g + k));
    b->a2 = g * b->a1;
    b->a3 = g * b->a2;
    b->k = k;
    b->amp += amp;
    b->dec = fm_expf(-1.0f / (ms * 0.001f * KM_SR));
}
static inline float burst_run(burst_t *b)
{
    float x = rnds() * b->amp, v3 = x - b->s2, v1 = b->a1 * b->s1 + b->a2 * v3, v2 = b->s2 + b->a2 * b->s1 + b->a3 * v3;
    b->s1 = 2.0f * v1 - b->s1;
    b->s2 = 2.0f * v2 - b->s2;
    b->amp *= b->dec;
    return v1 * b->k * 2.0f;
}

/* a force pulse into the shell or the air: a half-sine `ms` long, spread over the coming samples
 * (written into this chunk's excitation; a pulse longer than what's left of the chunk carries on in the
 * next through hit_tail) */
static float tail_l, tail_r, tail_c, tail_inc, tail_ph = 0.5f;
static void pulse(float l, float r, float c, float ms)
{
    float n = ms * 0.001f * KM_SR;
    if (n < 1.0f)
        n = 1.0f;
    tail_l = l * (FM_PI * 0.5f) / n;
    tail_r = r * (FM_PI * 0.5f) / n;
    tail_c = c * (FM_PI * 0.5f) / n;
    tail_inc = 0.5f / n;
    tail_ph = 0.0f;
}

/* The percussion: per hit, where it lands (-1 left, 0 middle, 1 right), the contact (ms: a flat palm is
 * long and deep, a fingertip short and bright), how much reaches the shell and the air, the skin's
 * burst (level, center Hz, q, decay ms), and how much the fields around it ring along. */
typedef struct {
    int8_t side;
    float ms, shell, cav, noise, nhz, nq, nms, sym;
} hit_t;
static const hit_t HITS[HIT_N] = {
    {-1, 3.2f, 1.00f, 0.75f, 0.38f, 1700.0f, 0.8f, 26.0f, 0.55f},    /* thwap, left */
    {-1, 0.35f, 0.55f, 0.12f, 0.55f, 3900.0f, 1.3f, 8.0f, 0.15f},    /* tak, left edge */
    {0, 7.0f, 0.22f, 1.80f, 0.14f, 380.0f, 0.7f, 34.0f, 0.30f},      /* gu */
    {0, 0.5f, 0.16f, 0.07f, 0.22f, 3100.0f, 1.0f, 6.0f, 0.0f},       /* ghost */
    {0, 1.2f, 0.30f, 0.70f, 0.30f, 1500.0f, 0.9f, 15.0f, 0.0f},      /* slap (and the ding, below) */
    {0, 0.3f, 0.42f, 0.10f, 0.48f, 4300.0f, 1.3f, 7.0f, 0.10f},      /* tak, by the ding */
    {0, 0.3f, 0.06f, 0.05f, 0.10f, 5200.0f, 1.0f, 3.0f, 0.0f},       /* harmonic (and the ding, below) */
    {0, 1.4f, 0.35f, 0.20f, 0.42f, 880.0f, 2.6f, 18.0f, 0.0f},       /* rim: a knuckle on the seam */
    {0, 2.5f, 0.0f, 0.15f, 0.10f, 600.0f, 0.7f, 30.0f, 0.0f},        /* mute: a palm landing */
    {1, 0.35f, 0.55f, 0.12f, 0.55f, 4100.0f, 1.3f, 8.0f, 0.15f},     /* tak, right edge */
    {1, 3.2f, 1.00f, 0.75f, 0.38f, 1850.0f, 0.8f, 26.0f, 0.55f},     /* thwap, right */
};

static void hit(int kind, int vel)
{
    const hit_t *h = &HITS[(unsigned)kind < HIT_N ? kind : 0];
    float vg = (float)vel * (1.0f / 127.0f), a = vg * vg * perc_lvl,
          l = h->side < 0 ? 1.0f : h->side > 0 ? 0.15f : 0.7f, r = h->side > 0 ? 1.0f : h->side < 0 ? 0.15f : 0.7f;
    int ding = pan_n ? pan_c[0] : 5000;
    pulse(a * h->shell * l * 0.55f, a * h->shell * r * 0.55f, a * h->cav * port_lvl * 1.6f, h->ms);
    if (h->noise > 0.0f) {
        float jit = 1.0f + 0.12f * rnds();                /* never quite the same hand twice */
        if (h->side <= 0)
            burst(&nb[0], a * h->noise * (h->side ? 1.0f : 0.7f), h->nhz * jit, h->nq, h->nms);
        if (h->side >= 0)
            burst(&nb[1], a * h->noise * (h->side ? 1.0f : 0.7f), h->nhz * jit, h->nq, h->nms);
    }
    if (kind == HIT_SLAP) {                             /* the ding struck, the palm stays on it */
        voice_t *v = voice_for(ding);
        strike(v, ding, vel, 0, 0.8f, EMPH_ALL);
        pend_add(PD_SLAPDAMP, ding, 0, 0, 0, (uint32_t)(0.03f * KM_SR));
    } else if (kind == HIT_HARM) {                      /* a finger on the ding's center: its octave sings */
        voice_t *v = voice_for(ding);
        strike(v, ding, vel, 0, 0.7f, EMPH_HARMONIC);
    } else if (kind == HIT_MUTE) {
        int i;
        for (i = 0; i < KM_NVOICE; i++)
            voice_damp(&vc[i]);
        for (i = 0; i < NPEND; i++)
            pend[i].on = 0;
        sh_mute = 0.05f * KM_SR;
        air_mute = 0.25f * KM_SR;
    }
    if (h->sym > 0.0f && pan_n) {                       /* the pan rings along: the ding, and the fields
                                                         * nearest the hand (odd fields left, even right) */
        int i, sv = (int)((float)vel * h->sym * (0.4f + 0.6f * sym_amt) * 0.45f);
        if (sv >= 4) {
            symp_ring(ding, sv, kind == HIT_GU ? EMPH_ALL : EMPH_FUND);
            for (i = 1; i < pan_n && i < 5; i++)
                if (h->side == 0 || (h->side < 0) == (i & 1))
                    pend_add(PD_SYMP, pan_c[i], sv * 2 / 3, 0, EMPH_FUND, (uint32_t)(0.004f * KM_SR) + (rnd() & 127u));
        }
    }
}

/* -------------------------------------------------------- shared bits --- */
typedef struct {
    float s1, s2, a1, a2, a3, gain;
} svf_t;
static float onepole(float hz) { return hz > 0.0f ? 1.0f - fm_expf(-FM_TWO_PI * hz / KM_SR) : 1.0f; }

/* ------------------------------------------------------------ reverb --- */
/* Dattorro's plate (J. Dattorro, "Effect Design, Part 1", JAES 1997), as FoMni has it: a predelay, four
 * input diffusers, then a figure-eight tank of two halves, each a modulated allpass, a delay, a damping
 * lowpass, an allpass and a delay, feeding the other half. Stereo out from seven taps a side. The
 * paper's lengths are for 29761 Hz; DS() scales them to 44.1 kHz. */
#define DS(n) (((n) * 14818 + 5000) / 10000)
#define PRE_N 442
#define EXC DS(16)
enum { L_PRE, L_IN1, L_IN2, L_IN3, L_IN4, L_APL, L_D1L, L_AP2L, L_D2L, L_APR, L_D1R, L_AP2R, L_D2R, L_N };
static const int DL_LEN[L_N] = {PRE_N, DS(142), DS(107), DS(379), DS(277), DS(672) + EXC + 2, DS(4453), DS(1800),
                                DS(3720), DS(908) + EXC + 2, DS(4217), DS(2656), DS(3163)};
#define DL_TOTAL (PRE_N + DS(142) + DS(107) + DS(379) + DS(277) + DS(672) + DS(4453) + DS(1800) + DS(3720) + DS(908) + \
                  DS(4217) + DS(2656) + DS(3163) + 2 * (EXC + 2))
static float pl_buf[DL_TOTAL] KM_POOL;
typedef struct {
    float *b;
    int n, i;
} dl_t;
static dl_t dl[L_N];
static float pl_bw, pl_damp_l, pl_damp_r, pl_decay = 0.5f, pl_dd2 = 0.5f, lfo_s, lfo_c = 1.0f;
/* Reverb 0 and the tail gone (out below -120 dB for 2 s, longer than a trip round the tank): the plate
 * stops until Reverb is turned up again. It is the biggest fixed cost; what it leaves is below hearing */
#define PL_IDLE 88200u
static uint32_t pl_quiet;

static inline float tap(const dl_t *d, int k)
{
    int j = d->i - k;
    return d->b[j < 0 ? j + d->n : j];
}
static inline void push(dl_t *d, float x)
{
    d->b[d->i] = x;
    if (++d->i >= d->n)
        d->i = 0;
}
static inline float allpass(dl_t *d, int len, float x, float g)
{
    float z = tap(d, len), v = x - g * z;
    push(d, fm_flush(v));
    return z + g * v;
}
static inline float allpass_mod(dl_t *d, float len, float x, float g)
{
    int k = (int)len;
    float f = len - (float)k, z = tap(d, k) + (tap(d, k + 1) - tap(d, k)) * f, v = x - g * z;
    push(d, fm_flush(v));
    return z + g * v;
}

static inline void reverb(float in, float *ol, float *or_)
{
    float x, a, b, fb_l = tap(&dl[L_D2R], DS(3163)), fb_r = tap(&dl[L_D2L], DS(3720));
    push(&dl[L_PRE], in);
    x = tap(&dl[L_PRE], PRE_N);
    pl_bw += 0.7f * (x - pl_bw);
    x = allpass(&dl[L_IN1], DS(142), pl_bw, 0.75f);
    x = allpass(&dl[L_IN2], DS(107), x, 0.75f);
    x = allpass(&dl[L_IN3], DS(379), x, 0.625f);
    x = allpass(&dl[L_IN4], DS(277), x, 0.625f);
    {
        float s = lfo_s + 0.000128f * lfo_c, c = lfo_c - 0.000128f * s;
        lfo_s = s;
        lfo_c = c;
    }
    a = allpass_mod(&dl[L_APL], (float)DS(672) + (float)EXC * lfo_s, x + pl_decay * fb_l, -0.7f);
    push(&dl[L_D1L], a);
    b = tap(&dl[L_D1L], DS(4453));
    pl_damp_l += 0.62f * (b - pl_damp_l);
    b = allpass(&dl[L_AP2L], DS(1800), pl_damp_l * pl_decay, pl_dd2);
    push(&dl[L_D2L], b);
    a = allpass_mod(&dl[L_APR], (float)DS(908) + (float)EXC * lfo_c, x + pl_decay * fb_r, -0.7f);
    push(&dl[L_D1R], a);
    b = tap(&dl[L_D1R], DS(4217));
    pl_damp_r += 0.62f * (b - pl_damp_r);
    b = allpass(&dl[L_AP2R], DS(2656), pl_damp_r * pl_decay, pl_dd2);
    push(&dl[L_D2R], b);
    *ol = 0.6f * (tap(&dl[L_D1R], DS(266)) + tap(&dl[L_D1R], DS(2974)) - tap(&dl[L_AP2R], DS(1913)) +
                  tap(&dl[L_D2R], DS(1996)) - tap(&dl[L_D1L], DS(1990)) - tap(&dl[L_AP2L], DS(187)) -
                  tap(&dl[L_D2L], DS(1066)));
    *or_ = 0.6f * (tap(&dl[L_D1L], DS(353)) + tap(&dl[L_D1L], DS(3627)) - tap(&dl[L_AP2L], DS(1228)) +
                   tap(&dl[L_D2L], DS(2673)) - tap(&dl[L_D1R], DS(2111)) - tap(&dl[L_AP2R], DS(335)) -
                   tap(&dl[L_D2R], DS(121)));
}

static void plate_init(void)
{
    float *p = pl_buf;
    int k, i;
    for (k = 0; k < L_N; k++) {
        dl[k].b = p;
        dl[k].n = DL_LEN[k];
        dl[k].i = 0;
        p += DL_LEN[k];
    }
    for (i = 0; i < DL_TOTAL; i++)
        pl_buf[i] = 0.0f;
    pl_bw = pl_damp_l = pl_damp_r = 0.0f;
    lfo_s = 0.0f;
    lfo_c = 1.0f;
    pl_quiet = 0;
}

/* -------------------------------------------------- half-rate lines --- */
/* The delay and the grain buffer run at 22.05 kHz in int16: twice the time in a quarter of the memory
 * of a float line. An echo of steel is dark anyway; the grains are mostly pitched up, and the pan
 * has little above 11 kHz. */
#define DLY_N 32768u                                    /* 1.49 s: a ping-pong of up to 0.74 s */
#define GRB_N 24576u                                    /* 1.11 s of grain source */
static int16_t dly_buf[DLY_N] KM_POOL;
static int16_t grb_buf[GRB_N] KM_POOL;
/* Stored at 0.3 of the signal through a soft limiter (tanh), read back x 1/0.3: a flurry of loud notes
 * sums to several times full scale before the output stage, which hard-clipped these lines (crackle in
 * the grains and the echoes). Linear within ~3 % up to 1.0, soft above, never a hard edge. */
#define LINE_IN 0.3f
#define S16 32767.0f
#define S16I (1.0f / (32767.0f * LINE_IN))
static inline int16_t to16(float x)
{
    x = fm_tanhf(x * LINE_IN) * S16;
    return (int16_t)(x < 0.0f ? x - 0.5f : x + 0.5f);
}

#ifdef OM_HOST
/* the device's .pool is 0x54000 bytes and the build keeps 8 KiB spare; the canvas (gfx.c) takes 240 x 144
 * x 2. Checked here because the device build cannot run where these tests do. */
_Static_assert(sizeof pl_buf + sizeof dly_buf + sizeof grb_buf + 4u * (1024u + 2u * 512u) + 240u * 144u * 2u <=
                   0x54000u - 8192u,
               "the .pool would overflow");                  /* (+ the chorus and tape lines, below) */
#endif

/* ---- the delay: one line, two taps. Written x = in + fb^2 * line(2T); the left hears line(T), the right
 * line(2T) * fb: echoes left, right, left... each fb of the one before. Its loop is darkened. */
static uint32_t dw;
static float d_t = 330.0f * 22.05f, d_tt, d_fb, d_lp, d_mix, d_prev_l, d_prev_r, d_cur_l, d_cur_r, d_in;

/* the line `back` (22.05 kHz samples, < DLY_N) behind the write position; dw stays wrapped, so the
 * integer part and the fraction keep every bit however long the device has been on */
static inline float dly_read(float back)
{
    uint32_t ib = (uint32_t)back, i0 = (dw - ib) & (DLY_N - 1u), i1 = (i0 - 1u) & (DLY_N - 1u);
    float f = back - (float)ib;
    return ((float)dly_buf[i0] + ((float)dly_buf[i1] - (float)dly_buf[i0]) * f) * S16I;
}

static inline void delay_tick(float in)                 /* one 22.05 kHz step */
{
    float l, r;
    d_t += 0.0015f * (d_tt - d_t);                      /* a turn of Time glides (a tape's pitch bend) */
    l = dly_read(d_t);
    r = dly_read(2.0f * d_t);
    d_lp += 0.45f * (r - d_lp);
    dly_buf[dw] = to16(in + d_fb * d_fb * d_lp);
    dw = (dw + 1u) & (DLY_N - 1u);
    d_prev_l = d_cur_l;
    d_prev_r = d_cur_r;
    d_cur_l = l;
    d_cur_r = r * d_fb;
}

/* ---- the grains: a ring of the dry sound, and up to NGRAIN windowed readers of it */
#define NGRAIN 10
typedef struct {
    float pos, rate, wph, winc, amp, pl, pr;
    uint8_t on;
} grain_t;
static grain_t gr[NGRAIN];
static uint32_t gw;                                     /* the write position, 0..GRB_N-1 */
static float g_timer, g_acc, win[257];

static inline float grb_read(float p)                   /* p in 0..GRB_N */
{
    int i;
    float f;
    i = (int)p;
    f = p - (float)i;
    {
        uint32_t a = (uint32_t)i >= GRB_N ? 0u : (uint32_t)i, b = a + 1u >= GRB_N ? 0u : a + 1u;
        return ((float)grb_buf[a] + ((float)grb_buf[b] - (float)grb_buf[a]) * f) * S16I;
    }
}

static void grain_spawn(void)
{
    static const float RATIO[GP_N] = {0.5f, 0.6674f, 1.0f, 1.4983f, 2.0f, 2.9966f, 1.0f, -1.0f};
    grain_t *g = 0;
    int i, gp = par[P_GPITCH];
    float spray = (float)par[P_GSPRAY] * 0.01f, len, ratio, back, rb;
    for (i = 0; i < NGRAIN; i++)
        if (!gr[i].on) {
            g = &gr[i];
            break;
        }
    if (!g)
        return;
    ratio = RATIO[(unsigned)gp < GP_N ? gp : 2];
    if (gp == GP_SHIMMER) {                             /* unison, fifth up, octave up: the shimmer */
        uint32_t c = rnd() % 3u;
        ratio = c == 0u ? 1.0f : c == 1u ? 1.4983f : 2.0f;
    }
    ratio *= fm_exp2f(rnds() * spray * (0.15f / 12.0f));   /* spray detunes a little too */
    len = (float)par[P_GSIZE] * (KM_SR / 1000.0f) * (1.0f + 0.4f * spray * rnds());
    rb = ratio * 0.5f;                                  /* buffer samples per output sample */
    if (len * fm_fabsf(rb) > (float)GRB_N * 0.45f)
        len = (float)GRB_N * 0.45f / fm_fabsf(rb);
    if (len < 256.0f)
        len = 256.0f;
    {   /* where it starts: behind the write head (moving at h) by enough that a fast grain never
         * reaches it, and not so far that a slow (or reversed) one falls off the oldest end */
        float h = km_frozen ? 0.0f : 0.5f, lo = 64.0f + fm_maxf(0.0f, (rb - h) * len),
              hi = (float)GRB_N - 64.0f - fm_maxf(0.0f, (h - rb) * len);
        back = lo + spray * rndf() * (float)GRB_N * 0.5f;
        if (back > hi)
            back = hi;
    }
    g->pos = (float)gw - back;                          /* positions stay within the ring */
    if (g->pos < 0.0f)
        g->pos += (float)GRB_N;
    g->rate = rb;
    g->wph = 0.0f;
    g->winc = 256.0f / len;
    {
        float dens = (float)par[P_GDENS], over = dens * len / KM_SR, p = rnds() * (0.3f + 0.7f * spray) * width;
        g->amp = 1.0f / fm_sqrtf(1.0f + over);
        g->pl = fm_sqrtf(0.5f - 0.5f * p);
        g->pr = fm_sqrtf(0.5f + 0.5f * p);
    }
    g->on = 1;
}

/* ------------------------------------------------- chorus, filter, tape --- */
/* Chorus: the dry sound (mono) into a 23 ms line, read by two taps that wander +-3 ms around 12 ms, a
 * quarter cycle apart, left and right: a slow ensemble shimmer, wide. On the instrument, before the
 * echoes and the room. */
#define CH_N 1024u
static float ch_buf[CH_N] KM_POOL;
static uint32_t ch_w;
static float ch_ph, ch_amt, ch_t, ch_at, ch_ad;      /* LFO phase (turns); amount now, target; this chunk's
                                                       * end value and step a sample (every setting glides
                                                       * sample by sample: a step a chunk was a click) */
static float ch_dl0, ch_dr0;                          /* the taps' delays at the start of this chunk */

static inline float line_read(const float *b, uint32_t mask, uint32_t w, float back)
{
    uint32_t ib = (uint32_t)back, i0 = (w - ib) & mask, i1 = (i0 - 1u) & mask;
    float f = back - (float)ib;
    return b[i0] + (b[i1] - b[i0]) * f;
}

/* Filter: one knob. Left of center a low-pass from 20 kHz down to ~200 Hz, right a high-pass from 20 Hz
 * up to ~3.9 kHz, the resonance rising with the sweep; center is off. Simper's SVF per channel, its
 * setting glides (no zipper, no click) */
static svf_t flt[2];
static float flt_t, flt_s;                     /* 1/q; target (-1..1) and how far now (0..1) */
static int flt_mode = -1;                             /* -1 low-pass, +1 high-pass (off: flt_w 0) */
static float flt_w, flt_wd;                           /* how much of the filtered sound (crossfade in and out) */
static float flt_c[4], flt_cd[4];                     /* a1 a2 a3 k now, and their step a sample: the setting
                                                       * moves sample by sample (a high-pass shows any jump) */

static float flt_inv = 1.0f / 64.0f;                  /* 1 / the chunk's length */
static void filter_coefs(void)
{
    float a = fm_fabsf(flt_s), hz = flt_mode < 0 ? 20000.0f * fm_exp2f(-6.6f * a) : 20.0f * fm_exp2f(7.6f * a), g;
    int i;
    if (hz > 19000.0f)
        hz = 19000.0f;
    g = fm_tanf(FM_PI * hz / KM_SR);
    {
        float k = 1.0f / (0.707f + 0.9f * a), a1 = 1.0f / (1.0f + g * (g + k)), to[4];
        to[0] = a1;
        to[1] = g * a1;
        to[2] = g * g * a1;
        to[3] = k;
        for (i = 0; i < 4; i++)
            flt_cd[i] = (to[i] - flt_c[i]) * flt_inv;
    }
}

static inline float filter_run(svf_t *f, float x)
{
    float v3 = x - f->s2, v1 = flt_c[0] * f->s1 + flt_c[1] * v3, v2 = f->s2 + flt_c[1] * f->s1 + flt_c[2] * v3;
    f->s1 = 2.0f * v1 - f->s1;
    f->s2 = 2.0f * v2 - f->s2;
    return flt_mode < 0 ? v2 : x - flt_c[3] * v1 - v2;
}

/* Tape: the whole output through a short line whose length wanders, slowly (wow, 0.55 Hz, and a slower
 * drift) and quickly (flutter, 7.3 Hz): the pitch wavers, up to ~12 cents at Tape 100. And saturates, as
 * tape does when it's driven. Lo-fi: a cheaper machine. The top rolls off (down to ~2.8 kHz), the image
 * narrows, and there is hiss, which follows the music down (as a noise reducer does), so silence is
 * silent. */
#define TP_N 512u
static float tp_l[TP_N] KM_POOL, tp_r[TP_N] KM_POOL;
static uint32_t tp_w;
static float tp_amt, tp_t, tp_mix, wow_ph, flut_ph, drift, drift_t, tp_d0, tp_ad, tp_md, tp_at, tp_mt;
static float lf_amt, lf_t, lf_a = 1.0f, lf_l, lf_r, hiss_env, lf_ad, lf_at, lf_aa, lf_aad;

/* the three, on one stereo sample each; the chunk's settings come from fx_chunk() */
static float ch_dl, ch_dr, ch_dd_l, ch_dd_r, tp_d, tp_dd;
static void fx_chunk(uint32_t n)
{
    float c;
    const float inv = 1.0f / (float)n;
    /* chorus: the amount glides; the taps' delays move linearly across the chunk */
    ch_amt = ch_at;                                    /* (where the last chunk's ramp ended) */
    ch_at += 0.2f * (ch_t - ch_at);
    if (ch_at < 1e-4f && ch_t == 0.0f)
        ch_at = 0.0f;
    ch_ad = (ch_at - ch_amt) * inv;
    ch_ph += 0.8f * (float)n / KM_SR;
    if (ch_ph >= 1.0f)
        ch_ph -= 1.0f;
    c = 529.2f + 132.3f * fm_sin_turns(ch_ph);          /* 12 ms +- 3 ms, in samples */
    ch_dd_l = (c - ch_dl0) / (float)n;
    ch_dl = ch_dl0;
    ch_dl0 = c;
    c = 529.2f + 132.3f * fm_sin_turns(ch_ph + 0.25f);
    ch_dd_r = (c - ch_dr0) / (float)n;
    ch_dr = ch_dr0;
    ch_dr0 = c;
    /* filter: always running, at its last setting when it is off, so it always holds the sound's
     * state: it fades in and out (~6 ms) without the burst of a filter started from rest, and goes from
     * low-pass to high-pass (the same filter, read the other way) by fading out, turning, fading in */
    {
        const int want = flt_t < 0.0f ? -1 : flt_t > 0.0f ? 1 : 0;
        float target;
        if (want && want != flt_mode && flt_w <= 0.0f)
            flt_mode = want;
        target = want && want == flt_mode ? 1.0f : 0.0f;
        if (target > 0.0f)
            flt_s += 0.12f * (fm_fabsf(flt_t) - flt_s);   /* (how far, on its side) */
        if (target == 0.0f && flt_w < 0.005f)
            flt_w = 0.0f;
        flt_wd = (target - flt_w) * 0.25f * inv;        /* (a quarter of the way a chunk) */
        flt_inv = inv;
        filter_coefs();
    }
    /* tape: in and out of the line with a quick crossfade; the line's length from wow, drift, flutter */
    tp_amt = tp_at;
    tp_mix = tp_mt;
    tp_at += 0.1f * (tp_t - tp_at);
    tp_mt += 0.25f * ((tp_t > 0.0f ? 1.0f : 0.0f) - tp_mt);
    if (tp_mt < 1e-4f && tp_t == 0.0f)
        tp_mt = tp_at = 0.0f;
    tp_ad = (tp_at - tp_amt) * inv;
    tp_md = (tp_mt - tp_mix) * inv;
    wow_ph += 0.55f * (float)n / KM_SR;
    flut_ph += 7.3f * (float)n / KM_SR;
    wow_ph -= wow_ph >= 1.0f ? 1.0f : 0.0f;
    flut_ph -= flut_ph >= 1.0f ? 1.0f : 0.0f;
    if ((now_s & 4095u) < n)
        drift_t = rnds();                              /* a new place to drift to, every ~0.1 s */
    drift += 0.004f * (drift_t - drift);
    c = 220.5f + tp_at * (88.2f * fm_sin_turns(wow_ph) + 30.0f * drift + 3.5f * fm_sin_turns(flut_ph));
    tp_dd = (c - tp_d0) / (float)n;
    tp_d = tp_d0;
    tp_d0 = c;
    /* lo-fi */
    lf_amt = lf_at;
    lf_a = lf_aa;
    lf_at += 0.1f * (lf_t - lf_at);
    if (lf_at < 1e-4f && lf_t == 0.0f)
        lf_at = 0.0f;
    lf_aa = onepole(18000.0f * fm_exp2f(-2.7f * lf_at));
    lf_ad = (lf_at - lf_amt) * inv;
    lf_aad = (lf_aa - lf_a) * inv;
}

static inline void fx_chorus(float *l, float *r)
{
    ch_buf[ch_w] = (*l + *r) * 0.5f;
    if (ch_amt > 0.0f || ch_ad > 0.0f) {
        float a = line_read(ch_buf, CH_N - 1u, ch_w, ch_dl), b = line_read(ch_buf, CH_N - 1u, ch_w, ch_dr);
        *l = *l * (1.0f - 0.25f * ch_amt) + a * 0.75f * ch_amt;
        *r = *r * (1.0f - 0.25f * ch_amt) + b * 0.75f * ch_amt;
    }
    ch_w = (ch_w + 1u) & (CH_N - 1u);
    ch_dl += ch_dd_l;
    ch_dr += ch_dd_r;
    ch_amt += ch_ad;
}

static inline void fx_out(float *l, float *r)
{
    {
        float a = filter_run(&flt[0], *l), b = filter_run(&flt[1], *r);
        int c;
        *l += (a - *l) * flt_w;
        *r += (b - *r) * flt_w;
        flt_w += flt_wd;
        for (c = 0; c < 4; c++)
            flt_c[c] += flt_cd[c];
        if (flt_w < 0.0f)
            flt_w = 0.0f;
    }
    tp_l[tp_w] = *l;
    tp_r[tp_w] = *r;
    if (tp_mix > 0.0f || tp_md > 0.0f) {               /* through the tape: wavering, driven */
        float a = line_read(tp_l, TP_N - 1u, tp_w, tp_d), b = line_read(tp_r, TP_N - 1u, tp_w, tp_d),
              drive = 1.0f + 2.0f * tp_amt, mk = 1.0f / drive;
        a = fm_tanhf(a * drive) * mk;
        b = fm_tanhf(b * drive) * mk;
        *l += (a - *l) * tp_mix;
        *r += (b - *r) * tp_mix;
    }
    tp_w = (tp_w + 1u) & (TP_N - 1u);
    tp_d += tp_dd;
    tp_amt += tp_ad;
    tp_mix += tp_md;
    if (lf_amt > 0.0f || lf_ad > 0.0f) {               /* a cheaper machine */
        float m = (*l + *r) * 0.5f, sd = (*l - *r) * 0.5f * (1.0f - 0.5f * lf_amt), e = fm_fabsf(m);
        lf_l += lf_a * (m + sd - lf_l);
        lf_r += lf_a * (m - sd - lf_r);
        hiss_env = e > hiss_env ? e : hiss_env * 0.99997f;   /* the hiss follows the music, ~1.5 s down */
        *l = lf_l + rnds() * lf_amt * 0.006f * fm_minf(1.0f, hiss_env * 8.0f);
        *r = lf_r + rnds() * lf_amt * 0.006f * fm_minf(1.0f, hiss_env * 8.0f);
        lf_amt += lf_ad;
        lf_a += lf_aad;
    } else {
        lf_l = *l;
        lf_r = *r;
    }
}

/* ------------------------------------------------------------- grooves --- */
/* Grooves over the pan: twelve pulses a cycle, three a beat. Each pulse plays up to two things: the
 * ding (D), a field (F1..F8, low to high, wrapping on a pan with fewer), or the percussion: a tak (K), a
 * ghost (H), the Gu (G), a soft left thwap (S). The first pulse of the cycle is accented, and no two
 * strikes are quite the same strength. */
enum { RD = 1, RF1, RF2, RF3, RF4, RF5, RF6, RF7, RF8, RK = 20, RH, RG, RS };
static const uint8_t PATS[PAT_N][12][2] = {
    /* Heartbeat: the ding on the beat, fields answering, taks and ghosts between */
    {{RD}, {0}, {RH}, {RF2}, {RK}, {RF1}, {RD}, {RH}, {RD}, {RF3}, {RK}, {RF2}},
    /* Cascade: down the fields into the ding, and back up */
    {{RF7}, {RF6}, {RF5}, {RF4}, {RF3}, {RF2}, {RF1}, {RD}, {RF1}, {RF2}, {RF3}, {RF4}},
    /* 3 over 2: the ding and its fifth every three pulses against the fields every two */
    {{RD, RF3}, {0}, {RF4}, {RF1}, {RF5}, {RH}, {RD, RF4}, {0}, {RF3}, {RF1}, {RF2}, {RH}},
    /* Backbeat: the ding and the Gu, thwaps on the backbeat, taks driving it */
    {{RD, RG}, {RK}, {RF3}, {RS}, {RK}, {RF2}, {RD}, {RK}, {RF4}, {RS}, {RF3}, {RH}},
};
static int pat_on, pat_step, pat_run = 1;
static float pat_left;
static uint32_t ext_ticks, ext_age;
volatile uint8_t km_pat_on, km_pat_pulse, km_ext;

static void pat_fire(void)
{
    const int p = (unsigned)par[P_PATTERN] < PAT_N ? par[P_PATTERN] : 0, pulse = pat_step % 12;
    const int tops = pan_n > 1 ? (pan_n < KM_NPAN ? pan_n : KM_NPAN) - 1 : 0;
    int k;
    for (k = 0; k < 2; k++) {
        int r = PATS[p][pulse][k], vel = 80 + (pulse == 0 ? 18 : 0) + (int)(rnds() * 7.0f);
        if (!r)
            continue;
        if (r == RK)
            hit(HIT_TAK, vel - 8);
        else if (r == RH)
            hit(HIT_GHOST, vel - 20);
        else if (r == RG)
            hit(HIT_GU, vel);
        else if (r == RS)
            hit(HIT_THWAP_L, vel - 22);
        else if (pan_n) {
            int i = r == RD || !tops ? 0 : 1 + (r - RF1) % tops;
            pluck(pan_c[i], r == RD ? vel + 6 : vel, i ? ((i & 1) ? -40 : 40) : 0);
        }
    }
    km_pat_pulse = (uint8_t)pulse;
    pat_step = (pulse + 1) % 12;
}

static void pat_chunk(uint32_t n)
{
    float len;
    if (!pat_on || !pan_n) {
        pat_left = 0.0f;
        return;
    }
    if (km_ext || !pat_run)                             /* (MIDI clock leads, or it was stopped) */
        return;
    len = KM_SR * 60.0f / ((float)par[P_PTEMPO] * 3.0f);
    pat_left -= (float)n;
    if (pat_left <= 0.0f) {
        pat_fire();
        pat_left += len;
        if (pat_left < 0.0f)
            pat_left = len;
    }
}

/* ---------------------------------------------------------- parameters --- */
static float rsend, g_mix;

static void apply(int p, int v)
{
    const km_param_t *pi = &PARAMS[p];
    if (v < pi->lo)
        v = pi->lo;
    if (v > pi->hi)
        v = pi->hi;
    par[p] = (int16_t)v;
    switch (p) {
    case P_STRIKE: break;
    case P_SUSTAIN: case P_HARMONICS: retune_all(); break;
    case P_METAL: case P_SIZE: case P_THICK:
        retune_all();
        shell_coefs();
        break;
    case P_TUNE:
        tunefac = fm_exp2f((float)v * (1.0f / 1200.0f));
        retune_all();
        air_dirty = 1;
        break;
    case P_SYMPATHY:
        sym_amt = (float)v * 0.01f;
        air_dirty = 1;
        break;
    case P_PORT:
        port_lvl = (float)v * 0.01f;
        knock_amt = 0.9f * port_lvl;
        break;
    case P_PERC: perc_lvl = (float)v * 0.01f; break;
    case P_REVERB: rsend = (float)v * 0.01f * (float)v * 0.01f; break;
    case P_RSIZE:
        pl_decay = 0.35f + 0.6f * (float)v * 0.01f;
        pl_dd2 = fm_clampf(pl_decay + 0.15f, 0.25f, 0.5f);
        break;
    case P_DELAY: d_mix = (float)v * 0.01f; break;
    case P_TIME: d_tt = (float)v * 22.05f; break;
    case P_FEEDBACK: d_fb = (float)v * 0.01f; break;
    case P_GRAIN: g_mix = (float)v * 0.01f * 1.4f; break;
    case P_WIDTH:
        width = (float)v * 0.01f;
        air_dirty = 1;
        break;
    case P_TAPE: tp_t = (float)v * 0.01f; break;
    case P_LOFI: lf_t = (float)v * 0.01f; break;
    case P_CHORUS: ch_t = (float)v * 0.01f; break;
    case P_FILTER: flt_t = (float)v * 0.01f; break;
    case P_WORN:
        worn = (float)v * 0.01f;
        retune_all();                                   /* the ringing fields take their quirks at once */
        break;
    default: break;
    }
}

static void shell_quiet(void)
{
    int k;
    for (k = 0; k < NSHELL; k++)
        sh[k].y1l = sh[k].y2l = sh[k].y1r = sh[k].y2r = 0.0f;
    cav_y1 = cav_y2 = 0.0f;
    nb[0].amp = nb[1].amp = nb[0].s1 = nb[0].s2 = nb[1].s1 = nb[1].s2 = 0.0f;
    tail_ph = 0.5f;
    sh_mute = 0.0f;
    shp_l = shp_r = cavp = 0.0f;
    km_shell_level[0] = km_shell_level[1] = km_cavity_level = 0.0f;
}

static void quiet(void)
{
    int i;
    for (i = 0; i < KM_NVOICE + KM_NFADE; i++)
        vc[i].on = 0;
    for (i = 0; i < KM_NVOICE; i++) {
        km_voice_level[i] = 0.0f;
        km_voice_cents[i] = -1;
    }
    for (i = 0; i < NPEND; i++)
        pend[i].on = 0;
    pat_on = 0;                                         /* (the UI's ARP goes off with it: ui_init) */
    km_pat_on = 0;
    for (i = 0; i < NGRAIN; i++)
        gr[i].on = 0;
    for (i = 0; i < (int)DLY_N; i++)
        dly_buf[i] = 0;
    for (i = 0; i < (int)CH_N; i++)
        ch_buf[i] = 0.0f;
    for (i = 0; i < (int)TP_N; i++)
        tp_l[i] = tp_r[i] = 0.0f;
    flt[0].s1 = flt[0].s2 = flt[1].s1 = flt[1].s2 = 0.0f;
    lf_l = lf_r = hiss_env = 0.0f;
    shell_quiet();
    air_quiet_all();
    d_lp = d_cur_l = d_cur_r = d_prev_l = d_prev_r = 0.0f;
    plate_init();
}

void km_init(void)
{
    int i;
    q_r = q_w = 0;
    rng = 0x12345678u;                                  /* the same performance, the same sound (the tests) */
    for (i = 0; i < 257; i++)                           /* Hann */
        win[i] = 0.5f - 0.5f * fm_cosf(FM_TWO_PI * (float)i / 256.0f);
    for (i = 0; i < (int)GRB_N; i++)
        grb_buf[i] = 0;
    gw = dw = 0;
    km_frozen = 0;
    bendfac = 1.0f;
    for (i = 0; i < P_NPARAMS; i++)
        apply(i, PARAMS[i].def);
    d_t = d_tt;
    ch_amt = ch_at = ch_t;
    flt_s = fm_fabsf(flt_t);
    flt_mode = flt_t > 0.0f ? 1 : -1;
    flt_w = flt_t != 0.0f ? 1.0f : 0.0f;
    flt_inv = 1.0f;
    filter_coefs();                                     /* a step of the whole way: in place at once */
    {
        int c;
        for (c = 0; c < 4; c++) {
            flt_c[c] += flt_cd[c];
            flt_cd[c] = 0.0f;
        }
    }
    tp_amt = tp_at = tp_t;
    tp_mix = tp_mt = tp_t > 0.0f ? 1.0f : 0.0f;
    lf_amt = lf_at = lf_t;
    lf_a = lf_aa = onepole(18000.0f * fm_exp2f(-2.7f * lf_t));
    ch_dl0 = ch_dr0 = 529.2f;
    tp_d0 = 220.5f;
    pan_n = 0;
    pat_on = 0;
    km_pat_on = 0;
    km_ext = 0;
    quiet();
    shell_coefs();
}

static uint8_t sustain;

static void drain(void)
{
    int bent = 0;
    while (q_r != q_w) {
        cmd_t c = q[q_r % QN];
        int i;
        BARRIER();
        q_r++;
        switch (c.c) {
        case C_SET:
            if (c.a >= 0 && c.a < P_NPARAMS)
                apply(c.a, c.b);
            break;
        case C_PLUCK:
            if (c.d > 0)
                pend_add(PD_STRIKE, c.a, c.b, c.pan, 0, (uint32_t)c.d * 44u + (uint32_t)c.d / 10u);
            else
                pluck(c.a, c.b, c.pan);
            break;
        case C_HIT:
            if (c.d > 0)
                pend_add(PD_HIT, c.a, c.b, 0, 0, (uint32_t)c.d * 44u + (uint32_t)c.d / 10u);
            else
                hit(c.a, c.b);
            break;
        case C_DAMP:
            for (i = 0; i < KM_NVOICE; i++)
                if (vc[i].on && vc[i].cents == c.a) {
                    if (sustain)
                        vc[i].released = 1;
                    else
                        voice_damp(&vc[i]);
                }
            for (i = 0; i < NPEND; i++)                  /* a strike let go before it got there */
                if (pend[i].on && pend[i].type == PD_STRIKE && pend[i].cents == c.a && !sustain)
                    pend[i].on = 0;
            break;
        case C_DAMPALL:
            for (i = 0; i < KM_NVOICE; i++)
                voice_damp(&vc[i]);
            for (i = 0; i < NPEND; i++)
                pend[i].on = 0;
            sh_mute = 0.05f * KM_SR;
            air_mute = 0.25f * KM_SR;
            break;
        case C_SUSTAIN:
            sustain = (uint8_t)(c.a != 0);
            if (!sustain)
                for (i = 0; i < KM_NVOICE; i++)
                    if (vc[i].released)
                        voice_damp(&vc[i]);
            break;
        case C_BEND:
            bendfac = fm_exp2f((float)c.a * (2.0f / 12.0f / 8192.0f));
            bent = 1;                                    /* a wheel sends dozens a block: retune once */
            break;
        case C_FREEZE:
            km_frozen = (uint8_t)(c.a != 0);
            break;
        case C_PANIC:
            quiet();
            break;
        case C_PATTERN:
            pat_on = c.a != 0;
            km_pat_on = (uint8_t)pat_on;
            pat_run = 1;                             /* (ARP starts it, whatever a Stop said) */
            pat_step = 0;
            pat_left = 0.0f;                         /* on: the first pulse at once */
            break;
        case C_PANCLR:
            pan_n = 0;
            air_dirty = 1;
            break;
        case C_PANADD:
            if (pan_n < KM_NPAN + KM_NDUO)
                pan_c[pan_n++] = c.a;
            air_dirty = 1;
            break;
        case C_CLOCK:
            ext_age = 0;
            if (c.a == KM_CLK_TICK) {
                km_ext = 1;                          /* someone else leads */
                if (pat_on && pan_n && pat_run && ext_ticks % 8u == 0u)   /* 24 a beat, 3 pulses */
                    pat_fire();
                ext_ticks++;
            } else if (c.a == KM_CLK_START) {
                ext_ticks = 0;
                pat_step = 0;
                pat_run = 1;
            } else if (c.a == KM_CLK_CONTINUE) {
                pat_run = 1;
            } else {
                pat_run = 0;
            }
            break;
        }
    }
    if (bent) {
        retune_all();
        air_dirty = 1;
    }
}

/* ------------------------------------------------------------- render --- */
static float mixl[BLK], mixr[BLK], mono[BLK], xb[BLK];

static void render_voices(uint32_t n)
{
    int i, k;
    uint32_t j;
    const int ding = pan_n ? pan_c[0] : -1;
    for (i = 0; i < KM_NVOICE + KM_NFADE; i++) {        /* the fields, then the fade slots */
        voice_t *v = &vc[i];
        float pk = 0.0f, vb[BLK];
        int excite = v->ex_ph < 0.5f;
        if (!v->on)
            continue;
        if (excite) {                                   /* the pulse (ex_ph < 0: the fingertip still on it) */
            float ph = v->ex_ph, kg = v->cents == ding ? 2.2f : 1.0f;   /* the ding moves the most air */
            for (j = 0; j < n; j++) {
                xb[j] = ph >= 0.0f && ph < 0.5f ? v->ex_amp * fm_sin_turns(ph) : 0.0f;
                knock[j] += xb[j] * kg;
                ph += v->ex_inc;
            }
            v->ex_ph = ph;
        }
        for (j = 0; j < n; j++)
            vb[j] = 0.0f;
        for (k = 0; k < v->nm; k++) {
            float y1 = v->y1[k], y2 = v->y2[k];
            const float b1 = v->b1[k], b2 = v->b2[k], g = v->g[k];
            if (!(v->live >> k & 1u) || (g == 0.0f && y1 == 0.0f && y2 == 0.0f))
                continue;
            if (excite && g != 0.0f) {
                for (j = 0; j < n; j++) {
                    float y = b1 * y1 + b2 * y2 + g * xb[j];
                    y2 = y1;
                    y1 = y;
                    vb[j] += y;
                }
            } else {
                for (j = 0; j < n; j++) {
                    float y = b1 * y1 + b2 * y2;
                    y2 = y1;
                    y1 = y;
                    vb[j] += y;
                }
            }
            v->y1[k] = fm_flush(y1);
            v->y2[k] = fm_flush(y2);
            if (!excite) {   /* below ~-110 dB it is silence: skip it until the next strike */
                const float s = v->sw[k], q = y1 * v->cw[k] - v->rr[k] * y2;
                if (y1 * y1 * s * s + q * q < 1e-11f * s * s) {
                    v->live &= (uint16_t)~(1u << k);
                    v->y1[k] = v->y2[k] = 0.0f;
                }
            }
        }
        if (v->click > 1e-6f && v->ex_ph >= 0.0f) {     /* the skin on the steel, as the fingertip leaves */
            float c = v->click, lp = v->click_lp, hp = v->click_hp;
            const float ca = v->click_a;
            for (j = 0; j < n; j++) {
                lp += ca * (c * rnds() - lp);
                hp += 0.043f * (lp - hp);              /* (~300 Hz) */
                vb[j] += (lp - hp) * 2.0f;
                c *= v->click_dec;
            }
            v->click = c;
            v->click_lp = fm_flush(lp);
            v->click_hp = fm_flush(hp);
        }
        if (i >= KM_NVOICE) {                           /* a stolen voice: out in FADE_S, smoothly */
            float g = v->fade;
            const float st = v->fade_step;
            for (j = 0; j < n; j++) {
                vb[j] *= g * g * (3.0f - 2.0f * g);     /* an S: flat where it starts and where it ends */
                g = g > st ? g - st : 0.0f;
            }
            v->fade = g;
            if (g <= 0.0f)
                v->on = 0;
        }
        {
            const float pl = v->pl, pr = v->pr;
            for (j = 0; j < n; j++) {
                float o = vb[j], a = fm_fabsf(o);
                pk = a > pk ? a : pk;
                mixl[j] += o * pl;
                mixr[j] += o * pr;
                mono[j] += o;
            }
        }
        if (i >= KM_NVOICE)
            continue;
        if (v->contact && !--v->contact) {              /* the fingertip lifts: free again */
            if (v->damp == CONTACT_D)
                v->damp = 1.0f;
            v->glide_rise = v->glide_pend * 0.25f;
            v->glide_pend = 0.0f;
            voice_coefs(v);
        }
        if (v->glide_rise > 0.0f) {
            v->glide += v->glide_rise;
            if (++v->rise_n >= 4u)
                v->glide_rise = 0.0f, v->rise_n = 0;
            voice_freq(v);
        } else if (v->glide != 0.0f && v->ex_ph >= 0.0f) {   /* it settles: ~45 ms */
            v->glide = v->glide > 0.05f ? v->glide * GLIDE_K : 0.0f;
            voice_freq(v);
        }
        v->level = pk > v->level ? pk : v->level * 0.9f + pk * 0.1f;
        km_voice_level[i] = v->level;
        if (pk < 2e-5f && !excite) {
            if (++v->quiet > 24u) {                     /* ~35 ms of nothing: the field has stopped */
                v->on = 0;
                km_voice_level[i] = 0.0f;
                km_voice_cents[i] = -1;
            }
        } else {
            v->quiet = 0;
        }
    }
}

/* the shell and the air: driven by the strikes' force (knock) and the hits' pulses; the skin's bursts */
static void render_shell(uint32_t n)
{
    uint32_t j;
    int k;
    float pkl = 0.0f, pkr = 0.0f, pkc = 0.0f, kin = 0.0f;
    const float mute = sh_mute > 0.0f ? 0.9965f : 1.0f;
    for (j = 0; j < n; j++)
        kin += fm_fabsf(knock[j]);
    if (sh_idle > 64u && kin == 0.0f && tail_ph >= 0.5f && nb[0].amp <= 1e-6f && nb[1].amp <= 1e-6f) {
        km_shell_level[0] = km_shell_level[1] = km_cavity_level = shp_l = shp_r = cavp = 0.0f;
        return;                                         /* silent, nothing coming: (the device's CPU) */
    }
    for (j = 0; j < n; j++) {
        float el = knock[j] * 0.005f, er = el, ec = knock[j] * knock_amt * 0.5f, sl = 0.0f, sr = 0.0f, c;
        if (tail_ph < 0.5f) {                           /* a hit's pulse */
            float s = fm_sin_turns(tail_ph);
            el += tail_l * s;
            er += tail_r * s;
            ec += tail_c * s;
            tail_ph += tail_inc;
        }
        for (k = 0; k < NSHELL; k++) {
            shmode_t *m = &sh[k];
            float yl = m->b1 * m->y1l + m->b2 * m->y2l + m->gl * el, yr = m->b1 * m->y1r + m->b2 * m->y2r + m->gr * er;
            m->y2l = m->y1l * mute;
            m->y1l = yl * mute;
            m->y2r = m->y1r * mute;
            m->y1r = yr * mute;
            sl += yl;
            sr += yr;
        }
        c = cav_b1 * cav_y1 + cav_b2 * cav_y2 + cav_g * ec;
        cav_y2 = cav_y1 * mute;
        cav_y1 = c * mute;
        if (nb[0].amp > 1e-6f)
            sl += burst_run(&nb[0]);
        if (nb[1].amp > 1e-6f)
            sr += burst_run(&nb[1]);
        c *= 1.4f;
        mixl[j] += sl * 0.9f + sr * 0.15f + c;           /* the shoulder you slap is the side you hear */
        mixr[j] += sr * 0.9f + sl * 0.15f + c;
        mono[j] += (sl + sr) * 0.5f + c;
        pkl = fm_maxf(pkl, fm_fabsf(sl));
        pkr = fm_maxf(pkr, fm_fabsf(sr));
        pkc = fm_maxf(pkc, fm_fabsf(c));
    }
    if (sh_mute > 0.0f)
        sh_mute -= (float)n;
    for (k = 0; k < NSHELL; k++) {
        sh[k].y1l = fm_flush(sh[k].y1l);
        sh[k].y2l = fm_flush(sh[k].y2l);
        sh[k].y1r = fm_flush(sh[k].y1r);
        sh[k].y2r = fm_flush(sh[k].y2r);
    }
    cav_y1 = fm_flush(cav_y1);
    cav_y2 = fm_flush(cav_y2);
    nb[0].s1 = fm_flush(nb[0].s1);
    nb[0].s2 = fm_flush(nb[0].s2);
    nb[1].s1 = fm_flush(nb[1].s1);
    nb[1].s2 = fm_flush(nb[1].s2);
    if (pkl + pkr + pkc < 1e-6f && kin == 0.0f) {      /* below hearing: after ~90 ms of it, stop */
        if (++sh_idle > 64u)
            shell_quiet();
    } else {
        sh_idle = 0;
    }
    shp_l = pkl > shp_l ? pkl : shp_l * 0.92f;
    shp_r = pkr > shp_r ? pkr : shp_r * 0.92f;
    cavp = pkc > cavp ? pkc : cavp * 0.92f;
    km_shell_level[0] = shp_l;
    km_shell_level[1] = shp_r;
    km_cavity_level = cavp;
}

/* ------------------------------------------------------------------ air --- */
/* The halo a real pan leaves after each note: the shell and the other fields ringing the harmonics of
 * what's played, the air inside carrying it. A comb resonator per note of the pan (the ding and its
 * fields, NAIR), each tuned to that note: fed the pan's sound, it rings that note's whole harmonic series
 * (1, 2, 3, 4 ... its partials, which a handpan's are), darkening as it goes. Only what's in the scale
 * resonates, so the halo is always in tune. Then two short allpasses a side diffuse it into the air,
 * the combs spread left and right as the fields sit. Sympathy sets how much. */
#define NAIR 10
#define AIR_N 832u                                      /* the longest comb: 55 Hz (A2, an octave down) */
static float air_buf[NAIR][AIR_N];
static uint32_t air_w;
static float air_d[NAIR], air_g[NAIR], air_in[NAIR], air_lp[NAIR], air_pl[NAIR], air_pr[NAIR];
static int air_n;
static uint32_t air_quiet;
static float air_out = 1.0f;                            /* the halo's level: a mute fades it, then it starts clean */
static float air_ap[4][160];                            /* the diffusers: lengths 113, 157 left; 127, 149 right */
static int air_api[4];
static const int AIR_APN[4] = {113, 157, 127, 149};

static void air_coefs(void)
{
    int c;
    air_n = pan_n < NAIR ? pan_n : NAIR;
    for (c = 0; c < air_n; c++) {
        float f = cents_hz(pan_c[c]), d, t60 = 1.2f + 2.2f * sym_amt, p;
        while (KM_SR / f > (float)(AIR_N - 4u))
            f *= 2.0f;
        while (KM_SR / f < 8.0f)
            f *= 0.5f;
        d = KM_SR / f - 0.5f;                           /* (the loop's low-pass delays it half a sample) */
        air_d[c] = d;
        air_g[c] = fm_exp2f(-9.9657843f * d / (t60 * KM_SR));   /* 10^(-3 d / (t60 sr)): -60 dB in t60 */
        air_in[c] = 1.0f - air_g[c];
        p = c ? ((c & 1) ? -0.55f : 0.55f) * width : 0.0f;
        air_pl[c] = fm_sqrtf(0.5f - 0.5f * p);
        air_pr[c] = fm_sqrtf(0.5f + 0.5f * p);
    }
    air_dirty = 0;
}

static inline float air_allpass(int k, float x)
{
    float *b = air_ap[k], z = b[air_api[k]], v = x + 0.6f * z;
    b[air_api[k]] = fm_flush(v);
    if (++air_api[k] >= AIR_APN[k])
        air_api[k] = 0;
    return z - 0.6f * v;
}

static void render_air(uint32_t n)
{
    uint32_t j;
    int c;
    float pk = 0.0f, lvl = sym_amt * 1.4f;
    if (air_dirty)
        air_coefs();
    if (!air_n || lvl <= 0.0f) {
        air_quiet = 0xFFFFu;
        air_mute = 0.0f;
        air_out = 1.0f;
        return;
    }
    {
        float e = 0.0f;
        for (j = 0; j < n; j++)
            e += fm_fabsf(mono[j]);
        if (e > 1e-6f)
            air_quiet = 0;
        else if (air_quiet > 1400u) {                   /* (~2 s of nothing in, the halo long gone: idle) */
            air_mute = 0.0f;
            air_out = 1.0f;
            return;
        }
    }
    for (j = 0; j < n; j++) {
        const float x = mono[j];
        float l = 0.0f, r = 0.0f;
        for (c = 0; c < air_n; c++) {
            float *b = air_buf[c], back = air_d[c], f, rd, y;
            uint32_t ib = (uint32_t)back, i0 = (air_w + AIR_N - ib) % AIR_N, i1 = (i0 + AIR_N - 1u) % AIR_N;
            f = back - (float)ib;
            rd = b[i0] + (b[i1] - b[i0]) * f;
            air_lp[c] += 0.5f * (rd - air_lp[c]);       /* each trip round, the top dims: the halo darkens */
            y = x * air_in[c] + air_g[c] * air_lp[c];
            b[air_w] = fm_flush(y);
            l += y * air_pl[c];
            r += y * air_pr[c];
        }
        air_w = air_w + 1u >= AIR_N ? 0u : air_w + 1u;
        if (air_mute > 0.0f)
            air_out *= 0.9995f;                         /* (-48 dB over the quarter second of a mute) */
        l = air_allpass(1, air_allpass(0, l)) * lvl * air_out;
        r = air_allpass(3, air_allpass(2, r)) * lvl * air_out;
        mixl[j] += l;
        mixr[j] += r;
        pk = fm_maxf(pk, fm_fabsf(l) + fm_fabsf(r));
    }
    for (c = 0; c < air_n; c++)
        air_lp[c] = fm_flush(air_lp[c]);
    if (air_mute > 0.0f) {
        air_mute -= (float)n;
        if (air_mute <= 0.0f) {                         /* gone: empty, and open for what comes next */
            air_quiet_all();
            air_out = 1.0f;
        }
    }
    if (pk < 1e-6f)
        air_quiet++;
}

static void air_quiet_all(void)
{
    int c;
    uint32_t i;
    for (c = 0; c < NAIR; c++) {
        for (i = 0; i < AIR_N; i++)
            air_buf[c][i] = 0.0f;
        air_lp[c] = 0.0f;
    }
    for (c = 0; c < 4; c++)
        for (i = 0; i < 160u; i++)
            air_ap[c][i] = 0.0f;
    air_quiet = 0xFFFFu;
}

static void render_chunk(int32_t *out, uint32_t n, float gain)
{
    uint32_t j;
    int i, active = 0;
    for (j = 0; j < n; j++)
        mixl[j] = mixr[j] = mono[j] = knock[j] = 0.0f;
    render_voices(n);
    render_shell(n);
    render_air(n);
    fx_chunk(n);
    for (j = 0; j < n; j++)                             /* the chorus, on the instrument */
        fx_chorus(&mixl[j], &mixr[j]);
    for (i = 0; i < NGRAIN; i++)
        active += gr[i].on;
    km_grains_on = (uint8_t)active;
    if (g_mix > 0.0f) {                                 /* time for a grain? */
        g_timer -= (float)n;
        if (g_timer <= 0.0f) {
            float iv = KM_SR / (float)par[P_GDENS];
            grain_spawn();
            g_timer += iv * (1.0f + 0.8f * (float)par[P_GSPRAY] * 0.01f * rnds());
            if (g_timer < 64.0f)
                g_timer = 64.0f;
        }
    }
    for (j = 0; j < n; j++) {
        float l = mixl[j], r = mixr[j], gl = 0.0f, grr = 0.0f, wl, wr, dlo, dro, fr;
        /* the grain buffer: the dry sound at half rate */
        if (!km_frozen) {
            if (now_s & 1u) {
                grb_buf[gw] = to16((g_acc + l + r) * 0.35f);
                if (++gw >= GRB_N)
                    gw = 0;
            } else {
                g_acc = l + r;
            }
        }
        if (active) {
            for (i = 0; i < NGRAIN; i++) {
                grain_t *g = &gr[i];
                int wi;
                float s, wf;
                if (!g->on)
                    continue;
                wi = (int)g->wph;
                wf = g->wph - (float)wi;
                s = grb_read(g->pos) * (win[wi] + (win[wi + 1] - win[wi]) * wf) * g->amp;
                gl += s * g->pl;
                grr += s * g->pr;
                g->pos += g->rate;
                if (g->pos >= (float)GRB_N)
                    g->pos -= (float)GRB_N;
                else if (g->pos < 0.0f)
                    g->pos += (float)GRB_N;
                g->wph += g->winc;
                if (g->wph >= 256.0f)
                    g->on = 0;
            }
            gl *= g_mix;
            grr *= g_mix;
        }
        /* the delay: in at half rate, out interpolated between its steps */
        if (now_s & 1u) {
            delay_tick((d_in + (l + r) * 0.5f + (gl + grr) * 0.5f) * 0.5f);
            fr = 0.0f;
        } else {
            d_in = (l + r) * 0.5f + (gl + grr) * 0.5f;
            fr = 0.5f;
        }
        dlo = (d_prev_l + (d_cur_l - d_prev_l) * fr) * d_mix;
        dro = (d_prev_r + (d_cur_r - d_prev_r) * fr) * d_mix;
        if (rsend > 0.0f)
            pl_quiet = 0;
        if (pl_quiet < PL_IDLE) {
            reverb(((l + r) * 0.5f + (gl + grr) * 0.8f + (dlo + dro) * 0.5f) * rsend * 0.6f, &wl, &wr);
            pl_quiet = rsend <= 0.0f && fm_fabsf(wl) + fm_fabsf(wr) < 1e-6f ? pl_quiet + 1u : 0u;
        } else {
            wl = wr = 0.0f;
        }
        l = l + gl + dlo * (0.5f + 0.5f * width) + dro * (0.5f - 0.5f * width) + wl;
        r = r + grr + dro * (0.5f + 0.5f * width) + dlo * (0.5f - 0.5f * width) + wr;
        fx_out(&l, &r);                                 /* the filter and the tape, on everything */
        l *= 0.55f * gain;
        r *= 0.55f * gain;
        out[2 * j] = (int32_t)(fm_tanhf(l) * 8300000.0f);
        out[2 * j + 1] = (int32_t)(fm_tanhf(r) * 8300000.0f);
        now_s++;
    }
}

void km_render(int32_t *out, uint32_t n, uint32_t gain_q12)
{
    const float gain = (float)gain_q12 * (1.0f / 4096.0f) * 2.0f;
    int32_t *const out0 = out, pk = 0;
    const uint32_t n0 = n;
    uint32_t j;
    drain();
    if (km_ext && ++ext_age > 86u)                      /* ~0.5 s without a tick: back to its own tempo */
        km_ext = 0;
    while (n) {
        uint32_t k = n > BLK ? BLK : n;
        int i;
        pat_chunk(k);
        for (i = 0; i < NPEND; i++)                     /* what's due: delayed strikes and hits, sympathy */
            if (pend[i].on && (int32_t)(pend[i].due - now_s) <= 0) {
                pend[i].on = 0;
                switch (pend[i].type) {
                case PD_STRIKE: pluck(pend[i].cents, pend[i].vel, pend[i].pan); break;
                case PD_HIT: hit(pend[i].cents, pend[i].vel); break;
                case PD_SYMP: symp_ring(pend[i].cents, pend[i].vel, pend[i].emph); break;
                default: {                              /* the slap's palm settles on the ding */
                    voice_t *v = voice_ringing(pend[i].cents);
                    if (v)
                        voice_damp_to(v, SLAP_D);
                    break;
                }
                }
            }
        render_chunk(out, k, gain);
        out += 2 * k;
        n -= k;
    }
    for (j = 0; j < 2u * n0; j++) {                     /* how loud it is, tails and all (UI: autosave) */
        int32_t a = out0[j] < 0 ? -out0[j] : out0[j];
        pk = a > pk ? a : pk;
    }
    km_out_level = (float)pk * (1.0f / 8388608.0f);
}
