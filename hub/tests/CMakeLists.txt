#CMake for Test
cmake_minimum_required(VERSION 3.10)

# Set compiler and linker flags for coverage
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -Wall -Wpedantic -Werror -Wextra -Wunused-parameter -Wmissing-prototypes -Wstrict-prototypes -fprofile-arcs -ftest-coverage")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-arcs -ftest-coverage")
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# CTest
include(CTest)

# Añadir los archivos fuente
set(SOURCES1
    ../src/hub.c
    ../src/orders.c
    hub_test.c
)

# Añadir los archivos fuente
set(SOURCES2
    ../src/listener.c
    listener_test.c
)

set(SOURCES3
    ../src/orders.c
    orders_test.c
)

set(SOURCES4
    ../src/authentication.c
    authentication_test.c
)

# Add executable
add_executable(hub_test ${SOURCES1})
add_executable(listener_test ${SOURCES2})
add_executable(orders_test ${SOURCES3})
add_executable(authentication_hub_test ${SOURCES4})

# Define the TESTING macro
target_compile_definitions(hub_test PRIVATE TESTING)
target_compile_definitions(listener_test PRIVATE TESTING)
target_compile_definitions(orders_test PRIVATE TESTING)
target_compile_definitions(authentication_hub_test PRIVATE TESTING)

# Link libraries
target_link_libraries(hub_test PRIVATE cjson::cjson unity::unity gcov)
target_link_libraries(listener_test PRIVATE cjson::cjson unity::unity gcov)
target_link_libraries(orders_test PRIVATE cjson::cjson unity::unity gcov)
target_link_libraries(authentication_hub_test PRIVATE cjson::cjson unity::unity gcov)

# Add the test executable to CTest
add_test(NAME hub_test COMMAND hub_test)
add_test(NAME listener_test COMMAND listener_test)
add_test(NAME orders_test COMMAND orders_test)
add_test(NAME authentication_hub_test COMMAND authentication_hub_test)
