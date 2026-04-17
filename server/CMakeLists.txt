cmake_minimum_required(VERSION 3.15)
project(server VERSION 1.0.0 DESCRIPTION "server" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic -Werror")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG}   -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -O2")

include_directories(include)

# ─── OpenMP ───────────────────────────────────────────────────────────────────
find_package(OpenMP REQUIRED)
message(STATUS "OpenMP habilitado: ${OpenMP_CXX_VERSION}")

if(OpenMP_CXX_FOUND)
    message(STATUS "OpenMP habilitado")
else()
    message(WARNING "OpenMP NO encontrado — compilando sin paralelismo")
endif()

# ─── Servidor principal ───────────────────────────────────────────────────────
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
    src/profiling_endpoint.cpp
)

find_package(PostgreSQL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(GTest REQUIRED)
find_package(mongocxx REQUIRED)

set(MONGO_CXX_TARGET "")
if(TARGET mongo::mongocxx_static)
    set(MONGO_CXX_TARGET mongo::mongocxx_static)
elseif(TARGET mongocxx::mongocxx)
    set(MONGO_CXX_TARGET mongocxx::mongocxx)
elseif(TARGET mongo::mongocxx_shared)
    set(MONGO_CXX_TARGET mongo::mongocxx_shared)
endif()
if(MONGO_CXX_TARGET STREQUAL "")
    message(FATAL_ERROR "No se encontró un target CMake para mongocxx.")
endif()

target_link_libraries(server
    PostgreSQL::PostgreSQL
    nlohmann_json::nlohmann_json
    ${MONGO_CXX_TARGET}
)


# ─── Benchmark standalone (no requiere Mongo/PostgreSQL) ─────────────────────
find_package(nlohmann_json QUIET)

add_executable(benchmark_bf
    src/benchmark_bf.cpp
    src/bellman_ford.cpp
    src/graph.cpp
    src/log.cpp
)

target_include_directories(benchmark_bf PRIVATE include)
target_link_libraries(benchmark_bf nlohmann_json::nlohmann_json)

if(OpenMP_CXX_FOUND)
    target_link_libraries(server OpenMP::OpenMP_CXX)
    target_compile_definitions(server PRIVATE OPENMP_ENABLED)
    target_link_libraries(benchmark_bf OpenMP::OpenMP_CXX)
endif()

# ─── Tests ────────────────────────────────────────────────────────────────────
if(RUN_COVERAGE)
    enable_testing()
    add_subdirectory(tests)
endif()
