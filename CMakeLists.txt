cmake_minimum_required(VERSION 3.12)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Pull in Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(pico-gba-cart C CXX)

# Initialise the Pico SDK
pico_sdk_init()

add_subdirectory(lib)

# Add executable. Default name is the project name, version 0.1

add_executable(pico-gba-cart
    main.cpp
)

pico_set_program_name(pico-gba-cart "pico-gba-cart")
pico_set_program_version(pico-gba-cart "0.1")

pico_enable_stdio_uart(pico-gba-cart 0)
pico_enable_stdio_usb(pico-gba-cart 0)

target_compile_options(pico-gba-cart PRIVATE -Wall)
target_link_libraries(pico-gba-cart pico_stdlib gbacart)

pico_add_extra_outputs(pico-gba-cart)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/pico-gba-cart.uf2
	DESTINATION bin
)