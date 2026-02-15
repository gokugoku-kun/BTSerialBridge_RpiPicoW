# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(picow_bt_spp C CXX ASM)

# Initialize the SDK
pico_sdk_init()

# Build configuration options
option(ENABLE_DEBUG_LOGGING "Enable debug logging" ON)

# Add an executable target for the project
add_executable(${PROJECT_NAME})

# Add C and C++ source files to the build
target_sources(${PROJECT_NAME} PRIVATE
  src/main.c
  src/uart_bridge.c
  src/status_manager.c
  src/watchdog_manager.c
  src/data_transfer_engine.c
  src/bt_spp_handler.c
  src/system_manager.c
  src/config_manager.c
  src/flash_storage.c
  src/debug_gpio.c
  src/switch_reader.c
)

# Link the Project to extra libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
  pico_stdlib
  pico_btstack_classic
  pico_btstack_cyw43
  pico_cyw43_arch_none
  hardware_uart
  hardware_gpio
  hardware_watchdog
  hardware_timer
  hardware_sync
  hardware_flash
  hardware_dma
)

# Including header files directly from project directory
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/src
)

# Compile definitions
if(ENABLE_DEBUG_LOGGING)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_DEBUG_LOGGING=1)
endif()

# Export binaries like hex, bin, and uf2 files.
pico_add_extra_outputs(${PROJECT_NAME})

# Enable or Disable UART
pico_enable_stdio_uart(${PROJECT_NAME} 0)

# Enable or Disable USB CDC
pico_enable_stdio_usb(${PROJECT_NAME} 1)

# Disable DTR check for USB CDC connection
add_definitions(-DPICO_STDIO_USB_CONNECTION_WITHOUT_DTR=1)



# Print build information
message(STATUS "Project: ${PROJECT_NAME}")
message(STATUS "Board: ${PICO_BOARD}")
message(STATUS "SDK Version: ${sdkVersion}")
message(STATUS "Debug Logging: ${ENABLE_DEBUG_LOGGING}")
