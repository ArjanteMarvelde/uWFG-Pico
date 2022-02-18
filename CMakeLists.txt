# Cmake Pico project file

# After changing this file, empty build folder and execute from there:  
#     cmake -G "NMake Makefiles" ..

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# initalize pico_sdk from installed location
# (note this can come from environment, CMake cache etc)
set(PICO_SDK_PATH "C:/Users/Arjan/Documents/Pico/pico-sdk")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(uWFG C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
# This creates a pico-sdk subdirectory in our project for the libraries
pico_sdk_init()

# Add executable. Default name is the project name, version 0.1
add_executable(uWFG uWFG.c gen.c waveform.c monitor.c lcd.c hmi.c lcdfont.c lcdlogo.c)

pico_set_program_name(uWFG "uWFG")
pico_set_program_version(uWFG "0.1")

# Create C header file with the name <pio program>.pio.h
pico_generate_pio_header(uWFG ${CMAKE_CURRENT_LIST_DIR}/wfgout.pio)

# Pull in our pico_stdlib which aggregates commonly used features
target_link_libraries(uWFG pico_stdlib)

# Disable uart output, enable usb output
pico_enable_stdio_uart(uWFG 0)
pico_enable_stdio_usb(uWFG 1)


# Add any user requested libraries
target_link_libraries(uWFG
        pico_stdlib
		pico_multicore
		hardware_irq
		hardware_i2c
		hardware_pwm
        hardware_gpio
        hardware_timer
        hardware_clocks
		hardware_pll
		hardware_adc
		hardware_pio
		hardware_dma
        )

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(uWFG)

