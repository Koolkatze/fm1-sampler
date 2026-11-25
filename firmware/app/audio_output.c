/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception */
#include "audio_output.h"
#include "fm1_engine_control.h"
#include "fm1_audio_io.h"

static unsigned output_gain;
static int32_t usb_pcm[FM1_BLOCK_WORDS] __attribute__((aligned(32)));
static int16_t bluetooth_pcm[FM1_BLOCK_WORDS] __attribute__((aligned(32)));

static inline __attribute__((always_inline)) int32_t limit(int64_t value)
{
    /* Bias signed 24-bit PCM into [0, 2^24). Unsigned arithmetic also
     * defines the check at the int64 limits, without two wide compares. */
    if (((uint64_t)value + 8388608U) >> 24)
        return value < 0 ? -8388608 : 8388607;
    return (int32_t)value;
}

/*
 * Click goes to the physical FM-1 DAC/headphone jack only. In particular it is
 * added AFTER fm1_usb_audio_write(), so USB capture and streamed phone audio
 * can never contain it. All state is fixed-point; no heap or libm in the ISR.
 *
 * Control task updates these 32-bit words; audio ISR reads one snapshot/block.
 * Periodic host phase updates slowly trim clock drift, not audible click timing.
 */
static volatile uint32_t metro_bpm10 = 1200;
static volatile uint32_t metro_settings; /* beats=0 until first valid command */
static volatile uint32_t metro_beatfrac, metro_sequence;
static uint64_t metro_position, metro_step, metro_length;
static int64_t metro_trim;
static uint32_t metro_seen;
static uint32_t metro_osc;
static int32_t metro_envelope;
static unsigned metro_fired, metro_started;

void fm1_metro_control(unsigned bpm10, unsigned beats, unsigned swing,
                       unsigned volume, unsigned mode, unsigned beat,
                       unsigned fraction14, unsigned recording)
{
    if (bpm10 < 10U) bpm10 = 10U;
    if (bpm10 > 16383U) bpm10 = 16383U;
    if (beats < 1U) beats = 1U;
    if (beats > 64U) beats = 64U;
    if (swing > 100U) swing = 100U;
    if (volume > 100U) volume = 100U;
    if (mode > 5U) mode = 2U;
    if (beat >= beats) beat = 0U;
    if (fraction14 > 16383U) fraction14 = 16383U;
    metro_bpm10 = bpm10;
    metro_settings = (beats & 127U) | (swing << 7) |
                     (volume << 14) | (mode << 21) | ((recording != 0U) << 24);
    metro_beatfrac = (beat << 14) | fraction14;
    ++metro_sequence;
}

FM1_AUDIO_CODE static void metro_jack_mix(int32_t *output, unsigned gain)
{
    const uint32_t config = metro_settings;
    const unsigned beats = config & 127U;
    if (beats < 1U || beats > 64U) return;
    const unsigned swing = (config >> 7) & 127U;
    const unsigned volume = (config >> 14) & 127U;
    const unsigned mode = (config >> 21) & 7U;
    const unsigned recording = (config >> 24) & 1U;
    const int audible = (mode == 3U || (mode == 4U && recording)) && volume > 0U;
    /* In MON modes this DAC is a private cue output: suppress USB music,
       Bluetooth and local synth while leaving USB capture unchanged. */
    if (mode >= 3U && mode <= 5U)
        for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) output[i] = 0;
    const uint32_t step = (uint32_t)(((uint64_t)metro_bpm10 << 32) /
                        (600ULL * FM1_SAMPLE_RATE));
    metro_step = step ? step : 1U;
    metro_length = (uint64_t)beats << 32;
    if (metro_sequence != metro_seen) {
        metro_seen = metro_sequence;
        const uint32_t phase = metro_beatfrac;
        uint64_t target = ((uint64_t)(phase >> 14) << 32) |
                          ((uint64_t)(phase & 16383U) << 18);
        if (target >= metro_length) target = 0U;
        /* Packet timestamp is the phone audio-thread phase sampled before
         * USB delivery. Do not hard reset for ordinary USB scheduling jitter. */
        if (!metro_started) {
            metro_position = target;
            metro_started = 1U;
            metro_trim = 0;
            metro_fired = 1U; /* avoid a spurious hit on initial attach */
        } else {
            int64_t error = (int64_t)target - (int64_t)metro_position;
            const int64_t half = (int64_t)(metro_length / 2);
            if (error > half) error -= (int64_t)metro_length;
            if (error < -half) error += (int64_t)metro_length;
            if (error > (int64_t)(1ULL << 31) || error < -(int64_t)(1ULL << 31)) {
                /* Transport jumped (restart/scene load), not mere drift. */
                metro_position = target;
                metro_trim = 0;
                metro_fired = 1U;
            } else {
                /* Bounded +/- 1% slew over roughly 0.5 seconds. */
                int64_t correction = error / (FM1_SAMPLE_RATE / 2);
                const int64_t maxTrim = (int64_t)metro_step / 100;
                if (correction > maxTrim) correction = maxTrim;
                if (correction < -maxTrim) correction = -maxTrim;
                metro_trim = correction;
            }
        }
    }
    for (unsigned i = 0; i < FM1_BLOCK_FRAMES; ++i) {
        const unsigned index = (unsigned)(metro_position >> 32);
        const uint32_t fraction = (uint32_t)metro_position;
        const uint32_t onset = (index & 1U) ? (uint32_t)(((uint64_t)swing << 31) / 100U) : 0U;
        if (!metro_fired && fraction >= onset) {
            metro_fired = 1U;
            metro_osc = 0U;
            metro_envelope = (int32_t)(((uint64_t)volume * (index == 0U ? 2600000U : 1800000U)) / 100U);
        }
        if (audible && metro_envelope > 64) {
            /* Band-limited enough for a cue, fast triangular oscillator
             * with exponential envelope; 1.6 kHz accented / 1 kHz other. */
            metro_osc += (index == 0U ? 155828844U : 97391528U); /* 1600/1000 Hz @44.1k */
            const int v = (int)((metro_osc >> 20) & 4095U);
            const int wave = v < 2048 ? v * 2 - 2048 : 6144 - v * 2;
            const int64_t tick = ((int64_t)wave * metro_envelope * gain) >> 18;
            output[2 * i] = limit((int64_t)output[2 * i] + tick);
            output[2 * i + 1] = limit((int64_t)output[2 * i + 1] + tick);
        }
        metro_envelope -= metro_envelope >> 8; /* ~5.8 ms decay at 44.1 kHz */
        const unsigned was = (unsigned)(metro_position >> 32);
        const int64_t increment = (int64_t)metro_step + metro_trim;
        metro_position += (uint64_t)(increment > 0 ? increment : 1);
        if (metro_position >= metro_length) metro_position -= metro_length;
        if (was != (unsigned)(metro_position >> 32)) metro_fired = 0U;
    }
}
void fm1_output_reset(void) { output_gain = 0; metro_position = 0U; metro_started = 0U; metro_fired = 0U; metro_envelope = 0; }
FM1_AUDIO_CODE void fm1_output_mix(int32_t *output)
{
    if (!output) return;
    unsigned target = fm1_engine_output_level();
    if (target > 127U) target = 127U;
    int delta = (int)target - (int)output_gain;
    output_gain = (unsigned)((int)output_gain + (delta > 8 ? 8 : delta < -8 ? -8 : delta));

    uint16_t usb_gain[2] = {0, 0};
    int usb_frames = fm1_usb_audio_read(usb_pcm, FM1_BLOCK_FRAMES, usb_gain);
    unsigned left = ((unsigned)usb_gain[0] * output_gain) >> 7;
    unsigned right = ((unsigned)usb_gain[1] * output_gain) >> 7;
    /* Accumulate USB before clipping, then add Bluetooth.
     * Discard the block if the USB stream resets. */
    for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) {
        int64_t value = ((int64_t)output[i] * output_gain) >> 7;
        if (usb_frames == FM1_BLOCK_FRAMES)
            value += ((int64_t)usb_pcm[i] * ((i & 1U) ? right : left)) >> 7;
        output[i] = usb_frames == -2 ? 0 : limit(value);
    }
    if (fm1_bluetooth_audio_read(bluetooth_pcm, FM1_BLOCK_FRAMES) == FM1_BLOCK_FRAMES) {
        /* output is already clipped to 24 bits. Adding signed 16-bit PCM
         * times at most 254 fits in 25 bits, so this stage needs no int64. */
        for (unsigned i = 0; i < FM1_BLOCK_WORDS; ++i) {
            int32_t value = output[i] + (int32_t)bluetooth_pcm[i] * (int32_t)(output_gain * 2U);
            output[i] = value > 8388607 ? 8388607 : value < -8388608 ? -8388608 : value;
        }
    }
    (void)fm1_usb_audio_write(output, FM1_BLOCK_FRAMES);
    /* Never route click into USB recording, Bluetooth or live PA stream. */
    metro_jack_mix(output, output_gain);
}
