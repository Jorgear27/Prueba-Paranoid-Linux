# Verify cmake version
cmake_minimum_required(VERSION 3.15)

# Set flags for coverage
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage -fprofile-arcs -ftest-coverage")

# Orders tests
add_executable(orders_server_test
  orders_test.cpp
  mongo_repository_stub.cpp
  ${CMAKE_SOURCE_DIR}/server/src/orders.cpp
  ${CMAKE_SOURCE_DIR}/server/src/log.cpp
  ${CMAKE_SOURCE_DIR}/server/src/database.cpp
  ${CMAKE_SOURCE_DIR}/server/src/sender.cpp
  ${CMAKE_SOURCE_DIR}/server/src/inventory.cpp
  ${CMAKE_SOURCE_DIR}/server/src/graph_router.cpp
  ${CMAKE_SOURCE_DIR}/server/src/graph.cpp
  ${CMAKE_SOURCE_DIR}/server/src/bellman_ford.cpp
  ${CMAKE_SOURCE_DIR}/server/src/ford_fulkerson.cpp
  ${CMAKE_SOURCE_DIR}/server/src/kaufmann_malgrange.cpp
)
target_link_libraries(orders_server_test PostgreSQL::PostgreSQL gtest::gtest nlohmann_json::nlohmann_json)
add_test(NAME orders_server_test COMMAND orders_server_test)

# Request Router tests
add_executable(request_router_test
  request_router_test.cpp
  mongo_repository_stub.cpp
  ${CMAKE_SOURCE_DIR}/server/src/orders.cpp
  ${CMAKE_SOURCE_DIR}/server/src/authentication.cpp
  ${CMAKE_SOURCE_DIR}/server/src/inventory.cpp
  ${CMAKE_SOURCE_DIR}/server/src/log.cpp
  ${CMAKE_SOURCE_DIR}/server/src/request_router.cpp
  ${CMAKE_SOURCE_DIR}/server/src/database.cpp
  ${CMAKE_SOURCE_DIR}/server/src/sender.cpp
  ${CMAKE_SOURCE_DIR}/server/src/graph_router.cpp
  ${CMAKE_SOURCE_DIR}/server/src/graph.cpp
  ${CMAKE_SOURCE_DIR}/server/src/bellman_ford.cpp
  ${CMAKE_SOURCE_DIR}/server/src/ford_fulkerson.cpp
  ${CMAKE_SOURCE_DIR}/server/src/kaufmann_malgrange.cpp
)
target_link_libraries(request_router_test PostgreSQL::PostgreSQL gtest::gtest nlohmann_json::nlohmann_json)
add_test(NAME request_router_test COMMAND request_router_test)

# Sender tests
add_executable(sender_test
  sender_test.cpp
  ${CMAKE_SOURCE_DIR}/server/src/sender.cpp
)
target_link_libraries(sender_test gtest::gtest)
add_test(NAME sender_test COMMAND sender_test)

# Inventory tests
add_executable(inventory_server_test
  inventory_test.cpp
  mongo_repository_stub.cpp
  ${CMAKE_SOURCE_DIR}/server/src/inventory.cpp
  ${CMAKE_SOURCE_DIR}/server/src/log.cpp
  ${CMAKE_SOURCE_DIR}/server/src/database.cpp
  ${CMAKE_SOURCE_DIR}/server/src/sender.cpp
  ${CMAKE_SOURCE_DIR}/server/src/graph_router.cpp
  ${CMAKE_SOURCE_DIR}/server/src/graph.cpp
  ${CMAKE_SOURCE_DIR}/server/src/bellman_ford.cpp
  ${CMAKE_SOURCE_DIR}/server/src/ford_fulkerson.cpp
  ${CMAKE_SOURCE_DIR}/server/src/kaufmann_malgrange.cpp
)
target_link_libraries(inventory_server_test PostgreSQL::PostgreSQL gtest::gtest nlohmann_json::nlohmann_json)
add_test(NAME inventory_server_test COMMAND inventory_server_test)

# Database tests
add_executable(database_test
  database_test.cpp
  ${CMAKE_SOURCE_DIR}/server/src/database.cpp
  ${CMAKE_SOURCE_DIR}/server/src/log.cpp
)
target_link_libraries(database_test PostgreSQL::PostgreSQL gtest::gtest nlohmann_json::nlohmann_json)
add_test(NAME database_test COMMAND database_test)

# Server tests
add_executable(server_test
  server_test.cpp
  mongo_repository_stub.cpp
  ${CMAKE_SOURCE_DIR}/server/src/server.cpp
  ${CMAKE_SOURCE_DIR}/server/src/log.cpp
  ${CMAKE_SOURCE_DIR}/server/src/request_router.cpp
  ${CMAKE_SOURCE_DIR}/server/src/orders.cpp
  ${CMAKE_SOURCE_DIR}/server/src/sender.cpp
  ${CMAKE_SOURCE_DIR}/server/src/inventory.cpp
  ${CMAKE_SOURCE_DIR}/server/src/database.cpp
  ${CMAKE_SOURCE_DIR}/server/src/authentication.cpp
  ${CMAKE_SOURCE_DIR}/server/src/graph_router.cpp
  ${CMAKE_SOURCE_DIR}/server/src/graph.cpp
  ${CMAKE_SOURCE_DIR}/server/src/bellman_ford.cpp
  ${CMAKE_SOURCE_DIR}/server/src/ford_fulkerson.cpp
  ${CMAKE_SOURCE_DIR}/server/src/kaufmann_malgrange.cpp
)
target_compile_definitions(server_test PRIVATE TESTING)
target_link_libraries(server_test PostgreSQL::PostgreSQL gtest::gtest nlohmann_json::nlohmann_json)
add_test(NAME server_test COMMAND server_test)

# Authentication tests
add_executable(auth_test_server
  authentication_test.cpp
  mongo_repository_stub.cpp
  ${CMAKE_SOURCE_DIR}/server/src/authentication.cpp
  ${CMAKE_SOURCE_DIR}/server/src/database.cpp
  ${CMAKE_SOURCE_DIR}/server/src/log.cpp
  ${CMAKE_SOURCE_DIR}/server/src/sender.cpp
  ${CMAKE_SOURCE_DIR}/server/src/graph_router.cpp
  ${CMAKE_SOURCE_DIR}/server/src/graph.cpp
  ${CMAKE_SOURCE_DIR}/server/src/bellman_ford.cpp
  ${CMAKE_SOURCE_DIR}/server/src/ford_fulkerson.cpp
  ${CMAKE_SOURCE_DIR}/server/src/kaufmann_malgrange.cpp
)
target_compile_definitions(auth_test_server PRIVATE TESTING)
target_link_libraries(auth_test_server PostgreSQL::PostgreSQL gtest::gtest nlohmann_json::nlohmann_json)
add_test(NAME auth_test_server COMMAND auth_test_server)

# Graph tests
add_executable(graph_test
  graph_test.cpp
  ${CMAKE_SOURCE_DIR}/server/src/graph.cpp
  ${CMAKE_SOURCE_DIR}/server/src/log.cpp
)
target_link_libraries(graph_test gtest::gtest nlohmann_json::nlohmann_json)
add_test(NAME graph_test COMMAND graph_test)

# Bellman-Ford tests
add_executable(bellman_ford_test
  bellman_ford_test.cpp
  ${CMAKE_SOURCE_DIR}/server/src/bellman_ford.cpp
  ${CMAKE_SOURCE_DIR}/server/src/graph.cpp
  ${CMAKE_SOURCE_DIR}/server/src/log.cpp
)
target_link_libraries(bellman_ford_test gtest::gtest nlohmann_json::nlohmann_json)
add_test(NAME bellman_ford_test COMMAND bellman_ford_test)

# Ford-Fulkerson tests
add_executable(ford_fulkerson_test
  ford_fulkerson_test.cpp
  ${CMAKE_SOURCE_DIR}/server/src/ford_fulkerson.cpp
  ${CMAKE_SOURCE_DIR}/server/src/graph.cpp
  ${CMAKE_SOURCE_DIR}/server/src/log.cpp
)
target_link_libraries(ford_fulkerson_test gtest::gtest nlohmann_json::nlohmann_json)
add_test(NAME ford_fulkerson_test COMMAND ford_fulkerson_test)

# Kaufmann-Malgrange tests
add_executable(kaufmann_malgrange_test
  kaufmann_malgrange_test.cpp
  ${CMAKE_SOURCE_DIR}/server/src/kaufmann_malgrange.cpp
  ${CMAKE_SOURCE_DIR}/server/src/graph.cpp
  ${CMAKE_SOURCE_DIR}/server/src/log.cpp
)
target_link_libraries(kaufmann_malgrange_test gtest::gtest nlohmann_json::nlohmann_json)
add_test(NAME kaufmann_malgrange_test COMMAND kaufmann_malgrange_test)

# GraphRouter tests (mock-based, no mongocxx driver)
add_executable(graph_router_test
  graph_router_test.cpp
  mongo_repository_stub.cpp
  ${CMAKE_SOURCE_DIR}/server/src/graph_router.cpp
  ${CMAKE_SOURCE_DIR}/server/src/database.cpp
  ${CMAKE_SOURCE_DIR}/server/src/graph.cpp
  ${CMAKE_SOURCE_DIR}/server/src/bellman_ford.cpp
  ${CMAKE_SOURCE_DIR}/server/src/ford_fulkerson.cpp
  ${CMAKE_SOURCE_DIR}/server/src/kaufmann_malgrange.cpp
  ${CMAKE_SOURCE_DIR}/server/src/log.cpp
)
target_link_libraries(graph_router_test PostgreSQL::PostgreSQL gtest::gtest nlohmann_json::nlohmann_json)
add_test(NAME graph_router_test COMMAND graph_router_test)

# MongoRepository tests (mock-based, no mongocxx driver)
add_executable(mongo_repository_test
  mongo_repository_test.cpp
  mongo_repository_stub.cpp
  ${CMAKE_SOURCE_DIR}/server/src/log.cpp
)
target_link_libraries(mongo_repository_test gtest::gtest nlohmann_json::nlohmann_json)
add_test(NAME mongo_repository_test COMMAND mongo_repository_test)

# Integration test for Database methods added in this feature.
add_executable(database_integration_test
  database_integration_test.cpp
  mongo_repository_stub.cpp
  ${CMAKE_SOURCE_DIR}/server/src/database.cpp
  ${CMAKE_SOURCE_DIR}/server/src/log.cpp
  ${CMAKE_SOURCE_DIR}/server/src/graph.cpp
)
target_link_libraries(database_integration_test PostgreSQL::PostgreSQL gtest::gtest nlohmann_json::nlohmann_json)
add_test(NAME database_integration_test COMMAND database_integration_test)
