/* SPDX-FileCopyrightText: 2026 HwzLoveDz
 * SPDX-License-Identifier: Apache-2.0 */
/* ESP32-A1 EV Board + onboard ESP32-S31. USB carries binary A1SK only;
 * startup and diagnostic text stay on UART. No SD card is mounted or written. */
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/i2s_common.h"
#include "driver/usb_serial_jtag.h"
#include "esp_attr.h"
#include "esp_board_manager_includes.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sound_key_detector.h"
#include "sound_key_hardware.h"
#include "sound_key_protocol.h"

#define PCM_SAMPLES 512U
#define PCM_WINDOW_MS (PCM_SAMPLES * 1000U / SK_SAMPLE_RATE_HZ)
static const char *TAG = "A1_SOUND_KEY";
static const char s_mic_label[] = "FL,NA,NA,NA,NA";
static sound_key_detector_t s_detector;
/* Named global can also be inspected via JTAG when CDC carries binary data. */
sound_key_hardware_t sound_key_hw_diag;
static DRAM_ATTR volatile uint32_t s_rx_overflows;
static int16_t s_pcm[PCM_SAMPLES] __attribute__((aligned(4)));
static uint32_t s_sequence, s_ack_id, s_measurement_count, s_fault;
static uint32_t s_init_stage;
static esp_err_t s_init_error;
static char s_boot_trace[4096];
static size_t s_boot_trace_used;
static portMUX_TYPE s_trace_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_command_buffer[256];
static size_t s_command_used;
static uint32_t now_ms(void);

static int trace_vprintf(const char *format, va_list args)
{
    char line[256];
    va_list copy;
    va_copy(copy, args);
    int length = vsnprintf(line, sizeof(line), format, copy);
    va_end(copy);
    if (length > 0) {
        size_t count = (size_t)length < sizeof(line) ? (size_t)length : sizeof(line) - 1;
        portENTER_CRITICAL(&s_trace_lock);
        size_t room = sizeof(s_boot_trace) - 1 - s_boot_trace_used;
        if (count > room) {
            size_t drop = count - room;
            memmove(s_boot_trace, s_boot_trace + drop, s_boot_trace_used - drop);
            s_boot_trace_used -= drop;
        }
        memcpy(s_boot_trace + s_boot_trace_used, line, count);
        s_boot_trace_used += count;
        s_boot_trace[s_boot_trace_used] = 0;
        portEXIT_CRITICAL(&s_trace_lock);
    }
    return vprintf(format, args);
}

static void send_boot_diagnostics(void)
{
    if (!usb_serial_jtag_is_connected()) return;
    /* Independent magic keeps fault diagnostics out of the production A1SK
     * parser. Header is the same; 48-byte payload is offset,total,40 bytes.
     * offset UINT32_MAX marks error-code,stage plus 40 bytes of error name. */
    uint8_t packet[72] = {0};
    memcpy(packet, "A1DB", 4);
    packet[4] = 1;
    packet[5] = 3;
    sk_put_u32(packet + 12, now_ms());
    sk_put_u16(packet + 16, 48);
    sk_put_u32(packet + 20, UINT32_MAX);
    sk_put_u32(packet + 24, (uint32_t)s_init_error);
    sk_put_u32(packet + 28, s_init_stage);
    snprintf((char *)packet + 32, 36, "%s", esp_err_to_name(s_init_error));
    sk_put_u32(packet + 68, sk_crc32(packet, 68));
    (void)usb_serial_jtag_write_bytes(packet, sizeof(packet), pdMS_TO_TICKS(2));
    for (size_t offset = 0; offset < s_boot_trace_used; offset += 40) {
        memset(packet + 20, 0, 48);
        sk_put_u32(packet + 20, offset);
        sk_put_u32(packet + 24, s_boot_trace_used);
        size_t count = s_boot_trace_used - offset;
        if (count > 40) count = 40;
        memcpy(packet + 28, s_boot_trace + offset, count);
        sk_put_u32(packet + 68, sk_crc32(packet, 68));
        (void)usb_serial_jtag_write_bytes(packet, sizeof(packet), pdMS_TO_TICKS(2));
    }
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void send_telemetry(void)
{
    const uint32_t sequence = s_sequence++;
    if (!usb_serial_jtag_is_connected()) return;
    uint8_t packet[SK_HEADER_BYTES + SK_TELEMETRY_BYTES + SK_CRC_BYTES] = {0};
    memcpy(packet, SK_MAGIC, 4);
    packet[4] = SK_PROTOCOL_VERSION;
    packet[5] = SK_PKT_TELEMETRY;
    sk_put_u32(packet + 8, sequence);
    sk_put_u32(packet + 12, now_ms());
    sk_put_u16(packet + 16, SK_TELEMETRY_BYTES);
    uint8_t *p = packet + SK_HEADER_BYTES;
    sk_put_u32(p, s_detector.target_hz);
    sk_put_u32(p + 4, s_detector.sample_rate);
    sk_put_u16(p + 8, s_detector.hold_ms);
    sk_put_u16(p + 10, s_detector.held_ms);
    sk_put_u16(p + 12, s_detector.strength_permille);
    const uint32_t fatal = s_fault & (SK_FAULT_BOARD | SK_FAULT_ADC | SK_FAULT_I2C);
    p[14] = fatal ? SK_FAULT : s_detector.state;
    p[15] = SK_BACKEND_SOFTWARE;
    sk_put_u32(p + 16, s_detector.unlock_count);
    sk_put_u32(p + 20, sound_key_hw_diag.tone_power);
    sk_put_u32(p + 24, sound_key_hw_diag.total_power);
    sk_put_u16(p + 28, s_detector.rms);
    sk_put_u16(p + 30, sound_key_hw_diag.coefficient);
    sk_put_u32(p + 32, PCM_WINDOW_MS);
    sk_put_u32(p + 36, s_ack_id);
    /* HW_NOT_READY means not calibrated, even when stable raw data exist. */
    uint32_t flags = s_fault | SK_FAULT_HW_NOT_READY;
    if (!sound_key_hw_diag.available || sound_key_hw_diag.stable_reads == 0)
        flags |= SK_FAULT_HW_READ_UNAVAILABLE;
    if (sound_key_hw_diag.saturated) flags |= SK_FAULT_HW_RANGE;
    if (s_detector.clipped) flags |= SK_FAULT_INPUT_CLIPPED;
    sk_put_u32(p + 40, flags);
    sk_put_u32(p + 44, s_measurement_count);
    const size_t bytes = SK_HEADER_BYTES + SK_TELEMETRY_BYTES;
    sk_put_u32(packet + bytes, sk_crc32(packet, bytes));
    /* USB driver accepts an entire BYTEBUF item or none. Bound host stalls. */
    (void)usb_serial_jtag_write_bytes(packet, sizeof(packet), pdMS_TO_TICKS(2));
}

static void apply_command(const uint8_t *p)
{
    const uint32_t id = sk_get_u32(p);
    if (id == 0 || p[5] || p[6] || p[7]) return;
    const uint32_t argument = sk_get_u32(p + 8);
    /* Retry of the most recently applied command only repeats its ack. */
    if (id == s_ack_id) return;
    if (p[4] == SK_CMD_SET_TARGET) {
        if (!sound_key_detector_set_target(&s_detector, argument)) return;
    } else if (p[4] == SK_CMD_RELOCK && argument == 0) {
        sound_key_detector_relock(&s_detector);
    } else {
        return;
    }
    s_ack_id = id; /* Only after the detector has been reset/reconfigured. */
    ESP_LOGI(TAG, "Applied command %lu, opcode=%u, target=%lu Hz",
             (unsigned long)id, p[4], (unsigned long)s_detector.target_hz);
}

static void receive_commands(void)
{
    uint8_t incoming[128];
    const int count = usb_serial_jtag_read_bytes(incoming, sizeof(incoming), 0);
    if (count > 0) {
        if (s_command_used + (size_t)count > sizeof(s_command_buffer)) s_command_used = 0;
        memcpy(s_command_buffer + s_command_used, incoming, count);
        s_command_used += count;
    }
    size_t consumed = 0;
    while (s_command_used - consumed >= SK_HEADER_BYTES) {
        const uint8_t *h = s_command_buffer + consumed;
        if (memcmp(h, SK_MAGIC, 4) || h[4] != SK_PROTOCOL_VERSION ||
            h[5] != SK_PKT_COMMAND || sk_get_u16(h + 6) ||
            sk_get_u16(h + 16) != SK_COMMAND_BYTES || sk_get_u16(h + 18)) {
            ++consumed;
            continue;
        }
        const size_t without_crc = SK_HEADER_BYTES + SK_COMMAND_BYTES;
        const size_t total = without_crc + SK_CRC_BYTES;
        if (s_command_used - consumed < total) break;
        if (sk_crc32(h, without_crc) != sk_get_u32(h + without_crc)) {
            ++consumed;
            continue;
        }
        apply_command(h + SK_HEADER_BYTES);
        consumed += total;
    }
    if (consumed) {
        s_command_used -= consumed;
        memmove(s_command_buffer, s_command_buffer + consumed, s_command_used);
    }
}

static void fatal_forever(uint32_t fault, esp_err_t error)
{
    s_fault |= fault;
    s_init_error = error;
    ESP_LOGE(TAG, "Capture stopped: flags=0x%lx, error=%s",
             (unsigned long)s_fault, esp_err_to_name(error));
    for (;;) {
        receive_commands();
        send_telemetry();
        static uint32_t last_diag;
        if (now_ms() - last_diag >= 2000U) {
            last_diag = now_ms();
            send_boot_diagnostics();
        }
        vTaskDelay(pdMS_TO_TICKS(SK_TELEMETRY_INTERVAL_MS));
    }
}

static esp_err_t configure_microphone(esp_codec_dev_handle_t *adc_dev)
{
    /* Match the proven board's codec creation order: DAC, then ADC. Neither
     * the DAC stream nor the SD filesystem is opened by this demo. */
    s_init_stage = 1;
    esp_err_t ret = esp_board_periph_init_all();
    if (ret != ESP_OK) return ret;
    void *board_cfg = NULL;
    s_init_stage = 2;
    ret = esp_board_device_get_config(ESP_BOARD_DEVICE_NAME_AUDIO_ADC, &board_cfg);
    if (ret != ESP_OK || board_cfg == NULL) return ret == ESP_OK ? ESP_FAIL : ret;
    dev_audio_codec_config_t cfg = *(const dev_audio_codec_config_t *)board_cfg;
    cfg.codec_adc_cfg.label = s_mic_label;
    cfg.codec_dac_cfg.ref_enable = false;
    cfg.codec_dac_cfg.ref_dac_ch = -1;
    s_init_stage = 3;
    ret = esp_board_device_override_config(ESP_BOARD_DEVICE_NAME_AUDIO_ADC, &cfg, sizeof(cfg));
    if (ret != ESP_OK) return ret;
    ESP_LOGI(TAG, "Control bus before codec creation: SCL=%d SDA=%d",
             gpio_get_level(GPIO_NUM_45), gpio_get_level(GPIO_NUM_46));
    s_init_stage = 6;
    const esp_err_t dac_ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
    ESP_LOGI(TAG, "Baseline DAC creation result=%s; SCL=%d SDA=%d",
             esp_err_to_name(dac_ret), gpio_get_level(GPIO_NUM_45), gpio_get_level(GPIO_NUM_46));
    /* Board Manager's full initialization also proceeds to ADC after a DAC
     * failure. ADC's result is checked explicitly here. */
    s_init_stage = 4;
    ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_ADC);
    ESP_LOGI(TAG, "ADC creation result=%s; SCL=%d SDA=%d",
             esp_err_to_name(ret), gpio_get_level(GPIO_NUM_45), gpio_get_level(GPIO_NUM_46));
    if (ret != ESP_OK) return ret;
    dev_audio_codec_handles_t *adc = NULL;
    s_init_stage = 5;
    ret = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_ADC, (void **)&adc);
    if (ret != ESP_OK || adc == NULL || adc->codec_dev == NULL)
        return ret == ESP_OK ? ESP_FAIL : ret;
    *adc_dev = adc->codec_dev;
    return ESP_OK;
}

static bool IRAM_ATTR rx_overflow(i2s_chan_handle_t handle, i2s_event_data_t *event, void *arg)
{
    (void)handle; (void)event; (void)arg;
    ++s_rx_overflows;
    return false;
}

static esp_err_t attach_overflow_watch(void)
{
    i2s_chan_handle_t rx = NULL;
    esp_err_t ret = esp_board_manager_get_periph_handle("i2s_audio_in", (void **)&rx);
    if (ret != ESP_OK || rx == NULL) return ret == ESP_OK ? ESP_FAIL : ret;
    i2s_chan_info_t info = {0};
    ret = i2s_channel_get_info(rx, &info);
    if (ret != ESP_OK) return ret;
    if (info.is_enabled && (ret = i2s_channel_disable(rx)) != ESP_OK) return ret;
    const i2s_event_callbacks_t callbacks = {.on_recv_q_ovf = rx_overflow};
    ret = i2s_channel_register_event_callback(rx, &callbacks, NULL);
    if (info.is_enabled) {
        const esp_err_t enable_result = i2s_channel_enable(rx);
        if (ret == ESP_OK) ret = enable_result;
    }
    return ret;
}

void app_main(void)
{
    esp_log_set_vprintf(trace_vprintf);
    sound_key_detector_init(&s_detector, SK_SAMPLE_RATE_HZ, SK_DEFAULT_TARGET_HZ,
                            SK_DEFAULT_HOLD_MS);
    usb_serial_jtag_driver_config_t usb_cfg = {
        .tx_buffer_size = 4096, .rx_buffer_size = 1024, .intr_priority = 0,
    };
    esp_err_t ret = usb_serial_jtag_driver_install(&usb_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "USB driver: %s", esp_err_to_name(ret));
        return;
    }
    esp_codec_dev_handle_t adc = NULL;
    ret = configure_microphone(&adc);
    if (ret != ESP_OK) fatal_forever(SK_FAULT_BOARD, ret);
    ret = attach_overflow_watch();
    if (ret != ESP_OK) fatal_forever(SK_FAULT_ADC, ret);
    esp_codec_dev_sample_info_t format = {
        .sample_rate = SK_SAMPLE_RATE_HZ, .channel = 1, .bits_per_sample = 16,
        .mclk_multiple = 768, .channel_mask = 0x01,
    };
    int codec_result = esp_codec_dev_open(adc, &format);
    if (codec_result != ESP_CODEC_DEV_OK) fatal_forever(SK_FAULT_ADC, ESP_FAIL);
    if (esp_codec_dev_set_in_gain(adc, 20.0f) != ESP_CODEC_DEV_OK)
        fatal_forever(SK_FAULT_ADC, ESP_FAIL);
    /* Ignore startup overflow while the codec clocks were reconfigured. */
    s_rx_overflows = 0;
    ret = sound_key_hardware_start(&sound_key_hw_diag);
    ESP_LOGI(TAG, "Software reference: 16k mono / 512 samples / 500ms; HW diagnostic=%s coeff=0x%04x",
             esp_err_to_name(ret), sound_key_hw_diag.coefficient);
    uint32_t last_tx = 0, last_log = 0;
    for (;;) {
        receive_commands();
        codec_result = esp_codec_dev_read(adc, s_pcm, sizeof(s_pcm));
        if (codec_result != ESP_CODEC_DEV_OK || s_rx_overflows != 0) {
            /* Incomplete PCM continuity is a capture fault, never a hit. */
            sound_key_detector_relock(&s_detector);
            (void)esp_codec_dev_close(adc);
            fatal_forever(SK_FAULT_ADC, ESP_FAIL);
        }
        uint32_t stamp = now_ms();
        sound_key_detector_process(&s_detector, s_pcm, PCM_SAMPLES, stamp);
        ++s_measurement_count;
        sound_key_hardware_poll(&sound_key_hw_diag, stamp);
        receive_commands();
        if (stamp - last_tx >= SK_TELEMETRY_INTERVAL_MS) {
            last_tx = stamp;
            send_telemetry();
        }
        if (stamp - last_log >= 2000U) {
            last_log = stamp;
            ESP_LOGI(TAG, "SW target=%lu strength=%u/1000 rms=%u state=%u hold=%u/%u; HW ctrl=0x%02x tone=%lu total=%lu stable=%lu",
                     (unsigned long)s_detector.target_hz, s_detector.strength_permille,
                     s_detector.rms, s_detector.state, s_detector.held_ms, s_detector.hold_ms,
                     sound_key_hw_diag.control, (unsigned long)sound_key_hw_diag.tone_power,
                     (unsigned long)sound_key_hw_diag.total_power,
                     (unsigned long)sound_key_hw_diag.stable_reads);
        }
    }
}
