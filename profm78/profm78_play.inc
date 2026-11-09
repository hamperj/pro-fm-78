/* SPDX-License-Identifier: GPL-3.0-only */
/* PRO-FM-78's players, all in the audio interrupt: the chord generator (SEL), the arpeggiator (ARP) and the loop
 * recorder (SEQ, PLAY, REC), on one clock of 24 ticks a beat (TEMPO's, or a MIDI clock's when one comes). A key or
 * a MIDI note (Q_KEY) goes through the chord generator, then to the arpeggiator when it is on, else to the voices.
 * REC records into a loop of 1-8 bars what is played: keys at the nearest 1/16 (played back through the chord
 * generator, as keys), or with the arp on the arp's notes where they fell (played back as they are). The loop plays
 * past the arp, through the chord generator, transposed. SWING shifts the notes of each 1/8 of both. The settings
 * (gen[]) are bytes the main loop writes; everything else here is the interrupt's. */
#ifndef VOICE_TRACE
#define VOICE_TRACE(note, vel) ((void)0)        /* (tests/profm78_host.c: every note the voices get) */
#endif

enum { G_CHORD, G_INV, G_VOICING, G_STRUM, G_AMODE, G_ARATE, G_AOCT, G_AGATE, G_TEMPO, G_SWING, G_LATCH, G_VELO,
       G_BARS, G_CLICK, G_TRANSP, G_N };
static uint8_t gen[16] = {[G_ARATE] = 3, [G_AGATE] = 4, [G_TEMPO] = 60, [G_BARS] = 1, [G_CLICK] = 1, [G_TRANSP] = 12};
/* G_AMODE: OFF UP DOWN UP/DN RANDOM PLAYED; G_ARATE: RATE_T; G_AOCT: 1..4; G_AGATE: 10..100 %; G_TEMPO: 60 + v BPM;
 * G_SWING: 50 + v %; G_VELO: 0 as played, else that; G_BARS: 1 << v; G_CLICK: OFF REC ON; G_TRANSP: v - 12 st */
enum { A_OFF, A_UP, A_DOWN, A_UPDN, A_RANDOM, A_PLAYED };
static const uint8_t RATE_T[6] = {24, 12, 8, 6, 4, 3};   /* 1/4 1/8 1/8T 1/16 1/16T 1/32, in ticks */
#define LOOP_T (96u << (gen[G_BARS] & 3u))     /* the loop's length in ticks */
#define TICK_S (44100.0f * 60.0f / 24.0f)      /* samples a tick, times the BPM */

static uint32_t now_s;                          /* samples since init (a control block at a time) */
static volatile uint32_t lit[4];                /* the notes the arp and the loop sound: the keys' lights follow */
static void voice_on(uint32_t note, uint32_t vel, uint32_t show)
{
    p5_key(T, note, vel);
    if (show) lit[note >> 5 & 3u] |= 1u << (note & 31u);
    VOICE_TRACE(note, vel);
}
static void voice_off(uint32_t note)
{
    p5_key(T, note, 0);
    lit[note >> 5 & 3u] &= ~(1u << (note & 31u));
    VOICE_TRACE(note, 0u);
}

/* ---- what comes later: strums, swung notes, releases; due at a sample */
enum { K_LIVE_STRUM, K_SEQ_STRUM, K_SEQ_EV, K_SEQ_CHOFF, K_SEQ_VOFF, K_ARP_ON, K_ARP_OFF };
#define K_SEQ ((1u << K_SEQ_STRUM) | (1u << K_SEQ_EV) | (1u << K_SEQ_CHOFF) | (1u << K_SEQ_VOFF))
#define K_OFFS ((1u << K_SEQ_CHOFF) | (1u << K_SEQ_VOFF) | (1u << K_ARP_OFF))
typedef struct { uint32_t due; uint16_t idx; uint8_t kind, key, note, vel; } sch_t;
#define NSCH 64u
static sch_t sch[NSCH], sch_due[NSCH];
static uint32_t n_sch;
static void fire(const sch_t *e);
static void later(uint32_t delay, uint32_t kind, uint32_t key, uint32_t note, uint32_t vel, uint32_t idx)
{
    sch_t e;
    e.due = now_s + delay; e.idx = (uint16_t)idx; e.kind = (uint8_t)kind;
    e.key = (uint8_t)key; e.note = (uint8_t)note; e.vel = (uint8_t)vel;
    if (n_sch < NSCH) sch[n_sch++] = e;
    else if (K_OFFS >> kind & 1u) fire(&e);     /* (full: a release now rather than a note left sounding) */
}
static void sch_run(void)                       /* the due ones, in the order they were asked for */
{
    uint32_t i, k = 0, nd = 0;
    for (i = 0; i < n_sch; i++)
        if ((int32_t)(now_s - sch[i].due) >= 0) sch_due[nd++] = sch[i];
        else sch[k++] = sch[i];
    n_sch = k;                                  /* (what they ask for in turn comes after) */
    for (i = 0; i < nd; i++)
        fire(&sch_due[i]);
}
static void sch_cancel(uint32_t kinds, int32_t key, uint32_t release)   /* kinds (bits), key (< 0: any): dropped,
                                                                         * their releases done now if asked */
{
    static sch_t r[NSCH];                       /* (the interrupt's stack is small; a release here cancels with
                                                 * release 0 only, so this is never used twice at once) */
    uint32_t i, k = 0, nr = 0;
    for (i = 0; i < n_sch; i++)
        if (kinds >> sch[i].kind & 1u && (key < 0 || sch[i].key == (uint32_t)key)) {
            if (release && K_OFFS >> sch[i].kind & 1u) r[nr++] = sch[i];
        } else
            sch[k++] = sch[i];
    n_sch = k;
    for (i = 0; i < nr; i++)
        fire(&r[i]);
}

/* ---- the clock: the next tick and how far towards it; a MIDI clock's ticks when they come */
static uint32_t tick_next, ext_n, clk_s, clk_seen, beat_s[8], n_beat;
static float tfrac = 1.0f, spt_ext = TICK_S / 120.0f;
static volatile uint32_t ext_bpm;               /* a MIDI clock's tempo (0: none), for the screen */
static int ext_on(void) { return clk_seen && now_s - clk_s < 22050u; }
static float spt(void) { return ext_on() ? spt_ext : TICK_S / (float)(60u + gen[G_TEMPO]); }
static float pos_now(void) { float p = (float)tick_next - 1.0f + (tfrac < 1.0f ? tfrac : 1.0f); return p < 0.0f ? 0.0f : p; }
static void clk_restart(void) { tick_next = 0; tfrac = 1.0f; }   /* tick 0 on the next control block */
static uint32_t swing_s(uint32_t t)             /* tick t's delay: each 1/8's notes shifted in proportion, its off-beat 1/16
                                                 * most (SWING 75 %: by a 1/32), so a 1/32's stay in order */
{
    uint32_t p = t % 12u;
    return (uint32_t)((float)((p < 6u ? p : 12u - p) * gen[G_SWING]) * (1.0f / 50.0f) * spt());
}

/* ---- the chord generator (SEL): a key plays a chord, its inversion, its voicing, strummed upwards. The keys'
 * chords and the loop's are apart (SRC); a note still to be strummed has bit 7 set */
#define N_CHORDS 15u
static const struct { const char *name; uint8_t n, iv[4]; } CHORDS[N_CHORDS] = {
    {"OFF", 1, {0}}, {"MAJOR", 3, {0, 4, 7}}, {"MINOR", 3, {0, 3, 7}}, {"SUS2", 3, {0, 2, 7}}, {"SUS4", 3, {0, 5, 7}},
    {"POWER", 3, {0, 7, 12}}, {"OCTAVE", 2, {0, 12}}, {"MAJ7", 4, {0, 4, 7, 11}}, {"MIN7", 4, {0, 3, 7, 10}},
    {"DOM7", 4, {0, 4, 7, 10}}, {"ADD9", 4, {0, 4, 7, 14}}, {"MINADD9", 4, {0, 3, 7, 14}}, {"DIM", 3, {0, 3, 6}},
    {"AUG", 3, {0, 4, 8}}, {"4THS", 3, {0, 5, 10}}};
enum { SRC_LIVE, SRC_SEQ };
static uint8_t chord_notes[2][128][5], chord_n[2][128];
static void live_out(uint32_t note, uint32_t vel);
static void chord_out(uint32_t src, uint32_t note, uint32_t vel)
{
    if (src == SRC_LIVE) live_out(note, vel);
    else if (vel) voice_on(note, vel, 1);
    else voice_off(note);
}
static void chord_on(uint32_t src, uint32_t key, uint32_t vel)
{
    uint32_t c = gen[G_CHORD] % N_CHORDS, n = CHORDS[c].n, i, j, inv = gen[G_INV] % n;
    int32_t v[5];
    for (i = 0; i < n; i++)
        v[i] = (int32_t)key + CHORDS[c].iv[i];
    for (i = 0; i < inv; i++) {                 /* an inversion: the lowest an octave up, in order again */
        int32_t lo = v[0] + 12;
        for (j = 1; j < n; j++) v[j - 1u] = v[j];
        v[n - 1u] = lo;
    }
    if (gen[G_VOICING] == 1u && n > 2u) v[1] += 12;   /* OPEN: the second up an octave */
    if (gen[G_VOICING] == 2u) v[n++] = v[0] - 12;     /* +BASS: the lowest doubled an octave down */
    chord_n[src][key] = 0;
    for (i = 0; i < n; i++) {
        uint32_t note = (uint32_t)clamp(v[i], 1, 127), at = gen[G_VOICING] == 2u ? (i + 1u) % n : i;   /* (the bass first) */
        if (!gen[G_STRUM] || !at) {
            chord_notes[src][key][chord_n[src][key]++] = (uint8_t)note;
            chord_out(src, note, vel);
        } else {
            chord_notes[src][key][chord_n[src][key]++] = (uint8_t)(note | 0x80u);
            later(at * gen[G_STRUM] * 441u, src == SRC_LIVE ? K_LIVE_STRUM : K_SEQ_STRUM, key, note, vel, 0);
        }
    }
}
static void chord_strum(uint32_t src, uint32_t key, uint32_t note, uint32_t vel)   /* a strummed note falls due */
{
    uint32_t i;
    for (i = 0; i < chord_n[src][key]; i++)
        if (chord_notes[src][key][i] == (note | 0x80u)) {
            chord_notes[src][key][i] = (uint8_t)note;
            chord_out(src, note, vel);
            return;
        }
}
static void chord_off(uint32_t src, uint32_t key)   /* the notes key's chord sounded let go, the rest never played */
{
    uint32_t i;
    sch_cancel(1u << (src == SRC_LIVE ? K_LIVE_STRUM : K_SEQ_STRUM), (int32_t)key, 0);
    for (i = 0; i < chord_n[src][key]; i++)
        if (!(chord_notes[src][key][i] & 0x80u))
            chord_out(src, chord_notes[src][key][i], 0);
    chord_n[src][key] = 0;
}

/* ---- the loop: up to NEV notes, each at a tick of it for so many ticks */
#define NEV 256u
enum { EV_USED = 1, EV_CHORD = 2, EV_OPEN = 4, EV_SKIP = 8 };   /* CHORD: a key's (through the chord generator);
                                                                 * OPEN: still held; SKIP: recorded ahead of the
                                                                 * playhead, not played back on this pass */
typedef struct { uint16_t at, len; uint8_t note, vel, fl, pad; } ev_t;
static ev_t ev[NEV];
enum { R_OFF, R_ARMED, R_ON };
static volatile uint32_t playing, rec, loop_t;  /* loop_t: the loop's tick now (the screen's bar and beat) */
static uint16_t rec_open[128];                  /* a key held while recording: its note + 1 */
static uint32_t rec_abs[128];                   /* (and where it began, in the clock's ticks) */
static uint32_t ev_free(void)
{
    uint32_t i;
    for (i = 0; i < NEV && ev[i].fl; i++) ;
    return i;                                   /* (NEV: full, the note is not recorded) */
}
static void rec_key(uint32_t key, uint32_t vel) /* a key while REC records: at the nearest 1/16, until let go */
{
    uint32_t q = (uint32_t)(pos_now() + 3.0f) / 6u * 6u, i, L = LOOP_T;
    if (!vel) {
        if ((i = rec_open[key]) != 0u) {
            uint32_t len = q > rec_abs[key] ? q - rec_abs[key] : 6u;
            ev[i - 1u].len = (uint16_t)(len > L ? L : len);
            ev[i - 1u].fl &= (uint8_t)~EV_OPEN;
            rec_open[key] = 0;
        }
        return;
    }
    if (rec != R_ON || !playing || (i = ev_free()) == NEV)
        return;
    ev[i].at = (uint16_t)(q % L); ev[i].len = 0; ev[i].note = (uint8_t)key; ev[i].vel = (uint8_t)vel;
    ev[i].fl = (uint8_t)(EV_USED | EV_CHORD | EV_OPEN | (q >= tick_next ? EV_SKIP : 0));
    rec_open[key] = (uint16_t)(i + 1u);
    rec_abs[key] = q;
}
static void rec_close(void)                     /* recording stops: the keys still held end here */
{
    uint32_t k;
    for (k = 0; k < 128u; k++)
        if (rec_open[k]) rec_key(k, 0);
}
static void rec_note(uint32_t t, uint32_t note, uint32_t vel, uint32_t len)   /* an arp note at tick t, as it fell */
{
    uint32_t i, L = LOOP_T;
    if (rec != R_ON || !playing || (i = ev_free()) == NEV)
        return;
    ev[i].at = (uint16_t)(t % L); ev[i].len = (uint16_t)(len < 1u ? 1u : len > L ? L : len);
    ev[i].note = (uint8_t)note; ev[i].vel = (uint8_t)vel; ev[i].fl = EV_USED;
}
static void seq_fire(uint32_t i)                /* loop note i sounds, transposed, for its length */
{
    const ev_t *e = &ev[i];
    int32_t note = (int32_t)e->note + gen[G_TRANSP] - 12;
    uint32_t len = (uint32_t)((float)e->len * spt());
    if (!(e->fl & EV_USED) || note < 1 || note > 127)
        return;
    if (e->fl & EV_CHORD) {
        if (chord_n[SRC_SEQ][note]) {           /* (the same key again before its end: that one ends now) */
            sch_cancel(1u << K_SEQ_CHOFF, note, 0);
            chord_off(SRC_SEQ, (uint32_t)note);
        }
        chord_on(SRC_SEQ, (uint32_t)note, e->vel);
        later(len, K_SEQ_CHOFF, (uint32_t)note, 0, 0, 0);
    } else {
        voice_on((uint32_t)note, e->vel, 1);
        later(len, K_SEQ_VOFF, 0, (uint32_t)note, 0, 0);
    }
}
static void seq_start(void)
{
    if (!ext_on()) clk_restart();               /* (a MIDI clock: the loop where the DAW's bars are) */
    playing = 1;
    if (rec == R_ARMED) rec = R_ON;
}
static void seq_stop(void)
{
    rec_close();
    rec = R_OFF;
    playing = 0;
    sch_cancel(K_SEQ, -1, 1);
}
static void seq_clear(void)
{
    sch_cancel(K_SEQ, -1, 1);
    memset(ev, 0, sizeof ev);
    memset(rec_open, 0, sizeof rec_open);
}
static uint32_t seq_has(void)
{
    uint32_t i;
    for (i = 0; i < NEV; i++)
        if (ev[i].fl) return 1;
    return 0;
}

/* ---- the arpeggiator: the notes held (after the chord generator), in the order they came */
static struct { uint8_t note, vel; } ah[16];
static uint32_t an, a_down, ai;
static int32_t a_rng = 0x1234567;
static uint8_t to_arp[128];                     /* where a note went: its release follows it */
static void arp_add(uint32_t note, uint32_t vel)
{
    uint32_t i;
    if (gen[G_LATCH] && !a_down) an = 0;        /* LATCH: a new hand once all were let go */
    a_down++;
    for (i = 0; i < an; i++)
        if (ah[i].note == note) return;
    if (an < 16u) {
        ah[an].note = (uint8_t)note;
        ah[an++].vel = (uint8_t)vel;
    }
    if (an == 1u && !playing && !ext_on()) {    /* the first note: on the beat, now */
        clk_restart();
        ai = 0;
    }
}
static void arp_remove(uint32_t note)
{
    uint32_t i, k = 0;
    if (a_down) a_down--;
    if (gen[G_LATCH]) return;
    for (i = 0; i < an; i++)
        if (ah[i].note != note) ah[k++] = ah[i];
    an = k;
}
static void live_out(uint32_t note, uint32_t vel)
{
    if (vel) {
        to_arp[note] = gen[G_AMODE] != A_OFF;
        if (to_arp[note]) arp_add(note, vel); else voice_on(note, vel, 0);
    } else if (to_arp[note]) arp_remove(note);
    else voice_off(note);
}
static void arp_play(uint32_t note, uint32_t vel, uint32_t gate)
{
    voice_on(note, vel, 1);
    later(gate, K_ARP_OFF, 0, note, 0, 0);
}
static void arp_step(uint32_t t, uint32_t r, uint32_t delay)   /* tick t, r ticks a step: the next note */
{
    uint8_t s[16];
    uint32_t n = an, N = n * (gen[G_AOCT] % 4u + 1u), i, j, k = ai++, idx, note, vel = 100, gate, P;
    for (i = 0; i < n; i++) s[i] = ah[i].note;
    if (gen[G_AMODE] != A_PLAYED)
        for (i = 1; i < n; i++)                 /* low to high */
            for (j = i; j && s[j - 1u] > s[j]; j--) { uint8_t x = s[j]; s[j] = s[j - 1u]; s[j - 1u] = x; }
    switch (gen[G_AMODE]) {
    case A_DOWN: idx = N - 1u - k % N; break;
    case A_UPDN: P = N > 1u ? 2u * N - 2u : 1u; k %= P; idx = k < N ? k : P - k; break;
    case A_RANDOM: idx = noise32(&a_rng) % N; break;
    default: idx = k % N; break;                /* UP, PLAYED */
    }
    note = s[idx % n] + 12u * (idx / n);
    if (note > 127u)
        return;
    for (i = 0; i < n; i++)
        if (ah[i].note == s[idx % n]) vel = ah[i].vel;
    if (gen[G_VELO]) vel = gen[G_VELO];
    gate = (uint32_t)((float)r * spt() * (float)(gen[G_AGATE] % 10u + 1u) * 0.1f);
    if (gate > 65535u) gate = 65535u;
    if (delay) later(delay, K_ARP_ON, 0, note, vel, gate);
    else arp_play(note, vel, gate);
    rec_note(t, note, vel, (r * (gen[G_AGATE] % 10u + 1u) + 5u) / 10u);
}

/* ---- the click (CLICK): a short decaying sine on the beat, higher on the bar's first */
static float ck_y1, ck_y2, ck_c;
static uint32_t ck_n;
#define CK_R2 (0.9975f * 0.9975f)
static void click(uint32_t accent)
{
    float w = accent ? 0.3562f : 0.2137f;       /* 2.5 kHz, 1.5 kHz */
    ck_c = 2.0f * 0.9975f * fm_cosf(w);
    ck_y1 = 5000.0f * fm_sinf(w);
    ck_y2 = 0.0f;
    ck_n = 2000u;
}
static int32_t click_sample(void)
{
    float y;
    if (!ck_n) return 0;
    ck_n--;
    y = ck_c * ck_y1 - CK_R2 * ck_y2;
    ck_y2 = ck_y1;
    ck_y1 = y;
    return (int32_t)y;
}

static void on_tick(uint32_t t)
{
    uint32_t i, r = RATE_T[gen[G_ARATE] % 6u], sw;
    if (gen[G_AMODE] == A_OFF) an = a_down = 0;
    else if (!gen[G_LATCH] && !a_down) an = 0;  /* (LATCH let go with no key held) */
    if (playing) {
        uint32_t lt = t % LOOP_T;
        loop_t = lt;
        if (lt % 24u == 0u && (gen[G_CLICK] == 2u || (gen[G_CLICK] == 1u && rec == R_ON)))
            click(lt % 96u == 0u);
        for (i = 0; i < NEV; i++) {
            ev_t *e = &ev[i];
            if (!(e->fl & EV_USED) || e->at != lt)
                continue;
            if (e->fl & (EV_OPEN | EV_SKIP)) {  /* (recorded just now: from the next pass) */
                e->fl &= (uint8_t)~EV_SKIP;
                continue;
            }
            if ((sw = swing_s(lt)) != 0u) later(sw, K_SEQ_EV, 0, 0, 0, i);
            else seq_fire(i);
        }
    }
    if (an && t % r == 0u)
        arp_step(t, r, swing_s(t));
}
static void clock_block(void)                   /* a control block: what falls due, the ticks it passes */
{
    int ext = ext_on();
    sch_run();
    if (ext && tick_next + 1u < ext_n) {        /* (behind the MIDI clock: catch up at once) */
        while (tick_next + 1u < ext_n) on_tick(tick_next++);
        tfrac = 0.0f;
    }
    tfrac += (float)CTL / spt();
    while (tfrac >= 1.0f && (!ext || tick_next <= ext_n)) {   /* (a MIDI clock: at most a tick ahead of it) */
        tfrac -= 1.0f;
        on_tick(tick_next++);
    }
    if (tfrac > 1.0f) tfrac = 1.0f;
}
static void clock_in(void)                      /* a MIDI clock's tick (24 a beat) */
{
    if (!ext_on()) {                            /* (the first: its ticks counted from ours) */
        ext_n = tick_next;
        n_beat = 0;
    }
    if (ext_n % 24u == 0u) {                    /* its tempo over the last 8 beats (the queue's 5.8 ms blocks blur a tick) */
        uint32_t d = n_beat ? now_s - beat_s[(n_beat < 8u ? 0u : n_beat) % 8u] : 0u,
                 nb = n_beat < 8u ? n_beat : 8u;
        if (nb && d > 8820u * nb && d < 176400u * nb) {   /* (20 .. 300 BPM) */
            spt_ext = (float)d / (24.0f * (float)nb);
            ext_bpm = (uint32_t)(TICK_S / spt_ext + 0.5f);
        }
        beat_s[n_beat++ % 8u] = now_s;
    }
    clk_s = now_s;
    clk_seen = 1;
    ext_n++;
}
static void clock_start(uint32_t from_top)      /* a DAW's start (from the top) or continue */
{
    if (from_top) {
        tick_next = ext_n = n_beat = 0;
        tfrac = 1.0f;
        clk_s = now_s;
        clk_seen = 1;
    }
    if (seq_has() || rec == R_ARMED) {
        playing = 1;
        if (rec == R_ARMED) rec = R_ON;
    }
}

static void key_in(uint32_t key, uint32_t vel)  /* a key or a MIDI note: recorded, then through the chord generator */
{
    chord_off(SRC_LIVE, key);                   /* (a key again before its release: its chord again) */
    if (vel && rec == R_ARMED) seq_start();     /* armed: the first note starts the loop */
    if (gen[G_AMODE] == A_OFF || !vel) rec_key(key, vel);   /* (the arp on: its notes are recorded instead; a release
                                                 * always, for a key held as the arp came on) */
    if (vel) chord_on(SRC_LIVE, key, vel);
}
static void players_all_off(void)               /* all notes off: every player lets go; the loop plays on */
{
    n_sch = 0;
    memset(chord_n, 0, sizeof chord_n);
    memset(to_arp, 0, sizeof to_arp);
    an = a_down = 0;
    rec_close();
    lit[0] = lit[1] = lit[2] = lit[3] = 0;
}

static void fire(const sch_t *e)
{
    switch (e->kind) {
    case K_LIVE_STRUM: chord_strum(SRC_LIVE, e->key, e->note, e->vel); break;
    case K_SEQ_STRUM: chord_strum(SRC_SEQ, e->key, e->note, e->vel); break;
    case K_SEQ_EV: seq_fire(e->idx); break;
    case K_SEQ_CHOFF: chord_off(SRC_SEQ, e->key); break;
    case K_SEQ_VOFF: case K_ARP_OFF: voice_off(e->note); break;
    case K_ARP_ON: arp_play(e->note, e->vel, e->idx); break;
    default: break;
    }
}
