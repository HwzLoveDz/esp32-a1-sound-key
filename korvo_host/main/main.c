/* SPDX-FileCopyrightText: 2026 HwzLoveDz
 * SPDX-License-Identifier: Apache-2.0 */
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sound_key_link.h"
#include "sound_key_ui.h"

static void uart_status_task(void *arg)
{
    (void)arg;
    for (;;) {
        sound_key_snapshot_t snapshot;
        sound_key_link_get_snapshot(&snapshot);
        const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        const int32_t age_ms = snapshot.valid ?
                               (int32_t)(now - snapshot.last_packet_ms) : -1;
        /* get_snapshot releases its lock before UART logging. This low-priority
         * task never sends commands or blocks the USB/LVGL owner tasks. */
        ESP_LOGI("SOUND_KEY_STATUS",
                 "connected=%u valid=%u pending=%u target=%lu state=%u backend=%u "
                 "count=%lu measurement=%lu ack=%lu rms=%u strength=%u held=%u/%u "
                 "flags=0x%08lx rx=%lu crc=%lu header=%lu tx=%lu gaps=%lu age_ms=%ld",
                 (unsigned)snapshot.connected, (unsigned)snapshot.valid,
                 (unsigned)snapshot.command_pending, (unsigned long)snapshot.target_hz,
                 (unsigned)snapshot.state, (unsigned)snapshot.backend,
                 (unsigned long)snapshot.unlock_count,
                 (unsigned long)snapshot.measurement_count, (unsigned long)snapshot.ack_id,
                 (unsigned)snapshot.rms, (unsigned)snapshot.strength_permille,
                 (unsigned)snapshot.held_ms, (unsigned)snapshot.hold_ms,
                 (unsigned long)snapshot.fault_flags, (unsigned long)snapshot.usb_rx_bytes,
                 (unsigned long)snapshot.crc_errors,
                 (unsigned long)snapshot.header_errors, (unsigned long)snapshot.tx_errors,
                 (unsigned long)snapshot.packet_gaps, (long)age_ms);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    esp_err_t ret = sound_key_ui_start();
    if (ret != ESP_OK) {
        ESP_LOGE("SOUND_KEY", "LCD/touch init failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = sound_key_link_start();
    if (ret != ESP_OK) {
        ESP_LOGE("SOUND_KEY", "USB host init failed: %s", esp_err_to_name(ret));
        return;
    }
    if (xTaskCreate(uart_status_task, "sk_uart_status", 4096, NULL, 1, NULL) != pdPASS) {
        ESP_LOGE("SOUND_KEY", "UART status task creation failed: out of memory");
    }
}
