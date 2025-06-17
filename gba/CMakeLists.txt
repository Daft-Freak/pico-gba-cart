cmake_minimum_required(VERSION 3.18)

set(CMAKE_C_STANDARD 11)

project(gba-rom C)

add_executable(gba-rom
    main.c
)

find_package(librom REQUIRED)
find_package(sdk-seven REQUIRED COMPONENTS libseven)

target_compile_options(gba-rom PRIVATE -ffunction-sections -fdata-sections -mthumb)
target_include_directories(gba-rom PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../)
target_link_libraries(gba-rom PRIVATE librom sdk-seven::libseven)
target_link_options(gba-rom PRIVATE -Wl,--gc-sections)

set_target_properties(gba-rom PROPERTIES
    ROM_ID CRPE
    ROM_VERSION 1
)

install_rom(gba-rom)