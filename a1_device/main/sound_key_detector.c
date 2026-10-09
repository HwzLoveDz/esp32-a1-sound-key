/* SPDX-FileCopyrightText: 2026 HwzLoveDz
 * SPDX-License-Identifier: Apache-2.0 */
#include "sound_key_detector.h"

#include <math.h>
#include <string.h>

#define SOUND_KEY_PI 3.14159265358979323846

static bool configuration_valid(const sound_key_detector_t *detector)
{
    return detector->sample_rate != 0U &&
           sk_target_valid(detector->target_hz) &&
           (uint64_t)detector->target_hz * 2U < detector->sample_rate;
}

static void clear_hold(sound_key_detector_t *detector)
{
    detector->held_ms = 0U;
    detector->held_time_remainder = 0U;
    detector->hit_active = false;
}

void sound_key_detector_relock(sound_key_detector_t *detector)
{
    if (detector == NULL) {
        return;
    }
    clear_hold(detector);
    detector->strength_permille = 0U;
    detector->rms = 0U;
    detector->clipped = false;
    detector->last_block_ms = 0U;
    detector->state = configuration_valid(detector) ? SK_WAITING : SK_FAULT;
}

void sound_key_detector_init(sound_key_detector_t *detector,
                             uint32_t rate, uint32_t target, uint16_t hold)
{
    if (detector == NULL) {
        return;
    }
    memset(detector, 0, sizeof(*detector));
    detector->sample_rate = rate;
    detector->target_hz = target;
    detector->hold_ms = hold != 0U ? hold : SK_DEFAULT_HOLD_MS;
    sound_key_detector_relock(detector);
}

bool sound_key_detector_set_target(sound_key_detector_t *detector, uint32_t hz)
{
    if (detector == NULL || detector->sample_rate == 0U ||
        !sk_target_valid(hz) || (uint64_t)hz * 2U >= detector->sample_rate) {
        return false;
    }
    detector->target_hz = hz;
    sound_key_detector_relock(detector);
    return true;
}

static void add_sample_time(sound_key_detector_t *detector, size_t count)
{
    const uint64_t numerator = (uint64_t)count * 1000U +
                               detector->held_time_remainder;
    const uint64_t elapsed_ms = numerator / detector->sample_rate;
    const uint64_t held_ms = (uint64_t)detector->held_ms + elapsed_ms;
    detector->held_time_remainder = (uint32_t)(numerator % detector->sample_rate);

    if (held_ms >= detector->hold_ms) {
        detector->held_ms = detector->hold_ms;
        detector->held_time_remainder = 0U;
        detector->state = SK_UNLOCKED;
        if (detector->unlock_count != UINT32_MAX) {
            ++detector->unlock_count;
        }
    } else {
        detector->held_ms = (uint16_t)held_ms;
        detector->state = SK_MATCHING;
    }
}

void sound_key_detector_process(sound_key_detector_t *detector,
                                const int16_t *samples, size_t count,
                                uint32_t now_ms)
{
    if (detector == NULL) {
        return;
    }
    if (!configuration_valid(detector)) {
        clear_hold(detector);
        detector->state = SK_FAULT;
        return;
    }

    const bool unlocked = detector->state == SK_UNLOCKED;
    if (!unlocked && detector->hit_active &&
        (uint32_t)(now_ms - detector->last_block_ms) > SOUND_KEY_MAX_GAP_MS) {
        clear_hold(detector);
    }
    detector->last_block_ms = now_ms;

    if (samples == NULL || count == 0U) {
        detector->strength_permille = 0U;
        detector->rms = 0U;
        detector->clipped = false;
        detector->hit_active = false;
        if (!unlocked) {
            clear_hold(detector);
            detector->state = SK_WAITING;
        }
        return;
    }

    double sum = 0.0;
    size_t clip_count = 0U;
    for (size_t i = 0; i < count; ++i) {
        const int32_t sample = samples[i];
        sum += sample;
        if (sample >= 32760 || sample <= -32760) {
            ++clip_count;
        }
    }
    const double mean = sum / (double)count;
    const double coefficient = 2.0 * cos(2.0 * SOUND_KEY_PI *
                                         detector->target_hz / detector->sample_rate);
    double q1 = 0.0;
    double q2 = 0.0;
    double sum2 = 0.0;
    for (size_t i = 0; i < count; ++i) {
        const double centered = (double)samples[i] - mean;
        const double q0 = centered + coefficient * q1 - q2;
        q2 = q1;
        q1 = q0;
        sum2 += centered * centered;
    }

    double power = q1 * q1 + q2 * q2 - coefficient * q1 * q2;
    if (power < 0.0) {
        power = 0.0;
    }
    double strength = sum2 > 0.0 ? 2.0 * power / ((double)count * sum2) : 0.0;
    if (!isfinite(strength) || strength < 0.0) {
        strength = 0.0;
    } else if (strength > 1.0) {
        strength = 1.0;
    }
    const double rms = sqrt(sum2 / (double)count);
    detector->strength_permille = (uint16_t)(strength * 1000.0 + 0.5);
    detector->rms = (uint16_t)(rms > UINT16_MAX ? UINT16_MAX : rms + 0.5);
    /* clip_count > floor(count / 100) is exactly more than 1 percent,
     * without multiplying size_t and risking integer overflow. */
    detector->clipped = clip_count > count / 100U;

    const uint16_t threshold = detector->hit_active ? SOUND_KEY_EXIT_PERMILLE
                                                    : SOUND_KEY_ENTER_PERMILLE;
    const bool audible = detector->rms >= SOUND_KEY_RMS_FLOOR;
    const bool hit = audible && !detector->clipped &&
                     detector->strength_permille >= threshold;
    detector->hit_active = hit;

    if (unlocked) {
        return;
    }
    if (!hit) {
        clear_hold(detector);
        detector->state = audible || detector->clipped ? SK_WRONG : SK_WAITING;
        return;
    }
    add_sample_time(detector, count);
}
