cmake_minimum_required(VERSION 3.16)

if(DEFINED ENV{ESP_CODEC_DEV_ROOT} AND NOT "$ENV{ESP_CODEC_DEV_ROOT}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{ESP_CODEC_DEV_ROOT}" KORVO_CODEC_ROOT)
    get_filename_component(KORVO_CODEC_ROOT "${KORVO_CODEC_ROOT}" ABSOLUTE)
    get_filename_component(KORVO_CODEC_NAME "${KORVO_CODEC_ROOT}" NAME)
    if(NOT KORVO_CODEC_NAME STREQUAL "esp_codec_dev" OR
       NOT EXISTS "${KORVO_CODEC_ROOT}/CMakeLists.txt" OR
       NOT EXISTS "${KORVO_CODEC_ROOT}/idf_component.yml")
        message(FATAL_ERROR "ESP_CODEC_DEV_ROOT must be a complete component directory named esp_codec_dev: ${KORVO_CODEC_ROOT}")
    endif()
    list(APPEND EXTRA_COMPONENT_DIRS "${KORVO_CODEC_ROOT}")
endif()

if(NOT DEFINED SDKCONFIG_DEFAULTS)
    set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;components/gen_bmgr_codes/board_manager.defaults;sdkconfig.defaults.esp32s31")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(a1_sound_key_host)
