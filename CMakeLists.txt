# Verify cmake version
cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

# Create project
project (Paranoid-Linux VERSION 1.0.0 DESCRIPTION "Internal-Supply-System" LANGUAGES C)

# Configure test and coverage toggles.
option(RUN_COVERAGE "Enable tests and coverage targets" OFF)
include(CTest)

if(RUN_COVERAGE)
    set(BUILD_TESTING ON CACHE BOOL "Enable testing" FORCE)
endif()

# Add subdirectories
add_subdirectory(server)
add_subdirectory(hub)
add_subdirectory(warehouse)
