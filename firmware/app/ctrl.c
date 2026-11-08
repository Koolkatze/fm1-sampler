/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * FM1-DAW control surface: panel events out over USB MIDI, LEDs + screen
 * commands in over SysEx. Replaces the stock UI (app/screen.c).
 * Protocol: see ../../PROTOCOL.md */
#include <string.h>
#include "fm1.h"
#include "instrument.h"

enum { SYSEX_MAX = 300, SYSEX_Q = 4, KEYS = 27, BUTTONS = 14, ENC = 7, KEY_BASE = 53 };
enum { CMD_LED = 1, CMD_CLEAR = 2, CMD_RECT = 3, CMD_TEXT = 4, CMD_PING = 5, CMD_SCALE = 6, CMD_PAL = 8, CMD_BLIT = 9, CMD_BATCH = 10, CMD_NOP = 0x7e };   /* numbers are the wire format (PROTOCOL.md): keep them explicit */
enum { EV_SCALE = 0x41, EV_REDRAW = 0x42 };
enum { BTN_OCT_DOWN = 0, BTN_OCT_UP = 1, BTN_SCL = 3, BTN_PLAY = 12 };

/* ---- SysEx receive: routing task -> control task ------------------------- */
static struct { uint8_t data[SYSEX_MAX]; volatile uint16_t length; } rxq[SYSEX_Q];
static volatile uint8_t rx_head;            /* written by routing task */
static uint8_t rx_tail;                     /* read by control task */
static volatile uint32_t rx_frames, rx_bytes, rx_dropped;

static void barrier(void)
{
#ifndef FM1_HOST_TEST
    __asm__ volatile("csync" ::: "memory");
#endif
}

/* Called from fm1_app_sysex (transfer.c) before its own filtering. */
int ctrl_sysex(const uint8_t *m, size_t n)
{
    if (n < 7 || m[0] != 0xf0 || m[1] != 0x7d || m[2] != 'D' || m[3] != 'A' || m[4] != 'W') return 0;
    rx_frames++; rx_bytes += (uint32_t)n;
    uint8_t slot = rx_head;
    if (n > SYSEX_MAX || rxq[slot].length) { rx_dropped++; return 1; }
    if (m[6] == CMD_NOP) return 1;
    memcpy(rxq[slot].data, m, n);
    barrier();
    rxq[slot].length = (uint16_t)n;
    rx_head = (uint8_t)((slot + 1) % SYSEX_Q);
    fm1_control_request();
    return 1;
}

/* ---- drawing ------------------------------------------------------------- */
/* Five columns per glyph, bit 0 at the top. 0-9, A-Z, then punctuation. */
static const uint8_t font[36][5] = {
    {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0, 0x42, 0x7f, 0x40, 0},       {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4b, 0x31}, {0x18, 0x14, 0x12, 0x7f, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3c, 0x4a, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1e}, {0x7e, 0x11, 0x11, 0x11, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36},
    {0x3e, 0x41, 0x41, 0x41, 0x22}, {0x7f, 0x41, 0x41, 0x22, 0x1c}, {0x7f, 0x49, 0x49, 0x49, 0x41},
    {0x7f, 0x09, 0x09, 0x09, 0x01}, {0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f},
    {0, 0x41, 0x7f, 0x41, 0},       {0x20, 0x40, 0x41, 0x3f, 0x01}, {0x7f, 0x08, 0x14, 0x22, 0x41},
    {0x7f, 0x40, 0x40, 0x40, 0x40}, {0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f},
    {0x3e, 0x41, 0x41, 0x41, 0x3e}, {0x7f, 0x09, 0x09, 0x09, 0x06}, {0x3e, 0x41, 0x51, 0x21, 0x5e},
    {0x7f, 0x09, 0x19, 0x29, 0x46}, {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7f, 0x01, 0x01},
    {0x3f, 0x40, 0x40, 0x40, 0x3f}, {0x1f, 0x20, 0x40, 0x20, 0x1f}, {0x3f, 0x40, 0x38, 0x40, 0x3f},
    {0x63, 0x14, 0x08, 0x14, 0x63}, {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}};
static const uint8_t punct[][6] = {   /* char, 5 columns */
    {'.', 0, 0x60, 0x60, 0, 0},      {':', 0, 0x36, 0x36, 0, 0},      {'-', 8, 8, 8, 8, 8},
    {'/', 0x20, 0x10, 0x08, 0x04, 0x02}, {'%', 0x23, 0x13, 0x08, 0x64, 0x62}, {'+', 8, 8, 0x3e, 8, 8},
    {'>', 0, 0x41, 0x22, 0x14, 8},   {'<', 8, 0x14, 0x22, 0x41, 0}, {'_', 0x40, 0x40, 0x40, 0x40, 0x40},
    {'!', 0, 0, 0x5f, 0, 0},         {'?', 2, 1, 0x51, 9, 6},        {'#', 0x14, 0x7f, 0x14, 0x7f, 0x14},
};
enum { MAX_SCALE = 4, CELL_W = 6 * MAX_SCALE, CELL_H = 8 * MAX_SCALE };
static uint16_t cell[CELL_W * CELL_H];   /* glyph cell, and the pixel buffer of CMD_BLIT (768 px) */
static uint16_t blit_pal[16];

static const uint8_t *glyph(unsigned c)
{
    static const uint8_t blank[5];
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c >= '0' && c <= '9') return font[c - '0'];
    if (c >= 'A' && c <= 'Z') return font[10 + c - 'A'];
    for (unsigned i = 0; i < sizeof punct / sizeof punct[0]; ++i)
        if (punct[i][0] == c) return punct[i] + 1;
    return blank;
}

static void draw_char(unsigned x, unsigned y, unsigned scale, unsigned c, uint16_t fg, uint16_t bg)
{
    unsigned w = 6 * scale, h = 8 * scale;
    if (x + w > FM1_SCREEN_WIDTH || y + h > FM1_SCREEN_HEIGHT) return;
    const uint8_t *g = glyph(c);
    for (unsigned py = 0; py < h; ++py) {
        unsigned row = py / scale;
        for (unsigned px = 0; px < w; ++px) {
            unsigned col = px / scale;
            cell[py * w + px] = col < 5 && row < 7 && (g[col] >> row & 1U) ? fg : bg;
        }
    }
    (void)fm1_screen_blit(x, y, w, h, cell, w * h);
}

static unsigned n14(const uint8_t *p) { return (unsigned)(p[0] & 127U) << 7 | (p[1] & 127U); }
static uint16_t color(const uint8_t *p) { return (uint16_t)((p[0] & 3U) << 14 | (p[1] & 127U) << 7 | (p[2] & 127U)); }

/* ---- scales (key index -> scale degree, root, octave, KEY or CHORD mode) ---------------- */
enum { SCALE_OFF = 7, SCALES = 8, MODE_KEY = 0, MODE_CHORD = 1 };
static const struct { const char *name; uint8_t n, step[12]; } scale_tab[SCALES - 1] = {
    {"CHROM", 12, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    {"MIX", 7, {0, 2, 4, 5, 7, 9, 10}},
    {"MAJ", 7, {0, 2, 4, 5, 7, 9, 11}},
    {"MIN", 7, {0, 2, 3, 5, 7, 8, 10}},
    {"HMIN", 7, {0, 2, 3, 5, 7, 8, 11}},
    {"PHRYG", 7, {0, 1, 3, 5, 7, 8, 10}},
    {"MIN+", 8, {0, 2, 3, 5, 7, 8, 10, 11}},
};
static const char *const note_name[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static uint8_t sc_scale = 3, sc_root, sc_mode;      /* default MIN, root C, KEY */
static int8_t sc_oct;                                /* -3..+3 */
static uint8_t overlay, overlay_dirty;

/* Note of scale degree `k` (0 = root of the first octave). */
static int degree_note(unsigned k)
{
    if (sc_scale == SCALE_OFF) return 53 + (int)k;   /* the plain chromatic keyboard, F3 upwards */
    const unsigned n = scale_tab[sc_scale].n;
    return 60 + sc_root + 12 * sc_oct + (int)(k / n) * 12 + scale_tab[sc_scale].step[k % n];
}
static unsigned key_notes(unsigned key, uint8_t *out)   /* notes sounded by one key, 1 or 3 */
{
    if (sc_scale == SCALE_OFF || sc_mode == MODE_KEY) { out[0] = (uint8_t)degree_note(key); return 1; }
    for (unsigned j = 0; j < 3; ++j) out[j] = (uint8_t)degree_note(key + 2 * j);
    return 3;
}
static uint8_t held_notes[KEYS][3], held_count[KEYS];

/* ---- MIDI out ------------------------------------------------------------ */
/* FM1 Sampler is a USB-MIDI panel: OCT-/OCT+/PLAY Note On/Off and
 * the K1 encoder are sent as separate events on the USB MIDI port.
 * The Android receiver maintains modifier state until Note Off. */
static int midi3(uint8_t status, uint8_t a, uint8_t b)
{
    uint8_t m[3] = {status, a, b};
    return fm1_midi_send_port(FM1_MIDI_USB, m, 3);
}
static void sysex_reply(const uint8_t *body, unsigned n)
{
    uint8_t m[40] = {0xf0, 0x7d, 'D', 'A', 'W', 1};
    if (n > sizeof m - 7) return;
    memcpy(m + 6, body, n);
    m[6 + n] = 0xf7;
    (void)fm1_midi_send_port(FM1_MIDI_USB, m, 7 + n);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 28 & 15U); p[1] = (uint8_t)(v >> 21 & 127U); p[2] = (uint8_t)(v >> 14 & 127U);
    p[3] = (uint8_t)(v >> 7 & 127U); p[4] = (uint8_t)(v & 127U);
}

static void report_scale(void)
{
    uint8_t b[6] = {EV_SCALE, sc_scale, sc_root, (uint8_t)(sc_oct + 3), sc_mode, 0};
    sysex_reply(b, 5);
}
static void scale_set(unsigned scale, unsigned root, int oct, unsigned mode)
{
    sc_scale = (uint8_t)(scale % SCALES); sc_root = (uint8_t)(root % 12);
    sc_oct = (int8_t)(oct < -3 ? -3 : oct > 3 ? 3 : oct); sc_mode = (uint8_t)(mode & 1U);
    overlay_dirty = 1; report_scale();
}

static void draw_text(unsigned x, unsigned y, unsigned scale, const char *t, uint16_t fg, uint16_t bg)
{
    for (; *t; ++t, x += 6 * scale) draw_char(x, y, scale, (unsigned char)*t, fg, bg);
}
static void draw_overlay(void)
{
    char b[24];
    fm1_screen_fill(0, 0, 240, 240, 0);
    fm1_screen_fill(0, 0, 240, 28, 0x20e4);
    draw_text(8, 6, 2, "SCALE", 0x07ff, 0x20e4);
    unsigned n = 0;
    const char *r = note_name[sc_root];
    while (*r) b[n++] = *r++;
    b[n++] = ' ';
    const char *sn = sc_scale == SCALE_OFF ? "OFF" : scale_tab[sc_scale].name;
    while (*sn) b[n++] = *sn++;
    b[n] = 0;
    draw_text(8, 44, 4, b, 0xef5d, 0);
    draw_text(8, 92, 2, sc_mode == MODE_KEY ? "MODE  KEY" : "MODE  CHORD", 0xfd20, 0);
    b[0] = 'O'; b[1] = 'C'; b[2] = 'T'; b[3] = ' '; b[4] = ' '; b[5] = sc_oct < 0 ? '-' : '+'; b[6] = (char)('0' + (sc_oct < 0 ? -sc_oct : sc_oct)); b[7] = 0;
    draw_text(8, 116, 2, b, 0xfd20, 0);
    draw_text(8, 152, 1, "K1 SCALE  K2 ROOT  K3 OCT  K4 MODE", 0x738e, 0);
    draw_text(8, 168, 1, "KEYS FROM THE LEFT:", 0x738e, 0);
    unsigned x = 8, y = 182;
    for (unsigned k = 0; k < 14; ++k) {
        int nt = degree_note(k);
        const char *nm = note_name[nt % 12];
        draw_text(x, y, 1, nm, k % 7 == 0 && sc_scale != SCALE_OFF ? 0x07ff : 0xef5d, 0);
        x += 16;
    }
    x = 8; y = 196;
    for (unsigned k = 14; k < 27; ++k) {
        int nt = degree_note(k);
        draw_text(x, y, 1, note_name[nt % 12], 0x738e, 0);
        x += 16;
    }
    draw_text(8, 224, 1, "RELEASE SCL TO RETURN", 0x738e, 0);
}

/* ---- state --------------------------------------------------------------- */
static uint32_t sent_buttons;
static uint32_t down_keys;                 /* keys whose Note On was sent */
static int enc_acc[ENC];
static int knob_sent = -100;
static uint32_t battery_at;

static void leds_apply(const uint8_t *p, unsigned n)
{
    if (n < 6) return;
    uint64_t mask = 0;
    for (unsigned i = 0; i < 6; ++i) mask |= (uint64_t)(p[i] & 127U) << (7U * i);
    for (unsigned k = 0; k < 41; ++k) fm1_led_set(k, (int)(mask >> k & 1U));
}

static void screen_cmd(unsigned cmd, const uint8_t *p, unsigned len);

/* CMD_BATCH: many small commands in one frame (the queue is counted in frames, not bytes). Payload: { n14 length, cmd, payload... } repeated. */
static void screen_batch(const uint8_t *p, unsigned len)
{
    unsigned i = 0;
    while (i + 3 <= len) {
        unsigned l = n14(p + i);
        if (l < 1 || i + 2 + l > len) break;
        screen_cmd(p[i + 2], p + i + 3, l - 1);
        i += 2 + l;
    }
}

static void screen_frame(const uint8_t *m, unsigned n)
{
    screen_cmd(m[6], m + 7, n - 8);
}

static void screen_cmd(unsigned cmd, const uint8_t *p, unsigned len)
{
    const unsigned drawing = cmd == CMD_CLEAR || cmd == CMD_RECT || cmd == CMD_TEXT || cmd == CMD_BLIT || cmd == CMD_BATCH;
    if (drawing && overlay) return;        /* the on-device scale screen owns the display */
    switch (cmd) {
    case CMD_LED: leds_apply(p, len); break;
    case CMD_CLEAR: if (len >= 3) fm1_screen_fill(0, 0, FM1_SCREEN_WIDTH, FM1_SCREEN_HEIGHT, color(p)); break;
    case CMD_RECT:
        if (len >= 11) {
            unsigned x = n14(p), y = n14(p + 2), w = n14(p + 4), h = n14(p + 6);
            if (w && h && x + w <= FM1_SCREEN_WIDTH && y + h <= FM1_SCREEN_HEIGHT)
                fm1_screen_fill(x, y, w, h, color(p + 8));
        }
        break;
    case CMD_TEXT:   /* x14 y14 scale fg(3) bg(3) text... */
        if (len >= 11) {
            unsigned x = n14(p), y = n14(p + 2), scale = p[4];
            uint16_t fg = color(p + 5), bg = color(p + 8);
            if (scale < 1 || scale > MAX_SCALE) break;
            for (unsigned i = 11; i < len; ++i, x += 6 * scale) draw_char(x, y, scale, p[i], fg, bg);
        }
        break;
    case CMD_PING:   /* id14 -> PONG id14 millis32 rx_frames rx_bytes rx_dropped */
        if (len >= 2) {
            uint8_t r[2 + 2 + 5 + 5 + 5 + 5] = {0x40, p[0], p[1]};
            put32(r + 3, fm1_millis()); put32(r + 8, rx_frames); put32(r + 13, rx_bytes); put32(r + 18, rx_dropped);
            sysex_reply(r, 23);
        }
        break;
    case CMD_PAL:    /* 16 colours x color(3): the palette of CMD_BLIT */
        for (unsigned i = 0; i < 16 && 3 * i + 3 <= len; ++i) blit_pal[i] = color(p + 3 * i);
        break;
    case CMD_BLIT:   /* x14 y14 w14 h14 tokens: a w*h rectangle (<= 768 px) of palette colours, run-length coded.
                      * Token: 1 byte = colour(4) << 3 | (run - 1); a run field of 7 means the next byte adds to it: run = 8 + next. */
        if (len >= 8) {
            unsigned x = n14(p), y = n14(p + 2), w = n14(p + 4), h = n14(p + 6), px = 0, total = w * h;
            if (!w || !h || total > CELL_W * CELL_H || x + w > FM1_SCREEN_WIDTH || y + h > FM1_SCREEN_HEIGHT) break;
            for (unsigned i = 8; i < len && px < total; ++i) {
                unsigned col = p[i] >> 3 & 15U, run = (p[i] & 7U) + 1U;
                if ((p[i] & 7U) == 7U && i + 1 < len) run = 8U + p[++i];
                uint16_t c = blit_pal[col];
                while (run-- && px < total) cell[px++] = c;
            }
            if (px == total) (void)fm1_screen_blit(x, y, w, h, cell, total);
        }
        break;
    case CMD_BATCH: screen_batch(p, len); break;
    case CMD_SCALE:  /* scale root octave+3 mode */
        if (len >= 4) scale_set(p[0], p[1], (int)p[2] - 3, p[3]);
        break;
    default: break;
    }
}

void groove_screen_init(void)
{
    sent_buttons = down_keys = 0;
    memset(enc_acc, 0, sizeof enc_acc);
    memset(held_count, 0, sizeof held_count);
    knob_sent = -100; battery_at = 0; overlay = overlay_dirty = 0;
    rx_head = rx_tail = 0;
    for (unsigned i = 0; i < SYSEX_Q; ++i) rxq[i].length = 0;
    fm1_controls_init();
    for (unsigned key = 0; key < 41; ++key) fm1_led_set(key, 0);
    fm1_screen_init();
    fm1_screen_fill(0, 0, FM1_SCREEN_WIDTH, FM1_SCREEN_HEIGHT, 0);
    draw_text(36, 90, 4, "SAMPLER", 0x07ff, 0);
    draw_text(27, 140, 2, "WAITING FOR PHONE", 0x738e, 0);
}

/* A key went down: sound its notes (one in KEY mode, a triad in CHORD mode) and remember them for the Note Off. */
static void key_down(unsigned key)
{
    uint8_t notes[3];
    const unsigned n = key_notes(key, notes);
    unsigned sent = 0;
    for (unsigned j = 0; j < n; ++j) {
        if (midi3(0x90, notes[j], n > 1 ? 85 : 100)) break;
        held_notes[key][sent++] = notes[j];
    }
    held_count[key] = (uint8_t)sent;
    down_keys |= 1UL << key;
}
static void key_up(unsigned key)
{
    while (held_count[key]) {
        if (midi3(0x80, held_notes[key][held_count[key] - 1], 0)) return;   /* retry next tick */
        --held_count[key];
    }
    down_keys &= ~(1UL << key);
}

void groove_screen_control(void)
{
    struct fm1_controls c;
    fm1_controls_read(&c);
    uint32_t now = fm1_millis();
    /* The knob is the physical MASTER: without this the output gain stays 0 and the jack is silent. */
    (void)groove_master_update(c.knob);

    /* SCL held = the on-device scale screen. */
    const unsigned scl = 0;   /* FM1 sampler: the phone owns the tonality; SCL is forwarded as a button (ch2 note 3), no on-device scale screen */
    if (scl && !overlay) { overlay = 1; overlay_dirty = 1; }
    else if (!scl && overlay) { overlay = 0; sysex_reply((const uint8_t[]){EV_REDRAW}, 1); }

    /* Forward OCT buttons like every other panel key so the phone can detect
     * OCT- + PLAY (restart) and OCT+ + PLAY (CUE). A standalone tap still
     * changes octave, but on release so a modifier cannot detune the keys.
     * Holding OCT+ for two seconds enters CUE select and never shifts octave. */
    {
        static uint32_t prev_oct, oct_since[2];
        static uint8_t oct_used[2];
        const uint32_t oct_mask = (1U << BTN_OCT_DOWN) | (1U << BTN_OCT_UP);
        const uint32_t pressed = c.buttons & ~prev_oct & oct_mask;
        const uint32_t released = ~c.buttons & prev_oct & oct_mask;
        for (unsigned i = 0; i < 2; ++i) {
            const uint32_t bit = 1U << i;
            if (pressed & bit) { oct_since[i] = now; oct_used[i] = 0; }
        }
        /* OCT+ and OCT- together select the phone's NOTE REPEAT rate menu.
         * Treat BOTH as consumed even if one was pressed earlier, so neither
         * release shifts the keyboard's octave by accident. MIDI events for
         * both buttons still leave on USB independently below. */
        if ((c.buttons & oct_mask) == oct_mask) {
            oct_used[BTN_OCT_DOWN] = 1;
            oct_used[BTN_OCT_UP] = 1;
        }
        for (unsigned i = 0; i < 2; ++i) {
            const uint32_t bit = 1U << i;
            if (c.buttons & bit) {
                if (c.buttons & (1U << BTN_PLAY)) oct_used[i] = 1;
                if (i == BTN_OCT_UP && (uint32_t)(now - oct_since[i]) >= 2000U) oct_used[i] = 1;
            }
            if (released & bit) {
                if (!oct_used[i]) scale_set(sc_scale, sc_root, sc_oct + (i == BTN_OCT_UP ? 1 : -1), sc_mode);
                oct_used[i] = 0;
            }
        }
        prev_oct = c.buttons & oct_mask;
    }

    /* Keys: notes through the scale, channel 1. */
    uint32_t keys = c.keys & ((1UL << KEYS) - 1U), kdiff = keys ^ down_keys;
    for (unsigned i = 0; i < KEYS && kdiff; ++i) {
        if (!(kdiff >> i & 1U)) continue;
        if (keys >> i & 1U) key_down(i); else key_up(i);
    }
    /* Physical OCT-/OCT+/PLAY: MIDI channel 2 notes 0/1/12.
     * Each press transmits Note On, each release transmits Note Off.
     * The receiving phone keeps the modifier state between the messages;
     * no simultaneous event is necessary. */
    uint32_t forwarded = c.buttons & ((1UL << BUTTONS) - 1U);
    uint32_t bdiff = forwarded ^ sent_buttons;
    for (unsigned i = 0; i < BUTTONS && bdiff; ++i)
        if (bdiff >> i & 1U) {
            if (midi3((forwarded >> i & 1U) ? 0x91 : 0x81, (uint8_t)i, 100)) break;
            sent_buttons ^= 1UL << i;
        }
    /* Encoders: on the scale screen K1..K4 set scale, root, octave, mode; otherwise relative CC 16..22 on channel 1. */
    for (unsigned i = 0; i < ENC; ++i) {
        if (overlay) {
            int d = c.encoders[i];
            if (!d) continue;
            if (i == 0) scale_set((unsigned)((int)sc_scale + (d > 0 ? 1 : SCALES - 1)), sc_root, sc_oct, sc_mode);
            else if (i == 1) scale_set(sc_scale, (unsigned)((int)sc_root + (d > 0 ? 1 : 11)), sc_oct, sc_mode);
            else if (i == 2) scale_set(sc_scale, sc_root, sc_oct + (d > 0 ? 1 : -1), sc_mode);
            else if (i == 3) scale_set(sc_scale, sc_root, sc_oct, (unsigned)!sc_mode);
            continue;
        }
        enc_acc[i] += c.encoders[i];
        while (enc_acc[i]) {
            int d = enc_acc[i] > 63 ? 63 : enc_acc[i] < -63 ? -63 : enc_acc[i];
            if (midi3(0xb0, (uint8_t)(16 + i), (uint8_t)(64 + d))) break;
            enc_acc[i] -= d;
        }
    }
    /* Knob: 14-bit CC 7 (MSB) / 39 (LSB) on channel 1, with hysteresis. */
    int knob = c.knob & 1023;
    if (knob - knob_sent > 3 || knob_sent - knob > 3) {
        if (!midi3(0xb0, 7, (uint8_t)(knob >> 3)) && !midi3(0xb0, 39, (uint8_t)((knob & 7) << 4))) knob_sent = knob;
    }
    /* Battery CC 24 and the scale state every 2 s, so a phone that connects later learns both. */
    if (now - battery_at >= 2000U && !midi3(0xb0, 24, (uint8_t)(c.battery >> 3))) { battery_at = now; report_scale(); }

    /* Phone commands. */
    while (rxq[rx_tail].length) {
        screen_frame(rxq[rx_tail].data, rxq[rx_tail].length);
        rxq[rx_tail].length = 0;
        rx_tail = (uint8_t)((rx_tail + 1) % SYSEX_Q);
    }
    if (overlay && overlay_dirty) { draw_overlay(); overlay_dirty = 0; }
}

void groove_screen_tap(uint32_t now) { (void)now; }
void groove_screen_mmc(unsigned command) { (void)command; }
