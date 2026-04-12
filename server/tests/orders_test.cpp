#include "orders.hpp"
#include <chrono>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <thread>

using ::testing::_;
using ::testing::Return;
using json = nlohmann::json;

// Mock classes for dependencies
class MockDatabase : public Database
{
  public:
    MOCK_METHOD(bool, insertOrUpdateOrder, (const std::string&, const std::string&, int, int), (override));
    MOCK_METHOD(std::string, getOrderStatus, (const std::string&), (override));
    MOCK_METHOD(bool, updateOrderStatus, (const std::string&, const std::string&), (override));
    MOCK_METHOD(nlohmann::json, getOrderDetails, (const std::string&), (override));
    MOCK_METHOD(std::vector<std::string>, getApprovedOrders, (), (override));
};

class MockLogger : public Logger
{
  public:
    MOCK_METHOD(void, log, (const std::string&, const std::string&), (override));
};

class MockSender : public Sender
{
  public:
    MOCK_METHOD(int, sendMessageToClient, (const std::string&, const std::string&), (override));
};

class MockInventoryManager : public InventoryManager
{
  public:
    MOCK_METHOD(std::string, findWarehouseForItem, (int, int), (override));
};

// Test fixture
class OrderManagerTest : public ::testing::Test
{
  protected:
    MockDatabase mockDatabase;
    MockLogger mockLogger;
    MockSender mockSender;
    MockInventoryManager mockInventoryManager;
    OrderManager orderManager;

    OrderManagerTest() : orderManager(mockDatabase, mockLogger, mockSender, mockInventoryManager)
    {
    } // Inject mocks
};

// Test handleNewOrder
TEST_F(OrderManagerTest, HandleNewOrder_ValidOrder)
{
    std::string jsonData = R"({
        "hub_id": "hub123",
        "order_id": "order123",
        "items_needed": [
            {"item_type": 1, "quantity": 10},
            {"item_type": 2, "quantity": 5}
        ]
    })";

    EXPECT_CALL(mockDatabase, insertOrUpdateOrder("order123", "hub123", 1, 10)).WillOnce(Return(true));
    EXPECT_CALL(mockDatabase, insertOrUpdateOrder("order123", "hub123", 2, 5)).WillOnce(Return(true));
    EXPECT_CALL(mockLogger, log("OrderManager", "[INFO] New order added for user: hub123"));

    orderManager.handleNewOrder(jsonData);
}

TEST_F(OrderManagerTest, HandleNewOrder_InvalidItems)
{
    std::string jsonData = R"({
        "hub_id": "hub123",
        "order_id": "order123",
        "items_needed": [
            {"item_type": 1}
        ]
    })";

    EXPECT_CALL(mockLogger, log(_, _)).Times(0);

    orderManager.handleNewOrder(jsonData);
}

TEST_F(OrderManagerTest, HandleNewOrder_InvalidArray)
{
    std::string jsonData = R"({
        "hub_id": "hub123",
        "order_id": "order123",
        "items_needed": "invalid_array"
    })";

    EXPECT_CALL(mockLogger, log(_, _)).Times(0);

    orderManager.handleNewOrder(jsonData);
}

TEST_F(OrderManagerTest, HandleNewOrder_InvalidHubID)
{
    std::string jsonData = R"({
        "hub_id": null,
        "order_id": "order123",
        "items_needed": [
            {"item_type": 1, "quantity": 10},
            {"item_type": 2, "quantity": 5}
        ]
    })";

    EXPECT_CALL(mockLogger, log(_, _)).Times(0);

    orderManager.handleNewOrder(jsonData);
}

TEST_F(OrderManagerTest, HandleNewOrder_InvalidOrderID)
{
    std::string jsonData = R"({
        "hub_id": "hub123",
        "order_id": null,
        "items_needed": [
            {"item_type": 1, "quantity": 10},
            {"item_type": 2, "quantity": 5}
        ]
    })";

    EXPECT_CALL(mockLogger, log(_, _)).Times(0);

    orderManager.handleNewOrder(jsonData);
}

// Test processApprovedOrders
TEST_F(OrderManagerTest, ProcessApprovedOrders)
{
    std::vector<std::string> approvedOrders = {"order123"};
    nlohmann::json orderDetails = R"({
        "items_needed": [
            {"item_type": 1, "quantity": 10},
            {"item_type": 2, "quantity": 5}
        ]
    })"_json;

    EXPECT_CALL(mockDatabase, getApprovedOrders()).WillOnce(Return(approvedOrders));
    EXPECT_CALL(mockDatabase, getOrderDetails("order123")).WillOnce(Return(orderDetails));
    EXPECT_CALL(mockDatabase, updateOrderStatus("order123", "Requested")).WillOnce(Return(true));

    std::thread t([&]() { orderManager.processApprovedOrders(); });
    std::this_thread::sleep_for(std::chrono::seconds(1));
    t.detach();
}

// Test supplyRequest
TEST_F(OrderManagerTest, SupplyRequest_Valid)
{
    std::vector<std::pair<int, int>> itemsNeeded = {{1, 10}, {2, 5}};
    EXPECT_CALL(mockInventoryManager, findWarehouseForItem(1, 10)).WillOnce(Return("warehouse1"));
    EXPECT_CALL(mockInventoryManager, findWarehouseForItem(2, 5)).WillOnce(Return("warehouse2"));

    std::string result = orderManager.supplyRequest("order123", itemsNeeded);
    EXPECT_NE(result.find("\"type\":\"supply_request\""), std::string::npos);
}

// Test handleOrderDispatch
TEST_F(OrderManagerTest, HandleOrderDispatch_Valid)
{
    std::string jsonData = R"({
        "order_id": "order123",
        "status": "shipped",
        "items_shipped": [
            {"item_type": 1, "quantity": 10}
        ]
    })";

    nlohmann::json orderDetails = R"({"user_id": "hub123"})"_json;

    EXPECT_CALL(mockDatabase, getOrderDetails("order123")).WillOnce(Return(orderDetails));
    EXPECT_CALL(mockSender, sendMessageToClient("hub123", _)).WillOnce(Return(0));
    EXPECT_CALL(mockDatabase, updateOrderStatus("order123", "shipped")).WillOnce(Return(true));

    orderManager.handleOrderDispatch(jsonData);
}

// Test handleCancelation
TEST_F(OrderManagerTest, HandleCancelation_Valid)
{
    std::string jsonData = R"({"order_id": "order123"})";
    nlohmann::json orderDetails = R"({"status": "Pending"})"_json;

    EXPECT_CALL(mockDatabase, getOrderDetails("order123")).WillOnce(Return(orderDetails));
    EXPECT_CALL(mockDatabase, updateOrderStatus("order123", "Canceled")).WillOnce(Return(true));

    std::string result = orderManager.handleCancelation(jsonData);
    EXPECT_NE(result.find("\"status\":\"success\""), std::string::npos);
}

// Test handleOrderStatusQuery
TEST_F(OrderManagerTest, HandleOrderStatusQuery_Valid)
{
    std::string jsonData = R"({
        "type": "status_query",
        "hub_id": "hub123",
        "timestamp": "2025-04-01T12:00:00Z",
        "order_id": "order123"
    })";

    nlohmann::json orderDetails = R"({"status": "Pending"})"_json;

    EXPECT_CALL(mockDatabase, getOrderDetails("order123")).WillOnce(Return(orderDetails));

    std::string result = orderManager.handleOrderStatusQuery(jsonData);
    EXPECT_NE(result.find("\"status\":\"Pending\""), std::string::npos);
}

// Test deliveryUpdate
TEST_F(OrderManagerTest, DeliveryUpdate_Valid)
{
    std::string jsonData = R"({
        "timestamp": "2025-04-01T12:00:00Z",
        "hub_id": "hub123",
        "order_id": "order123",
        "status": "Delivered"
    })";

    EXPECT_CALL(mockDatabase, updateOrderStatus("order123", "Delivered")).WillOnce(Return(true));

    orderManager.deliveryUpdate(jsonData);
}

// Error in uploading order
TEST_F(OrderManagerTest, HandleNewOrder_ErrorDB)
{
    std::string jsonData = R"({
        "hub_id": "hub123",
        "order_id": "order123",
        "items_needed": [
            {"item_type": 1, "quantity": 10}
        ]
    })";

    EXPECT_CALL(mockDatabase, insertOrUpdateOrder("order123", "hub123", 1, 10)).WillOnce(Return(false));
    EXPECT_CALL(mockLogger, log("OrderManager", "[ERROR] Failed to insert/update order in DB."));

    orderManager.handleNewOrder(jsonData);
}

// SupplyRequest error communication with warehouse
TEST_F(OrderManagerTest, SupplyRequest_Exception)
{
    EXPECT_CALL(mockSender, sendMessageToClient("warehouse123", _)).WillOnce(Return(-1));
    EXPECT_CALL(mockLogger, log("OrderManager", "[ERROR] Failed to send supply request to warehouse: warehouse123"));
    std::vector<std::pair<int, int>> itemsNeeded = {{1, 10}};
    EXPECT_CALL(mockInventoryManager, findWarehouseForItem(_, _)).WillOnce(Return("warehouse123"));

    std::string result = orderManager.supplyRequest("order123", itemsNeeded);
}

// No warehouse found in supplyRequest
TEST_F(OrderManagerTest, SupplyRequest_NoWarehouseFound)
{
    std::vector<std::pair<int, int>> itemsNeeded = {{1, 10}};
    EXPECT_CALL(mockInventoryManager, findWarehouseForItem(1, 10)).WillOnce(Return(""));

    std::string result = orderManager.supplyRequest("order123", itemsNeeded);
    EXPECT_NE(result.find("\"fulfilled_by\":\"none\""), std::string::npos);
}

// Exception in handleOrderDispatch
TEST_F(OrderManagerTest, HandleOrderDispatch_Exception)
{
    std::string jsonData = R"({
        "order_id": "order123",
        "status": "shipped"
    })"; // Missing "items_shipped"

    EXPECT_CALL(mockLogger, log("OrderManager", _));
    orderManager.handleOrderDispatch(jsonData);
}

// Missing fields in handleOrderStatusQuery
TEST_F(OrderManagerTest, HandleOrderStatusQuery_MissingFields)
{
    std::string jsonData = R"({
        "type": "status_query",
        "hub_id": "hub123"
    })"; // Missing "timestamp" and "order_id"

    EXPECT_CALL(mockLogger, log("OrderManager", _));
    std::string result = orderManager.handleOrderStatusQuery(jsonData);
    EXPECT_NE(result.find("\"status\":\"error\""), std::string::npos);
}

// Error updating order status
TEST_F(OrderManagerTest, UpdateOrderStatus_Error)
{
    std::string orderId = "order123";
    std::string newStatus = "shipped";

    EXPECT_CALL(mockDatabase, updateOrderStatus(orderId, newStatus)).WillOnce(Return(false));
    EXPECT_CALL(mockLogger, log("OrderManager", "Failed to update order status for order: order123"));

    orderManager.updateOrderStatus(orderId, newStatus);
}

// Handle Order Status Query invalid type (not a string)
TEST_F(OrderManagerTest, HandleOrderStatusQuery_InvalidType)
{
    std::string jsonData = R"({
        "type": 123,
        "hub_id": "hub123",
        "timestamp": "2025-04-01T12:00:00Z",
        "order_id": "order123"
    })";

    EXPECT_CALL(mockLogger, log("OrderManager", _));
    std::string result = orderManager.handleOrderStatusQuery(jsonData);
    EXPECT_NE(result.find("\"status\":\"error\""), std::string::npos);
}

// Handle Order Status Query invalid hub_id (not a string)
TEST_F(OrderManagerTest, HandleOrderStatusQuery_InvalidHubId)
{
    std::string jsonData = R"({
        "type": "status_query",
        "hub_id": 123,
        "timestamp": "2025-04-01T12:00:00Z",
        "order_id": "order123"
    })";

    EXPECT_CALL(mockLogger, log("OrderManager", _));
    std::string result = orderManager.handleOrderStatusQuery(jsonData);
    EXPECT_NE(result.find("\"status\":\"error\""), std::string::npos);
}

// Handle Order Status Query invalid orderid (not a string)
TEST_F(OrderManagerTest, HandleOrderStatusQuery_InvalidOrderId)
{
    std::string jsonData = R"({
        "type": "status_query",
        "hub_id": "hub123",
        "timestamp": "2025-04-01T12:00:00Z",
        "order_id": 123
    })";

    EXPECT_CALL(mockLogger, log("OrderManager", _));
    std::string result = orderManager.handleOrderStatusQuery(jsonData);
    EXPECT_NE(result.find("\"status\":\"error\""), std::string::npos);
}

// Failed to send order dispatch notification
TEST_F(OrderManagerTest, HandleOrderDispatch_FailedNotification)
{
    std::string jsonData = R"({
        "order_id": "order123",
        "status": "shipped",
        "items_shipped": [{"item_type": 1, "quantity": 10}]
    })";

    EXPECT_CALL(mockDatabase, getOrderDetails("order123")).WillOnce(Return(nlohmann::json{{"user_id", "hub123"}}));
    EXPECT_CALL(mockSender, sendMessageToClient("hub123", _)).WillOnce(Return(-1));
    EXPECT_CALL(mockLogger, log("OrderManager", "[ERROR] Failed to send order dispatch notification to hub: hub123"));

    orderManager.handleOrderDispatch(jsonData);
}

// Test for line 286: Exception in handleCancelation
TEST_F(OrderManagerTest, HandleCancelation_Exception)
{
    std::string jsonData = R"({
        "order_id": "order123"
    })";

    EXPECT_CALL(mockDatabase, getOrderDetails("order123"))
        .WillOnce(testing::Throw(std::runtime_error("Mock exception")));
    EXPECT_CALL(mockLogger, log("OrderManager", _));

    std::string result = orderManager.handleCancelation(jsonData);
    EXPECT_NE(result.find("\"status\":\"error\""), std::string::npos);
}

// Test for line 290: Order already approved in handleCancelation
TEST_F(OrderManagerTest, HandleCancelation_AlreadyApproved)
{
    std::string jsonData = R"({
        "order_id": "order123"
    })";

    EXPECT_CALL(mockDatabase, getOrderDetails("order123")).WillOnce(Return(nlohmann::json{{"status", "Approved"}}));
    EXPECT_CALL(mockLogger, log("OrderManager", "[ERROR] The order was already approved, it cannot be canceled."));

    std::string result = orderManager.handleCancelation(jsonData);
    EXPECT_NE(result.find("\"status\":\"error\""), std::string::npos);
}

// Invalid timestamp in deliveryUpdate
TEST_F(OrderManagerTest, DeliveryUpdate_InvalidTimestamp)
{
    std::string jsonData = R"({
        "hub_id": "hub123",
        "order_id": "order123",
        "status": "Delivered"
    })";

    // We expect the logger not to log the success message
    EXPECT_CALL(mockLogger, log("OrderManager", _)).Times(0);

    orderManager.deliveryUpdate(jsonData);
}

// Invalid order_id in deliveryUpdate
TEST_F(OrderManagerTest, DeliveryUpdate_InvalidOrderID)
{
    std::string jsonData = R"({
        "timestamp": "2025-04-01T12:00:00Z",
        "hub_id": "hub123",
        "status": "Delivered"
    })";

    // We expect the logger not to log the success message
    EXPECT_CALL(mockLogger, log("OrderManager", _)).Times(0);

    orderManager.deliveryUpdate(jsonData);
}

// Invalid hub_id in deliveryUpdate

TEST_F(OrderManagerTest, DeliveryUpdate_InvalidHubID)
{
    std::string jsonData = R"({
        "timestamp": "2025-04-01T12:00:00Z",
        "order_id": "order123",
        "status": "Delivered"
    })";

    EXPECT_CALL(mockLogger, log("OrderManager", _)).Times(0);

    orderManager.deliveryUpdate(jsonData);
}

// Invalid status in deliveryUpdate
TEST_F(OrderManagerTest, DeliveryUpdate_InvalidStatus)
{
    std::string jsonData = R"({
        "timestamp": "2025-04-01T12:00:00Z",
        "hub_id": "hub123",
        "order_id": "order123"
    })";

    EXPECT_CALL(mockLogger, log("OrderManager", _)).Times(0);

    orderManager.deliveryUpdate(jsonData);
}
