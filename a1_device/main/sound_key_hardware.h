/* SPDX-FileCopyrightText: 2026 HwzLoveDz
 * SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* Experimental raw register diagnostics, never an unlock decision source.
 * Preserve the SDK's existing coefficient; the Q format and input Fs are
 * not specified consistently enough to derive a validated frequency. */
typedef struct {
    uint16_t coefficient;
    uint32_t tone_power;
    uint32_t total_power;
    uint32_t stable_reads;
    uint8_t control;
    bool available;
    bool saturated;
} sound_key_hardware_t;

esp_err_t sound_key_hardware_start(sound_key_hardware_t *result);
void sound_key_hardware_poll(sound_key_hardware_t *result, uint32_t now_ms);
