cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED ENV{ESP32_A1_SDK_ROOT} OR "$ENV{ESP32_A1_SDK_ROOT}" STREQUAL "")
    message(FATAL_ERROR "Set ESP32_A1_SDK_ROOT to your separately obtained esp32_a1_v0.5 SDK. The SDK is not distributed in this repository.")
endif()
file(TO_CMAKE_PATH "$ENV{ESP32_A1_SDK_ROOT}" A1_SDK_ROOT)
get_filename_component(A1_SDK_ROOT "${A1_SDK_ROOT}" ABSOLUTE)
set(ENV{ESP32_A1_SDK_ROOT} "${A1_SDK_ROOT}")
foreach(A1_REQUIRED_FILE IN ITEMS
    src/esp32_a1_codec.c src/esp32_a1_workaround.c src/esp32_a1_adc_cali.c
    src/esp32_a1_vad.c src/esp32_a1_int.c src/esp32_a1_int_ctrl.c src/esp32_a1_clock.c
    include/esp32_a1_codec.h private_inc/hw/audio_codec_reg.h
    examples/esp_friends_boards/idf_component.yml
    examples/esp_friends_boards/esp32_a1_ev_board/board_info.yaml)
    if(NOT EXISTS "${A1_SDK_ROOT}/${A1_REQUIRED_FILE}")
        message(FATAL_ERROR "ESP32_A1_SDK_ROOT is incomplete: missing ${A1_REQUIRED_FILE} under ${A1_SDK_ROOT}")
    endif()
endforeach()

# A local esp_codec_dev component takes precedence over the registry dependency.
if(DEFINED ENV{ESP_CODEC_DEV_ROOT} AND NOT "$ENV{ESP_CODEC_DEV_ROOT}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{ESP_CODEC_DEV_ROOT}" A1_CODEC_ROOT)
    get_filename_component(A1_CODEC_ROOT "${A1_CODEC_ROOT}" ABSOLUTE)
    get_filename_component(A1_CODEC_NAME "${A1_CODEC_ROOT}" NAME)
    if(NOT A1_CODEC_NAME STREQUAL "esp_codec_dev" OR
       NOT EXISTS "${A1_CODEC_ROOT}/CMakeLists.txt" OR
       NOT EXISTS "${A1_CODEC_ROOT}/idf_component.yml")
        message(FATAL_ERROR "ESP_CODEC_DEV_ROOT must be a complete component directory named esp_codec_dev: ${A1_CODEC_ROOT}")
    endif()
    list(APPEND EXTRA_COMPONENT_DIRS "${A1_CODEC_ROOT}")
endif()

# Apply the checked-in board configuration on the first clone/build, before
# esp_board_manager's idf.py extension has been downloaded and discovered.
if(NOT DEFINED SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;components/gen_bmgr_codes/board_manager.defaults;sdkconfig.defaults.esp32s31")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(a1_sound_key_device)
