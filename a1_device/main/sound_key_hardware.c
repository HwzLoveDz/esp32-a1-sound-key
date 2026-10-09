/* SPDX-FileCopyrightText: 2026 HwzLoveDz
 * SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include "driver/i2c_master.h"
#include "esp_board_manager_includes.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audio_codec_reg.h"
#include "sound_key_protocol.h"
#include "sound_key_hardware.h"

static i2c_master_dev_handle_t s_device;
static uint32_t s_last_poll_ms;
static int64_t s_deadline_us;

static int transaction_budget_ms(void)
{
    int64_t remaining = s_deadline_us - esp_timer_get_time();
    if (remaining <= 0) return 0;
    int budget = (int)((remaining + 999) / 1000);
    return budget > 5 ? 5 : budget;
}

static esp_err_t reg_read(uint16_t reg, uint8_t *value)
{
    const uint8_t address[2] = {(uint8_t)(reg >> 8), (uint8_t)reg};
    int budget = transaction_budget_ms();
    if (!budget) return ESP_ERR_TIMEOUT;
    return i2c_master_transmit_receive(s_device, address, 2, value, 1, budget);
}

static esp_err_t reg_write(uint16_t reg, uint8_t value)
{
    const uint8_t packet[3] = {(uint8_t)(reg >> 8), (uint8_t)reg, value};
    int budget = transaction_budget_ms();
    if (!budget) return ESP_ERR_TIMEOUT;
    return i2c_master_transmit(s_device, packet, 3, budget);
}

static esp_err_t read_power(uint8_t bytes[8])
{
    /* Read each documented register, without assuming burst auto-increment. */
    for (unsigned i = 0; i < 8; ++i) {
        esp_err_t ret = reg_read(AUDIO_CODEC_GOERTZEL_TONE_PWR_0_REG + i, bytes + i);
        if (ret != ESP_OK) return ret;
    }
    return ESP_OK;
}

esp_err_t sound_key_hardware_start(sound_key_hardware_t *result)
{
    memset(result, 0, sizeof(*result));
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t ret = esp_board_manager_get_periph_handle("i2c_master", (void **)&bus);
    if (ret != ESP_OK || bus == NULL) return ret == ESP_OK ? ESP_FAIL : ret;
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = 0x2c, /* Board config uses shifted address 0x58. */
        .scl_speed_hz = 400000,
    };
    ret = i2c_master_bus_add_device(bus, &cfg, &s_device);
    if (ret != ESP_OK) return ret;
    s_deadline_us = esp_timer_get_time() + 15000;
    uint8_t low = 0, high = 0, conf = 0;
    if ((ret = reg_read(AUDIO_CODEC_GOERTZEL_COEFF_L_REG, &low)) != ESP_OK ||
        (ret = reg_read(AUDIO_CODEC_GOERTZEL_COEFF_H_REG, &high)) != ESP_OK ||
        (ret = reg_read(AUDIO_CODEC_GOERTZEL_CONF_REG, &conf)) != ESP_OK ||
        (ret = reg_write(AUDIO_CODEC_GOERTZEL_CTRL_REG, 0)) != ESP_OK ||
        (ret = reg_write(AUDIO_CODEC_GOERTZEL_CONF_REG,
                         conf & (uint8_t)~AUDIO_CODEC_GOERTZEL_CH_NUM_M)) != ESP_OK ||
        (ret = reg_write(AUDIO_CODEC_GOERTZEL_CTRL_REG, AUDIO_CODEC_GOERTZEL_CLEAR)) != ESP_OK ||
        (ret = reg_write(AUDIO_CODEC_GOERTZEL_CTRL_REG, AUDIO_CODEC_GOERTZEL_START)) != ESP_OK) {
        (void)i2c_master_bus_rm_device(s_device);
        s_device = NULL;
        return ret;
    }
    result->coefficient = (uint16_t)low | ((uint16_t)high << 8);
    result->available = true;
    return ESP_OK;
}

void sound_key_hardware_poll(sound_key_hardware_t *result, uint32_t now_ms)
{
    if (!result->available || now_ms - s_last_poll_ms < 250U) return;
    s_last_poll_ms = now_ms;
    /* Bound the whole optional diagnostic, rather than each transaction alone. */
    s_deadline_us = esp_timer_get_time() + 15000;
    uint8_t control = 0;
    esp_err_t ret = reg_read(AUDIO_CODEC_GOERTZEL_CTRL_REG, &control);
    result->control = control;
    if (ret != ESP_OK) goto disable;
    if (!(control & AUDIO_CODEC_GOERTZEL_DONE)) return;
    /* Stop before the two reads. This is a diagnostic consistency check;
     * retention after stop and DONE freshness still require bench evidence. */
    ret = reg_write(AUDIO_CODEC_GOERTZEL_CTRL_REG, 0);
    if (ret != ESP_OK) goto disable;
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        ret = reg_read(AUDIO_CODEC_GOERTZEL_CTRL_REG, &control);
        if (ret != ESP_OK) goto disable;
        if (!(control & AUDIO_CODEC_GOERTZEL_BUSY)) break;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    result->control = control;
    if (control & AUDIO_CODEC_GOERTZEL_BUSY) goto restart;
    uint8_t first[8], second[8];
    ret = read_power(first);
    if (ret != ESP_OK) goto disable;
    ret = read_power(second);
    if (ret != ESP_OK) goto disable;
    if (memcmp(first, second, sizeof(first)) == 0) {
        result->tone_power = sk_get_u32(first);
        result->total_power = sk_get_u32(first + 4);
        result->saturated = result->tone_power == UINT32_MAX || result->total_power == UINT32_MAX;
        ++result->stable_reads;
    }
restart:
    ret = reg_write(AUDIO_CODEC_GOERTZEL_CTRL_REG, AUDIO_CODEC_GOERTZEL_CLEAR);
    if (ret == ESP_OK)
        ret = reg_write(AUDIO_CODEC_GOERTZEL_CTRL_REG, AUDIO_CODEC_GOERTZEL_START);
    if (ret == ESP_OK) return;
disable:
    /* Stop diagnostics on an I2C failure; PCM software detection is independent. */
    s_deadline_us = esp_timer_get_time() + 1000;
    (void)reg_write(AUDIO_CODEC_GOERTZEL_CTRL_REG, 0);
    result->available = false;
    result->tone_power = result->total_power = 0;
}
