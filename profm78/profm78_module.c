/* SPDX-License-Identifier: GPL-3.0-only */
/* PRO-FM-78: Melodee's Prophet-5 engine (github.com/keremimo/melodee, 1.6.1, 165d177; Kerem Kilic, Ellic Studio) as a
 * fm1-dock module, with Sequential's 200 factory programs, unchanged (p5/FACTORY.md): its engine as Melodee has
 * it (p5/eng_prophet.c, taken by take.py) on what it reads of Melodee (profm78_melodee.inc). Eight voices with the
 * host's second core (every other one on it), five without; POLY or UNISON as each program says; the filter (SSI
 * Rev 1/2 or Curtis Rev 3), Poly-Mod, the wheel's LFO, glide and VINTAGE are the program's. Melodee calls the
 * engine a prototype: not yet calibrated against a Prophet.
 *   keys / MIDI          notes; MIDI also the bend (the program's range), CC 1 the wheel, channel pressure
 *   ALGORITHM            the type: 1978 (the originals, 511 .. 558), BASS, LEAD .. FX (profm78_types.h), INIT
 *   PRESETS, SELECT      the type's programs
 *   HOME                 PRO-FM-78's home (and at power-on): cutoff, resonance, LFO amount, amp release on KNOB 1..4;
 *                        held 1.5 s the host's module list. The panel is PRO-FM-78's (HB_F_PANEL): its keys, OCT- /
 *                        OCT+ (shown on its own top line), the keys' force as velocity and aftertouch
 *   FX SEL ENV LFO EDIT  a page of four controls on KNOB 1..4, in their units (again: the section's next page, after
 *                        its last HOME): FX the mixer; SEL the chord generator (a key plays a chord, inverted, voiced,
 *                        strummed); ENV the envelopes; LFO the LFO, Poly-Mod; EDIT the filter, oscillators, waveforms.
 *                        A program's edits last until another program is picked
 *   ARP                  the arpeggiator (mode, rate, octaves, gate; again: tempo, swing, latch, velocity)
 *   SEQ                  the loop recorder's page (tempo, bars, click, transpose); PLAY starts / stops the loop; REC
 *                        stopped arms it (the first note starts it), playing records over it (again: stops
 *                        recording), held 1 s clears it; SAVE keeps the loop and the players' settings in flash
 *                        (profm78_play.inc). A DAW's MIDI clock sets the tempo, its start and stop play the loop
 *   CC 74 71 73 72       cutoff, resonance, amp attack, amp release, absolute (the host's KNOBS CC option)
 * The screen is a Prophet's: black, walnut cheeks, the program on a red LED display. Notes, controllers and program
 * changes reach the engine in the audio interrupt (a queue); the controls are bytes of the program the engine
 * reads each block. */
#include "profm78_melodee.inc"
#include "profm78_types.h"

#define T (&trk[0])
#define FIRST_1978 161u                         /* preset 161: Sequential's program 511 */
#define GAIN_Q12 680                            /* a five-note chord of the loudest of the 201 (Tight Bass: eight voices in
                                                 * unison, its peak as their phases fall) under full scale with ~5 % to
                                                 * spare (tests/profm78_host.c); the median ~-10 dBFS */

/* ---- the queue: main loop (note, midi, ui) -> audio interrupt */
enum { Q_KEY, Q_WHEEL, Q_PRESSURE, Q_BEND, Q_PRESET, Q_ALL_OFF, Q_CLK, Q_START, Q_CONT, Q_STOP, Q_PLAY, Q_REC,
       Q_CLEAR };
#define QN 64u
static uint32_t q[QN];
static volatile uint32_t q_w, q_r;
static void put(uint32_t kind, uint32_t a, uint32_t b)
{
    if (q_w - q_r < QN) {                       /* (full: dropped) */
        q[q_w % QN] = kind | a << 8 | b << 16;
        q_w = q_w + 1u;
    }
}

static volatile uint32_t cur;                   /* the preset sounding (audio interrupt's) */
static void load(uint32_t k)
{
    p5_note_off_all(T);
    p5_preset_loaded(T, k);                     /* the factory program k - 1 (0: INIT), its voice mode */
    cur = k;
}

#include "profm78_play.inc"

static void take(uint32_t m)                    /* one queued event, in the audio interrupt */
{
    uint32_t a = m >> 8 & 0xFFu, b = m >> 16;
    switch (m & 0xFFu) {
    case Q_KEY: key_in(a, b); break;
    case Q_WHEEL: T->mw = (uint8_t)a; break;
    case Q_PRESSURE: T->at = (uint8_t)a; break;
    case Q_BEND: T->bend_raw = (int16_t)((int32_t)(a | b << 7) - 8192); break;
    case Q_PRESET: load(a | b << 8); break;
    case Q_ALL_OFF: players_all_off(); p5_note_off_all(T); break;   /* (released: their tails ring out, the program
                                                                     * as it is, the loop playing on) */
    case Q_CLK: clock_in(); break;
    case Q_START: clock_start(1); break;
    case Q_CONT: clock_start(0); break;
    case Q_STOP: if (playing) seq_stop(); break;
    case Q_PLAY: if (playing) seq_stop(); else seq_start(); break;   /* (armed: recording from the top) */
    case Q_REC:                                 /* stopped: armed (the first note starts it); playing: overdub on, off */
        if (rec == R_ON) { rec_close(); rec = R_OFF; }
        else rec = rec == R_ARMED ? R_OFF : playing ? R_ON : R_ARMED;
        break;
    case Q_CLEAR: seq_clear(); break;
    }
}

static void render(int32_t *out, uint32_t n)
{
    int32_t buf[CTL];
    uint32_t i, at;
    while (q_r != q_w) {
        take(q[q_r % QN]);
        q_r = q_r + 1u;
    }
    for (at = 0; at + CTL <= n; at += CTL) {    /* shortcut: a block not a multiple of CTL leaves its rest silent; the host's are */
        clock_block();
        p5_block_render(T, buf);
        now_s += CTL;
        for (i = 0; i < CTL; i++) {
            int32_t s = clamp(((buf[i] * GAIN_Q12) >> 12) + click_sample(), -32767, 32767);
            out[2u * (at + i)] += s;
            out[2u * (at + i) + 1u] += s;
        }
    }
}

/* ---- the programs by type: ALGORITHM a type, PRESETS its programs (each type keeps its place) */
#define TN (T_N + 2u)                           /* 1978, the types of profm78_types.h, INIT */
static uint8_t members[200u + 40u + 1u], t_start[TN], t_count[TN], t_pos[TN];
static uint32_t type, want;                     /* the type shown, the preset asked for */
static uint32_t edited;                         /* a control turned since the program was picked */
static const char *type_name(uint32_t t) { return !t ? "1978" : t <= T_N ? TYPE_NAME[t - 1u] : "INIT"; }
static void types_init(void)
{
    uint32_t t, k, n = 0;
    for (t = 0; t < TN; t++) {
        t_start[t] = (uint8_t)n;
        for (k = 1; k <= 200u; k++)
            if (!t ? k >= FIRST_1978 : t <= T_N && TYPE_OF[k - 1u] == t - 1u)
                members[n++] = (uint8_t)k;
        if (t == TN - 1u)
            members[n++] = 0;                   /* INIT */
        t_count[t] = (uint8_t)(n - t_start[t]);
    }
}
static void pick(uint32_t k)                    /* preset k (0: INIT, 1..200: the factory programs) */
{
    want = k;
    edited = 0;
    put(Q_PRESET, k & 0xFFu, k >> 8);
}
static void pick_in_type(void) { pick(members[t_start[type] + t_pos[type]]); }

/* ---- the controls: bytes of the program (prophet_patch.c), each shown in its unit */
enum { U_PCT, U_CUT, U_TIME, U_SEMI, U_FREQB, U_CENTS, U_GLIDE, U_LFO, U_KEYS, U_ONOFF, U_WAVEA, U_WAVEB, U_WAVEL, U_PW,
       U_CHORD, U_INV, U_VOICING, U_STRUM, U_AMODE, U_RATE, U_AOCT, U_AGATE, U_TEMPO, U_SWING, U_LATCH, U_VELO, U_BARS,
       U_CLICK, U_TRANSP };                    /* (U_CHORD ..: the players' settings, gen[], not the program's) */
typedef struct { const char *label; uint8_t idx, unit; } ctl_t;
typedef struct { const char *title; uint8_t btn; ctl_t c[4]; } page_t;
#define NO_BTN 0xFFu                            /* HOME: no section button */
static const page_t PAGES[] = {                 /* a section's pages next to each other: a press the next, after its last HOME */
    {"HOME", NO_BTN, {{"CUTOFF", P5_CUTOFF, U_CUT}, {"RESO", P5_RESONANCE, U_PCT},
                      {"LFO", P5_LFO_INITIAL, U_PCT}, {"RELEASE", P5_RELEASE_AMP, U_TIME}}},
    {"FILTER", HB_BTN_EDIT, {{"CUTOFF", P5_CUTOFF, U_CUT}, {"RESO", P5_RESONANCE, U_PCT},
                             {"ENV AMT", P5_ENV_FILTER, U_PCT}, {"KEYBD", P5_KEY_FILTER, U_KEYS}}},
    {"OSCILLATORS", HB_BTN_EDIT, {{"FREQ A", P5_FREQ_A, U_SEMI}, {"FREQ B", P5_FREQ_B, U_FREQB},
                                  {"FINE B", P5_FINE_B, U_CENTS}, {"GLIDE", P5_GLIDE, U_GLIDE}}},
    {"WAVEFORMS", HB_BTN_EDIT, {{"OSC A", P5_SAW_A, U_WAVEA}, {"OSC B", P5_SAW_B, U_WAVEB},
                                {"PW A", P5_PW_A, U_PW}, {"SYNC", P5_SYNC, U_ONOFF}}},
    {"CHORD", HB_BTN_SEL, {{"CHORD", G_CHORD, U_CHORD}, {"INVERT", G_INV, U_INV}, {"VOICING", G_VOICING, U_VOICING},
                           {"STRUM", G_STRUM, U_STRUM}}},
    {"FILTER ENVELOPE", HB_BTN_ENV, {{"ATTACK", P5_ATTACK_FILTER, U_TIME}, {"DECAY", P5_DECAY_FILTER, U_TIME},
                                     {"SUSTAIN", P5_SUSTAIN_FILTER, U_PCT}, {"RELEASE", P5_RELEASE_FILTER, U_TIME}}},
    {"AMPLIFIER ENVELOPE", HB_BTN_ENV, {{"ATTACK", P5_ATTACK_AMP, U_TIME}, {"DECAY", P5_DECAY_AMP, U_TIME},
                                        {"SUSTAIN", P5_SUSTAIN_AMP, U_PCT}, {"RELEASE", P5_RELEASE_AMP, U_TIME}}},
    {"MIXER", HB_BTN_FX, {{"OSC A", P5_LEVEL_A, U_PCT}, {"OSC B", P5_LEVEL_B, U_PCT},
                          {"NOISE", P5_NOISE, U_PCT}, {"VINTAGE", P5_VINTAGE, U_PCT}}},
    {"LFO", HB_BTN_LFO, {{"RATE", P5_LFO_RATE, U_LFO}, {"AMOUNT", P5_LFO_INITIAL, U_PCT},
                         {"NOISE", P5_WHEEL_MIX, U_PCT}, {"WAVE", P5_LFO_SAW, U_WAVEL}}},
    {"POLY-MOD", HB_BTN_LFO, {{"FIL ENV", P5_POLY_ENV, U_PCT}, {"OSC B", P5_POLY_B, U_PCT},
                              {">FREQ A", P5_POLY_FREQ, U_ONOFF}, {">FILTER", P5_POLY_FILTER, U_ONOFF}}},
    {"ARPEGGIATOR", HB_BTN_ARP, {{"MODE", G_AMODE, U_AMODE}, {"RATE", G_ARATE, U_RATE}, {"OCTAVES", G_AOCT, U_AOCT},
                                 {"GATE", G_AGATE, U_AGATE}}},
    {"ARP OPTIONS", HB_BTN_ARP, {{"TEMPO", G_TEMPO, U_TEMPO}, {"SWING", G_SWING, U_SWING}, {"LATCH", G_LATCH, U_LATCH},
                                 {"VELO", G_VELO, U_VELO}}},
    {"LOOP RECORDER", HB_BTN_SEQ, {{"TEMPO", G_TEMPO, U_TEMPO}, {"BARS", G_BARS, U_BARS}, {"CLICK", G_CLICK, U_CLICK},
                                   {"TRANSP", G_TRANSP, U_TRANSP}}},
};
#define NPAGES (sizeof PAGES / sizeof PAGES[0])
static const uint8_t PAGE_BTN[5] = {HB_BTN_FX, HB_BTN_SEL, HB_BTN_ENV, HB_BTN_LFO, HB_BTN_EDIT};   /* the panel's order */
static const char *const BTN_NAME[5] = {"FX", "SEL", "ENV", "LFO", "EDIT"};
static uint32_t page;                           /* 0: HOME */

static uint8_t *raw(void) { return p5_patch_of(T)->raw; }
static int keyed_b(void) { return raw()[P5_KEY_B] != 0; }
static int32_t ctl_max(const ctl_t *c)
{
    switch (c->unit) {
    case U_SEMI: return 48;                     /* semitone detents: raw 0, 2 .. 96 (-12 .. +36) */
    case U_FREQB: return keyed_b() ? 48 : 127;  /* keyboard off: B's own nine octaves */
    case U_KEYS: return 2;
    case U_ONOFF: return 1;
    case U_WAVEA: return 3;
    case U_WAVEB: case U_WAVEL: return 7;
    case U_CHORD: return N_CHORDS - 1;
    case U_INV: return 3;
    case U_VOICING: return 2;
    case U_STRUM: return 20;
    case U_AMODE: case U_RATE: return 5;
    case U_AOCT: case U_BARS: return 3;
    case U_AGATE: return 9;
    case U_TEMPO: return 140;
    case U_SWING: return 25;
    case U_LATCH: return 1;
    case U_CLICK: return 2;
    case U_TRANSP: return 24;
    default: return 127;
    }
}
static int32_t ctl_get(const ctl_t *c)
{
    const uint8_t *r = raw();
    if (c->unit >= U_CHORD)
        return gen[c->idx];
    switch (c->unit) {
    case U_SEMI: return clamp(r[c->idx], 0, 96) / 2;
    case U_FREQB: return keyed_b() ? clamp(r[c->idx], 0, 96) / 2 : clamp(r[c->idx], 0, 127);
    case U_WAVEA: return !!r[c->idx] | !!r[c->idx + 1u] << 1;
    case U_WAVEB: case U_WAVEL: return !!r[c->idx] | !!r[c->idx + 1u] << 1 | !!r[c->idx + 2u] << 2;
    default: return clamp(r[c->idx], 0, ctl_max(c));
    }
}
static void ctl_set(const ctl_t *c, int32_t v)  /* (main loop: one byte at a time, the engine reads them a block each) */
{
    uint8_t *r = raw();
    v = clamp(v, 0, ctl_max(c));
    if (c->unit >= U_CHORD) {                   /* (the generator's, not the program's: no EDITED) */
        gen[c->idx] = (uint8_t)v;
        return;
    }
    if (c->unit == U_SEMI || (c->unit == U_FREQB && keyed_b()))
        r[c->idx] = (uint8_t)(v * 2);
    else if (c->unit >= U_WAVEA && c->unit <= U_WAVEL) {
        r[c->idx] = (uint8_t)(v & 1);
        r[c->idx + 1u] = (uint8_t)(v >> 1 & 1);
        if (c->unit != U_WAVEA) r[c->idx + 2u] = (uint8_t)(v >> 2 & 1);
    } else
        r[c->idx] = (uint8_t)v;
    edited = 1;
}

/* text: s gets v with d decimals (rounded) and unit; returns s */
static char *fmt(char *s, float v, uint32_t d, const char *unit)
{
    uint32_t i = 0, k, p = d == 2u ? 100u : d ? 10u : 1u, n = (uint32_t)(v * (float)p + 0.5f), w;
    char t[12];
    for (w = 0, k = n / p; ; k /= 10u) { t[w++] = (char)('0' + k % 10u); if (k < 10u) break; }
    while (w) s[i++] = t[--w];
    if (d) {
        s[i++] = '.';
        if (d == 2u) s[i++] = (char)('0' + n / 10u % 10u);
        s[i++] = (char)('0' + n % 10u);
    }
    while (*unit) s[i++] = *unit++;
    s[i] = 0;
    return s;
}
static char *fmt_auto(char *s, float v, const char *unit)   /* 3 significant digits: 4.21, 43.6, 870 */
{
    return fmt(s, v, v < 9.995f ? 2u : v < 99.95f ? 1u : 0u, unit);
}
static char *fmt_signed(char *s, int32_t v, const char *unit)
{
    s[0] = v < 0 ? '-' : '+';
    fmt(s + 1, (float)(v < 0 ? -v : v), 0, unit);
    return s;
}
/* the engine's mappings (eng_prophet.c, p5_tables.h), at the program's settings */
static float cutoff_hz(uint32_t v)              /* at the lowest C (MIDI 36), key tracking's anchor */
{
    uint32_t ssi = !raw()[P5_FILTER_REV], open = 127u - v > 66u ? 66u : 127u - v;
    float x = (float)(1618u + v * 236u + (ssi ? open * 38u : 0u)) / 256.0f;
    return 30.0f * fm_exp2f(x * (9.0589f / 127.0f));   /* the table's 30 Hz .. 16 kHz over 0..127 */
}
static float time_ms(uint32_t v) { return fm_exp2f((float)v * (13.2877f / 127.0f)); }   /* 1 ms .. 10 s */
static void ctl_text(char *s, const ctl_t *c, int32_t v)
{
    static const char *const WA[4] = {"OFF", "SAW", "PULSE", "SAW+PLS"};
    static const char *const WB[8] = {"OFF", "SAW", "TRI", "SAW+TRI", "PULSE", "SAW+PLS", "TRI+PLS", "ALL"};
    static const char *const WL[8] = {"OFF", "SAW", "TRI", "SAW+TRI", "SQUARE", "SAW+SQR", "TRI+SQR", "ALL"};
    static const char *const KB[3] = {"OFF", "HALF", "FULL"};
    float f;
    switch (c->unit) {
    case U_PCT: fmt(s, (float)v * (100.0f / 127.0f), 0, "%"); break;
    case U_CUT:
        f = cutoff_hz((uint32_t)v);
        if (f < 999.5f) fmt_auto(s, f, " Hz"); else fmt_auto(s, f * 0.001f, "kHz");
        break;
    case U_TIME:
        f = time_ms((uint32_t)v);
        if (f < 999.5f) fmt_auto(s, f, " ms"); else fmt_auto(s, f * 0.001f, " s");
        break;
    case U_SEMI: fmt_signed(s, v - 12, " st"); break;
    case U_FREQB:
        if (keyed_b()) { fmt_signed(s, v - 12, " st"); break; }
        f = 440.0f * fm_exp2f(((float)v * (108.0f / 127.0f) + 12.0f - 69.0f) * (1.0f / 12.0f));
        fmt_auto(s, raw()[P5_LOW_B] ? f * (1.0f / 128.0f) : f, " Hz");   /* (LO FREQ: seven octaves down) */
        break;
    case U_CENTS: fmt_signed(s, v * 100 / 128, " ct"); break;
    case U_GLIDE: {                             /* the engine's step: 1/16 semitone a block of CTL samples */
        int32_t step = (int32_t)(ENV_LIN[v] >> 14);
        if (!v) { str_cpy(s, "OFF", 8); break; }
        fmt_auto(s, 192.0f * CTL * 1000.0f / (44100.0f * (float)(step < 1 ? 1 : step)), " ms");   /* an octave */
        break;
    }
    case U_LFO: fmt_auto(s, (float)P5_LFO_INC[v] * (44100.0f / 4294967296.0f), " Hz"); break;
    case U_KEYS: str_cpy(s, KB[v], 8); break;
    case U_ONOFF: str_cpy(s, v ? "ON" : "OFF", 8); break;
    case U_WAVEA: str_cpy(s, WA[v], 8); break;
    case U_WAVEB: str_cpy(s, WB[v], 8); break;
    case U_WAVEL: str_cpy(s, WL[v], 8); break;
    case U_CHORD: str_cpy(s, CHORDS[v].name, 8); break;
    case U_INV: { static const char *const I[4] = {"ROOT", "1ST", "2ND", "3RD"}; str_cpy(s, I[v], 8); break; }
    case U_VOICING: { static const char *const V[3] = {"CLOSE", "OPEN", "+BASS"}; str_cpy(s, V[v], 8); break; }
    case U_STRUM: if (v) fmt(s, (float)(v * 10), 0, " ms"); else str_cpy(s, "OFF", 8); break;
    case U_AMODE: { static const char *const M[6] = {"OFF", "UP", "DOWN", "UP/DN", "RANDOM", "PLAYED"}; str_cpy(s, M[v], 8); break; }
    case U_RATE: { static const char *const R[6] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"}; str_cpy(s, R[v], 8); break; }
    case U_AOCT: fmt(s, (float)(v + 1), 0, " OCT"); break;
    case U_AGATE: fmt(s, (float)(v * 10 + 10), 0, "%"); break;
    case U_TEMPO:
        if (ext_bpm && ext_on()) { str_cpy(s, "EXT ", 8); fmt(s + 4, (float)ext_bpm, 0, ""); }
        else fmt(s, (float)(v + 60), 0, " BPM");
        break;
    case U_SWING: fmt(s, (float)(v + 50), 0, "%"); break;
    case U_LATCH: str_cpy(s, v ? "ON" : "OFF", 8); break;
    case U_VELO: if (v) fmt(s, (float)v, 0, ""); else str_cpy(s, "PLAYED", 8); break;
    case U_BARS: if (v) fmt(s, (float)(1 << v), 0, " BARS"); else str_cpy(s, "1 BAR", 8); break;
    case U_CLICK: { static const char *const C[3] = {"OFF", "REC", "ON"}; str_cpy(s, C[v], 8); break; }
    case U_TRANSP: fmt_signed(s, v - 12, " st"); break;
    case U_PW: fmt(s, (float)clamp(v * 258, 1024, 31743) * (100.0f / 32768.0f), 0, "%"); break;   /* (p5_pw's limits) */
    }
}

/* ---- the screen: a Prophet's black panel between walnut cheeks, the host's line above (HB_TOP) */
#define C_LEGEND HB_RGB(236, 228, 208)
#define C_VALUE  HB_RGB(255, 255, 255)
#define C_DIM    HB_RGB(104, 98, 90)
#define C_LED    HB_RGB(255, 40, 24)
#define C_LEDOFF HB_RGB(30, 5, 4)
#define C_BEZEL  HB_RGB(20, 16, 14)
#define C_EDITED HB_RGB(255, 150, 40)
#define CHEEK 8                                 /* the walnut's width a side */
#define PANEL_W (240 - 2 * CHEEK)
#define COL_W (PANEL_W / 4)
#define Y_PROG 24
#define Y_HEAD 102
#define Y_KNOBS 126
#define Y_FOOT 224

static uint16_t mix(uint32_t r0, uint32_t g0, uint32_t b0, uint32_t r1, uint32_t g1, uint32_t b1, float t)
{
    return HB_RGB((uint32_t)((float)r0 + ((float)r1 - (float)r0) * t), (uint32_t)((float)g0 + ((float)g1 - (float)g0) * t),
                  (uint32_t)((float)b0 + ((float)b1 - (float)b0) * t));
}
static void wood(int32_t x0, float phase, int32_t inner)   /* a walnut cheek: grain that wanders down it */
{
    int32_t x, y, h = 240 - Y_PROG;
    H->cv_begin(CHEEK, h, 0);
    for (y = 0; y < h; y++)
        for (x = 0; x < CHEEK; x++) {
            float g = fm_sinf((float)x * 0.9f + phase + (float)y * 0.04f + 2.2f * fm_sinf((float)y * 0.019f + phase));
            float t = g * g;                    /* (dark lines, wide light bands) */
            uint16_t c = mix(66, 34, 16, 124, 72, 36, t);
            if (x == inner) c = mix(30, 16, 8, 66, 34, 16, t * 0.5f);   /* the shadow on the panel side */
            else if (x == (inner ? 0 : CHEEK - 1)) c = mix(124, 72, 36, 150, 96, 52, t);   /* the rounded edge */
            H->cv_pixel(x, y, c);
        }
    H->cv_blit(x0, Y_PROG);
}

static void seg_digit(int32_t x, int32_t y, int32_t d)   /* a red seven-segment digit, 18 x 32 (d < 0: "-") */
{
    static const uint8_t SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};   /* gfedcba */
    static const int8_t R[7][4] = {{4, 0, 10, 3}, {15, 4, 3, 11}, {15, 17, 3, 11}, {4, 29, 10, 3}, {0, 17, 3, 11},
                                   {0, 4, 3, 11}, {4, 14, 10, 3}};   /* (a pixel apart at the corners, as an LED's) */
    uint32_t on = d < 0 ? 0x40u : SEG[d], k;
    for (k = 0; k < 7u; k++)
        H->cv_rect(x + R[k][0], y + R[k][1], R[k][2], R[k][3], on >> k & 1u ? C_LED : C_LEDOFF);
}
static void draw_program(void)
{
    char s[24];
    preset_t pr;
    uint32_t f = want - 1u, i = 0;
    H->cv_begin(PANEL_W, Y_HEAD - Y_PROG, 0);
    H->cv_rect(4, 6, 68, 42, C_BEZEL);          /* the LED window: group, bank, program */
    seg_digit(9, 11, want ? (int32_t)(f / 40u + 1u) : -1);
    seg_digit(29, 11, want ? (int32_t)(f % 40u / 8u + 1u) : -1);
    seg_digit(49, 11, want ? (int32_t)(f % 8u + 1u) : -1);
    H->cv_text(82, 4, 1, type_name(type), C_LEGEND);
    fmt(s, (float)(t_pos[type] + 1u), 0, " OF ");
    while (s[i]) i++;
    fmt(s + i, (float)t_count[type], 0, "");
    H->cv_text(84, 36, 0, s, C_DIM);
    if (gen[G_CHORD])                               /* the chord generator on: which chord */
        H->cv_text(PANEL_W - 4 - H->text_w(0, CHORDS[gen[G_CHORD]].name), 36, 0, CHORDS[gen[G_CHORD]].name, C_EDITED);
    E->preset_make(want, &pr, s);
    H->cv_text(6, 56, 0, s, C_VALUE);
    if (edited)
        H->cv_text(PANEL_W - 4 - H->text_w(0, "EDITED"), 56, 0, "EDITED", C_EDITED);
    H->cv_blit(CHEEK, Y_PROG);
}
static const char *toast;                       /* a word for a moment in the section's place (LOOP SAVED) */
static uint32_t toast_ms, hold;                /* hold: REC held to clear, 1 .. 16 of the way */
static void say(const char *t) { toast = t; toast_ms = H->ms(); }
static void draw_head(void)                     /* the section's name between rules, as on the panel */
{
    const char *t = hold ? "HOLD TO CLEAR" : toast ? toast : PAGES[page].title;
    int32_t w = H->text_w(0, t), x = (PANEL_W - w) / 2;
    H->cv_begin(PANEL_W, 22, 0);
    H->cv_rect(4, 10, x - 10, 1, C_LEGEND);
    H->cv_rect(x + w + 6, 10, PANEL_W - 4 - (x + w + 6), 1, C_LEGEND);
    H->cv_text(x, 3, 0, t, hold ? C_LED : toast ? C_EDITED : C_LEGEND);
    if (hold) H->cv_rect(4, 18, (PANEL_W - 8) * (int32_t)hold / 16, 3, C_LED);   /* full: cleared */
    H->cv_blit(CHEEK, Y_HEAD);
}
static void draw_foot(void)                     /* the section buttons, the lit one's section in the legend's colour */
{
    uint32_t k;
    H->cv_begin(PANEL_W, 240 - Y_FOOT, 0);
    for (k = 0; k < 5u; k++)
        H->cv_text(4 + (int32_t)k * 43 + (43 - H->text_w(0, BTN_NAME[k])) / 2, 0, 0, BTN_NAME[k],
                   PAGES[page].btn == PAGE_BTN[k] ? C_LEGEND : C_DIM);
    H->cv_blit(CHEEK, Y_FOOT);
}
/* the small font set close (7 px a glyph, 4 a space: its glyphs are 7 wide): SUSTAIN, "26.1 ms" in a column */
static int32_t ntext_w(const char *s)
{
    int32_t w = 0;
    for (; *s; s++) w += *s == ' ' ? 4 : 7;
    return w;
}
static void ntext(int32_t x, const char *s, int32_t y, uint16_t c)   /* centred in a column */
{
    char g[2] = {0, 0};
    for (x += (COL_W - ntext_w(s)) / 2; *s; s++) {
        g[0] = *s;
        if (*s != ' ') H->cv_text(x, y, 0, g, c);
        x += *s == ' ' ? 4 : 7;
    }
}
static void draw_knob(uint32_t k, int32_t v)    /* KNOB k+1: its name, a black knob over its scale, its value */
{
    const ctl_t *c = &PAGES[page].c[k];
    char s[16];
    int32_t cx = COL_W / 2, cy = 44, x, y, i;
    float a = (-0.75f + 1.5f * (float)v / (float)ctl_max(c)) * FM_PI, sn = fm_sinf(a), cs = fm_cosf(a);
    H->cv_begin(COL_W, Y_FOOT - Y_KNOBS, 0);
    ntext(0, c->label, 0, C_LEGEND);
    for (i = 0; i <= 10; i++) {                 /* the scale: eleven ticks over 270 degrees */
        float t = (-0.75f + 0.15f * (float)i) * FM_PI, ts = fm_sinf(t), tc = fm_cosf(t);
        H->cv_line(cx + (int32_t)(21.0f * ts), cy - (int32_t)(21.0f * tc), cx + (int32_t)(24.0f * ts),
                   cy - (int32_t)(24.0f * tc), C_DIM);
    }
    for (y = -18; y <= 18; y++)                 /* the knob: black, a grey rim, lit from above */
        for (x = -18; x <= 18; x++) {
            int32_t d = x * x + y * y;
            if (d <= 18 * 18)
                H->cv_pixel(cx + x, cy + y, d > 16 * 16 ? (y < 0 ? HB_RGB(120, 120, 116) : HB_RGB(70, 70, 68))
                                                        : y < -10 ? HB_RGB(40, 40, 40) : HB_RGB(22, 22, 22));
        }
    for (i = -1; i <= 1; i++)                   /* its white line */
        H->cv_line(cx + i, cy, cx + i + (int32_t)(15.0f * sn), cy - (int32_t)(15.0f * cs), C_VALUE);
    ctl_text(s, c, v);
    ntext(0, s, 74, C_VALUE);
    H->cv_blit(CHEEK + (int32_t)k * COL_W, Y_KNOBS);
}

static uint32_t d_want = ~0u, d_page = ~0u, d_cur = ~0u, d_edited, d_type, d_chord;
static int32_t d_val[4];
static uint32_t knob_ms[4];

/* ---- the panel is the module's (HB_F_PANEL): its keys, OCT- / OCT+, a short HOME (held 1.5 s: the host's list) */
#define KEY0 53                                 /* key 0 = F3, as the host plays them */
static int32_t oct, d_oct = 99;                 /* -2 .. +2 */
static uint8_t key_note[27];                    /* the note each key started: its release stops that one */
static uint8_t key_held[27], key_lit[27];       /* (a key's light: held, or its note played by the arp or the loop) */
static uint32_t home_ms, rec_ms, rec_down, rec_done;   /* rec_done: cleared while still held */
static char d_top[16] = "?";
static void top_state(char *s)                  /* the players on the top line: ARP, ARMED, REC 2.3, PLAY 1.4 */
{
    uint32_t i = 0, t = loop_t;
    if (gen[G_AMODE]) { str_cpy(s, "ARP ", 8); i = 4; }
    if (rec == R_ARMED) str_cpy(s + i, "ARMED", 8);
    else if (playing) {
        str_cpy(s + i, rec == R_ON ? "REC " : "PLAY ", 8);
        while (s[i]) i++;
        s[i++] = (char)('1' + t / 96u);
        s[i++] = '.';
        s[i++] = (char)('1' + t % 96u / 24u);
        s[i] = 0;
    } else s[i] = 0;
}
static void draw_top(void)                      /* the host's line, the module's now (HB_F_SCREEN): name, players,
                                                 * octave */
{
    char s[8] = "OCT +0";
    s[4] = oct < 0 ? '-' : '+';
    s[5] = (char)('0' + (oct < 0 ? -oct : oct));
    H->cv_begin(240, 20, 0);
    H->cv_text(4, 2, 0, "PRO-FM-78", C_EDITED);
    H->cv_text(88, 2, 0, d_top, rec ? C_LED : C_LEGEND);
    H->cv_text(236 - H->text_w(0, s), 2, 0, s, C_DIM);
    H->cv_rect(0, 19, 240, 1, C_DIM);
    H->cv_blit(0, 0);
}

static uint8_t d_led[4] = {9, 9, 9, 9};       /* ARP SEQ PLAY REC */
static uint32_t d_ext;
static const char *d_toast;
static uint32_t d_hold;
static int str_eq(const char *a, const char *b) { while (*a && *a == *b) a++, b++; return *a == *b; }
static void draw(uint32_t full)
{
    static const uint8_t PBTN[4] = {HB_BTN_ARP, HB_BTN_SEQ, HB_BTN_PLAY, HB_BTN_REC};
    uint32_t k, now = H->ms(), led[4];
    char st[16];
    if (full || d_want == ~0u) {
        H->fill(0, 0, 240, 240, 0);
        wood(0, 0.0f, CHEEK - 1);
        wood(240 - CHEEK, 2.1f, 0);
        d_want = d_page = d_cur = ~0u;
        d_oct = 99;
    }
    top_state(st);
    if (oct != d_oct || !str_eq(st, d_top)) {
        d_oct = oct;
        str_cpy(d_top, st, sizeof d_top);
        draw_top();
    }
    if (toast && now - toast_ms > 1500u)        /* (the word gone: the section's name again) */
        toast = 0;
    if (cur != d_cur) {                         /* a program loaded: every value again (some depend on others) */
        d_cur = cur;
        d_val[0] = d_val[1] = d_val[2] = d_val[3] = -1;
    }
    if (want != d_want || edited != d_edited || type != d_type || gen[G_CHORD] != d_chord) {
        d_chord = gen[G_CHORD];
        d_want = want;
        d_edited = edited;
        d_type = type;
        draw_program();
    }
    if (page != d_page) {
        d_page = page;
        draw_head();
        draw_foot();
        for (k = 0; k < 5u; k++)
            H->led(PAGE_BTN[k], PAGES[page].btn == PAGE_BTN[k] ? 1u : 2u);   /* (the others glow; HOME: all) */
        d_val[0] = d_val[1] = d_val[2] = d_val[3] = -1;
    }
    if (rec_down && !rec_done && now - rec_ms >= 1000u) {   /* REC held 1 s: cleared, without waiting for it to go */
        put(Q_CLEAR, 0, 0); say("LOOP CLEARED");
        rec_done = 1;
    }
    hold = rec_down && !rec_done && now - rec_ms >= 250u ? 1u + (now - rec_ms - 250u) * 15u / 750u : 0u;
    if (toast != d_toast || hold != d_hold) {
        d_toast = toast;
        d_hold = hold;
        draw_head();
    }
    if ((ext_on() ? ext_bpm : 0u) != d_ext) {   /* a MIDI clock comes or goes, or changes: TEMPO again */
        d_ext = ext_on() ? ext_bpm : 0u;
        d_val[0] = -1;
    }
    led[0] = gen[G_AMODE] || PAGES[page].btn == HB_BTN_ARP ? 1u : 2u;
    led[1] = playing || PAGES[page].btn == HB_BTN_SEQ ? 1u : 2u;
    led[2] = playing ? 1u : 2u;
    led[3] = rec == R_ON ? 1u : rec == R_ARMED ? (now / 250u & 1u) : 2u;   /* (armed: flashing) */
    for (k = 0; k < 4u; k++)
        if (led[k] != d_led[k]) H->led(PBTN[k], d_led[k] = (uint8_t)led[k]);
    for (k = 0; k < 27u; k++) {                 /* the keys: held, or their note played by the arp or the loop */
        int32_t n = KEY0 + (int32_t)k + 12 * oct;
        uint32_t on = key_held[k] || (n >= 0 && n < 128 && (lit[n >> 5] >> (n & 31) & 1u));
        if (on != key_lit[k]) H->led(HB_LED_KEY(k), key_lit[k] = (uint8_t)on);
    }
    for (k = 0; k < 4u; k++) {
        int32_t v = ctl_get(&PAGES[page].c[k]);
        if (v != d_val[k]) {
            d_val[k] = v;
            draw_knob(k, v);
        }
    }
}

/* ---- SAVE: the loop and the players' settings in PRO-FM-78's flash bank, loaded at the start */
#define SV_MAGIC 0x31504F4Cu                    /* "LOP1" */
static struct { uint32_t magic; uint8_t gen[16]; ev_t ev[NEV]; } sv;
static void loop_save(void)
{
    uint32_t i;
    if (playing || rec) { say("STOP THE LOOP FIRST"); return; }   /* (a save stalls the sound ~100 ms) */
    for (i = 0; i < NVOICE; i++)
        if (voices[i].active || an) { say("LET NOTES END"); return; }   /* (release tails too; a latched arp) */
    if (!HB_HOST_HAS(H, bank_save)) { say("NO SAVE HERE"); return; }
    sv.magic = SV_MAGIC;
    memcpy(sv.gen, gen, sizeof gen);
    memcpy(sv.ev, ev, sizeof ev);
    say(H->bank_save(&sv, sizeof sv) == 0 ? "LOOP SAVED" : "SAVE FAILED");
}
static void loop_load(void)
{
    uint32_t i;
    if (!HB_HOST_HAS(H, bank_load) || H->bank_load(&sv, sizeof sv) != 0 || sv.magic != SV_MAGIC)
        return;
    memcpy(gen, sv.gen, sizeof gen);
    memcpy(ev, sv.ev, sizeof ev);
    for (i = 0; i < NEV; i++)
        ev[i].fl &= EV_USED | EV_CHORD;
}

static void note(uint32_t ch, uint32_t key, uint32_t vel);
static void ui(uint32_t ev, int32_t arg)
{
    uint32_t id = HB_EV_ID(ev), k;
    switch (HB_EV_KIND(ev)) {
    case HB_EV_KEY_DOWN:                        /* arg: a sensed key's velocity, else 0 (100, as the host plays them) */
        if (id < 27u) {
            key_note[id] = (uint8_t)clamp(KEY0 + (int32_t)id + 12 * oct, 0, 127);
            note(0, key_note[id], arg > 0 ? (uint32_t)arg : 100u);
            H->led(HB_LED_KEY(id), key_held[id] = key_lit[id] = 1);
        }
        return;
    case HB_EV_KEY_UP:
        if (id < 27u) {
            note(0, key_note[id], 0);
            H->led(HB_LED_KEY(id), key_held[id] = key_lit[id] = 0);
        }
        return;
    case HB_EV_KEY_PRESSURE: put(Q_PRESSURE, (uint32_t)arg, 0); return;
    case HB_EV_BTN_UP:                          /* HOME let go soon: PRO-FM-78's home (held 1.5 s the host lets it go
                                                 * for us, then opens its list) */
        if (id == HB_BTN_HOME && H->ms() - home_ms < 1400u)
            page = 0;
        if (id == HB_BTN_REC && rec_down) {     /* REC: a press arms, records, stops recording; held 1 s clears (draw) */
            rec_down = 0;
            if (!rec_done && H->ms() - rec_ms < 1000u) put(Q_REC, 0, 0);
            else if (!rec_done) { put(Q_CLEAR, 0, 0); say("LOOP CLEARED"); }   /* (let go before draw saw 1 s) */
        }
        return;
    default: break;
    }
    if (HB_EV_KIND(ev) == HB_EV_BTN_DOWN) {     /* a section: its first page, again its next, after its last HOME */
        if (id == HB_BTN_HOME) home_ms = H->ms();
        else if (id == HB_BTN_REC) { rec_ms = H->ms(); rec_down = 1; rec_done = 0; }
        else if (id == HB_BTN_PLAY) put(Q_PLAY, 0, 0);
        else if (id == HB_BTN_SAVE) loop_save();
        else if (id == HB_BTN_OCT_DOWN && oct > -2) oct--;
        else if (id == HB_BTN_OCT_UP && oct < 2) oct++;
        else if (PAGES[page].btn == id)
            page = page + 1u < NPAGES && PAGES[page + 1u].btn == id ? page + 1u : 0u;
        else
            for (k = 0; k < NPAGES; k++)
                if (PAGES[k].btn == id) { page = k; break; }
        return;
    }
    if (HB_EV_KIND(ev) != HB_EV_ENC)
        return;
    if (id == HB_ENC_ALGORITHM) {
        type = (uint32_t)(((int32_t)type + arg % (int32_t)TN + (int32_t)TN) % (int32_t)TN);
        pick_in_type();
    } else if (id == HB_ENC_PRESETS || id == HB_ENC_SELECT) {
        int32_t n = t_count[type];
        t_pos[type] = (uint8_t)(((int32_t)t_pos[type] + arg % n + n) % n);
        pick_in_type();
    } else if (id >= HB_ENC_KNOB1 && id <= HB_ENC_KNOB4) {
        const ctl_t *c = &PAGES[page].c[id - HB_ENC_KNOB1];
        uint32_t i = id - HB_ENC_KNOB1, now = H->ms();
        int32_t step = ctl_max(c) > 48 && now - knob_ms[i] < 40u ? 4 : 1;   /* turned fast: four a detent */
        knob_ms[i] = now;
        ctl_set(c, ctl_get(c) + arg * step);
    }
}

static void note(uint32_t ch, uint32_t key, uint32_t vel)   /* keys and MIDI alike: to the players (the interrupt) */
{
    (void)ch;
    put(Q_KEY, key & 127u, vel & 127u);
}
static void cc(uint32_t ch, uint32_t num, uint32_t val)
{
    static const ctl_t SOUND[4] = {{"", P5_CUTOFF, U_CUT}, {"", P5_RESONANCE, U_PCT}, {"", P5_ATTACK_AMP, U_TIME},
                                   {"", P5_RELEASE_AMP, U_TIME}};   /* MIDI's sound controllers 74, 71, 73, 72 */
    (void)ch;
    if (num == 1u) put(Q_WHEEL, val, 0);
    else if (num == 74u) ctl_set(&SOUND[0], (int32_t)val);
    else if (num == 71u) ctl_set(&SOUND[1], (int32_t)val);
    else if (num == 73u) ctl_set(&SOUND[2], (int32_t)val);
    else if (num == 72u) ctl_set(&SOUND[3], (int32_t)val);
    else if (num == 120u || num == 123u) {      /* all sound / notes off (a DAW's start and stop): every note let go,
                                                 * the program and its edits kept */
        put(Q_ALL_OFF, 0, 0);
    }
}
static void midi(uint32_t st, uint32_t d1, uint32_t d2)
{
    if (st >= 0xF8u) {                          /* a DAW's clock and transport (USB): the arp and the loop follow */
        if (st == 0xF8u) put(Q_CLK, 0, 0);
        else if (st == 0xFAu) put(Q_START, 0, 0);
        else if (st == 0xFBu) put(Q_CONT, 0, 0);
        else if (st == 0xFCu) put(Q_STOP, 0, 0);
        return;
    }
    switch (st & 0xF0u) {
    case 0x80u: note(0, d1, 0); break;
    case 0x90u: note(0, d1, d2); break;
    case 0xB0u: cc(0, d1, d2); break;
    case 0xD0u: put(Q_PRESSURE, d1, 0); break;
    case 0xE0u: put(Q_BEND, d1, d2); break;
    default: break;
    }
}

static int init(const hb_host_t *h)
{
    H = h;
    c1_lent = HB_HOST_HAS(H, c1_wait);
    types_init();
    type = 0;                                   /* 1978, its first: 511 */
    want = FIRST_1978;
    load(want);
    loop_load();
    H->log("profm78: PRO-FM-78 (Melodee's Prophet engine)");
    return 0;
}

static void release(void)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        voices[i].active = voices[i].gate = 0;
    T->nmono = T->mono_note = 0;
    playing = 0;                                /* (the loop stopped, kept until another module's RAM) */
    rec = R_OFF;
    rec_down = 0;
    players_all_off();
    for (i = 0; i < 5u; i++)
        H->led(PAGE_BTN[i], 0);
    H->led(HB_BTN_ARP, 0); H->led(HB_BTN_SEQ, 0); H->led(HB_BTN_PLAY, 0); H->led(HB_BTN_REC, 0);
    d_led[0] = d_led[1] = d_led[2] = d_led[3] = 9;
    memset(key_lit, 0, sizeof key_lit);
    memset(key_held, 0, sizeof key_held);
    for (i = 0; i < 27u; i++)
        H->led(HB_LED_KEY(i), 0);
    d_want = ~0u;                               /* (back: all of the screen again) */
}

HB_DESC = {HB_ABI, "PRO-FM-78", init, release, note, cc, render, ui, draw, 0, 0, HB_F_SCREEN | HB_F_PANEL, midi};
