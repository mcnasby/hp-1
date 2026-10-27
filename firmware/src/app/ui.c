/* SPDX-License-Identifier: GPL-3.0-only */
/* HP-1 UI: the panel and the screen. Part of the unity build (after gfx.c and project.c); the host
 * simulator includes it too. (The frame of it, knobs, bands and autosave, is FoMni's ui.c, by way of
 * FiMba-1.)
 *
 *   white keys     the pan's notes: Pan (the ding on both middle keys, the fields zigzag outward as
 *                  they climb around the pan), Mirror (right for left) or Linear (low to high); past
 *                  the pan's own fields the scale goes on up, as on an extended pan
 *   black keys     the two at each end are always the steel: a palm thwap and a fingertip tak a side.
 *                  The seven between follow Black: Kit (Gu, ghost, ding slap, tak, ding harmonic, rim,
 *                  mute) or Duo (a second pan whose scale coordinates with the first)
 *   PLAY freeze the grains     REC palm mute     ARP tap: the groove on / off; held: the Groove page
 *   OCT- / OCT+    the octave
 *   HOME ENV FX LFO SEL SEQ GLO   the pages (pressed again: the button's next page): HOME Pan, Shell;
 *                  ENV Touch; FX Space, Color; LFO Grain, Spread; SEL Keys, Duo; SEQ Groove; GLO Setup.
 *                  EDIT steps through them all.
 *   SELECT metal   ALGORITHM scale (and its usual key)   PRESETS key   KNOB 1-4 the page's values
 *   SAVE           saves (it also saves by itself, a few seconds after a change, when quiet) */

enum { V_PAN, V_SHELL, V_TOUCH, V_SPACE, V_COLOR, V_GRAIN, V_SPREAD, V_KEYS, V_DUO, V_GROOVE, V_SETUP, NVIEWS };
static const char *const VIEW_NAME[NVIEWS] = {"Pan", "Shell", "Touch", "Space", "Color", "Grain", "Spread",
                                              "Keys", "Duo", "Groove", "Setup"};
static const uint8_t VIEW_BTN[NVIEWS] = {B_HOME, B_HOME, B_ENV, B_FX, B_FX, B_LFO, B_LFO, B_SEL, B_SEL, B_SEQ, B_GLO};
#define K_NONE 255                     /* a knob with nothing on it */
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
    {P_PATTERN, P_PTEMPO, P_PERC, K_NONE},
    {P_TUNE, P_MIDICH, P_MIDIOUT, P_THEME},
};
#define ARP_HOLD_MS 500u               /* ARP held this long: the Groove page (a tap: the groove on / off) */
#define SAVE_HOLD_MS 1500u             /* SAVE held this long: reset to the defaults? (a tap: save) */
#define RESET_ASK_MS 5000u             /* ...asked this long: SAVE again resets, anything else keeps */

#define NWHITE KM_NWHITE
#define NBLACK KM_NBLACK
static const uint8_t WHITE_K[NWHITE] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK_K[NBLACK] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};

/* the black keys: the steel at both ends, the kit (or the second pan) between */
#define B_DUO0 2                       /* the first of the seven middle keys */
static const uint8_t HIT_OF[NBLACK] = {HIT_THWAP_L, HIT_TAK_L, HIT_GU, HIT_GHOST, HIT_SLAP, HIT_TAK, HIT_HARM,
                                       HIT_RIM, HIT_MUTE, HIT_TAK_R, HIT_THWAP_R};
static const char *const HIT_NAME[HIT_N] = {"Thwp", "Tak", "Gu", "Gho", "Slap", "Tak", "Harm", "Rim", "Mute", "Tak", "Thwp"};
static const char *const HIT_LONG[HIT_N] = {"THWAP", "TAK", "GU", "GHOST", "DING SLAP", "TAK", "HARMONIC", "RIM",
                                            "MUTE", "TAK", "THWAP"};
static int is_duo_key(int b) { return proj.par[P_BLACK] == BLK_DUO && b >= B_DUO0 && b < B_DUO0 + KM_NDUO; }

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
    uint8_t pat_on;                    /* the groove running (ARP) */
    uint32_t arp_t;                    /* when ARP went down (held: the Groove page) */
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
} ui;

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

/* the value as this screen shows it: the second pan by its name */
static void value_text(int k, char *t)
{
    if (k == P_DUO)
        km_duo_name(proj.par[P_SCALE], proj.par[P_KEY], proj.par[P_DUO], t);
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
    say(km_param_info(k)->name, t);
}

static int knob_hi(int k) { return k == P_DUO ? km_duo_count(proj.par[P_SCALE], proj.par[P_KEY]) - 1 : km_param_info(k)->hi; }

static void knob_set(int k, int v)
{
    const km_param_t *p = km_param_info(k);
    const int hi = knob_hi(k);
    v = v < p->lo ? p->lo : v > hi ? hi : v;
    if (v == proj.par[k])
        return;
    proj.par[k] = (int16_t)v;
    km_set(k, v);
    mark_dirty();
    if (k == P_SCALE) {                              /* a new pan comes in the key it's usually made in */
        proj.par[P_KEY] = (int16_t)km_scale_root(v);
        km_set(P_KEY, proj.par[P_KEY]);
    }
    if (k == P_SCALE || k == P_KEY) {                /* and its second pan matches it afresh */
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

/* a knob that does nothing now: the second pan when the black keys are the kit */
static int knob_off(int k) { return (k == P_DUO || k == P_DUOLVL) && proj.par[P_BLACK] != BLK_DUO; }

static void turn(int role, int k, int32_t e)
{
    const km_param_t *p = km_param_info(k);
    if (!e || k == K_NONE)
        return;
    if (knob_off(k)) {
        say("DUO OFF", "KEYS: BLACK DUO");
        return;
    }
    knob_set(k, proj.par[k] + accel(role, e, knob_hi(k) - p->lo));
}

/* ------------------------------------------------------------- notes --- */
static int octave_now(void) { return proj.par[P_OCTAVE]; }
static int ding_now(void) { return km_ding_cents(proj.par[P_KEY], octave_now()); }
static int tune(int c) { return proj.par[P_TUNING] == TUNE_JUST ? km_just_cents(c, ding_now()) : c; }
static int pan_notes(int *n) { return km_pan_notes(proj.par[P_SCALE], proj.par[P_KEY], octave_now(), n); }
static int white_index(int w) { return km_white_index(proj.par[P_LAYOUT], w); }
static int white_cents(int w) { return tune(km_white_cents(proj.par[P_LAYOUT], proj.par[P_SCALE], proj.par[P_KEY], octave_now(), w)); }
static int white_pan(int w) { return (int)(km_white_pos(proj.par[P_LAYOUT], w) * 100.0f); }
static int duo_cents(int i)
{
    int n[KM_NDUO];
    km_duo_notes(proj.par[P_SCALE], proj.par[P_KEY], octave_now(), proj.par[P_DUO], n);
    return tune(n[i < 0 ? 0 : i >= KM_NDUO ? KM_NDUO - 1 : i]);
}
/* the second pan's key b (a black key) : its note's index, laid out as the white keys are */
static int duo_index(int b) { return km_duo_index(proj.par[P_LAYOUT], b - B_DUO0); }
static int view_knob(int i) { return VIEW_KNOB[ui.view][i]; }
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
    if (proj.par[P_BLACK] == BLK_DUO)
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

/* key k strikes a note: remembered so its release can damp it */
static void key_strike(int k, int cents, int vel, int pan)
{
    vel = human_vel(vel);
    km_pluck(cents, vel, pan, human_ms());
    ui.key_cents[k] = (int16_t)cents;
    ui.key_on[k] = 1;
    note_out(1, cents_note(cents), (uint8_t)(vel < 1 ? 1 : vel > 127 ? 127 : vel));
}

static void key_release(int k)
{
    if (!ui.key_on[k])
        return;
    if (proj.par[P_RELEASE])
        km_damp(ui.key_cents[k]);
    note_out(0, cents_note(ui.key_cents[k]), 0);
    ui.key_on[k] = 0;
}

static void set_freeze(int on)
{
    ui.frozen = (uint8_t)on;
    km_freeze(on);
    say("GRAINS", on ? "FROZEN" : "LIVE");
}

static void strike_hit(int kind)
{
    km_hit(kind, human_vel(104), 0);
    ui.hit_t[kind] = ui.frame + 1u;
    if (kind == HIT_SLAP || kind == HIT_HARM)
        ui.ripple[0] = ui.frame + 1u;
}

static void white_down(int w)
{
    key_strike(WHITE_K[w], white_cents(w), 100, white_pan(w));
    ui.ripple[white_index(w)] = ui.frame + 1u;
}

static void black_down(int b)
{
    if (is_duo_key(b)) {                             /* the second pan, standing to the right */
        const int i = duo_index(b);
        key_strike(BLACK_K[b], duo_cents(i), 100 * proj.par[P_DUOLVL] / 100 + 12, 55 + 8 * (b - B_DUO0 - 3));
        ui.ripple[KM_NPAN + i] = ui.frame + 1u;
        return;
    }
    strike_hit(HIT_OF[b]);
}

static void black_up(int b) { key_release(BLACK_K[b]); }

/* ------------------------------------------------------------ groove --- */
static void groove(int on)
{
    char t[16];
    ui.pat_on = (uint8_t)on;
    km_pattern(on);
    km_param_text(P_PATTERN, proj.par[P_PATTERN], t);
    s_upper(t);
    say("GROOVE", on ? t : "OFF");
}

/* ------------------------------------------------------------ buttons --- */
static void set_view(int v)
{
    ui.view = (uint8_t)v;
    ui.touched = -1;
}

/* back to the defaults: every sound setting, the groove stopped and the grains let go. The Setup page
 * (tuning, MIDI, the theme) is about the room and the rig, not the sound: it stays. */
static void reset_defaults(void)
{
    static const uint8_t KEEP[4] = {P_TUNE, P_MIDICH, P_MIDIOUT, P_THEME};
    int16_t keep[4];
    int i;
    for (i = 0; i < 4; i++)
        keep[i] = proj.par[KEEP[i]];
    if (ui.pat_on)
        groove(0);
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
    if (VIEW_BTN[ui.view] == b) {                   /* pressed again: its next page, or round to its first */
        v = ui.view + 1;
        if (v >= NVIEWS || VIEW_BTN[v] != b)
            for (v = ui.view; v > 0 && VIEW_BTN[v - 1] == b; v--)
                ;
        set_view(v);
        return;
    }
    for (v = 0; v < NVIEWS; v++)
        if (VIEW_BTN[v] == b) {
            set_view(v);
            return;
        }
    switch (b) {
    case B_EDIT: set_view((ui.view + 1) % NVIEWS); break;
    case B_PLAY: set_freeze(!ui.frozen); break;
    case B_REC:
        strike_hit(HIT_MUTE);
        say("PALM", "MUTE");
        break;
    case B_ARP: groove(!ui.pat_on); break;
    case B_OCTDN: case B_OCTUP:
        knob_set(P_OCTAVE, proj.par[P_OCTAVE] + (b == B_OCTUP ? 1 : -1));
        say_param(P_OCTAVE);
        break;
    case B_SAVE:
        if (project_save() == 0) {
            ui.dirty = 0;
            say("SAVED", 0);
        } else {
            say("NOT SAVED", "FLASH ERROR");
        }
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
        if (white_cents(w) == cents) {
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
    case 49: case 57: return HIT_HARM;                     /* crashes */
    case 51: case 59: return HIT_GHOST;                    /* rides */
    case 54: case 69: case 70: return HIT_TAK_L;           /* tambourine, shakers */
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
        if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu) {   /* clock: the groove follows it */
            km_clock(st == 0xF8u ? KM_CLK_TICK : st == 0xFAu ? KM_CLK_START : st == 0xFBu ? KM_CLK_CONTINUE : KM_CLK_STOP);
            continue;
        }
        if (st >= 0xF0u)
            continue;                                       /* sysex and the rest: not ours */
        if (type == 0x90u && d2 && (st & 0x0Fu) == 9u && drum_hit(d1) >= 0) {
            km_hit(drum_hit(d1), (int)d2, 0);
            ui.hit_t[drum_hit(d1)] = ui.frame + 1u;
            ui.act_t = plat_ms();
            continue;
        }
        if (proj.par[P_MIDICH] && (st & 0x0Fu) + 1u != (uint32_t)proj.par[P_MIDICH])
            continue;
        if (type == 0x90u && d2) {
            int c = tune((int)d1 * 100);
            km_pluck(c, (int)d2, midi_pan(c), 0);
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
    int32_t e;
    ch = btn ^ ui.btn;
    ui.btn = btn;
    for (i = 0; i < NB; i++) {
        uint32_t m = 1u << i;
        if (!(ch & m))
            continue;
        if (btn & m) {
            ui.btn_used &= ~m;
            if (i == B_ARP)
                ui.arp_t = plat_ms();
            if (i == B_SAVE)
                ui.save_t = plat_ms();
            if (i == B_PLAY || i == B_REC)             /* the playing buttons act on the press */
                button((int)i);
        } else if (!(ui.btn_used & m) && i != B_PLAY && i != B_REC) {
            button((int)i);
        }
    }
    if ((btn >> B_ARP & 1u) && !(ui.btn_used >> B_ARP & 1u) && plat_ms() - ui.arp_t >= ARP_HOLD_MS) {
        set_view(V_GROOVE);                          /* ARP held: the groove's settings (no toggle on release) */
        ui.btn_used |= 1u << B_ARP;
    }
    if ((btn >> B_SAVE & 1u) && !(ui.btn_used >> B_SAVE & 1u) && !ui.reset_ask &&
        plat_ms() - ui.save_t >= SAVE_HOLD_MS) {
        reset_ask(1);                                /* SAVE held: ask (no save on release) */
        ui.btn_used |= 1u << B_SAVE;
    }
    if (ui.reset_ask && (int32_t)(plat_ms() - ui.reset_ask) >= 0)
        reset_ask(0);                                /* not answered: kept */
    ch = keys ^ ui.keys;
    if (ui.reset_ask && (ch & keys))                 /* a key while "RESET?" asks: kept (and it plays) */
        reset_ask(0);
    ui.keys = keys;
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
        ui.act_t = plat_ms();
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
            ui.touch_until = plat_ms() + 1200u;
            ui.act_t = plat_ms();
        }
    pan_sync();
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

static void draw_header(void)
{
    char t[24];
    int msg = plat_ms() < ui.msg_until;
    uint32_t h = hash(hash(2166136261u, ui.view | (uint32_t)ui.frozen << 8 | (uint32_t)proj.par[P_RELEASE] << 9 |
                                            (uint32_t)proj.par[P_THEME] << 14 | (uint32_t)proj.par[P_BLACK] << 15 |
                                            (uint32_t)ui.pat_on << 12 | (uint32_t)(ui.pat_on && km_pat_pulse % 3u == 0u) << 13 |
                                            (uint32_t)ui.dirty << 10 | (uint32_t)msg << 11),
                      (uint32_t)proj.par[P_KEY] | (uint32_t)proj.par[P_SCALE] << 4 | (uint32_t)(proj.par[P_OCTAVE] + 2) << 9);
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
        int32_t x = 232, w;
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
        x = 18 + text_w(&FONT_B, VIEW_NAME[ui.view]);
        {   /* the button's pages, when it has more than one: a dot each, this one filled */
            int first = ui.view, n = 0, v;
            while (first > 0 && VIEW_BTN[first - 1] == VIEW_BTN[ui.view])
                first--;
            for (v = first; v < NVIEWS && VIEW_BTN[v] == VIEW_BTN[ui.view]; v++)
                n++;
            if (n > 1) {
                for (v = 0; v < n; v++)
                    cv_round(x - 4 + v * 9, 12, 6, 6, 3, first + v == ui.view ? K_TEXT : K_LINE);
                x += n * 9 + 2;
            }
        }
        if (ui.frozen) {
            w = text_w(&FONT_XS, "FRZ") + 10;
            cv_round(x, 6, w, 16, 6, K_TEAL_T);
            cv_text(x + 5, 7, &FONT_XS, "FRZ", K_TEAL);
            x += w + 4;
        }
        if (ui.pat_on) {                             /* the groove: bright on the beat */
            int beat = km_pat_pulse % 3u == 0u;
            w = text_w(&FONT_XS, "GRV") + 10;
            cv_round(x, 6, w, 16, 6, beat ? K_ACC : K_ACC_T);
            cv_text(x + 5, 7, &FONT_XS, "GRV", beat ? K_WHITE : K_ACC);
            x += w + 4;
        }
        if (proj.par[P_RELEASE]) {
            w = text_w(&FONT_XS, "DAMP") + 10;
            cv_round(x, 6, w, 16, 6, K_ACC_T);
            cv_text(x + 5, 7, &FONT_XS, "DAMP", K_ACC);
            x += w + 4;
        }
        if (ui.dirty)
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

/* where field i (1..8) sits on a pan of radius R at (cx, cy) */
static spot_t field_spot(int i, int32_t cx, int32_t cy, int32_t R)
{
    static const int8_t SX[8] = {-1, 1, -1, 1, -1, 1, -1, 1};
    static const int16_t SIN[4] = {500, 940, 940, 500}, COS[4] = {866, 342, -342, -866};   /* 30 70 110 150 deg, x1000 */
    const int p = ((i - 1) / 2) & 3, ring = R * 68 / 100;
    spot_t s;
    s.x = (int16_t)(cx + SX[(i - 1) & 7] * ring * SIN[p] / 1000);
    s.y = (int16_t)(cy + ring * COS[p] / 1000);
    s.r = (int16_t)(R * (21 - 2 * p) / 100);
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
    if (label) {
        const char *nm = KM_NOTE_NAME[((cents + 50) / 100) % 12];
        text_c(s.x, s.y - 8, &FONT_XS, nm, t > 8 ? mix(ink, dark, 12) : ink);
    }
}

static void draw_pan(void)
{
    int n[KM_NPAN], own = pan_notes(n), i, w;
    const int duo = proj.par[P_BLACK] == BLK_DUO;
    /* (with the second pan, or Linear's extra notes all on the right, the pan moves over) */
    const int32_t R = 52, cx = duo ? 90 : proj.par[P_LAYOUT] == LAY_LINEAR ? 92 : 120, cy = 55;
    const uint16_t dark = METAL->dark, light = METAL->light, ink = METAL->ink;
    int glow[2];
    for (i = 0; i < 2; i++)                           /* the rim, where a thwap or a tak just landed */
        glow[i] = ui.rim[i];
    draw_shell(cx, cy, R, dark, light, glow);
    {   /* the Gu: the air inside, breathing round the ding */
        int g = ui.rim[2];
        if (g > 1)
            cv_ring(cx, cy, 17 + g / 4, 18 + g / 4, 0, mix(mix(dark, light, 8), K_GLOW, g));
    }
    for (i = 0; i < own && i < 9; i++) {              /* the ding and its fields */
        spot_t s = i ? field_spot(i, cx, cy, R) : (spot_t){(int16_t)cx, (int16_t)cy, 14};
        int c = tune(n[i]);
        draw_field(s, c, (int)ripple_age(ui.ripple[i]), note_level(c), i == 0, dark, light, ink, 1);
    }
    /* the notes past the pan's own fields (the white keys at the ends): small extra fields, under the keys
     * that play them */
    for (w = 0; w < NWHITE; w++) {
        int idx = white_index(w);
        if (idx >= own) {
            spot_t s = {(int16_t)(9 + w * 15), 104, 6};
            int c = tune(n[idx]);
            if (duo && w >= 11)                       /* (the second pan stands there) */
                continue;
            draw_field(s, c, (int)ripple_age(ui.ripple[idx]), note_level(c), 0, dark, light, ink, 0);
            {
                const char *nm = KM_NOTE_NAME[((c + 50) / 100) % 12];
                text_c(s.x, s.y - 20, &FONT_XS, nm, K_DIM);
            }
        }
    }
    if (duo) {   /* the second pan, smaller, to the right: its ding and six fields */
        const int32_t dx = 200, dy = 50, dr = 34;
        draw_shell(dx, dy, dr, dark, light, (int[2]){0, 0});
        for (i = 0; i < KM_NDUO; i++) {
            spot_t s = i ? field_spot(i, dx, dy, dr) : (spot_t){(int16_t)dx, (int16_t)dy, 9};
            int c = duo_cents(i);
            if (i)
                s.r = (int16_t)(s.r + 1);
            draw_field(s, c, (int)ripple_age(ui.ripple[KM_NPAN + i]), note_level(c), i == 0, dark, light, ink, 0);
        }
        {
            char t[16];
            km_duo_name(proj.par[P_SCALE], proj.par[P_KEY], proj.par[P_DUO], t);
            text_c(dx, dy + dr + 4, &FONT_XS, t, K_DIM);
        }
    }
}

/* the black keys, as a strip: the steel at both ends, the kit or the second pan's notes between */
static void draw_black(int32_t y0)
{
    int b;
    for (b = 0; b < NBLACK; b++) {
        char t[12];
        int32_t x = 4 + b * 20 + ((b >= 3) + (b >= 5) + (b >= 8) + (b >= 10)) * 3;   /* grouped as the keys are */
        int on = (ui.keys >> BLACK_K[b] & 1u) != 0, steel = b < B_DUO0 || b >= B_DUO0 + KM_NDUO;
        uint16_t bg = steel ? K_ACC_T : K_TEAL_T, fg = steel ? K_ACC : K_TEXT;
        if (is_duo_key(b)) {
            const int c = duo_cents(duo_index(b));
            s_cpy(t, KM_NOTE_NAME[((c + 50) / 100) % 12]);
            if (note_level(c) > 0.15f)
                on = 1;
        } else {
            const int k = HIT_OF[b];
            s_cpy(t, HIT_NAME[k]);
            if (ripple_age(ui.hit_t[k]) < 12u)
                on = 1;
        }
        if (on) {
            bg = steel ? K_ACC : K_TEAL;
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
    if (h == ui.sig[1])
        return;
    ui.sig[1] = h;
    cv_begin(240, MAIN_H, K_BG);
    draw_pan();
    draw_black(114);
    cv_blit(0, MAIN_Y);
}

static void draw_knobs(void)
{
    uint32_t h = hash(2166136261u, ui.view | (uint32_t)ui.touched << 8 | (uint32_t)proj.par[P_THEME] << 16 |
                                       (uint32_t)proj.par[P_BLACK] << 20), i;
    int now_touch = plat_ms() < ui.touch_until;
    for (i = 0; i < 4; i++)
        h = hash(h, view_knob((int)i) == K_NONE ? 0xFFFFu : (uint32_t)proj.par[view_knob((int)i)] | (uint32_t)view_knob((int)i) << 16);
    h = hash(h, (uint32_t)now_touch | (uint32_t)proj.par[P_SCALE] << 4 | (uint32_t)proj.par[P_KEY] << 8);
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
            text_c(cx, 6, &FONT_XS, km_param_info(k)->name, K_LINE);
            text_c(cx, 26, &FONT_B, "--", K_LINE);
            continue;
        }
        lo = km_param_info(k)->lo;
        hi = knob_hi(k);
        v = proj.par[k];
        text_c(cx, 6, &FONT_XS, km_param_info(k)->name, hot ? K_TEXT : K_DIM);
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

/* the ding's keys, marked by a dim LED (Pan, Mirror: the middle two; Linear: the first), and with Duo
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

static void leds(void)
{
    uint32_t b = VIEW_BTN[ui.view] < NB ? 1u << VIEW_BTN[ui.view] : 0u, k = 0, i;
    if (ui.frozen)
        b |= 1u << B_PLAY;
    if (ui.pat_on && km_pat_pulse % 3u != 0u)        /* the groove: lit, dark on each beat */
        b |= 1u << B_ARP;
    for (i = 0; i < NWHITE; i++)                     /* the notes that ring */
        if (note_level(white_cents((int)i)) > 0.08f)
            k |= 1u << WHITE_K[i];
    for (i = 0; i < NBLACK; i++)
        if (is_duo_key((int)i) ? note_level(duo_cents(duo_index((int)i))) > 0.08f : ripple_age(ui.hit_t[HIT_OF[i]]) < 12u)
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
    if (!ui.dirty || ui.btn || ui.keys || now - ui.act_t < AUTOSAVE_QUIET || ui.frozen || ui.pat_on ||
        km_out_level > 0.0005f)
        return;
    for (i = 0; i < KM_NVOICE; i++)
        if (km_voice_level[i] > 0.0f)
            return;
    if (project_save() == 0)
        ui.dirty = 0;
}

void ui_init(void)
{
    char t[24];
    memset(&ui, 0, sizeof ui);
    ui.touched = -1;
    ui.rng = 0x2545F491u;
    lcd_fill(0, 0, 240, 240, K_BG);
    pan_sync();
    pan_name(t);
    s_upper(t);
    say("HP-1", t);                                  /* hello, and which pan this is */
}

void ui_frame(void)
{
    input();
    autosave();
    draw_header();
    draw_main();
    draw_knobs();
    leds();
    ui.frame++;
}

void ui_input_only(void) { input(); }
