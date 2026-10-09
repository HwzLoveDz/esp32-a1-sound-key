/* SPDX-FileCopyrightText: 2026 HwzLoveDz
 * SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sound_key_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SOUND_KEY_RMS_FLOOR
#define SOUND_KEY_RMS_FLOOR 180U
#endif
#ifndef SOUND_KEY_ENTER_PERMILLE
#define SOUND_KEY_ENTER_PERMILLE 700U
#endif
#ifndef SOUND_KEY_EXIT_PERMILLE
#define SOUND_KEY_EXIT_PERMILLE 550U
#endif
#ifndef SOUND_KEY_MAX_GAP_MS
#define SOUND_KEY_MAX_GAP_MS 100U
#endif

typedef struct sound_key_detector_t {
    uint32_t sample_rate;
    uint32_t target_hz;
    uint16_t hold_ms;
    uint16_t held_ms;
    uint16_t strength_permille;
    uint16_t rms;
    enum sk_state state;
    uint32_t unlock_count;
    bool clipped;
    uint32_t last_block_ms;
    bool hit_active;
    /* Fractional milliseconds expressed in units of 1 / sample_rate.
     * This keeps hold time tied to samples when a block is not an exact ms. */
    uint32_t held_time_remainder;
} sound_key_detector_t;

/* Invalid rates/targets produce SK_FAULT. A zero hold selects the default. */
void sound_key_detector_init(sound_key_detector_t *detector,
                             uint32_t rate, uint32_t target, uint16_t hold);

/* Accepts only 500, 1000 or 2000 Hz, below Nyquist. On success, relocks and
 * clears measurements and hold time while retaining unlock_count. */
bool sound_key_detector_set_target(sound_key_detector_t *detector, uint32_t hz);
void sound_key_detector_relock(sound_key_detector_t *detector);

/* Processes signed mono PCM. Intended input is 512 samples at 16 kHz.
 * now_ms is a monotonic, wrapping uint32_t timestamp. A gap over 100 ms
 * breaks an unfinished match. Hold time accumulates from accepted PCM,
 * including the first block, rather than elapsed CPU time.
 * SK_UNLOCKED remains latched until relock or a target change. */
void sound_key_detector_process(sound_key_detector_t *detector,
                                const int16_t *samples, size_t count,
                                uint32_t now_ms);

#ifdef __cplusplus
}
#endif
