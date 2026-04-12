#include "server.hpp"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

// Mock classes for dependencies
class MockDatabase : public Database
{
  public:
    MOCK_METHOD(bool, insertOrUpdateUser, (const std::string& userId, double latitude, double longitude), (override));
    MOCK_METHOD(bool, updateUserOnlineStatus, (const std::string& userId, bool isOnline), (override));
};

class MockSender : public Sender
{
  public:
    MOCK_METHOD(void, removeConnection, (const std::string& userId), (override));
};

class MockLogger : public Logger
{
  public:
    MOCK_METHOD(void, log, (const std::string& tag, const std::string& message), (override));
};

class MockRequestRouter : public RequestRouter
{
  public:
    MockRequestRouter(Authentication& auth, InventoryManager& inventoryManager, OrderManager& orderManager)
        : RequestRouter(auth, inventoryManager, orderManager)
    {
    }

    MOCK_METHOD(std::string, routeRequest, (const std::string& request, int clientSocket), (override));
};

// Test fixture for the Server class
class ServerTest : public ::testing::Test
{
  protected:
    MockLogger mockLogger;
    MockDatabase mockDb;
    MockSender mockSender;
    Authentication mockAuth{mockDb, mockSender}; // Pass mock dependencies to Authentication
    InventoryManager mockInventoryManager;
    OrderManager mockOrderManager;
    MockRequestRouter mockRouter;
    Server server;

    ServerTest()
        : mockRouter(mockAuth, mockInventoryManager, mockOrderManager), // Initialize MockRequestRouter
          server(mockLogger, mockRouter, mockOrderManager)              // Pass shared OrderManager to Server
    {
    }
};

// Test the Server::initialize method
TEST_F(ServerTest, InitializeSuccess)
{
    EXPECT_CALL(mockLogger, log("Server", "[INFO] Initializing server...")).Times(1);
    EXPECT_CALL(mockLogger, log("Server", "[INFO] Server initialized on port 8080")).Times(1);

    bool result = server.initialize();
    EXPECT_TRUE(result);
}

TEST_F(ServerTest, RunServer)
{
    // Expect the logger to log the server start and stop messages
    EXPECT_CALL(mockLogger, log("Server", "[INFO] Server is running and listening for connections...")).Times(1);
    EXPECT_CALL(mockLogger, log("Server", "[INFO] Stopping server...")).Times(1);

    // Run the server in a separate thread and stop it immediately
    std::thread serverThread([&]() { server.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    server.stop();
    serverThread.join();
}

TEST_F(ServerTest, HandleClient)
{
    int mockClientSocket[2]; // Use a socket pair to simulate client-server communication
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, mockClientSocket), 0) << "Failed to create socket pair";

    const char* mockRequest = "{\"action\":\"test\"}";
    const char* mockResponse = "{\"status\":\"success\"}";

    // Set expectation on the MockRequestRouter
    EXPECT_CALL(mockRouter, routeRequest(mockRequest, mockClientSocket[1])).WillOnce(::testing::Return(mockResponse));

    // Simulate client handling in a separate thread
    std::thread clientThread([&]() { server.handleClient(mockClientSocket[1]); });

    // Simulate sending data to the server
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    write(mockClientSocket[0], mockRequest, strlen(mockRequest)); // Send mock request to the server

    char buffer[BUFFER_SIZE] = {0};
    read(mockClientSocket[0], buffer, sizeof(buffer)); // Read the response from the server
    EXPECT_STREQ(buffer, mockResponse);                // Verify the response

    server.stop();
    clientThread.join();

    close(mockClientSocket[0]);
    close(mockClientSocket[1]);
}

// Test the Server::stop method
TEST_F(ServerTest, StopServer)
{
    EXPECT_CALL(mockLogger, log("Server", "[INFO] Stopping server...")).Times(1);

    server.stop();
    EXPECT_FALSE(server.isRunning());
}

TEST_F(ServerTest, HandleClientDisconnection)
{
    int mockClientSocket[2]; // Use a socket pair to simulate client-server communication
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, mockClientSocket), 0) << "Failed to create socket pair";

    // Expect the logger to log the client disconnection
    EXPECT_CALL(mockLogger, log("Server", ::testing::StartsWith("[INFO] Client disconnected:"))).Times(1);

    // Simulate client handling in a separate thread
    std::thread clientThread([&]() { server.handleClient(mockClientSocket[1]); });

    close(mockClientSocket[0]); // Simulate client disconnecting
    clientThread.join();

    close(mockClientSocket[1]);
}

TEST_F(ServerTest, HandleClientDisconnectResponse)
{
    int mockClientSocket[2]; // Use a socket pair to simulate client-server communication
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, mockClientSocket), 0) << "Failed to create socket pair";

    const char* mockRequest = "{\"action\":\"disconnect\"}";
    const char* mockResponse =
        "{\"order\":\"disconnect\",\"status\":\"success\",\"message\":\"Disconnect request processed\"}";

    // Set expectation on the MockRequestRouter
    EXPECT_CALL(mockRouter, routeRequest(mockRequest, mockClientSocket[1])).WillOnce(::testing::Return(mockResponse));

    // Expect the logger to log the disconnecting client message
    EXPECT_CALL(mockLogger, log("Server", ::testing::StartsWith("[INFO] Disconnecting client:"))).Times(1);

    // Simulate client handling in a separate thread
    std::thread clientThread([&]() { server.handleClient(mockClientSocket[1]); });

    // Simulate sending data to the server
    write(mockClientSocket[0], mockRequest, strlen(mockRequest)); // Send mock request to the server

    clientThread.join();

    close(mockClientSocket[0]);
    close(mockClientSocket[1]);
}

TEST_F(ServerTest, HandleClientRouteRequestException)
{
    int mockClientSocket[2]; // Use a socket pair to simulate client-server communication
    ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, mockClientSocket), 0) << "Failed to create socket pair";

    const char* mockRequest = "{\"action\":\"invalid\"}";
    const char* errorResponse = "{\"status\":\"error\",\"message\":\"Invalid request\"}";

    // Set expectation on the MockRequestRouter to throw an exception
    EXPECT_CALL(mockRouter, routeRequest(mockRequest, mockClientSocket[1]))
        .WillOnce(::testing::Throw(std::runtime_error("Invalid request")));

    // Simulate client handling in a separate thread
    std::thread clientThread([&]() { server.handleClient(mockClientSocket[1]); });

    // Simulate sending data to the server
    write(mockClientSocket[0], mockRequest, strlen(mockRequest)); // Send mock request to the server

    char buffer[BUFFER_SIZE] = {0};
    read(mockClientSocket[0], buffer, sizeof(buffer)); // Read the response from the server
    EXPECT_STREQ(buffer, errorResponse);               // Verify the error response

    clientThread.join();

    close(mockClientSocket[0]);
    close(mockClientSocket[1]);
}
