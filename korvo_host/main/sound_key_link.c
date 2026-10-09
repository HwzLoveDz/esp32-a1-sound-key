/* SPDX-FileCopyrightText: 2026 HwzLoveDz
 * SPDX-License-Identifier: Apache-2.0 */
/* ESP32-S31-Korvo-1 owner task for the A1SK USB Serial/JTAG link. */
#include "sound_key_link.h"

#include <stddef.h>
#include <string.h>

#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "usb/cdc_acm_host.h"
#include "usb/usb_host.h"

#define A1_USB_VID              0x303A
#define A1_USB_PID              0x1001
#define A1_USB_INTERFACE        0
#define RX_BUFFER_BYTES        (4U * (SK_HEADER_BYTES + SK_MAX_PAYLOAD + SK_CRC_BYTES))
#define COMMAND_QUEUE_DEPTH    4U
#define COMMAND_RETRY_MS        300U
#define COMMAND_SLOW_RETRY_MS   1000U
#define COMMAND_TX_TIMEOUT_MS   100U
#define COMMAND_DEADLINE_MS     3000U
#define RECOVERY_BACKOFF_MS     3000U
#define TRANSPORT_BACKOFF_MS    1000U
#define CLOSE_RETRY_COUNT       3U
#define CLOSE_RETRY_MS          200U

typedef struct {
    uint32_t generation;
    uint32_t argument;
    uint8_t opcode;
} command_request_t;

typedef struct {
    command_request_t request;
    uint32_t id;
    uint32_t started_ms;
    uint32_t sent_ms;
    uint32_t minimum_rx_count;
    uint32_t attempts;
    bool active;
    bool sent;
} pending_command_t;

static const char *TAG = "sound_key_link";
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static sound_key_snapshot_t s_state = {
    .state = SK_WAITING,
    .backend = SK_BACKEND_UNCONFIRMED,
    .target_hz = SK_DEFAULT_TARGET_HZ,
    .sample_rate = SK_SAMPLE_RATE_HZ,
    .hold_ms = SK_DEFAULT_HOLD_MS,
};
static QueueHandle_t s_commands;
static TaskHandle_t s_worker_task;
static bool s_started;
static bool s_starting;
static bool s_disconnected;
static bool s_recovery_requested;
static bool s_transport_reopen_requested;
static bool s_link_stalled;
static bool s_barrier_complete;
static bool s_sync_active;
static bool s_command_failed;
static uint32_t s_command_failed_ms;
static uint32_t s_generation;
static uint32_t s_pending_requests;
static uint32_t s_desired_target = SK_DEFAULT_TARGET_HZ;
static uint32_t s_received_packets;
static uint32_t s_connection_ms;
static uint8_t s_latest_state = SK_WAITING;
static uint16_t s_latest_held;
static uint16_t s_latest_strength;

/* Only the CDC driver's data callback touches the byte parser while open.
 * The owner resets it before open and after close cancels all transfers. */
static uint8_t s_rx[RX_BUFFER_BYTES];
static size_t s_rx_len;
static bool s_have_sequence;
static uint32_t s_expected_sequence;
static uint32_t s_device_timestamp;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void refresh_display_locked(void)
{
    /* Every freshness comparison samples time while holding s_lock, so a CDC
     * callback cannot publish a newer last_packet_ms after our time sample. */
    uint32_t now = now_ms();
    s_state.command_pending = s_state.connected && !s_transport_reopen_requested &&
                              (s_sync_active || s_pending_requests != 0U);
    s_state.valid = s_state.connected && !s_disconnected &&
                    !s_recovery_requested && !s_transport_reopen_requested && !s_link_stalled &&
                    !s_command_failed && s_barrier_complete && s_pending_requests == 0U &&
                    s_received_packets != 0U &&
                    (uint32_t)(now - s_state.last_packet_ms) < SK_LINK_STALL_MS;
    /* Never expose a cached UNLOCKED state during a command or recovery. */
    s_state.state = s_state.valid ? s_latest_state :
                    (s_state.connected && (s_command_failed || s_latest_state == SK_FAULT) ?
                     SK_FAULT : SK_WAITING);
    s_state.held_ms = s_state.valid ? s_latest_held : 0U;
    s_state.strength_permille = s_state.valid ? s_latest_strength : 0U;
}

static void invalidate_display_locked(void)
{
    s_state.valid = false;
    s_state.state = SK_WAITING;
    s_state.held_ms = 0U;
    s_state.strength_permille = 0U;
    s_state.ack_id = 0U;
    s_latest_state = SK_WAITING;
    s_latest_held = 0U;
    s_latest_strength = 0U;
}

static void parser_reset(void)
{
    s_rx_len = 0U;
    s_have_sequence = false;
    s_expected_sequence = 0U;
    s_device_timestamp = 0U;
}

static void parser_drop(size_t count)
{
    if (count >= s_rx_len) {
        s_rx_len = 0U;
    } else {
        memmove(s_rx, s_rx + count, s_rx_len - count);
        s_rx_len -= count;
    }
}

static bool telemetry_fields_valid(const uint8_t *p)
{
    return sk_target_valid(sk_get_u32(p)) &&
           sk_get_u32(p + 4) == SK_SAMPLE_RATE_HZ &&
           sk_get_u16(p + 8) != 0U &&
           sk_get_u16(p + 12) <= 1000U &&
           p[14] <= SK_FAULT &&
           p[15] >= SK_BACKEND_HW && p[15] <= SK_BACKEND_UNCONFIRMED;
}

static void handle_telemetry(uint32_t sequence, uint32_t timestamp, const uint8_t *p)
{
    bool notify = false;
    taskENTER_CRITICAL(&s_lock);
    uint32_t now = now_ms();
    if (!s_state.connected || s_disconnected || s_transport_reopen_requested) {
        taskEXIT_CRITICAL(&s_lock);
        return;
    }
    if (s_have_sequence && sequence == s_expected_sequence - 1U &&
        timestamp == s_device_timestamp) {
        /* A duplicate must not refresh the watchdog or acknowledge a new command. */
        taskEXIT_CRITICAL(&s_lock);
        return;
    }
    bool restarted = s_have_sequence &&
                     ((int32_t)(sequence - s_expected_sequence) < 0 ||
                      (int32_t)(timestamp - s_device_timestamp) < 0);
    if (!restarted && s_have_sequence && sequence != s_expected_sequence) {
        s_state.packet_gaps += sequence - s_expected_sequence;
    }
    s_have_sequence = true;
    s_expected_sequence = sequence + 1U;
    s_device_timestamp = timestamp;
    ++s_received_packets;
    s_state.last_packet_ms = now;
    s_link_stalled = false;
    if (restarted) {
        /* The wire format has no boot ID. A backward sequence/timestamp is
         * treated conservatively as a new boot and requires fresh commands. */
        s_recovery_requested = true;
        s_barrier_complete = false;
        s_sync_active = false;
        invalidate_display_locked();
        refresh_display_locked();
        notify = true;
    } else {
        s_state.target_hz = sk_get_u32(p);
        s_state.sample_rate = sk_get_u32(p + 4);
        s_state.hold_ms = sk_get_u16(p + 8);
        s_latest_held = sk_get_u16(p + 10);
        s_latest_strength = sk_get_u16(p + 12);
        s_latest_state = p[14];
        s_state.backend = p[15];
        s_state.unlock_count = sk_get_u32(p + 16);
        s_state.tone_power = sk_get_u32(p + 20);
        s_state.total_power = sk_get_u32(p + 24);
        s_state.rms = sk_get_u16(p + 28);
        s_state.coefficient = sk_get_u16(p + 30);
        s_state.window_ms = sk_get_u32(p + 32);
        s_state.ack_id = sk_get_u32(p + 36);
        s_state.fault_flags = sk_get_u32(p + 40);
        s_state.measurement_count = sk_get_u32(p + 44);
        refresh_display_locked();
        notify = s_state.command_pending;
    }
    taskEXIT_CRITICAL(&s_lock);
    if (notify && s_worker_task != NULL) xTaskNotifyGive(s_worker_task);
}

static void parser_process(void)
{
    for (;;) {
        if (s_rx_len < 4U) return;
        size_t start = 0U;
        while (start + 4U <= s_rx_len && memcmp(s_rx + start, SK_MAGIC, 4U) != 0) {
            ++start;
        }
        if (start != 0U) {
            /* Retain a split magic's final three bytes between USB callbacks. */
            if (start + 4U > s_rx_len) start = s_rx_len - 3U;
            parser_drop(start);
            if (s_rx_len < 4U) return;
        }
        if (s_rx_len < SK_HEADER_BYTES) return;
        uint16_t payload_len = sk_get_u16(s_rx + 16);
        if (s_rx[4] != SK_PROTOCOL_VERSION || s_rx[5] != SK_PKT_TELEMETRY ||
            sk_get_u16(s_rx + 6) != 0U || sk_get_u16(s_rx + 18) != 0U ||
            payload_len != SK_TELEMETRY_BYTES) {
            taskENTER_CRITICAL(&s_lock);
            ++s_state.header_errors;
            taskEXIT_CRITICAL(&s_lock);
            parser_drop(1U);
            continue;
        }
        size_t packet_len = SK_HEADER_BYTES + (size_t)payload_len + SK_CRC_BYTES;
        if (s_rx_len < packet_len) return;
        if (sk_crc32(s_rx, packet_len - SK_CRC_BYTES) !=
            sk_get_u32(s_rx + packet_len - SK_CRC_BYTES)) {
            taskENTER_CRITICAL(&s_lock);
            ++s_state.crc_errors;
            taskEXIT_CRITICAL(&s_lock);
            parser_drop(1U);
            continue;
        }
        if (!telemetry_fields_valid(s_rx + SK_HEADER_BYTES)) {
            taskENTER_CRITICAL(&s_lock);
            ++s_state.header_errors;
            taskEXIT_CRITICAL(&s_lock);
        } else {
            handle_telemetry(sk_get_u32(s_rx + 8), sk_get_u32(s_rx + 12),
                             s_rx + SK_HEADER_BYTES);
        }
        parser_drop(packet_len);
    }
}

static bool on_usb_data(const uint8_t *data, size_t len, void *arg)
{
    (void)arg;
    /* Count raw callbacks independently of frame parsing and ACK validity. */
    taskENTER_CRITICAL(&s_lock);
    s_state.usb_rx_bytes += (uint32_t)len;
    taskEXIT_CRITICAL(&s_lock);
    while (len != 0U) {
        if (s_rx_len == sizeof(s_rx)) {
            parser_process();
            if (s_rx_len == sizeof(s_rx)) parser_drop(1U);
        }
        size_t take = sizeof(s_rx) - s_rx_len;
        if (take > len) take = len;
        memcpy(s_rx + s_rx_len, data, take);
        s_rx_len += take;
        data += take;
        len -= take;
        parser_process();
    }
    return true;
}

static void on_usb_event(const cdc_acm_host_dev_event_data_t *event, void *arg)
{
    (void)arg;
    if (event->type == CDC_ACM_HOST_DEVICE_DISCONNECTED) {
        taskENTER_CRITICAL(&s_lock);
        s_disconnected = true;
        s_state.connected = false;
        s_barrier_complete = false;
        ++s_generation;
        s_pending_requests = 0U;
        invalidate_display_locked();
        refresh_display_locked();
        taskEXIT_CRITICAL(&s_lock);
        if (s_worker_task != NULL) xTaskNotifyGive(s_worker_task);
    } else if (event->type == CDC_ACM_HOST_ERROR) {
        taskENTER_CRITICAL(&s_lock);
        ++s_state.tx_errors;
        s_transport_reopen_requested = true;
        s_barrier_complete = false;
        s_sync_active = false;
        invalidate_display_locked();
        refresh_display_locked();
        taskEXIT_CRITICAL(&s_lock);
        if (s_worker_task != NULL) xTaskNotifyGive(s_worker_task);
    }
}

static void usb_host_events(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t flags = 0U;
        esp_err_t ret = usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "USB Host events: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            (void)usb_host_device_free_all();
        }
    }
}

static uint32_t next_command_id(uint32_t *next)
{
    /* A random initial ID avoids reusing the A1's retained ack after a host
     * reboot. Thereafter IDs remain unique across reconnects in this boot. */
    ++*next;
    if (*next == 0U) ++*next;
    return *next;
}

static void prepare_command(pending_command_t *pending, const command_request_t *request,
                            uint32_t *next_id)
{
    memset(pending, 0, sizeof(*pending));
    pending->request = *request;
    pending->id = next_command_id(next_id);
    pending->started_ms = now_ms();
    pending->active = true;
}

static esp_err_t transmit_command(cdc_acm_dev_hdl_t dev, pending_command_t *pending,
                                  uint32_t *tx_sequence)
{
    uint8_t packet[SK_HEADER_BYTES + SK_COMMAND_BYTES + SK_CRC_BYTES] = {0};
    memcpy(packet, SK_MAGIC, 4U);
    packet[4] = SK_PROTOCOL_VERSION;
    packet[5] = SK_PKT_COMMAND;
    sk_put_u32(packet + 8, (*tx_sequence)++);
    sk_put_u32(packet + 12, now_ms());
    sk_put_u16(packet + 16, SK_COMMAND_BYTES);
    sk_put_u32(packet + SK_HEADER_BYTES, pending->id);
    packet[SK_HEADER_BYTES + 4U] = pending->request.opcode;
    sk_put_u32(packet + SK_HEADER_BYTES + 8U, pending->request.argument);
    sk_put_u32(packet + sizeof(packet) - SK_CRC_BYTES,
                sk_crc32(packet, sizeof(packet) - SK_CRC_BYTES));
    if (!pending->sent) {
        taskENTER_CRITICAL(&s_lock);
        pending->minimum_rx_count = s_received_packets;
        taskEXIT_CRITICAL(&s_lock);
    }
    esp_err_t ret = cdc_acm_host_data_tx_blocking(dev, packet, sizeof(packet),
                                                COMMAND_TX_TIMEOUT_MS);
    pending->sent = true;
    pending->sent_ms = now_ms();
    ++pending->attempts;
    if (ret != ESP_OK) {
        taskENTER_CRITICAL(&s_lock);
        ++s_state.tx_errors;
        taskEXIT_CRITICAL(&s_lock);
        if (pending->attempts == 1U || (pending->attempts % 10U) == 0U) {
            ESP_LOGW(TAG, "Command %lu USB write: %s", (unsigned long)pending->id,
                     esp_err_to_name(ret));
        }
    }
    return ret;
}

static esp_err_t close_device_attempts(cdc_acm_dev_hdl_t dev)
{
    esp_err_t ret = ESP_FAIL;
    for (unsigned attempt = 1U; attempt <= CLOSE_RETRY_COUNT; ++attempt) {
        ret = cdc_acm_host_close(dev);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "USB close attempt %u: %s", attempt, esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGW(TAG, "USB close attempt %u: %s", attempt, esp_err_to_name(ret));
        if (attempt < CLOSE_RETRY_COUNT) vTaskDelay(pdMS_TO_TICKS(CLOSE_RETRY_MS));
    }
    return ret;
}

static void close_device_owner(cdc_acm_dev_hdl_t dev)
{
    /* Only the owner calls close, outside s_lock. Never allocate another
     * handle unless the previous one has been released successfully. */
    if (close_device_attempts(dev) == ESP_OK) return;
    ESP_LOGE(TAG, "USB close failed; link remains invalid until physical disconnect");
    for (;;) {
        taskENTER_CRITICAL(&s_lock);
        bool disconnected = s_disconnected;
        taskEXIT_CRITICAL(&s_lock);
        if (disconnected) break;
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(500));
    }
    if (close_device_attempts(dev) == ESP_OK) return;
    ESP_LOGE(TAG, "USB close still failed after disconnect; owner stopped, reboot required");
    /* Keep the task handle alive for late driver notifications, but perform
     * no more USB operations or opens after an unreleased handle. */
    for (;;) (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

static void usb_link_worker(void *arg)
{
    (void)arg;
    s_worker_task = xTaskGetCurrentTaskHandle();
    uint32_t command_id = esp_random();
    uint32_t tx_sequence = 0U;
    for (;;) {
        parser_reset();
        (void)xQueueReset(s_commands);
        taskENTER_CRITICAL(&s_lock);
        s_disconnected = false;
        s_state.connected = false;
        s_recovery_requested = false;
        s_transport_reopen_requested = false;
        s_barrier_complete = false;
        s_sync_active = false;
        s_command_failed = false;
        s_link_stalled = false;
        s_pending_requests = 0U;
        s_received_packets = 0U;
        s_state.last_packet_ms = 0U;
        invalidate_display_locked();
        refresh_display_locked();
        taskEXIT_CRITICAL(&s_lock);

        cdc_acm_host_device_config_t config = {
            .connection_timeout_ms = 1000,
            .out_buffer_size = 64,
            .in_buffer_size = 512,
            .user_arg = NULL,
            .event_cb = on_usb_event,
            .data_cb = on_usb_data,
        };
        cdc_acm_dev_hdl_t dev = NULL;
        esp_err_t ret = cdc_acm_host_open(A1_USB_VID, A1_USB_PID, A1_USB_INTERFACE,
                                         &config, &dev);
        if (ret != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        }
        taskENTER_CRITICAL(&s_lock);
        bool disconnected_during_open = s_disconnected;
        bool transport_failed_during_open = s_transport_reopen_requested;
        uint32_t generation = ++s_generation;
        if (!disconnected_during_open && !transport_failed_during_open) {
            s_transport_reopen_requested = false;
            s_state.connected = true;
            s_sync_active = true;
            s_connection_ms = now_ms();
            ++s_state.usb_connections;
        }
        refresh_display_locked();
        taskEXIT_CRITICAL(&s_lock);
        if (disconnected_during_open || transport_failed_during_open) {
            close_device_owner(dev);
            parser_reset();
            if (!disconnected_during_open) vTaskDelay(pdMS_TO_TICKS(TRANSPORT_BACKOFF_MS));
            continue;
        }
        ESP_LOGI(TAG, "A1 connected: USB Serial/JTAG 303A:1001 interface 0");
        /* Deliberately omit line coding and DTR/RTS requests: Espressif's
         * control-line sequences can reset the board or enter download mode. */
        unsigned barrier_stage = 0U; /* RELOCK, SET_TARGET, then ready. */
        pending_command_t pending = {0};
        bool reopen_transport = false;
        for (;;) {
            taskENTER_CRITICAL(&s_lock);
            uint32_t now = now_ms();
            bool disconnected = s_disconnected;
            if (!disconnected && !s_transport_reopen_requested && s_command_failed &&
                (uint32_t)(now - s_command_failed_ms) >= RECOVERY_BACKOFF_MS) {
                s_recovery_requested = true;
            }
            uint32_t last_rx = s_received_packets != 0U ?
                               s_state.last_packet_ms : s_connection_ms;
            if (!disconnected && !s_transport_reopen_requested && !s_link_stalled &&
                (uint32_t)(now - last_rx) >= SK_LINK_STALL_MS) {
                s_link_stalled = true;
                s_transport_reopen_requested = true;
                s_barrier_complete = false;
                s_sync_active = false;
                invalidate_display_locked();
            }
            reopen_transport = !disconnected && s_transport_reopen_requested;
            bool recover = !disconnected && !reopen_transport && s_recovery_requested;
            if (recover) {
                s_recovery_requested = false;
                s_barrier_complete = false;
                s_sync_active = true;
                s_command_failed = false;
                s_pending_requests = 0U;
                generation = ++s_generation;
                invalidate_display_locked();
            }
            refresh_display_locked();
            taskEXIT_CRITICAL(&s_lock);
            if (disconnected || reopen_transport) break;
            if (recover) {
                (void)xQueueReset(s_commands);
                pending.active = false;
                barrier_stage = 0U;
                ESP_LOGW(TAG, "Application sync restarted; requiring fresh relock and target ack");
            }

            if (pending.active && pending.sent) {
                taskENTER_CRITICAL(&s_lock);
                now = now_ms();
                bool acked = !s_disconnected && !s_recovery_requested &&
                             !s_transport_reopen_requested &&
                             pending.request.generation == s_generation &&
                             s_received_packets != pending.minimum_rx_count &&
                             s_state.ack_id == pending.id &&
                             (uint32_t)(now - s_state.last_packet_ms) < SK_LINK_STALL_MS &&
                             (pending.request.opcode != SK_CMD_SET_TARGET ||
                              s_state.target_hz == pending.request.argument);
                if (acked) {
                    if (barrier_stage < 2U) {
                        ++barrier_stage;
                        if (barrier_stage == 2U) {
                            s_barrier_complete = true;
                            s_sync_active = false;
                        }
                    } else if (s_pending_requests != 0U) {
                        --s_pending_requests;
                    }
                    refresh_display_locked();
                }
                taskEXIT_CRITICAL(&s_lock);
                if (acked) pending.active = false;
            }

            if (pending.active && (uint32_t)(now - pending.started_ms) >= COMMAND_DEADLINE_MS) {
                taskENTER_CRITICAL(&s_lock);
                s_command_failed = true;
                s_command_failed_ms = now;
                s_sync_active = false;
                s_barrier_complete = false;
                s_pending_requests = 0U;
                generation = ++s_generation;
                ++s_state.tx_errors;
                invalidate_display_locked();
                refresh_display_locked();
                taskEXIT_CRITICAL(&s_lock);
                (void)xQueueReset(s_commands);
                ESP_LOGW(TAG, "Command %lu ack deadline expired; invalid until fresh sync",
                         (unsigned long)pending.id);
                pending.active = false;
            }

            if (!pending.active) {
                taskENTER_CRITICAL(&s_lock);
                bool commands_paused = s_command_failed || s_recovery_requested ||
                                       s_transport_reopen_requested || s_disconnected;
                taskEXIT_CRITICAL(&s_lock);
                if (commands_paused) {
                    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
                    continue;
                }
                command_request_t request = {.generation = generation};
                bool have_request = false;
                if (barrier_stage < 2U) {
                    request.opcode = barrier_stage == 0U ? SK_CMD_RELOCK : SK_CMD_SET_TARGET;
                    taskENTER_CRITICAL(&s_lock);
                    request.argument = barrier_stage == 0U ? 0U : s_desired_target;
                    taskEXIT_CRITICAL(&s_lock);
                    have_request = true;
                } else {
                    /* A callback may have invalidated this session while the
                     * worker was sending; stale queued requests are discarded. */
                    while (xQueueReceive(s_commands, &request, 0) == pdTRUE) {
                        if (request.generation == generation) {
                            have_request = true;
                            break;
                        }
                    }
                }
                if (have_request) prepare_command(&pending, &request, &command_id);
            }
            uint32_t retry_ms = pending.attempts < 6U ?
                                COMMAND_RETRY_MS : COMMAND_SLOW_RETRY_MS;
            taskENTER_CRITICAL(&s_lock);
            bool can_transmit = !s_disconnected && !s_transport_reopen_requested &&
                                !s_recovery_requested &&
                                pending.request.generation == s_generation;
            taskEXIT_CRITICAL(&s_lock);
            if (pending.active && can_transmit && (!pending.sent ||
                (uint32_t)(now - pending.sent_ms) >= retry_ms)) {
                (void)transmit_command(dev, &pending, &tx_sequence);
            }
            (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
        }
        taskENTER_CRITICAL(&s_lock);
        s_state.connected = false;
        s_barrier_complete = false;
        s_sync_active = false;
        s_pending_requests = 0U;
        ++s_generation;
        invalidate_display_locked();
        refresh_display_locked();
        taskEXIT_CRITICAL(&s_lock);
        (void)xQueueReset(s_commands);
        if (reopen_transport) {
            ESP_LOGW(TAG, "USB transport error or telemetry timeout; reopening after close");
        }
        close_device_owner(dev);
        parser_reset();
        taskENTER_CRITICAL(&s_lock);
        bool physically_disconnected = s_disconnected;
        taskEXIT_CRITICAL(&s_lock);
        if (reopen_transport && !physically_disconnected) {
            ESP_LOGI(TAG, "USB transport reopen backoff: %u ms", TRANSPORT_BACKOFF_MS);
            vTaskDelay(pdMS_TO_TICKS(TRANSPORT_BACKOFF_MS));
        } else {
            ESP_LOGI(TAG, "A1 disconnected; waiting for USB reconnect");
        }
    }
}

esp_err_t sound_key_link_start(void)
{
    taskENTER_CRITICAL(&s_lock);
    bool already_started = s_started || s_starting;
    if (!already_started) s_starting = true;
    taskEXIT_CRITICAL(&s_lock);
    if (already_started) return ESP_ERR_INVALID_STATE;

    bool host_installed = false;
    bool cdc_installed = false;
    TaskHandle_t events_task = NULL;
    esp_err_t ret = ESP_ERR_NO_MEM;
    s_commands = xQueueCreate(COMMAND_QUEUE_DEPTH, sizeof(command_request_t));
    if (s_commands == NULL) goto fail;
    const usb_host_config_t host_cfg = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LOWMED,
    };
    ret = usb_host_install(&host_cfg);
    if (ret != ESP_OK) goto fail;
    host_installed = true;
    const cdc_acm_host_driver_config_t cdc_cfg = {
        .driver_task_stack_size = 6144,
        .driver_task_priority = 11,
        .xCoreID = 1,
        .new_dev_cb = NULL,
    };
    ret = cdc_acm_host_install(&cdc_cfg);
    if (ret != ESP_OK) goto fail;
    cdc_installed = true;
    if (xTaskCreatePinnedToCore(usb_host_events, "sk_usb_events", 4096, NULL, 9,
                                &events_task, 1) != pdPASS) {
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }
    if (xTaskCreatePinnedToCore(usb_link_worker, "sk_usb_link", 6144, NULL, 8,
                                &s_worker_task, 1) != pdPASS) {
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }
    taskENTER_CRITICAL(&s_lock);
    s_started = true;
    s_starting = false;
    taskEXIT_CRITICAL(&s_lock);
    return ESP_OK;

fail:
    /* No link worker exists on this path, hence no device was opened. Keep
     * Host events serviced while the CDC client unregisters. */
    if (cdc_installed) (void)cdc_acm_host_uninstall();
    if (events_task != NULL) vTaskDelete(events_task);
    if (host_installed) {
        (void)usb_host_device_free_all();
        (void)usb_host_uninstall();
    }
    if (s_commands != NULL) {
        vQueueDelete(s_commands);
        s_commands = NULL;
    }
    taskENTER_CRITICAL(&s_lock);
    s_starting = false;
    taskEXIT_CRITICAL(&s_lock);
    return ret;
}

void sound_key_link_get_snapshot(sound_key_snapshot_t *out)
{
    if (out == NULL) return;
    taskENTER_CRITICAL(&s_lock);
    refresh_display_locked();
    *out = s_state;
    taskEXIT_CRITICAL(&s_lock);
}

static esp_err_t enqueue_command(uint8_t opcode, uint32_t argument)
{
    command_request_t request = {.opcode = opcode, .argument = argument};
    taskENTER_CRITICAL(&s_lock);
    uint32_t now = now_ms();
    bool available = s_started && s_state.connected && !s_disconnected &&
                     s_barrier_complete && !s_recovery_requested &&
                     !s_transport_reopen_requested && !s_link_stalled &&
                     s_received_packets != 0U &&
                     (uint32_t)(now - s_state.last_packet_ms) < SK_LINK_STALL_MS;
    if (available) {
        request.generation = s_generation;
        ++s_pending_requests;
        refresh_display_locked();
    }
    taskEXIT_CRITICAL(&s_lock);
    if (!available) return ESP_ERR_INVALID_STATE;

    /* LVGL callbacks never perform blocking USB operations or wait for space. */
    BaseType_t accepted = xQueueSend(s_commands, &request, 0);
    taskENTER_CRITICAL(&s_lock);
    bool same_session = request.generation == s_generation && !s_disconnected &&
                        !s_transport_reopen_requested;
    if (same_session && accepted == pdTRUE && opcode == SK_CMD_SET_TARGET) {
        s_desired_target = argument;
    } else if (same_session && accepted != pdTRUE && s_pending_requests != 0U) {
        --s_pending_requests;
    }
    refresh_display_locked();
    taskEXIT_CRITICAL(&s_lock);
    if (!same_session) return ESP_ERR_INVALID_STATE;
    if (accepted != pdTRUE) return ESP_ERR_NO_MEM;
    if (s_worker_task != NULL) xTaskNotifyGive(s_worker_task);
    return ESP_OK;
}

esp_err_t sound_key_link_set_target(uint32_t hz)
{
    if (!sk_target_valid(hz)) return ESP_ERR_INVALID_ARG;
    return enqueue_command(SK_CMD_SET_TARGET, hz);
}

esp_err_t sound_key_link_relock(void)
{
    return enqueue_command(SK_CMD_RELOCK, 0U);
}
