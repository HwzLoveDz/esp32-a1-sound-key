/* SPDX-FileCopyrightText: 2026 HwzLoveDz
 * SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stddef.h>
#include <stdint.h>

#define SK_MAGIC "A1SK"
#define SK_PROTOCOL_VERSION 1U
#define SK_HEADER_BYTES 20U
#define SK_CRC_BYTES 4U
#define SK_TELEMETRY_BYTES 48U
#define SK_COMMAND_BYTES 12U
#define SK_MAX_PAYLOAD 48U
#define SK_DEFAULT_TARGET_HZ 1000U
#define SK_DEFAULT_HOLD_MS 500U
#define SK_SAMPLE_RATE_HZ 16000U
#define SK_TELEMETRY_INTERVAL_MS 50U
#define SK_LINK_STALL_MS 1500U

enum sk_packet_type { SK_PKT_TELEMETRY = 1, SK_PKT_COMMAND = 2 };
enum sk_state { SK_WAITING = 0, SK_MATCHING, SK_UNLOCKED, SK_WRONG, SK_FAULT };
enum sk_backend { SK_BACKEND_HW = 1, SK_BACKEND_SOFTWARE = 2, SK_BACKEND_UNCONFIRMED = 3 };
enum sk_command { SK_CMD_SET_TARGET = 1, SK_CMD_RELOCK = 2 };
enum sk_fault {
    SK_FAULT_BOARD = 1U << 0,
    SK_FAULT_ADC = 1U << 1,
    SK_FAULT_I2C = 1U << 2,
    SK_FAULT_HW_NOT_READY = 1U << 3,
    SK_FAULT_HW_RANGE = 1U << 4,
    SK_FAULT_INPUT_CLIPPED = 1U << 5,
    SK_FAULT_HW_READ_UNAVAILABLE = 1U << 6,
};

/* Byte-encoded, little endian. Header: magic[4], version:u8, type:u8,
 * flags:u16 (0), sequence:u32, timestamp_ms:u32, payload_len:u16, reserved:u16 (0).
 * Trailer: IEEE CRC32 over header and payload, excluding trailer.
 * Telemetry offsets:
 * 0 target_hz:u32, 4 sample_rate:u32, 8 hold_ms:u16, 10 held_ms:u16,
 * 12 strength_permille:u16, 14 state:u8, 15 backend:u8,
 * 16 unlock_count:u32, 20 tone_power:u32, 24 total_power:u32 (hardware raw),
 * 28 rms:u16, 30 coefficient:u16, 32 window_ms:u32,
 * 36 ack_id:u32, 40 fault_flags:u32, 44 measurement_count:u32 (PCM blocks).
 * SOFTWARE backend: strength is PCM tone-energy fraction, RMS is centered
 * PCM counts, and window_ms is the software PCM window (not the unknown HW
 * window). Hardware coefficient/power remain independent uncalibrated raw.
 * Command: 0 command_id:u32, 4 opcode:u8, 5..7 reserved (0), 8 argument:u32.
 * Device acknowledges applied command IDs in telemetry. Set target accepts
 * only 500, 1000, 2000 Hz. Relock resets the detector, retaining unlock_count.
 */
static inline uint16_t sk_get_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static inline uint32_t sk_get_u32(const uint8_t *p) {
    return (uint32_t)sk_get_u16(p) | ((uint32_t)sk_get_u16(p + 2) << 16);
}
static inline void sk_put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
}
static inline void sk_put_u32(uint8_t *p, uint32_t v) {
    sk_put_u16(p, (uint16_t)v); sk_put_u16(p + 2, (uint16_t)(v >> 16));
}
static inline uint32_t sk_crc32(const uint8_t *p, size_t n) {
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < n; ++i) {
        crc ^= p[i];
        for (unsigned b = 0; b < 8; ++b)
            crc = (crc >> 1) ^ ((crc & 1U) ? UINT32_C(0xedb88320) : 0U);
    }
    return ~crc;
}
static inline int sk_target_valid(uint32_t hz) {
    return hz == 500U || hz == 1000U || hz == 2000U;
}
