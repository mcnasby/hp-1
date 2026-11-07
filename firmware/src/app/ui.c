/* SPDX-License-Identifier: GPL-3.0-only */
/* HP-1 UI: the panel and the screen. Part of the unity build (after gfx.c and project.c); the host
 * simulator includes it too. (The frame of it, knobs, bands and autosave, is FoMni's ui.c, by way of
 * FiMba-1.)
 *
 *   white keys     the two at each end are the steel: a palm thwap outside, a fingertip tak inside it.
 *                  The twelve between, the pan's notes: Pan (the ding on both middle keys, the fields
 *                  zigzag outward as they climb around the pan, the first on the right), Pan rev (left
 *                  first) or Linear (low to high); past the pan's own fields the scale goes on up, as on
 *                  an extended pan
 *   black keys     follow Black. Kit: castanets, an egg shaker pushed, then the steel's Gu, ghost, ding
 *                  slap, tak, ding harmonic, rim and mute, the shaker pulled, a splash cymbal. Duo: castanets,
 *                  a second pan of nine notes whose scale coordinates with the first, the Gu
 *   PLAY tap: the transport (the pattern, or the song; the groove with it); held: freeze the grains
 *   REC tap: arm / disarm recording (armed and stopped, the first key starts it); held: palm mute;
 *                  held with PLAY: armed, and started after the count-in
 *   SEQ held       white keys 1-16 choose the pattern, OCT- clears it (twice), OCT+ copies it into the
 *                  next empty one, EDIT undoes the last take
 *   OCT- / OCT+    the octave (Projects: load / save the slot, twice)
 *   HOME ENV FX LFO SEL ARP SEQ GLO   the pages (pressed again: the button's next page): HOME Pan, Shell;
 *                  ENV Touch, Feel; FX Space, Color; LFO Grain, Spread; SEL Keys, Duo; ARP Arp, Arp 2; SEQ
 *                  Groove, Pattern, Steps, Tempo, Song; GLO Setup, Projects. EDIT steps through them all.
 *   SELECT metal   ALGORITHM scale (and its usual key)   PRESETS key   KNOB 1-4 the page's values
 *   SAVE           saves (it also saves by itself, a few seconds after a change, when quiet) */

/* (the first eleven keep their numbers: the scenarios and saved habits know them) */
enum { V_PAN, V_SHELL, V_TOUCH, V_SPACE, V_COLOR, V_GRAIN, V_SPREAD, V_KEYS, V_DUO, V_GROOVE, V_SETUP,
       V_ARP, V_ARP2, V_PATTERN, V_STEPS, V_TEMPO, V_SONG, V_PROJECTS, V_FEEL, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Pan", "Shell", "Touch", "Space", "Color", "Grain", "Spread",
                                              "Keys", "Duo", "Groove", "Setup", "Arp", "Arp 2", "Pattern",
                                              "Steps", "Tempo", "Song", "Projects", "Feel"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_HOME, B_ENV, B_FX, B_FX, B_LFO, B_LFO, B_SEL, B_SEL, B_SEQ, B_GLO,
                                         B_ARP, B_ARP, B_SEQ, B_SEQ, B_SEQ, B_SEQ, B_GLO, B_ENV};
/* the pages in order (EDIT; a button's, pressed again) */
static const uint8_t PAGE_ORDER[NVIEWS] = {V_PAN, V_SHELL, V_TOUCH, V_FEEL, V_SPACE, V_COLOR, V_GRAIN, V_SPREAD,
                                           V_KEYS, V_DUO, V_ARP, V_ARP2, V_GROOVE, V_PATTERN, V_STEPS, V_TEMPO,
                                           V_SONG, V_SETUP, V_PROJECTS};
#define K_NONE 255                     /* a knob with nothing on it */
/* knobs that aren't saved settings (P_*): the sequence's own values, and the cursors on it */
enum { KV_PAT = 64, KV_LEN, KV_DIV, KV_SWING, KV_STEP, KV_NOTE, KV_SLEN, KV_SVEL, KV_SEC, KV_SPAT, KV_SREP, KV_SONG,
       KV_SLOT, KV_NPOS, KV_NCHR, KV_END };
static const uint8_t VIEW_KNOB[NVIEWS][4] = {
    {P_METAL, P_STRIKE, P_SUSTAIN, P_HARMONICS},
    {P_SIZE, P_THICK, P_SYMPATHY, P_PORT},
    {P_WORN, P_HUMAN, P_TUNING, P_RELEASE},
    {P_REVERB, P_RSIZE, P_DELAY, P_TIME},
    {P_TAPE, P_LOFI, P_CHORUS, P_FILTER},
    {P_GRAIN, P_GSIZE, P_GDENS, P_GPITCH},
    {P_GSPRAY, P_FEEDBACK, P_WIDTH, K_NONE},
    {P_LAYOUT, P_SCALE, P_KEY, P_BLACK},
    {P_DUO, P_DUOLVL, P_PERC, P_OCTAVE},
    {P_PATTERN, P_PTEMPO, P_PERC, P_GROOVE},
    {P_TUNE, P_MIDICH, P_MIDIOUT, P_THEME},
    {P_AMODE, P_ARATE, P_AOCT, P_AGATE},
    {P_ASWING, P_ACHANCE, P_AHOLD, P_AORDER},
    {KV_PAT, KV_LEN, KV_DIV, KV_SWING},
    {KV_STEP, KV_NOTE, KV_SLEN, KV_SVEL},
    {P_PTEMPO, P_QUANT, P_COUNTIN, P_CLICK},
    {KV_SEC, KV_SPAT, KV_SREP, KV_SONG},
    {KV_SLOT, KV_NPOS, KV_NCHR, K_NONE},
    {P_FEEL, K_NONE, K_NONE, K_NONE},
};
#define HOLD_MS 500u                   /* PLAY held this long: freeze; REC: palm mute (a tap: the transport, arm) */
#define ASK_MS 3000u                   /* "CLEAR?", "SAVE TO A?", "LOAD A?": the same button again within this */
#define SAVE_HOLD_MS 1500u             /* SAVE held this long: reset to the defaults? (a tap: save) */
#define RESET_ASK_MS 5000u             /* ...asked this long: SAVE again resets, anything else keeps */

#define NWHITE KM_NWHITE
#define NBLACK KM_NBLACK
static const uint8_t WHITE_K[NWHITE] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_K[NBLACK] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};

/* the black keys: the kit (or the second pan between castanets and the Gu) */
#define B_DUO0 1                       /* the first of the second pan's nine keys */
static const uint8_t HIT_OF[NBLACK] = {HIT_CASTANET, HIT_SHAKE_L, HIT_GU, HIT_GHOST, HIT_SLAP, HIT_TAK, HIT_HARM,
                                       HIT_RIM, HIT_MUTE, HIT_SHAKE_R, HIT_SPLASH};
static const char *const HIT_NAME[HIT_N] = {"Thwp", "Tak", "Gu", "Gho", "Slap", "Tak", "Harm", "Rim", "Mute", "Tak", "Thwp",
                                            "Spl", "Shk", "Shk", "Cas"};
/* the black keys a second pan (Duo: it shares the first's root, Duo key: in another key) */
static int duo_on(void) { return proj.par[P_BLACK] == BLK_DUO || proj.par[P_BLACK] == BLK_DUOKEY; }
static int duo_other(void) { return proj.par[P_BLACK] == BLK_DUOKEY; }
static int is_duo_key(int b) { return duo_on() && b >= B_DUO0 && b < B_DUO0 + KM_NDUO; }
/* what black key b plays when it isn't the second pan's */
static int black_hit(int b) { return duo_on() && b == NBLACK - 1 ? HIT_GU : HIT_OF[b]; }

/* the screen's colors (Setup: Theme). Dark (the default): a dim room, warm cream type, and gold for what
 * sounds. Light: sand and ink. The pan keeps its metal in both. */
typedef struct {
    uint16_t bg, panel, line, dim, text, white, acc, acc_t, teal, teal_t, glow;
} theme_t;
static const theme_t THEMES[THEME_N] = {
    {RGB(18, 16, 15), RGB(36, 32, 29), RGB(66, 59, 52), RGB(140, 128, 114), RGB(238, 228, 212), RGB(20, 16, 12),
     RGB(242, 168, 72), RGB(86, 58, 28), RGB(96, 184, 172), RGB(30, 62, 58), RGB(255, 206, 120)},
    {RGB(240, 234, 222), RGB(226, 217, 201), RGB(204, 192, 174), RGB(128, 116, 100), RGB(46, 40, 34),
     RGB(255, 250, 240), RGB(208, 116, 36), RGB(246, 216, 184), RGB(34, 128, 120), RGB(198, 228, 222),
     RGB(255, 186, 84)},
};
#define THEME (&THEMES[(unsigned)proj.par[P_THEME] < THEME_N ? proj.par[P_THEME] : 0])
#define K_BG (THEME->bg)
#define K_PANEL (THEME->panel)
#define K_LINE (THEME->line)
#define K_DIM (THEME->dim)
#define K_TEXT (THEME->text)
#define K_WHITE (THEME->white)                 /* text on the accent */
#define K_ACC (THEME->acc)
#define K_ACC_T (THEME->acc_t)
#define K_TEAL (THEME->teal)
#define K_TEAL_T (THEME->teal_t)
#define K_GLOW (THEME->glow)

/* the metals as they look: the shell's shadow and its light, and the ink engraved on it */
static const struct {
    uint16_t dark, light, ink;
} METAL_COL[MET_N] = {
    {RGB(44, 33, 26), RGB(158, 118, 80), RGB(250, 236, 214)},     /* Nitrided: dark bronze */
    {RGB(78, 84, 92), RGB(222, 228, 234), RGB(28, 30, 34)},        /* Stainless: bright steel */
    {RGB(52, 28, 20), RGB(186, 102, 60), RGB(255, 230, 206)},      /* Ember: copper-brown */
    {RGB(46, 46, 48), RGB(146, 142, 134), RGB(244, 240, 232)},     /* Raw: gray steel */
};
#define METAL (&METAL_COL[(unsigned)proj.par[P_METAL] < MET_N ? proj.par[P_METAL] : 0])

#define NRIPPLE (KM_NPAN + KM_NDUO)    /* a ripple a note: the main pan's, then the second pan's */
static struct {
    uint8_t view;
    uint32_t btn, keys, btn_used;
    uint32_t enc_t[NE];
    uint8_t dirty;
    uint32_t act_t;
    char msg[2][24];
    uint32_t msg_until;
    int8_t touched;
    uint32_t touch_until;
    uint32_t frame;
    uint32_t sig[3];
    uint32_t pan_sig;                  /* the notes last given to the engine */
    uint32_t play_t, rec_t;            /* when PLAY / REC went down (held: freeze, palm mute) */
    uint8_t step, sec, slot, npos;     /* the cursors: Steps, Song, Projects (the name's letter) */
    uint8_t ask;                       /* waiting for the same button again (ASK_*) */
    uint32_t ask_until;
    uint8_t entry;                     /* Steps: a key wrote at the cursor (another one held with it joins it) */
    uint32_t out_n;                    /* notes the sequencer and the arp played (the host's checks) */
    int16_t out_last;
    uint32_t save_t;                   /* when SAVE went down (held: reset to the defaults?) */
    uint32_t reset_ask;                /* until when "RESET?" waits for SAVE (0: not asking) */
    uint8_t frozen;
    uint32_t rng;
    uint32_t ripple[NRIPPLE];          /* the frame each note was last struck (0: never) */
    uint8_t rim[3];                    /* the rim's glow, left and right, and the air's (0..16): they follow
                                        * the shell and the cavity, and fade slower than they do */
    uint32_t hit_t[HIT_N];             /* ...each hit */
    int16_t key_cents[NKEYS];          /* what each key struck (to damp it, and its MIDI note off) */
    uint8_t key_on[NKEYS];
    uint8_t key_arp[NKEYS];            /* ...or gave the arp (EV_* + 1) */
} ui;
enum { ASK_NONE, ASK_CLEAR, ASK_SAVE, ASK_LOAD };

static void say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
}
void ui_say(const char *a, const char *b) { say(a, b); }
int ui_dirty(void) { return ui.dirty != 0; }

static void itoa_u(uint32_t v, char *b)
{
    char t[12];
    int n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
}

static void mark_dirty(void) { ui.dirty = 1; }

/* (no C library on the device: libc.c has only mem*) */
static int32_t s_len(const char *s)
{
    int32_t n = 0;
    while (s[n])
        n++;
    return n;
}
static void s_cpy(char *d, const char *s)
{
    while ((*d++ = *s++))
        ;
}
static void s_upper(char *s)
{
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z')
            *s = (char)(*s - 32);
}

static uint32_t ui_rnd(void)
{
    uint32_t x = ui.rng ? ui.rng : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return ui.rng = x;
}

/* ------------------------------------------------------------ values --- */
/* "D Kurd": the pan's key and scale */
static void pan_name(char *t)
{
    char s[16];
    km_param_text(P_KEY, proj.par[P_KEY], t);
    km_param_text(P_SCALE, proj.par[P_SCALE], s);
    t += s_len(t);
    *t++ = ' ';
    s_cpy(t, s);
}

/* the knobs that aren't settings (KV_*: below, with the notes) */
static void kv_text(int k, char *t);
static int kv_get(int k);
static int kv_lo(int k);
static int kv_hi(int k);
static int kv_off(int k);
static void kv_set(int k, int v);
static const char *kv_name(int k);
static int knob_val(int k) { return k >= KV_PAT ? kv_get(k) : proj.par[k]; }
static const char *knob_name(int k) { return k >= KV_PAT ? kv_name(k) : km_param_info(k)->name; }
static int knob_lo(int k) { return k >= KV_PAT ? kv_lo(k) : km_param_info(k)->lo; }

/* the value as this screen shows it: the second pan by its name */
static void value_text(int k, char *t)
{
    if (k >= KV_PAT)
        kv_text(k, t);
    else if (k == P_DUO)
        km_duo_name(proj.par[P_SCALE], proj.par[P_KEY], duo_other(), proj.par[P_DUO], t);
    else
        km_param_text(k, proj.par[k], t);
}

static void say_param(int k)
{
    char t[24];
    if (k == P_SCALE || k == P_KEY) {
        pan_name(t);
        s_upper(t);
        say("PAN", t);
        return;
    }
    value_text(k, t);
    s_upper(t);
    say(knob_name(k), t);
}

static int knob_hi(int k)
{
    return k >= KV_PAT ? kv_hi(k) : k == P_DUO ? km_duo_count(proj.par[P_SCALE], proj.par[P_KEY], duo_other()) - 1 : km_param_info(k)->hi;
}

static void knob_set(int k, int v)
{
    const int lo = knob_lo(k), hi = knob_hi(k);
    v = v < lo ? lo : v > hi ? hi : v;
    if (k >= KV_PAT) {
        if (v != kv_get(k))
            kv_set(k, v);
        return;
    }
    if (v == proj.par[k])
        return;
    proj.par[k] = (int16_t)v;
    km_set(k, v);
    mark_dirty();
    if (k == P_SCALE) {                              /* a new pan comes in the key it's usually made in */
        proj.par[P_KEY] = (int16_t)km_scale_root(v);
        km_set(P_KEY, proj.par[P_KEY]);
    }
    if (k == P_SCALE || k == P_KEY || k == P_BLACK) {   /* and its second pan matches it afresh */
        proj.par[P_DUO] = 0;
        km_set(P_DUO, 0);
    }
}

/* knob acceleration by speed: a slow turn is one step a detent, a quick one up to 8 (Time, Grain ms:
 * hundreds of steps) */
static uint8_t accel_off;              /* the host simulator's "spin": exact steps */
static int32_t accel(int role, int32_t s, int range)
{
    uint32_t now = plat_ms(), dt = now - ui.enc_t[role], a = (uint32_t)(s < 0 ? -s : s), m;
    ui.enc_t[role] = now;
    if (range <= 24 || !a || accel_off)
        return s;
    if (a > 1)
        dt /= a;
    m = dt < 15u ? 8u : dt < 30u ? 5u : dt < 60u ? 2u : 1u;
    return s * (int32_t)m;
}

/* a knob that does nothing now: the second pan when the black keys are the kit; a rest's length */
static int knob_off(int k)
{
    if (k >= KV_PAT)
        return kv_off(k);
    return (k == P_DUO || k == P_DUOLVL) && !duo_on();
}

static void turn(int role, int k, int32_t e)
{
    if (!e || k == K_NONE)
        return;
    if (knob_off(k)) {
        if (k >= KV_PAT)
            say("A REST", "K2: ITS NOTE");
        else
            say("DUO OFF", "KEYS: BLACK DUO");
        return;
    }
    knob_set(k, knob_val(k) + accel(role, e, knob_hi(k) - knob_lo(k)));
}

/* ------------------------------------------------------------- notes --- */
static int octave_now(void) { return proj.par[P_OCTAVE]; }
static int ding_now(void) { return km_ding_cents(proj.par[P_KEY], octave_now()); }
static int tune(int c) { return proj.par[P_TUNING] == TUNE_JUST ? km_just_cents(c, ding_now()) : c; }
static int pan_notes(int *n) { return km_pan_notes(proj.par[P_SCALE], proj.par[P_KEY], octave_now(), n); }
static int white_index(int w) { return km_white_index(proj.par[P_LAYOUT], w); }
static int white_cents(int w)                   /* (-1: a key of the steel) */
{
    const int c = km_white_cents(proj.par[P_LAYOUT], proj.par[P_SCALE], proj.par[P_KEY], octave_now(), w);
    return c < 0 ? -1 : tune(c);
}
/* how many of the pan's notes the white keys reach (Pan: the ding and ten; Linear: twelve) */
static int notes_reached(void)
{
    int w, m = 0;
    for (w = 0; w < NWHITE; w++)
        if (white_index(w) >= m)
            m = white_index(w) + 1;
    return m;
}
static int white_pan(int w) { return (int)(km_white_pos(proj.par[P_LAYOUT], w) * 100.0f); }
static int duo_cents(int i)
{
    int n[KM_NDUO];
    km_duo_notes(proj.par[P_SCALE], proj.par[P_KEY], duo_other(), octave_now(), proj.par[P_DUO], n);
    return tune(n[i < 0 ? 0 : i >= KM_NDUO ? KM_NDUO - 1 : i]);
}
/* the second pan's key b (a black key) : its note's index, laid out as the white keys are */
static int duo_index(int b) { return km_duo_index(proj.par[P_LAYOUT], b - B_DUO0); }
static int view_knob(int i) { return VIEW_KNOB[ui.view][i]; }
/* a note's name, spelled as the pan's scale spells it (G# on an E pan, Ab on an F one) */
static const char *note_name(int cents) { return km_note_name(cents, km_sharps(proj.par[P_SCALE], proj.par[P_KEY])); }
static int midi_ch(void) { return proj.par[P_MIDICH] ? proj.par[P_MIDICH] - 1 : 0; }
static uint8_t cents_note(int c) { int n = (c + 50) / 100; return (uint8_t)(n < 0 ? 0 : n > 127 ? 127 : n); }

/* the engine's picture of the pan (sympathy, the grooves): sent when it changes */
static void pan_sync(void)
{
    int n[KM_NPAN + KM_NDUO], i, m = KM_NPAN;
    uint32_t h = 2166136261u;
    pan_notes(n);
    for (i = 0; i < KM_NPAN; i++)
        n[i] = tune(n[i]);
    if (duo_on())
        for (i = 0; i < KM_NDUO; i++)
            n[m++] = duo_cents(i);
    for (i = 0; i < m; i++)
        h = (h ^ (uint32_t)n[i]) * 16777619u;
    if (h == ui.pan_sig)
        return;
    ui.pan_sig = h;
    km_pan(n, m);
}

static void note_out(int on, uint8_t n, uint8_t vel)
{
    uint32_t st;
    if (!proj.par[P_MIDIOUT])
        return;
    st = (on ? 0x90u : 0x80u) | (uint32_t)midi_ch();
    plat_midi_out(st >> 4 | st << 8 | (uint32_t)n << 16 | (uint32_t)(on ? vel : 0) << 24);
}

/* Humanize: the FM-1's keys only know on and off, so each strike gets a hand's variation: its strength
 * (and with it, its brightness), and at the top of the range a hair of timing */
static int human_vel(int vel)
{
    const int h = proj.par[P_HUMAN];
    int d = (int)(ui_rnd() % 201u) - 100;
    return vel + d * h / 400;
}
static int human_ms(void) { return proj.par[P_HUMAN] > 50 ? (int)(ui_rnd() % (uint32_t)(1 + (proj.par[P_HUMAN] - 50) / 12)) : 0; }

/* -------------------------------------------------------------- feel --- */
/* Feel: Expressive (the default). The FM-1's keys are contacts, on or off: they can't tell a light tap
 * from a hard press. So the touch is played in time. Every strike is a lighter, quicker fingertip (a
 * little brighter); a key let go within TAP_MS is a quick tap: the field rings shorter (Release Ring;
 * Damp damps it as ever); a key held past PRESS_MS is a hand resting on the field, pressing in over
 * PRESS_RISE_MS (an S), and lifting with the key: the note fuller and darker (km_press; MIDI out: poly
 * pressure). Over MIDI, the controller's own pressure: poly (A0) on its note, channel (D0) on every
 * note held. Classic: none of it, the sound as it always was. */
#define TAP_MS 140u
#define PRESS_MS 300u
#define PRESS_RISE_MS 1200u
#define PRESS_STEP 4                   /* sent as it moves this much (of 127): ~30 messages a press */
#define LIGHT 100                      /* km_pluck_x: the strike Strike + 14 */
static struct {
    uint32_t t[NKEYS];                 /* when each key went down */
    uint8_t p[NKEYS];                  /* the pressure last sent for it */
    uint32_t mon[4], mprs[4];          /* MIDI notes held, and those with pressure on them */
} tch;

static int expressive(void) { return proj.par[P_FEEL] == FEEL_EXPRESSIVE; }

static void press_out(int cents, int p)
{
    uint32_t st = 0xA0u | (uint32_t)midi_ch();
    if (proj.par[P_MIDIOUT])
        plat_midi_out(st >> 4 | st << 8 | (uint32_t)cents_note(cents) << 16 | (uint32_t)p << 24);
}

static void touch_pluck(int k, int cents, int vel, int pan, int ms)
{
    if (expressive())
        km_pluck_x(cents, vel, pan, ms, LIGHT);
    else
        km_pluck(cents, vel, pan, ms);
    tch.t[k] = plat_ms();
    tch.p[k] = 0;
}

static void touch_send(int k, int p)
{
    tch.p[k] = (uint8_t)p;
    km_press(ui.key_cents[k], p);
    press_out(ui.key_cents[k], p);
}

static void touch_up(int k)
{
    if (tch.p[k])
        touch_send(k, 0);                        /* the hand comes off */
    if (expressive() && !proj.par[P_RELEASE] && plat_ms() - tch.t[k] < TAP_MS)
        km_staccato(ui.key_cents[k]);
}

/* each frame: the keys held press in */
static void touch_tick(void)
{
    const uint32_t now = plat_ms();
    int k;
    for (k = 0; k < NKEYS; k++) {
        int p = 0, d;
        if (!ui.key_on[k])
            continue;
        if (expressive() && now - tch.t[k] > PRESS_MS) {
            float x = (float)(now - tch.t[k] - PRESS_MS) * (1.0f / (float)PRESS_RISE_MS);
            x = x > 1.0f ? 1.0f : x;
            p = (int)(127.0f * x * x * (3.0f - 2.0f * x) + 0.5f);
        }
        d = p - tch.p[k];
        if (d && (d >= PRESS_STEP || d <= -PRESS_STEP || !p || p == 127))
            touch_send(k, p);
    }
}

/* MIDI in (on its channel): the notes held, and the pressure on them */
static void midi_press(uint32_t n, uint32_t p)
{
    const uint32_t m = 1u << (n & 31u);
    km_press(tune((int)n * 100), (int)p);
    tch.mprs[n >> 5] = p ? tch.mprs[n >> 5] | m : tch.mprs[n >> 5] & ~m;
}

static void touch_midi(uint32_t type, uint32_t d1, uint32_t d2)
{
    const uint32_t m = 1u << (d1 & 31u);
    uint32_t n;
    if (type == 0x90u && d2) {
        tch.mon[d1 >> 5] |= m;
        tch.mprs[d1 >> 5] &= ~m;                 /* (a strike: the hand came off it) */
    } else if (type == 0x80u || type == 0x90u) {
        tch.mon[d1 >> 5] &= ~m;
        if (tch.mprs[d1 >> 5] & m)
            midi_press(d1, 0);
    } else if (type == 0xA0u && expressive()) {  /* poly pressure: its note */
        midi_press(d1, d2);
    } else if (type == 0xD0u && expressive()) {  /* channel pressure: every note held */
        for (n = 0; n < 128u; n++)
            if (tch.mon[n >> 5] >> (n & 31u) & 1u)
                midi_press(n, d1);
    }
}

/* key k strikes a note: remembered so its release can damp it; recorded while recording (rec) */
static void key_strike(int k, int kind, int cents, int vel, int pan, int rec)
{
    vel = human_vel(vel);
    vel = vel < 1 ? 1 : vel > 127 ? 127 : vel;
    touch_pluck(k, cents, vel, pan, human_ms());
    ui.key_cents[k] = (int16_t)cents;
    ui.key_on[k] = 1;
    note_out(1, cents_note(cents), (uint8_t)vel);
    if (rec)
        seq_note_on(k, kind, cents, vel);
}

static void key_release(int k)
{
    if (ui.key_arp[k]) {                             /* the arp lets it go */
        seq_arp_key(ui.key_arp[k] - 1, ui.key_cents[k], 0);
        ui.key_arp[k] = 0;
    }
    if (!ui.key_on[k])
        return;
    touch_up(k);
    if (proj.par[P_RELEASE])
        km_damp(ui.key_cents[k]);
    note_out(0, cents_note(ui.key_cents[k]), 0);
    ui.key_on[k] = 0;
    seq_note_off(k);
}

static void set_freeze(int on)
{
    ui.frozen = (uint8_t)on;
    km_freeze(on);
    say("GRAINS", on ? "FROZEN" : "LIVE");
}

static void hit_seen(int kind)
{
    ui.hit_t[kind] = ui.frame + 1u;
    if (kind == HIT_SLAP || kind == HIT_HARM)
        ui.ripple[0] = ui.frame + 1u;
}

static void strike_hit(int kind, int rec)
{
    const int vel = human_vel(104);
    km_hit(kind, vel, 0);
    hit_seen(kind);
    if (rec)
        seq_note_on(-1, EV_HIT, kind, vel < 1 ? 1 : vel > 127 ? 127 : vel);
}

/* Steps, stopped: a key writes its note at the cursor and moves it on (a key held with it: a chord) */
static int step_entry(void) { return ui.view == V_STEPS && !seq_running(); }
static void step_write(int k, int kind, int val)
{
    const int p = sq.h.cur, len = sq.h.pat[p].len;
    if (ui.entry && (ui.keys & ~(1u << k))) {        /* with the last one */
        seq_step_put(p, (ui.step + len - 1) % len, kind, val, 100, 1);
        return;
    }
    seq_step_rest(p, ui.step);
    seq_step_put(p, ui.step, kind, val, 100, 1);
    ui.step = (uint8_t)((ui.step + 1) % len);
    ui.entry = 1;
}

/* a key's note (EV_PAN, EV_DUO): written on Steps, given to the arp while it runs, else struck. 1: struck */
static int note_key(int k, int kind, int cents, int vel, int pan)
{
    if (step_entry()) {
        key_strike(k, kind, cents, vel, pan, 0);
        step_write(k, kind, cents);
        return 1;
    }
    if (seq_arp_on()) {
        ui.key_arp[k] = (uint8_t)(kind + 1);
        ui.key_cents[k] = (int16_t)cents;
        seq_arp_key(kind, cents, 1);
        return 0;
    }
    key_strike(k, kind, cents, vel, pan, 1);
    return 1;
}

static void hit_key(int k, int kind)
{
    strike_hit(kind, !step_entry());
    if (step_entry())
        step_write(k, EV_HIT, kind);
}

static int seq_held(void) { return (ui.btn >> B_SEQ & 1u) != 0; }
static void choose_pattern(int p)
{
    char t[8];
    ui.btn_used |= 1u << B_SEQ;                      /* (SEQ's release: no page) */
    seq_select(p);
    itoa_u((uint32_t)p + 1u, t);
    say(seq_next_pat() >= 0 ? "NEXT PATTERN" : "PATTERN", t);
}

static void white_down(int w)
{
    if (seq_held()) {                                /* SEQ held: the patterns */
        choose_pattern(w);
        return;
    }
    if (km_white_hit(w) >= 0) {                      /* the steel, at the ends */
        hit_key(WHITE_K[w], km_white_hit(w));
        return;
    }
    if (note_key(WHITE_K[w], EV_PAN, white_cents(w), 100, white_pan(w)))
        ui.ripple[white_index(w)] = ui.frame + 1u;
}

static void black_down(int b)
{
    if (is_duo_key(b)) {                             /* the second pan, standing to the right */
        const int i = duo_index(b);
        if (note_key(BLACK_K[b], EV_DUO, duo_cents(i), 100 * proj.par[P_DUOLVL] / 100 + 12, 55 + 6 * (b - B_DUO0 - KM_NDUO / 2)))
            ui.ripple[KM_NPAN + i] = ui.frame + 1u;
        return;
    }
    hit_key(BLACK_K[b], black_hit(b));
}

static void black_up(int b) { key_release(BLACK_K[b]); }

/* --------------------------------------------------- the sequencer's notes --- */
static int midi_pan(int cents);
/* where a note the sequencer plays sits (and its ripple) */
static int note_pan(int kind, int cents)
{
    int b;
    if (kind != EV_DUO)
        return midi_pan(cents);
    for (b = B_DUO0; b < B_DUO0 + KM_NDUO; b++)
        if (is_duo_key(b) && duo_cents(duo_index(b)) == cents) {
            ui.ripple[KM_NPAN + duo_index(b)] = ui.frame + 1u;
            return 55 + 6 * (b - B_DUO0 - KM_NDUO / 2);
        }
    return 55;
}

static void seq_out_note(int kind, int val, int vel, int delay_ms)
{
    ui.out_n++;
    ui.out_last = (int16_t)val;
    if (kind == EV_HIT) {
        km_hit(val, vel, delay_ms);
        hit_seen(val);
        return;
    }
    km_pluck(val, vel, note_pan(kind, val), delay_ms);
    note_out(1, cents_note(val), (uint8_t)vel);
}

/* a note's end: a finger on it when Release is Damp (else it rings, as a strike does) */
static void seq_out_off(int kind, int val, int delay_ms)
{
    if (kind == EV_HIT)
        return;
    if (proj.par[P_RELEASE])
        km_release(val, delay_ms);
    note_out(0, cents_note(val), 0);
}

static void seq_out_click(int flags, int delay_ms)
{
    const int c = proj.par[P_CLICK];
    int level = c > CLICK_LEVELS ? c - CLICK_LEVELS : c;
    if (!level && (flags & 2))
        level = CLICK_LEVELS / 2;                    /* (a count-in clicks, Click off or not) */
    click_post(flags & 1, level, delay_ms);
}

static void seq_out_groove(int pulse, int delay_ms) { km_groove_pulse(pulse, delay_ms); }
static void seq_out_say(const char *a, const char *b) { say(a, b); }

/* ------------------------------------------- the sequence's knobs (KV_*) --- */
static const char NAME_CH[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
static const char *const HIT_LONG[HIT_N] = {"Thwap L", "Tak L", "Gu", "Ghost", "Slap", "Tak", "Harmonic", "Rim", "Mute",
                                            "Tak R", "Thwap R", "Splash", "Shaker", "Shaker 2", "Castanet"};
#define NCHOICE (1 + KM_NPAN + KM_NDUO + HIT_N)

/* what a step can hold: a rest, the notes the keys reach, the second pan's (Duo), the hits */
static int note_choices(uint8_t *kind, int16_t *val)
{
    int n[KM_NPAN], m = 1, i;
    const int all = notes_reached();
    kind[0] = 0xFF;
    val[0] = 0;
    pan_notes(n);
    for (i = 0; i < all; i++) {
        kind[m] = EV_PAN;
        val[m++] = (int16_t)tune(n[i]);
    }
    if (duo_on())
        for (i = 0; i < KM_NDUO; i++) {
            kind[m] = EV_DUO;
            val[m++] = (int16_t)duo_cents(i);
        }
    for (i = 0; i < HIT_N; i++) {
        kind[m] = EV_HIT;
        val[m++] = (int16_t)i;
    }
    return m;
}

static int cur_ev(void) { return seq_step_ev(sq.h.cur, ui.step); }
static int ev_kind_of(int e) { return sq.ev[e].val >> 14; }
static int ev_val_of(int e) { return sq.ev[e].val & 0x3FFF; }

/* a note by name and octave: "D3" */
static void note_text(int cents, char *t)
{
    s_cpy(t, note_name(cents));
    itoa_u((uint32_t)((cents + 50) / 1200 - 1), t + s_len(t));
}

static const char *kv_name(int k)
{
    static const char *const N[KV_END - KV_PAT] = {"Pattern", "Length", "Division", "Swing", "Step", "Note", "Length",
                                                   "Velocity", "Section", "Pattern", "Repeats", "Song", "Slot", "Letter",
                                                   "Character"};
    return N[(unsigned)(k - KV_PAT) < (unsigned)(KV_END - KV_PAT) ? k - KV_PAT : 0];
}

static int kv_lo(int k) { return k == KV_LEN || k == KV_SLEN || k == KV_SVEL || k == KV_SREP ? 1 : 0; }

static int kv_hi(int k)
{
    uint8_t kd[NCHOICE];
    int16_t v[NCHOICE];
    switch (k) {
    case KV_PAT: return SEQ_NPAT - 1;
    case KV_LEN: return SEQ_MAXLEN;
    case KV_DIV: return DIV_N - 1;
    case KV_SWING: return 100;
    case KV_STEP: return sq.h.pat[sq.h.cur].len - 1;
    case KV_NOTE: return note_choices(kd, v) - 1;
    case KV_SLEN: return 16;
    case KV_SVEL: return 127;
    case KV_SEC: return SEQ_NSEC - 1;
    case KV_SPAT: return SEQ_END;
    case KV_SREP: return 16;
    case KV_SONG: return SONG_LOOP;
    case KV_SLOT: return PLAT_NSLOT - 1;
    case KV_NPOS: return SEQ_NAME - 2;
    case KV_NCHR: return (int)sizeof NAME_CH - 2;
    default: return 0;
    }
}

static int kv_off(int k) { return (k == KV_SLEN || k == KV_SVEL) && cur_ev() < 0; }

static int kv_get(int k)
{
    const int p = sq.h.cur, e = cur_ev();
    switch (k) {
    case KV_PAT: return p;
    case KV_LEN: return sq.h.pat[p].len;
    case KV_DIV: return sq.h.pat[p].div;
    case KV_SWING: return sq.h.pat[p].swing;
    case KV_STEP: return ui.step;
    case KV_NOTE: {
        uint8_t kd[NCHOICE];
        int16_t v[NCHOICE];
        int n = note_choices(kd, v), i, best = 0, bd = 1 << 30;
        if (e < 0)
            return 0;
        for (i = 1; i < n; i++)                      /* its own, else the nearest of its kind */
            if (kd[i] == ev_kind_of(e)) {
                int d = v[i] - ev_val_of(e);
                d = d < 0 ? -d : d;
                if (d < bd) {
                    bd = d;
                    best = i;
                }
            }
        return best;
    }
    case KV_SLEN: {
        const int s = e < 0 ? 1 : (sq.ev[e].len + SEQ_SUB - 1) / SEQ_SUB;
        return s < 1 ? 1 : s > 16 ? 16 : s;
    }
    case KV_SVEL: return e < 0 ? 100 : sq.ev[e].vel & 0x7F;
    case KV_SEC: return ui.sec;
    case KV_SPAT: return sq.h.sec[ui.sec].pat;
    case KV_SREP: return sq.h.sec[ui.sec].rep;
    case KV_SONG: return sq.h.song;
    case KV_SLOT: return ui.slot;
    case KV_NPOS: return ui.npos;
    case KV_NCHR: {
        int i;
        for (i = 1; NAME_CH[i]; i++)
            if (NAME_CH[i] == sq.h.name[ui.npos])
                return i;
        return 0;
    }
    default: return 0;
    }
}

static void kv_set(int k, int v)
{
    const int p = sq.h.cur, e = cur_ev();
    switch (k) {
    case KV_PAT: seq_select(v); return;
    case KV_LEN:
        sq.h.pat[p].len = (uint8_t)v;
        if (ui.step >= v)
            ui.step = (uint8_t)(v - 1);
        break;
    case KV_DIV: sq.h.pat[p].div = (uint8_t)v; break;
    case KV_SWING: sq.h.pat[p].swing = (uint8_t)v; break;
    case KV_STEP: ui.step = (uint8_t)v; return;
    case KV_NOTE: {                                  /* the step's note (its length and strength kept) */
        uint8_t kd[NCHOICE];
        int16_t vl[NCHOICE];
        const int vel = e < 0 ? 100 : sq.ev[e].vel & 0x7F, len = kv_get(KV_SLEN);
        note_choices(kd, vl);
        seq_step_rest(p, ui.step);
        if (v > 0) {
            seq_step_put(p, ui.step, kd[v], vl[v], vel, len);
            if (!seq_running())                      /* (heard as it's chosen) */
                seq_out_note(kd[v], vl[v], vel, 0);
        }
        break;
    }
    case KV_SLEN:
        if (e >= 0)
            sq.ev[e].len = (uint16_t)(v * SEQ_SUB);
        break;
    case KV_SVEL:
        if (e >= 0)
            sq.ev[e].vel = (uint8_t)v;
        break;
    case KV_SEC: ui.sec = (uint8_t)v; return;
    case KV_SPAT: sq.h.sec[ui.sec].pat = (uint8_t)v; break;
    case KV_SREP: sq.h.sec[ui.sec].rep = (uint8_t)v; break;
    case KV_SONG: sq.h.song = (uint8_t)v; break;
    case KV_SLOT:
        ui.slot = (uint8_t)v;
        ui.ask = ASK_NONE;
        return;
    case KV_NPOS: ui.npos = (uint8_t)v; return;
    case KV_NCHR: {                                  /* (letters past the name's end: spaces before it) */
        int i;
        for (i = 0; i < ui.npos; i++)
            if (!sq.h.name[i])
                sq.h.name[i] = ' ';
        sq.h.name[ui.npos] = NAME_CH[v];
        for (i = SEQ_NAME - 2; i >= 0 && (sq.h.name[i] == ' ' || !sq.h.name[i]); i--)
            sq.h.name[i] = 0;                        /* (no spaces at its end) */
        break;
    }
    default: return;
    }
    seq_retime();
    seq_set_dirty();
}

static void kv_text(int k, char *t)
{
    const int v = kv_get(k), e = cur_ev();
    switch (k) {
    case KV_PAT: case KV_STEP: case KV_SEC: case KV_NPOS: itoa_u((uint32_t)v + 1u, t); return;
    case KV_DIV: km_param_text(P_ARATE, v, t); return;
    case KV_NOTE:
        if (e < 0)
            s_cpy(t, "Rest");
        else if (ev_kind_of(e) == EV_HIT)
            s_cpy(t, HIT_LONG[ev_val_of(e) < HIT_N ? ev_val_of(e) : 0]);
        else if (ev_kind_of(e) == EV_DUO) {
            s_cpy(t, "Duo ");
            note_text(ev_val_of(e), t + 4);
        } else
            note_text(ev_val_of(e), t);
        return;
    case KV_SPAT:
        if (v >= SEQ_END)
            s_cpy(t, "End");
        else
            itoa_u((uint32_t)v + 1u, t);
        return;
    case KV_SONG: s_cpy(t, v == SONG_LOOP ? "Loop" : v ? "On" : "Off"); return;
    case KV_SLOT:
        t[0] = (char)('A' + v);
        t[1] = 0;
        return;
    case KV_NCHR:
        if (!v)
            s_cpy(t, "Space");
        else {
            t[0] = NAME_CH[v];
            t[1] = 0;
        }
        return;
    default: itoa_u((uint32_t)v, t); return;
    }
}

/* ------------------------------------------------------------ buttons --- */
static void set_view(int v)
{
    ui.view = (uint8_t)v;
    ui.touched = -1;
}

static int page_pos(int v)
{
    int i;
    for (i = 0; i < NVIEWS; i++)
        if (PAGE_ORDER[i] == v)
            return i;
    return 0;
}

/* back to the defaults: every sound setting, the transport stopped and the grains let go. The Setup page
 * (tuning, MIDI, the theme) is about the room and the rig, not the sound: it stays. So does the sequence. */
static void reset_defaults(void)
{
    static const uint8_t KEEP[4] = {P_TUNE, P_MIDICH, P_MIDIOUT, P_THEME};
    int16_t keep[4];
    int i;
    for (i = 0; i < 4; i++)
        keep[i] = proj.par[KEEP[i]];
    seq_play(0);
    set_freeze(0);
    project_defaults();
    for (i = 0; i < 4; i++)
        proj.par[KEEP[i]] = keep[i];
    project_apply();
    mark_dirty();
    say("RESET", "TO DEFAULTS");
}

static void reset_ask(int on)
{
    ui.reset_ask = on ? plat_ms() + RESET_ASK_MS : 0u;
    if (on) {
        say("RESET?", "SAVE: YES");
        ui.msg_until = ui.reset_ask;
    } else {
        say("KEPT", 0);
    }
}

/* "CLEAR?", "SAVE TO A?", "LOAD A?": the same button again within ASK_MS does it */
static int ask(int what, const char *a, const char *b)
{
    if (ui.ask == what && (int32_t)(plat_ms() - ui.ask_until) < 0) {
        ui.ask = ASK_NONE;
        return 1;
    }
    ui.ask = (uint8_t)what;
    ui.ask_until = plat_ms() + ASK_MS;
    say(a, b);
    ui.msg_until = ui.ask_until;
    return 0;
}

static int save_all(void)
{
    int rc = project_save();
    if (rc == 0)
        ui.dirty = 0;
    if (seq_dirty() && !seq_running() && !seq_armed())
        rc |= seq_save();
    return rc;
}

/* Projects: OCT+ saves the working project into the slot, OCT- loads it (each pressed twice) */
static void slot_button(int b)
{
    char t[24] = "SAVE TO A?", l[24] = "LOAD A?";
    const int s = ui.slot;
    t[8] = l[5] = (char)('A' + s);
    if (b == B_OCTUP) {
        if (!ask(ASK_SAVE, t, "OCT+ AGAIN"))
            return;
        if (seq_running())
            seq_play(0);
        t[0] = (char)('A' + s);
        t[1] = 0;
        if (ct_slot_save(s) == 0 && save_all() == 0)
            say("SAVED TO", t);
        else
            say("NOT SAVED", "FLASH ERROR");
        return;
    }
    if (!ct_slot_used(s)) {
        t[0] = (char)('A' + s);
        t[1] = 0;
        say("EMPTY SLOT", t);
        return;
    }
    if (!ask(ASK_LOAD, l, "OCT- AGAIN"))
        return;
    set_freeze(0);
    if (ct_slot_load(s) == 0) {
        ui.step = 0;
        s_cpy(t, sq.h.name[0] ? sq.h.name : "A PROJECT");
        say("LOADED", t);
    } else {
        say("NOT LOADED", "BAD SLOT");
    }
}

/* SEQ held: OCT- clears the pattern (twice), OCT+ copies it into the next empty one, EDIT undoes */
static void seq_chord(int b)
{
    char t[24] = "CLEAR P", n[8];
    ui.btn_used |= 1u << B_SEQ | 1u << b;
    itoa_u((uint32_t)sq.h.cur + 1u, n);
    if (b == B_OCTDN) {
        s_cpy(t + 7, n);
        s_cpy(t + s_len(t), "?");
        if (ask(ASK_CLEAR, t, "OCT- AGAIN")) {
            seq_clear_pat(sq.h.cur);
            say("CLEARED", n);
        }
    } else if (b == B_OCTUP) {
        const int to = seq_copy_pat();
        if (to < 0) {
            say("NO EMPTY", "PATTERN");
        } else {
            itoa_u((uint32_t)to + 1u, n);
            say("COPIED TO", n);
        }
    } else if (seq_undo() == 0) {
        say("UNDONE", 0);
    } else {
        say("NOTHING", "TO UNDO");
    }
}

static void transport_say(void)
{
    if (!seq_running())
        say("STOPPED", 0);
    else if (seq_counting())
        say("COUNT-IN", 0);
    else
        say(seq_armed() ? "RECORDING" : "PLAYING", 0);
}

static void button(int b)
{
    int v;
    if (ui.reset_ask) {                             /* "RESET?": SAVE resets, any other button keeps */
        ui.reset_ask = 0u;
        if (b == B_SAVE)
            reset_defaults();
        else
            say("KEPT", 0);
        return;
    }
    if (ui.ask && !((ui.ask == ASK_SAVE && b == B_OCTUP) || (ui.ask == ASK_LOAD && b == B_OCTDN))) {
        ui.ask = ASK_NONE;                          /* asked, and another button: kept */
        say("KEPT", 0);
    }
    if (VIEW_BTN[ui.view] == b) {                   /* pressed again: its next page, or round to its first */
        int i = page_pos(ui.view);
        do
            i = (i + 1) % NVIEWS;
        while (VIEW_BTN[PAGE_ORDER[i]] != b);
        set_view(PAGE_ORDER[i]);
        return;
    }
    for (v = 0; v < NVIEWS; v++)
        if (VIEW_BTN[PAGE_ORDER[v]] == b) {
            set_view(PAGE_ORDER[v]);
            return;
        }
    switch (b) {
    case B_EDIT: set_view(PAGE_ORDER[(page_pos(ui.view) + 1) % NVIEWS]); break;
    case B_PLAY:
        seq_play(!seq_running());
        transport_say();
        break;
    case B_REC:
        seq_rec_tap();
        if (!seq_armed())
            say("REC", "OFF");
        else
            say("REC", seq_running() ? "AT THE NEXT LOOP" : "PLAY OR A KEY");
        break;
    case B_OCTDN: case B_OCTUP:
        if (ui.view == V_PROJECTS) {
            slot_button(b);
            break;
        }
        knob_set(P_OCTAVE, proj.par[P_OCTAVE] + (b == B_OCTUP ? 1 : -1));
        say_param(P_OCTAVE);
        break;
    case B_SAVE:
        if (save_all() == 0)
            say("SAVED", 0);
        else
            say("NOT SAVED", "FLASH ERROR");
        break;
    default: break;
    }
}

/* ------------------------------------------------------------- MIDI in --- */
/* where a MIDI note sits: the white key that plays it now, else alternate sides by pitch */
static int midi_pan(int cents)
{
    int w;
    for (w = 0; w < NWHITE; w++)
        if (white_index(w) >= 0 && white_cents(w) == cents) {
            ui.ripple[white_index(w)] = ui.frame + 1u;
            return white_pan(w);
        }
    return ((cents / 100) & 1) ? 40 : -40;
}

static void midi_cc(uint32_t cc, uint32_t v)
{
    const int p = (int)(v * 100u / 127u);
    switch (cc) {
    case 1: knob_set(P_SYMPATHY, p); break;                 /* mod wheel: how much the pan rings along */
    case 64: km_sustain(v >= 64u); break;
    case 72: knob_set(P_SUSTAIN, p); break;
    case 73: knob_set(P_STRIKE, p); break;
    case 74: knob_set(P_HARMONICS, p); break;
    case 91: knob_set(P_REVERB, p); break;
    case 93: knob_set(P_GRAIN, p); break;
    case 94: knob_set(P_DELAY, p); break;
    case 120: case 123: km_damp_all(); break;               /* all sound / notes off */
    default: break;
    }
}

/* MIDI channel 10 (the drums): the steel, General MIDI-ish */
static int drum_hit(uint32_t note)
{
    switch (note) {
    case 35: case 36: return HIT_GU;                       /* kicks */
    case 37: return HIT_RIM;                               /* side stick */
    case 38: case 40: return HIT_THWAP_L;                  /* snares */
    case 39: return HIT_SLAP;                              /* clap */
    case 41: case 43: return HIT_THWAP_R;
    case 42: case 44: return HIT_TAK;                      /* hats */
    case 46: return HIT_TAK_R;
    case 49: case 55: case 57: return HIT_SPLASH;          /* crashes, the splash */
    case 51: case 59: return HIT_GHOST;                    /* rides */
    case 53: return HIT_HARM;                              /* ride bell */
    case 54: return HIT_TAK_L;                             /* tambourine */
    case 69: return HIT_SHAKE_L;                           /* cabasa */
    case 70: return HIT_SHAKE_R;                           /* maracas */
    case 85: return HIT_CASTANET;                          /* castanets (GM2) */
    default: return -1;
    }
}

/* MIDI in (USB and the TRS jack: one queue). Notes strike the note of their pitch (Tuning and Tune
 * applied), velocity is how hard; note off damps it when Release is Damp (the pedal holds it). Channel
 * 10 plays the steel. Clock (any channel) leads the groove. Channel: Setup's MIDI ch (Omni: all). */
static void midi_in(void)
{
    uint32_t pkt;
    while (plat_midi_in(&pkt)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu, type = st & 0xF0u;
        if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu) {   /* clock: the transport follows it */
            const int m = st == 0xF8u ? KM_CLK_TICK : st == 0xFAu ? KM_CLK_START : st == 0xFBu ? KM_CLK_CONTINUE : KM_CLK_STOP;
            km_clock(m);
            seq_midi_clock(m);
            continue;
        }
        if (st >= 0xF0u)
            continue;                                       /* sysex and the rest: not ours */
        if (type == 0x90u && d2 && (st & 0x0Fu) == 9u && drum_hit(d1) >= 0) {
            km_hit(drum_hit(d1), (int)d2, 0);
            ui.hit_t[drum_hit(d1)] = ui.frame + 1u;
            seq_note_on(-1, EV_HIT, drum_hit(d1), (int)d2);  /* (recorded, as the keys are) */
            ui.act_t = plat_ms();
            continue;
        }
        if (proj.par[P_MIDICH] && (st & 0x0Fu) + 1u != (uint32_t)proj.par[P_MIDICH])
            continue;
        touch_midi(type, d1, d2);                       /* (Feel) */
        if (type == 0x90u && d2) {
            int c = tune((int)d1 * 100);
            km_pluck(c, (int)d2, midi_pan(c), 0);
            seq_note_on(-1, EV_PAN, c, (int)d2);
        } else if (type == 0x80u || type == 0x90u) {
            if (proj.par[P_RELEASE])
                km_damp(tune((int)d1 * 100));
        } else if (type == 0xB0u) {
            midi_cc(d1, d2);
        } else if (type == 0xC0u) {
            knob_set(P_METAL, (int)(d1 % MET_N));
        } else if (type == 0xE0u) {
            km_bend((int)(d2 << 7 | d1) - 8192);
        }
        ui.act_t = plat_ms();
    }
}

static void input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), ch, i;
    const uint32_t now = plat_ms();
    int32_t e;
    ch = btn ^ ui.btn;
    ui.btn = btn;
    for (i = 0; i < NB; i++) {
        uint32_t m = 1u << i;
        if (!(ch & m))
            continue;
        if (btn & m) {
            ui.btn_used &= ~m;
            if (i == B_SAVE)
                ui.save_t = now;
            if (i == B_PLAY)
                ui.play_t = now;
            if (i == B_REC)
                ui.rec_t = now;
            if (i == B_PLAY && (btn >> B_REC & 1u)) {  /* REC held, PLAY: armed and started (the count-in) */
                ui.btn_used |= 1u << B_PLAY | 1u << B_REC;
                seq_rec_play();
                transport_say();
            }
            if ((btn >> B_SEQ & 1u) && (i == B_OCTDN || i == B_OCTUP || i == B_EDIT))
                seq_chord((int)i);
        } else if (!(ui.btn_used & m)) {
            button((int)i);                          /* (a tap acts on its release: a hold does something else) */
        }
    }
    if ((btn >> B_PLAY & 1u) && !(ui.btn_used >> B_PLAY & 1u) && now - ui.play_t >= HOLD_MS) {
        set_freeze(!ui.frozen);                      /* PLAY held: the grains frozen, or live again */
        ui.btn_used |= 1u << B_PLAY;
    }
    if ((btn >> B_REC & 1u) && !(ui.btn_used >> B_REC & 1u) && now - ui.rec_t >= HOLD_MS) {
        strike_hit(HIT_MUTE, 1);                     /* REC held: a palm over the top */
        say("PALM", "MUTE");
        ui.btn_used |= 1u << B_REC;
    }
    if ((btn >> B_SAVE & 1u) && !(ui.btn_used >> B_SAVE & 1u) && !ui.reset_ask && now - ui.save_t >= SAVE_HOLD_MS) {
        reset_ask(1);                                /* SAVE held: ask (no save on release) */
        ui.btn_used |= 1u << B_SAVE;
    }
    if (ui.reset_ask && (int32_t)(now - ui.reset_ask) >= 0)
        reset_ask(0);                                /* not answered: kept */
    if (ui.ask && (int32_t)(now - ui.ask_until) >= 0)
        ui.ask = ASK_NONE;
    ch = keys ^ ui.keys;
    if (ui.reset_ask && (ch & keys))                 /* a key while "RESET?" asks: kept (and it plays) */
        reset_ask(0);
    ui.keys = keys;
    if (!keys)
        ui.entry = 0;
    for (i = 0; i < NBLACK; i++)
        if (ch >> BLACK_K[i] & 1u) {
            if (keys >> BLACK_K[i] & 1u)
                black_down((int)i);
            else
                black_up((int)i);
        }
    for (i = 0; i < NWHITE; i++)
        if (ch >> WHITE_K[i] & 1u) {
            if (keys >> WHITE_K[i] & 1u)
                white_down((int)i);
            else
                key_release(WHITE_K[i]);
        }
    if (btn || keys || ch)
        ui.act_t = now;
    midi_in();
    if ((e = plat_enc(EN_SELECT)) != 0) {
        knob_set(P_METAL, (proj.par[P_METAL] + (e > 0 ? 1 : MET_N - 1)) % MET_N);
        say_param(P_METAL);
    }
    if ((e = plat_enc(EN_ALGO)) != 0) {
        knob_set(P_SCALE, (proj.par[P_SCALE] + (e > 0 ? 1 : KM_NSCALE - 1)) % KM_NSCALE);
        say_param(P_SCALE);
    }
    if ((e = plat_enc(EN_PRESET)) != 0) {
        knob_set(P_KEY, (proj.par[P_KEY] + (e > 0 ? 1 : 11)) % 12);
        say_param(P_KEY);
    }
    for (i = 0; i < 4; i++)
        if ((e = plat_enc(EN_K1 + (int)i)) != 0) {
            const int k = view_knob((int)i);
            turn(EN_K1 + (int)i, k, e);
            if (k == P_SCALE || k == P_KEY)
                say_param(k);
            ui.touched = (int8_t)i;
            ui.touch_until = now + 1200u;
            ui.act_t = now;
        }
    if (ct_loaded())                                 /* (a slot loaded, here or over the web) */
        mark_dirty();
    if (ui.step >= sq.h.pat[sq.h.cur].len)
        ui.step = (uint8_t)(sq.h.pat[sq.h.cur].len - 1);
    if (ui.sec >= SEQ_NSEC)
        ui.sec = 0;
    pan_sync();
    seq_poll();                                      /* the clock: what's due in the next moments */
}

/* ------------------------------------------------------------- screen --- */
/* three bands: the header (28 px), the instrument (144), the knobs (68) */
#define HDR_H 28
#define MAIN_Y 28
#define MAIN_H 144
#define KNB_Y 172
#define KNB_H 68

static uint32_t hash(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }

static void cv_round(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    static const uint8_t IN[7][6] = {{0}, {1}, {1, 0}, {2, 1, 0}, {2, 1, 0, 0}, {3, 2, 1, 0, 0}, {4, 2, 1, 1, 0, 0}};
    int32_t j;
    if (r > 6)
        r = 6;
    if (r > h / 2)
        r = h / 2;
    cv_rect(x, y + r, w, h - 2 * r, c);
    for (j = 0; j < r; j++) {
        int32_t d = IN[r][j];
        cv_rect(x + d, y + j, w - 2 * d, 1, c);
        cv_rect(x + d, y + h - 1 - j, w - 2 * d, 1, c);
    }
}

/* the half-width of a disc of radius r at row y (0 past it) */
static int32_t disc_w(int32_t r, int32_t y)
{
    int32_t x = 0;
    if (y < 0)
        y = -y;
    if (y > r)
        return -1;
    while ((x + 1) * (x + 1) + y * y <= r * r)
        x++;
    return x;
}

static void cv_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t y;
    for (y = -r; y <= r; y++) {
        int32_t x = disc_w(r, y);
        cv_rect(cx - x, cy + y, 2 * x + 1, 1, c);
    }
}

/* a ring from radius r0 out to r1; side: -1 its left half, 1 its right, 0 both */
static void cv_ring(int32_t cx, int32_t cy, int32_t r0, int32_t r1, int side, uint16_t c)
{
    int32_t y;
    for (y = -r1; y <= r1; y++) {
        int32_t xo = disc_w(r1, y), xi = disc_w(r0, y) + 1;
        if (xo < 0 || xi > xo)
            continue;
        if (side <= 0)
            cv_rect(cx - xo, cy + y, xo - xi + 1, 1, c);
        if (side >= 0)
            cv_rect(cx + xi, cy + y, xo - xi + 1, 1, c);
    }
}

static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}

static uint16_t mix(uint16_t a, uint16_t b, int t)   /* t 0..16: a -> b */
{
    int r, g, bl;
    t = t < 0 ? 0 : t > 16 ? 16 : t;
    r = ((a >> 11) * (16 - t) + (b >> 11) * t) / 16;
    g = (((a >> 5) & 63) * (16 - t) + ((b >> 5) & 63) * t) / 16;
    bl = ((a & 31) * (16 - t) + (b & 31) * t) / 16;
    return (uint16_t)(r << 11 | g << 5 | bl);
}

/* the transport, for the header: "REC 2.3", "WAIT 1.4", "P3 1.2" (the pattern playing), "S2 1.2" (the
 * song's section), "IN 3" (the count-in's beats to go), "REC" (armed); "" stopped. 1: in the accent */
static int transport_text(char *t)
{
    const int pos = seq_pos(), n = seq_counting();
    char *q = t;
    *t = 0;
    if (!seq_running()) {
        if (seq_armed())
            s_cpy(t, "REC");
        return 1;
    }
    if (n) {
        s_cpy(t, "IN ");
        itoa_u((uint32_t)n, t + 3);
        return 1;
    }
    if (seq_recording())
        s_cpy(q, "REC ");
    else if (seq_waiting())
        s_cpy(q, "WAIT ");
    else {
        *q++ = seq_section() >= 0 ? 'S' : 'P';
        itoa_u((uint32_t)(seq_section() >= 0 ? seq_section() : seq_playing_pat()) + 1u, q);
        s_cpy(q + s_len(q), " ");
    }
    q = t + s_len(t);
    itoa_u((uint32_t)(pos / (4 * SEQ_PPQ)) + 1u, q);
    q += s_len(q);
    *q++ = '.';
    itoa_u((uint32_t)(pos / SEQ_PPQ % 4) + 1u, q);
    return seq_armed();
}

static int groove_on(void) { return seq_running() && proj.par[P_GROOVE] && !seq_counting(); }
static int on_beat(void) { return seq_pos() % SEQ_PPQ < SEQ_PPQ / 4; }

static void draw_header(void)
{
    char t[24], tt[16];
    int msg = plat_ms() < ui.msg_until, hot = transport_text(tt), i;
    uint32_t h = hash(hash(2166136261u, ui.view | (uint32_t)ui.frozen << 8 | (uint32_t)proj.par[P_RELEASE] << 9 |
                                            (uint32_t)proj.par[P_THEME] << 14 | (uint32_t)proj.par[P_BLACK] << 15 |
                                            (uint32_t)groove_on() << 12 | (uint32_t)(groove_on() && on_beat()) << 13 |
                                            (uint32_t)ui.dirty << 10 | (uint32_t)msg << 11 | (uint32_t)hot << 16),
                      (uint32_t)proj.par[P_KEY] | (uint32_t)proj.par[P_SCALE] << 4 | (uint32_t)(proj.par[P_OCTAVE] + 2) << 9);
    for (i = 0; tt[i]; i++)
        h = hash(h, (uint32_t)tt[i]);
    if (msg)
        h = hash(hash(h, (uint32_t)ui.msg[0][0] << 8 | ui.msg[0][1]), ui.msg_until ^ (uint32_t)ui.msg[1][0] << 3 ^ (uint32_t)ui.msg[1][1]);
    if (ui.view == V_SETUP)
        h = hash(h, plat_cpu_pct() | plat_xruns() << 8);
    if (h == ui.sig[0])
        return;
    ui.sig[0] = h;
    cv_begin(240, HDR_H, K_BG);
    if (msg) {
        int32_t x = cv_text(10, 6, &FONT_B, ui.msg[0], K_TEXT);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], K_ACC);
    } else {
        int32_t x = 232, w, right;
        cv_text(10, 6, &FONT_B, VIEW_NAME[ui.view], K_TEXT);
        if (ui.view == V_SETUP) {                     /* the audio load, for a check on the hardware */
            itoa_u(plat_cpu_pct(), t);
            w = s_len(t);
            t[w++] = '%';
            t[w] = 0;
            if (plat_xruns()) {
                t[w++] = ' ';
                t[w++] = 'X';
                itoa_u(plat_xruns(), t + w);
            }
            cv_text(x - text_w(&FONT_S, t), 6, &FONT_S, t, K_DIM);
            x -= text_w(&FONT_S, t) + 6;
            x -= text_w(&FONT_S, "CPU");
            cv_text(x, 6, &FONT_S, "CPU", K_DIM);
            for (w = 0; OM_VERSION[w] && OM_VERSION[w] != ' ' && w < 15; w++)   /* the version: "0.1.0", "DEV" */
                t[w] = OM_VERSION[w];
            t[w] = 0;
            x -= text_w(&FONT_S, t) + 12;
            cv_text(x, 6, &FONT_S, t, K_DIM);
        } else if (tt[0]) {                          /* the transport, in place of the pan's name */
            x -= text_w(&FONT_S, tt);
            cv_text(x, 6, &FONT_S, tt, hot ? K_ACC : K_TEXT);
        } else {                                     /* "D Kurd", and its octave when shifted */
            pan_name(t);
            if (proj.par[P_OCTAVE]) {
                w = s_len(t);
                t[w++] = ' ';
                km_param_text(P_OCTAVE, proj.par[P_OCTAVE], t + w);
            }
            x -= text_w(&FONT_S, t);
            cv_text(x, 6, &FONT_S, t, K_TEXT);
        }
        right = x - 4;                               /* (the chips stop short of it) */
        x = 18 + text_w(&FONT_B, VIEW_NAME[ui.view]);
        {   /* the button's pages, when it has more than one: a dot each, this one filled */
            int n = 0, me = 0, v;
            for (v = 0; v < NVIEWS; v++)
                if (VIEW_BTN[PAGE_ORDER[v]] == VIEW_BTN[ui.view]) {
                    if (PAGE_ORDER[v] == ui.view)
                        me = n;
                    n++;
                }
            if (n > 1) {
                for (v = 0; v < n; v++)
                    cv_round(x - 4 + v * 9, 12, 6, 6, 3, v == me ? K_TEXT : K_LINE);
                x += n * 9 + 2;
            }
        }
        if (ui.frozen && x + text_w(&FONT_XS, "FRZ") + 10 <= right) {
            w = text_w(&FONT_XS, "FRZ") + 10;
            cv_round(x, 6, w, 16, 6, K_TEAL_T);
            cv_text(x + 5, 7, &FONT_XS, "FRZ", K_TEAL);
            x += w + 4;
        }
        if (groove_on() && x + text_w(&FONT_XS, "GRV") + 10 <= right) {   /* the groove: bright on the beat */
            int beat = on_beat();
            w = text_w(&FONT_XS, "GRV") + 10;
            cv_round(x, 6, w, 16, 6, beat ? K_ACC : K_ACC_T);
            cv_text(x + 5, 7, &FONT_XS, "GRV", beat ? K_WHITE : K_ACC);
            x += w + 4;
        }
        if (proj.par[P_RELEASE] && x + text_w(&FONT_XS, "DAMP") + 10 <= right) {
            w = text_w(&FONT_XS, "DAMP") + 10;
            cv_round(x, 6, w, 16, 6, K_ACC_T);
            cv_text(x + 5, 7, &FONT_XS, "DAMP", K_ACC);
            x += w + 4;
        }
        if (ui.dirty && x + 5 <= right)
            cv_round(x, 12, 5, 5, 2, K_DIM);
    }
    cv_blit(0, 0);
}

/* how loud the note at `cents` rings now (0..1) */
static float note_level(int cents)
{
    int v;
    float l = 0.0f;
    for (v = 0; v < KM_NVOICE; v++)
        if (km_voice_cents[v] == cents && km_voice_level[v] > l)
            l = km_voice_level[v];
    return l * 2.5f > 1.0f ? 1.0f : l * 2.5f;
}

/* frames since a ripple started, or a large number */
#define RIPPLE_FR 14u
static uint32_t ripple_age(uint32_t t) { return t ? ui.frame + 1u - t : 999u; }

/* The pan from above. The shell: shaded as a dome, its light from the upper left, the rim that glows on
 * the side a thwap lands. The ding in the middle, the fields around it climbing in a zigzag from the
 * player's side (the bottom) to the top, the low ones the biggest. Each field shows its note and glows
 * as it rings; a strike sends a ripple out from it. */
typedef struct {
    int16_t x, y, r;
} spot_t;

/* where a field sits: q-th (0..m-1) of the m fields of a ring of radius `ring` round (cx, cy), set evenly
 * and climbing in the zigzag from the player's side (the bottom) to the top, the first on side `first`
 * (+1 right, as makers lay them out; -1 left, Pan rev): q at (2 (q / 2) + 1) x 180 / m degrees from
 * straight down. With an odd m the last sits at the top. */
static spot_t ring_spot(int q, int m, int first, int32_t cx, int32_t cy, int32_t ring, int r)
{
    const int side = (q & 1) ? -first : first;
    const float a = (float)(2 * (q / 2) + 1) * FM_PI / (float)m, sx = (float)ring * fm_sinf(a), sy = (float)ring * fm_cosf(a);
    spot_t s;
    s.x = (int16_t)(cx + (int32_t)((float)side * sx + (side < 0 ? -0.5f : 0.5f)));
    s.y = (int16_t)(cy + (int32_t)(sy + (sy < 0.0f ? -0.5f : 0.5f)));
    s.r = (int16_t)r;
    return s;
}

static void draw_shell(int32_t cx, int32_t cy, int32_t R, uint16_t dark, uint16_t light, int side_glow[2])
{
    int32_t r;
    cv_disc(cx, cy, R + 1, mix(K_BG, dark, 10));
    for (r = R; r > 0; r -= 3) {                      /* the dome: lighter toward its crown, which leans left and up */
        int32_t off = (R - r) / 4;
        cv_disc(cx - off, cy - off - (R - r) / 12, r, mix(dark, light, 3 + (R - r) * 11 / R));
    }
    cv_ring(cx, cy, R - 3, R, 0, mix(dark, light, 5));   /* the rim seam */
    if (side_glow[0])
        cv_ring(cx, cy, R - 4, R + 1, -1, mix(mix(dark, light, 5), K_GLOW, side_glow[0]));
    if (side_glow[1])
        cv_ring(cx, cy, R - 4, R + 1, 1, mix(mix(dark, light, 5), K_GLOW, side_glow[1]));
}

static void draw_field(spot_t s, int cents, int age, float lv, int ding, uint16_t dark, uint16_t light, uint16_t ink,
                       int label)
{
    const int t = (int)(lv * 16.0f + 0.5f);
    uint16_t base = mix(dark, light, ding ? 9 : 7), fill = mix(base, K_GLOW, t * 3 / 4);
    cv_disc(s.x, s.y, s.r + 1, mix(dark, light, 3));  /* the field's border, hammered in */
    cv_disc(s.x, s.y, s.r, fill);
    if (ding) {                                       /* the ding's dome */
        cv_disc(s.x - 1, s.y - 1, s.r * 6 / 10, mix(mix(base, light, 6), K_GLOW, t * 3 / 4));
    } else if (!label && s.r >= 5) {                  /* the dimple (a labeled field shows its note there) */
        cv_ring(s.x, s.y, s.r * 4 / 10, s.r * 4 / 10 + 1, 0, mix(fill, dark, 4));
    }
    if (age < (int)RIPPLE_FR) {                       /* a ripple, out from the strike */
        int32_t rr = s.r + 2 + age * 3 / 2;
        cv_ring(s.x, s.y, rr - 1, rr, 0, mix(K_GLOW, base, age * 16 / (int)RIPPLE_FR));
    }
    if (label) {                                      /* (2: an extra field's, a little quieter) */
        const char *nm = note_name(cents);
        text_c(s.x, s.y - 8, &FONT_XS, nm, t > 8 ? mix(ink, dark, 12) : label == 2 ? mix(ink, dark, 5) : ink);
    }
}

/* The pan, with Duo the second pan beside it, centered in the space y0..y1 */
static void draw_pan(int32_t y0, int32_t y1)
{
    int n[KM_NPAN], own = pan_notes(n), all = notes_reached(), i;
    const int duo = duo_on(), first = proj.par[P_LAYOUT] == LAY_PANREV ? -1 : 1;
    const int32_t R = duo ? 52 : 55, cx = duo ? 90 : 120, cy = (y0 + y1) / 2;   /* (the second pan beside it: over) */
    const uint16_t dark = METAL->dark, light = METAL->light, ink = METAL->ink;
    int glow[2];
    for (i = 0; i < 2; i++)                           /* the rim, where a thwap or a tak just landed */
        glow[i] = ui.rim[i];
    draw_shell(cx, cy, R, dark, light, glow);
    {   /* the Gu: the air inside, breathing round the ding */
        int g = ui.rim[2];
        if (g > 1)
            cv_ring(cx, cy, 15 + g / 4, 16 + g / 4, 0, mix(mix(dark, light, 8), K_GLOW, g));
    }
    /* every note the keys play: the ding, and round it in one ring, evenly spaced, the fields climbing
     * in the zigzag, the low ones the biggest; the notes past the pan's own fields (an extended pan's)
     * carry the ring on, their labels a little quieter */
    for (i = 0; i < all; i++) {
        int c = tune(n[i]);
        spot_t s = i ? ring_spot(i - 1, all - 1, first, cx, cy, R * (all > 10 ? 58 : 54) / 100,
                                 all > 10 ? 8 - (i - 1) / 5 : 9 - (i - 1) / 4)
                     : (spot_t){(int16_t)cx, (int16_t)cy, 13};
        draw_field(s, c, (int)ripple_age(ui.ripple[i]), note_level(c), i == 0, dark, light, ink, i < own ? 1 : 2);
    }
    if (duo) {   /* the second pan, smaller, to the right: its ding and eight fields, evenly round it */
        const int32_t dx = 200, dy = cy - 6, dr = 34;
        draw_shell(dx, dy, dr, dark, light, (int[2]){0, 0});
        for (i = 0; i < KM_NDUO; i++) {
            spot_t s = i ? ring_spot(i - 1, KM_NDUO - 1, first, dx, dy, dr * 69 / 100, 7)
                         : (spot_t){(int16_t)dx, (int16_t)dy, 8};
            int c = duo_cents(i);
            draw_field(s, c, (int)ripple_age(ui.ripple[KM_NPAN + i]), note_level(c), i == 0, dark, light, ink, 1);
        }
        {
            char t[16];
            km_duo_name(proj.par[P_SCALE], proj.par[P_KEY], duo_other(), proj.par[P_DUO], t);
            text_c(dx, dy + dr + 4, &FONT_XS, t, K_DIM);
        }
    }
}

/* the black keys, as a strip: the steel's kit in the middle, the hand percussion round it. With Duo
 * only the end keys: the second pan between shows its own notes, and its keys' room stays empty */
static void draw_black(int32_t y0)
{
    int b;
    for (b = 0; b < NBLACK; b++) {
        char t[12];
        int32_t x = 4 + b * 20 + ((b >= 3) + (b >= 5) + (b >= 8) + (b >= 10)) * 3;   /* grouped as the keys are */
        int on = (ui.keys >> BLACK_K[b] & 1u) != 0, hand = duo_on() || b < 2 || b > NBLACK - 3;
        uint16_t bg = hand ? K_ACC_T : K_TEAL_T, fg = hand ? K_ACC : K_TEXT;
        const int k = black_hit(b);
        if (is_duo_key(b))
            continue;
        s_cpy(t, HIT_NAME[k]);
        if (ripple_age(ui.hit_t[k]) < 12u)
            on = 1;
        if (on) {
            bg = hand ? K_ACC : K_TEAL;
            fg = K_WHITE;
        }
        cv_round(x, y0, 18, 26, 5, bg);
        if (text_w(&FONT_XS, t) > 18 && t[2]) {       /* two lines: "Th" "wp" */
            char a[3] = {t[0], t[1], 0};
            text_c(x + 9, y0 + 1, &FONT_XS, a, fg);
            text_c(x + 9, y0 + 12, &FONT_XS, t + 2, fg);
        } else {
            text_c(x + 9, y0 + 6, &FONT_XS, t, fg);
        }
    }
}

/* The sequence's pages show it in place of the pans (the black keys stay over it): Steps the pattern as a
 * grid of its steps, Song its sections, Projects the slots and the project's name. */
static int sheet_page(void) { return ui.view == V_STEPS || ui.view == V_SONG || ui.view == V_PROJECTS; }

static uint32_t sheet_hash(uint32_t h)
{
    int i, a, b;
    h = hash(h, (uint32_t)ui.view | (uint32_t)ui.step << 8 | (uint32_t)ui.sec << 16 | (uint32_t)ui.slot << 24);
    h = hash(h, (uint32_t)sq.h.cur | (uint32_t)sq.h.pat[sq.h.cur].len << 8 | (uint32_t)seq_playing_pat() << 16 |
                    (uint32_t)seq_running() << 24 | (uint32_t)sq.h.song << 25);
    h = hash(h, (uint32_t)sq.h.nev | (uint32_t)ui.npos << 16 | (uint32_t)ui.ask << 24);
    if (seq_running())
        h = hash(h, (uint32_t)(seq_pos() / seq_step_ticks(seq_playing_pat())) | (uint32_t)seq_section() << 16);
    if (ui.view == V_STEPS) {
        a = 0;
        b = 0;
        for (i = 0; i < sq.h.nev; i++)               /* (the pattern's events) */
            if (sq.ev[i].pat == sq.h.cur) {
                h = hash(hash(h, (uint32_t)sq.ev[i].at | (uint32_t)sq.ev[i].val << 16), sq.ev[i].len);
                a++;
            }
        h = hash(h, (uint32_t)a + (uint32_t)b);
    } else if (ui.view == V_SONG) {
        for (i = 0; i < SEQ_NSEC; i++)
            h = hash(h, (uint32_t)sq.h.sec[i].pat | (uint32_t)sq.h.sec[i].rep << 8);
    } else {
        for (i = 0; i < SEQ_NAME; i++)
            h = hash(h, (uint32_t)sq.h.name[i]);
        for (i = 0; i < PLAT_NSLOT; i++)
            h = hash(h, (uint32_t)ct_slot_used(i) | (uint32_t)ct_slot_name(i)[0] << 1 | (uint32_t)ct_slot_name(i)[1] << 9);
    }
    return h;
}

/* Steps: up to four rows of sixteen, a beat's four together; a note teal, a hit gold, the cursor ringed,
 * the step playing lit */
static void draw_steps(int32_t y0)
{
    const int p = sq.h.cur, len = sq.h.pat[p].len,
              now = seq_running() && seq_playing_pat() == p ? seq_pos() / seq_step_ticks(p) : -1;
    char t[24], v[16];
    int s, e;
    s_cpy(t, "Pattern ");
    itoa_u((uint32_t)p + 1u, t + s_len(t));
    cv_text(8, y0, &FONT_S, t, K_TEXT);
    itoa_u((uint32_t)len, t);
    s_cpy(t + s_len(t), " x ");
    km_param_text(P_ARATE, sq.h.pat[p].div, t + s_len(t));
    cv_text(232 - text_w(&FONT_S, t), y0, &FONT_S, t, K_DIM);
    {   /* the rows as tall as the room allows (a bar: one row of tall cells) */
        const int rows = (len + 15) / 16, ch = rows == 1 ? 34 : rows == 2 ? 24 : rows == 3 ? 15 : 11;
        const int32_t top = y0 + 22 + (64 - rows * ch - (rows - 1) * 5) / 2;
        uint32_t held[2] = {0, 0};                   /* the steps a longer note still rings over */
        for (e = 0; e < sq.h.nev; e++)
            if (sq.ev[e].pat == p && sq.ev[e].val >> 14 != EV_HIT) {
                int a = (sq.ev[e].at + SEQ_SUB / 2) / SEQ_SUB, z = a + (sq.ev[e].len + SEQ_SUB / 2) / SEQ_SUB;
                for (a++; a < z && a < SEQ_MAXLEN; a++)
                    held[a >> 5] |= 1u << (a & 31);
            }
        for (s = 0; s < len; s++) {
            const int c = s % 16, r = s / 16, n = seq_step_count(p, s);
            const int32_t x = 9 + c * 13 + (c / 4) * 4, y = top + r * (ch + 5);
            uint16_t col = held[s >> 5] >> (s & 31) & 1u ? mix(K_PANEL, K_TEAL, 5) : K_PANEL;
            e = seq_step_ev(p, s);
            if (e >= 0)
                col = ev_kind_of(e) == EV_HIT ? mix(K_ACC_T, K_ACC, 9) : n > 1 ? mix(K_TEAL, K_TEXT, 7) : K_TEAL;
            if (s == now)
                col = K_GLOW;
            if (s == ui.step)
                cv_round(x - 2, y - 2, 15, ch + 4, 3, K_ACC);
            cv_round(x, y, 11, ch, 2, col);
        }
    }
    e = seq_step_ev(p, ui.step);                     /* the cursor's step, below */
    s_cpy(t, "Step ");
    itoa_u((uint32_t)ui.step + 1u, t + 5);
    cv_text(8, y0 + 90, &FONT_S, t, K_DIM);
    kv_text(KV_NOTE, v);
    if (e >= 0 && seq_step_count(p, ui.step) > 1)
        s_cpy(v + s_len(v), " +");
    cv_text(70, y0 + 90, &FONT_S, v, e >= 0 ? K_TEXT : K_LINE);
}

/* Song: five of its sections round the cursor; the one playing marked */
static void draw_song(int32_t y0)
{
    int first = ui.sec - 2, i;
    if (first > SEQ_NSEC - 5)
        first = SEQ_NSEC - 5;
    if (first < 0)
        first = 0;
    for (i = 0; i < 5; i++) {
        const int s = first + i, cur = s == ui.sec;
        const int32_t y = y0 + 4 + i * 22;
        char t[16];
        if (cur)
            cv_round(6, y - 2, 228, 20, 5, K_PANEL);
        if (seq_section() == s)
            cv_disc(16, y + 8, 3, K_GLOW);
        itoa_u((uint32_t)s + 1u, t);
        cv_text(26, y, &FONT_S, t, K_DIM);
        if (sq.h.sec[s].pat >= SEQ_END) {
            cv_text(66, y, &FONT_S, "End", cur ? K_ACC : K_LINE);
            continue;
        }
        s_cpy(t, "Pattern ");
        itoa_u((uint32_t)sq.h.sec[s].pat + 1u, t + 8);
        cv_text(66, y, &FONT_S, t, cur ? K_ACC : K_TEXT);
        t[0] = 'x';
        itoa_u(sq.h.sec[s].rep, t + 1);
        cv_text(180, y, &FONT_S, t, cur ? K_ACC : K_DIM);
    }
}

/* Projects: the four slots (each its project's name), then the working project's, its letter marked */
static void draw_projects(int32_t y0)
{
    int i;
    int32_t x;
    for (i = 0; i < PLAT_NSLOT; i++) {
        const int32_t y = y0 + 2 + i * 21;
        const int cur = i == ui.slot;
        char t[2] = {(char)('A' + i), 0};
        const char *nm = ct_slot_used(i) ? (ct_slot_name(i)[0] ? ct_slot_name(i) : "UNNAMED") : "empty";
        if (cur)
            cv_round(6, y - 2, 228, 19, 5, K_PANEL);
        cv_text(16, y, &FONT_S, t, cur ? K_ACC : K_DIM);
        cv_text(40, y, &FONT_S, nm, ct_slot_used(i) ? (cur ? K_ACC : K_TEXT) : K_LINE);
    }
    x = cv_text(16, y0 + 90, &FONT_S, "Name", K_DIM) + 12;
    for (i = 0; i < SEQ_NAME - 1; i++) {             /* a cell a letter, the one K2 points at underlined */
        char t[2] = {sq.h.name[i] && i < (int)sizeof sq.h.name ? sq.h.name[i] : ' ', 0};
        int32_t j;
        for (j = 0; j < i && sq.h.name[j]; j++)
            ;
        if (j < i)
            t[0] = ' ';
        text_c(x + 5, y0 + 90, &FONT_S, t, K_TEXT);
        cv_rect(x, y0 + 106, 10, 2, i == ui.npos ? K_ACC : K_LINE);
        x += 12;
    }
}

static void draw_main(void)
{
    uint32_t h = hash(2166136261u, (uint32_t)proj.par[P_LAYOUT] | (uint32_t)proj.par[P_SCALE] << 4 |
                                       (uint32_t)proj.par[P_KEY] << 8 | (uint32_t)(octave_now() + 4) << 12 |
                                       (uint32_t)proj.par[P_BLACK] << 16 | (uint32_t)proj.par[P_METAL] << 18 |
                                       (uint32_t)proj.par[P_DUO] << 21 | (uint32_t)proj.par[P_TUNING] << 27 |
                                       (uint32_t)proj.par[P_THEME] << 30);
    int v, moving = 0;
    for (v = 0; v < KM_NVOICE; v++)
        h = hash(h, (uint32_t)(km_voice_level[v] * 40.0f) | (uint32_t)(uint16_t)km_voice_cents[v] << 8);
    {
        const float lv[3] = {km_shell_level[0] * 10.0f, km_shell_level[1] * 10.0f, km_cavity_level * 6.0f};
        for (v = 0; v < 3; v++) {
            int g = (int)((lv[v] > 1.0f ? 1.0f : lv[v]) * 16.0f), f = ui.rim[v] * 13 / 16;
            ui.rim[v] = (uint8_t)(g > f ? g : f);
        }
        h = hash(h, (uint32_t)ui.rim[0] | (uint32_t)ui.rim[1] << 8 | (uint32_t)ui.rim[2] << 16);
    }
    h = hash(h, ui.keys);
    for (v = 0; v < NRIPPLE; v++)
        moving |= ripple_age(ui.ripple[v]) < RIPPLE_FR;
    for (v = 0; v < HIT_N; v++)
        moving |= ripple_age(ui.hit_t[v]) < 13u;
    if (moving)
        h = hash(h, ui.frame);                        /* the ripples spread: every frame while they do */
    if (sheet_page())
        h = sheet_hash(h);
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    draw_black(1);                                   /* the black keys over the pans (the keys' upper row) */
    if (ui.view == V_STEPS)
        draw_steps(32);
    else if (ui.view == V_SONG)
        draw_song(32);
    else if (ui.view == V_PROJECTS)
        draw_projects(32);
    else
        draw_pan(29, MAIN_H);
    cv_blit(0, MAIN_Y);
}

static void draw_knobs(void)
{
    uint32_t h = hash(2166136261u, ui.view | (uint32_t)ui.touched << 8 | (uint32_t)proj.par[P_THEME] << 16 |
                                       (uint32_t)proj.par[P_BLACK] << 20), i;
    int now_touch = plat_ms() < ui.touch_until;
    for (i = 0; i < 4; i++) {
        const int k = view_knob((int)i);
        h = hash(h, k == K_NONE ? 0xFFFFu : (uint32_t)(uint16_t)knob_val(k) | (uint32_t)k << 16 | (uint32_t)knob_off(k) << 24);
        if (k == KV_NOTE || k == KV_STEP || k == KV_NCHR)   /* (a step's note: the same index, another note) */
            h = hash(h, (uint32_t)cur_ev() | (uint32_t)(cur_ev() >= 0 ? sq.ev[cur_ev()].val : 0u) << 16);
    }
    h = hash(h, (uint32_t)now_touch | (uint32_t)proj.par[P_SCALE] << 4 | (uint32_t)proj.par[P_KEY] << 8 |
                    (uint32_t)proj.par[P_LAYOUT] << 12);
    if (h == ui.sig[2])
        return;
    ui.sig[2] = h;
    cv_begin(240, KNB_H, K_BG);
    cv_round(4, 2, 232, KNB_H - 6, 6, K_PANEL);
    for (i = 0; i < 4; i++) {
        int k = view_knob((int)i), lo, hi, v;
        int32_t x = (int32_t)i * 60, cx = x + 30, bw;
        int hot = now_touch && ui.touched == (int)i;
        char t[16];
        if (i)
            cv_rect(x, 14, 1, KNB_H - 30, K_LINE);
        if (k == K_NONE)
            continue;
        if (knob_off(k)) {                           /* nothing to set now: the name grayed, no value, no bar */
            text_c(cx, 6, &FONT_XS, knob_name(k), K_LINE);
            text_c(cx, 26, &FONT_B, "--", K_LINE);
            continue;
        }
        lo = knob_lo(k);
        hi = knob_hi(k);
        v = knob_val(k);
        text_c(cx, 6, &FONT_XS, knob_name(k), hot ? K_TEXT : K_DIM);
        value_text(k, t);
        {   /* the value: big, unless it has lower case (the big face has none) or is long */
            const char *q = t;
            int lower = 0;
            for (; *q; q++)
                lower |= *q >= 'a' && *q <= 'z';
            if (lower || text_w(&FONT_M, t) > 56) {
                if (text_w(&FONT_B, t) > 58) {        /* "Ab Celtic": the key above the name */
                    char *sp = t;
                    while (*sp && *sp != ' ')
                        sp++;
                    if (*sp) {
                        *sp = 0;
                        text_c(cx, 20, &FONT_XS, t, hot ? K_ACC : K_TEXT);
                        text_c(cx, 32, &FONT_XS, sp + 1, hot ? K_ACC : K_TEXT);
                    } else {
                        text_c(cx, 26, &FONT_XS, t, hot ? K_ACC : K_TEXT);
                    }
                } else {
                    text_c(cx, 26, &FONT_B, t, hot ? K_ACC : K_TEXT);
                }
            } else {
                text_c(cx, 22, &FONT_M, t, hot ? K_ACC : K_TEXT);
            }
        }
        bw = hi > lo ? (int32_t)(44 * (v - lo) / (hi - lo)) : 0;
        cv_round(cx - 22, 50, 44, 6, 3, K_LINE);
        if (lo < 0) {                                /* centered values: a bar from the middle */
            int32_t m = cx - 22 + 44 * (0 - lo) / (hi - lo), e = cx - 22 + bw;
            cv_round(e < m ? e : m, 50, (e < m ? m - e : e - m) + 6, 6, 3, hot ? K_ACC : K_TEAL);
        } else if (bw) {
            cv_round(cx - 22, 50, bw < 6 ? 6 : bw, 6, 3, hot ? K_ACC : K_TEAL);
        }
    }
    cv_blit(0, KNB_Y);
}

/* the ding's keys, marked by a dim LED (Pan, Pan rev: the middle two; Linear: the first), and with Duo
 * the second pan's ding */
static uint32_t ding_keys(void)
{
    uint32_t m = 0;
    int w;
    for (w = 0; w < NWHITE; w++)
        if (!white_index(w))
            m |= 1u << WHITE_K[w];
    for (w = B_DUO0; w < B_DUO0 + KM_NDUO; w++)      /* and the second pan's, with Duo */
        if (is_duo_key(w) && !duo_index(w))
            m |= 1u << BLACK_K[w];
    return m;
}

/* the buttons: the page's lit; ARP flashing on the beat while the arp runs; PLAY lit while the transport
 * runs (dark on each beat) or, stopped, while the grains are frozen; REC lit while recording, blinking
 * while it waits (armed, the count-in, the loop's end) */
static void leds(void)
{
    uint32_t b = VIEW_BTN[ui.view] < NB ? 1u << VIEW_BTN[ui.view] : 0u, k = 0, i;
    const uint32_t beat = seq_running() ? (uint32_t)seq_pos() % SEQ_PPQ : seq_ticks() % SEQ_PPQ;
    if (seq_arp_on() && beat < SEQ_PPQ / 4u)
        b ^= 1u << B_ARP;
    if (seq_running() ? beat >= SEQ_PPQ / 8u : ui.frozen)
        b |= 1u << B_PLAY;
    if (seq_recording() || (seq_armed() && beat < SEQ_PPQ / 2u))
        b |= 1u << B_REC;
    if (seq_held()) {                                /* SEQ held: the patterns (the one chosen lit, the full dim) */
        uint32_t d = 0;
        for (i = 0; i < NWHITE; i++)
            if (seq_used((int)i))
                d |= 1u << WHITE_K[i];
        k = 1u << WHITE_K[sq.h.cur];
        if (seq_next_pat() >= 0 && beat < SEQ_PPQ / 2u)
            k |= 1u << WHITE_K[seq_next_pat()];
        plat_leds(b, k | ui.keys, d & ~k);
        return;
    }
    for (i = 0; i < NWHITE; i++)                     /* the notes that ring, the steel just struck */
        if (km_white_hit((int)i) >= 0 ? ripple_age(ui.hit_t[km_white_hit((int)i)]) < 12u
                                      : note_level(white_cents((int)i)) > 0.08f)
            k |= 1u << WHITE_K[i];
    for (i = 0; i < NBLACK; i++)
        if (is_duo_key((int)i) ? note_level(duo_cents(duo_index((int)i))) > 0.08f : ripple_age(ui.hit_t[black_hit((int)i)]) < 12u)
            k |= 1u << BLACK_K[i];
    k |= ui.keys;
    plat_leds(b, k, ding_keys());
}

/* AUTOSAVE: a few seconds after a change, when nothing rings and nothing is touched: a flash erase
 * silences the audio for a moment, so it waits for the output itself to be quiet (reverb and echo
 * tails too: cutting one was a click) */
#define AUTOSAVE_QUIET 4000u
static void autosave(void)
{
    uint32_t now = plat_ms(), i;
    if ((!ui.dirty && !seq_dirty()) || ui.btn || ui.keys || now - ui.act_t < AUTOSAVE_QUIET || ui.frozen ||
        seq_running() || seq_armed() || seq_arp_held() || km_out_level > 0.0005f)
        return;                                      /* (never while the transport runs) */
    for (i = 0; i < KM_NVOICE; i++)
        if (km_voice_level[i] > 0.0f)
            return;
    if (ui.dirty && project_save() == 0)
        ui.dirty = 0;
    if (seq_dirty())
        seq_save();
}

void ui_init(void)
{
    char t[24];
    memset(&ui, 0, sizeof ui);
    memset(&tch, 0, sizeof tch);
    ui.touched = -1;
    ui.rng = 0x2545F491u;
    lcd_fill(0, 0, 240, 240, K_BG);
    seq_init();
    pan_sync();
    pan_name(t);
    s_upper(t);
    say("HP-1", t);                                  /* hello, and which pan this is */
}

void ui_frame(void)
{
    input();
    touch_tick();
    autosave();
    draw_header();
    draw_main();
    draw_knobs();
    leds();
    ui.frame++;
}

void ui_input_only(void) { input(); }
