cmake_minimum_required(VERSION 3.16)
project(courier_sim VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)   # Needed for Q_OBJECT / moc

# =============================================================================
# Find Qt5
# =============================================================================
find_package(Qt5 REQUIRED COMPONENTS Core Widgets)

# =============================================================================
# Compile-time configuration (override at cmake configure time)
# =============================================================================
if(NOT DEFINED EMPLOYEE_ID)
    set(EMPLOYEE_ID "\"E001\"")
endif()
if(NOT DEFINED MQTT_BROKER_ADDR)
    set(MQTT_BROKER_ADDR "\"localhost\"")
endif()
if(NOT DEFINED MQTT_BROKER_PORT)
    set(MQTT_BROKER_PORT 1883)
endif()
# WiFi credentials unused in simulation but required by courier_state.h includes
set(WIFI_SSID "\"sim\"")
set(WIFI_PSK  "\"sim\"")

# =============================================================================
# Source files
#
# We compile the same C business logic as the Zephyr build, but link it
# against Qt instead of Zephyr. Zephyr-specific calls (k_mutex, k_sem, etc.)
# are stubbed in main_sim.cpp.
# =============================================================================
add_executable(courier_sim
    main_sim.cpp
    ui_sim.cpp
    # Business logic — platform-independent C
    ../src/courier_state.c
    ../src/mqtt_handler.c
    # gps_task.c is NOT included — GPS is driven by a QTimer in main_sim.cpp
)

target_include_directories(courier_sim PRIVATE
    ../src
    ../src/ui
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(courier_sim PRIVATE
    EMPLOYEE_ID=${EMPLOYEE_ID}
    MQTT_BROKER_ADDR=${MQTT_BROKER_ADDR}
    MQTT_BROKER_PORT=${MQTT_BROKER_PORT}
    WIFI_SSID=${WIFI_SSID}
    WIFI_PSK=${WIFI_PSK}
    # Suppress Zephyr-specific headers that the sim doesn't have
    COURIER_SIMULATION=1
)

# =============================================================================
# Find MQTT client library for the simulation.
# Use Mosquitto's libmosquitto or Paho MQTT C for the desktop build.
# The Zephyr build uses CONFIG_MQTT_LIB instead.
# =============================================================================
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(MOSQUITTO libmosquitto)
endif()

if(MOSQUITTO_FOUND)
    target_link_libraries(courier_sim PRIVATE ${MOSQUITTO_LIBRARIES})
    target_include_directories(courier_sim PRIVATE ${MOSQUITTO_INCLUDE_DIRS})
    target_compile_definitions(courier_sim PRIVATE MQTT_BACKEND_MOSQUITTO=1)
    message(STATUS "Using libmosquitto for simulation MQTT")
else()
    message(WARNING
        "libmosquitto not found. "
        "MQTT publish/subscribe will be no-ops in simulation. "
        "Install: sudo apt install libmosquitto-dev  (or brew install mosquitto)")
    target_compile_definitions(courier_sim PRIVATE MQTT_BACKEND_STUB=1)
endif()

target_link_libraries(courier_sim PRIVATE
    Qt5::Core
    Qt5::Widgets
    pthread
)

# =============================================================================
# Zephyr type stubs header
# The C source files include <zephyr/kernel.h> etc. In the simulation we
# provide a minimal stub header that satisfies the includes without Zephyr SDK.
# =============================================================================
target_compile_definitions(courier_sim PRIVATE
    # Tell mqtt_handler.c and courier_state.c to use stub types
    ZEPHYR_STUB_TYPES=1
)
