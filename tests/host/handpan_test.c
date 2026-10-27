/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument on the host (no libm: the device has none, so neither does this test):
 *   the music: the scales as the makers build them, the three layouts, the dings' register, the
 *              second pans that go with each pan
 *   the fields: every metal in tune with its octave and fifth, the decays, damping, one voice per
 *              field, voice stealing, delayed strikes, sympathy
 *   the steel: every hit, its side, the Gu's pitch with the size, the mute, the slap, the harmonic
 *   the FX: delay, grains, color, no clicks, the grooves and MIDI clock, a 20-minute run
 * and one WAV per metal in build/host/materials/, for listening.
 *   handpan_test [OUTDIR] */
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "handpan.h"
#include "fastmath.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static int32_t blk[2 * 256];
#define MAXS (44100 * 4)
static float bufl[MAXS], bufr[MAXS];

/* render n frames into bufl / bufr from offset at; peak and NaN check */
static float peak;
static int nan_seen;
static uint32_t master = 4096 / 2;                /* the MASTER pot, Q12 */
static void render(uint32_t at, uint32_t n)
{
    uint32_t k;
    while (n) {
        uint32_t m = n > 256 ? 256 : n;
        km_render(blk, m, master);
        for (k = 0; k < m; k++) {
            float l = (float)blk[2 * k] / 8388608.0f, r = (float)blk[2 * k + 1] / 8388608.0f;
            if (l != l || r != r)
                nan_seen = 1;
            if (fm_fabsf(l) > peak)
                peak = fm_fabsf(l);
            if (fm_fabsf(r) > peak)
                peak = fm_fabsf(r);
            if (at + k < MAXS) {
                bufl[at + k] = l;
                bufr[at + k] = r;
            }
        }
        at += m;
        n -= m;
    }
}

static void fresh(void)
{
    km_init();
    km_set(P_REVERB, 0);
    km_set(P_SYMPATHY, 0);                       /* (each note on its own, unless a test wants the pan) */
    peak = 0.0f;
    nan_seen = 0;
}

/* the DTFT magnitude of bufl[a..a+n) at f Hz, Hann window */
static float mag(uint32_t a, uint32_t n, float f)
{
    float re = 0.0f, im = 0.0f, w = FM_TWO_PI * f / 44100.0f;
    uint32_t i;
    for (i = 0; i < n; i++) {
        float h = 0.5f - 0.5f * fm_cosf(FM_TWO_PI * (float)i / (float)n), x = bufl[a + i] * h;
        re += x * fm_cosf(w * (float)i);
        im -= x * fm_sinf(w * (float)i);
    }
    return fm_sqrtf(re * re + im * im);
}

/* the strongest frequency within +-60 cents of f, to ~0.3 cent */
static float find_pitch(uint32_t a, uint32_t n, float f)
{
    float best = 0.0f, bf = f, c;
    for (c = -60.0f; c <= 60.0f; c += 1.0f) {
        float ff = f * fm_exp2f(c / 1200.0f), m = mag(a, n, ff);
        if (m > best) {
            best = m;
            bf = ff;
        }
    }
    f = bf;
    for (c = -1.0f; c <= 1.0f; c += 0.1f) {
        float ff = f * fm_exp2f(c / 1200.0f), m = mag(a, n, ff);
        if (m > best) {
            best = m;
            bf = ff;
        }
    }
    return bf;
}

static float cents_off(float got, float want) { return 1200.0f * fm_log2f(got / want); }

static float rms(const float *b, uint32_t a, uint32_t n)
{
    float s = 0.0f;
    uint32_t i;
    for (i = 0; i < n; i++)
        s += b[a + i] * b[a + i];
    return fm_sqrtf(s / (float)n);
}

static void wav_write(const char *path, uint32_t n)
{
    FILE *f = fopen(path, "wb");
    uint32_t i, v;
    uint16_t h16;
    if (!f)
        return;
    fwrite("RIFF", 1, 4, f);
    v = 36 + n * 4;
    fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    v = 16;
    fwrite(&v, 4, 1, f);
    h16 = 1;
    fwrite(&h16, 2, 1, f);
    h16 = 2;
    fwrite(&h16, 2, 1, f);
    v = 44100;
    fwrite(&v, 4, 1, f);
    v = 44100 * 4;
    fwrite(&v, 4, 1, f);
    h16 = 4;
    fwrite(&h16, 2, 1, f);
    h16 = 16;
    fwrite(&h16, 2, 1, f);
    fwrite("data", 1, 4, f);
    v = n * 4;
    fwrite(&v, 4, 1, f);
    for (i = 0; i < n; i++) {
        int16_t s[2];
        s[0] = (int16_t)(fm_clampf(bufl[i], -1.0f, 1.0f) * 32767.0f);
        s[1] = (int16_t)(fm_clampf(bufr[i], -1.0f, 1.0f) * 32767.0f);
        fwrite(s, 2, 2, f);
    }
    fclose(f);
}

/* the sharpest edge (fourth difference) in bufl[a, a+n) */
static float edge_max(uint32_t a, uint32_t n)
{
    uint32_t i;
    float m = 0.0f;
    for (i = a + 4; i < a + n; i++)
        m = fm_maxf(m, fm_fabsf(bufl[i] - 4.0f * bufl[i - 1] + 6.0f * bufl[i - 2] - 4.0f * bufl[i - 3] + bufl[i - 4]));
    return m;
}

static float hz(int cents) { return 8.17579892f * fm_exp2f((float)cents / 1200.0f); }
static float db(float x) { return 20.0f * fm_log2f(x > 1e-12f ? x : 1e-12f) * 0.30103f; }

/* the pan's notes, the ding first, as the UI gives them */
static void give_pan(int scale, int key)
{
    int n[KM_NPAN];
    km_pan_notes(scale, key, 0, n);
    km_pan(n, KM_NPAN);
}

static uint16_t pcs_of(const int *c, int n)
{
    uint16_t m = 0;
    int i;
    for (i = 0; i < n; i++)
        m |= (uint16_t)(1u << (((c[i] / 100) % 12 + 12) % 12));
    return m;
}

static void test_music(void)
{
    int i, s, k, n[KM_NPAN], own;
    char b[24];
    /* D Kurd, as the makers build it: D3 / A3 Bb3 C4 D4 E4 F4 G4 A4, then the scale on up */
    static const int KURD[KM_NPAN] = {5000, 5700, 5800, 6000, 6200, 6400, 6500, 6700, 6900,
                                      7000, 7200, 7400, 7600, 7700, 7900, 8100};
    own = km_pan_notes(0, 2, 0, n);
    CHECK(own == 9, "Kurd has a ding and 8 fields (%d)", own);
    for (i = 0; i < KM_NPAN; i++)
        CHECK(n[i] == KURD[i], "D Kurd note %d: %d, wanted %d", i, n[i], KURD[i]);
    /* Pan: the ding on both middle keys, then the fields zigzag outward, left first */
    CHECK(km_white_cents(LAY_PAN, 0, 2, 0, 7) == 5000 && km_white_cents(LAY_PAN, 0, 2, 0, 8) == 5000, "ding in the middle");
    CHECK(km_white_cents(LAY_PAN, 0, 2, 0, 6) == 5700 && km_white_cents(LAY_PAN, 0, 2, 0, 9) == 5800, "A left, Bb right");
    CHECK(km_white_cents(LAY_PAN, 0, 2, 0, 5) == 6000 && km_white_cents(LAY_PAN, 0, 2, 0, 10) == 6200, "C left, D right");
    CHECK(km_white_cents(LAY_PAN, 0, 2, 0, 0) == 7700 && km_white_cents(LAY_PAN, 0, 2, 0, 15) == 7900, "the ends");
    for (i = 1; i < 7; i++) {                    /* each step out from the middle climbs */
        CHECK(km_white_cents(LAY_PAN, 0, 2, 0, 7 - i) > km_white_cents(LAY_PAN, 0, 2, 0, 8 - i), "left climbs %d", i);
        CHECK(km_white_cents(LAY_PAN, 0, 2, 0, 8 + i) > km_white_cents(LAY_PAN, 0, 2, 0, 7 + i), "right climbs %d", i);
    }
    for (i = 0; i < 16; i++) {
        CHECK(km_white_cents(LAY_MIRROR, 3, 6, 0, i) == km_white_cents(LAY_PAN, 3, 6, 0, 15 - i), "mirror %d", i);
        CHECK(km_white_cents(LAY_LINEAR, 0, 2, 0, i) == KURD[i], "linear %d", i);
    }
    CHECK(km_white_pos(LAY_PAN, 7) == 0.0f && km_white_pos(LAY_PAN, 2) < -0.5f && km_white_pos(LAY_PAN, 13) > 0.5f, "pan position");
    /* the ding between A2 and G#3, as the makers have it; the octave moves it */
    for (k = 0; k < 12; k++) {
        int d = km_ding_cents(k, 0);
        CHECK(d >= 4500 && d <= 5600 && d % 1200 == (k * 100) % 1200, "ding of key %d: %d", k, d);
    }
    CHECK(km_ding_cents(2, 1) == 6200 && km_ding_cents(2, -1) == 3800, "octave");
    CHECK(km_ding_cents(km_scale_root(3), 0) == 5400, "Pygmy comes in F#3");
    /* every scale: its fields climb, and what comes past them stays in the scale */
    for (s = 0; s < KM_NSCALE; s++) {
        own = km_pan_notes(s, km_scale_root(s), 0, n);
        CHECK(own == km_scale_tops(s) + 1 && own >= 8 && own <= 10, "%s has %d notes", km_scale_name(s), own);
        for (i = 1; i < KM_NPAN; i++)
            CHECK(n[i] > n[i - 1], "%s climbs at %d", km_scale_name(s), i);
        CHECK((pcs_of(n, KM_NPAN) & ~pcs_of(n, own)) == 0, "%s: the extension is in the scale", km_scale_name(s));
        CHECK(n[KM_NPAN - 1] - n[0] <= 4200, "%s spans %d cents", km_scale_name(s), n[KM_NPAN - 1] - n[0]);
    }
    /* the second pan: for every pan in every key, at least one; all of its notes in the first pan's (or,
     * for a pan of few notes, all but one); never the same pan; its ding a register above */
    for (s = 0; s < KM_NSCALE; s++)
        for (k = 0; k < 12; k++) {
            int cnt = km_duo_count(s, k), c;
            uint16_t main_pcs;
            km_pan_notes(s, k, 0, n);
            main_pcs = pcs_of(n, KM_NPAN);
            CHECK(cnt >= 1 && cnt <= KM_NDUOCHOICE, "%s in %s: %d second pans", km_scale_name(s), KM_NOTE_NAME[k], cnt);
            for (c = 0; c < cnt; c++) {
                int ds, dk, d[KM_NPAN], outside, bit, dn[KM_NDUO];
                km_duo_choice(s, k, c, &ds, &dk);
                CHECK(!(ds == s && dk == k), "the same pan offered");
                km_pan_notes(ds, dk, 0, d);
                for (outside = 0, bit = 0; bit < 12; bit++)
                    outside += (pcs_of(d, km_scale_tops(ds) + 1) >> bit & 1u) && !(main_pcs >> bit & 1u);
                CHECK(outside <= 1, "%s %s with %s %s: %d notes outside", KM_NOTE_NAME[k], km_scale_name(s),
                      KM_NOTE_NAME[dk], km_scale_name(ds), outside);
                km_duo_notes(s, k, 0, c, dn);
                CHECK(dn[0] >= km_ding_cents(k, 0) + 300 && dn[0] <= km_ding_cents(k, 0) + 1400, "duo ding %d", dn[0]);
                for (i = 1; i < KM_NDUO; i++)
                    CHECK(dn[i] > dn[i - 1], "duo climbs");
            }
        }
    CHECK(!strcmp(km_duo_name(0, 2, 0, b), "D Celtic"), "D Kurd's best second pan: %s", b);
    km_duo_choice(3, 6, 0, &s, &k);
    CHECK(k == 6, "F# Pygmy's best second pan shares its ding (%s %s)", KM_NOTE_NAME[k], km_scale_name(s));
    for (i = 0; i < P_NPARAMS; i++) {
        const km_param_t *p = km_param_info(i);
        km_param_text(i, p->hi, b);
        CHECK(p->lo <= p->def && p->def <= p->hi && b[0], "param %d", i);
    }
    km_param_text(P_THICK, 12, b);
    CHECK(!strcmp(b, "1.2mm"), "thickness text %s", b);
    km_param_text(P_SIZE, 53, b);
    CHECK(!strcmp(b, "53cm"), "size text %s", b);
}

/* each metal: the fundamental in tune, and the tuner's octave and compound fifth there, at 2 and 3
 * times it, with nothing much between (at 2.5) */
static void test_partials(void)
{
    static const int NOTES[3] = {5000, 6200, 7400};
    int m, i;
    for (m = 0; m < MET_N; m++)
        for (i = 0; i < 3; i++) {
            float f = hz(NOTES[i]), off, a1, a2, a3, a25;
            fresh();
            km_set(P_METAL, m);
            km_set(P_PORT, 0);
            km_pluck(NOTES[i], 100, 0, 0);
            render(0, 16384);
            off = cents_off(find_pitch(4096, 8192, f), f);
            CHECK(off > -3.0f && off < 3.0f, "%s %d: %.1f cents off", km_metal(m)->name, NOTES[i], (double)off);
            a1 = mag(4096, 8192, f);
            a2 = mag(4096, 8192, 2.0f * f);
            a3 = mag(4096, 8192, 3.0f * f);
            a25 = mag(4096, 8192, 2.5f * f);
            CHECK(db(a2 / a1) > -18.0f && db(a3 / a1) > -26.0f, "%s %d: octave %.1f dB, fifth %.1f dB", km_metal(m)->name,
                  NOTES[i], (double)db(a2 / a1), (double)db(a3 / a1));
            CHECK(db(a25 / a1) < -30.0f, "%s %d: between the partials %.1f dB", km_metal(m)->name, NOTES[i],
                  (double)db(a25 / a1));
            CHECK(!nan_seen && peak < 0.9f && peak > 0.02f, "%s level %.3f", km_metal(m)->name, (double)peak);
        }
    {   /* Harmonics: the octave and fifth with it */
        float lo, hi, f = hz(5000);
        fresh();
        km_set(P_HARMONICS, 10);
        km_pluck(5000, 100, 0, 0);
        render(0, 16384);
        lo = mag(4096, 8192, 2.0f * f) / mag(4096, 8192, f);
        fresh();
        km_set(P_HARMONICS, 100);
        km_pluck(5000, 100, 0, 0);
        render(0, 16384);
        hi = mag(4096, 8192, 2.0f * f) / mag(4096, 8192, f);
        CHECK(db(hi / lo) > 12.0f, "Harmonics 10 -> 100: the octave %.1f dB up", (double)db(hi / lo));
    }
}

static float ring_at(int metal, int sustain, int cents, float sec)
{
    fresh();
    km_set(P_METAL, metal);
    km_set(P_SUSTAIN, sustain);
    km_pluck(cents, 100, 0, 0);
    render(0, (uint32_t)(sec * 44100.0f) + 4410);
    return rms(bufl, (uint32_t)(sec * 44100.0f), 4410);
}

static void test_decay(void)
{
    float st = ring_at(MET_STAINLESS, 50, 5000, 3.0f), raw = ring_at(MET_RAW, 50, 5000, 3.0f),
          lo = ring_at(MET_NITRIDED, 0, 5000, 1.5f), hi = ring_at(MET_NITRIDED, 100, 5000, 1.5f),
          ding = ring_at(MET_NITRIDED, 50, 5000, 3.0f), top = ring_at(MET_NITRIDED, 50, 6900, 3.0f), start;
    CHECK(st > 2.0f * raw, "Stainless rings longer than Raw (%.5f %.5f)", (double)st, (double)raw);
    CHECK(hi > 4.0f * lo, "Sustain (%.5f %.5f)", (double)lo, (double)hi);
    CHECK(ding > top, "the ding outlasts a high field (%.5f %.5f)", (double)ding, (double)top);
    /* a handpan rings: the ding still well up three seconds on */
    fresh();
    km_pluck(5000, 100, 0, 0);
    render(0, 44100 * 3 + 4410);
    start = rms(bufl, 2205, 4410);
    CHECK(db(ding / start) > -30.0f, "the ding at 3 s: %.1f dB", (double)db(ding / start));
    {   /* a bigger, thicker pan rings longer */
        float small, big;
        fresh();
        km_set(P_SIZE, 45);
        km_set(P_THICK, 10);
        km_pluck(5000, 100, 0, 0);
        render(0, 44100 * 2 + 4410);
        small = rms(bufl, 44100 * 2, 4410);
        fresh();
        km_set(P_SIZE, 60);
        km_set(P_THICK, 14);
        km_pluck(5000, 100, 0, 0);
        render(0, 44100 * 2 + 4410);
        big = rms(bufl, 44100 * 2, 4410);
        CHECK(big > 1.5f * small, "size and thickness: %.5f vs %.5f", (double)big, (double)small);
    }
}

static void test_damp(void)
{
    float ring, damped;
    fresh();
    km_pluck(6000, 100, 0, 0);
    render(0, 44100);
    ring = rms(bufl, 30000, 4410);
    fresh();
    km_pluck(6000, 100, 0, 0);
    render(0, 4410);
    km_damp(6000);
    render(4410, 44100 - 4410);
    damped = rms(bufl, 30000, 4410);
    CHECK(damped < ring * 0.05f, "damp (%.5f vs %.5f)", (double)damped, (double)ring);
    fresh();                                     /* the pedal holds a damp back */
    km_sustain(1);
    km_pluck(6000, 100, 0, 0);
    render(0, 4410);
    km_damp(6000);
    render(4410, 44100 - 4410);
    CHECK(rms(bufl, 30000, 4410) > ring * 0.8f, "sustain kept it ringing");
    km_sustain(0);
    render(0, 44100);
    CHECK(rms(bufl, 30000, 4410) < ring * 0.05f, "pedal up damps");
    fresh();
    km_pluck(6000, 100, 0, 0);
    km_pluck(6700, 100, 0, 0);
    render(0, 4410);
    km_damp_all();
    render(0, 44100);
    CHECK(rms(bufl, 30000, 4410) < ring * 0.05f, "damp all");
}

static void test_voices(void)
{
    int i, n = 0;
    fresh();
    km_pluck(6000, 100, 0, 0);
    render(0, 1000);
    km_pluck(6000, 100, 0, 0);                   /* the same field: the same voice */
    render(0, 1000);
    for (i = 0; i < KM_NVOICE; i++)
        n += km_voice_cents[i] == 6000;
    CHECK(n == 1, "one voice per field (%d)", n);
    for (i = 0; i < 30; i++) {                   /* more notes than voices: no crash, all bounded */
        km_pluck(4800 + 100 * i, 127, (i & 1) ? 100 : -100, 0);
        render(0, 300);
    }
    render(0, 44100);
    CHECK(!nan_seen && peak < 1.0f, "30 strikes: peak %.3f", (double)peak);
    for (i = 0; i < KM_NVOICE; i++)
        CHECK(km_voice_level[i] == km_voice_level[i], "level not NaN");
}

static void test_delayed(void)
{
    uint32_t i, first = 0;
    fresh();
    km_pluck(6000, 100, 0, 0);
    km_pluck(6400, 100, 0, 50);                  /* 50 ms later */
    for (i = 0; i < 40; i++) {
        int k, on = 0;
        render(0, 64);
        for (k = 0; k < KM_NVOICE; k++)
            on += km_voice_cents[k] == 6400;
        if (on && !first)
            first = (i + 1) * 64;
    }
    CHECK(first >= 2205 && first <= 2205 + 128, "the second strike at %u samples (2205 wanted)", first);
    fresh();
    km_pluck(6000, 100, 0, 100);
    km_damp(6000);                               /* let go before it sounds: it never does */
    render(0, 8820);
    CHECK(peak < 1e-4f, "a cancelled strike sounded (%.5f)", (double)peak);
}

/* Sympathy: D4 struck on a D Kurd wakes the D3 ding (its octave) and the A3 field (its compound fifth
 * meets A3's octave? no: D4's fifth partial is A5, A3's fifth is E5, A3's octave A4... D4 x 3 = A5 =
 * A4 x 2 = A3 x 4: A4 rings along). Off at 0. A free voice only. */
static void test_sympathy(void)
{
    int on, v, seen_ding = 0, seen_a4 = 0;
    for (on = 0; on < 2; on++) {
        int k;
        fresh();
        km_set(P_SYMPATHY, on ? 100 : 0);
        give_pan(0, 2);
        render(0, 256);
        km_pluck(6200, 110, 0, 0);
        for (k = 0; k < 40; k++) {
            render(0, 256);
            for (v = 0; v < KM_NVOICE; v++) {
                if (km_voice_cents[v] == 5000)
                    seen_ding |= 1 << on;
                if (km_voice_cents[v] == 6900)
                    seen_a4 |= 1 << on;
            }
        }
    }
    CHECK(seen_ding == 2, "D4 wakes the ding with Sympathy, not without (%d)", seen_ding);
    CHECK(seen_a4 == 2, "D4 wakes A4 (its fifth partial) with Sympathy, not without (%d)", seen_a4);
    {   /* it rings softly: well under the note that woke it */
        float l = 0.0f, d = 0.0f;
        for (v = 0; v < KM_NVOICE; v++) {
            if (km_voice_cents[v] == 5000)
                d = km_voice_level[v];
            if (km_voice_cents[v] == 6200)
                l = km_voice_level[v];
        }
        CHECK(d > 0.0f && d < 0.4f * l, "the ding along: %.4f, the note %.4f", (double)d, (double)l);
    }
    {   /* a full pan: sympathy never takes a sounding note's voice */
        int i, n = 0;
        fresh();
        km_set(P_SYMPATHY, 100);
        give_pan(0, 2);
        for (i = 0; i < KM_NVOICE; i++)
            km_pluck(3000 + 100 * i, 100, 0, 0);
        render(0, 512);
        km_pluck(6200, 110, 0, 0);
        render(0, 8820);
        for (i = 0; i < KM_NVOICE; i++)
            n += km_voice_cents[i] >= 3000 && km_voice_cents[i] < 3000 + 100 * KM_NVOICE;
        CHECK(n == KM_NVOICE - 1, "sympathy kept off sounding voices (%d of the 16 left)", n);
    }
}

/* the steel: every hit sounds and stays bounded; a thwap lands on its side; the Gu is low; the mute
 * damps; the slap is short; the harmonic is the ding's octave */
static float lowness(uint32_t a, uint32_t n)       /* the energy below ~160 Hz against all of it, dB */
{
    uint32_t i;
    float lp = 0.0f, lp2 = 0.0f, e = 0.0f, lo = 0.0f;
    for (i = a; i < a + n; i++) {
        lp += 0.022f * (bufl[i] - lp);
        lp2 += 0.022f * (lp - lp2);
        e += bufl[i] * bufl[i];
        lo += lp2 * lp2;
    }
    return db(fm_sqrtf(lo / e));
}

static float bright(const float *b, uint32_t n)    /* the rms above ~700 Hz (three poles: the Gu's boom out) */
{
    uint32_t i;
    float h1 = 0.0f, h2 = 0.0f, h3 = 0.0f, p0 = 0.0f, p1 = 0.0f, p2 = 0.0f, e = 0.0f;
    for (i = 0; i < n; i++) {                    /* one-pole high-passes, a ~ exp(-2 pi 700 / sr) */
        float a = 0.905f, x = b[i];
        h1 = a * (h1 + x - p0);
        p0 = x;
        h2 = a * (h2 + h1 - p1);
        p1 = h1;
        h3 = a * (h3 + h2 - p2);
        p2 = h2;
        e += h3 * h3;
    }
    return fm_sqrtf(e / (float)n);
}

static void test_hits(void)
{
    int k;
    for (k = 0; k < HIT_N; k++) {
        fresh();
        give_pan(0, 2);
        render(0, 256);
        km_hit(k, 110, 0);
        render(0, 22050);
        if (k != HIT_MUTE)
            CHECK(peak > 0.01f, "hit %d sounds: %.4f", k, (double)peak);
        CHECK(!nan_seen && peak < 0.95f, "hit %d peak %.3f", k, (double)peak);
    }
    {   /* sides */
        float l, r;
        fresh();
        give_pan(0, 2);
        render(0, 256);
        km_hit(HIT_THWAP_L, 110, 0);
        render(0, 8820);
        l = bright(bufl, 8820);                  /* (the air inside is in the middle: the side is in the steel) */
        r = bright(bufr, 8820);
        CHECK(l > 1.4f * r, "a left thwap is left (%.4f %.4f)", (double)l, (double)r);
        CHECK(km_shell_level[0] > km_shell_level[1], "the left shoulder rings");
        fresh();
        give_pan(0, 2);
        render(0, 256);
        km_hit(HIT_TAK_R, 110, 0);
        render(0, 8820);
        CHECK(bright(bufr, 8820) > 1.4f * bright(bufl, 8820), "a right tak is right");
    }
    {   /* the Gu is the air: low; a tak is not */
        float gu, tak;
        fresh();
        km_set(P_SYMPATHY, 0);
        km_hit(HIT_GU, 110, 0);
        render(0, 11025);
        gu = lowness(0, 11025);
        CHECK(km_cavity_level > 0.0f || peak > 0.0f, "the cavity moved");
        fresh();
        km_hit(HIT_TAK, 110, 0);
        render(0, 11025);
        tak = lowness(0, 11025);
        CHECK(gu > -4.0f && gu > tak + 6.0f, "Gu low: %.1f dB below 160 Hz, tak %.1f", (double)gu, (double)tak);
    }
    {   /* a bigger pan's Gu is lower (more air behind the same port) */
        float small, big;
        fresh();
        km_set(P_SIZE, 44);
        km_hit(HIT_GU, 110, 0);
        render(0, 22050);
        small = find_pitch(1000, 16384, 110.0f);
        fresh();
        km_set(P_SIZE, 62);
        km_hit(HIT_GU, 110, 0);
        render(0, 22050);
        big = find_pitch(1000, 16384, 80.0f);
        CHECK(small > big * 1.2f, "Gu: %.1f Hz small, %.1f Hz big", (double)small, (double)big);
    }
    {   /* Mute: a ringing field and the shell, damped */
        float ring;
        fresh();
        give_pan(0, 2);
        km_pluck(6200, 110, 0, 0);
        render(0, 22050);
        ring = rms(bufl, 11025, 4410);
        km_hit(HIT_MUTE, 100, 0);
        render(0, 22050);
        CHECK(rms(bufl, 17640, 4410) < ring * 0.03f, "mute: %.5f vs %.5f", (double)rms(bufl, 17640, 4410), (double)ring);
    }
    {   /* the slap: the ding, closed at once */
        float open_, slap;
        fresh();
        give_pan(0, 2);
        km_pluck(5000, 110, 0, 0);
        render(0, 22050);
        open_ = rms(bufl, 13230, 4410);
        fresh();
        give_pan(0, 2);
        render(0, 256);
        km_hit(HIT_SLAP, 110, 0);
        render(0, 22050);
        slap = rms(bufl, 13230, 4410);
        CHECK(slap < open_ * 0.1f, "slap closed: %.5f vs open %.5f", (double)slap, (double)open_);
    }
    {   /* the harmonic: the ding's octave over its fundamental */
        float f = hz(5000), a1, a2;
        fresh();
        km_set(P_PORT, 0);
        give_pan(0, 2);
        render(0, 256);
        km_hit(HIT_HARM, 110, 0);
        render(0, 16384);
        a1 = mag(4096, 8192, f);
        a2 = mag(4096, 8192, 2.0f * f);
        CHECK(a2 > 4.0f * a1, "harmonic: octave %.1f dB over the fundamental", (double)db(a2 / a1));
    }
    {   /* Percussion 0: the steel is silent (the ding of the slap still rings) */
        fresh();
        km_set(P_PERC, 0);
        km_hit(HIT_THWAP_L, 120, 0);
        render(0, 8820);
        CHECK(peak < 1e-4f, "Percussion 0: thwap %.5f", (double)peak);
    }
    {   /* silent again: the shell stops (its cost on the device) */
        fresh();
        km_hit(HIT_THWAP_R, 120, 0);
        render(0, 44100 * 3);
        CHECK(km_shell_level[0] == 0.0f && km_shell_level[1] == 0.0f && km_cavity_level == 0.0f, "the shell idles");
    }
}

/* What a strike adds to what was already sounding: the session rendered twice, with and without it
 * (the engine is deterministic), the difference kept. Its sharpest edge, by the fourth difference: a pop
 * (a step, a corner) keeps it, a tone hardly does. (FiMba-1's test.) */
static float with_out[44100];
static float pluck_corner(int n_ring, int ring_from, int ring_vel, int cents, int vel)
{
    int pass, i;
    float m = 0.0f;
    for (pass = 0; pass < 2; pass++) {
        fresh();
        km_set(P_STRIKE, 0);
        km_set(P_SUSTAIN, 100);
        km_set(P_PORT, 0);
        for (i = 0; i < n_ring; i++)
            km_pluck(ring_from + 100 * i, ring_vel, 0, 0);
        render(0, 22050);
        if (pass)
            km_pluck(cents, vel, 0, 20);         /* 20 ms in: the moment it lands is inside the window */
        render(0, 8820);
        if (!pass)
            for (i = 0; i < 8820; i++)
                with_out[i] = bufl[i];
    }
    for (i = 4; i < 8820; i++) {
        float d[5];
        int k;
        for (k = 0; k < 5; k++)
            d[k] = bufl[i - k] - with_out[i - k];
        m = fm_maxf(m, fm_fabsf(d[0] - 4.0f * d[1] + 6.0f * d[2] - 4.0f * d[3] + d[4]));
    }
    return m;
}

static void test_no_pops(void)
{
    float alone, steal, again, fresh_note;
    master = 4096 / 16;
    alone = pluck_corner(0, 0, 0, 7400, 50);
    steal = pluck_corner(KM_NVOICE, 4300, 127, 7400, 50);
    again = pluck_corner(1, 5000, 127, 5000, 50);
    fresh_note = pluck_corner(1, 5000, 127, 5100, 50);
    CHECK(steal < alone * 3.0f, "a stolen voice pops: edge %.6f, the strike alone %.6f", (double)steal, (double)alone);
    CHECK(again < fresh_note * 3.0f + 3e-6f, "a re-struck field pops: edge %.6f, a fresh one %.6f", (double)again, (double)fresh_note);
    printf("  edges: strike alone %.6f, stealing %.6f | fresh field %.6f, struck again %.6f\n", (double)alone,
           (double)steal, (double)fresh_note, (double)again);
    master = 4096 / 2;
}

static void test_just(void)
{
    int i;
    CHECK(km_just_cents(6400, 6000) == 6386 && km_just_cents(6700, 6000) == 6702 && km_just_cents(7000, 6000) == 7018,
          "just: E %d G %d Bb %d", km_just_cents(6400, 6000), km_just_cents(6700, 6000), km_just_cents(7000, 6000));
    CHECK(km_just_cents(5900, 6000) == 5888 && km_just_cents(6000, 6000) == 6000 && km_just_cents(7200, 6000) == 7200,
          "just below and at the octave");
    for (i = 0; i < 12; i++)
        CHECK(fm_fabsf((float)(km_just_cents(6000 + 100 * i, 6000) - 6000 - 100 * i)) < 20.0f, "just within 20 cents %d", i);
}

/* Worn: each field its own quirks, the same every time; none at 0 */
static float worn_pitch(int worn, int cents)
{
    fresh();
    km_set(P_HARMONICS, 0);
    km_set(P_PORT, 0);
    km_set(P_WORN, worn);
    km_pluck(cents, 60, 0, 0);
    render(0, 16384);
    return cents_off(find_pitch(4096, 8192, hz(cents)), hz(cents));
}

static void test_worn(void)
{
    static const int T[6] = {5000, 5700, 5800, 6000, 6200, 6400};
    float off[6], lo = 99.0f, hi = -99.0f;
    int i;
    for (i = 0; i < 6; i++) {
        CHECK(fm_fabsf(worn_pitch(0, T[i])) < 2.0f, "Worn 0: %d in tune (%.1f)", T[i], (double)worn_pitch(0, T[i]));
        off[i] = worn_pitch(100, T[i]);
        lo = fm_minf(lo, off[i]);
        hi = fm_maxf(hi, off[i]);
        CHECK(fm_fabsf(off[i]) < 13.0f, "Worn 100: %d within 12 cents (%.1f)", T[i], (double)off[i]);
    }
    CHECK(hi - lo > 4.0f, "Worn 100: the fields differ (%.1f .. %.1f cents)", (double)lo, (double)hi);
    CHECK(fm_fabsf(worn_pitch(100, T[2]) - off[2]) < 0.3f, "Worn: the same field, the same quirk");
}

static void delay_check(void)
{
    float before, at;
    km_set(P_REVERB, 0);
    km_set(P_GRAIN, 0);
    km_set(P_DELAY, 100);
    km_set(P_TIME, 200);
    km_set(P_FEEDBACK, 50);
    km_set(P_METAL, MET_RAW);                    /* short: the echo stands out */
    km_set(P_SUSTAIN, 0);
    km_set(P_PORT, 0);
    render(0, 44100);                            /* Time glides to 200 ms */
    km_pluck(7200, 120, 0, 0);
    render(0, 44100);
    before = rms(bufl, 6000, 1000);
    at = rms(bufl, 8820, 1000);
    CHECK(at > 3.0f * before, "an echo at 200 ms (%.5f before, %.5f at)", (double)before, (double)at);
    at = rms(bufr, 17640, 1000);
    CHECK(at > 3.0f * rms(bufr, 13000, 1000), "the second echo, right, at 400 ms (%.5f)", (double)at);
}

static void test_delay(void)
{
    fresh();
    delay_check();
}

static void test_grains(void)
{
    float dry, wet;
    int i, on = 0;
    fresh();
    km_set(P_METAL, MET_RAW);
    km_set(P_SUSTAIN, 0);
    km_pluck(7200, 120, 0, 0);
    render(0, 44100 * 2);
    dry = rms(bufl, 60000, 20000);
    fresh();
    km_set(P_METAL, MET_RAW);
    km_set(P_SUSTAIN, 0);
    km_set(P_GRAIN, 100);
    km_set(P_GSPRAY, 80);
    km_pluck(7200, 120, 0, 0);
    render(0, 22050);
    km_freeze(1);                                /* the buffer keeps the strike */
    for (i = 0; i < 40; i++) {
        render(22050 + (uint32_t)i * 2205, 2205);
        on |= km_grains_on;
    }
    wet = rms(bufl, 60000, 20000);
    CHECK(on && wet > 5.0f * dry, "frozen grains keep sounding (%.5f vs dry %.5f)", (double)wet, (double)dry);
    CHECK(km_frozen == 1, "frozen");
    CHECK(!nan_seen && peak < 1.0f, "grains peak %.3f", (double)peak);
    for (i = 0; i < GP_N; i++) {
        km_set(P_GPITCH, i);
        render(0, 22050);
    }
    CHECK(!nan_seen && peak < 1.0f, "grain pitches peak %.3f", (double)peak);
}

/* hours on: the delay and grain rings must not slow down or lose precision. 20 minutes, dense grains,
 * echoes, the pan's sympathy and the steel */
static void test_long_run(void)
{
    uint32_t i, blocks = 20u * 60u * 44100u / 256u;
    clock_t t0 = 0, t_first = 0, t_last = 0;
    fresh();
    give_pan(0, 2);
    km_set(P_SYMPATHY, 60);
    km_set(P_GRAIN, 100);
    km_set(P_GDENS, 60);
    km_set(P_GSPRAY, 100);
    km_set(P_DELAY, 50);
    km_set(P_FEEDBACK, 60);
    for (i = 0; i < blocks; i++) {
        if (i % 200u == 0u)
            km_pluck(5000 + (int)(i / 200u % 24u) * 100, 100, 0, 0);
        if (i % 300u == 150u)
            km_hit((int)(i / 300u % HIT_N), 100, 0);
        if (i == 0u || i == blocks - 1723u)
            t0 = clock();
        km_render(blk, 256, 2048);
        if (i == 1722u)
            t_first = clock() - t0;
        if (i == blocks - 1u)
            t_last = clock() - t0;
    }
    CHECK(t_last < t_first * 3 / 2 + CLOCKS_PER_SEC / 50, "the last 10 s took %ld ticks, the first %ld", (long)t_last,
          (long)t_first);
    km_damp_all();
    km_set(P_GRAIN, 0);
    render(0, 44100 * 3);
    CHECK(!nan_seen, "NaN after 20 minutes");
    delay_check();                               /* the echo still lands where Time says, no km_init */
}

/* the effects on one ringing note: what each does, that none clicks as it comes in or goes */
static void test_color(void)
{
    float f = hz(6000), lo = 1e9f, hi = -1e9f;
    uint32_t i;
    fresh();
    km_set(P_TAPE, 100);
    km_set(P_SUSTAIN, 100);
    km_set(P_HARMONICS, 0);
    for (i = 0; i < 4; i++) {
        float c;
        km_pluck(6000, 110, 0, 0);
        render(0, 11025);
        c = cents_off(find_pitch(3000, 8000, f), f);
        lo = fm_minf(lo, c);
        hi = fm_maxf(hi, c);
    }
    CHECK(hi - lo > 3.0f, "tape wow: the pitch moved %.1f cents", (double)(hi - lo));
    CHECK(!nan_seen && peak < 1.0f, "tape peak %.3f", (double)peak);
    {   /* filter: the low-pass takes a high note down, the high-pass a low one */
        static const int SET[3] = {0, -80, 80}, NOTE[2] = {8400, 4800};
        float lv[3][2];
        int k, nn;
        master = 4096 / 16;
        for (k = 0; k < 3; k++)
            for (nn = 0; nn < 2; nn++) {
                fresh();
                km_set(P_HARMONICS, 0);
                km_set(P_PORT, 0);
                km_set(P_FILTER, SET[k]);
                render(0, 4410);
                km_pluck(NOTE[nn], 110, 0, 0);
                render(0, 22050);
                lv[k][nn] = db(rms(bufl, 0, 22050));
            }
        master = 4096 / 2;
        CHECK(lv[1][0] < lv[0][0] - 8.0f && fm_fabsf(lv[1][1] - lv[0][1]) < 3.0f,
              "low-pass -80: C7 %+.1f dB, C3 %+.1f dB", (double)(lv[1][0] - lv[0][0]), (double)(lv[1][1] - lv[0][1]));
        CHECK(lv[2][1] < lv[0][1] - 10.0f && fm_fabsf(lv[2][0] - lv[0][0]) < 3.0f,
              "high-pass +80: C3 %+.1f dB, C7 %+.1f dB", (double)(lv[2][1] - lv[0][1]), (double)(lv[2][0] - lv[0][0]));
    }
    {   /* chorus widens */
        double sl = 0, sr = 0, slr = 0;
        fresh();
        km_set(P_CHORUS, 100);
        km_set(P_WIDTH, 0);
        km_set(P_PORT, 0);
        km_pluck(6700, 110, 0, 0);
        render(0, 44100);
        for (i = 4410; i < 44100; i++) {
            sl += (double)bufl[i] * (double)bufl[i];
            sr += (double)bufr[i] * (double)bufr[i];
            slr += (double)bufl[i] * (double)bufr[i];
        }
        CHECK(slr / (sl > sr ? sl : sr) < 0.97, "chorus widens: correlation %.3f", slr / (sl > sr ? sl : sr));
    }
    fresh();                                     /* lo-fi: hiss while it plays, silence after */
    km_set(P_LOFI, 100);
    km_set(P_METAL, MET_RAW);
    km_set(P_SUSTAIN, 0);
    km_pluck(6000, 110, 0, 0);
    render(0, 44100 * 4);
    CHECK(rms(bufl, 44100 * 3, 44100) < 1e-4f, "lo-fi hiss dies with the music: %.6f", (double)rms(bufl, 44100 * 3, 44100));
    {   /* none clicks in or out mid-note */
        static const int P[5] = {P_TAPE, P_LOFI, P_CHORUS, P_FILTER, P_FILTER};
        static const int V[5] = {100, 100, 100, -60, 60};
        int k;
        for (k = 0; k < 5; k++) {
            float on, off, alone, settled;
            master = 4096 / 16;
            fresh();
            km_set(P_SUSTAIN, 100);
            km_set(P_HARMONICS, 100);
            km_set(P_PORT, 0);
            km_pluck(8400, 110, 0, 0);           /* high and bright: a tone's own edges are measurable */
            render(0, 22050);
            alone = edge_max(11025, 11025);
            km_set(P[k], V[k]);
            render(0, 22050);
            on = edge_max(0, 4410);
            settled = edge_max(11025, 11025);
            km_set(P[k], 0);
            render(0, 22050);
            off = edge_max(0, 4410);
            master = 4096 / 2;
            CHECK(fm_maxf(on, off) < fm_maxf(alone, settled) * 4.0f + 2e-6f,
                  "%s %d in / out mid-note: edge %.6f / %.6f, settled %.6f, alone %.6f", km_param_info(P[k])->name, V[k],
                  (double)on, (double)off, (double)settled, (double)alone);
        }
    }
}

/* the groove: pulses at the tempo (3 a beat) over the pan; MIDI clock leads when it comes */
static void test_groove(void)
{
    int i, pulses = 0, last = -1, ding = 0, top = 0;
    fresh();
    give_pan(0, 2);
    km_set(P_PTEMPO, 120);                       /* 6 pulses a second */
    km_set(P_PATTERN, PAT_CASCADE);
    km_pattern(1);
    for (i = 0; i < 344; i++) {                  /* 2 s, in blocks of 256 */
        int v;
        render(0, 256);
        if (km_pat_pulse != last) {
            pulses++;
            last = km_pat_pulse;
        }
        for (v = 0; v < KM_NVOICE; v++) {
            ding |= km_voice_cents[v] == 5000;
            top |= km_voice_cents[v] == 6700;    /* F7: G4 */
        }
    }
    CHECK(km_pat_on && pulses >= 11 && pulses <= 13, "groove at 120: %d pulses in 2 s (12 wanted)", pulses);
    CHECK(ding && top, "the groove reaches the ding and the top fields (%d %d)", ding, top);
    km_pattern(0);
    render(0, 256);
    last = km_pat_pulse;
    render(0, 44100);
    CHECK(!km_pat_on && km_pat_pulse == last, "the groove stops");
    {   /* Backbeat plays the steel too */
        float sh = 0.0f;
        fresh();
        give_pan(0, 2);
        km_set(P_PATTERN, PAT_BACKBEAT);
        km_pattern(1);
        for (i = 0; i < 172; i++) {
            render(0, 256);
            sh = fm_maxf(sh, km_shell_level[0]);
        }
        CHECK(sh > 0.01f, "Backbeat's thwaps ring the shell (%.4f)", (double)sh);
    }
    fresh();                                     /* MIDI clock: 8 ticks a pulse */
    give_pan(0, 2);
    km_pattern(1);
    km_clock(KM_CLK_START);
    pulses = 0;
    last = -1;
    for (i = 0; i < 96; i++) {
        km_clock(KM_CLK_TICK);
        render(0, 64);
        if (km_pat_pulse != last) {
            pulses++;
            last = km_pat_pulse;
        }
    }
    CHECK(km_ext && pulses == 12, "MIDI clock: %d pulses in 96 ticks (12 wanted)", pulses);
    km_clock(KM_CLK_STOP);
    render(0, 256);
    last = km_pat_pulse;
    render(0, 44100);
    CHECK(km_pat_pulse == last, "MIDI stop: the groove held (pulse %d -> %d)", last, km_pat_pulse);
    km_pattern(1);
    render(0, 22050);
    CHECK(km_pat_pulse != last, "ARP after a stop: the groove runs");
    for (i = 0; i < PAT_N; i++) {                /* every groove on every scale: bounded, no NaN */
        int s;
        for (s = 0; s < KM_NSCALE; s += 3) {
            fresh();
            give_pan(s, km_scale_root(s));
            km_set(P_PATTERN, i);
            km_set(P_PTEMPO, 200);
            km_set(P_SYMPATHY, 100);
            km_pattern(1);
            render(0, 44100 * 2);
            CHECK(!nan_seen && peak < 1.0f && peak > 0.05f, "groove %d on %s: peak %.3f", i, km_scale_name(s), (double)peak);
        }
    }
}

static void test_reverb_idle(void)
{
    float dry = 0.0f, wet = 0.0f;
    int r;
    for (r = 0; r < 2; r++) {
        fresh();
        km_set(P_METAL, MET_RAW);
        km_set(P_SUSTAIN, 0);
        km_set(P_PORT, 0);
        render(0, 44100 * 3);
        if (r)
            km_set(P_REVERB, 80);
        km_pluck(7200, 120, 0, 0);
        render(0, 44100 * 2);
        if (r)
            wet = rms(bufl, 44100, 22050);
        else
            dry = rms(bufl, 44100, 22050);
    }
    CHECK(wet > 1e-4f && wet > 20.0f * dry, "the reverb wakes (%.6f vs dry %.6f)", (double)wet, (double)dry);
    CHECK(!nan_seen, "reverb idle NaN");
}

/* one WAV per metal: up the D Kurd and back down, a thwap each side, the Gu, the ding's harmonic */
static void listen(const char *dir)
{
    int m, i, n[KM_NPAN];
    char path[512];
    km_pan_notes(0, 2, 0, n);
    for (m = 0; m < MET_N; m++) {
        fresh();
        km_set(P_METAL, m);
        km_set(P_REVERB, 38);
        km_set(P_SYMPATHY, 40);
        give_pan(0, 2);
        for (i = 0; i < 9; i++)
            km_pluck(n[i], 100, i ? ((i & 1) ? -40 : 40) : 0, 200 * i);
        km_hit(HIT_THWAP_L, 100, 1900);
        km_hit(HIT_THWAP_R, 100, 2250);
        km_hit(HIT_GU, 110, 2600);
        km_hit(HIT_HARM, 100, 3000);
        render(0, MAXS);
        snprintf(path, sizeof path, "%s/%s.wav", dir, km_metal(m)->name);
        wav_write(path, MAXS);
    }
    printf("  wrote %s/*.wav (one per metal)\n", dir);
}

int main(int argc, char **argv)
{
    test_music();
    test_partials();
    test_decay();
    test_damp();
    test_voices();
    test_delayed();
    test_sympathy();
    test_hits();
    test_delay();
    test_reverb_idle();
    test_grains();
    test_no_pops();
    test_just();
    test_worn();
    test_color();
    test_groove();
    test_long_run();
    if (argc > 1)
        listen(argv[1]);
    CHECK(km_dropped == 0, "dropped commands %u", km_dropped);
    printf(fails ? "handpan: %d FAILED\n" : "handpan: all passed\n", fails);
    return fails != 0;
}
