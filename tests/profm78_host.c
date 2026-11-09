/* SPDX-License-Identifier: GPL-3.0-only */
/* PRO-FM-78 (profm78) on this computer, through its own render(), note(), ui() and draw(): INIT and every one
 * of Sequential's 200 programs sound a held five-note chord, within full scale; POLY plays eight voices with a second
 * core (every other one on it) and five without, and steals the oldest; a core 1 that times out leaves the sound on
 * core 0; a UNISON program stacks its voices on the lowest key held; ALGORITHM steps the types and PRESETS a type's
 * programs; HOME first, the section buttons step their pages and back to HOME; the knobs edit the program and show
 * their units; the chord generator plays a chord a key, inverted, voiced and strummed; the arpeggiator steps the notes
 * held on the clock (TEMPO's or a MIDI clock's), latches them; the loop recorder records keys at the 1/16 and the arp's
 * notes as they fell, plays them back transposed, stops, clears, saves and loads with SAVE. Out come build/host/profm78.wav (the 40 programs of
 * 1978, a chord each) and build/host/profm78-*.png (the screen, drawn by the host's own canvas and fonts).
 *   tests/run_host.sh   (fm1-dock beside this repository, or FM1_DOCK) */
#define HB_DESC static const hb_module_t hb_module    /* (no .hb_desc section on a computer) */
#include <stdint.h>
static void trace(uint32_t note, uint32_t vel);
#define VOICE_TRACE(n, v) trace(n, v)
#include "../profm78/profm78_module.c"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HALF 256u                               /* the host's block */

/* ---- the screen, as dock.c draws it (gfx.c: the canvas, the fonts), into a framebuffer */
static uint16_t fb[240 * 240];
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = y; j < y + h && j < 240u; j++)
        for (i = x; i < x + w && i < 240u; i++)
            fb[j * 240u + i] = c;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            if (x + i < 240u && y + j < 240u) {
                uint16_t c = px[j * w + i];
                fb[(y + j) * 240u + x + i] = (uint16_t)(c >> 8 | c << 8);
            }
}
#undef C_DIM
#define CV_MAX (240u * 96u)
#define section(x) aligned(2)                   /* (gfx.c's .pool: no such section on a Mac) */
#include "gfx.c"
#undef section
static void h_fill(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) { lcd_fill((uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, c); }
static void h_text(int32_t x, int32_t y, int32_t w, uint32_t big, const char *s, uint16_t fg, uint32_t center)
{ draw_text_box((uint32_t)x, (uint32_t)y, (uint32_t)w, big ? &FONT_L : &FONT_S, s, fg, center ? 1 : 0); }
static void h_cv_begin(int32_t w, int32_t h, uint16_t bg)
{
    if ((uint32_t)(w * h) > CV_MAX) h = (int32_t)(CV_MAX / (uint32_t)w);
    cv_begin((uint32_t)w, (uint32_t)h, bg);
}
static void h_cv_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c) { cv_rect(x, y, w, h, c); }
static void h_cv_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c) { cv_line(x0, y0, x1, y1, c); }
static void h_cv_pixel(int32_t x, int32_t y, uint16_t c) { cv_pset(x, y, c); }
static void h_cv_text(int32_t x, int32_t y, uint32_t big, const char *s, uint16_t fg) { cv_text(x, y, big ? &FONT_L : &FONT_S, s, fg); }
static void h_cv_blit(int32_t x, int32_t y) { cv_blit((uint32_t)x, (uint32_t)y); }
static int32_t h_text_w(uint32_t big, const char *s) { return text_w(big ? &FONT_L : &FONT_S, s); }
static uint32_t now_ms;
static uint32_t h_ms(void) { return now_ms; }
static uint8_t led_on[41];                 /* the buttons, then the keys (HB_LED_KEY) */
static void h_led(uint32_t id, uint32_t on) { if (id < 41u) led_on[id] = (uint8_t)on; }
static void h_log(const char *s) { (void)s; }
static void shot(const char *base, const char *name)   /* the screen (all of it the module's: HB_F_SCREEN) */
{
    char path[256];
    FILE *f;
    uint32_t i;
    snprintf(path, sizeof path, "%s-%s.ppm", base, name);
    if (!(f = fopen(path, "wb"))) return;
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = fb[i];
        fputc((c >> 11) * 255 / 31, f);
        fputc((c >> 5 & 63) * 255 / 63, f);
        fputc((c & 31) * 255 / 31, f);
    }
    fclose(f);
}

/* ---- a second core that runs its job when the render waits for it (on: takes jobs; late: the next wait times out) */
static int c1_on = 1, c1_late, c1_jobs;
static void (*c1_fn)(uint32_t);
static int h_c1_run(void (*fn)(uint32_t), uint32_t arg) { (void)arg; if (!c1_on) return 0; c1_fn = fn; return 1; }
static int h_c1_wait(void)
{
    if (c1_late) { c1_late = 0; c1_on = 0; return -1; }   /* (held from then on, as the host's) */
    c1_fn(0);
    c1_jobs++;
    return 0;
}

/* ---- every note the voices get, with its sample */
static struct { uint32_t s, note, vel; } tr[8192];
static uint32_t n_tr;
static void trace(uint32_t note, uint32_t vel)
{
    if (n_tr < 8192u) { tr[n_tr].s = now_s; tr[n_tr].note = note; tr[n_tr++].vel = vel; }
}
static uint32_t ons(uint32_t from, uint32_t to, uint32_t note, uint32_t *first)   /* note-ons of note (0: any) */
{
    uint32_t i, n = 0;
    for (i = 0; i < n_tr; i++)
        if (tr[i].vel && tr[i].s >= from && tr[i].s < to && (!note || tr[i].note == note)) {
            if (!n && first) *first = tr[i].s;
            n++;
        }
    return n;
}

/* ---- PRO-FM-78's flash bank */
static uint8_t bank_mem[HB_BANK_MAX];
static uint32_t bank_n;
static int h_bank_save(const void *p, uint32_t n) { if (n > HB_BANK_MAX) return -1; memcpy(bank_mem, p, n); bank_n = n; return 0; }
static int h_bank_load(void *p, uint32_t n) { if (!bank_n || n != bank_n) return -1; memcpy(p, bank_mem, n); return 0; }

static int failed;
#define CHECK(ok, ...) do { if (!(ok)) { failed = 1; printf("FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static FILE *wav;
static uint32_t wav_n;
static int32_t peak, clips;
static void play(uint32_t ms)                  /* ms of sound, its peak and full-scale samples counted */
{
    int32_t out[2 * HALF];
    uint32_t b, i;
    for (b = 0; b < ms * 44100u / 1000u / HALF; b++) {
        for (i = 0; i < 2u * HALF; i++)
            out[i] = 0;
        hb_module.render(out, HALF);
        now_ms += 6u;
        for (i = 0; i < HALF; i++) {
            int32_t s = out[2u * i], a = s < 0 ? -s : s;
            int16_t w = (int16_t)s;
            if (a > peak) peak = a;
            clips += a >= 32767;
            if (wav) { fwrite(&w, 2, 1, wav); wav_n++; }
        }
    }
}
static void select_preset(uint32_t k)
{
    pick(k);
    play(6);
    CHECK(cur == k, "preset %u selected (%u)", k, cur);
}
static uint32_t active(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++) n += voices[i].active;
    return n;
}
static void turn(uint32_t enc, int32_t clicks)  /* an encoder, slowly (a detent a step), as ui() hears it */
{
    hb_module.ui(HB_EV_ENC | enc, clicks);
    now_ms += 100u;
    play(6);
    hb_module.draw(0);
}
static void press(uint32_t btn) { hb_module.ui(HB_EV_BTN_DOWN | btn, 0); hb_module.draw(0); }
static const char *shown(uint32_t k)            /* knob k's value as the screen says it */
{
    static char s[16];
    ctl_text(s, &PAGES[page].c[k], ctl_get(&PAGES[page].c[k]));
    return s;
}
static const uint8_t CHORD[5] = {48, 55, 60, 64, 67};

static void rec_tap(void)                       /* REC pressed and let go */
{
    hb_module.ui(HB_EV_BTN_DOWN | HB_BTN_REC, 0); now_ms += 100u;
    hb_module.ui(HB_EV_BTN_UP | HB_BTN_REC, 0); play(6); hb_module.draw(0);
}
static void clocked(uint32_t ms, uint32_t bpm)  /* ms of sound under a DAW's MIDI clock (its ticks before each block) */
{
    static double due;
    double tick = 44100.0 * 60.0 / 24.0 / bpm;
    uint32_t b;
    if (due < now_s) due = now_s;
    for (b = 0; b < ms * 44100u / 1000u / HALF; b++) {
        while (due <= now_s + HALF) { hb_module.midi(0xF8u, 0, 0); due += tick; }
        play(6);
    }
    if (due < now_s) due = now_s;
}
static void arp_and_loop(const char *base)
{
    uint32_t i, n, t0, first = 0, ok, lit_seen = 0;
    static const uint32_t UP[6] = {60, 64, 67, 60, 64, 67};

    /* the arpeggiator: UP at 1/16, 120 BPM: C E G from the lowest, 5512 samples apart */
    select_preset(FIRST_1978);
    press(HB_BTN_ARP);
    CHECK(!strcmp(PAGES[page].title, "ARPEGGIATOR") && led_on[HB_BTN_ARP] == 1u, "ARP: the arpeggiator, its light");
    turn(HB_ENC_KNOB1, 1);
    CHECK(!strcmp(shown(0), "UP") && !strcmp(shown(1), "1/16") && !strcmp(shown(3), "50%"), "UP 1/16 50%%: %s %s %s",
          shown(0), shown(1), shown(3));
    n_tr = 0; t0 = now_s;
    hb_module.note(0, 67, 100); hb_module.note(0, 60, 100); hb_module.note(0, 64, 100);
    play(700);
    for (n = 0, ok = 1, i = 0; i < n_tr && n < 6u; i++)
        if (tr[i].vel) {
            if (tr[i].note != UP[n]) ok = 0;
            if (n && (tr[i].s - first > 5512u + 40u || tr[i].s - first + 40u < 5512u)) ok = 0;
            first = tr[i].s; n++;
        }
    CHECK(n == 6u && ok && ons(t0, t0 + 300u, 0, 0) == 1u, "C E G held: 60 64 67 60 64 67, a 1/16 apart, the first at once");
    for (i = 0; i < 3u; i++) hb_module.note(0, CHORD[i + 2u], 0);
    play(100); t0 = now_s; play(500);
    CHECK(ons(t0, now_s, 0, 0) == 0u, "let go: the arp stops");
    turn(HB_ENC_KNOB3, 1);                      /* two octaves */
    n_tr = 0; t0 = now_s;
    hb_module.note(0, 60, 100);
    for (i = 0; i < 100u; i++) { play(6); hb_module.draw(0); lit_seen |= led_on[HB_LED_KEY(72 - 53)] == 1u; }
    CHECK(ons(t0, now_s, 60, 0) >= 2u && ons(t0, now_s, 72, 0) >= 2u && lit_seen, "OCTAVES 2: 60 72 .., key 72's light follows");
    hb_module.note(0, 60, 0); play(100);
    turn(HB_ENC_KNOB3, -1);

    press(HB_BTN_ARP);                          /* ARP OPTIONS: LATCH */
    CHECK(!strcmp(PAGES[page].title, "ARP OPTIONS") && !strcmp(shown(0), "120 BPM"), "ARP again: options, 120 BPM (%s)", shown(0));
    turn(HB_ENC_KNOB3, 1);
    hb_module.note(0, 60, 100); play(50); hb_module.note(0, 60, 0);
    t0 = now_s; play(600);
    CHECK(ons(t0, now_s, 60, 0) >= 4u, "LATCH: let go, 60 plays on");
    hb_module.note(0, 62, 100); play(50); hb_module.note(0, 62, 0);
    t0 = now_s; play(600);
    CHECK(ons(t0, now_s, 62, 0) >= 4u && ons(t0, now_s, 60, 0) == 0u, "a new key after all let go: 62 alone");
    turn(HB_ENC_KNOB3, -1);
    play(100); t0 = now_s; play(400);
    CHECK(ons(t0, now_s, 0, 0) == 0u, "LATCH off: it stops");

    /* a DAW's MIDI clock: 150 BPM, a 1/16 4410 samples, TEMPO shows it; gone, TEMPO's own again */
    hb_module.midi(0xFAu, 0, 0);
    hb_module.note(0, 60, 100);
    clocked(1500, 150);
    n_tr = 0; t0 = now_s;
    clocked(1000, 150);
    n = ons(t0, now_s, 60, &first);
    for (ok = 0, i = 0; i < n_tr; i++) if (tr[i].vel) ok = tr[i].s;
    CHECK(n >= 9u && n <= 11u && (ok - first) / (n - 1u) > 4410u - 60u && (ok - first) / (n - 1u) < 4410u + 60u,
          "MIDI clock 150: a 1/16 %u samples (4410), %u in a second", n > 1u ? (ok - first) / (n - 1u) : 0u, n);
    hb_module.draw(0);
    CHECK(!strcmp(shown(0), "EXT 150"), "TEMPO: EXT 150 (%s)", shown(0));
    hb_module.midi(0xFCu, 0, 0);
    play(700); hb_module.draw(0);
    CHECK(!strcmp(shown(0), "120 BPM"), "the clock gone: 120 BPM again (%s)", shown(0));
    hb_module.note(0, 60, 0);
    press(HB_BTN_ARP);
    CHECK(page == 0u, "ARP after its last page: HOME");
    gen[G_AMODE] = A_OFF;
    play(500);

    /* the loop recorder: 1 bar (2 s); armed, the first key starts it; keys at the 1/16, played from the next pass */
    press(HB_BTN_SEQ);
    CHECK(!strcmp(PAGES[page].title, "LOOP RECORDER") && led_on[HB_BTN_SEQ] == 1u, "SEQ: the loop recorder");
    turn(HB_ENC_KNOB2, -1);
    CHECK(!strcmp(shown(1), "1 BAR") && !strcmp(shown(2), "REC"), "1 BAR, CLICK REC (%s %s)", shown(1), shown(2));
    rec_tap();
    CHECK(rec == R_ARMED && !playing && !strcmp(d_top, "ARMED"), "REC: armed (%s)", d_top);
    n_tr = 0; t0 = now_s;
    hb_module.note(0, 60, 100); play(250); hb_module.note(0, 60, 0);
    while (now_s - t0 < 44100u - 300u) play(6);  /* ~ beat 3, a little early */
    hb_module.note(0, 64, 100); play(500); hb_module.note(0, 64, 0);
    hb_module.draw(0);
    CHECK(playing && rec == R_ON && !strncmp(d_top, "REC 1.", 6) && led_on[HB_BTN_REC] == 1u, "recording (%s)", d_top);
    CHECK(ons(t0, t0 + 88200u - 600u, 64, 0) == 1u && ons(t0, t0 + 88200u - 600u, 60, 0) == 1u, "pass 1: each key once");
    rec_tap();
    CHECK(rec == R_OFF && playing, "REC again: recording stops, the loop plays on");
    while (now_s - t0 < 2u * 88200u) play(6);
    CHECK(ons(t0 + 88200u - 100u, t0 + 88200u + 100u, 60, 0) == 1u, "pass 2: 60 on the downbeat");
    CHECK(ons(t0 + 88200u + 44100u - 100u, t0 + 88200u + 44100u + 100u, 64, 0) == 1u, "pass 2: 64 on beat 3 (quantised)");
    turn(HB_ENC_KNOB4, 2);
    CHECK(!strcmp(shown(3), "+2 st"), "TRANSP +2 st (%s)", shown(3));
    while (now_s - t0 < 3u * 88200u) play(6);
    CHECK(ons(t0 + 2u * 88200u - 100u, now_s, 62, 0) == 1u && ons(t0 + 2u * 88200u - 100u, now_s, 66, 0) == 1u,
          "pass 3: transposed, 62 and 66");
    turn(HB_ENC_KNOB4, -2);
    if (base) { hb_module.draw(0); shot(base, "loop"); }
    press(HB_BTN_PLAY);
    play(50); hb_module.draw(0);
    for (n = 0, i = 0; i < NVOICE; i++) n += voices[i].gate;
    CHECK(!playing && n == 0u && led_on[HB_BTN_PLAY] == 2u, "PLAY: stopped, its notes let go");

    /* SAVE: refused while a tail rings (it would stall it); quiet, the loop in the bank, back at the next start */
    hb_module.ui(HB_EV_BTN_DOWN | HB_BTN_SAVE, 0); hb_module.draw(0);
    CHECK(active() && bank_n == 0u && !strcmp(toast, "LET NOTES END"), "SAVE with a tail ringing: refused");
    for (i = 0; i < 2000u && active(); i++) play(6);
    hb_module.ui(HB_EV_BTN_DOWN | HB_BTN_SAVE, 0); hb_module.draw(0);
    CHECK(bank_n == sizeof sv && !strcmp(toast, "LOOP SAVED"), "SAVE: %u bytes in the bank", bank_n);
    /* REC held: a bar fills on the screen, at 1 s the loop is cleared while still held; its release does nothing */
    hb_module.ui(HB_EV_BTN_DOWN | HB_BTN_REC, 0); now_ms += 600u; hb_module.draw(0);
    CHECK(hold > 1u && hold < 16u && seq_has(), "REC held 0.6 s: HOLD TO CLEAR, the bar %u/16, the loop kept", hold);
    if (base) shot(base, "hold");
    now_ms += 500u; hb_module.draw(0); play(6);
    CHECK(!hold && !seq_has() && !strcmp(toast, "LOOP CLEARED"), "REC held 1.1 s: cleared, still held");
    hb_module.ui(HB_EV_BTN_UP | HB_BTN_REC, 0); play(6);
    CHECK(rec == R_OFF, "its release: not armed");
    loop_load();
    for (n = 0, i = 0; i < NEV; i++) n += ev[i].fl != 0;
    CHECK(n == 2u, "loaded: its two notes (%u)", n);

    /* a DAW's start plays the loop, its stop stops it; CLICK ON clicks with nothing playing */
    hb_module.midi(0xFAu, 0, 0); play(12);
    CHECK(playing, "a DAW's start: the loop plays");
    hb_module.midi(0xFCu, 0, 0); play(12);
    CHECK(!playing, "its stop: stopped");
    hb_module.ui(HB_EV_BTN_DOWN | HB_BTN_REC, 0); now_ms += 1200u; hb_module.ui(HB_EV_BTN_UP | HB_BTN_REC, 0);
    play(3000);
    turn(HB_ENC_KNOB3, 1);
    press(HB_BTN_PLAY); peak = 0; play(100);
    CHECK(playing && peak > 1000, "CLICK ON, an empty loop: the click (peak %d)", peak);
    press(HB_BTN_PLAY); turn(HB_ENC_KNOB3, -1);

    /* the arp's notes recorded as they fall: played back with the key let go */
    gen[G_AMODE] = A_UP;
    rec_tap();
    n_tr = 0; t0 = now_s;
    hb_module.note(0, 60, 100);
    while (now_s - t0 < 88200u - 2000u) play(6);
    hb_module.note(0, 60, 0);
    rec_tap();
    while (now_s - t0 < 2u * 88200u) play(6);
    CHECK(ons(t0 + 88200u, t0 + 2u * 88200u, 60, 0) >= 15u, "the arp's 16 notes play back, the key let go (%u)",
          ons(t0 + 88200u, t0 + 2u * 88200u, 60, 0));
    press(HB_BTN_PLAY);
    gen[G_AMODE] = A_OFF;
    hb_module.ui(HB_EV_BTN_DOWN | HB_BTN_REC, 0); now_ms += 1200u; hb_module.ui(HB_EV_BTN_UP | HB_BTN_REC, 0);
    play(30000);

    /* SWING 75 % at 1/32: every 1/8's notes shifted in proportion (0, 4.5, 9, 10.5 ticks), still in order */
    gen[G_AMODE] = A_UP; gen[G_ARATE] = 5; gen[G_SWING] = 25;
    n_tr = 0; t0 = now_s;
    hb_module.note(0, 60, 100);
    play(2000);
    hb_module.note(0, 60, 0);
    {
        uint32_t last = 0, gap = ~0u, m = 0;
        for (i = 0; i < n_tr; i++)
            if (tr[i].vel) { if (m++ && tr[i].s - last < gap) gap = tr[i].s - last; last = tr[i].s; }
        CHECK(m >= 30u && gap > 1300u && gap < 1460u, "SWING 75%%, 1/32: %u notes, never closer than 1.5 ticks (%u)", m, gap);
    }
    gen[G_ARATE] = 3; gen[G_SWING] = 0;
    play(1000);

    /* a key held as the arp comes on while recording: its release still ends its note in the loop */
    gen[G_AMODE] = A_OFF;
    rec_tap();
    hb_module.note(0, 60, 100); play(300);
    gen[G_AMODE] = A_UP; play(100);
    hb_module.note(0, 60, 0); play(100);
    for (n = 0, i = 0; i < NEV; i++) n += (ev[i].fl & EV_USED) && ev[i].note == 60u && !(ev[i].fl & EV_OPEN);
    CHECK(n == 1u, "a key held as the arp came on: its release recorded");
    gen[G_AMODE] = A_OFF;
    rec_tap(); press(HB_BTN_PLAY);
    hb_module.ui(HB_EV_BTN_DOWN | HB_BTN_REC, 0); now_ms += 1200u; hb_module.ui(HB_EV_BTN_UP | HB_BTN_REC, 0);
    play(30000);
    page = 0;
}

int main(int argc, char **argv)
{
    static const hb_host_t host = {.abi = HB_ABI, .size = sizeof(hb_host_t), .ms = h_ms, .log = h_log, .fill = h_fill,
                                   .text = h_text, .cv_begin = h_cv_begin, .cv_rect = h_cv_rect, .cv_line = h_cv_line,
                                   .cv_pixel = h_cv_pixel, .cv_text = h_cv_text, .cv_blit = h_cv_blit, .led = h_led,
                                   .text_w = h_text_w, .c1_run = h_c1_run, .c1_wait = h_c1_wait,
                                   .bank_load = h_bank_load, .bank_save = h_bank_save};
    const char *base = argc > 1 ? argv[1] : 0;
    char path[256], name[24];
    uint32_t k, i, quiet = 0, worst = 0, n;
    int32_t worst_peak = 0;
    preset_t pr;
    hb_module.init(&host);
    hb_module.draw(1);
    CHECK(cur == FIRST_1978 && want == FIRST_1978 && !strcmp(type_name(type), "1978"), "starts on 511, type 1978");
    CHECK(!strcmp(PAGES[page].title, "HOME") && led_on[HB_BTN_EDIT] == 2u && led_on[HB_BTN_FX] == 2u,
          "HOME first, every section glows");
    CHECK(PAGES[0].c[0].idx == P5_CUTOFF && PAGES[0].c[1].idx == P5_RESONANCE && PAGES[0].c[2].idx == P5_LFO_INITIAL
          && PAGES[0].c[3].idx == P5_RELEASE_AMP, "HOME: cutoff, resonance, LFO, release");
    if (base) shot(base, "home");

    /* the panel is the module's: its keys (F3 = key 0) through the octave, a short HOME its home, a long one not */
    CHECK(hb_module.flags == (HB_F_SCREEN | HB_F_PANEL), "the screen and the panel are PRO-FM-78's");
    hb_module.ui(HB_EV_BTN_DOWN | HB_BTN_OCT_UP, 0); hb_module.draw(0);
    hb_module.ui(HB_EV_KEY_DOWN | 7u, 0); play(12);          /* C4 + an octave */
    for (n = 0, i = 0; i < NVOICE; i++) n += voices[i].gate && voices[i].note == 72u && voices[i].mvel == 100u;
    CHECK(n == 1u && led_on[HB_LED_KEY(7)] == 1u, "OCT+, key 7: C5 at velocity 100, its light on");
    hb_module.ui(HB_EV_BTN_DOWN | HB_BTN_OCT_DOWN, 0);
    hb_module.ui(HB_EV_KEY_UP | 7u, 0); play(12);            /* its release: the note it started */
    for (n = 0, i = 0; i < NVOICE; i++) n += voices[i].gate;
    CHECK(n == 0u && led_on[HB_LED_KEY(7)] == 0u, "key 7 let go after OCT-: C5 released");
    hb_module.ui(HB_EV_KEY_DOWN | 0u, 90); play(12);
    for (n = 0, i = 0; i < NVOICE; i++) n += voices[i].gate && voices[i].note == 53u && voices[i].mvel == 90u;
    CHECK(n == 1u, "key 0 sensed at 90: F3 at 90");
    hb_module.ui(HB_EV_KEY_UP | 0u, 0); play(12);
    press(HB_BTN_LFO);
    hb_module.ui(HB_EV_BTN_DOWN | HB_BTN_HOME, 0); now_ms += 200u; hb_module.ui(HB_EV_BTN_UP | HB_BTN_HOME, 0);
    CHECK(page == 0u, "a short HOME: PRO-FM-78's home");
    press(HB_BTN_LFO);
    hb_module.ui(HB_EV_BTN_DOWN | HB_BTN_HOME, 0); now_ms += 1500u; hb_module.ui(HB_EV_BTN_UP | HB_BTN_HOME, 0);
    CHECK(!strcmp(PAGES[page].title, "LFO"), "HOME held 1.5 s (the host's list): the page stays");
    page = 0;

    /* the types: every program in exactly one of BASS .. FX; 1978 the 40 of Group 5; INIT last */
    for (n = 0, k = 1; k <= T_N; k++) n += t_count[k];
    CHECK(n == 200u && t_count[0] == 40u && t_count[TN - 1u] == 1u, "200 programs in the types (%u), 40 in 1978", n);
    turn(HB_ENC_ALGORITHM, 1);
    CHECK(!strcmp(type_name(type), "BASS") && TYPE_OF[want - 1u] == T_BASS && cur == want, "ALGORITHM: BASS, a bass");
    turn(HB_ENC_PRESETS, 3);
    CHECK(t_pos[type] == 3u && TYPE_OF[want - 1u] == T_BASS && cur == want, "PRESETS: the fourth bass");
    turn(HB_ENC_PRESETS, -5);
    CHECK(t_pos[type] == t_count[type] - 2u, "PRESETS wraps within the type");
    if (base) shot(base, "bass");
    turn(HB_ENC_ALGORITHM, -2);
    CHECK(want == 0u && !strcmp(type_name(type), "INIT"), "ALGORITHM wraps back: INIT");
    turn(HB_ENC_ALGORITHM, 1);
    CHECK(want == FIRST_1978, "1978 kept its place: 511");

    /* the controls: in their units, edits are the program's bytes */
    press(HB_BTN_EDIT);
    CHECK(!strcmp(PAGES[page].title, "FILTER") && led_on[HB_BTN_EDIT] == 1u, "EDIT: the filter");
    raw()[P5_CUTOFF] = 0; raw()[P5_FILTER_REV] = 1;
    CHECK(!strcmp(shown(0), "41.0 Hz"), "cutoff 0 (Curtis) shows 41.0 Hz: %s", shown(0));
    raw()[P5_CUTOFF] = 127;
    CHECK(!strncmp(shown(0), "13.", 3) && strstr(shown(0), "kHz"), "cutoff 127 shows ~13 kHz: %s", shown(0));
    press(HB_BTN_ENV);
    CHECK(!strcmp(PAGES[page].title, "FILTER ENVELOPE") && led_on[HB_BTN_ENV] == 1u, "ENV: the filter envelope");
    press(HB_BTN_ENV);
    CHECK(!strcmp(PAGES[page].title, "AMPLIFIER ENVELOPE"), "ENV again: the amplifier's");
    raw()[P5_RELEASE_AMP] = 0;
    CHECK(!strcmp(shown(3), "1.00 ms"), "release 0: 1.00 ms (%s)", shown(3));
    raw()[P5_RELEASE_AMP] = 127;
    CHECK(!strcmp(shown(3), "10.0 s"), "release 127: 10.0 s (%s)", shown(3));
    turn(HB_ENC_KNOB4, -3);
    CHECK(raw()[P5_RELEASE_AMP] == 124u && edited, "KNOB 4 turns the release down three: %u", raw()[P5_RELEASE_AMP]);
    for (i = 0; i < 40u; i++) { hb_module.ui(HB_EV_ENC | HB_ENC_KNOB4, -1); now_ms += 10u; }
    CHECK(raw()[P5_RELEASE_AMP] < 124u - 40u, "turned fast: four a detent (%u)", raw()[P5_RELEASE_AMP]);
    raw()[P5_RELEASE_AMP] = 80;
    hb_module.draw(0);
    if (base) shot(base, "amp-env");
    press(HB_BTN_EDIT);
    CHECK(!strcmp(PAGES[page].title, "FILTER"), "EDIT from another section: its first page");
    press(HB_BTN_EDIT);
    CHECK(!strcmp(PAGES[page].title, "OSCILLATORS"), "EDIT again: the oscillators");
    raw()[P5_FREQ_A] = 24; raw()[P5_KEY_B] = 1; raw()[P5_FREQ_B] = 38;
    CHECK(!strcmp(shown(0), "+0 st") && !strcmp(shown(1), "+7 st"), "FREQ A +0 st, B +7 st: %s %s", shown(0), shown(1));
    turn(HB_ENC_KNOB2, 5);
    CHECK(raw()[P5_FREQ_B] == 48u, "FREQ B: a detent a semitone (raw %u)", raw()[P5_FREQ_B]);
    press(HB_BTN_EDIT);
    CHECK(!strcmp(PAGES[page].title, "WAVEFORMS"), "EDIT a third time: the waveforms");
    raw()[P5_SAW_B] = 1; raw()[P5_TRI_B] = 0; raw()[P5_PULSE_B] = 1;
    CHECK(!strcmp(shown(1), "SAW+PLS"), "OSC B: SAW+PLS (%s)", shown(1));
    turn(HB_ENC_KNOB2, 1);
    CHECK(raw()[P5_SAW_B] == 0u && raw()[P5_TRI_B] == 1u && raw()[P5_PULSE_B] == 1u, "OSC B: then TRI+PLS");
    if (base) shot(base, "waves");
    press(HB_BTN_EDIT);
    CHECK(page == 0u, "EDIT after its last page: HOME");
    press(HB_BTN_LFO);
    raw()[P5_LFO_RATE] = 74;
    CHECK(!strncmp(shown(0), "2.4", 3), "LFO 74: ~2.4 Hz, as Melodee measured (%s)", shown(0));
    hb_module.draw(0);
    if (base) shot(base, "lfo");
    for (k = 0; k < NPAGES; k++)                /* every value of every control fits its column */
        for (i = 0; i < 4u; i++) {
            const ctl_t *c = &PAGES[k].c[i];
            int32_t v, worst_w = 0;
            char s[16];
            for (v = 0; v <= ctl_max(c); v++) {
                int32_t w;
                page = k;
                ctl_text(s, c, v);
                w = ntext_w(s);
                if (w > worst_w) worst_w = w;
            }
            CHECK(worst_w <= COL_W - 6 && ntext_w(c->label) <= COL_W - 6, "%s %s fits its column, a gap to the next (%d px)",
                  PAGES[k].title, c->label, worst_w);
        }
    page = 0;

    /* a DAW's start / stop (all notes off, all sound off): the notes let go, the program's edits kept */
    select_preset(FIRST_1978);
    raw()[P5_CUTOFF] = 99; edited = 1;
    hb_module.note(0, 60, 100); hb_module.note(0, 64, 100); play(12);
    hb_module.midi(0xB0u, 123u, 0); play(12);
    for (n = 0, i = 0; i < NVOICE; i++) n += voices[i].gate;
    CHECK(n == 0u && raw()[P5_CUTOFF] == 99u && edited && cur == FIRST_1978, "CC 123: notes off, the edit kept");
    hb_module.note(0, 60, 100); play(12);
    hb_module.midi(0xB0u, 120u, 0); play(12);
    for (n = 0, i = 0; i < NVOICE; i++) n += voices[i].gate;
    CHECK(n == 0u && raw()[P5_CUTOFF] == 99u, "CC 120: likewise");
    play(30000);

    /* the chord generator (SEL): MAJOR, inverted, voiced, strummed; a release lets go of the chord it played */
    select_preset(FIRST_1978);
    press(HB_BTN_SEL);
    CHECK(!strcmp(PAGES[page].title, "CHORD") && led_on[HB_BTN_SEL] == 1u, "SEL: the chord generator");
    turn(HB_ENC_KNOB1, 1);
    CHECK(!strcmp(shown(0), "MAJOR"), "KNOB 1: MAJOR (%s)", shown(0));
    hb_module.note(0, 60, 100); play(12);
    for (n = 0, i = 0; i < NVOICE; i++)
        n += voices[i].gate && (voices[i].note == 60u || voices[i].note == 64u || voices[i].note == 67u);
    CHECK(n == 3u, "C plays C E G (%u)", n);
    turn(HB_ENC_KNOB1, 1);                      /* (MINOR now: the release still lets go of C E G) */
    hb_module.note(0, 60, 0); play(12);
    for (n = 0, i = 0; i < NVOICE; i++) n += voices[i].gate;
    CHECK(n == 0u, "its release lets go of all three (%u held)", n);
    turn(HB_ENC_KNOB2, 1); turn(HB_ENC_KNOB3, 2);   /* MINOR, first inversion, +BASS: Eb G C' and C an octave down */
    hb_module.note(0, 60, 100); play(12);
    for (n = 0, i = 0; i < NVOICE; i++)
        n += voices[i].gate && (voices[i].note == 63u || voices[i].note == 67u || voices[i].note == 72u || voices[i].note == 63u - 12u);
    CHECK(n == 4u, "C minor, first inversion, +BASS: Eb G C, Eb below (%u)", n);
    hb_module.note(0, 60, 0); play(12);
    turn(HB_ENC_KNOB3, -2); turn(HB_ENC_KNOB4, 5);  /* strummed, 50 ms a note */
    CHECK(!strcmp(shown(3), "50 ms"), "STRUM 50 ms (%s)", shown(3));
    hb_module.note(0, 48, 100); play(12);
    for (n = 0, i = 0; i < NVOICE; i++) n += voices[i].gate;
    CHECK(n == 1u, "strummed: the first note at once (%u)", n);
    for (i = 0; i < 20u; i++) { now_ms += 10u; hb_module.draw(0); play(6); }
    for (n = 0, i = 0; i < NVOICE; i++) n += voices[i].gate;
    CHECK(n == 3u, "then the others (%u)", n);
    if (base) shot(base, "chord");
    hb_module.note(0, 48, 0);
    turn(HB_ENC_KNOB1, -20); turn(HB_ENC_KNOB2, -5); turn(HB_ENC_KNOB4, -20);   /* chords off again */
    play(30000);
    page = 0;
    arp_and_loop(base);

    for (k = 0; k < E->npresets; k++) {         /* every program: a held chord sounds, within full scale */
        select_preset(k);
        peak = clips = 0;
        for (i = 0; i < 5u; i++)
            hb_module.note(0, CHORD[i], 100);
        play(2500);
        for (i = 0; i < 5u; i++)
            hb_module.note(0, CHORD[i], 0);
        play(300);
        E->preset_make(k, &pr, name);
        if (peak < 300) { quiet++; printf("  quiet: %u %s (peak %d)\n", k, name, peak); }
        if (peak > worst_peak) { worst_peak = peak; worst = k; }
        CHECK(clips == 0, "preset %u %s: %d samples at full scale", k, name, clips);
    }
    E->preset_make(worst, &pr, name);
    printf("  loudest: %u %s, peak %d of 32767\n", worst, name, worst_peak);
    CHECK(quiet == 0, "%u programs silent under a held chord", quiet);

    select_preset(FIRST_1978);                  /* POLY: eight voices with core 1, the ninth note steals the oldest */
    CHECK(T->p[P_VOICE] == V_POLY, "511 plays POLY");
    c1_jobs = 0;
    for (i = 0; i < 9u; i++) { hb_module.note(0, 50u + i, 100); play(12); }
    CHECK(active() == 8u, "nine keys, eight voices on two cores (%u)", active());
    CHECK(c1_jobs > 0, "core 1 rendered voices");
    for (i = 0; i < NVOICE; i++)
        CHECK(voices[i].note != 50u, "the oldest (50) was stolen");
    peak = 0;
    c1_late = 1;                                /* core 1 times out mid-chord: core 0 renders on, five for new notes */
    play(200);
    CHECK(peak > 300 && !audio_worker_online && c1_dead, "core 1 late: the chord sounds on (peak %d)", peak);
    for (i = 0; i < 9u; i++) hb_module.note(0, 50u + i, 0);
    play(30000);
    CHECK(active() == 0u, "all released, all voices end (%u left)", active());
    for (i = 0; i < 6u; i++) { hb_module.note(0, 70u + i, 100); play(12); }
    CHECK(active() == 5u, "one core: six keys, five voices (%u)", active());
    for (i = 0; i < 6u; i++) hb_module.note(0, 70u + i, 0);
    play(30000);

    for (k = 1; k < E->npresets && T->p[P_VOICE] != V_UNISON; k++)   /* UNISON: its voices on the lowest key held */
        select_preset(k);
    CHECK(T->p[P_VOICE] == V_UNISON, "a factory program plays UNISON");
    if (T->p[P_VOICE] == V_UNISON) {
        uint32_t nv = E->cap(T);
        hb_module.note(0, 60, 100); hb_module.note(0, 64, 100); play(12);
        for (i = 0; i < nv; i++)
            CHECK(voices[i].active && voices[i].note == (T->p[P_PRIO] == 1 ? 60u : 64u), "unison voice %u on the key", i);
        hb_module.note(0, 60, 0); hb_module.note(0, 64, 0);
    }

    snprintf(path, sizeof path, "%s.wav", base ? base : "");
    if (base && (wav = fopen(path, "wb"))) {   /* the 1978 programs, for listening */
        static uint8_t hdr[44];
        fwrite(hdr, 1, 44, wav);
        for (k = FIRST_1978; k < E->npresets; k++) {
            select_preset(k);
            for (i = 0; i < 3u; i++) hb_module.note(0, CHORD[i + 1u], 100);
            play(1500);
            for (i = 0; i < 3u; i++) hb_module.note(0, CHORD[i + 1u], 0);
            play(700);
        }
        uint32_t f[] = {0x46464952u, 36u + 2u * wav_n, 0x45564157u, 0x20746D66u, 16u, 1u | 1u << 16, 44100u, 88200u,
                        2u | 16u << 16, 0x61746164u, 2u * wav_n};
        fseek(wav, 0, SEEK_SET);
        fwrite(f, 4, 11, wav);
        fclose(wav);
        printf("  %s: the 1978 programs, %u s\n", path, wav_n / 44100u);
    }
    printf(failed ? "profm78_host: FAILED\n" : "profm78_host: ok\n");
    return failed;
}
