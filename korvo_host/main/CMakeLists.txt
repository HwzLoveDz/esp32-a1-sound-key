idf_component_register(
    SRCS "main.c" "sound_key_link.c" "sound_key_ui.c" "sound_key_fonts.c"
    INCLUDE_DIRS "." "../../common"
    REQUIRES esp_board_manager esp_lvgl_port lvgl usb_host_cdc_acm
    PRIV_REQUIRES esp_timer
)
