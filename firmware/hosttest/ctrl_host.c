/* Host test of app/ctrl.c: stubs for the FM-1 SDK, scripted panel input, captured MIDI/SysEx and a framebuffer. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fm1.h"
#include "ctrl.c"

/* ---- SDK stubs ---- */
static struct fm1_controls ctl;
static uint32_t ms;
static uint16_t fb[240 * 240];
static unsigned char midi_log[4096][3]; static unsigned midi_port_log[4096];
static int midi_n; static int midi_full;
static unsigned ready_ports = 1U << FM1_MIDI_USB;
unsigned fm1_midi_ready_ports(void) { return ready_ports; }
static unsigned char sx_log[64][40]; static int sx_len[64]; static int sx_n;
int groove_master_update(unsigned adc) { (void)adc; return 0; }
void fm1_controls_init(void) {}
void fm1_controls_read(struct fm1_controls *s) { *s = ctl; memset(ctl.encoders, 0, sizeof ctl.encoders); }
void fm1_led_set(unsigned k, int on) { (void)k; (void)on; }
uint32_t fm1_millis(void) { return ms; }
void fm1_control_request(void) {}
void fm1_screen_init(void) {}
void fm1_screen_fill(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t c) { for (unsigned j = y; j < y + h && j < 240; j++) for (unsigned i = x; i < x + w && i < 240; i++) fb[j * 240 + i] = c; }
int fm1_screen_blit(unsigned x, unsigned y, unsigned w, unsigned h, const uint16_t *p, size_t n) { (void)n; for (unsigned j = 0; j < h; j++) for (unsigned i = 0; i < w; i++) if (x + i < 240 && y + j < 240) fb[(y + j) * 240 + x + i] = p[j * w + i]; return 0; }
int fm1_midi_send_port(unsigned port, const uint8_t *m, size_t n) {
    if (!(ready_ports & (1U << port)) || midi_full) return -2;
    if (m[0] == 0xf0) { sx_len[sx_n] = (int)n; memcpy(sx_log[sx_n], m, n > 40 ? 40 : n); sx_n++; return 0; }
    midi_port_log[midi_n] = port;
    midi_log[midi_n][0] = m[0]; midi_log[midi_n][1] = m[1]; midi_log[midi_n][2] = m[2]; midi_n++; return 0;
}

static int fails;
#define CHECK(c, ...) do { if (c) printf("PASS  "); else { printf("FAIL  "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)
static void tick(void) { ms += 33; groove_screen_control(); }
static void press_key(int k, int down) { if (down) ctl.keys |= 1UL << k; else ctl.keys &= ~(1UL << k); tick(); }
static int count_button(unsigned port, unsigned status, unsigned button) {
    int found = 0;
    for (int i = 0; i < midi_n; ++i)
        if (midi_port_log[i] == port && midi_log[i][0] == status &&
            midi_log[i][1] == button) ++found;
    return found;
}
static int last_on(void) { for (int i = midi_n - 1; i >= 0; i--) if (midi_log[i][0] == 0x90) return midi_log[i][1]; return -1; }
static void ppm(const char *p) {
    FILE *f = fopen(p, "wb"); fprintf(f, "P6\n480 480\n255\n");
    for (int y = 0; y < 480; y++) for (int x = 0; x < 480; x++) { uint16_t c = fb[(y / 2) * 240 + x / 2]; unsigned char rgb[3] = {(unsigned char)((c >> 11) << 3), (unsigned char)(((c >> 5) & 63) << 2), (unsigned char)((c & 31) << 3)}; fwrite(rgb, 1, 3, f); }
    fclose(f);
}

int main(void) {
    groove_screen_init();
    /* default scale: MIN, root C, octave 0: key 0 = C4 (60), key 2 = Eb, key 7 = C5 */
    press_key(0, 1); CHECK(last_on() == 60, "key 0 -> %d (C)", last_on()); press_key(0, 0);
    press_key(2, 1); CHECK(last_on() == 63, "key 2 -> %d (Eb, minor third)", last_on()); press_key(2, 0);
    press_key(7, 1); CHECK(last_on() == 72, "key 7 -> %d (C an octave up)", last_on()); press_key(7, 0);
    press_key(26, 1); CHECK(last_on() == 104, "key 26 -> %d", last_on()); press_key(26, 0);
    int ons = 0, offs = 0; for (int i = 0; i < midi_n; i++) { if (midi_log[i][0] == 0x90) ons++; if (midi_log[i][0] == 0x80) offs++; }
    CHECK(ons == 4 && offs == 4, "every note on has its off (%d/%d)", ons, offs);
    /* scale screen: hold SCL, K1 next scale (HMIN), K2 root +2 (D), K3 octave +1 */
    ctl.buttons |= 1U << 3; tick();
    ctl.encoders[0] = 1; tick(); ctl.encoders[1] = 1; tick(); ctl.encoders[1] = 1; tick(); ctl.encoders[2] = 1; tick();
    ppm("scale_screen_min.ppm");
    CHECK(sc_scale == 4 && sc_root == 2 && sc_oct == 1, "scale HMIN root D oct +1 (got %u %u %d)", sc_scale, sc_root, sc_oct);
    int cc16 = 0; for (int i = 0; i < midi_n; i++) if (midi_log[i][0] == 0xb0 && midi_log[i][1] >= 16 && midi_log[i][1] < 23) cc16++;
    CHECK(cc16 == 0, "encoders on the scale screen are not forwarded");
    ctl.buttons &= ~(1U << 3); tick();
    int redraw = 0, state = 0; for (int i = 0; i < sx_n; i++) { if (sx_log[i][6] == EV_REDRAW) redraw++; if (sx_log[i][6] == EV_SCALE) state++; }
    CHECK(redraw == 1 && state >= 4, "phone told to redraw once, state reported %d times", state);
    press_key(0, 1); CHECK(last_on() == 60 + 2 + 12, "HMIN root D oct+1: key 0 -> %d (D5)", last_on()); press_key(0, 0);
    press_key(6, 1); CHECK(last_on() == 60 + 2 + 12 + 11, "HMIN key 6 -> %d (the raised 7th)", last_on()); press_key(6, 0);
    /* OCT messages are required by the Android app, not just local shifts. */
    int down0 = count_button(FM1_MIDI_USB, 0x91, 0);
    int up0 = count_button(FM1_MIDI_USB, 0x81, 0);
    ctl.buttons |= 1U << 0; tick();
    CHECK(count_button(FM1_MIDI_USB, 0x91, 0) == down0 + 1,
          "OCT- Note On (CH2 note 0) transmitted over USB immediately");
    tick(); tick();
    CHECK(count_button(FM1_MIDI_USB, 0x91, 0) == down0 + 1,
          "OCT- held is not spuriously re-sent as repeated Note Ons");
    ctl.buttons &= ~(1U << 0); tick();
    CHECK(count_button(FM1_MIDI_USB, 0x81, 0) == up0 + 1,
          "OCT- Note Off (CH2 note 0) transmitted on release");
    CHECK(sc_oct == 0, "OCT- tap still lowers keyboard octave");

    const int initial_oct = sc_oct;
    down0 = count_button(FM1_MIDI_USB, 0x91, 0);
    int playDown = count_button(FM1_MIDI_USB, 0x91, 12);
    ctl.buttons |= 1U << 0; tick();
    tick(); tick(); // OCT- arrives first, PLAY arrives on a later scan
    ctl.buttons |= 1U << 12; tick();
    CHECK(count_button(FM1_MIDI_USB, 0x91, 0) == down0 + 1 &&
          count_button(FM1_MIDI_USB, 0x91, 12) == playDown + 1,
          "OCT- and PLAY emit independent MIDI Note On events");
    ctl.buttons &= ~(1U << 12); tick();
    ctl.buttons &= ~(1U << 0); tick();
    CHECK(sc_oct == initial_oct, "OCT- used as modifier does not shift octave");
    CHECK(count_button(FM1_MIDI_USB, 0x81, 12) >= 1,
          "PLAY/STOP emits matching Note Off on release");

    int up1 = count_button(FM1_MIDI_USB, 0x81, 1);
    int down1 = count_button(FM1_MIDI_USB, 0x91, 1);
    ctl.buttons |= 1U << 1; tick();
    CHECK(count_button(FM1_MIDI_USB, 0x91, 1) == down1 + 1,
          "OCT+ Note On transmitted over USB");
    ctl.buttons |= 1U << 12; tick();
    ctl.encoders[2] = 1; tick(); // K1 on firmware maps to CC18
    int k1 = 0;
    for (int i = 0; i < midi_n; i++)
        if (midi_log[i][0] == 0xb0 && midi_log[i][1] == 18) ++k1;
    CHECK(k1 >= 1, "K1 is forwarded as CC18 while NOTE REPEAT held");
    ctl.buttons &= ~(1U << 12); tick();
    ctl.buttons &= ~(1U << 1); tick();
    CHECK(count_button(FM1_MIDI_USB, 0x81, 1) == up1 + 1,
          "OCT+ Note Off transmitted on release");

    // BLE receiver must get all three physical panel events too; the USB
    // port may be unplugged while Android remains connected over Bluetooth.
    ready_ports = 1U << FM1_MIDI_BLE;
    down0 = count_button(FM1_MIDI_BLE, 0x91, 0);
    ctl.buttons |= 1U << 0; tick();
    ctl.buttons |= 1U << 12; tick();
    CHECK(count_button(FM1_MIDI_BLE, 0x91, 0) == down0 + 1 &&
          count_button(FM1_MIDI_BLE, 0x91, 12) >= 1,
          "BLE transmits OCT- and PLAY independently without USB");
    ctl.buttons &= ~(1U << 12); tick();
    ctl.buttons &= ~(1U << 0); tick();
    CHECK(count_button(FM1_MIDI_BLE, 0x81, 0) >= 1 &&
          count_button(FM1_MIDI_BLE, 0x81, 12) >= 1,
          "BLE transmits corresponding OCT-/PLAY releases");
    ready_ports = 1U << FM1_MIDI_USB;
    /* chord mode */
    scale_set(3, 0, 0, MODE_CHORD);
    midi_n = 0; press_key(0, 1);
    CHECK(midi_n == 3 && midi_log[0][1] == 60 && midi_log[1][1] == 63 && midi_log[2][1] == 67, "chord mode: key 0 -> C Eb G (%d %d %d)", midi_log[0][1], midi_log[1][1], midi_log[2][1]);
    scale_set(0, 0, 0, MODE_KEY); press_key(0, 0);   /* scale changed while held: the Note Offs must still be the chord */
    int off_ok = 0; for (int i = 3; i < midi_n; i++) if (midi_log[i][0] == 0x80 && (midi_log[i][1] == 60 || midi_log[i][1] == 63 || midi_log[i][1] == 67)) off_ok++;
    CHECK(off_ok == 3, "changing the scale while a chord is held releases the right notes (%d)", off_ok);
    /* OFF = plain keyboard */
    scale_set(SCALE_OFF, 0, 0, MODE_KEY); press_key(5, 1); CHECK(last_on() == 58, "scale OFF: key 5 -> %d (chromatic from F3)", last_on()); press_key(5, 0);
    /* phone sets the scale */
    { uint8_t m[] = {0xf0, 0x7d, 'D', 'A', 'W', 1, CMD_SCALE, 2, 9, 3 + 0, 0, 0xf7}; ctrl_sysex(m, sizeof m); tick(); }
    CHECK(sc_scale == 2 && sc_root == 9 && sc_oct == 0, "phone SysEx sets MAJ root A");
    /* queue full: Note On is not lost silently */
    midi_full = 1; press_key(1, 1); midi_full = 0; press_key(1, 0);
    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails != 0;
}
