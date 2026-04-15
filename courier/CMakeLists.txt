cmake_minimum_required(VERSION 3.20.0)

# Find Zephyr before the project() call so the toolchain is set correctly.
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(courier_rugged VERSION 1.0.0 LANGUAGES C)

# =============================================================================
# Source files
# =============================================================================
target_sources(app PRIVATE
    src/main.c
    src/mqtt_handler.c
    src/gps_task.c
    src/courier_state.c
    src/ui/ui.c
)

# =============================================================================
# Include directories
# =============================================================================
target_include_directories(app PRIVATE
    src
    src/ui
)

# =============================================================================
# Compile definitions
# =============================================================================
# Set at build time: employee ID, WiFi credentials, broker address.
# Override via: cmake -DEMPLOYEE_ID='"E042"' ...
# In the future these come from NVS provisioning, not compile-time constants.
if(NOT DEFINED EMPLOYEE_ID)
    set(EMPLOYEE_ID "\"E001\"")
endif()
if(NOT DEFINED WIFI_SSID)
    set(WIFI_SSID "\"DHL_Logistics\"")
endif()
if(NOT DEFINED WIFI_PSK)
    set(WIFI_PSK "\"changeme\"")
endif()
if(NOT DEFINED MQTT_BROKER_ADDR)
    set(MQTT_BROKER_ADDR "\"192.168.1.100\"")   # RabbitMQ host (host-gateway)
endif()
if(NOT DEFINED MQTT_BROKER_PORT)
    set(MQTT_BROKER_PORT 1883)
endif()

target_compile_definitions(app PRIVATE
    EMPLOYEE_ID=${EMPLOYEE_ID}
    WIFI_SSID=${WIFI_SSID}
    WIFI_PSK=${WIFI_PSK}
    MQTT_BROKER_ADDR=${MQTT_BROKER_ADDR}
    MQTT_BROKER_PORT=${MQTT_BROKER_PORT}
)
