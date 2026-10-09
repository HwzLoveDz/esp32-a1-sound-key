/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * Auto-generated device configuration file
 * DO NOT MODIFY THIS FILE MANUALLY
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include "esp_board_device.h"
#include "dev_audio_codec.h"
#include "dev_fs_fat.h"
#include "dev_power_ctrl.h"
#include "driver/i2c_master.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"

// Device configuration structures
const static dev_power_ctrl_config_t esp_bmgr_osc_en_ctrl_cfg = {
    .name = "osc_en_ctrl",
    .sub_type = "gpio",
    .sub_cfg = {
        .gpio = {
            .gpio_name = "gpio_osc_en",
            .active_level = 1,
        },
    },
};

const static dev_audio_codec_config_t esp_bmgr_audio_dac_cfg = {
    .name = "audio_dac",
    .chip = "esp32_a1",
    .type = "audio_codec",
    .data_if_type = 0,
    .adc_enabled = false,
    .dac_enabled = true,
    .pa_peripheral = {
        .name = NULL,
        .port = -1,
    },
    .reset_peripheral = {
        .name = "gpio_a1_en",
        .port = 2,
    },
    .i2c_cfg = {
        .name = "i2c_master",
        .port = 0,
        .address = 88,
        .frequency = 400000,
    },
    .i2s_cfg = {
        .name = "i2s_audio_out",
        .port = 0,
        .clk_src = I2S_CLK_SRC_APLL,
        .tx_aux_out_io = -1,
        .tx_aux_out_line = 0,
        .tx_aux_out_invert = false,
    },
    .adc_data_cfg = {
        .periph_name = NULL,
        .sample_rate_hz = 0,
        .max_store_buf_size = 0,
        .conv_frame_size = 0,
        .conv_mode = 0,
        .format = 0,
        .pattern_num = 0,
        .cfg_mode = 0,
        .cfg = {
            .single_unit = {
                .unit_id = 0,
                .atten = 0,
                .bit_width = 0,
                .channel_id = {},
            },
        },
    },
    .codec_sys_cfg = {
        .is_master = false,
        .no_mclk = true,
    },
    .codec_adc_cfg = {
        .digital_mic = false,
        .label = "",
    },
    .codec_dac_cfg = {
        .ref_enable = true,
        .ref_dac_ch = 1,
        .real_adc_data_ch = 0,
    },
    .codec_pa_cfg = {
        .pa_pin = -1,
        .pa_active_low = true,
        .hw_gain = {
            .pa_gain = 0.0,
        },
    },
    .codec_reset_cfg = {
        .reset_pin = 2,
        .reset_active_low = true,
    },
    .metadata = NULL,
    .metadata_size = 0,
};

const static dev_audio_codec_config_t esp_bmgr_audio_adc_cfg = {
    .name = "audio_adc",
    .chip = "esp32_a1",
    .type = "audio_codec",
    .data_if_type = 0,
    .adc_enabled = true,
    .dac_enabled = false,
    .pa_peripheral = {
        .name = NULL,
        .port = -1,
    },
    .reset_peripheral = {
        .name = "gpio_a1_en",
        .port = 2,
    },
    .i2c_cfg = {
        .name = "i2c_master",
        .port = 0,
        .address = 88,
        .frequency = 400000,
    },
    .i2s_cfg = {
        .name = "i2s_audio_in",
        .port = 0,
        .clk_src = I2S_CLK_SRC_APLL,
        .tx_aux_out_io = -1,
        .tx_aux_out_line = 0,
        .tx_aux_out_invert = false,
    },
    .adc_data_cfg = {
        .periph_name = NULL,
        .sample_rate_hz = 0,
        .max_store_buf_size = 0,
        .conv_frame_size = 0,
        .conv_mode = 0,
        .format = 0,
        .pattern_num = 0,
        .cfg_mode = 0,
        .cfg = {
            .single_unit = {
                .unit_id = 0,
                .atten = 0,
                .bit_width = 0,
                .channel_id = {},
            },
        },
    },
    .codec_sys_cfg = {
        .is_master = false,
        .no_mclk = true,
    },
    .codec_adc_cfg = {
        .digital_mic = false,
        .label = "FL,FR,BL,BR,RE",
    },
    .codec_dac_cfg = {
        .ref_enable = true,
        .ref_dac_ch = 1,
        .real_adc_data_ch = 0,
    },
    .codec_pa_cfg = {
        .pa_pin = -1,
        .pa_active_low = true,
        .hw_gain = {
            .pa_gain = 0.0,
        },
    },
    .codec_reset_cfg = {
        .reset_pin = 2,
        .reset_active_low = true,
    },
    .metadata = NULL,
    .metadata_size = 0,
};

const static dev_fs_fat_config_t esp_bmgr_fs_sdcard_cfg = {
    .name = "fs_sdcard",
    .mount_point = "/sdcard",
    .frequency = SDMMC_FREQ_HIGHSPEED,
    .vfs_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16384,
    },
    .sub_type = "sdmmc",
    .sub_cfg = {
        .sdmmc = {
            .slot = SDMMC_HOST_SLOT_0,
            .bus_width = 4,
            .slot_flags = SDMMC_SLOT_FLAG_INTERNAL_PULLUP,
            .pins = {
                .clk = 24,
                .cmd = 25,
                .d0 = 20,
                .d1 = 21,
                .d2 = 22,
                .d3 = 23,
                .d4 = -1,
                .d5 = -1,
                .d6 = -1,
                .d7 = -1,
                .cd = -1,
                .wp = -1,
            },
            .ldo_chan_id = -1,
        },
    },
};

// Device descriptor array
static const char* esp_bmgr_audio_dac_deps[] = {
    "osc_en_ctrl",
};
static const char* esp_bmgr_audio_adc_deps[] = {
    "osc_en_ctrl",
};

const esp_board_device_desc_t g_esp_board_devices[] = {
    {
        .next = &g_esp_board_devices[1],
        .name = "osc_en_ctrl",
        .chip = NULL,
        .type = "power_ctrl",
        .sub_type = "gpio",
        .cfg = &esp_bmgr_osc_en_ctrl_cfg,
        .cfg_size = sizeof(esp_bmgr_osc_en_ctrl_cfg),
        .init_skip = false,
        .depends_on = NULL,
        .depends_on_num = 0,
    },
    {
        .next = &g_esp_board_devices[2],
        .name = "audio_dac",
        .chip = "esp32_a1",
        .type = "audio_codec",
        .sub_type = NULL,
        .cfg = &esp_bmgr_audio_dac_cfg,
        .cfg_size = sizeof(esp_bmgr_audio_dac_cfg),
        .init_skip = false,
        .depends_on = esp_bmgr_audio_dac_deps,
        .depends_on_num = 1,
    },
    {
        .next = &g_esp_board_devices[3],
        .name = "audio_adc",
        .chip = "esp32_a1",
        .type = "audio_codec",
        .sub_type = NULL,
        .cfg = &esp_bmgr_audio_adc_cfg,
        .cfg_size = sizeof(esp_bmgr_audio_adc_cfg),
        .init_skip = false,
        .depends_on = esp_bmgr_audio_adc_deps,
        .depends_on_num = 1,
    },
    {
        .next = NULL,
        .name = "fs_sdcard",
        .chip = NULL,
        .type = "fs_fat",
        .sub_type = "sdmmc",
        .cfg = &esp_bmgr_fs_sdcard_cfg,
        .cfg_size = sizeof(esp_bmgr_fs_sdcard_cfg),
        .init_skip = false,
        .depends_on = NULL,
        .depends_on_num = 0,
    },
};
