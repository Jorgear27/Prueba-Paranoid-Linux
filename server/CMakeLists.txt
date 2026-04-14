# Verify cmake version
cmake_minimum_required(VERSION 3.15)

# Create project
project (server VERSION 1.0.0 DESCRIPTION "server" LANGUAGES CXX)

# Set compiler and linker flags
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic -Werror")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O2")
set(CMAKE_CXX_FLAGS_MINSIZEREL "${CMAKE_CXX_FLAGS_MINSIZEREL} -Os")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")

# Include headers
include_directories(include)

# Add the source files
add_executable(server
    src/main.cpp
    src/server.cpp
    src/authentication.cpp
    src/inventory.cpp
    src/request_router.cpp
    src/orders.cpp
    src/log.cpp
    src/database.cpp
    src/sender.cpp
    src/graph.cpp
    src/bellman_ford.cpp
    src/ford_fulkerson.cpp
    src/kaufmann_malgrange.cpp
    src/graph_router.cpp
    src/http_server.cpp
    src/mongo_repository.cpp
)

# Find dependencies en Conan
find_package(PostgreSQL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(GTest REQUIRED)
find_package(mongocxx REQUIRED)

# Resolve mongocxx target name across Conan/system package variants.
set(MONGO_CXX_TARGET "")
if(TARGET mongo::mongocxx_static)
    set(MONGO_CXX_TARGET mongo::mongocxx_static)
elseif(TARGET mongocxx::mongocxx)
    set(MONGO_CXX_TARGET mongocxx::mongocxx)
elseif(TARGET mongo::mongocxx_shared)
    set(MONGO_CXX_TARGET mongo::mongocxx_shared)
endif()

if(MONGO_CXX_TARGET STREQUAL "")
    message(FATAL_ERROR "Could not find a mongocxx CMake target. Tried: mongo::mongocxx_static, mongocxx::mongocxx, mongo::mongocxx_shared")
endif()

# Link dependencies
target_link_libraries(server PostgreSQL::PostgreSQL nlohmann_json::nlohmann_json ${MONGO_CXX_TARGET})

# Add tests
if (BUILD_TESTING)
    add_subdirectory(tests)
endif()
