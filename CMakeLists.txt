# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

if(NOT DEFINED RETROSCOPE_TARGET)
    set(RETROSCOPE_TARGET "tab5" CACHE STRING "Hardware target")
endif()

set(SDKCONFIG_DEFAULTS "sdkconfig.defaults" "sdkconfig.defaults.esp32p4")
if(RETROSCOPE_TARGET STREQUAL "tab5")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.defaults.m5stack_tab5")
elseif(RETROSCOPE_TARGET STREQUAL "esp32_p4_panel_eth_2ro")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.defaults.esp32_p4_panel_eth_2ro")
elseif(RETROSCOPE_TARGET STREQUAL "jc8012p4a1c_i_w_y")
    list(APPEND SDKCONFIG_DEFAULTS "sdkconfig.defaults.jc8012p4a1c_i_w_y")
else()
    message(FATAL_ERROR "Unknown RETROSCOPE_TARGET='${RETROSCOPE_TARGET}'")
endif()

set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig.${RETROSCOPE_TARGET}")
# Use the same board selection for managed dependencies and source files.
# This must be available before the component manager evaluates the manifest.
set(ENV{RETROSCOPE_TARGET} "${RETROSCOPE_TARGET}")
# Use the same board selection for managed dependencies and source files.
# This must be available before the component manager evaluates the manifest.
set(ENV{RETROSCOPE_TARGET} "${RETROSCOPE_TARGET}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Export the board selection to IDF's separate component-requirements pass.
idf_build_set_property(RETROSCOPE_TARGET "${RETROSCOPE_TARGET}")
project(esp32_p4_photoframe)
