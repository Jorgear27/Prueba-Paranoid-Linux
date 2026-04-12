#include "graph.hpp"
#include "kaufmann_malgrange.hpp"
#include <algorithm>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <unordered_set>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Test graph definitions
// ---------------------------------------------------------------------------
namespace
{

/**
 * @brief Single FC node — trivial Hamiltonian circuit (node → itself).
 *
 * Expected: feasible = true, path = ["W001", "W001"].
 */
json singleNodeMap()
{
    return json::parse(R"([
        {
            "node_id": "W001", "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true, "is_active": true,
            "connections": []
        }
    ])");
}

/**
 * @brief Complete directed graph on 3 nodes: every node connects to every
 *        other in both directions.
 *
 * W001 ↔ W002 ↔ W003 ↔ W001 (all six directed edges present)
 *
 * A Hamiltonian circuit exists, W001→W002→W003→W001.
 * Expected: feasible = true.
 */
json completeThreeNodeMap()
{
    return json::parse(R"([
        {
            "node_id": "W001", "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true, "is_active": true,
            "connections": [
                { "target_node_id": "W002", "base_weight": 1.0,
                  "connection_type": "road", "connection_conditions": [] },
                { "target_node_id": "W003", "base_weight": 1.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W002", "node_type": "fulfillment_center",
            "node_location": { "latitude": 1, "longitude": 0 },
            "is_secure": true, "is_active": true,
            "connections": [
                { "target_node_id": "W001", "base_weight": 1.0,
                  "connection_type": "road", "connection_conditions": [] },
                { "target_node_id": "W003", "base_weight": 1.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W003", "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 1 },
            "is_secure": true, "is_active": true,
            "connections": [
                { "target_node_id": "W001", "base_weight": 1.0,
                  "connection_type": "road", "connection_conditions": [] },
                { "target_node_id": "W002", "base_weight": 1.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        }
    ])");
}

/**
 * @brief Directed cycle on 4 nodes: W001→W002→W003→W004→W001.
 *
 * This is a Hamiltonian circuit by itself,
 * Expected: feasible = true.
 */
json directedCycleFourNodeMap()
{
    return json::parse(R"([
        {
            "node_id": "W001", "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true, "is_active": true,
            "connections": [
                { "target_node_id": "W002", "base_weight": 1.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W002", "node_type": "fulfillment_center",
            "node_location": { "latitude": 1, "longitude": 0 },
            "is_secure": true, "is_active": true,
            "connections": [
                { "target_node_id": "W003", "base_weight": 1.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W003", "node_type": "fulfillment_center",
            "node_location": { "latitude": 2, "longitude": 0 },
            "is_secure": true, "is_active": true,
            "connections": [
                { "target_node_id": "W004", "base_weight": 1.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W004", "node_type": "fulfillment_center",
            "node_location": { "latitude": 3, "longitude": 0 },
            "is_secure": true, "is_active": true,
            "connections": [
                { "target_node_id": "W001", "base_weight": 1.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        }
    ])");
}

/**
 * @brief Linear chain only: W001→W002→W003. No back-edges.
 *
 * A Hamiltonian circuit is IMPOSSIBLE because:
 *   - W003 has no outgoing edges, so the circuit can never close.
 * Expected: feasible = false.
 */
json linearChainNoCycleMap()
{
    return json::parse(R"([
        {
            "node_id": "W001", "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true, "is_active": true,
            "connections": [
                { "target_node_id": "W002", "base_weight": 1.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W002", "node_type": "fulfillment_center",
            "node_location": { "latitude": 1, "longitude": 0 },
            "is_secure": true, "is_active": true,
            "connections": [
                { "target_node_id": "W003", "base_weight": 1.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W003", "node_type": "fulfillment_center",
            "node_location": { "latitude": 2, "longitude": 0 },
            "is_secure": true, "is_active": true,
            "connections": []
        }
    ])");
}

/**
 * @brief Disconnected FC subgraph: {W001, W002} and {W003} with no edges
 *        between the two components.
 *
 * A Hamiltonian circuit is IMPOSSIBLE because W003 is unreachable from W001 and W002
 * Expected: feasible = false.
 */
json disconnectedMap()
{
    return json::parse(R"([
        {
            "node_id": "W001", "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true, "is_active": true,
            "connections": [
                { "target_node_id": "W002", "base_weight": 1.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W002", "node_type": "fulfillment_center",
            "node_location": { "latitude": 1, "longitude": 0 },
            "is_secure": true, "is_active": true,
            "connections": [
                { "target_node_id": "W001", "base_weight": 1.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W003", "node_type": "fulfillment_center",
            "node_location": { "latitude": 5, "longitude": 5 },
            "is_secure": true, "is_active": true,
            "connections": []
        }
    ])");
}

/**
 * @brief Mixed graph: two FC nodes with a circuit, plus two market nodes.
 *
 * FC subgraph: W001→W002→W001 (circuit of length 2).
 * Expected: feasible = true (the 2-node directed circuit W001→W002→W001).
 */
json mixedTypeMap()
{
    return json::parse(R"([
        {
            "node_id": "W001", "node_type": "fulfillment_center",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true, "is_active": true,
            "connections": [
                { "target_node_id": "W002", "base_weight": 1.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W002", "node_type": "fulfillment_center",
            "node_location": { "latitude": 1, "longitude": 0 },
            "is_secure": true, "is_active": true,
            "connections": [
                { "target_node_id": "W001", "base_weight": 1.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "H001", "node_type": "market",
            "node_location": { "latitude": 2, "longitude": 0 },
            "is_secure": true, "is_active": true,
            "connections": [
                { "target_node_id": "H002", "base_weight": 1.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "H002", "node_type": "market",
            "node_location": { "latitude": 3, "longitude": 0 },
            "is_secure": true, "is_active": true,
            "connections": []
        }
    ])");
}

/**
 * @brief Builds a JSON map with exactly (n) FC nodes arranged as a complete
 *        bidirectional graph
 */
json completeDirectedGraphOfSize(int n)
{
    json nodes = json::array();
    for (int i = 1; i <= n; ++i)
    {
        json connections = json::array();
        for (int j = 1; j <= n; ++j)
        {
            if (i == j)
                continue;
            connections.push_back({{"target_node_id", "W" + std::to_string(j)},
                                   {"base_weight", 1.0},
                                   {"connection_type", "road"},
                                   {"connection_conditions", json::array()}});
        }
        nodes.push_back({{"node_id", "W" + std::to_string(i)},
                         {"node_type", "fulfillment_center"},
                         {"node_location", {{"latitude", i}, {"longitude", 0}}},
                         {"is_secure", true},
                         {"is_active", true},
                         {"connections", connections}});
    }
    return nodes;
}

// ---------------------------------------------------------------------------
// Helper: verify a path is a valid Hamiltonian circuit on a set of node ids.
// ---------------------------------------------------------------------------
bool isValidHamiltonianCircuit(const std::vector<std::string>& path, const std::vector<std::string>& expectedNodes,
                               const json& mapJson)
{
    const int n = static_cast<int>(expectedNodes.size());

    // Path must have exactly n+1 entries (n nodes + closing node).
    if (static_cast<int>(path.size()) != n + 1)
    {
        return false;
    }

    // First and last must be the same.
    if (path.front() != path.back())
    {
        return false;
    }

    // The interior nodes must be a permutation of expectedNodes.
    std::vector<std::string> interior(path.begin(), path.end() - 1);
    std::vector<std::string> sorted_interior = interior;
    std::vector<std::string> sorted_expected = expectedNodes;
    std::sort(sorted_interior.begin(), sorted_interior.end());
    std::sort(sorted_expected.begin(), sorted_expected.end());
    if (sorted_interior != sorted_expected)
    {
        return false;
    }

    // Every consecutive pair (path[i], path[i+1]) must be a valid directed edge.
    // Build edge set from mapJson.
    std::unordered_set<std::string> edgeSet;
    for (const auto& nodeJson : mapJson)
    {
        if (!nodeJson.contains("connections"))
            continue;
        const std::string fromId = nodeJson["node_id"].get<std::string>();
        for (const auto& conn : nodeJson["connections"])
        {
            const std::string toId = conn["target_node_id"].get<std::string>();
            edgeSet.insert(fromId + "->" + toId);
        }
    }

    for (int i = 0; i < n; ++i)
    {
        const std::string edgeKey = path[i] + "->" + path[i + 1];
        if (edgeSet.count(edgeKey) == 0)
        {
            return false;
        }
    }

    return true;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Test cases
// ---------------------------------------------------------------------------
class KaufmannMalgrangeTest : public ::testing::Test
{
  protected:
    Graph graph;
    KaufmannMalgrange km;
};

// ===========================================================================
// Empty FC subgraph
// ===========================================================================

TEST_F(KaufmannMalgrangeTest, Compute_NoFCNodes_ReturnsError)
{
    json marketOnlyMap = json::parse(R"([
        {
            "node_id": "H001", "node_type": "market",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true, "is_active": true, "connections": []
        }
    ])");

    graph.build(marketOnlyMap);
    const KaufmannMalgrange::Result result = km.compute(graph);

    EXPECT_FALSE(result.feasible);
    EXPECT_FALSE(result.error.empty());
    EXPECT_TRUE(result.path.empty());
}

// ===========================================================================
// Node-count limit
// ===========================================================================

TEST_F(KaufmannMalgrangeTest, Compute_ExactlyMaxNodes_Succeeds)
{
    // MAX_NODES nodes — must not return an error.
    graph.build(completeDirectedGraphOfSize(KaufmannMalgrange::MAX_NODES));
    const KaufmannMalgrange::Result result = km.compute(graph);

    EXPECT_TRUE(result.error.empty());
    // A complete directed graph always has a Hamiltonian circuit.
    EXPECT_TRUE(result.feasible);
}

TEST_F(KaufmannMalgrangeTest, Compute_OneOverMaxNodes_ReturnsError)
{
    graph.build(completeDirectedGraphOfSize(KaufmannMalgrange::MAX_NODES + 1));
    const KaufmannMalgrange::Result result = km.compute(graph);

    EXPECT_FALSE(result.feasible);
    EXPECT_FALSE(result.error.empty());
    EXPECT_TRUE(result.path.empty());
}

// ===========================================================================
// Single node — trivial circuit
// ===========================================================================

TEST_F(KaufmannMalgrangeTest, Compute_SingleNode_FeasibleTrue)
{
    graph.build(singleNodeMap());
    const KaufmannMalgrange::Result result = km.compute(graph);

    EXPECT_TRUE(result.error.empty());
    EXPECT_TRUE(result.feasible);
}

TEST_F(KaufmannMalgrangeTest, Compute_SingleNode_PathIsNodeTwice)
{
    graph.build(singleNodeMap());
    const KaufmannMalgrange::Result result = km.compute(graph);

    ASSERT_EQ(result.path.size(), 2u);
    EXPECT_EQ(result.path[0], "W001");
    EXPECT_EQ(result.path[1], "W001");
}

// ===========================================================================
// Complete 3-node graph — circuit must exist
// ===========================================================================

TEST_F(KaufmannMalgrangeTest, Compute_CompleteThreeNodes_FeasibleTrue)
{
    const json mapJson = completeThreeNodeMap();
    graph.build(mapJson);
    const KaufmannMalgrange::Result result = km.compute(graph);

    EXPECT_TRUE(result.error.empty());
    EXPECT_TRUE(result.feasible);
}

TEST_F(KaufmannMalgrangeTest, Compute_CompleteThreeNodes_PathIsValidCircuit)
{
    const json mapJson = completeThreeNodeMap();
    graph.build(mapJson);
    const KaufmannMalgrange::Result result = km.compute(graph);

    ASSERT_TRUE(result.feasible);
    const std::vector<std::string> nodes = {"W001", "W002", "W003"};
    EXPECT_TRUE(isValidHamiltonianCircuit(result.path, nodes, mapJson));
}

TEST_F(KaufmannMalgrangeTest, Compute_CompleteThreeNodes_PathHasFourEntries)
{
    graph.build(completeThreeNodeMap());
    const KaufmannMalgrange::Result result = km.compute(graph);

    // 3 nodes + 1 closing node = 4 entries.
    ASSERT_TRUE(result.feasible);
    EXPECT_EQ(result.path.size(), 4u);
}

TEST_F(KaufmannMalgrangeTest, Compute_CompleteThreeNodes_FirstEqualsLast)
{
    graph.build(completeThreeNodeMap());
    const KaufmannMalgrange::Result result = km.compute(graph);

    ASSERT_TRUE(result.feasible);
    ASSERT_GE(result.path.size(), 2u);
    EXPECT_EQ(result.path.front(), result.path.back());
}

// ===========================================================================
// Directed 4-node cycle — exactly one circuit
// ===========================================================================

TEST_F(KaufmannMalgrangeTest, Compute_DirectedCycleFourNodes_FeasibleTrue)
{
    const json mapJson = directedCycleFourNodeMap();
    graph.build(mapJson);
    const KaufmannMalgrange::Result result = km.compute(graph);

    EXPECT_TRUE(result.error.empty());
    EXPECT_TRUE(result.feasible);
}

TEST_F(KaufmannMalgrangeTest, Compute_DirectedCycleFourNodes_PathIsValidCircuit)
{
    const json mapJson = directedCycleFourNodeMap();
    graph.build(mapJson);
    const KaufmannMalgrange::Result result = km.compute(graph);

    ASSERT_TRUE(result.feasible);
    const std::vector<std::string> nodes = {"W001", "W002", "W003", "W004"};
    EXPECT_TRUE(isValidHamiltonianCircuit(result.path, nodes, mapJson));
}

TEST_F(KaufmannMalgrangeTest, Compute_DirectedCycleFourNodes_PathHasFiveEntries)
{
    graph.build(directedCycleFourNodeMap());
    const KaufmannMalgrange::Result result = km.compute(graph);

    // 4 nodes + 1 closing node = 5 entries.
    ASSERT_TRUE(result.feasible);
    EXPECT_EQ(result.path.size(), 5u);
}

// ===========================================================================
// Linear chain — no circuit possible
// ===========================================================================

TEST_F(KaufmannMalgrangeTest, Compute_LinearChainNoCycle_FeasibleFalse)
{
    graph.build(linearChainNoCycleMap());
    const KaufmannMalgrange::Result result = km.compute(graph);

    EXPECT_TRUE(result.error.empty()); // No guard triggered — valid input.
    EXPECT_FALSE(result.feasible);
    EXPECT_TRUE(result.path.empty());
}

// ===========================================================================
// Disconnected graph — no circuit possible
// ===========================================================================

TEST_F(KaufmannMalgrangeTest, Compute_Disconnected_FeasibleFalse)
{
    graph.build(disconnectedMap());
    const KaufmannMalgrange::Result result = km.compute(graph);

    EXPECT_TRUE(result.error.empty());
    EXPECT_FALSE(result.feasible);
    EXPECT_TRUE(result.path.empty());
}

// ===========================================================================
// Mixed-type graph — market nodes must be invisible
// ===========================================================================

TEST_F(KaufmannMalgrangeTest, Compute_MixedGraph_OnlyFCNodesInPath)
{
    graph.build(mixedTypeMap());
    const KaufmannMalgrange::Result result = km.compute(graph);

    ASSERT_TRUE(result.feasible);

    for (const std::string& nodeId : result.path)
    {
        EXPECT_TRUE(nodeId == "W001" || nodeId == "W002") << "Unexpected node in path: " << nodeId;
    }
}

TEST_F(KaufmannMalgrangeTest, Compute_MixedGraph_FeasibleTrue)
{
    graph.build(mixedTypeMap());
    const KaufmannMalgrange::Result result = km.compute(graph);

    EXPECT_TRUE(result.error.empty());
    EXPECT_TRUE(result.feasible);
}

TEST_F(KaufmannMalgrangeTest, Compute_MixedGraph_PathIsValidCircuit)
{
    const json mapJson = mixedTypeMap();
    graph.build(mapJson);
    const KaufmannMalgrange::Result result = km.compute(graph);

    ASSERT_TRUE(result.feasible);
    // Only FC nodes matter; build the edge set only for FC edges.
    const std::vector<std::string> fcNodes = {"W001", "W002"};
    EXPECT_TRUE(isValidHamiltonianCircuit(result.path, fcNodes, mapJson));
}

// ===========================================================================
// Path structural invariants (applicable to all feasible results)
// ===========================================================================

TEST_F(KaufmannMalgrangeTest, Compute_FeasibleResult_PathLengthIsVPlusOne)
{
    // 4-node directed cycle: V=4, path must have 5 entries.
    graph.build(directedCycleFourNodeMap());
    const KaufmannMalgrange::Result result = km.compute(graph);

    ASSERT_TRUE(result.feasible);
    const std::size_t V = graph.getActiveNodes(NodeType::FulfillmentCenter).size();
    EXPECT_EQ(result.path.size(), V + 1);
}

TEST_F(KaufmannMalgrangeTest, Compute_FeasibleResult_AllFCNodesAppearExactlyOnce)
{
    graph.build(completeThreeNodeMap());
    const KaufmannMalgrange::Result result = km.compute(graph);

    ASSERT_TRUE(result.feasible);

    // Interior nodes (exclude the closing duplicate) must each appear once.
    std::unordered_map<std::string, int> counts;
    for (std::size_t i = 0; i + 1 < result.path.size(); ++i)
    {
        counts[result.path[i]]++;
    }

    const std::vector<Node> fcNodes = graph.getActiveNodes(NodeType::FulfillmentCenter);
    EXPECT_EQ(counts.size(), fcNodes.size());

    for (const Node& node : fcNodes)
    {
        ASSERT_EQ(counts.count(node.id), 1u) << "Node missing from path: " << node.id;
        EXPECT_EQ(counts.at(node.id), 1) << "Node appears more than once: " << node.id;
    }
}

// ===========================================================================
// Instance reuse — multiple compute() calls on the same KM instance must not interfere
// ===========================================================================

TEST_F(KaufmannMalgrangeTest, Compute_Reuse_DifferentGraphsProduceIndependentResults)
{
    graph.build(completeThreeNodeMap());
    const KaufmannMalgrange::Result r1 = km.compute(graph);

    graph.build(linearChainNoCycleMap());
    const KaufmannMalgrange::Result r2 = km.compute(graph);

    EXPECT_TRUE(r1.feasible);
    EXPECT_FALSE(r2.feasible);
}
