/* SPDX-License-Identifier: GPL-3.0-only */
/* HP-1's clock, arpeggiator, sequencer and songs (seq.h). Part of the unity build, after project.c; the
 * host simulator and tests/host/seq_test.c include it too. The arpeggiator, the recording (overdub,
 * quantize on the take's end, one undo), the song's sections and the transport's gestures follow
 * Melodee's (Kerem Kilic / Ellic Studio, GPL-3.0), reshaped for one instrument and one track.
 *
 * Time: clock ticks, SEQ_PPQ a beat (96 a sixteenth), from the Tempo or from MIDI clock (24 a beat: 16
 * ticks each). The clock runs always: the arpeggiator follows it, the transport too while it plays.
 * Everything due within the next LOOK_MS is posted at once with its delay, so the engine places it to
 * the millisecond however long the screen takes; under MIDI clock nothing is ahead (a tick plays now). */
#define LOOK_MS 30
#define EXT_MS 500u                     /* MIDI clock leads while a tick came this recently */
#define NOFF 32                         /* notes waiting to end */
#define NHELD 16
#define TAKE 0x80u                      /* an event's vel bit: of the last take (in RAM; a load drops it) */
#define REPLACE_SUB 12                  /* a note recorded this near (an eighth of a step) the same one: replaced */
#define NRM 64                          /* the notes a take replaced, kept for its undo */

seq_data_t sq OM_POOL;
/* The undo (one, the last take's): its notes carry TAKE; the ones it replaced wait here. Undone, the
 * first go and the others come back. An edit of the events after it (a step, a clear, a copy) ends it. */
static seq_ev_t rm[NRM];
static int nrm;

static const uint16_t DIVT[DIV_N] = {384, 192, 96, 48, 128, 64, 768, 1536, 3072, 6144};   /* DIV_* in ticks */
_Static_assert(sizeof(seq_head_t) == 236 && sizeof(seq_ev_t) == 8, "the saved layout (content.h) moved");

static struct {
    uint8_t run, arm, take, undo_ok, dirty, ext, cur, next, sec, rep, stop_due;
    uint32_t abs, acc, last_ms, ext_ms;  /* the free clock: ticks, and the part of one (ms x bpm x 384) */
    uint32_t done;                       /* everything before this tick is posted */
    uint32_t tstart;                     /* the transport's zero (after the count-in) */
    uint32_t t0, plen;                   /* this loop of the pattern: its start, its length */
    uint32_t pt0, pplen;                 /* the loop before (a note played just after a start that was posted early) */
    uint8_t pcur;
    uint32_t rec_from;                   /* the take records from this tick (before: WAIT) */
    uint32_t stop_at;
} tr;

static struct {
    int16_t c[NHELD];                    /* held notes (cents), in the order played */
    uint8_t k[NHELD];                    /* their kind */
    uint8_t n, phys;                     /* how many; keys physically down */
    uint32_t next, idx;                  /* the next step's tick; steps so far */
    uint8_t live;
} arp;

static struct { uint32_t at; int16_t val; uint8_t kind, on; } offs[NOFF];

/* a key's note while recording: its event, to give it its length on release */
static struct { uint16_t at, val; uint8_t pat, on; uint32_t t; } held_rec[32];

static int clampi_(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static uint16_t ev_val(int kind, int val) { return (uint16_t)(kind << 14 | (val & 0x3FFF)); }
static int ev_kind(const seq_ev_t *e) { return e->val >> 14; }
static int ev_note(const seq_ev_t *e) { return e->val & 0x3FFF; }
static void ev_move(int to, int from, int n)              /* (overlapping; the device has no memmove) */
{
    int i;
    if (to < from)
        for (i = 0; i < n; i++)
            sq.ev[to + i] = sq.ev[from + i];
    else
        for (i = n - 1; i >= 0; i--)
            sq.ev[to + i] = sq.ev[from + i];
}

/* ------------------------------------------------------------ the data --- */
int seq_step_ticks(int pat) { return DIVT[sq.h.pat[pat & 15].div < DIV_N ? sq.h.pat[pat & 15].div : DIV_16]; }
static uint32_t pat_ticks(int pat) { return (uint32_t)sq.h.pat[pat & 15].len * (uint32_t)seq_step_ticks(pat); }
int seq_dirty(void) { return tr.dirty; }
void seq_set_dirty(void) { tr.dirty = 1; }

static void pat_defaults(int p)
{
    sq.h.pat[p].len = 16;
    sq.h.pat[p].div = DIV_16;
    sq.h.pat[p].swing = 0;
    sq.h.pat[p].rsv = 0;
}

void seq_clear_all(void)
{
    int i;
    memset(&sq.h, 0, sizeof sq.h);
    sq.h.magic = SEQ_MAGIC;
    sq.h.format = SEQ_FORMAT;
    for (i = 0; i < SEQ_NPAT; i++)
        pat_defaults(i);
    for (i = 0; i < SEQ_NSEC; i++) {
        sq.h.sec[i].pat = SEQ_END;
        sq.h.sec[i].rep = 1;
    }
}

/* events in order: pattern, then time */
static int ev_before(const seq_ev_t *a, const seq_ev_t *b) { return a->pat != b->pat ? a->pat < b->pat : a->at < b->at; }

int seq_fix(void)
{
    int i, n = 0;
    if (sq.h.magic != SEQ_MAGIC || sq.h.format < 1u || sq.h.format > SEQ_FORMAT)
        return -1;
    sq.h.format = SEQ_FORMAT;
    sq.h.name[SEQ_NAME - 1] = 0;
    for (i = 0; sq.h.name[i]; i++)
        if (!((sq.h.name[i] >= 'A' && sq.h.name[i] <= 'Z') || (sq.h.name[i] >= '0' && sq.h.name[i] <= '9') ||
              sq.h.name[i] == '-' || sq.h.name[i] == ' '))
            sq.h.name[i] = ' ';
    if (sq.h.cur >= SEQ_NPAT)
        sq.h.cur = 0;
    if (sq.h.song > SONG_LOOP)
        sq.h.song = SONG_OFF;
    for (i = 0; i < SEQ_NPAT; i++) {
        seq_pat_t *p = &sq.h.pat[i];
        if (p->len < 1 || p->len > SEQ_MAXLEN || p->div >= DIV_N || p->swing > 100)
            pat_defaults(i);
    }
    for (i = 0; i < SEQ_NSEC; i++) {
        if (sq.h.sec[i].pat > SEQ_END)
            sq.h.sec[i].pat = SEQ_END;
        sq.h.sec[i].rep = (uint8_t)clampi_(sq.h.sec[i].rep, 1, 16);
    }
    if (sq.h.nev > SEQ_NEV)
        sq.h.nev = SEQ_NEV;
    for (i = 0; i < sq.h.nev; i++) {                    /* the sound ones, in order (a bad or stray one dropped) */
        seq_ev_t e = sq.ev[i];
        int k = ev_kind(&e);
        e.vel &= 0x7Fu;
        if (e.pat >= SEQ_NPAT || e.at >= SEQ_MAXLEN * SEQ_SUB || !e.vel || k > EV_HIT ||
            (k == EV_HIT ? ev_note(&e) >= HIT_N : ev_note(&e) > 12700))
            continue;
        if (n && ev_before(&e, &sq.ev[n - 1])) {
            int j = n;
            while (j > 0 && ev_before(&e, &sq.ev[j - 1])) {
                sq.ev[j] = sq.ev[j - 1];
                j--;
            }
            sq.ev[j] = e;
        } else {
            sq.ev[n] = e;
        }
        n++;
    }
    sq.h.nev = (uint16_t)n;
    return 0;
}

int seq_load(void)
{
    int n;
    memset(&sq, 0, sizeof sq);
    n = plat_store_load(OBJ_SEQ, &sq, sizeof sq);
    if (n >= (int)sizeof(seq_head_t) && seq_fix() == 0 && SEQ_BYTES(sq.h.nev) <= (uint32_t)n) {
        tr.dirty = 0;
        return 0;
    }
    seq_clear_all();
    tr.dirty = 0;
    return -1;
}

int seq_save(void)
{
    int rc = plat_store_save(OBJ_SEQ, &sq, SEQ_BYTES(sq.h.nev));
    if (rc == 0)
        tr.dirty = 0;
    return rc;
}

/* the pattern's events: [*a, *b) */
static void pat_span(int pat, int *a, int *b)
{
    int lo = 0, hi = sq.h.nev;
    while (lo < hi) {
        int m = (lo + hi) / 2;
        if (sq.ev[m].pat < pat)
            lo = m + 1;
        else
            hi = m;
    }
    *a = lo;
    hi = sq.h.nev;
    while (lo < hi) {
        int m = (lo + hi) / 2;
        if (sq.ev[m].pat <= pat)
            lo = m + 1;
        else
            hi = m;
    }
    *b = lo;
}

int seq_used(int pat)
{
    int a, b;
    pat_span(pat, &a, &b);
    return b - a;
}

static void ev_del(int i)
{
    if (i < 0 || i >= sq.h.nev)
        return;
    ev_move(i, i + 1, sq.h.nev - i - 1);
    sq.h.nev--;
    tr.dirty = 1;
}

/* a take's note replaced one from before: kept for the undo (too many: no undo for this take) */
static void rm_keep(const seq_ev_t *e)
{
    if (e->vel & TAKE)
        return;
    if (nrm < NRM)
        rm[nrm++] = *e;
    else
        tr.undo_ok = 0;
}

static int ev_insert(seq_ev_t e)                          /* in its place, as it is. -1: full */
{
    int a, b, i;
    if (sq.h.nev >= SEQ_NEV)
        return -1;
    pat_span(e.pat, &a, &b);
    for (i = a; i < b && sq.ev[i].at <= e.at; i++)
        ;
    ev_move(i + 1, i, sq.h.nev - i);
    sq.ev[i] = e;
    sq.h.nev++;
    tr.dirty = 1;
    return i;
}

/* in its place; the same note this near in the same pattern is replaced. -1: full */
static int ev_put(seq_ev_t e)
{
    int a, b, i;
    pat_span(e.pat, &a, &b);
    for (i = a; i < b; i++)
        if (sq.ev[i].val == e.val && (sq.ev[i].at > e.at ? sq.ev[i].at - e.at : e.at - sq.ev[i].at) < REPLACE_SUB) {
            if (e.vel & TAKE)
                rm_keep(&sq.ev[i]);
            ev_del(i);
            break;
        }
    return ev_insert(e);
}

/* the undo ends: the last take's notes are the pattern's like the rest */
static void undo_drop(void)
{
    int i;
    for (i = 0; i < sq.h.nev; i++)
        sq.ev[i].vel &= 0x7Fu;
    tr.undo_ok = 0;
    nrm = 0;
}

/* ------------------------------------------------------------- swing --- */
/* Swing moves every second step later (its partner earlier ends there too): a pair of steps (2 x 96)
 * bent at its middle, which moves by up to 40% of a step (Melodee's). A pattern's events are kept
 * straight; playing bends them, recording straightens what was played. */
static int swing_sub(int pat) { return sq.h.pat[pat].swing * SEQ_SUB / 250; }
static uint32_t bend(int pat, uint32_t x)              /* straight sub-steps -> swung sub-steps */
{
    const uint32_t sw = (uint32_t)swing_sub(pat), p = x / (2 * SEQ_SUB) * (2 * SEQ_SUB), r = x - p;
    if (!sw)
        return x;
    return p + (r < SEQ_SUB ? r * (SEQ_SUB + sw) / SEQ_SUB : SEQ_SUB + sw + (r - SEQ_SUB) * (SEQ_SUB - sw) / SEQ_SUB);
}
static uint32_t unbend(int pat, uint32_t y)            /* swung -> straight */
{
    const uint32_t sw = (uint32_t)swing_sub(pat), p = y / (2 * SEQ_SUB) * (2 * SEQ_SUB), r = y - p;
    if (!sw)
        return y;
    return p + (r < SEQ_SUB + sw ? r * SEQ_SUB / (SEQ_SUB + sw) : SEQ_SUB + (r - SEQ_SUB - sw) * SEQ_SUB / (SEQ_SUB - sw));
}
static uint32_t ev_tick(int pat, uint32_t at) { return bend(pat, at) * (uint32_t)seq_step_ticks(pat) / SEQ_SUB; }
static uint32_t tick_at(int pat, uint32_t t)            /* a tick in the pattern -> its straight time */
{
    return unbend(pat, (t * SEQ_SUB + (uint32_t)seq_step_ticks(pat) / 2u) / (uint32_t)seq_step_ticks(pat));
}

/* ------------------------------------------------------------- clock --- */
int seq_bpm(void) { return clampi_(proj.par[P_PTEMPO], 40, 240); }
uint32_t seq_ticks(void) { return tr.abs; }
int seq_ext(void) { return tr.ext; }

static void clock_run(void)
{
    const uint32_t now = plat_ms(), dt = now - tr.last_ms;
    tr.last_ms = now;
    if (tr.ext && now - tr.ext_ms > EXT_MS)
        tr.ext = 0;                                      /* the clock stopped coming: our own tempo again */
    if (tr.ext || dt > 1000u)
        return;
    tr.acc += dt * (uint32_t)seq_bpm() * SEQ_PPQ;
    tr.abs += tr.acc / 60000u;
    tr.acc %= 60000u;
}

/* ms from now to tick t (posted ahead: the engine places it) */
static int delay_ms(uint32_t t)
{
    int32_t d = (int32_t)(t - tr.abs);
    if (d <= 0 || tr.ext)
        return 0;
    return (int)(((uint32_t)d * 60000u - tr.acc) / ((uint32_t)seq_bpm() * SEQ_PPQ));
}

/* ------------------------------------------------------- notes ending --- */
static void off_add(uint32_t at, int kind, int val)
{
    int i;
    for (i = 0; i < NOFF; i++)
        if (!offs[i].on) {
            offs[i].on = 1;
            offs[i].at = at;
            offs[i].kind = (uint8_t)kind;
            offs[i].val = (int16_t)val;
            return;
        }
    seq_out_off(kind, val, delay_ms(at));               /* (full: posted now, as near as it can) */
}

static void offs_due(uint32_t until, int all)
{
    int i;
    for (i = 0; i < NOFF; i++)
        if (offs[i].on && (all || (int32_t)(offs[i].at - until) < 0)) {
            offs[i].on = 0;
            seq_out_off(offs[i].kind, offs[i].val, all ? 0 : delay_ms(offs[i].at));
        }
}

/* ---------------------------------------------------------- recording --- */
int seq_running(void) { return tr.run; }
int seq_armed(void) { return tr.arm; }
int seq_counting(void) { return tr.run && (int32_t)(tr.abs - tr.tstart) < 0 ? (int)((tr.tstart - tr.abs + SEQ_PPQ - 1) / SEQ_PPQ) : 0; }
int seq_recording(void) { return tr.arm && tr.run && (int32_t)(tr.abs - tr.rec_from) >= 0; }
int seq_waiting(void) { return tr.arm && tr.run && (int32_t)(tr.abs - tr.rec_from) < 0; }
int seq_playing_pat(void) { return tr.run ? tr.cur : sq.h.cur; }
int seq_next_pat(void) { return tr.run && tr.next < SEQ_NPAT ? tr.next : -1; }
int seq_section(void) { return tr.run && sq.h.song ? tr.sec : -1; }
int seq_pos(void)
{
    if (!tr.run || (int32_t)(tr.abs - tr.t0) < 0)
        return 0;
    return (int)(tr.abs - tr.t0);
}

/* where tick t falls: the pattern and its straight time (-1: not recording then) */
static int rec_where(uint32_t t, int *pat, uint32_t *at)
{
    uint32_t p;
    if (!tr.arm || !tr.run || (int32_t)(t - tr.rec_from) < 0)
        return -1;
    if ((int32_t)(t - tr.t0) >= 0) {
        *pat = tr.cur;
        p = (t - tr.t0) % (tr.plen ? tr.plen : 1u);
    } else {
        *pat = tr.pcur;
        p = (t - tr.pt0) % (tr.pplen ? tr.pplen : 1u);
    }
    *at = tick_at(*pat, p);
    if (*at >= (uint32_t)sq.h.pat[*pat].len * SEQ_SUB)
        *at = 0;                                          /* (the very end: the next loop's start) */
    return 0;
}

static void take_begin(void)                             /* the first note of a take: the last one's undo ends */
{
    if (tr.take)
        return;
    undo_drop();
    tr.undo_ok = 1;
    tr.take = 1;
}

/* the take ends: Quantize moves its notes to the grid (one landing on the same note: that one goes) */
static void take_end(void)
{
    const int q = proj.par[P_QUANT];
    int i, j;
    if (!tr.take)
        return;
    tr.take = 0;
    for (i = 0; i < (int)(sizeof held_rec / sizeof held_rec[0]); i++)
        held_rec[i].on = 0;
    if (q <= 0 || q > 6)
        return;
    for (i = 0; i < sq.h.nev; i++) {
        seq_ev_t *e = &sq.ev[i];
        if (e->vel & TAKE) {
            const uint32_t st = (uint32_t)seq_step_ticks(e->pat), g = DIVT[q - 1], t = (uint32_t)e->at * st / SEQ_SUB,
                           sn = (t + g / 2u) / g * g, end = (uint32_t)sq.h.pat[e->pat].len * SEQ_SUB;
            const uint32_t at = sn * SEQ_SUB / st;
            e->at = (uint16_t)(at >= end ? 0u : at);
        }
    }
    for (i = 1; i < sq.h.nev; i++)                       /* in order again (they moved a little) */
        for (j = i; j > 0 && ev_before(&sq.ev[j], &sq.ev[j - 1]); j--) {
            seq_ev_t t = sq.ev[j];
            sq.ev[j] = sq.ev[j - 1];
            sq.ev[j - 1] = t;
        }
    for (i = 0; i < sq.h.nev; i++)                       /* the same note twice at one time: the take's stays */
        for (j = i + 1; j < sq.h.nev && sq.ev[j].pat == sq.ev[i].pat && sq.ev[j].at - sq.ev[i].at < REPLACE_SUB; j++)
            if (sq.ev[j].val == sq.ev[i].val && ((sq.ev[i].vel | sq.ev[j].vel) & TAKE)) {
                const int go = sq.ev[i].vel & TAKE ? j : i;
                rm_keep(&sq.ev[go]);
                ev_del(go);
                i--;                                     /* (this one again: a third?) */
                break;
            }
    tr.dirty = 1;
}

static void record(uint32_t t, int kind, int val, int vel, uint32_t len_ticks, int key)
{
    seq_ev_t e;
    int pat;
    uint32_t at;
    if (rec_where(t, &pat, &at))
        return;
    take_begin();
    e.at = (uint16_t)at;
    e.len = (uint16_t)clampi_((int)(len_ticks * SEQ_SUB / (uint32_t)seq_step_ticks(pat)), 1, 0xFFFF);
    e.val = ev_val(kind, val);
    e.vel = (uint8_t)(clampi_(vel, 1, 127) | TAKE);
    e.pat = (uint8_t)pat;
    if (ev_put(e) < 0) {
        seq_out_say("SEQUENCE", "FULL");
        return;
    }
    if (key >= 0 && key < (int)(sizeof held_rec / sizeof held_rec[0])) {
        held_rec[key].on = 1;
        held_rec[key].at = e.at;
        held_rec[key].val = e.val;
        held_rec[key].pat = e.pat;
        held_rec[key].t = t;
    }
}

/* -------------------------------------------------------------- arp --- */
int seq_arp_on(void) { return proj.par[P_AMODE] != ARP_OFF; }
int seq_arp_held(void) { return arp.n; }

void seq_arp_key(int kind, int cents, int down)
{
    int i, k = 0;
    if (down) {
        if (proj.par[P_AHOLD] && arp.phys == 0)
            arp.n = 0;                                   /* a new chord replaces the latched one */
        arp.phys++;
        for (i = 0; i < arp.n; i++)
            if (arp.c[i] == cents && arp.k[i] == kind)
                return;
        if (arp.n < NHELD) {
            arp.c[arp.n] = (int16_t)cents;
            arp.k[arp.n++] = (uint8_t)kind;
        }
        if (arp.n == 1 && !arp.live) {                    /* the first: at once */
            arp.next = tr.abs;
            arp.idx = 0;
            arp.live = 1;
        }
        return;
    }
    if (arp.phys)
        arp.phys--;
    if (proj.par[P_AHOLD])
        return;
    for (i = 0; i < arp.n; i++)
        if (!(arp.c[i] == cents && arp.k[i] == kind)) {
            arp.c[k] = arp.c[i];
            arp.k[k++] = arp.k[i];
        }
    arp.n = (uint8_t)k;
}

static uint32_t arp_rnd(void)
{
    static uint32_t x = 0x6D2B79F5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

/* the note list (held notes by pitch or as played, over Octaves) and this step's pick */
static int arp_pick(int *kind, int *cents)
{
    int16_t c[NHELD];
    uint8_t k[NHELD];
    int n = arp.n, i, j, len, oct = clampi_(proj.par[P_AOCT], 1, 4), mode = proj.par[P_AMODE];
    uint32_t pick;
    if (!n)
        return -1;
    for (i = 0; i < n; i++) {
        c[i] = arp.c[i];
        k[i] = arp.k[i];
    }
    if (!proj.par[P_AORDER])                              /* Note: low to high */
        for (i = 1; i < n; i++)
            for (j = i; j > 0 && c[j - 1] > c[j]; j--) {
                int16_t t = c[j];
                uint8_t u = k[j];
                c[j] = c[j - 1];
                k[j] = k[j - 1];
                c[j - 1] = t;
                k[j - 1] = u;
            }
    if (mode == ARP_REPEAT) {                             /* the last played, again (no octaves) */
        *kind = arp.k[n - 1];
        *cents = arp.c[n - 1];
        return 0;
    }
    len = n * oct;
    switch (mode) {
    case ARP_DOWN: pick = (uint32_t)len - 1u - arp.idx % (uint32_t)len; break;
    case ARP_UPDN: {
        const uint32_t cyc = len > 1 ? 2u * (uint32_t)len - 2u : 1u, q = arp.idx % cyc;
        pick = q < (uint32_t)len ? q : cyc - q;
        break;
    }
    case ARP_RANDOM: pick = arp_rnd() % (uint32_t)len; break;
    default: pick = arp.idx % (uint32_t)len; break;
    }
    *kind = k[pick % (uint32_t)n];
    *cents = c[pick % (uint32_t)n] + 1200 * (int)(pick / (uint32_t)n);   /* an octave up: still in the pan's scale */
    if (*cents > 12700)
        *cents = 12700;
    return 0;
}

static void arp_run(uint32_t until)
{
    if (!seq_arp_on()) {
        arp.n = 0;
        arp.phys = 0;
    } else if (!proj.par[P_AHOLD] && !arp.phys) {
        arp.n = 0;                                      /* (Hold turned off: the latch lets go) */
    }
    if (!arp.n) {
        arp.live = 0;
        return;
    }
    while ((int32_t)(arp.next - until) < 0) {
        const uint32_t per = DIVT[proj.par[P_ARATE] < DIV_N ? proj.par[P_ARATE] : DIV_16],
                       sw = (uint32_t)proj.par[P_ASWING] * per / 250u, t = arp.next,
                       gate = per * (uint32_t)clampi_(proj.par[P_AGATE], 1, 127) / 128u;
        int kind, cents;
        if (arp_pick(&kind, &cents) == 0 && (int)(arp_rnd() % 127u) < proj.par[P_ACHANCE]) {
            const int vel = 100;
            seq_out_note(kind, cents, vel, delay_ms(t));
            off_add(t + (gate ? gate : 1u), kind, cents);
            record(t, kind, cents, vel, gate, -1);         /* recording: what the arp plays */
        }
        if (tr.run && (int32_t)(t - tr.tstart) >= 0) {   /* the transport running: on its grid */
            const uint32_t k = (t - tr.tstart) / per + 1u;
            arp.next = tr.tstart + k * per + ((k & 1u) ? sw : 0u);
        } else {
            arp.next = t + ((arp.idx & 1u) ? per - sw : per + sw);
        }
        arp.idx++;
    }
}

/* ---------------------------------------------------------- transport --- */
static void pat_start(int pat, uint32_t at)
{
    tr.pt0 = tr.t0;
    tr.pplen = tr.plen;
    tr.pcur = tr.cur;
    tr.cur = (uint8_t)pat;
    tr.t0 = at;
    tr.plen = pat_ticks(pat);
}

/* section s, if it plays a pattern (-1: past the last, or End: the song's end) */
static int song_find(int s) { return s >= 0 && s < SEQ_NSEC && sq.h.sec[s].pat < SEQ_NPAT ? s : -1; }

static void transport_stop(void)
{
    tr.run = 0;
    if (tr.arm) {
        tr.arm = 0;
        take_end();
    }
    offs_due(0, 1);                                      /* every note ends */
    tr.next = 0xFF;
    tr.stop_due = 0;
}

static void transport_start(uint32_t count)               /* count: ticks of count-in */
{
    int s = 0;
    tr.run = 1;
    tr.stop_due = 0;
    tr.next = 0xFF;
    tr.tstart = tr.abs + count;
    tr.done = tr.abs;
    tr.rep = 0;
    tr.sec = 0;
    if (sq.h.song != SONG_OFF && (s = song_find(0)) < 0) {
        seq_out_say("SONG", "IS EMPTY");
        tr.run = 0;
        return;
    }
    tr.plen = 0;
    pat_start(sq.h.song != SONG_OFF ? sq.h.sec[s].pat : sq.h.cur, tr.tstart);
    tr.pt0 = tr.t0;
    tr.pplen = tr.plen;
    tr.pcur = tr.cur;
    tr.rec_from = tr.tstart;
    if (arp.live)
        arp.next = tr.tstart;                            /* (the arp falls in with it) */
}

/* the pattern's end: the next loop (a chosen pattern, a song's next section, or the song's end) */
static void boundary(void)
{
    const uint32_t at = tr.t0 + tr.plen;
    int pat = tr.cur;
    if (sq.h.song != SONG_OFF) {
        if (++tr.rep >= sq.h.sec[tr.sec].rep) {
            int s = song_find(tr.sec + 1);
            tr.rep = 0;
            if (s < 0) {
                if (sq.h.song == SONG_LOOP && (s = song_find(0)) >= 0) {
                    tr.sec = (uint8_t)s;
                } else {
                    tr.stop_due = 1;                     /* the song's end: stop there */
                    tr.stop_at = at;
                    pat_start(pat, at);
                    tr.plen = 0xFFFFFFFu;               /* (nothing more of it posted) */
                    return;
                }
            } else {
                tr.sec = (uint8_t)s;
            }
        }
        pat = sq.h.sec[tr.sec].pat;
    } else if (tr.next < SEQ_NPAT) {
        pat = tr.next;
        tr.next = 0xFF;
    }
    pat_start(pat, at);
}

/* the pattern's events from tick a to b of this loop (pattern ticks) */
static void pat_events(uint32_t a, uint32_t b)
{
    const int p = tr.cur;
    const uint32_t end = (uint32_t)sq.h.pat[p].len * SEQ_SUB, st = (uint32_t)seq_step_ticks(p);
    int i, e;
    pat_span(p, &i, &e);
    for (; i < e; i++) {
        const seq_ev_t *v = &sq.ev[i];
        uint32_t t;
        if (v->at >= end)
            break;
        t = ev_tick(p, v->at);
        if (t < a)
            continue;
        if (t >= b)
            break;
        seq_out_note(ev_kind(v), ev_note(v), v->vel & 0x7F, delay_ms(tr.t0 + t));
        if (ev_kind(v) != EV_HIT)
            off_add(tr.t0 + t + (uint32_t)v->len * st / SEQ_SUB, ev_kind(v), ev_note(v));
    }
}

/* the beats (the click) and the groove's pulses in [a, b) */
static void beats(uint32_t a, uint32_t b)
{
    const int click = proj.par[P_CLICK];
    uint32_t t;
    int32_t k;
    k = (int32_t)(a - tr.tstart);
    k = k >= 0 ? (k + SEQ_PPQ - 1) / SEQ_PPQ : -(-k / SEQ_PPQ);
    for (t = tr.tstart + (uint32_t)(k * SEQ_PPQ); (int32_t)(t - b) < 0; t += SEQ_PPQ, k++)
        if (k < 0 || (click && (click > CLICK_LEVELS || tr.arm)))   /* (a count-in always clicks) */
            seq_out_click((((k % 4) + 4) % 4 == 0) | (k < 0) << 1, delay_ms(t));
    if (proj.par[P_GROOVE]) {
        k = (int32_t)(a - tr.tstart);
        if (k < 0)
            k = 0;
        k = (k + 127) / 128;
        for (t = tr.tstart + (uint32_t)k * 128u; (int32_t)(t - b) < 0; t += 128u, k++)
            seq_out_groove(k % 12, delay_ms(t));
    }
}

void seq_poll(void)
{
    uint32_t ahead;
    clock_run();
    ahead = tr.abs + 1u + (tr.ext ? 0u : (uint32_t)(LOOK_MS * seq_bpm() * SEQ_PPQ / 60000));
    if (tr.stop_due && (int32_t)(tr.abs - tr.stop_at) >= 0)
        transport_stop();
    if (!tr.run) {
        tr.done = ahead;
    } else {
        while ((int32_t)(tr.done - ahead) < 0) {
            uint32_t end = ahead;
            if (!tr.stop_due && (int32_t)(tr.done - (tr.t0 + tr.plen)) >= 0) {
                boundary();
                continue;
            }
            if (!tr.stop_due && (int32_t)(end - (tr.t0 + tr.plen)) > 0)
                end = tr.t0 + tr.plen;
            if (tr.stop_due && (int32_t)(end - tr.stop_at) > 0)
                end = tr.stop_at;
            beats(tr.done, end);
            if ((int32_t)(end - tr.t0) > 0 && !tr.stop_due) {
                const uint32_t a = (int32_t)(tr.done - tr.t0) > 0 ? tr.done - tr.t0 : 0u;
                pat_events(a, end - tr.t0);
            }
            if (tr.done == end)
                break;
            tr.done = end;
        }
    }
    arp_run(ahead);
    offs_due(ahead, 0);
}

void seq_init(void)
{
    memset(&tr, 0, sizeof tr);
    memset(&arp, 0, sizeof arp);
    memset(offs, 0, sizeof offs);
    memset(held_rec, 0, sizeof held_rec);
    tr.next = 0xFF;
    tr.last_ms = plat_ms();
    tr.cur = sq.h.cur;
}

void seq_midi_clock(int msg)
{
    switch (msg) {
    case KM_CLK_TICK:
        tr.ext = 1;
        tr.ext_ms = plat_ms();
        tr.abs += SEQ_PPQ / 24u;
        tr.acc = 0;
        break;
    case KM_CLK_START:
        if (tr.run)
            transport_stop();
        tr.ext = 1;
        tr.ext_ms = plat_ms();
        transport_start(0);
        break;
    case KM_CLK_CONTINUE:
        if (!tr.run)
            transport_start(0);
        break;
    default:
        if (tr.run)
            transport_stop();
        break;
    }
}

void seq_play(int on)
{
    clock_run();
    if (on && !tr.run) {
        uint32_t count = 0;
        if (tr.arm && !tr.ext)
            count = (uint32_t)proj.par[P_COUNTIN] * 4u * SEQ_PPQ;   /* armed: the count-in */
        transport_start(count);
    } else if (!on && tr.run) {
        transport_stop();
    }
}

void seq_rec_tap(void)
{
    clock_run();
    if (tr.arm) {
        tr.arm = 0;
        take_end();
        return;
    }
    tr.arm = 1;
    if (tr.run)                                          /* playing: from the pattern's next start */
        tr.rec_from = (int32_t)(tr.abs - tr.tstart) < 0 ? tr.tstart : tr.t0 + tr.plen;
}

void seq_rec_play(void)
{
    clock_run();
    if (tr.run) {
        if (!tr.arm)
            seq_rec_tap();
        return;
    }
    tr.arm = 1;
    seq_play(1);
}

/* a key's note: recorded while recording (an armed, stopped transport starts with it) */
void seq_note_on(int key, int kind, int val, int vel)
{
    clock_run();
    if (tr.arm && !tr.run) {                             /* the pattern's start sounds, then this note joins it */
        transport_start(0);
        beats(tr.abs, tr.abs + 1u);
        pat_events(0u, 1u);
        tr.done = tr.abs + 1u;
    }
    record(tr.abs, kind, val, vel, (uint32_t)seq_step_ticks(tr.cur) / 2u, key);
}

void seq_note_off(int key)
{
    int a, b, i;
    if (key < 0 || key >= (int)(sizeof held_rec / sizeof held_rec[0]) || !held_rec[key].on)
        return;
    held_rec[key].on = 0;
    clock_run();
    pat_span(held_rec[key].pat, &a, &b);
    for (i = a; i < b; i++)
        if (sq.ev[i].at == held_rec[key].at && sq.ev[i].val == held_rec[key].val) {
            const uint32_t d = tr.abs - held_rec[key].t;
            sq.ev[i].len = (uint16_t)clampi_((int)(d * SEQ_SUB / (uint32_t)seq_step_ticks(held_rec[key].pat)), 1, 0xFFFF);
            break;
        }
}

/* ----------------------------------------------------------- patterns --- */
void seq_select(int pat)
{
    pat &= 15;
    if (sq.h.cur != pat)
        tr.dirty = 1;
    sq.h.cur = (uint8_t)pat;                             /* (the one the pages edit) */
    if (tr.run && !sq.h.song)
        tr.next = pat == tr.cur ? 0xFF : (uint8_t)pat;   /* it plays from the loop's end */
    else if (!tr.run)
        tr.cur = (uint8_t)pat;
}

void seq_clear_pat(int pat)
{
    int a, b;
    undo_drop();
    pat_span(pat, &a, &b);
    ev_move(a, b, sq.h.nev - b);
    sq.h.nev = (uint16_t)(sq.h.nev - (b - a));
    pat_defaults(pat);
    tr.dirty = 1;
    if (tr.run && tr.cur == pat)
        tr.plen = pat_ticks(pat);
}

int seq_copy_pat(void)
{
    const int from = sq.h.cur;
    int to, a, b, i;
    for (to = (from + 1) % SEQ_NPAT; to != from; to = (to + 1) % SEQ_NPAT)
        if (!seq_used(to))
            break;
    if (to == from)
        return -1;
    undo_drop();
    pat_span(from, &a, &b);
    if (sq.h.nev + (b - a) > SEQ_NEV)
        return -1;
    sq.h.pat[to] = sq.h.pat[from];
    for (i = a; i < b; i++) {
        seq_ev_t e = sq.ev[i];
        e.pat = (uint8_t)to;
        e.vel &= 0x7Fu;
        ev_put(e);
        if (to < from) {                                 /* (inserted before: the source moved on) */
            a++;
            b++;
            i++;
        }
    }
    seq_select(to);
    tr.dirty = 1;
    return to;
}

void seq_retime(void)
{
    if (tr.run && !tr.stop_due)
        tr.plen = pat_ticks(tr.cur);
}

int seq_undo(void)
{
    int i;
    if (!tr.undo_ok || tr.take)
        return -1;
    for (i = sq.h.nev - 1; i >= 0; i--)                 /* the take's notes go */
        if (sq.ev[i].vel & TAKE)
            ev_del(i);
    for (i = 0; i < nrm; i++)                            /* the ones they replaced come back */
        ev_insert(rm[i]);
    nrm = 0;
    tr.undo_ok = 0;
    tr.dirty = 1;
    if (tr.run)
        tr.plen = pat_ticks(tr.cur);
    return 0;
}

/* --------------------------------------------------------- step edit --- */
int seq_step_ev(int pat, int step)
{
    int a, b;
    const int lo = step * SEQ_SUB - SEQ_SUB / 2, hi = step * SEQ_SUB + SEQ_SUB / 2;
    pat_span(pat, &a, &b);
    for (; a < b; a++)
        if (sq.ev[a].at >= lo && sq.ev[a].at < hi)
            return a;
    return -1;
}

int seq_step_count(int pat, int step)
{
    int a, b, n = 0;
    const int lo = step * SEQ_SUB - SEQ_SUB / 2, hi = step * SEQ_SUB + SEQ_SUB / 2;
    pat_span(pat, &a, &b);
    for (; a < b; a++)
        n += sq.ev[a].at >= lo && sq.ev[a].at < hi;
    return n;
}

void seq_step_rest(int pat, int step)
{
    int i;
    undo_drop();
    while ((i = seq_step_ev(pat, step)) >= 0)
        ev_del(i);
}

void seq_step_put(int pat, int step, int kind, int val, int vel, int steps)
{
    seq_ev_t e;
    e.at = (uint16_t)(step * SEQ_SUB);
    e.len = (uint16_t)(clampi_(steps, 1, SEQ_MAXLEN) * SEQ_SUB);
    e.val = ev_val(kind, val);
    e.vel = (uint8_t)clampi_(vel, 1, 127);
    e.pat = (uint8_t)pat;
    undo_drop();
    if (ev_put(e) < 0)
        seq_out_say("SEQUENCE", "FULL");
}
