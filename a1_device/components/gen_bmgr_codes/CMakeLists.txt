# Board information output
message(STATUS "Selected Board: esp32_a1_ev_board")
set(A1_BOARD_ROOT "$ENV{ESP32_A1_SDK_ROOT}/examples/esp_friends_boards/esp32_a1_ev_board")
message(STATUS "Board Path: ${A1_BOARD_ROOT}")

idf_component_register(
    SRC_DIRS "." "${A1_BOARD_ROOT}"
    INCLUDE_DIRS "." "${A1_BOARD_ROOT}"
    REQUIRES esp_board_manager
)

# This is equivalent to adding WHOLE_ARCHIVE option to the idf_component_register call above:
idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)
