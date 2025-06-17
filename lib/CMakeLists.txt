
if(NOT DEFINED GBA_TOOLCHAIN_FILE)
    message("Downloading GBA toolchain...")

    include(FetchContent)

    FetchContent_Declare(gba-toolchain
        GIT_REPOSITORY https://github.com/felixjones/gba-toolchain
        GIT_TAG 28a92c95b9b3040e7871ce0d46098d81d9464528 #from experimental branch
    )

    FetchContent_MakeAvailable(gba-toolchain)

    set(GBA_TOOLCHAIN_FILE ${gba-toolchain_SOURCE_DIR}/cmake/gba.toolchain.cmake)
endif()

# build gba ROM
include(ExternalProject)
ExternalProject_Add(gba-rom-build
    PREFIX gba
    SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/../gba
    BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/gba
    INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/gba

    CMAKE_ARGS "-DCMAKE_TOOLCHAIN_FILE=${GBA_TOOLCHAIN_FILE}" -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR> -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=${CMAKE_EXPORT_COMPILE_COMMANDS}
    BUILD_ALWAYS TRUE
    INSTALL_BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/gba/bin/gba-rom.gba
)

add_custom_command(
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/gba/bin/
    OUTPUT gba-rom.o
    COMMAND ${CMAKE_OBJCOPY} -I binary -O elf32-littlearm -B armv6s-m --set-section-alignment .data=4 gba-rom.gba ${CMAKE_CURRENT_BINARY_DIR}/gba-rom.o
    DEPENDS gba-rom-build ${CMAKE_CURRENT_BINARY_DIR}/gba/bin/gba-rom.gba
)

add_library(gbacart STATIC
    gbacart.cpp
    gba-rom.o
)

target_link_libraries(gbacart pico_stdlib hardware_dma hardware_pio)
target_include_directories(gbacart PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

pico_generate_pio_header(gbacart ${CMAKE_CURRENT_LIST_DIR}/gba-cart.pio)