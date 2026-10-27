/* SPDX-License-Identifier: GPL-3.0-only */
/* The instrument: a handpan by modal synthesis. Five parts:
 *
 *   FIELDS  KM_NVOICE voices, one per sounding tone field (or the ding). A tone field is a dimpled,
 *           hammered membrane tuned so its first modes sit at 1 : 2 : 3 (: 4), the fundamental, the
 *           octave, the compound fifth and a faint double octave. Measured pans (Alon, York 2015) ring
 *           ~3 s, the octave nearly as loud as the fundamental and often outlasting it, the fifth 10-20
 *           dB down, and the octave and fifth beating at 3-9 Hz: each is split in two (the field's
 *           near-twin modes, and its neighbors a hair off tune), and the two pulse against each other.
 *           That pulse is the handpan's shimmer. The fundamental doesn't split. A strike is a half-sine
 *           force pulse with a short burst of skin on steel; it also rings the field's own higher,
 *           inharmonic modes for a moment: the metallic ping of a struck pan, gone in ~0.2 s. The metal
 *           sets the levels and the decays.
 *   SHELL   the instrument around the fields: a 1.2 mm steel shell (its own inharmonic modes, a bank a
 *           side, which the thwaps ring and the notes brush) and the air inside, a Helmholtz cavity
 *           tuned by the bottom port (the "Gu"). Size and Thickness move both. Sympathy: a struck note
 *           sets the fields whose partials it meets ringing softly (a struck D4 wakes the D3 ding's
 *           octave), the whole pan breathing with what's played; and the AIR, a bank of resonators
 *           tuned to the pan's notes, each ringing its whole harmonic series, diffused: the airy halo
 *           of harmonics a real pan leaves hanging after each note.
 *   HITS    percussion on the steel, not on a field: palm thwaps and fingertip taks on the shoulders,
 *           the Gu, ghost notes, a muted ding slap, the ding's harmonic, a knuckle on the rim.
 *   FX      a chorus and a granular cloud on the dry sound, a ping-pong delay, a plate reverb
 *           (Dattorro's, from FoMni); then on everything a filter and a tape (wow, flutter, lo-fi).
 *   GROOVE  patterns over the pan's notes and its percussion, on a cycle of 12 pulses (3 a beat), at a
 *           tempo or following MIDI clock.
 *   MUSIC   handpan scales (ding + tone fields, as the makers build them), the key layouts and the
 *           second pan that coordinates with the first: pure functions, used by the UI and the tests.
 *
 * Threads: km_render() runs in the audio interrupt. Everything else posts commands, which the render
 * drains at the start of a block; the state marked (UI) is written by the render and only read
 * elsewhere. Float only, no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define KM_SR 44100.0f
#define KM_NVOICE 16                   /* fields sounding at once */
#define KM_NMODE 10                    /* resonators per field: fundamental, octave + twin, fifth + twin, 2 oct,
                                        * and four of the strike's metallic ping */
#define KM_NWHITE 16                   /* the white keys: one field each (the middle two both the ding) */
#define KM_NBLACK 11
#define KM_NPAN 16                     /* a pan's notes: the ding and up to 15 above it */
#define KM_NDUO 7                      /* the second pan on the black keys: its ding and six fields */

/* parameters: value ranges in km_param_info(). Some are the UI's (layout, MIDI): kept here so one
 * table names and bounds every value that is saved. */
enum {
    P_METAL, P_STRIKE, P_SUSTAIN, P_HARMONICS,       /* Pan page */
    P_SIZE, P_THICK, P_SYMPATHY, P_PORT,             /* Shell page */
    P_REVERB, P_RSIZE, P_DELAY, P_TIME,              /* Space page */
    P_TAPE, P_LOFI, P_CHORUS, P_FILTER,              /* Color page */
    P_GRAIN, P_GSIZE, P_GDENS, P_GPITCH,             /* Grain page */
    P_GSPRAY, P_FEEDBACK, P_WIDTH,                   /* Spread page */
    P_LAYOUT, P_SCALE, P_KEY, P_BLACK,               /* Keys page */
    P_DUO, P_DUOLVL, P_PERC, P_OCTAVE,               /* Duo page */
    P_WORN, P_HUMAN, P_TUNING, P_RELEASE,            /* Touch page */
    P_PATTERN, P_PTEMPO,                             /* Groove page */
    P_TUNE, P_MIDICH, P_MIDIOUT, P_THEME,            /* Setup page */
    P_NPARAMS
};
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} km_param_t;
const km_param_t *km_param_info(int i);
void km_param_text(int i, int v, char *buf);   /* the value as the screen shows it (12 bytes) */

enum { MET_NITRIDED, MET_STAINLESS, MET_EMBER, MET_RAW, MET_N };
enum { LAY_PAN, LAY_MIRROR, LAY_LINEAR, LAY_N };
enum { BLK_KIT, BLK_DUO, BLK_N };
enum { PAT_HEART, PAT_CASCADE, PAT_HEMIOLA, PAT_BACKBEAT, PAT_N };
enum { TUNE_EQUAL, TUNE_JUST, TUNE_N };
enum { THEME_DARK, THEME_LIGHT, THEME_N };
enum { GP_DOWN12, GP_DOWN7, GP_UNISON, GP_UP7, GP_UP12, GP_UP19, GP_SHIMMER, GP_REVERSE, GP_N };
#define KM_NSCALE 11
extern const char *const KM_NOTE_NAME[12];

/* the percussion (km_hit): where on the instrument, and with what */
enum {
    HIT_THWAP_L,                       /* a palm slapped flat on the left shoulder */
    HIT_TAK_L,                         /* fingertips on the left shoulder's edge: a bright tak */
    HIT_GU,                            /* a palm over the bottom port: the cavity's boom */
    HIT_GHOST,                         /* a light fingertip between the fields */
    HIT_SLAP,                          /* the ding struck and held: a short, closed slap */
    HIT_TAK,                           /* a fingertip tak near the ding */
    HIT_HARM,                          /* the ding's harmonic: a finger resting on it as it's struck */
    HIT_RIM,                           /* a knuckle on the rim seam */
    HIT_MUTE,                          /* a palm over the top: everything damped */
    HIT_TAK_R,
    HIT_THWAP_R,
    HIT_N
};

/* a metal: the fundamental's T60 at D3 (shorter up the scale by (f / D3)^-pitch_k); the octave's, the
 * fifth's and the double octave's levels; the octave's and the fifth's T60 against the fundamental's;
 * the beat (Hz, the mean: each field its own, 0.7 to 1.3 times it) and how deep it pulses (the twin's
 * level against its partner's); the skin on the steel; the strike's metallic ping; the shell's
 * brightness and its T60 */
typedef struct {
    const char *name;
    float t60, pitch_k, oct, fifth, dbl, oct_t, fifth_t, beat, depth, touch, ping, shell_bright, shell_t60;
} km_metal_t;
const km_metal_t *km_metal(int i);

/* ---- music: everything in cents over MIDI note 0 (6000 = middle C) */
const char *km_scale_name(int scale);
int km_scale_root(int scale);                      /* the key a maker builds it in most often (0..11) */
int km_scale_tops(int scale);                      /* its tone fields around the ding */
int km_ding_cents(int key, int octave);            /* the ding: between A2 and G#3, then the octave */
/* the pan's notes, low to high: the ding, its fields, then the scale continued above them (as on the
 * extended "mutant" pans) up to KM_NPAN. Returns how many are the instrument's own (ding + fields) */
int km_pan_notes(int scale, int key, int octave, int out[KM_NPAN]);
/* the note white key w (0..15) plays: Pan (the ding on both middle keys, the fields zigzag outward,
 * as they climb around a real pan), Mirror (the same, right for left) or Linear (low to high) */
int km_white_index(int layout, int w);             /* into km_pan_notes */
int km_white_cents(int layout, int scale, int key, int octave, int w);
/* where on the instrument a field sits: -1 (left) .. +1 (right), for its pan */
float km_white_pos(int layout, int w);
/* a pan's accidentals spelled as its scale spells them: bit p set, pitch class p is a sharp (C#), else
 * a flat (Db). Each is named after the letter its scale doesn't already use (E SaByeD's G# and D#, D
 * Kurd's Bb); when both or neither are used, C# Eb F# Ab Bb */
uint16_t km_sharps(int scale, int key);
const char *km_note_name(int cents, uint16_t sharps);   /* "G#", "Bb", "E" */
/* just intonation: a note on the 12-step grid, retuned to the pure ratio of its interval over the
 * tonic (5-limit: 16/15 9/8 6/5 5/4 4/3 45/32 3/2 8/5 5/3 9/5 15/8) */
int km_just_cents(int cents, int tonic_cents);

/* the second pan (Duo): the scales and keys whose notes sit within the first pan's, best first (the
 * same ding, then a fifth, a fourth, the relative...). Choice 0 is the best match. */
#define KM_NDUOCHOICE 24
int km_duo_count(int scale, int key);
void km_duo_choice(int scale, int key, int choice, int *duo_scale, int *duo_key);
/* its notes: its ding (a register above the first pan's) and its first six fields */
int km_duo_notes(int scale, int key, int octave, int choice, int out[KM_NDUO]);
/* the note (into km_duo_notes) the second pan's key i (0..6, left to right) plays: laid out as the
 * white keys are (Layout). Pan: its ding on the middle key, its fields alternating outward, left
 * first; Mirror: right first; Linear: low to high */
int km_duo_index(int layout, int i);
const char *km_duo_name(int scale, int key, int choice, char *buf);   /* "A Kurd" (16 bytes) */

/* ---- commands (main loop -> render). The main loop is the only producer. */
void km_init(void);
void km_set(int param, int value);
/* strike the field at `cents` (0..12700), velocity 1..127, pan -100..100, delay_ms later (0..2000) */
void km_pluck(int cents, int vel, int pan, int delay_ms);
void km_hit(int kind, int vel, int delay_ms);      /* percussion (HIT_*), on the pan's ding */
void km_pan(const int *cents, int n);              /* the pan's notes, the ding first (sympathy, grooves) */
void km_damp(int cents);                           /* a finger on that field (a key let go, Release Damp) */
void km_damp_all(void);                            /* a palm over the top */
void km_sustain(int on);                           /* the pedal (CC64): km_damp waits for it to lift */
void km_bend(int v);                               /* pitch bend, -8192..8191: +-2 semitones */
void km_freeze(int on);                            /* the grain buffer stops recording */
void km_pattern(int on);
/* MIDI clock in: tick (F8), start (FA), continue (FB), stop (FC). While ticks come (the last within
 * ~0.5 s) the groove follows them, 8 a pulse; otherwise its own Tempo */
enum { KM_CLK_TICK, KM_CLK_START, KM_CLK_CONTINUE, KM_CLK_STOP };
void km_clock(int msg);
void km_panic(void);                               /* everything quiet, now */

/* the render (audio ISR): n stereo frames, 24-bit in int32; gain Q12 (the MASTER pot) */
void km_render(int32_t *out_lr, uint32_t n, uint32_t gain_q12);

/* state for the screen (UI) */
extern volatile float km_voice_level[KM_NVOICE];   /* each voice's level, 0..~1 */
extern volatile int16_t km_voice_cents[KM_NVOICE]; /* the field it plays, -1 none */
extern volatile uint8_t km_frozen;
extern volatile uint8_t km_grains_on;              /* grains sounding now */
extern volatile float km_shell_level[2];           /* the shell ringing, left and right, 0..~1 */
extern volatile float km_cavity_level;             /* the air inside (the Gu) */
extern volatile uint32_t km_dropped;               /* commands lost to a full queue (never, we hope) */
extern volatile float km_out_level;                /* the output's peak over the last block (tails too) */
extern volatile uint8_t km_pat_on, km_pat_pulse;   /* the groove running, its pulse (0..11) */
extern volatile uint8_t km_ext;                    /* following MIDI clock */
