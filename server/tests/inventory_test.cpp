#include "graph_router.hpp"
#include "inventory.hpp"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Return;

// Mock classes for dependencies
class MockLogger : public Logger
{
  public:
    MOCK_METHOD(void, log, (const std::string&, const std::string&), (override));
};

class MockDatabase : public Database
{
  public:
    MOCK_METHOD(bool, updateInventoryStockLevel, (const std::string&, int, int), (override));
    MOCK_METHOD(bool, insertOrUpdateInventory, (const std::string&, int, int, int), (override));
    MOCK_METHOD(std::string, findWarehouseForItem, (int, int), (override));
    MOCK_METHOD(nlohmann::json, buildGraphSnapshot, (), (override));
    MOCK_METHOD(bool, replaceConnections, (const std::string&, const nlohmann::json&), (override));
};

// ---------------------------------------------------------------------------
// Fixture
// ---------------------------------------------------------------------------
class InventoryManagerTest : public ::testing::Test
{
  protected:
    MockLogger mockLogger;
    MockDatabase mockDatabase;
    InventoryManager inventoryManager{mockLogger, mockDatabase};

    void SetUp() override
    {
        // Suppress graph refresh calls — GraphRouter singleton uses real DB
        // but tests need it silent.
        EXPECT_CALL(mockDatabase, buildGraphSnapshot())
            .Times(AnyNumber())
            .WillRepeatedly(Return(nlohmann::json::array()));
    }
};

// Test for handleInventoryUpdate
TEST_F(InventoryManagerTest, HandleInventoryUpdate_Success)
{
    std::string jsonData = R"({
                "user_id": "user123",
                "inventory": [
                        {"item_type": 1, "stock_level": 100, "threshold": 50},
                        {"item_type": 2, "stock_level": 200, "threshold": 100}
                ]
        })";

    EXPECT_CALL(mockDatabase, insertOrUpdateInventory("user123", 1, 100, 50)).WillOnce(::testing::Return(true));
    EXPECT_CALL(mockDatabase, insertOrUpdateInventory("user123", 2, 200, 100)).WillOnce(::testing::Return(true));
    EXPECT_CALL(mockLogger,
                log("InventoryManager", ::testing::StartsWith("[INFO] Inventory updated for user: user123")));

    inventoryManager.handleInventoryUpdate(jsonData);
}

// Test for handleInventoryUpdate with failure
TEST_F(InventoryManagerTest, HandleInventoryUpdate_Failure)
{
    std::string jsonData = R"({
                "user_id": "user123",
                "inventory": [
                        {"item_type": 1, "stock_level": 100, "threshold": 50}
                ]
        })";

    EXPECT_CALL(mockDatabase, insertOrUpdateInventory("user123", 1, 100, 50)).WillOnce(::testing::Return(false));
    EXPECT_CALL(mockLogger,
                log("InventoryManager", ::testing::StartsWith("[ERROR] Failed to update inventory for user: user123")));

    inventoryManager.handleInventoryUpdate(jsonData);
}

// Test for handleRestockNotice
TEST_F(InventoryManagerTest, HandleRestockNotice_Success)
{
    std::string jsonData = R"({
                "user_id": "user123",
                "item_type": 1,
                "stock_level": 150
        })";

    EXPECT_CALL(mockDatabase, updateInventoryStockLevel("user123", 1, 150)).WillOnce(Return(true));
    EXPECT_CALL(mockLogger, log("InventoryManager", ::testing::HasSubstr("[INFO] Restock applied")));

    inventoryManager.handleRestockNotice(jsonData);
}

// Test for findWarehouseForItem
TEST_F(InventoryManagerTest, FindWarehouseForItem_Success)
{
    int itemType = 1;
    int quantityNeeded = 50;
    std::string warehouseId = "warehouse123";

    EXPECT_CALL(mockDatabase, findWarehouseForItem(itemType, quantityNeeded)).WillOnce(::testing::Return(warehouseId));

    std::string result = inventoryManager.findWarehouseForItem(itemType, quantityNeeded);
    EXPECT_EQ(result, warehouseId);
}

// Test for findWarehouseForItem with no warehouse found
TEST_F(InventoryManagerTest, FindWarehouseForItem_NoWarehouseFound)
{
    int itemType = 1;
    int quantityNeeded = 50;

    EXPECT_CALL(mockDatabase, findWarehouseForItem(itemType, quantityNeeded)).WillOnce(::testing::Return(""));
    EXPECT_CALL(mockLogger, log("InventoryManager", ::testing::StartsWith("[INFO] No warehouse found for item type:")));

    std::string result = inventoryManager.findWarehouseForItem(itemType, quantityNeeded);
    EXPECT_EQ(result, "");
}

// Test for getInstance method
TEST(InventoryManagerSingletonTest, GetInstance_ReturnsSameInstance)
{
    InventoryManager& instance1 = InventoryManager::getInstance();
    InventoryManager& instance2 = InventoryManager::getInstance();

    // Verify that both references point to the same instance
    EXPECT_EQ(&instance1, &instance2);
}

TEST_F(InventoryManagerTest, HandleInventoryUpdate_ExceptionThrown)
{
    std::string jsonData = R"({
                "user_id": "user123",
                "inventory": [
                        {"item_type": 1, "stock_level": 100, "threshold": 50}
                ]
        })";

    // Simulate an exception when calling insertOrUpdateInventory
    EXPECT_CALL(mockDatabase, insertOrUpdateInventory("user123", 1, 100, 50))
        .WillOnce(::testing::Throw(std::runtime_error("Database error")));

    EXPECT_CALL(mockLogger, log("InventoryManager",
                                ::testing::StartsWith("[ERROR] Exception occurred while handling inventory update:")));

    inventoryManager.handleInventoryUpdate(jsonData);
}

TEST_F(InventoryManagerTest, HandleRestockNotice_ExceptionThrown)
{
    std::string invalidJsonData = R"({
                "user_id": "user123",
                "item_type": "invalid_type", // Invalid type for item_type
                "stock_level": 150
        })";

    EXPECT_CALL(mockLogger, log("InventoryManager",
                                ::testing::StartsWith("[ERROR] Exception occurred while handling restock notice:")));

    inventoryManager.handleRestockNotice(invalidJsonData);
}

TEST_F(InventoryManagerTest, FindWarehouseForItem_ExceptionThrown)
{
    int itemType = 1;
    int quantityNeeded = 50;

    // Simulate an exception when calling findWarehouseForItem
    EXPECT_CALL(mockDatabase, findWarehouseForItem(itemType, quantityNeeded))
        .WillOnce(::testing::Throw(std::runtime_error("Database query error")));

    EXPECT_CALL(mockLogger,
                log("InventoryManager", ::testing::StartsWith("[ERROR] Exception occurred while finding warehouse:")));

    std::string result = inventoryManager.findWarehouseForItem(itemType, quantityNeeded);
    EXPECT_EQ(result, ""); // Expect an empty string as the return value
}

// ===========================================================================
// handleRestockNotice — previously a no-op
// ===========================================================================

TEST_F(InventoryManagerTest, HandleRestockNotice_CallsUpdateStockLevel)
{
    const std::string jsonData = R"({
        "user_id": "W001",
        "item_type": 0,
        "stock_level": 500
    })";

    EXPECT_CALL(mockDatabase, updateInventoryStockLevel("W001", 0, 500)).WillOnce(Return(true));
    EXPECT_CALL(mockLogger, log("InventoryManager", ::testing::HasSubstr("[INFO] Restock applied")));

    inventoryManager.handleRestockNotice(jsonData);
}

TEST_F(InventoryManagerTest, HandleRestockNotice_DatabaseFailure_LogsError)
{
    const std::string jsonData = R"({
        "user_id": "W001",
        "item_type": 1,
        "stock_level": 200
    })";

    EXPECT_CALL(mockDatabase, updateInventoryStockLevel("W001", 1, 200)).WillOnce(Return(false));
    EXPECT_CALL(mockLogger, log("InventoryManager", ::testing::HasSubstr("[ERROR]")));

    // Should not throw — failure is logged but not propagated.
    EXPECT_NO_THROW(inventoryManager.handleRestockNotice(jsonData));
}

TEST_F(InventoryManagerTest, HandleRestockNotice_ZeroStock_IsPersistedCorrectly)
{
    // A warehouse restocking to 0 (sold out) should still persist — the graph
    // refresh will then block its edges via the zero-stock rule.
    const std::string jsonData = R"({
        "user_id": "W002",
        "item_type": 2,
        "stock_level": 0
    })";

    EXPECT_CALL(mockDatabase, updateInventoryStockLevel("W002", 2, 0)).WillOnce(Return(true));
    EXPECT_CALL(mockLogger, log("InventoryManager", ::testing::HasSubstr("[INFO] Restock applied")));

    inventoryManager.handleRestockNotice(jsonData);
}

TEST_F(InventoryManagerTest, HandleRestockNotice_InvalidJson_DoesNotCallDb)
{
    EXPECT_CALL(mockDatabase, updateInventoryStockLevel(_, _, _)).Times(0);
    EXPECT_CALL(mockLogger, log("InventoryManager", ::testing::HasSubstr("[ERROR]")));

    inventoryManager.handleRestockNotice("not valid json {{{");
}

TEST_F(InventoryManagerTest, HandleRestockNotice_MissingField_DoesNotCallDb)
{
    // Missing stock_level field — should throw internally and log error.
    EXPECT_CALL(mockDatabase, updateInventoryStockLevel(_, _, _)).Times(0);
    EXPECT_CALL(mockLogger, log("InventoryManager", ::testing::HasSubstr("[ERROR]")));

    inventoryManager.handleRestockNotice(R"({"user_id":"W001","item_type":0})");
}
