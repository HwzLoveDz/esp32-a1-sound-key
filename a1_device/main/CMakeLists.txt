idf_component_register(
    SRCS "sound_key_device.c" "sound_key_detector.c" "sound_key_hardware.c"
    INCLUDE_DIRS "." "../../common"
    PRIV_INCLUDE_DIRS "$ENV{ESP32_A1_SDK_ROOT}/private_inc/hw"
    REQUIRES driver esp_driver_usb_serial_jtag esp_timer esp_board_manager esp32_a1 esp_codec_dev
)
