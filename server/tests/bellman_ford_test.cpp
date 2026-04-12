#include "bellman_ford.hpp"
#include "graph.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Graphs simulation: we build small graphs directly in the test code using JSON,
// which is parsed and fed to Graph::build(). This allows us to define precise
// graph structures and edge conditions for testing Bellman-Ford.
// ---------------------------------------------------------------------------
namespace
{

/**
 * @brief Linear chain: H001 → H002 → H003 (road, no conditions).
 *
 * Expected distances from H001:
 *   H001 → 0.0
 *   H002 → 5.0   (direct edge, cost = 5.0)
 *   H003 → 8.0   (5.0 + 3.0)
 */
json linearChainMap()
{
    return json::parse(R"([
        {
            "node_id": "H001",
            "node_type": "market",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "H002", "base_weight": 5.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "H002",
            "node_type": "market",
            "node_location": { "latitude": 1, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "H003", "base_weight": 3.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "H003",
            "node_type": "market",
            "node_location": { "latitude": 2, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        }
    ])");
}

/**
 * @brief Diamond graph: H001 → H002 and H001 → H003 → H002.
 *
 * H001 → H002: cost 10.0 (direct road)
 * H001 → H003: cost  2.0 (direct road)
 * H003 → H002: cost  3.0 (direct road)
 *
 * Expected distances from H001:
 *   H001 → 0.0
 *   H002 → 5.0   (via H003: 2.0 + 3.0, cheaper than direct 10.0)
 *   H003 → 2.0
 */
json diamondMap()
{
    return json::parse(R"([
        {
            "node_id": "H001",
            "node_type": "market",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "H002", "base_weight": 10.0,
                  "connection_type": "road", "connection_conditions": [] },
                { "target_node_id": "H003", "base_weight":  2.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "H002",
            "node_type": "market",
            "node_location": { "latitude": 1, "longitude": 1 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        },
        {
            "node_id": "H003",
            "node_type": "market",
            "node_location": { "latitude": 0, "longitude": 1 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "H002", "base_weight": 3.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        }
    ])");
}

/**
 * @brief Disconnected graph: H001 ↔ H002, isolated H003.
 *
 * Expected distances from H001:
 *   H001 → 0.0
 *   H002 → 4.0
 *   H003 → UNREACHABLE (not in the distances map)
 */
json disconnectedMap()
{
    return json::parse(R"([
        {
            "node_id": "H001",
            "node_type": "market",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "H002", "base_weight": 4.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "H002",
            "node_type": "market",
            "node_location": { "latitude": 1, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        },
        {
            "node_id": "H003",
            "node_type": "market",
            "node_location": { "latitude": 5, "longitude": 5 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        }
    ])");
}

/**
 * @brief Single-node graph: only H001, no edges.
 *
 * Expected distances from H001:
 *   H001 → 0.0  (source to itself)
 */
json singleNodeMap()
{
    return json::parse(R"([
        {
            "node_id": "H001",
            "node_type": "market",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        }
    ])");
}

/**
 * @brief Mixed graph: two markets H001/H002, two FCs W001/W002.
 *
 * The FC nodes must be invisible to Bellman-Ford.
 */
json mixedTypeMap()
{
    return json::parse(R"([
        {
            "node_id": "H001",
            "node_type": "market",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "H002", "base_weight": 7.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "H002",
            "node_type": "market",
            "node_location": { "latitude": 1, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        },
        {
            "node_id": "W001",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 2, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "W002", "base_weight": 9.0,
                  "connection_type": "rail", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W002",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 3, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        }
    ])");
}

/**
 * @brief Graph with rail edges and conditions so we can verify the
 *        cost formula is applied correctly end-to-end.
 *
 * H001 → H002: base=10, rail(0.7), foggy(+0.1)
 *   cost = 10 × 0.7 × (1.0 + 0.1) = 10 × 0.7 × 1.1 = 7.7
 *
 * H001 → H003: base=10, road(1.0), reinforced(-0.3)
 *   cost = 10 × 1.0 × (1.0 - 0.3) = 10 × 1.0 × 0.7 = 7.0
 *
 * H003 → H002: base=1, road(1.0), no conditions
 *   cost = 1 × 1.0 × 1.0 = 1.0
 *
 * Expected distances from H001:
 *   H001 → 0.0
 *   H002 → 7.7   ONLY if direct path (7.7) < indirect (H003: 7.0 + 1.0 = 8.0)
 *                 → direct wins: 7.7
 *   H003 → 7.0
 */
json conditionCostMap()
{
    return json::parse(R"([
        {
            "node_id": "H001",
            "node_type": "market",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "H002", "base_weight": 10.0,
                  "connection_type": "rail", "connection_conditions": ["foggy"] },
                { "target_node_id": "H003", "base_weight": 10.0,
                  "connection_type": "road", "connection_conditions": ["reinforced"] }
            ]
        },
        {
            "node_id": "H002",
            "node_type": "market",
            "node_location": { "latitude": 1, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        },
        {
            "node_id": "H003",
            "node_type": "market",
            "node_location": { "latitude": 0, "longitude": 1 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "H002", "base_weight": 1.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        }
    ])");
}

} // namespace

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------
class BellmanFordTest : public ::testing::Test
{
  protected:
    Graph graph;
    BellmanFord bf;
};

// ===========================================================================
// Pre-condition / input validation
// ===========================================================================

TEST_F(BellmanFordTest, Compute_SourceNotInGraph_ThrowsInvalidArgument)
{
    graph.build(linearChainMap());
    EXPECT_THROW(bf.compute(graph, "H999"), std::invalid_argument);
}

TEST_F(BellmanFordTest, Compute_SourceIsFulfillmentCenter_ThrowsInvalidArgument)
{
    graph.build(mixedTypeMap());
    // W001 exists but is a FC, not a market.
    EXPECT_THROW(bf.compute(graph, "W001"), std::invalid_argument);
}

TEST_F(BellmanFordTest, Compute_NoMarketNodes_ThrowsInvalidArgument)
{
    // Build a graph with only fulfillment centers.
    json fcOnlyMap = json::parse(R"([
        {
            "node_id": "W001",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true, "is_active": true,
            "connections": []
        }
    ])");

    graph.build(fcOnlyMap);
    EXPECT_THROW(bf.compute(graph, "W001"), std::invalid_argument);
}

// ===========================================================================
// Single node
// ===========================================================================

TEST_F(BellmanFordTest, Compute_SingleNode_SourceDistanceIsZero)
{
    graph.build(singleNodeMap());
    const BellmanFord::Result result = bf.compute(graph, "H001");

    EXPECT_FALSE(result.has_negative_cycle);
    ASSERT_EQ(result.distances.count("H001"), 1u);
    EXPECT_DOUBLE_EQ(result.distances.at("H001"), 0.0);
    EXPECT_TRUE(result.predecessors.empty());
}

// ===========================================================================
// Linear chain
// ===========================================================================

TEST_F(BellmanFordTest, Compute_LinearChain_SourceDistanceIsZero)
{
    graph.build(linearChainMap());
    const BellmanFord::Result result = bf.compute(graph, "H001");

    EXPECT_FALSE(result.has_negative_cycle);
    EXPECT_DOUBLE_EQ(result.distances.at("H001"), 0.0);
}

TEST_F(BellmanFordTest, Compute_LinearChain_DirectEdgeDistance)
{
    graph.build(linearChainMap());
    const BellmanFord::Result result = bf.compute(graph, "H001");

    // H001 → H002: base=5, road, no conditions → cost = 5.0
    EXPECT_DOUBLE_EQ(result.distances.at("H002"), 5.0);
}

TEST_F(BellmanFordTest, Compute_LinearChain_TwoHopDistance)
{
    graph.build(linearChainMap());
    const BellmanFord::Result result = bf.compute(graph, "H001");

    // H001 → H002 → H003: 5.0 + 3.0 = 8.0
    EXPECT_DOUBLE_EQ(result.distances.at("H003"), 8.0);
}

TEST_F(BellmanFordTest, Compute_LinearChain_PredecessorChain)
{
    graph.build(linearChainMap());
    const BellmanFord::Result result = bf.compute(graph, "H001");

    // Predecessor of H002 must be H001.
    ASSERT_EQ(result.predecessors.count("H002"), 1u);
    EXPECT_EQ(result.predecessors.at("H002"), "H001");

    // Predecessor of H003 must be H002.
    ASSERT_EQ(result.predecessors.count("H003"), 1u);
    EXPECT_EQ(result.predecessors.at("H003"), "H002");

    // Source has no predecessor.
    EXPECT_EQ(result.predecessors.count("H001"), 0u);
}

// ===========================================================================
// Diamond graph
// ===========================================================================

TEST_F(BellmanFordTest, Compute_Diamond_ChoosesShortestPath)
{
    graph.build(diamondMap());
    const BellmanFord::Result result = bf.compute(graph, "H001");

    EXPECT_FALSE(result.has_negative_cycle);

    // Direct H001 → H002 costs 10.0.
    // Indirect H001 → H003 → H002 costs 2.0 + 3.0 = 5.0.
    // Algorithm must choose the indirect path.
    EXPECT_DOUBLE_EQ(result.distances.at("H002"), 5.0);
    EXPECT_DOUBLE_EQ(result.distances.at("H003"), 2.0);
}

TEST_F(BellmanFordTest, Compute_Diamond_PredecessorOfH002IsH003)
{
    graph.build(diamondMap());
    const BellmanFord::Result result = bf.compute(graph, "H001");

    ASSERT_EQ(result.predecessors.count("H002"), 1u);
    EXPECT_EQ(result.predecessors.at("H002"), "H003");

    ASSERT_EQ(result.predecessors.count("H003"), 1u);
    EXPECT_EQ(result.predecessors.at("H003"), "H001");
}

// ===========================================================================
// Disconnected graph
// ===========================================================================

TEST_F(BellmanFordTest, Compute_Disconnected_ReachableNodeHasCorrectDistance)
{
    graph.build(disconnectedMap());
    const BellmanFord::Result result = bf.compute(graph, "H001");

    EXPECT_FALSE(result.has_negative_cycle);
    EXPECT_DOUBLE_EQ(result.distances.at("H001"), 0.0);
    EXPECT_DOUBLE_EQ(result.distances.at("H002"), 4.0);
}

TEST_F(BellmanFordTest, Compute_Disconnected_UnreachableNodeDistanceIsINF)
{
    graph.build(disconnectedMap());
    const BellmanFord::Result result = bf.compute(graph, "H001");

    // H003 is isolated — its distance must remain UNREACHABLE (infinity).
    ASSERT_EQ(result.distances.count("H003"), 1u);
    EXPECT_EQ(result.distances.at("H003"), UNREACHABLE);
}

TEST_F(BellmanFordTest, Compute_Disconnected_UnreachableNodeHasNoPredecessor)
{
    graph.build(disconnectedMap());
    const BellmanFord::Result result = bf.compute(graph, "H001");

    EXPECT_EQ(result.predecessors.count("H003"), 0u);
}

// ===========================================================================
// Mixed-type graph
// ===========================================================================

TEST_F(BellmanFordTest, Compute_MixedGraph_FCNodesAbsentFromDistances)
{
    graph.build(mixedTypeMap());
    const BellmanFord::Result result = bf.compute(graph, "H001");

    EXPECT_FALSE(result.has_negative_cycle);

    // Only market nodes should appear in distances.
    EXPECT_EQ(result.distances.count("W001"), 0u);
    EXPECT_EQ(result.distances.count("W002"), 0u);

    // Market nodes must be present.
    EXPECT_EQ(result.distances.count("H001"), 1u);
    EXPECT_EQ(result.distances.count("H002"), 1u);
}

TEST_F(BellmanFordTest, Compute_MixedGraph_MarketEdgeCostCorrect)
{
    graph.build(mixedTypeMap());
    const BellmanFord::Result result = bf.compute(graph, "H001");

    // H001 → H002: base=7, road, no conditions → cost = 7.0
    EXPECT_DOUBLE_EQ(result.distances.at("H002"), 7.0);
}

// ===========================================================================
// Edge cost formula end-to-end
// ===========================================================================

TEST_F(BellmanFordTest, Compute_ConditionCost_DirectRailPathWithFoggy)
{
    graph.build(conditionCostMap());
    const BellmanFord::Result result = bf.compute(graph, "H001");

    EXPECT_FALSE(result.has_negative_cycle);

    // H001 → H002 direct: base=10, rail(0.7), foggy(+0.1)
    //   cost = 10 × 0.7 × (1.0 + 0.1) = 7.7
    // H001 → H003 → H002: 7.0 + 1.0 = 8.0
    // Direct path wins.
    EXPECT_NEAR(result.distances.at("H002"), 7.7, 1e-9);
}

TEST_F(BellmanFordTest, Compute_ConditionCost_ReinforcedRoadPath)
{
    graph.build(conditionCostMap());
    const BellmanFord::Result result = bf.compute(graph, "H001");

    // H001 → H003: base=10, road(1.0), reinforced(-0.3)
    //   cost = 10 × 1.0 × (1.0 - 0.3) = 7.0
    EXPECT_NEAR(result.distances.at("H003"), 7.0, 1e-9);
}

// ===========================================================================
// No negative cycle on all valid graphs (absence test)
// ===========================================================================

TEST_F(BellmanFordTest, Compute_LinearChain_HasNegativeCycleIsFalse)
{
    graph.build(linearChainMap());
    const BellmanFord::Result result = bf.compute(graph, "H001");
    EXPECT_FALSE(result.has_negative_cycle);
}

TEST_F(BellmanFordTest, Compute_Diamond_HasNegativeCycleIsFalse)
{
    graph.build(diamondMap());
    const BellmanFord::Result result = bf.compute(graph, "H001");
    EXPECT_FALSE(result.has_negative_cycle);
}

TEST_F(BellmanFordTest, Compute_Disconnected_HasNegativeCycleIsFalse)
{
    graph.build(disconnectedMap());
    const BellmanFord::Result result = bf.compute(graph, "H001");
    EXPECT_FALSE(result.has_negative_cycle);
}

// ===========================================================================
// Reachability from a non-source node
// ===========================================================================

TEST_F(BellmanFordTest, Compute_LinearChain_FromMiddleNode_H001IsUnreachable)
{
    graph.build(linearChainMap());

    // Chain is H001 → H002 → H003 (directed). Starting from H002:
    // H001 is not reachable (no back-edge), H003 is reachable.
    const BellmanFord::Result result = bf.compute(graph, "H002");

    EXPECT_FALSE(result.has_negative_cycle);
    EXPECT_DOUBLE_EQ(result.distances.at("H002"), 0.0);
    EXPECT_DOUBLE_EQ(result.distances.at("H003"), 3.0);
    EXPECT_EQ(result.distances.at("H001"), UNREACHABLE);
}

// ===========================================================================
// Check that instance is stateless, each call should produce an independent Result
// ===========================================================================

TEST_F(BellmanFordTest, Compute_Reuse_DifferentGraphs_ProducesIndependentResults)
{
    graph.build(linearChainMap());
    const BellmanFord::Result r1 = bf.compute(graph, "H001");

    graph.build(diamondMap());
    const BellmanFord::Result r2 = bf.compute(graph, "H001");

    // r1 and r2 must be independent.
    EXPECT_DOUBLE_EQ(r1.distances.at("H002"), 5.0); // linear: direct 5.0
    EXPECT_DOUBLE_EQ(r2.distances.at("H002"), 5.0); // diamond: via H003: 2+3=5
    EXPECT_DOUBLE_EQ(r2.distances.at("H003"), 2.0); // only in diamond result
}
