
if(NOT DEFINED GBA_TOOLCHAIN_FILE)
    message(FATAL_ERROR "Set GBA_TOOLCHAIN_FILE! (path to gba-toolchain/arm-gba-toolchain.cmake)")
endif()

# build gba ROM
include(ExternalProject)
ExternalProject_Add(gba-rom-build
    PREFIX gba
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../gba
    BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/gba

    CMAKE_ARGS "-DCMAKE_TOOLCHAIN_FILE=${GBA_TOOLCHAIN_FILE}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=${CMAKE_EXPORT_COMPILE_COMMANDS} -DCMAKE_POLICY_VERSION_MINIMUM=3.5
    BUILD_ALWAYS TRUE
    BUILD_BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/gba/gba-rom.gba

    INSTALL_COMMAND ""
)

add_custom_command(
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/gba/
    OUTPUT gba-rom.o
    COMMAND ${CMAKE_OBJCOPY} -I binary -O elf32-littlearm -B armv6s-m --set-section-alignment .data=4 --rename-section .data=.gba_rom_data gba-rom.gba ${CMAKE_CURRENT_BINARY_DIR}/gba-rom.o
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/gba/gba-rom.gba
)

add_library(gbacart STATIC
    gbacart.cpp
    gba-rom.o
)

target_link_libraries(gbacart pico_stdlib hardware_dma hardware_pio)
target_include_directories(gbacart PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

pico_generate_pio_header(gbacart ${CMAKE_CURRENT_LIST_DIR}/gba-cart.pio)

set(LINKER_SCRIPT_2350 ${CMAKE_CURRENT_LIST_DIR}/memmap_2350.ld PARENT_SCOPE)

function(gbacart_set_linker_script TARGET)
    set_target_properties(${TARGET} PROPERTIES PICO_TARGET_LINKER_SCRIPT ${LINKER_SCRIPT_2350})
    pico_add_link_depend(${TARGET} ${LINKER_SCRIPT_2350})
endfunction()