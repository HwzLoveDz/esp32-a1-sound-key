# The supplied SDK directory is named esp32_a1_v0.5, but ESP-IDF component
# dependencies require the logical component name esp32_a1. Compile its source
# in place through this small local wrapper; do not copy or edit the SDK.
set(A1_SDK_ROOT "$ENV{ESP32_A1_SDK_ROOT}")

idf_component_register(
    SRCS
        "${A1_SDK_ROOT}/src/esp32_a1_codec.c"
        "${A1_SDK_ROOT}/src/esp32_a1_workaround.c"
        "${A1_SDK_ROOT}/src/esp32_a1_adc_cali.c"
        "${A1_SDK_ROOT}/src/esp32_a1_vad.c"
        "${A1_SDK_ROOT}/src/esp32_a1_int.c"
        "${A1_SDK_ROOT}/src/esp32_a1_int_ctrl.c"
        "${A1_SDK_ROOT}/src/esp32_a1_clock.c"
    INCLUDE_DIRS "${A1_SDK_ROOT}/include"
    PRIV_INCLUDE_DIRS "${A1_SDK_ROOT}/private_inc" "${A1_SDK_ROOT}/private_inc/hw"
    REQUIRES esp_codec_dev
    WHOLE_ARCHIVE
)
