# Verify cmake version
cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

# Create project
project (Paranoid-Linux VERSION 1.0.0 DESCRIPTION "Internal-Supply-System" LANGUAGES C)

# Coverage and tests are enabled from CI via -DRUN_COVERAGE=TRUE.
option(RUN_COVERAGE "Enable tests and coverage targets" OFF)

if(RUN_COVERAGE)
	include(CTest)
	enable_testing()
endif()

# Add subdirectories
add_subdirectory(server)
add_subdirectory(hub)
add_subdirectory(warehouse)
