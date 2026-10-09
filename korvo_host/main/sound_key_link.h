/* SPDX-FileCopyrightText: 2026 HwzLoveDz
 * SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "sound_key_protocol.h"

typedef struct {
    bool connected, valid, command_pending;
    uint8_t state, backend;
    uint32_t target_hz, sample_rate;
    uint16_t hold_ms, held_ms, strength_permille, rms, coefficient;
    uint32_t unlock_count, tone_power, total_power, window_ms;
    uint32_t ack_id, fault_flags, measurement_count, last_packet_ms;
    uint32_t usb_connections, usb_rx_bytes, packet_gaps, crc_errors, header_errors, tx_errors;
} sound_key_snapshot_t;

esp_err_t sound_key_link_start(void);
void sound_key_link_get_snapshot(sound_key_snapshot_t *out);
esp_err_t sound_key_link_set_target(uint32_t hz);
esp_err_t sound_key_link_relock(void);
