cmake_minimum_required(VERSION 3.13)
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
project(pico_endgame C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(CARD_SOURCE "${CMAKE_CURRENT_LIST_DIR}/assets/ENDGAME" CACHE PATH "ENDGAME directory")
set(CARD_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(GLOB_RECURSE CARD_INPUTS CONFIGURE_DEPENDS "${CARD_SOURCE}/*")
add_custom_command(OUTPUT "${CARD_GENERATED}/card_blob.S" "${CARD_GENERATED}/card_index.h"
  COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_LIST_DIR}/tools/pack_card.py"
          "${CARD_SOURCE}" --output "${CARD_GENERATED}"
  DEPENDS ${CARD_INPUTS} "${CMAKE_CURRENT_LIST_DIR}/tools/pack_card.py"
  VERBATIM)
add_custom_target(card_data DEPENDS "${CARD_GENERATED}/card_blob.S" "${CARD_GENERATED}/card_index.h")
add_executable(pico_endgame src/main.c src/usb_descriptors.c
  third_party/puff/puff.c "${CARD_GENERATED}/card_blob.S")
add_dependencies(pico_endgame card_data)
target_include_directories(pico_endgame PRIVATE src third_party/puff "${CARD_GENERATED}")
target_link_libraries(pico_endgame PRIVATE pico_stdlib tinyusb_device tinyusb_board)
target_compile_definitions(pico_endgame PRIVATE PICO_STACK_SIZE=16384)
pico_enable_stdio_usb(pico_endgame 0)
pico_enable_stdio_uart(pico_endgame 0)
target_compile_options(pico_endgame PRIVATE -Wall -Wextra)
pico_set_program_name(pico_endgame "RRXD Lite recovery and session tools")
pico_set_program_version(pico_endgame "0.7.13")
pico_add_extra_outputs(pico_endgame)
