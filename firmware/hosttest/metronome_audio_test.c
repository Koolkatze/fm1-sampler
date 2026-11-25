/* Host DSP regression: the FM1 jack is a private monitor while USB capture is untouched. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define FM1_HOST_TEST 1
#include "../app/audio_output.c"

static int32_t recorded[FM1_BLOCK_WORDS];
static unsigned usb_writes;
unsigned fm1_engine_output_level(void) { return 127U; }
int fm1_usb_audio_read(int32_t *pcm, unsigned frames, uint16_t gain[2])
{
    if (frames != FM1_BLOCK_FRAMES) return -1;
    for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) pcm[i] = 20000;
    gain[0] = gain[1] = 128;
    return (int)frames;
}
int fm1_bluetooth_audio_read(int16_t *pcm, unsigned frames)
{
    (void)pcm; (void)frames;
    return 0;
}
int fm1_usb_audio_write(const int32_t *pcm, unsigned frames)
{
    memcpy(recorded, pcm, sizeof recorded);
    ++usb_writes;
    return (int)frames;
}
static int failures;
#define CHECK(expr, message) do { if (!(expr)) { ++failures; printf("FAIL %s\n", message); } else printf("PASS %s\n", message); } while (0)
static int32_t peak(const int32_t *pcm)
{
    int32_t largest = 0;
    for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) {
        const int32_t x = pcm[i] < 0 ? -pcm[i] : pcm[i];
        if (x > largest) largest = x;
    }
    return largest;
}
int main(void)
{
    int32_t jack[FM1_BLOCK_WORDS] = {0};
    fm1_output_reset();
    fm1_metro_control(1200, 7, 25, 80, 3, 0, 0, 0);
    fm1_output_mix(jack);
    CHECK(peak(recorded) > 0,
          "USB capture receives the music stream before headphone-only click");
    CHECK(peak(jack) == 0,
          "private jack suppresses USB music immediately in MON mode");

    int clickSeen = 0, captureClean = 1;
    for (int b = 0; b < 260; ++b) {
        memset(jack, 0, sizeof jack);
        fm1_output_mix(jack);
        if (peak(jack) > 1000) clickSeen = 1;
        /* The FM1 master ramps from 0 to 127 over 16 audio blocks. */
        if (b > 17 && peak(recorded) != (20000 * 127 / 128)) captureClean = 0;
    }
    CHECK(clickSeen, "firmware synthesizes private clicks on the FM1 headphone DAC");
    CHECK(captureClean, "private clicks never enter USB recorded PCM");

    fm1_metro_control(1200, 7, 25, 80, 5, 0, 0, 0);
    memset(jack, 0, sizeof jack);
    fm1_output_mix(jack);
    CHECK(peak(jack) == 0, "OFF MON mutes the FM1 headphone jack");
    fm1_metro_control(1200, 7, 25, 80, 4, 0, 0, 0);
    memset(jack, 0, sizeof jack);
    fm1_output_mix(jack);
    CHECK(peak(jack) == 0, "REC MON does not click while recording is off");

    fm1_metro_control(1200, 7, 25, 80, 0, 0, 0, 0);
    memset(jack, 0, sizeof jack);
    fm1_output_mix(jack);
    CHECK(peak(jack) == peak(recorded) && peak(jack) > 19000, "ordinary mode retains standard USB audio on the jack");
    CHECK(usb_writes > 0, "USB output path remains active");
    printf("%s (%d errors)\n", failures ? "FAILED" : "ALL OK", failures);
    return failures ? 1 : 0;
}
