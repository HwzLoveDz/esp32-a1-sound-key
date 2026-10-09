/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * Auto-generated peripheral configuration file
 * DO NOT MODIFY THIS FILE MANUALLY
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include "esp_board_periph.h"
#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "hal/gpio_types.h"
#include "periph_gpio.h"
#include "periph_i2s.h"

// Peripheral configuration structures
const static i2c_master_bus_config_t esp_bmgr_i2c_master_cfg = {
    .i2c_port = I2C_NUM_0,
    .sda_io_num = 46,
    .scl_io_num = 45,
    .glitch_ignore_cnt = 7,
    .intr_priority = 1,
    .trans_queue_depth = 0,
    .flags = {
        .enable_internal_pullup = true,
    },
    .clk_source = I2C_CLK_SRC_DEFAULT,
};

const static periph_i2s_config_t esp_bmgr_i2s_audio_out_cfg = {
    .port = I2S_NUM_0,
    .role = I2S_ROLE_MASTER,
    .mode = I2S_COMM_MODE_TDM,
    .direction = I2S_DIR_TX,
    .i2s_cfg = {
        .tdm = {
            .clk_cfg = {
                .sample_rate_hz = 65000,
                .clk_src = I2S_CLK_SRC_APLL,
                .ext_clk_freq_hz = 0,
                .mclk_multiple = 960,
                .bclk_div = 8,
            },
            .slot_cfg = {
                .data_bit_width = I2S_DATA_BIT_WIDTH_24BIT,
                .slot_bit_width = I2S_SLOT_BIT_WIDTH_24BIT,
                .slot_mode = I2S_SLOT_MODE_STEREO,
                .slot_mask = I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3 | I2S_TDM_SLOT4,
                .ws_width = 60,
                .ws_pol = false,
                .bit_shift = true,
                .left_align = false,
                .big_endian = false,
                .bit_order_lsb = false,
                .skip_mask = false,
                .total_slot = 5,
            },
            .gpio_cfg = {
                .mclk = 36,
                .bclk = 37,
                .ws = 1,
                .dout = 35,
                .din = 6,
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv = false,
                },
            },
        },
    },
};

const static periph_i2s_config_t esp_bmgr_i2s_audio_in_cfg = {
    .port = I2S_NUM_0,
    .role = I2S_ROLE_MASTER,
    .mode = I2S_COMM_MODE_TDM,
    .direction = I2S_DIR_RX,
    .i2s_cfg = {
        .tdm = {
            .clk_cfg = {
                .sample_rate_hz = 65000,
                .clk_src = I2S_CLK_SRC_APLL,
                .ext_clk_freq_hz = 0,
                .mclk_multiple = 960,
                .bclk_div = 8,
            },
            .slot_cfg = {
                .data_bit_width = I2S_DATA_BIT_WIDTH_24BIT,
                .slot_bit_width = I2S_SLOT_BIT_WIDTH_24BIT,
                .slot_mode = I2S_SLOT_MODE_STEREO,
                .slot_mask = I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3 | I2S_TDM_SLOT4,
                .ws_width = 60,
                .ws_pol = false,
                .bit_shift = true,
                .left_align = false,
                .big_endian = false,
                .bit_order_lsb = false,
                .skip_mask = false,
                .total_slot = 5,
            },
            .gpio_cfg = {
                .mclk = 36,
                .bclk = 37,
                .ws = 1,
                .dout = 35,
                .din = 6,
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv = false,
                },
            },
        },
    },
};

const static periph_gpio_config_t esp_bmgr_gpio_a1_int_cfg = {
    .gpio_config = {
        .pin_bit_mask = BIT64(7),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    },
    .default_level = 0,
};

const static periph_gpio_config_t esp_bmgr_gpio_a1_en_cfg = {
    .gpio_config = {
        .pin_bit_mask = BIT64(2),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    },
    .default_level = 1,
};

const static periph_gpio_config_t esp_bmgr_gpio_osc_en_cfg = {
    .gpio_config = {
        .pin_bit_mask = BIT64(38),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    },
    .default_level = 1,
};

// Peripheral descriptor array
const esp_board_periph_desc_t g_esp_board_peripherals[] = {
    {
        .next = &g_esp_board_peripherals[1],
        .name = "i2c_master",
        .type = "i2c",
        .format = NULL,
        .role = ESP_BOARD_PERIPH_ROLE_MASTER,
        .cfg = &esp_bmgr_i2c_master_cfg,
        .cfg_size = sizeof(esp_bmgr_i2c_master_cfg),
        .id = 0,
    },
    {
        .next = &g_esp_board_peripherals[2],
        .name = "i2s_audio_out",
        .type = "i2s",
        .format = "tdm-out",
        .role = ESP_BOARD_PERIPH_ROLE_MASTER,
        .cfg = &esp_bmgr_i2s_audio_out_cfg,
        .cfg_size = sizeof(esp_bmgr_i2s_audio_out_cfg),
        .id = 0,
    },
    {
        .next = &g_esp_board_peripherals[3],
        .name = "i2s_audio_in",
        .type = "i2s",
        .format = "tdm-in",
        .role = ESP_BOARD_PERIPH_ROLE_MASTER,
        .cfg = &esp_bmgr_i2s_audio_in_cfg,
        .cfg_size = sizeof(esp_bmgr_i2s_audio_in_cfg),
        .id = 0,
    },
    {
        .next = &g_esp_board_peripherals[4],
        .name = "gpio_a1_int",
        .type = "gpio",
        .format = NULL,
        .role = ESP_BOARD_PERIPH_ROLE_IO,
        .cfg = &esp_bmgr_gpio_a1_int_cfg,
        .cfg_size = sizeof(esp_bmgr_gpio_a1_int_cfg),
        .id = 0,
    },
    {
        .next = &g_esp_board_peripherals[5],
        .name = "gpio_a1_en",
        .type = "gpio",
        .format = NULL,
        .role = ESP_BOARD_PERIPH_ROLE_IO,
        .cfg = &esp_bmgr_gpio_a1_en_cfg,
        .cfg_size = sizeof(esp_bmgr_gpio_a1_en_cfg),
        .id = 0,
    },
    {
        .next = NULL,
        .name = "gpio_osc_en",
        .type = "gpio",
        .format = NULL,
        .role = ESP_BOARD_PERIPH_ROLE_IO,
        .cfg = &esp_bmgr_gpio_osc_en_cfg,
        .cfg_size = sizeof(esp_bmgr_gpio_osc_en_cfg),
        .id = 0,
    },
};
