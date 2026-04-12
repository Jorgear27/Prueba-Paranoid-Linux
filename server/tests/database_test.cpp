#include "database.hpp"
#include "graph.hpp"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using ::testing::_;
using ::testing::Return;
using json = nlohmann::json;

// Test fixture for Database
class DatabaseTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        // Ensure the database connection is initialized before each test
        db = &Database::getInstance();
        db->getConnection();
        ASSERT_TRUE(db->is_connected()) << "Database should be connected.";
    }

    void TearDown() override
    {
        // Disconnect the database connection after each test
        db->disconnect();
        db = nullptr; // Reset the pointer to avoid dangling reference
    }

    Database* db;
};

// Test the singleton instance
TEST_F(DatabaseTest, SingletonInstance)
{
    Database* instance1 = &Database::getInstance();
    Database* instance2 = &Database::getInstance();
    ASSERT_EQ(instance1, instance2) << "Singleton instances should be the same.";
}

// Test disconnecting from the database
TEST_F(DatabaseTest, Disconnect)
{
    db->disconnect();
    ASSERT_FALSE(db->is_connected()) << "Database should be disconnected.";
}

// Test reconnecting to the database
TEST_F(DatabaseTest, Reconnect)
{
    db->disconnect();
    ASSERT_FALSE(db->is_connected()) << "Database should be disconnected.";
    db->getConnection();
    ASSERT_TRUE(db->is_connected()) << "Database should reconnect successfully.";
}

// Test getConnection when already connected
TEST_F(DatabaseTest, GetConnectionWhenConnected)
{
    PGconn* conn = db->getConnection();
    ASSERT_NE(conn, nullptr) << "Connection object should not be null when connected.";
    ASSERT_TRUE(db->is_connected()) << "Database should remain connected.";
}

// Test database constructor with invalid connection string
TEST_F(DatabaseTest, ConstructorConnectionFailure)
{
    // Use an invalid connection string
    std::string invalidConnectionString =
        "host=invalid_host dbname=invalid_db user=invalid_user password=invalid_password";

    // Create a new instance of the database with the invalid connection string
    Database dbInstance(invalidConnectionString);

    // Verify that the connection is not established
    ASSERT_FALSE(dbInstance.is_connected()) << "Database connection should fail with invalid connection string.";

    // Clean up
    dbInstance.disconnect(); // Ensure the connection is closed
}

// Test getConnection with reconnection failure
TEST_F(DatabaseTest, ReconnectionFailure)
{
    // Use an invalid connection string
    std::string invalidConnectionString =
        "host=invalid_host dbname=invalid_db user=invalid_user password=invalid_password";

    // Disconnect the database to force reconnection
    db->disconnect();

    // Attempt to get a connection, which should fail
    PGconn* conn = db->getConnection(invalidConnectionString);
    ASSERT_EQ(conn, nullptr) << "Connection object should be null after failed reconnection.";
    ASSERT_FALSE(db->is_connected()) << "Database should remain disconnected after failed reconnection.";
}

TEST_F(DatabaseTest, InsertOrUpdateUser)
{
    std::string userId = "Htest";
    double latitude = 37.7749;
    double longitude = -122.4194;

    // Insert a new user
    ASSERT_TRUE(db->insertOrUpdateUser(userId, latitude, longitude));

    // Update the same user
    latitude = 40.7128;
    longitude = -74.0060;
    ASSERT_TRUE(db->insertOrUpdateUser(userId, latitude, longitude));
}

TEST_F(DatabaseTest, InsertOrUpdateUserFailure)
{
    std::string userId = "Htest";
    double latitude = 37.7749;
    double longitude = -122.4194;

    // Disconnect the database to simulate a failure
    db->disconnect();
    ASSERT_FALSE(db->insertOrUpdateUser(userId, latitude, longitude))
        << "Inserting/updating user should fail when database is disconnected.";
}

TEST_F(DatabaseTest, InsertOrUpdateOrder)
{
    std::string orderId = "Htest_123";
    std::string hubId = "Htest";
    int itemType = 1;
    int quantity = 10;

    // Insert a new order
    ASSERT_TRUE(db->insertOrUpdateOrder(orderId, hubId, itemType, quantity));

    // Update the same order
    quantity = 20;
    ASSERT_TRUE(db->insertOrUpdateOrder(orderId, hubId, itemType, quantity));
}

TEST_F(DatabaseTest, InsertOrUpdateOrderFailure)
{
    std::string orderId = "Htest_123";
    std::string hubId = "Htest";
    int itemType = 1;
    int quantity = 10;

    // Disconnect the database to simulate a failure
    db->disconnect();
    ASSERT_FALSE(db->insertOrUpdateOrder(orderId, hubId, itemType, quantity))
        << "Inserting/updating order should fail when database is disconnected.";
}

TEST_F(DatabaseTest, GetOrderStatus)
{
    std::string orderId = "Htest_123";
    std::string hubId = "Htest";
    int itemType = 1;
    int quantity = 10;

    // Insert an order
    db->insertOrUpdateOrder(orderId, hubId, itemType, quantity);

    // Retrieve the status
    std::string status = db->getOrderStatus(orderId);
    ASSERT_EQ(status, "Pending");
}

TEST_F(DatabaseTest, GetOrderStatusFailure)
{
    std::string orderId = "Htest_123";

    // Disconnect the database to simulate a failure
    db->disconnect();
    ASSERT_EQ(db->getOrderStatus(orderId), "") << "Getting order status should fail when database is disconnected.";
}

TEST_F(DatabaseTest, UpdateOrderStatus)
{
    std::string orderId = "Htest_123";
    std::string hubId = "Htest";
    int itemType = 1;
    int quantity = 10;

    // Insert an order
    db->insertOrUpdateOrder(orderId, hubId, itemType, quantity);

    // Update the status
    ASSERT_TRUE(db->updateOrderStatus(orderId, "Approved"));

    // Verify the updated status
    std::string status = db->getOrderStatus(orderId);
    ASSERT_EQ(status, "Approved");
}

TEST_F(DatabaseTest, UpdateOrderStatusFailure)
{
    std::string orderId = "Htest_123";

    // Disconnect the database to simulate a failure
    db->disconnect();
    ASSERT_FALSE(db->updateOrderStatus(orderId, "Approved"))
        << "Updating order status should fail when database is disconnected.";
}

TEST_F(DatabaseTest, GetOrderDetails)
{
    std::string orderId = "Htest_123";
    std::string hubId = "Htest";
    int itemType = 1;
    int quantity = 10;

    // Insert an order
    db->insertOrUpdateOrder(orderId, hubId, itemType, quantity);

    // Retrieve order details
    nlohmann::json details = db->getOrderDetails(orderId);
    ASSERT_EQ(details["order_id"], orderId);
    ASSERT_EQ(details["user_id"], hubId);
    ASSERT_EQ(details["status"], "Pending");
}

TEST_F(DatabaseTest, GetOrderDetailsIDNotFound)
{
    std::string orderId = "Htest_invalid";

    // Retrieve order details for a non-existent order
    nlohmann::json details = db->getOrderDetails(orderId);
    ASSERT_EQ(details["status"], "error") << "Getting order details should fail for non-existent order.";
}

TEST_F(DatabaseTest, GetOrderDetailsFailure)
{
    std::string orderId = "Htest_123";

    // Disconnect the database to simulate a failure
    db->disconnect();
    nlohmann::json details = db->getOrderDetails(orderId);
    ASSERT_EQ(details["status"], "error") << "Getting order details should fail when database is disconnected.";
}

TEST_F(DatabaseTest, GetApprovedOrders)
{
    std::string orderId = "Htest_123";
    std::string hubId = "Htest";
    int itemType = 1;
    int quantity = 10;

    // Insert an order and approve it
    db->insertOrUpdateOrder(orderId, hubId, itemType, quantity);
    db->updateOrderStatus(orderId, "Approved");

    // Retrieve approved orders
    std::vector<std::string> approvedOrders = db->getApprovedOrders();
    ASSERT_EQ(approvedOrders.size(), 1);
    ASSERT_EQ(approvedOrders[0], orderId);
}

TEST_F(DatabaseTest, GetApprovedOrdersFailure)
{
    // Disconnect the database to simulate a failure
    db->disconnect();
    std::vector<std::string> approvedOrders = db->getApprovedOrders();
    ASSERT_TRUE(approvedOrders.empty()) << "Getting approved orders should fail when database is disconnected.";
}

TEST_F(DatabaseTest, UpdateUserOnlineStatus)
{
    std::string userId = "Htest";
    double latitude = 37.7749;
    double longitude = -122.4194;

    // Insert a new user
    db->insertOrUpdateUser(userId, latitude, longitude);

    // Update the user's online status
    ASSERT_TRUE(db->updateUserOnlineStatus(userId, false));
}

TEST_F(DatabaseTest, UpdateUserOnlineStatusFailure)
{
    std::string userId = "Htest";

    // Disconnect the database to simulate a failure
    db->disconnect();
    ASSERT_FALSE(db->updateUserOnlineStatus(userId, false))
        << "Updating user online status should fail when database is disconnected.";
}

TEST_F(DatabaseTest, FindWarehouseForItem)
{
    std::string warehouseId = "Wtest";
    int itemType = 1;
    int stockLevel = 100;
    int stockThreshold = 10;

    // Insert a warehouse user and inventory
    db->insertOrUpdateUser(warehouseId, 0.0, 0.0);
    db->insertOrUpdateInventory(warehouseId, itemType, stockLevel, stockThreshold);

    // Find a warehouse for the item
    std::string foundWarehouse = db->findWarehouseForItem(itemType, 50);
    ASSERT_EQ(foundWarehouse, warehouseId);
}

TEST_F(DatabaseTest, FindWarehouseForItemFailure)
{
    int itemType = 1;
    int quantityNeeded = 50;

    // Disconnect the database to simulate a failure
    db->disconnect();
    std::string foundWarehouse = db->findWarehouseForItem(itemType, quantityNeeded);
    ASSERT_EQ(foundWarehouse, "") << "Finding warehouse should fail when database is disconnected.";
}

TEST_F(DatabaseTest, InsertOrUpdateInventory)
{
    std::string userId = "Wtest";
    int itemType = 1;
    int stockLevel = 100;
    int stockThreshold = 10;

    // Insert inventory
    ASSERT_TRUE(db->insertOrUpdateInventory(userId, itemType, stockLevel, stockThreshold));

    // Update inventory
    stockLevel = 200;
    ASSERT_TRUE(db->insertOrUpdateInventory(userId, itemType, stockLevel, stockThreshold));
}

TEST_F(DatabaseTest, InsertOrUpdateInventoryFailure)
{
    std::string userId = "Wtest";
    int itemType = 1;
    int stockLevel = 100;
    int stockThreshold = 10;

    // Disconnect the database to simulate a failure
    db->disconnect();
    ASSERT_FALSE(db->insertOrUpdateInventory(userId, itemType, stockLevel, stockThreshold))
        << "Inserting/updating inventory should fail when database is disconnected.";
}
