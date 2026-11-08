/* Four valid quadrature transitions make one detent. Preserve partial movement
 * during idle, reject diagonal glitches, never invent a turn on stationary input. */
#pragma once
#include <stdint.h>
static inline int fm1_encoder_decode(uint8_t *previous, int8_t *partial,
                                      unsigned phase, const int8_t quadrature[16])
{
    if (*previous > 3U) {
        *previous = (uint8_t)phase;
        *partial = 0;
        return 0;
    }
    if (phase == *previous) return 0;
    unsigned old = *previous;
    *previous = (uint8_t)phase;
    if ((old ^ phase) == 3U) { *partial = 0; return 0; }
    int sum = *partial + quadrature[old * 4U + phase];
    if (sum >= 4) { *partial = (int8_t)(sum - 4); return 1; }
    if (sum <= -4) { *partial = (int8_t)(sum + 4); return -1; }
    *partial = (int8_t)sum;
    return 0;
}
