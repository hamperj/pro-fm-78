/* SPDX-License-Identifier: GPL-3.0-only */
/* What Melodee's Prophet engine (p5/eng_prophet.c, taken by take.py) reads of Melodee around it, on the fm1-dock
 * host: core.h's track, voice and engine structures (the fields it reads), dsp.c's helpers (noise, the dither,
 * cents_inc), and of voice.c what runs an engine with its own envelopes (ownenv): the voices for POLY and UNISON,
 * the bend's smoothing, a block, and its audio worker on the host's second core. After Melodee 1.6.1 (Kerem Kilic,
 * Ellic Studio; GPL-3.0-only). One part, eight voices on two cores (five on one), 440 Hz, none of Melodee's matrix,
 * LFO or track levels (the Prophet has its own). */
#include <stdint.h>
#include "hb_module.h"

void *memcpy(void *d, const void *s, __SIZE_TYPE__ n);   /* (modules/lib/hb_lib.c; a computer's libc: tests/profm78_host.c) */
void *memset(void *d, int c, __SIZE_TYPE__ n);
int memcmp(const void *a, const void *b, __SIZE_TYPE__ n);

/* where Melodee keeps the engine's sample kernels and per-sample tables: in RAM, away from the flash cache */
#ifdef __APPLE__                                /* (tests/profm78_host.c on a Mac: Mach-O's segment,section) */
#define P5_RAM_TEXT "__TEXT,__p5_rt,regular,pure_instructions"
#define P5_RAM_DATA "__DATA,__p5_tables"
#else
#define P5_RAM_TEXT ".ram_text"
#define P5_RAM_DATA ".data.p5_tables"
#endif

#define P5_OVERSAMPLE 1u                        /* (as Melodee ships it: 2x did not fit its budget) */
#define NPART 1u
#define MELODEE_DUAL_CORE 1                     /* eight voices while core 1 renders every other one, else five */
#define NVOICE 8u                               /* = the engine's P5_POLY with two cores */
#define VOICE_FS 24000                          /* one voice near -6 dBFS (dsp.c) */
#define RES_OK() 1                              /* the factory programs are in the module's flash */
#define audio_batch_active 0                    /* (Melodee's batched renders: not here) */
#define AUDIO_BATCH_BYTES 256u
#define AP_PROPHET 0u
#include "p5/p5_tables.h"

enum { P_VOICE, P_PRIO, P_ALLOC, P_DETUNE, P_RESERVED34, P_RESERVED36, P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7,
       P_COUNT };
enum { V_POLY, V_MONO, V_LEGATO, V_UNISON };   /* P_VOICE */
enum { F_INT, F_SEMI };

typedef struct {
    const char *label;
    uint8_t fmt;
    int16_t min, max, def;
    const char *const *names;
    const char *unit;
} param_desc_t;
typedef struct {
    uint8_t active, gate, note, mvel;           /* mvel: the note-on velocity */
    int32_t pitch16, pitch_cur;                 /* 1/16 semitone (no common glide: the Prophet glides itself) */
    int32_t fine;                               /* unison detune, 1/4096 of the increment */
    int32_t env_out;                            /* the last block's amplitude, Q15 */
    uint32_t ph[3], age;
} voice_t;
typedef struct { int32_t pitch16, amp0, amp1, cutoff, shape, fine; } vmod_t;
typedef struct track {
    int16_t p[P_COUNT];
    voice_t *v;
    uint8_t mw, at;                             /* CC 1, channel aftertouch */
    int16_t bend_raw;                           /* -8192..8191 */
    uint8_t mono_stack[16], nmono, mono_note;   /* UNISON: keys held, in press order */
} track_t;
typedef struct { uint8_t env[4]; } preset_t;
typedef struct {
    const char *name;
    const char *page_title[2];
    param_desc_t edit[8];
    void (*preset_make)(uint32_t k, preset_t *p, char *name);
    uint8_t npresets, knob[4], poly, ownenv;
    void (*note_on)(track_t *t, voice_t *v);
    void (*render)(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m);
    int (*done)(track_t *t, voice_t *v);
    void (*block)(track_t *t);
    uint32_t (*units)(const track_t *t);
    uint32_t (*cap)(const track_t *t);
    void (*legato)(track_t *t, voice_t *v);
    void (*post)(track_t *t, int32_t *out, uint32_t n, uint32_t nr);
} engine_t;

static voice_t voices[NVOICE];
static track_t trk[NPART] = {{.v = voices}};
static uint8_t voice_was;                       /* what the voice note_on starts did: 0 free, 1 released, 2 held */
static uint8_t midi_verb[NPART];                /* (Melodee's release override: none) */
static void audio_batch_push(void (*k)(const void *, int32_t *), const void *d, uint32_t sz, int32_t *out, uint32_t n,
                             uint32_t ap)
{ (void)k; (void)d; (void)sz; (void)out; (void)n; (void)ap; }

static void str_cpy(char *d, const char *s, unsigned max)
{
    unsigned i = 0;
    while (s[i] && i + 1 < max) { d[i] = s[i]; i++; }
    if (max) d[i] = 0;
}
/* the sample helpers inline: the engine's kernels run from .ram_text and must not call into the flash */
static inline __attribute__((always_inline)) int32_t clamp(int32_t v, int32_t lo, int32_t hi)
{ return v < lo ? lo : v > hi ? hi : v; }
static inline __attribute__((always_inline)) uint32_t noise32(int32_t *st)
{
    uint32_t s = (uint32_t)*st;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *st = (int32_t)s;
    return s;
}
/* TPDF dither for the cut to whole samples (dsp.c #100): a fading note's error becomes a steady floor */
#define DITH_N 32u
static float dith_f[DITH_N];
static uint32_t dith_r = 0x2545F491u;
static void dither_block(void)
{
    uint32_t r = dith_r, k;
    for (k = 0; k < DITH_N; k++) {              /* two uniforms from one LCG step: u1 - u2 is triangular */
        r = r * 1664525u + 1013904223u;
        dith_f[k] = (float)((int32_t)(r >> 17) - (int32_t)((r >> 2) & 32767u)) * (1.0f / 32768.0f);
    }
    dith_r = r;
}
static inline __attribute__((always_inline)) int32_t dith_cutf(float x, uint32_t k)
{
    return x != 0.0f ? (int32_t)(x + dith_f[k & (DITH_N - 1u)] + 12582912.0f) - 12582912 : 0;
}
/* ---- the second core: Melodee's audio worker on the host's c1_run / c1_wait (as FM-106's), from the render only.
 * The engine hands core 1 every other sounding voice and renders the next itself (p5_render, p5_join). Its voice cap
 * reads audio_worker_online: eight only once core 1 has taken a job, so a host whose core 1 is still being screened,
 * failed its checks or timed out never lets eight voices fall on core 0 (more than one core's block) */
static const hb_host_t *H;
static uint8_t c1_lent, c1_dead;                /* the host has a second core; it timed out once (held from then on) */
static uint8_t audio_worker_online;             /* core 1 took the last job offered */
static void (*aw_fn)(void *);
static void *aw_ctx;
static void aw_job(uint32_t arg) { (void)arg; aw_fn(aw_ctx); }   /* on core 1 */
static void c1_nop(uint32_t arg) { (void)arg; }
static void c1_probe(void)                      /* each block until core 1 is online: an empty job */
{
    if (audio_worker_online || !c1_lent || c1_dead || !H->c1_run(c1_nop, 0))
        return;
    audio_worker_online = H->c1_wait() == 0;
    c1_dead = !audio_worker_online;
}
static int audio_worker_submit(void (*fn)(void *), void *ctx)
{
    aw_fn = fn;
    aw_ctx = ctx;
    if (H->c1_run(aw_job, 0))
        return 1;
    audio_worker_online = 0;                    /* none lent: five voices for new notes until it takes a job again */
    return 0;
}
static int audio_worker_join(void)
{
    if (H->c1_wait() != 0) {                    /* core 1 did not finish (held from now on): this voice on core 0, from
                                                 * where core 1 left it, this once */
        c1_dead = 1;
        audio_worker_online = 0;
        aw_job(0);
    }
    return 1;
}

/* the increment of pitch16 plus ct cents and fine (1/4096): 1 ct = 2.367 / 4096 */
static inline uint32_t cents_inc(int32_t pitch16, int32_t ct, int32_t fine)
{
    int32_t d16 = ct * 16 / 100, rem = ct * 16 - d16 * 100;
    uint32_t inc = pitch_inc((uint32_t)clamp(pitch16 + d16, 0, 2047));
    return inc + (uint32_t)((int32_t)(inc >> 12) * (rem * 2367 / 16000 + fine));
}

#include "p5/eng_prophet.c"

static p5_part_t p5_parts[NPART];
static p5_part_t *p5_part(uint32_t part) { return &p5_parts[part % NPART]; }
static const engine_t *const E = &ENG_P5_TEST;

/* the bend (voice.c): the patch's range, smoothed over ~6 ms to an exact landing; Q8 semitones */
static int32_t bend_q8, bend_target;
static void p5_update_bend(track_t *t)
{
    int32_t range = (clamp(p5_patch_of(t)->raw[P5_BEND], 0, 11) + 1) * 256;
    bend_target = t->bend_raw * range / (t->bend_raw < 0 ? 8192 : 8191);
}

/* ---- the voices (voice.c, for POLY and UNISON) */
static uint32_t vage;
static void voice_start(track_t *t, voice_t *v, uint32_t note, uint32_t vel)
{
    voice_was = (uint8_t)(!v->active ? 0u : v->gate ? 2u : 1u);
    if (!v->active)
        v->env_out = 0;                         /* sounding: the attack starts from where it is */
    v->note = (uint8_t)note;
    v->gate = v->active = 1;
    v->age = ++vage;
    v->pitch16 = v->pitch_cur = (int32_t)note * 16;
    v->mvel = (uint8_t)vel;
    E->note_on(t, v);
}
static voice_t *voice_alloc(track_t *t, uint32_t note)
{
    uint32_t np = E->cap(t), i, best = np;
    int32_t bd = 0x7FFFFFFF;
    for (i = 0; i < np; i++)                    /* the same key again: its voice */
        if (t->v[i].active && t->v[i].note == note)
            return &t->v[i];
    for (i = 0; i < np; i++) {                  /* a free voice, the one last nearest this note (P_ALLOC, as the original) */
        int32_t d = t->v[i].pitch_cur - (int32_t)note * 16;
        if (t->v[i].active)
            continue;
        d = d < 0 ? -d : d;
        if (!t->v[i].pitch_cur)
            d = 0x7FFFFFF0;                     /* never played: last choice */
        if (d < bd) { bd = d; best = i; }
    }
    if (best < np)
        return &t->v[best];
    for (i = 0; i < np; i++)                    /* the oldest released, else the oldest held */
        if (!t->v[i].gate && (best == np || t->v[i].age < t->v[best].age))
            best = i;
    if (best == np)
        for (i = 0, best = 0; i < np; i++)
            if (t->v[i].age < t->v[best].age)
                best = i;
    return &t->v[best];
}
/* UNISON: one note on the cap's voices, spread by DETUNE (the outer ones as wide for any count) */
static void mono_play(track_t *t, uint32_t note, uint32_t vel, int retrig)
{
    uint32_t nv = E->cap(t), i;
    for (i = 0; i < nv; i++) {
        voice_t *v = &t->v[i];
        int32_t k = 2 * (int32_t)i - (int32_t)(nv - 1u);
        v->fine = nv > 1u ? k * 7 / (int32_t)(nv - 1u) * t->p[P_DETUNE] * 56 / 889 : 0;   /* up to ~±40 cents */
        if (retrig || !v->active || !v->gate) {
            if (!v->active && i)
                v->ph[0] = vage * 2654435761u + i * 0x9E3779B9u;   /* random start phases: in phase they stack */
            voice_start(t, v, note, vel);
        } else {                                /* between held keys: the new pitch, the envelopes go on */
            v->note = (uint8_t)note;
            v->pitch16 = v->pitch_cur = (int32_t)note * 16;
            v->mvel = (uint8_t)vel;
            E->legato(t, v);
        }
    }
    t->mono_note = (uint8_t)note;
}
static uint32_t mono_pick(const track_t *t)    /* PRIO: 0 the last key, 1 the lowest, 2 the highest; 0 = none */
{
    uint32_t i, n;
    if (!t->nmono)
        return 0;
    n = t->mono_stack[t->nmono - 1u];
    for (i = 0; i < t->nmono; i++) {
        if (t->p[P_PRIO] == 1 && t->mono_stack[i] < n) n = t->mono_stack[i];
        if (t->p[P_PRIO] == 2 && t->mono_stack[i] > n) n = t->mono_stack[i];
    }
    return n;
}
static void mono_remove(track_t *t, uint32_t note)
{
    uint32_t j, k = 0;
    for (j = 0; j < t->nmono; j++)
        if (t->mono_stack[j] != note)
            t->mono_stack[k++] = t->mono_stack[j];
    t->nmono = (uint8_t)k;
}
static void p5_note_off_all(track_t *t)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        t->v[i].gate = 0;
    t->nmono = t->mono_note = 0;
}
static void p5_key(track_t *t, uint32_t note, uint32_t vel)   /* vel 0: let go */
{
    uint32_t i;
    if (note < 1u || note > 127u)               /* (0: "none" on the mono stack) */
        return;
    if (vel) {
        if (t->p[P_VOICE] != V_UNISON) {
            voice_t *v = voice_alloc(t, note);
            v->fine = 0;
            voice_start(t, v, note, vel);
            return;
        }
        uint32_t sounding = t->mono_note && t->v[0].active && t->v[0].gate, want;
        mono_remove(t, note);
        if (t->nmono >= sizeof t->mono_stack)
            mono_remove(t, t->mono_stack[0]);
        t->mono_stack[t->nmono++] = (uint8_t)note;
        want = mono_pick(t);
        if (!(sounding && want == t->mono_note))   /* (LOW priority: a higher key does not win) */
            mono_play(t, want, vel, !sounding);
        return;
    }
    mono_remove(t, note);
    if (t->p[P_VOICE] == V_UNISON && t->mono_note == note) {
        uint32_t next = mono_pick(t);
        if (next)
            mono_play(t, next, t->v[0].mvel, 0);   /* back to a key still held, legato */
        else
            p5_note_off_all(t);
    }
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].gate && t->v[i].note == note)
            t->v[i].gate = 0;
}

/* one block of CTL samples of the part (mono, Q15-ish at VOICE_FS a voice) into buf, cleared here (voice.c
 * track_render_audio with ownenv: the engine's envelopes are the amplitude, done() ends a voice, whose last
 * block then fades to 0) */
static void p5_block_render(track_t *t, int32_t *buf)
{
    uint32_t i, nr = 0, live = 0;
    int32_t d, bend16, bend_fine;
    for (i = 0; i < CTL; i++)
        buf[i] = 0;
    c1_probe();
    dither_block();
    d = bend_target - bend_q8;
    bend_q8 += d > 0 ? (d < 4 ? d : (d + 3) / 4) : (d > -4 ? d : (d - 3) / 4);
    E->block(t);
    for (i = 0; i < NVOICE; i++)
        live |= t->v[i].active;
    if (!live) {
        E->post(t, buf, CTL, 0);
        return;
    }
    bend16 = bend_q8 >= 0 ? bend_q8 / 16 : -((-bend_q8 + 15) / 16);
    bend_fine = (bend_q8 - bend16 * 16) * 2367 / 2560;
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &t->v[i];
        vmod_t m;
        if (!v->active)
            continue;
        if (E->done(t, v)) {
            v->active = v->gate = 0;
            m.amp1 = 0;
        } else {
            m.amp1 = 32767;
        }
        m.amp0 = v->env_out;
        v->env_out = m.amp1;
        m.pitch16 = clamp(v->pitch_cur + bend16, 0, 2047);
        m.fine = v->fine + bend_fine;
        m.cutoff = 0;
        m.shape = 64 << 8;                      /* (no common modulation: the patch's own widths) */
        E->render(t, v, buf, CTL, &m);
        nr++;
    }
    E->post(t, buf, CTL, nr);
}
