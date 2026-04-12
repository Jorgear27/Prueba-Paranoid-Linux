#include "database.hpp"
#include "graph.hpp"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using ::testing::_;
using ::testing::Return;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// MockDatabase — mocks methods added in Database.hpp so tests run without a live PostgreSQL server.
// ---------------------------------------------------------------------------
class MockDatabase : public Database
{
  public:
    MOCK_METHOD(bool, replaceConnections, (const std::string&, const nlohmann::json&), (override));
    MOCK_METHOD(bool, updateInventoryStockLevel, (const std::string&, int, int), (override));
    MOCK_METHOD(nlohmann::json, buildGraphSnapshot, (), (override));
    MOCK_METHOD(bool, insertOrUpdateUser, (const std::string&, double, double), (override));
    MOCK_METHOD(bool, insertOrUpdateOrder, (const std::string&, const std::string&, int, int), (override));
    MOCK_METHOD(std::string, getOrderStatus, (const std::string&), (override));
    MOCK_METHOD(bool, updateOrderStatus, (const std::string&, const std::string&), (override));
    MOCK_METHOD(bool, updateUserOnlineStatus, (const std::string&, bool), (override));
    MOCK_METHOD(bool, insertOrUpdateInventory, (const std::string&, int, int, int), (override));
};

// ---------------------------------------------------------------------------
// Helper: minimal valid JSON snapshot (two nodes, one edge)
// ---------------------------------------------------------------------------
namespace
{
json twoNodeSnapshot()
{
    return json::parse(R"([
        {
            "node_id": "W001", "node_type": "fulfillment_center",
            "node_location": {"latitude": 0, "longitude": 0},
            "is_secure": true, "is_active": true,
            "connections": [
                {"target_node_id": "H001", "base_weight": 5.0,
                 "connection_type": "road", "connection_conditions": []}
            ]
        },
        {
            "node_id": "H001", "node_type": "market",
            "node_location": {"latitude": 1, "longitude": 0},
            "is_secure": true, "is_active": true,
            "connections": []
        }
    ])");
}
} // anonymous namespace

// ---------------------------------------------------------------------------
// Tests: replaceConnections()
// ---------------------------------------------------------------------------

TEST(ReplaceConnectionsTest, CalledWithCorrectNodeId)
{
    MockDatabase db;
    json connections = json::array();

    EXPECT_CALL(db, replaceConnections("W001", connections)).Times(1).WillOnce(Return(true));

    EXPECT_TRUE(db.replaceConnections("W001", connections));
}

TEST(ReplaceConnectionsTest, CalledWithConnectionArray)
{
    MockDatabase db;
    json connections = json::parse(R"([
        {"target_node_id": "H001", "base_weight": 5.0,
         "connection_type": "road", "connection_conditions": []}
    ])");

    EXPECT_CALL(db, replaceConnections("W001", connections)).Times(1).WillOnce(Return(true));

    EXPECT_TRUE(db.replaceConnections("W001", connections));
}

TEST(ReplaceConnectionsTest, ReturnsFalseOnFailure)
{
    MockDatabase db;
    EXPECT_CALL(db, replaceConnections(_, _)).WillOnce(Return(false));
    EXPECT_FALSE(db.replaceConnections("W001", json::array()));
}

TEST(ReplaceConnectionsTest, EmptyArrayIsAccepted)
{
    MockDatabase db;
    EXPECT_CALL(db, replaceConnections("W001", json::array())).WillOnce(Return(true));
    EXPECT_TRUE(db.replaceConnections("W001", json::array()));
}

// ---------------------------------------------------------------------------
// Tests: updateInventoryStockLevel()
// ---------------------------------------------------------------------------

TEST(UpdateInventoryStockLevelTest, CalledWithCorrectArgs)
{
    MockDatabase db;
    EXPECT_CALL(db, updateInventoryStockLevel("W001", 0, 500)).WillOnce(Return(true));
    EXPECT_TRUE(db.updateInventoryStockLevel("W001", 0, 500));
}

TEST(UpdateInventoryStockLevelTest, ReturnsFalseOnFailure)
{
    MockDatabase db;
    EXPECT_CALL(db, updateInventoryStockLevel(_, _, _)).WillOnce(Return(false));
    EXPECT_FALSE(db.updateInventoryStockLevel("W001", 1, 0));
}

TEST(UpdateInventoryStockLevelTest, ZeroStockIsValid)
{
    MockDatabase db;
    EXPECT_CALL(db, updateInventoryStockLevel("W002", 2, 0)).WillOnce(Return(true));
    EXPECT_TRUE(db.updateInventoryStockLevel("W002", 2, 0));
}

// ---------------------------------------------------------------------------
// Tests: buildGraphSnapshot()
// ---------------------------------------------------------------------------

TEST(BuildGraphSnapshotTest, ReturnsJsonArray)
{
    MockDatabase db;
    EXPECT_CALL(db, buildGraphSnapshot()).WillOnce(Return(twoNodeSnapshot()));

    const json result = db.buildGraphSnapshot();
    EXPECT_TRUE(result.is_array());
}

TEST(BuildGraphSnapshotTest, ReturnedSnapshotIsValidForGraphBuild)
{
    MockDatabase db;
    EXPECT_CALL(db, buildGraphSnapshot()).WillOnce(Return(twoNodeSnapshot()));

    const json snapshot = db.buildGraphSnapshot();

    // The snapshot must be accepted by Graph::build() without throwing.
    Graph graph;
    EXPECT_NO_THROW(graph.build(snapshot));
    EXPECT_EQ(graph.nodeCount(), 2u);
    EXPECT_EQ(graph.edgeCount(), 1u);
}

TEST(BuildGraphSnapshotTest, EmptySnapshotOnError)
{
    MockDatabase db;
    EXPECT_CALL(db, buildGraphSnapshot()).WillOnce(Return(json::array()));

    const json result = db.buildGraphSnapshot();
    EXPECT_TRUE(result.empty());
}

TEST(BuildGraphSnapshotTest, SnapshotContainsNodeIdField)
{
    MockDatabase db;
    EXPECT_CALL(db, buildGraphSnapshot()).WillOnce(Return(twoNodeSnapshot()));

    const json result = db.buildGraphSnapshot();
    ASSERT_GE(result.size(), 1u);
    EXPECT_TRUE(result[0].contains("node_id"));
    EXPECT_TRUE(result[0].contains("node_type"));
    EXPECT_TRUE(result[0].contains("connections"));
}

TEST(BuildGraphSnapshotTest, WarehouseNodeHasFulfillmentCenterType)
{
    MockDatabase db;
    EXPECT_CALL(db, buildGraphSnapshot()).WillOnce(Return(twoNodeSnapshot()));

    const json result = db.buildGraphSnapshot();

    bool foundFC = false;
    for (const auto& node : result)
    {
        if (node["node_id"] == "W001")
        {
            EXPECT_EQ(node["node_type"], "fulfillment_center");
            foundFC = true;
        }
    }
    EXPECT_TRUE(foundFC);
}

TEST(BuildGraphSnapshotTest, MarketNodeHasMarketType)
{
    MockDatabase db;
    EXPECT_CALL(db, buildGraphSnapshot()).WillOnce(Return(twoNodeSnapshot()));

    const json result = db.buildGraphSnapshot();

    bool foundMarket = false;
    for (const auto& node : result)
    {
        if (node["node_id"] == "H001")
        {
            EXPECT_EQ(node["node_type"], "market");
            foundMarket = true;
        }
    }
    EXPECT_TRUE(foundMarket);
}
