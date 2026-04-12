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

set(SOURCES1
    ../src/authentication.c
    authentication_test.c
)

set(SOURCES2
    ../src/inventory.c
    inventory_test.c
)

set(SOURCES3
    ../src/listener.c
    listener_test.c
)

set(SOURCES4
    ../src/orders.c
    orders_test.c
)

set(SOURCES5
    ../src/warehouse.c
    warehouse_test.c
)

# Add executable
add_executable(authentication_test ${SOURCES1})
add_executable(inventory_test ${SOURCES2})
add_executable(listener_wh_test ${SOURCES3})
add_executable(orders_wh_test ${SOURCES4})
add_executable(warehouse_test ${SOURCES5})

# Define the TESTING macro
target_compile_definitions(authentication_test PRIVATE TESTING)
target_compile_definitions(inventory_test PRIVATE TESTING)
target_compile_definitions(listener_wh_test PRIVATE TESTING)
target_compile_definitions(orders_wh_test PRIVATE TESTING)
target_compile_definitions(warehouse_test PRIVATE TESTING)

# Link libraries
target_link_libraries(authentication_test PRIVATE cjson::cjson unity::unity gcov)
target_link_libraries(inventory_test PRIVATE cjson::cjson unity::unity gcov)
target_link_libraries(listener_wh_test PRIVATE cjson::cjson unity::unity gcov)
target_link_libraries(orders_wh_test PRIVATE cjson::cjson unity::unity gcov)
target_link_libraries(warehouse_test PRIVATE cjson::cjson unity::unity gcov)

# Add the test executable to CTest
add_test(NAME authentication_test COMMAND authentication_test)
add_test(NAME inventory_test COMMAND inventory_test)
add_test(NAME listener_wh_test COMMAND listener_wh_test)
add_test(NAME orders_wh_test COMMAND orders_wh_test)
add_test(NAME warehouse_test COMMAND warehouse_test)
