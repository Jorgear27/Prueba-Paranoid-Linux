#include "authentication.hpp"
#include "graph_router.hpp"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Return;

#define SOCKET_TEST 1

// ---------------------------------------------------------------------------
// Mocks
// ---------------------------------------------------------------------------
class MockDatabase : public Database
{
  public:
    MOCK_METHOD(bool, insertOrUpdateUser, (const std::string&, double, double), (override));
    MOCK_METHOD(bool, updateUserOnlineStatus, (const std::string&, bool), (override));
    MOCK_METHOD(bool, replaceConnections, (const std::string&, const nlohmann::json&), (override));
    MOCK_METHOD(nlohmann::json, buildGraphSnapshot, (), (override));
    MOCK_METHOD(bool, updateInventoryStockLevel, (const std::string&, int, int), (override));
};

class MockSender : public Sender
{
  public:
    MOCK_METHOD(void, removeConnection, (const std::string&), (override));
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
class AuthenticationManagerTest : public ::testing::Test
{
  protected:
    MockDatabase mockDb;
    MockSender mockSender;
    Authentication auth{mockDb, mockSender};

    // GraphRouter::getInstance() uses the real singleton DB, so we allow buildGraphSnapshot
    // any number of times and return an empty array to keep tests lightweight.
    void SetUp() override
    {
        EXPECT_CALL(mockDb, buildGraphSnapshot()).Times(AnyNumber()).WillRepeatedly(Return(nlohmann::json::array()));
    }
};

// Test for validation of client information missing latitude and longitude
TEST_F(AuthenticationManagerTest, MissingLatitudeLongitude)
{
    std::string invalidJson = R"({
        "type": "client_info",
        "timestamp": "2025-04-01T12:01:00Z",
        "hub_id": "H001",
        "location": {
            "latitude": null,
            "longitude": null
        }
    })";
    EXPECT_CALL(mockDb, insertOrUpdateUser("H001", 40.7128, -74.0060)).Times(0); // Must return failure before DB call
    std::string response = auth.processClientInfo(invalidJson, SOCKET_TEST);
    EXPECT_EQ(response, "{\"status\":\"error\",\"message\":\"Invalid client information\"}");
}

// Test for validation of client information latitude and longitude out of range
TEST_F(AuthenticationManagerTest, InvalidLatitudeLongitude)
{
    std::string invalidJson = R"({
        "type": "client_info",
        "timestamp": "2025-04-01T12:01:00Z",
        "hub_id": "H001",
        "location": {
            "latitude": 100.0,
            "longitude": -200.0
        }
    })";
    EXPECT_CALL(mockDb, insertOrUpdateUser("H001", 100.0, -200.0)).Times(0); // Must return failure before DB call
    std::string response = auth.processClientInfo(invalidJson, SOCKET_TEST);
    EXPECT_EQ(response, "{\"status\":\"error\",\"message\":\"Invalid client information\"}");
}

// Test for valid client information
TEST_F(AuthenticationManagerTest, ValidHubInfo)
{
    std::string validJson = R"({
        "type": "client_info",
        "timestamp": "2025-04-01T12:01:00Z",
        "hub_id": "H001",
        "location": {
            "latitude": 40.7128,
            "longitude": -74.0060
        }
    })";
    EXPECT_CALL(mockDb, insertOrUpdateUser("H001", 40.7128, -74.0060))
        .Times(1)
        .WillOnce(testing::Return(true)); // Simulate successful database operation
    std::string response = auth.processClientInfo(validJson, SOCKET_TEST);
    EXPECT_EQ(response, "{\"status\":\"success\",\"message\":\"Client information received and stored\"}");
}

// Test for valid client information
TEST_F(AuthenticationManagerTest, ValidWhInfo)
{
    std::string validJson = R"({
        "type": "client_info",
        "timestamp": "2025-04-01T12:01:00Z",
        "warehouse_id": "W001",
        "location": {
            "latitude": 20.3050,
            "longitude": -14.0527
        }
    })";
    EXPECT_CALL(mockDb, insertOrUpdateUser("W001", 20.3050, -14.0527))
        .Times(1)
        .WillOnce(testing::Return(true)); // Simulate successful database operation
    std::string response = auth.processClientInfo(validJson, SOCKET_TEST);
    EXPECT_EQ(response, "{\"status\":\"success\",\"message\":\"Client information received and stored\"}");
}

// Test for invalid client information (missing timestamp)
TEST_F(AuthenticationManagerTest, MissingTimestamp)
{
    std::string invalidJson = R"({
        "type": "client_info",
        "hub_id": "H001",
        "location": {
            "latitude": 40.7128,
            "longitude": -74.0060
        }
    })";
    EXPECT_CALL(mockDb, insertOrUpdateUser("H001", 40.7128, -74.0060)).Times(0); // Must return failure before DB call
    std::string response = auth.processClientInfo(invalidJson, SOCKET_TEST);
    EXPECT_EQ(response, "{\"status\":\"error\",\"message\":\"Invalid client information\"}");
}

// Test for invalid client information (missing hub_id/warehouse_id)
TEST_F(AuthenticationManagerTest, MissingClientId)
{
    std::string invalidJson = R"({
        "type": "client_info",
        "timestamp": "2025-04-01T12:01:00Z",
        "location": {
            "latitude": 40.7128,
            "longitude": -74.0060
        }
    })";
    EXPECT_CALL(mockDb, insertOrUpdateUser("H001", 40.7128, -74.0060)).Times(0); // Must return failure before DB call
    std::string response = auth.processClientInfo(invalidJson, SOCKET_TEST);
    EXPECT_EQ(response, "{\"status\":\"error\",\"message\":\"Invalid client information\"}");
}

// Test for invalid location (missing array)
TEST_F(AuthenticationManagerTest, MissingLocation)
{
    std::string invalidJson = R"({
        "type": "client_info",
        "timestamp": "2025-04-01T12:01:00Z",
        "hub_id": "H001"
    })";
    EXPECT_CALL(mockDb, insertOrUpdateUser("H001", 40.7128, -74.0060)).Times(0); // Must return failure before DB call
    std::string response = auth.processClientInfo(invalidJson, SOCKET_TEST);
    EXPECT_EQ(response, "{\"status\":\"error\",\"message\":\"Invalid client information\"}");
}

// Test for invalid JSON format
TEST_F(AuthenticationManagerTest, InvalidJsonFormat)
{
    std::string invalidJson = R"({
        "type": "client_info",
        "timestamp": "2025-04-01T12:01:00Z",
        "hub_id": "H001",
        "location": {
            "latitude": "invalid_latitude",
            "longitude": -74.0060
        }
    })";
    EXPECT_CALL(mockDb, insertOrUpdateUser("H001", 40.7128, -74.0060)).Times(0); // Must return failure before DB call
    std::string response = auth.processClientInfo(invalidJson, SOCKET_TEST);
    EXPECT_THAT(response, ::testing::HasSubstr("Invalid JSON format or missing fields"));
}

// Test for database retries and failure
TEST_F(AuthenticationManagerTest, DatabaseRetriesAndFails)
{
    std::string validJson = R"({
        "type": "client_info",
        "timestamp": "2025-04-01T12:01:00Z",
        "hub_id": "H001",
        "location": {
            "latitude": 40.7128,
            "longitude": -74.0060
        }
    })";

    EXPECT_CALL(mockDb, insertOrUpdateUser("H001", 40.7128, -74.0060))
        .Times(3)
        .WillRepeatedly(testing::Return(false)); // Simulate failure for all retries

    std::string response = auth.processClientInfo(validJson, SOCKET_TEST);
    EXPECT_EQ(response, "{\"status\":\"error\",\"message\":\"Failed to process new client after retries\"}");
}

// Test for database success after failure
TEST_F(AuthenticationManagerTest, DatabaseFailsThenSucceeds)
{
    std::string validJson = R"({
        "type": "client_info",
        "timestamp": "2025-04-01T12:01:00Z",
        "hub_id": "H001",
        "location": {
            "latitude": 40.7128,
            "longitude": -74.0060
        }
    })";

    EXPECT_CALL(mockDb, insertOrUpdateUser("H001", 40.7128, -74.0060))
        .WillOnce(testing::Return(false))
        .WillOnce(testing::Return(false))
        .WillOnce(testing::Return(true));

    std::string response = auth.processClientInfo(validJson, SOCKET_TEST);
    EXPECT_EQ(response, "{\"status\":\"success\",\"message\":\"Client information received and stored\"}");
}

TEST_F(AuthenticationManagerTest, HandleClientDisconnectionSuccess)
{
    std::string jsonData = R"({
        "user_id": "H001",
        "timestamp": "2025-04-01T12:01:00Z"
    })";

    // Expect the database to update the user's online status successfully
    EXPECT_CALL(mockDb, updateUserOnlineStatus("H001", false)).Times(1).WillOnce(testing::Return(true));

    // Expect the Sender to remove the connection
    EXPECT_CALL(mockSender, removeConnection("H001")).Times(1);

    // Call the method
    auth.handleClientDisconnection(jsonData, SOCKET_TEST);
}

TEST_F(AuthenticationManagerTest, HandleClientDisconnectionDatabaseFailure)
{
    std::string jsonData = R"({
        "user_id": "H001",
        "timestamp": "2025-04-01T12:01:00Z"
    })";

    // Simulate a failure in updating the user's online status
    EXPECT_CALL(mockDb, updateUserOnlineStatus("H001", false)).Times(1).WillOnce(testing::Return(false));

    // Expect the Sender to still remove the connection
    EXPECT_CALL(mockSender, removeConnection("H001")).Times(1);

    // Call the method
    auth.handleClientDisconnection(jsonData, SOCKET_TEST);
}

// ===========================================================================
// processClientInfo — connections field
// ===========================================================================

TEST_F(AuthenticationManagerTest, ProcessClientInfo_WithConnections_CallsReplaceConnections)
{
    const std::string json = R"({
        "type": "client_info",
        "timestamp": "2025-07-15T10:00:00Z",
        "warehouse_id": "W001",
        "is_secure": true,
        "location": { "latitude": 40.71, "longitude": -74.00 },
        "connections": [
            { "target_node_id": "H001", "base_weight": 5.0,
              "connection_type": "road", "connection_conditions": [] }
        ]
    })";

    EXPECT_CALL(mockDb, insertOrUpdateUser("W001", 40.71, -74.00)).WillOnce(Return(true));
    EXPECT_CALL(mockDb, replaceConnections("W001", _)).Times(1).WillOnce(Return(true));

    const std::string resp = auth.processClientInfo(json, SOCKET_TEST);
    EXPECT_EQ(resp, "{\"status\":\"success\",\"message\":\"Client information received and stored\"}");
}

TEST_F(AuthenticationManagerTest, ProcessClientInfo_WithoutConnections_DoesNotCallReplaceConnections)
{
    // Backward-compatible: no "connections" field → replaceConnections not called.
    const std::string json = R"({
        "type": "client_info",
        "timestamp": "2025-07-15T10:00:00Z",
        "hub_id": "H001",
        "location": { "latitude": 40.80, "longitude": -73.95 }
    })";

    EXPECT_CALL(mockDb, insertOrUpdateUser("H001", 40.80, -73.95)).WillOnce(Return(true));
    EXPECT_CALL(mockDb, replaceConnections(_, _)).Times(0);

    const std::string resp = auth.processClientInfo(json, SOCKET_TEST);
    EXPECT_EQ(resp, "{\"status\":\"success\",\"message\":\"Client information received and stored\"}");
}

TEST_F(AuthenticationManagerTest, ProcessClientInfo_EmptyConnectionsArray_DoesNotCallReplace)
{
    // connections: [] is an empty array — skip replaceConnections to avoid
    // deleting existing edges when none are provided.
    const std::string json = R"({
        "type": "client_info",
        "timestamp": "2025-07-15T10:00:00Z",
        "warehouse_id": "W001",
        "location": { "latitude": 40.71, "longitude": -74.00 },
        "connections": []
    })";

    EXPECT_CALL(mockDb, insertOrUpdateUser("W001", 40.71, -74.00)).WillOnce(Return(true));
    EXPECT_CALL(mockDb, replaceConnections(_, _)).Times(0);

    const std::string resp = auth.processClientInfo(json, SOCKET_TEST);
    EXPECT_EQ(resp, "{\"status\":\"success\",\"message\":\"Client information received and stored\"}");
}

TEST_F(AuthenticationManagerTest, ProcessClientInfo_ConnectionStorageFails_StillReturnsSuccess)
{
    // Non-fatal: replaceConnections failure should not block the auth response.
    const std::string json = R"({
        "type": "client_info",
        "timestamp": "2025-07-15T10:00:00Z",
        "warehouse_id": "W001",
        "location": { "latitude": 40.71, "longitude": -74.00 },
        "connections": [
            { "target_node_id": "H001", "base_weight": 5.0,
              "connection_type": "road", "connection_conditions": [] }
        ]
    })";

    EXPECT_CALL(mockDb, insertOrUpdateUser("W001", 40.71, -74.00)).WillOnce(Return(true));
    EXPECT_CALL(mockDb, replaceConnections("W001", _)).WillOnce(Return(false)); // Storage fails.

    // Auth response should still succeed — edges are non-fatal.
    const std::string resp = auth.processClientInfo(json, SOCKET_TEST);
    EXPECT_EQ(resp, "{\"status\":\"success\",\"message\":\"Client information received and stored\"}");
}

TEST_F(AuthenticationManagerTest, ProcessClientInfo_MultipleConnections_PassedToReplace)
{
    const std::string json = R"({
        "type": "client_info",
        "timestamp": "2025-07-15T10:00:00Z",
        "warehouse_id": "W001",
        "location": { "latitude": 40.71, "longitude": -74.00 },
        "connections": [
            { "target_node_id": "H001", "base_weight": 5.0,
              "connection_type": "road", "connection_conditions": [] },
            { "target_node_id": "W002", "base_weight": 10.0,
              "connection_type": "rail", "connection_conditions": ["foggy"] }
        ]
    })";

    EXPECT_CALL(mockDb, insertOrUpdateUser("W001", 40.71, -74.00)).WillOnce(Return(true));

    nlohmann::json capturedConnections;
    EXPECT_CALL(mockDb, replaceConnections("W001", _))
        .WillOnce(::testing::DoAll(::testing::SaveArg<1>(&capturedConnections), Return(true)));

    auth.processClientInfo(json, SOCKET_TEST);

    ASSERT_EQ(capturedConnections.size(), 2u);
    EXPECT_EQ(capturedConnections[0]["target_node_id"], "H001");
    EXPECT_EQ(capturedConnections[1]["target_node_id"], "W002");
}

// ===========================================================================
// handleClientDisconnection — graph refresh on disconnect
// ===========================================================================

TEST_F(AuthenticationManagerTest, HandleDisconnect_UpdatesOnlineStatusAndRemovesConnection)
{
    const std::string json = R"({
        "user_id": "W001",
        "timestamp": "2025-07-15T10:05:00Z"
    })";

    EXPECT_CALL(mockDb, updateUserOnlineStatus("W001", false)).WillOnce(Return(true));
    EXPECT_CALL(mockSender, removeConnection("W001")).Times(1);

    auth.handleClientDisconnection(json, SOCKET_TEST);
}

TEST_F(AuthenticationManagerTest, HandleDisconnect_DatabaseFailure_StillRemovesConnection)
{
    const std::string json = R"({
        "user_id": "H001",
        "timestamp": "2025-07-15T10:05:00Z"
    })";

    EXPECT_CALL(mockDb, updateUserOnlineStatus("H001", false)).WillOnce(Return(false)); // DB fails.
    EXPECT_CALL(mockSender, removeConnection("H001")).Times(1);

    // Should not throw even when DB update fails.
    EXPECT_NO_THROW(auth.handleClientDisconnection(json, SOCKET_TEST));
}
