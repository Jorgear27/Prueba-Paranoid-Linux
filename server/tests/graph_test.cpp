#include "graph.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Helper: build a valid JSON map for use across the tests.
// ---------------------------------------------------------------------------
namespace
{

/**
 * @brief Returns a two-node, one-edge JSON array (W001 → W002, road, no conditions).
 *        Both nodes are fulfillment centers, active, and secure.
 */
json twoFulfillmentCenterMap()
{
    return json::parse(R"([
        {
            "node_id": "W001",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 40.71, "longitude": -74.00 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                {
                    "target_node_id": "W002",
                    "base_weight": 10.0,
                    "connection_type": "road",
                    "connection_conditions": []
                }
            ]
        },
        {
            "node_id": "W002",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 41.00, "longitude": -73.50 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        }
    ])");
}

/**
 * @brief Returns a two-node JSON array where both nodes are markets (hubs).
 */
json twoMarketMap()
{
    return json::parse(R"([
        {
            "node_id": "H001",
            "node_type": "market",
            "node_location": { "latitude": 40.71, "longitude": -74.00 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                {
                    "target_node_id": "H002",
                    "base_weight": 5.0,
                    "connection_type": "rail",
                    "connection_conditions": ["foggy"]
                }
            ]
        },
        {
            "node_id": "H002",
            "node_type": "market",
            "node_location": { "latitude": 41.00, "longitude": -73.50 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        }
    ])");
}

} // namespace

// ---------------------------------------------------------------------------
// Test fixture for Graph tests.
// ---------------------------------------------------------------------------
class GraphTest : public ::testing::Test
{
  protected:
    Graph graph;
};

// ===========================================================================
// build() — basic success cases
// ===========================================================================

TEST_F(GraphTest, Build_EmptyArray_ProducesEmptyGraph)
{
    graph.build(json::array());
    EXPECT_TRUE(graph.empty());
    EXPECT_EQ(graph.nodeCount(), 0u);
    EXPECT_EQ(graph.edgeCount(), 0u);
}

TEST_F(GraphTest, Build_TwoFulfillmentCenters_LoadsCorrectCounts)
{
    graph.build(twoFulfillmentCenterMap());

    EXPECT_FALSE(graph.empty());
    EXPECT_EQ(graph.nodeCount(), 2u);
    EXPECT_EQ(graph.edgeCount(), 1u);
}

TEST_F(GraphTest, Build_TwoMarkets_LoadsCorrectCounts)
{
    graph.build(twoMarketMap());

    EXPECT_EQ(graph.nodeCount(), 2u);
    EXPECT_EQ(graph.edgeCount(), 1u);
}

TEST_F(GraphTest, Build_IsIdempotent_SecondCallReplacesFirstGraph)
{
    graph.build(twoFulfillmentCenterMap());
    ASSERT_EQ(graph.nodeCount(), 2u);

    // Build again with a different map; state must be fully replaced.
    graph.build(twoMarketMap());
    EXPECT_EQ(graph.nodeCount(), 2u);
    EXPECT_TRUE(graph.hasNode("H001"));
    EXPECT_FALSE(graph.hasNode("W001")); // Old nodes must be gone.
}

// ===========================================================================
// build() — obstacle filtering
// ===========================================================================

TEST_F(GraphTest, Build_InactiveNodeIsDiscarded)
{
    json map = json::parse(R"([
        {
            "node_id": "W001",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": false,
            "connections": []
        }
    ])");

    graph.build(map);
    EXPECT_TRUE(graph.empty());
    EXPECT_FALSE(graph.hasNode("W001"));
}

TEST_F(GraphTest, Build_InsecureNodeIsDiscarded)
{
    json map = json::parse(R"([
        {
            "node_id": "W001",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": false,
            "is_active": true,
            "connections": []
        }
    ])");

    graph.build(map);
    EXPECT_TRUE(graph.empty());
    EXPECT_FALSE(graph.hasNode("W001"));
}

TEST_F(GraphTest, Build_MissingObstacleFlagsDefaultToActive)
{
    // is_secure and is_active are optional; absent means true.
    json map = json::parse(R"([
        {
            "node_id": "W001",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "connections": []
        }
    ])");

    graph.build(map);
    EXPECT_EQ(graph.nodeCount(), 1u);
    EXPECT_TRUE(graph.hasNode("W001"));
}

TEST_F(GraphTest, Build_DanglingConnectionIsDiscarded)
{
    // W001 → W999, but W999 is not in the map.
    json map = json::parse(R"([
        {
            "node_id": "W001",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                {
                    "target_node_id": "W999",
                    "base_weight": 10.0,
                    "connection_type": "road",
                    "connection_conditions": []
                }
            ]
        }
    ])");

    graph.build(map);
    EXPECT_EQ(graph.nodeCount(), 1u);
    EXPECT_EQ(graph.edgeCount(), 0u); // Dangling edge is dropped.
}

TEST_F(GraphTest, Build_ConnectionTargetingObstacleNodeIsDiscarded)
{
    // W001 → W002, but W002 is inactive (obstacle).
    json map = json::parse(R"([
        {
            "node_id": "W001",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                {
                    "target_node_id": "W002",
                    "base_weight": 10.0,
                    "connection_type": "road",
                    "connection_conditions": []
                }
            ]
        },
        {
            "node_id": "W002",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 1, "longitude": 1 },
            "is_secure": true,
            "is_active": false,
            "connections": []
        }
    ])");

    graph.build(map);
    EXPECT_EQ(graph.nodeCount(), 1u); // Only W001 is active.
    EXPECT_EQ(graph.edgeCount(), 0u); // Edge to W002 (obstacle) is dropped.
}

// ===========================================================================
// build() — error cases
// ===========================================================================

TEST_F(GraphTest, Build_NonArrayPayload_ThrowsInvalidArgument)
{
    EXPECT_THROW(graph.build(json::object()), std::invalid_argument);
}

TEST_F(GraphTest, Build_DuplicateNodeId_ThrowsInvalidArgument)
{
    json map = json::parse(R"([
        {
            "node_id": "W001",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        },
        {
            "node_id": "W001",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 1, "longitude": 1 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        }
    ])");

    EXPECT_THROW(graph.build(map), std::invalid_argument);
}

TEST_F(GraphTest, Build_UnknownNodeType_ThrowsInvalidArgument)
{
    json map = json::parse(R"([
        {
            "node_id": "X001",
            "node_type": "spaceship",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        }
    ])");

    EXPECT_THROW(graph.build(map), std::invalid_argument);
}

// ===========================================================================
// build() — blocked edge handling
// ===========================================================================

TEST_F(GraphTest, Build_BlockedEdgeIsDiscarded)
{
    json map = json::parse(R"([
        {
            "node_id": "W001",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                {
                    "target_node_id": "W002",
                    "base_weight": 10.0,
                    "connection_type": "blocked",
                    "connection_conditions": []
                }
            ]
        },
        {
            "node_id": "W002",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 1, "longitude": 1 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        }
    ])");

    graph.build(map);
    EXPECT_EQ(graph.nodeCount(), 2u);
    EXPECT_EQ(graph.edgeCount(), 0u); // Blocked edge must not be stored.
}

// ===========================================================================
// Edge cost formula
// ===========================================================================

TEST_F(GraphTest, Build_EdgeCost_RoadNoConditions_EqualToBaseWeight)
{
    // road modifier = 1.0, no conditions → totalModifier = 1.0 + 0.0 = 1.0
    // cost = 10.0 × 1.0 × 1.0 = 10.0
    graph.build(twoFulfillmentCenterMap());

    auto edges = graph.getEdges(NodeType::FulfillmentCenter);
    ASSERT_EQ(edges.size(), 1u);
    EXPECT_DOUBLE_EQ(edges[0].cost, 10.0);
}

TEST_F(GraphTest, Build_EdgeCost_RailWithFoggy_CorrectFormula)
{
    // rail modifier = 0.7, foggy = +0.1
    // totalModifier = 1.0 + 0.1 = 1.1
    // cost = 5.0 × 0.7 × 1.1 = 3.85
    graph.build(twoMarketMap());

    auto edges = graph.getEdges(NodeType::Market);
    ASSERT_EQ(edges.size(), 1u);
    EXPECT_NEAR(edges[0].cost, 3.85, 1e-9);
}

TEST_F(GraphTest, Build_EdgeCost_DroneWithRainAndInfected_CorrectFormula)
{
    // drone = 1.2, rain = +0.2, infected_activity = +0.3
    // totalModifier = 1.0 + 0.2 + 0.3 = 1.5
    // cost = 8.0 × 1.2 × 1.5 = 14.4
    json map = json::parse(R"([
        {
            "node_id": "W001",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                {
                    "target_node_id": "W002",
                    "base_weight": 8.0,
                    "connection_type": "drone",
                    "connection_conditions": ["rain", "infected_activity"]
                }
            ]
        },
        {
            "node_id": "W002",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 1, "longitude": 1 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        }
    ])");

    graph.build(map);
    auto edges = graph.getEdges(NodeType::FulfillmentCenter);
    ASSERT_EQ(edges.size(), 1u);
    EXPECT_NEAR(edges[0].cost, 14.4, 1e-9);
}

TEST_F(GraphTest, Build_EdgeCost_ReinforcedAndCleared_ReducesModifier)
{
    // road = 1.0, reinforced = -0.3, cleared = -0.2
    // totalModifier = 1.0 + (-0.3) + (-0.2) = 0.5
    // cost = 10.0 × 1.0 × 0.5 = 5.0
    json map = json::parse(R"([
        {
            "node_id": "W001",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                {
                    "target_node_id": "W002",
                    "base_weight": 10.0,
                    "connection_type": "road",
                    "connection_conditions": ["reinforced", "cleared"]
                }
            ]
        },
        {
            "node_id": "W002",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 1, "longitude": 1 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        }
    ])");

    graph.build(map);
    auto edges = graph.getEdges(NodeType::FulfillmentCenter);
    ASSERT_EQ(edges.size(), 1u);
    EXPECT_NEAR(edges[0].cost, 5.0, 1e-9);
}

TEST_F(GraphTest, Build_EdgeCost_AllConditionsModifiers_AreCorrect)
{
    // Verify each type modifier individually using a road base (1.0):
    // base = 1.0, type = rail (0.7), no conditions → cost = 1.0 × 0.7 × 1.0 = 0.7
    json map = json::parse(R"([
        {
            "node_id": "W001",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "W002", "base_weight": 1.0, "connection_type": "rail",      "connection_conditions": [] },
                { "target_node_id": "W002", "base_weight": 1.0, "connection_type": "waterway",  "connection_conditions": [] },
                { "target_node_id": "W002", "base_weight": 1.0, "connection_type": "tunnel",    "connection_conditions": [] },
                { "target_node_id": "W002", "base_weight": 1.0, "connection_type": "drone",     "connection_conditions": [] },
                { "target_node_id": "W002", "base_weight": 1.0, "connection_type": "trail",     "connection_conditions": [] },
                { "target_node_id": "W002", "base_weight": 1.0, "connection_type": "bridge",    "connection_conditions": [] },
                { "target_node_id": "W002", "base_weight": 1.0, "connection_type": "manual",    "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W002",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 1, "longitude": 1 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        }
    ])");

    graph.build(map);
    auto edges = graph.getEdges(NodeType::FulfillmentCenter);
    ASSERT_EQ(edges.size(), 7u);

    // Collect costs into a set to verify each expected value appears.
    std::vector<double> costs;
    for (const auto& e : edges)
    {
        costs.push_back(e.cost);
    }
    std::sort(costs.begin(), costs.end());

    // Expected costs (base=1.0, no conditions → totalModifier=1.0):
    // rail=0.7, waterway=0.9, tunnel=1.1, drone=1.2, trail=1.3, bridge=1.4, manual=1.6
    const std::vector<double> expected = {0.7, 0.9, 1.1, 1.2, 1.3, 1.4, 1.6};
    ASSERT_EQ(costs.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        EXPECT_NEAR(costs[i], expected[i], 1e-9);
    }
}

// ===========================================================================
// getEdges() — type filtering
// ===========================================================================

TEST_F(GraphTest, GetEdges_FulfillmentCenter_ReturnsOnlyFCEdges)
{
    // Map with one FC→FC edge and one Market→Market edge.
    json map = json::parse(R"([
        {
            "node_id": "W001",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "W002", "base_weight": 5.0, "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W002",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 1, "longitude": 1 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        },
        {
            "node_id": "H001",
            "node_type": "market",
            "node_location": { "latitude": 2, "longitude": 2 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "H002", "base_weight": 3.0, "connection_type": "rail", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "H002",
            "node_type": "market",
            "node_location": { "latitude": 3, "longitude": 3 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        }
    ])");

    graph.build(map);

    auto fcEdges = graph.getEdges(NodeType::FulfillmentCenter);
    auto marketEdges = graph.getEdges(NodeType::Market);

    ASSERT_EQ(fcEdges.size(), 1u);
    ASSERT_EQ(marketEdges.size(), 1u);

    EXPECT_EQ(fcEdges[0].from_id, "W001");
    EXPECT_EQ(fcEdges[0].to_id, "W002");

    EXPECT_EQ(marketEdges[0].from_id, "H001");
    EXPECT_EQ(marketEdges[0].to_id, "H002");
}

// ===========================================================================
// getActiveNodes() — type filtering
// ===========================================================================

TEST_F(GraphTest, GetActiveNodes_FulfillmentCenter_ReturnsOnlyFCNodes)
{
    json map = json::parse(R"([
        {
            "node_id": "W001",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        },
        {
            "node_id": "H001",
            "node_type": "market",
            "node_location": { "latitude": 1, "longitude": 1 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        }
    ])");

    graph.build(map);

    auto fcNodes = graph.getActiveNodes(NodeType::FulfillmentCenter);
    auto marketNodes = graph.getActiveNodes(NodeType::Market);

    ASSERT_EQ(fcNodes.size(), 1u);
    EXPECT_EQ(fcNodes[0].id, "W001");

    ASSERT_EQ(marketNodes.size(), 1u);
    EXPECT_EQ(marketNodes[0].id, "H001");
}

// ===========================================================================
// hasNode() and getNodeType()
// ===========================================================================

TEST_F(GraphTest, HasNode_ExistingNode_ReturnsTrue)
{
    graph.build(twoFulfillmentCenterMap());
    EXPECT_TRUE(graph.hasNode("W001"));
    EXPECT_TRUE(graph.hasNode("W002"));
}

TEST_F(GraphTest, HasNode_NonExistentNode_ReturnsFalse)
{
    graph.build(twoFulfillmentCenterMap());
    EXPECT_FALSE(graph.hasNode("W999"));
}

TEST_F(GraphTest, GetNodeType_FulfillmentCenter_ReturnsCorrectType)
{
    graph.build(twoFulfillmentCenterMap());
    EXPECT_EQ(graph.getNodeType("W001"), NodeType::FulfillmentCenter);
}

TEST_F(GraphTest, GetNodeType_Market_ReturnsCorrectType)
{
    graph.build(twoMarketMap());
    EXPECT_EQ(graph.getNodeType("H001"), NodeType::Market);
}

TEST_F(GraphTest, GetNodeType_NonExistentNode_ThrowsOutOfRange)
{
    graph.build(twoFulfillmentCenterMap());
    EXPECT_THROW(graph.getNodeType("DOES_NOT_EXIST"), std::out_of_range);
}

// ===========================================================================
// Edge capacity field
// ===========================================================================

TEST_F(GraphTest, Build_EdgeCapacity_TruncatedFromBaseWeight)
{
    // base_weight = 10.7 → capacity must be 10 (floor/truncation).
    json map = json::parse(R"([
        {
            "node_id": "W001",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                {
                    "target_node_id": "W002",
                    "base_weight": 10.7,
                    "connection_type": "road",
                    "connection_conditions": []
                }
            ]
        },
        {
            "node_id": "W002",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 1, "longitude": 1 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        }
    ])");

    graph.build(map);
    auto edges = graph.getEdges(NodeType::FulfillmentCenter);
    ASSERT_EQ(edges.size(), 1u);
    EXPECT_EQ(edges[0].capacity, 10);
}

// ===========================================================================
// Mixed graph (fulfillment centers + markets in same payload)
// ===========================================================================

TEST_F(GraphTest, Build_MixedGraph_TotalNodeAndEdgeCounts)
{
    json map = json::parse(R"([
        {
            "node_id": "W001",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "W002", "base_weight": 5.0, "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W002",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 1, "longitude": 1 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        },
        {
            "node_id": "H001",
            "node_type": "market",
            "node_location": { "latitude": 2, "longitude": 2 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "H002", "base_weight": 3.0, "connection_type": "rail", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "H002",
            "node_type": "market",
            "node_location": { "latitude": 3, "longitude": 3 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        }
    ])");

    graph.build(map);

    EXPECT_EQ(graph.nodeCount(), 4u);
    EXPECT_EQ(graph.edgeCount(), 2u);

    EXPECT_EQ(graph.getActiveNodes(NodeType::FulfillmentCenter).size(), 2u);
    EXPECT_EQ(graph.getActiveNodes(NodeType::Market).size(), 2u);
    EXPECT_EQ(graph.getEdges(NodeType::FulfillmentCenter).size(), 1u);
    EXPECT_EQ(graph.getEdges(NodeType::Market).size(), 1u);
}
