#include "ford_fulkerson.hpp"
#include "graph.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Test graph definitions
//
// All capacity values are derived from base_weight cast to int (truncated).
// ---------------------------------------------------------------------------
namespace
{

/**
 * @brief Single direct edge: W001 → W002, capacity 10.
 *
 * Max flow = 10 (only one path, bottlenecked by the single edge).
 */
json singleEdgeMap()
{
    return json::parse(R"([
        {
            "node_id": "W001",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "W002", "base_weight": 10.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W002",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 1, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        }
    ])");
}

/**
 * @brief Two-path graph.
 *
 * W001 → W002: capacity 10
 * W001 → W003: capacity  8
 * W002 → W004: capacity  6
 * W003 → W004: capacity  9
 *
 * Paths from W001 to W004:
 *   Path A: W001→W002→W004  bottleneck = min(10, 6) = 6
 *   Path B: W001→W003→W004  bottleneck = min(8, 9) = 8
 *
 * Max flow = 6 + 8 = 14.
 */
json twoPathMap()
{
    return json::parse(R"([
        {
            "node_id": "W001",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "W002", "base_weight": 10.0,
                  "connection_type": "road", "connection_conditions": [] },
                { "target_node_id": "W003", "base_weight":  8.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W002",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 1, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "W004", "base_weight": 6.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W003",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 1 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "W004", "base_weight": 9.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W004",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 1, "longitude": 1 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        }
    ])");
}

/**
 * @brief Bottleneck in the middle.
 *
 * W001 → W002: capacity 100
 * W002 → W003: capacity   3   ← bottleneck
 * W003 → W004: capacity 100
 *
 * Max flow W001→W004 = 3.
 */
json bottleneckMap()
{
    return json::parse(R"([
        {
            "node_id": "W001",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "W002", "base_weight": 100.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W002",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 1, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "W003", "base_weight": 3.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W003",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 2, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "W004", "base_weight": 100.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W004",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 3, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        }
    ])");
}

/**
 * @brief No path from source to sink.
 *
 * W001 → W002 exists, but there is no edge reaching W003.
 * Max flow W001→W003 = 0.
 */
json noPathMap()
{
    return json::parse(R"([
        {
            "node_id": "W001",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "W002", "base_weight": 5.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W002",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 1, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        },
        {
            "node_id": "W003",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 2, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        }
    ])");
}

/**
 * @brief Mixed-type graph: two FC nodes and two market nodes.
 *
 * Market nodes H001/H002 and their edges must be invisible to Ford-Fulkerson.
 * Max flow W001→W002 = 7.
 */
json mixedTypeMap()
{
    return json::parse(R"([
        {
            "node_id": "W001",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "W002", "base_weight": 7.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W002",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 1, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        },
        {
            "node_id": "H001",
            "node_type": "market",
            "node_location": { "latitude": 2, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "H002", "base_weight": 99.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "H002",
            "node_type": "market",
            "node_location": { "latitude": 3, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        }
    ])");
}

/**
 * @brief Capacity truncation: base_weight 9.9 → capacity 9.
 *
 * W001 → W002: base_weight=9.9 → capacity = static_cast<int>(9.9) = 9.
 * Max flow = 9.
 */
json truncatedCapacityMap()
{
    return json::parse(R"([
        {
            "node_id": "W001",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "W002", "base_weight": 9.9,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W002",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 1, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        }
    ])");
}

/**
 * @brief Graph that requires backward-edge relaxation (Edmonds-Karp correctness).
 *
 * This is the classic example that DFS Ford-Fulkerson may solve inefficiently
 * in some graphs, but Edmonds-Karp (BFS) always solves correctly.
 *
 * Topology:
 *   W001 → W002: capacity 10
 *   W001 → W003: capacity 10
 *   W002 → W003: capacity  1
 *   W002 → W004: capacity 10
 *   W003 → W004: capacity 10
 *
 * Max flow W001→W004:
 *   Path A: W001→W002→W004  flow=10 (fills W001→W002 and W002→W004)
 *   Path B: W001→W003→W004  flow=10 (fills W001→W003 and W003→W004)
 *   The W002→W003 edge with capacity 1 is never needed for the optimum.
 *   Max flow = 20.
 */
json backwardEdgeMap()
{
    return json::parse(R"([
        {
            "node_id": "W001",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "W002", "base_weight": 10.0,
                  "connection_type": "road", "connection_conditions": [] },
                { "target_node_id": "W003", "base_weight": 10.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W002",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 1, "longitude": 0 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "W003", "base_weight": 1.0,
                  "connection_type": "road", "connection_conditions": [] },
                { "target_node_id": "W004", "base_weight": 10.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W003",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 1 },
            "is_secure": true,
            "is_active": true,
            "connections": [
                { "target_node_id": "W004", "base_weight": 10.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W004",
            "node_type": "fulfillment_center",
            "node_location": { "latitude": 1, "longitude": 1 },
            "is_secure": true,
            "is_active": true,
            "connections": []
        }
    ])");
}

} // namespace

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------
class FordFulkersonTest : public ::testing::Test
{
  protected:
    Graph graph;
    FordFulkerson ff;
};

// ===========================================================================
// Pre-condition / input validation
// ===========================================================================

TEST_F(FordFulkersonTest, Compute_SourceNotInGraph_ThrowsInvalidArgument)
{
    graph.build(singleEdgeMap());
    EXPECT_THROW(ff.compute(graph, "W999", "W002"), std::invalid_argument);
}

TEST_F(FordFulkersonTest, Compute_SinkNotInGraph_ThrowsInvalidArgument)
{
    graph.build(singleEdgeMap());
    EXPECT_THROW(ff.compute(graph, "W001", "W999"), std::invalid_argument);
}

TEST_F(FordFulkersonTest, Compute_SourceIsMarketNode_ThrowsInvalidArgument)
{
    graph.build(mixedTypeMap());
    EXPECT_THROW(ff.compute(graph, "H001", "W002"), std::invalid_argument);
}

TEST_F(FordFulkersonTest, Compute_SinkIsMarketNode_ThrowsInvalidArgument)
{
    graph.build(mixedTypeMap());
    EXPECT_THROW(ff.compute(graph, "W001", "H002"), std::invalid_argument);
}

TEST_F(FordFulkersonTest, Compute_SourceEqualsSink_ThrowsInvalidArgument)
{
    graph.build(singleEdgeMap());
    EXPECT_THROW(ff.compute(graph, "W001", "W001"), std::invalid_argument);
}

TEST_F(FordFulkersonTest, Compute_NoFCNodes_ThrowsInvalidArgument)
{
    json marketOnlyMap = json::parse(R"([
        {
            "node_id": "H001", "node_type": "market",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true, "is_active": true, "connections": []
        }
    ])");

    graph.build(marketOnlyMap);
    // W001 and W002 do not exist — both lookups throw.
    EXPECT_THROW(ff.compute(graph, "W001", "W002"), std::invalid_argument);
}

// ===========================================================================
// Single edge
// ===========================================================================

TEST_F(FordFulkersonTest, Compute_SingleEdge_MaxFlowEqualsCapacity)
{
    graph.build(singleEdgeMap());
    const FordFulkerson::Result result = ff.compute(graph, "W001", "W002");

    EXPECT_EQ(result.max_flow, 10);
}

TEST_F(FordFulkersonTest, Compute_SingleEdge_FlowMatrixReflectsFullFlow)
{
    graph.build(singleEdgeMap());
    const FordFulkerson::Result result = ff.compute(graph, "W001", "W002");

    ASSERT_EQ(result.flow_matrix.count("W001"), 1u);
    ASSERT_EQ(result.flow_matrix.at("W001").count("W002"), 1u);
    EXPECT_EQ(result.flow_matrix.at("W001").at("W002"), 10);
}

TEST_F(FordFulkersonTest, Compute_SingleEdge_NoBackwardEdgesInFlowMatrix)
{
    graph.build(singleEdgeMap());
    const FordFulkerson::Result result = ff.compute(graph, "W001", "W002");

    // W002→W001 is a backward (residual bookkeeping) edge — must not appear.
    const bool backwardPresent =
        result.flow_matrix.count("W002") != 0 && result.flow_matrix.at("W002").count("W001") != 0;
    EXPECT_FALSE(backwardPresent);
}

// ===========================================================================
// Two-path graph — parallel routes
// ===========================================================================

TEST_F(FordFulkersonTest, Compute_TwoPath_MaxFlowIsCorrect)
{
    graph.build(twoPathMap());
    const FordFulkerson::Result result = ff.compute(graph, "W001", "W004");

    EXPECT_EQ(result.max_flow, 14);
}

TEST_F(FordFulkersonTest, Compute_TwoPath_FlowConservationAtIntermediateNodes)
{
    graph.build(twoPathMap());
    const FordFulkerson::Result result = ff.compute(graph, "W001", "W004");

    // Flow conservation at W002: flow_in(W001→W002) == flow_out(W002→W004).
    const int flowIntoW002 = result.flow_matrix.count("W001") != 0 && result.flow_matrix.at("W001").count("W002") != 0
                                 ? result.flow_matrix.at("W001").at("W002")
                                 : 0;
    const int flowOutW002 = result.flow_matrix.count("W002") != 0 && result.flow_matrix.at("W002").count("W004") != 0
                                ? result.flow_matrix.at("W002").at("W004")
                                : 0;
    EXPECT_EQ(flowIntoW002, flowOutW002);

    // Flow conservation at W003: flow_in(W001→W003) == flow_out(W003→W004).
    const int flowIntoW003 = result.flow_matrix.count("W001") != 0 && result.flow_matrix.at("W001").count("W003") != 0
                                 ? result.flow_matrix.at("W001").at("W003")
                                 : 0;
    const int flowOutW003 = result.flow_matrix.count("W003") != 0 && result.flow_matrix.at("W003").count("W004") != 0
                                ? result.flow_matrix.at("W003").at("W004")
                                : 0;
    EXPECT_EQ(flowIntoW003, flowOutW003);
}

TEST_F(FordFulkersonTest, Compute_TwoPath_TotalFlowAtSource)
{
    graph.build(twoPathMap());
    const FordFulkerson::Result result = ff.compute(graph, "W001", "W004");

    // Sum of all outgoing flow from W001 must equal max_flow.
    int totalOut = 0;
    if (result.flow_matrix.count("W001") != 0)
    {
        for (const auto& [v, flow] : result.flow_matrix.at("W001"))
        {
            totalOut += flow;
        }
    }
    EXPECT_EQ(totalOut, result.max_flow);
}

TEST_F(FordFulkersonTest, Compute_TwoPath_FlowDoesNotExceedCapacity)
{
    graph.build(twoPathMap());
    const FordFulkerson::Result result = ff.compute(graph, "W001", "W004");

    // Each individual edge flow must be within [0, capacity].
    // Capacities: W001→W002=10, W001→W003=8, W002→W004=6, W003→W004=9.
    const auto checkEdge = [&](const std::string& u, const std::string& v, int cap) {
        if (result.flow_matrix.count(u) != 0 && result.flow_matrix.at(u).count(v) != 0)
        {
            const int f = result.flow_matrix.at(u).at(v);
            EXPECT_GE(f, 0) << u << "→" << v;
            EXPECT_LE(f, cap) << u << "→" << v;
        }
    };

    checkEdge("W001", "W002", 10);
    checkEdge("W001", "W003", 8);
    checkEdge("W002", "W004", 6);
    checkEdge("W003", "W004", 9);
}

// ===========================================================================
// Bottleneck graph
// ===========================================================================

TEST_F(FordFulkersonTest, Compute_Bottleneck_MaxFlowIsBottleneckCapacity)
{
    graph.build(bottleneckMap());
    const FordFulkerson::Result result = ff.compute(graph, "W001", "W004");

    EXPECT_EQ(result.max_flow, 3);
}

TEST_F(FordFulkersonTest, Compute_Bottleneck_BottleneckEdgeIsFullySaturated)
{
    graph.build(bottleneckMap());
    const FordFulkerson::Result result = ff.compute(graph, "W001", "W004");

    // W002→W003 has capacity 3 and must carry exactly 3 units.
    ASSERT_EQ(result.flow_matrix.count("W002"), 1u);
    ASSERT_EQ(result.flow_matrix.at("W002").count("W003"), 1u);
    EXPECT_EQ(result.flow_matrix.at("W002").at("W003"), 3);
}

// ===========================================================================
// No path from source to sink
// ===========================================================================

TEST_F(FordFulkersonTest, Compute_NoPath_MaxFlowIsZero)
{
    graph.build(noPathMap());
    const FordFulkerson::Result result = ff.compute(graph, "W001", "W003");

    EXPECT_EQ(result.max_flow, 0);
}

TEST_F(FordFulkersonTest, Compute_NoPath_FlowMatrixHasNoPositiveFlow)
{
    graph.build(noPathMap());
    const FordFulkerson::Result result = ff.compute(graph, "W001", "W003");

    for (const auto& [u, targets] : result.flow_matrix)
    {
        for (const auto& [v, flow] : targets)
        {
            EXPECT_EQ(flow, 0) << "Expected zero flow on " << u << "→" << v;
        }
    }
}

// ===========================================================================
// Mixed-type graph
// ===========================================================================

TEST_F(FordFulkersonTest, Compute_MixedGraph_MaxFlowIgnoresMarketEdges)
{
    graph.build(mixedTypeMap());
    const FordFulkerson::Result result = ff.compute(graph, "W001", "W002");

    // Market edge H001→H002 (capacity 99) must not contribute.
    EXPECT_EQ(result.max_flow, 7);
}

TEST_F(FordFulkersonTest, Compute_MixedGraph_FlowMatrixContainsOnlyFCEdges)
{
    graph.build(mixedTypeMap());
    const FordFulkerson::Result result = ff.compute(graph, "W001", "W002");

    // H001 and H002 must not appear anywhere in the flow_matrix.
    EXPECT_EQ(result.flow_matrix.count("H001"), 0u);
    EXPECT_EQ(result.flow_matrix.count("H002"), 0u);

    for (const auto& [u, targets] : result.flow_matrix)
    {
        EXPECT_EQ(targets.count("H001"), 0u) << "H001 appears as target of " << u;
        EXPECT_EQ(targets.count("H002"), 0u) << "H002 appears as target of " << u;
    }
}

// ===========================================================================
// Capacity truncation
// ===========================================================================

TEST_F(FordFulkersonTest, Compute_TruncatedCapacity_MaxFlowUsesTruncatedValue)
{
    graph.build(truncatedCapacityMap());
    const FordFulkerson::Result result = ff.compute(graph, "W001", "W002");

    // base_weight=9.9 → capacity=9 (truncated, not rounded).
    EXPECT_EQ(result.max_flow, 9);
}

// ===========================================================================
// Backward-edge relaxation correctness (Edmonds-Karp guarantee)
// ===========================================================================

TEST_F(FordFulkersonTest, Compute_BackwardEdge_MaxFlowIsCorrect)
{
    graph.build(backwardEdgeMap());
    const FordFulkerson::Result result = ff.compute(graph, "W001", "W004");

    EXPECT_EQ(result.max_flow, 20);
}

TEST_F(FordFulkersonTest, Compute_BackwardEdge_FlowDoesNotExceedAnyCapacity)
{
    graph.build(backwardEdgeMap());
    const FordFulkerson::Result result = ff.compute(graph, "W001", "W004");

    // capacities: W001→W002=10, W001→W003=10, W002→W003=1,
    //             W002→W004=10, W003→W004=10
    const auto checkEdge = [&](const std::string& u, const std::string& v, int cap) {
        if (result.flow_matrix.count(u) != 0 && result.flow_matrix.at(u).count(v) != 0)
        {
            const int f = result.flow_matrix.at(u).at(v);
            EXPECT_GE(f, 0) << u << "→" << v;
            EXPECT_LE(f, cap) << u << "→" << v;
        }
    };

    checkEdge("W001", "W002", 10);
    checkEdge("W001", "W003", 10);
    checkEdge("W002", "W003", 1);
    checkEdge("W002", "W004", 10);
    checkEdge("W003", "W004", 10);
}

TEST_F(FordFulkersonTest, Compute_BackwardEdge_FlowConservationAtIntermediates)
{
    graph.build(backwardEdgeMap());
    const FordFulkerson::Result result = ff.compute(graph, "W001", "W004");

    // Helper: compute net inflow − net outflow at an intermediate node.
    // For a correct flow this should be 0.
    const auto netBalance = [&](const std::string& node) -> int {
        int inflow = 0;
        int outflow = 0;

        for (const auto& [u, targets] : result.flow_matrix)
        {
            if (targets.count(node) != 0)
            {
                inflow += targets.at(node);
            }
        }

        if (result.flow_matrix.count(node) != 0)
        {
            for (const auto& [v, flow] : result.flow_matrix.at(node))
            {
                outflow += flow;
            }
        }

        return inflow - outflow;
    };

    // W002 and W003 are intermediate — their balance must be 0.
    EXPECT_EQ(netBalance("W002"), 0);
    EXPECT_EQ(netBalance("W003"), 0);
}

// ===========================================================================
// Instance reuse — same FordFulkerson object, different calls, different graphs
// ===========================================================================

TEST_F(FordFulkersonTest, Compute_Reuse_DifferentGraphsProduceIndependentResults)
{
    graph.build(singleEdgeMap());
    const FordFulkerson::Result r1 = ff.compute(graph, "W001", "W002");

    graph.build(bottleneckMap());
    const FordFulkerson::Result r2 = ff.compute(graph, "W001", "W004");

    EXPECT_EQ(r1.max_flow, 10);
    EXPECT_EQ(r2.max_flow, 3);
}
