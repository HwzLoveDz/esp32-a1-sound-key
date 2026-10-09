# Board information output
message(STATUS "Selected Board: esp32_s31_korvo_1")
set(KORVO_BOARD_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../managed_components/espressif__esp_boards/esp32_s31_korvo_1")
message(STATUS "Board Path: ${KORVO_BOARD_ROOT}")

idf_component_register(
    SRC_DIRS "." "${KORVO_BOARD_ROOT}"
    INCLUDE_DIRS "." "${KORVO_BOARD_ROOT}"
    REQUIRES esp_board_manager
)

# This is equivalent to adding WHOLE_ARCHIVE option to the idf_component_register call above:
idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)
