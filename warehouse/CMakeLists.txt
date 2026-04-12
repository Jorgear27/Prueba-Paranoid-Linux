# Verify cmake version
cmake_minimum_required(VERSION 3.25 FATAL_ERROR)

# Flags for compiling
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -Wall -Wpedantic -Werror -Wextra -Wunused-parameter -Wmissing-prototypes -Wstrict-prototypes")

# Create project
project (warehouse VERSION 1.0.0 DESCRIPTION "warehouse" LANGUAGES C)

# Include headers
include_directories(include)

# Find dependencies en Conan
find_package(cJSON REQUIRED)
find_package(unity REQUIRED)

# Añadir los archivos fuente
set(SOURCES
    src/main.c
    src/authentication.c
    src/warehouse.c
    src/inventory.c
    src/listener.c
    src/orders.c
)
# add_executable --> .exe - binary
# add_library --> .a - .so - .dll STATIC SHARED (Librerias estáticas y dinámicas)
add_executable(${PROJECT_NAME} ${SOURCES})
target_link_libraries(${PROJECT_NAME} PRIVATE cjson::cjson unity::unity)

# Add tests
if (RUN_COVERAGE)
    enable_testing()
    add_subdirectory(tests)
endif()
