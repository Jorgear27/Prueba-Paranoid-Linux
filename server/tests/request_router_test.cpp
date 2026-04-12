#include "authentication.hpp"
#include "inventory.hpp"
#include "orders.hpp"
#include "request_router.hpp"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using ::testing::_;
using ::testing::Return;
using json = nlohmann::json;

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

class MockAuthentication : public Authentication
{
  public:
    MockAuthentication(Database& db, Sender& sender) : Authentication(db, sender)
    {
    }

    MOCK_METHOD(std::string, processClientInfo, (const std::string&, int), (override));
    MOCK_METHOD(void, handleClientDisconnection, (const std::string&, int), (override));
};

class MockInventoryManager : public InventoryManager
{
  public:
    MOCK_METHOD(void, handleInventoryUpdate, (const std::string&), (override));
    MOCK_METHOD(void, handleRestockNotice, (const std::string&), (override));
};

class MockOrderManager : public OrderManager
{
  public:
    MOCK_METHOD(void, handleNewOrder, (const std::string&), (override));
    MOCK_METHOD(void, handleOrderDispatch, (const std::string&), (override));
    MOCK_METHOD(void, deliveryUpdate, (const std::string&), (override));
    MOCK_METHOD(std::string, handleOrderStatusQuery, (const std::string&), (override));
    MOCK_METHOD(std::string, handleCancelation, (const std::string&), (override));
};

// Test fixture
class RequestRouterTest : public ::testing::Test
{
  protected:
    MockDatabase mockDb;
    MockSender mockSender;
    MockAuthentication mockAuth{mockDb, mockSender}; // Pass mock dependencies to MockAuthentication
    MockInventoryManager mockInventoryManager;
    MockOrderManager mockOrderManager;
    RequestRouter router;

    RequestRouterTest() : router(mockAuth, mockInventoryManager, mockOrderManager)
    {
    }
};

// Test for ClientInfo request
TEST_F(RequestRouterTest, RouteRequest_ClientInfo)
{
    std::string jsonData = R"({"type": "client_info"})";
    int sock = 1;

    EXPECT_CALL(mockAuth, processClientInfo(jsonData, sock)).WillOnce(Return(R"({"status": "success"})"));

    std::string response = router.routeRequest(jsonData, sock);
    EXPECT_EQ(response, R"({"status": "success"})");
}

// Test for InventoryUpdate request
TEST_F(RequestRouterTest, RouteRequest_InventoryUpdate)
{
    std::string jsonData = R"({"type": "inventory_update"})";

    EXPECT_CALL(mockInventoryManager, handleInventoryUpdate(jsonData));

    std::string response = router.routeRequest(jsonData, 0);

    // Parse the actual and expected responses into JSON objects
    json actualResponse = json::parse(response);
    json expectedResponse = json::parse(R"({"status":"success","message":"Inventory updated"})");

    EXPECT_EQ(actualResponse, expectedResponse); // Compare JSON objects
}

// Test for RestockNotice request
TEST_F(RequestRouterTest, RouteRequest_RestockNotice)
{
    std::string jsonData = R"({"type": "restock_notice"})";

    EXPECT_CALL(mockInventoryManager, handleRestockNotice(jsonData));

    std::string response = router.routeRequest(jsonData, 0);
    json actualResponse = json::parse(response);
    json expectedResponse = json::parse(R"({"status":"success","message":"Restock notice processed"})");

    EXPECT_EQ(actualResponse, expectedResponse);
}

// Test for OrderRequest request
TEST_F(RequestRouterTest, RouteRequest_OrderRequest)
{
    std::string jsonData = R"({"type": "order_request"})";

    EXPECT_CALL(mockOrderManager, handleNewOrder(jsonData));

    std::string response = router.routeRequest(jsonData, 0);
    json actualResponse = json::parse(response);
    json expectedResponse = json::parse(R"({"status":"success","message":"New order created"})");

    EXPECT_EQ(actualResponse, expectedResponse);
}

// Test for OrderDispatch request
TEST_F(RequestRouterTest, RouteRequest_OrderDispatch)
{
    std::string jsonData = R"({"type": "order_dispatch"})";

    EXPECT_CALL(mockOrderManager, handleOrderDispatch(jsonData));

    std::string response = router.routeRequest(jsonData, 0);
    json actualResponse = json::parse(response);
    json expectedResponse = json::parse(R"({"status":"success","message":"Order dispatched"})");

    EXPECT_EQ(actualResponse, expectedResponse);
}

// Test for DeliveryUpdate request
TEST_F(RequestRouterTest, RouteRequest_DeliveryUpdate)
{
    std::string jsonData = R"({"type": "delivery_update"})";

    EXPECT_CALL(mockOrderManager, deliveryUpdate(jsonData));

    std::string response = router.routeRequest(jsonData, 0);
    json actualResponse = json::parse(response);
    json expectedResponse = json::parse(R"({"status":"success","message":"Delivery updated"})");

    EXPECT_EQ(actualResponse, expectedResponse);
}

// Test for DisconnectRequest request
TEST_F(RequestRouterTest, RouteRequest_DisconnectRequest)
{
    std::string jsonData = R"({"type": "disconnect_request"})";
    int sock = 1;

    EXPECT_CALL(mockAuth, handleClientDisconnection(jsonData, sock));

    std::string response = router.routeRequest(jsonData, sock);
    json actualResponse = json::parse(response);
    json expectedResponse =
        json::parse(R"({"order":"disconnect","status":"success","message":"Disconnect request processed"})");

    EXPECT_EQ(actualResponse, expectedResponse);
}

// Test for OrderStatusQuery request
TEST_F(RequestRouterTest, RouteRequest_OrderStatusQuery)
{
    std::string jsonData = R"({"type": "order_status"})";

    EXPECT_CALL(mockOrderManager, handleOrderStatusQuery(jsonData)).WillOnce(Return(R"({"status": "success"})"));

    std::string response = router.routeRequest(jsonData, 0);
    EXPECT_EQ(response, R"({"status": "success"})");
}

// Test for CancelOrder request
TEST_F(RequestRouterTest, RouteRequest_CancelOrder)
{
    std::string jsonData = R"({"type": "cancel_order"})";

    EXPECT_CALL(mockOrderManager, handleCancelation(jsonData)).WillOnce(Return(R"({"status": "success"})"));

    std::string response = router.routeRequest(jsonData, 0);
    EXPECT_EQ(response, R"({"status": "success"})");
}

// Test for Unknown request type
TEST_F(RequestRouterTest, RouteRequest_UnknownRequest)
{
    std::string jsonData = R"({"type": "unknown_request"})";

    std::string response = router.routeRequest(jsonData, 0);
    EXPECT_EQ(response, R"({"status":"unknown request"})");
}

// Test for invalid JSON
TEST_F(RequestRouterTest, RouteRequest_InvalidJSON)
{
    std::string jsonData = R"({invalid_json})";

    std::string response = router.routeRequest(jsonData, 0);
    EXPECT_NE(response.find("\"status\":\"error\""), std::string::npos);
}
