/* SPDX-License-Identifier: GPL-3.0-only */
/* HP-1's arpeggiator, sequencer and songs (seq.c): one clock, one track. Its events are what the
 * instrument plays: the pan's notes and the second pan's (in cents, as struck) and the hits on the steel
 * and the kit (HIT_*). Included by app.h. */
#pragma once
#include <stdint.h>

#define SEQ_NPAT 16                     /* patterns in the working project */
#define SEQ_NEV 1000                    /* events, all patterns together (8 KB in the pool) */
#define SEQ_NSEC 64                     /* a song's sections */
#define SEQ_MAXLEN 64                   /* a pattern's steps */
#define SEQ_SUB 96                      /* an event's time: step x 96 + where in the step (0..95) */
#define SEQ_END SEQ_NPAT                /* a section's pattern: End */
#define SEQ_NAME 16                     /* a project's name: 15 characters and the 0 */
#define SEQ_PPQ 384                     /* clock ticks a beat (a sixteenth: 96) */
#define SEQ_MAGIC 0x51455348u           /* "HSEQ" */
#define SEQ_FORMAT 1u

enum { EV_PAN, EV_DUO, EV_HIT };        /* an event's kind: the pan's note, the second pan's, a hit */
enum { SONG_OFF, SONG_ON, SONG_LOOP };

typedef struct {                        /* 8 bytes */
    uint16_t at;                        /* step x SEQ_SUB + sub-step (unswung) */
    uint16_t len;                       /* how long it was held, in SEQ_SUB parts of a step */
    uint16_t val;                       /* kind << 14 | cents (EV_PAN, EV_DUO) or HIT_* (EV_HIT) */
    uint8_t vel;                        /* 1..127 (bit 7: new in this take, in RAM only) */
    uint8_t pat;                        /* its pattern, 0..15 */
} seq_ev_t;
typedef struct { uint8_t len, div, swing, rsv; } seq_pat_t;   /* steps 1..64, DIV_*, 0..100 */
typedef struct { uint8_t pat, rep; } seq_sec_t;               /* a pattern (or SEQ_END), played 1..16 times */
typedef struct {
    uint32_t magic, format;
    char name[SEQ_NAME];                /* the project's name (A-Z 0-9 - and spaces) */
    uint16_t nev;
    uint8_t cur, song;                  /* the pattern chosen; Song: SONG_* */
    seq_pat_t pat[SEQ_NPAT];
    seq_sec_t sec[SEQ_NSEC];
    uint8_t rsv[16];
} seq_head_t;
typedef struct {                        /* the payload of OBJ_SEQ: the head, then nev events (sorted: pat, at) */
    seq_head_t h;
    seq_ev_t ev[SEQ_NEV];
} seq_data_t;
#define SEQ_BYTES(n) ((uint32_t)sizeof(seq_head_t) + (uint32_t)(n) * (uint32_t)sizeof(seq_ev_t))

extern seq_data_t sq;

/* the data */
void seq_clear_all(void);               /* empty: 16 empty patterns of 16 sixteenths, no song */
int seq_load(void);                     /* the working sequence from flash: 0, else empty */
int seq_save(void);
int seq_fix(void);                      /* after a load from elsewhere (a slot): out-of-range values made
                                         * safe, bad events dropped. 0 = it was a sequence at all */
int seq_dirty(void);
void seq_set_dirty(void);
int seq_used(int pat);                  /* events in it */
int seq_step_ticks(int pat);            /* a step's clock ticks */

/* the clock and the transport. seq_poll runs the clock to now and plays what falls within the next
 * few milliseconds (each strike posted with its delay, so a slow screen frame never makes it late) */
void seq_init(void);                    /* (after a load) the transport stopped */
void seq_poll(void);
void seq_midi_clock(int msg);           /* KM_CLK_*: MIDI clock leads while its ticks come */
void seq_play(int on);                  /* the transport (the pattern, or the song with Song on) */
void seq_rec_tap(void);                 /* arm / disarm recording */
void seq_rec_play(void);                /* REC held and PLAY: armed, and started (with the count-in) */
int seq_running(void);                  /* playing (or counting in) */
int seq_counting(void);                 /* counting in: beats to go (0: not) */
int seq_armed(void);
int seq_recording(void);                /* armed and the take running (not waiting) */
int seq_waiting(void);                  /* armed, the take to start at the pattern's next start */
int seq_ext(void);                      /* following MIDI clock */
int seq_playing_pat(void);              /* the pattern sounding (0..15) */
int seq_pos(void);                      /* where in it, in ticks */
int seq_section(void);                  /* the song's section playing (-1: not in a song) */
uint32_t seq_ticks(void);               /* the free-running clock (the arp's LED) */
int seq_bpm(void);

/* what the player does: a key's note (kind, cents or hit), its velocity; key: which key (its end) */
void seq_note_on(int key, int kind, int val, int vel);
void seq_note_off(int key);
/* the arpeggiator: held notes feed it (kind EV_PAN or EV_DUO, cents) */
int seq_arp_on(void);
void seq_arp_key(int kind, int cents, int down);
int seq_arp_held(void);

/* patterns: choose (while playing, at the loop's end), clear, copy into the next empty one, undo */
void seq_select(int pat);
int seq_next_pat(void);                 /* the one waiting for the loop's end (-1: none) */
void seq_clear_pat(int pat);
int seq_copy_pat(void);                 /* the new one (-1: none empty) */
int seq_undo(void);                     /* 0 = the last take (or clear) undone */
void seq_retime(void);                  /* a pattern's length or division changed (the loop playing follows) */

/* step edit: the event nearest step s of pattern p (its index, -1: a rest) */
int seq_step_ev(int pat, int step);
void seq_step_put(int pat, int step, int kind, int val, int vel, int steps);
void seq_step_rest(int pat, int step);
int seq_step_count(int pat, int step);

/* the instrument, the click and the groove, as seq.c plays them (ui.c; a test's own) */
static void seq_out_note(int kind, int val, int vel, int delay_ms);
static void seq_out_off(int kind, int val, int delay_ms);
static void seq_out_click(int accent, int delay_ms);
static void seq_out_groove(int pulse, int delay_ms);
static void seq_out_say(const char *a, const char *b);
