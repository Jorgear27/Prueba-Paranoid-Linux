/**
 * @file graph.hpp
 * @brief Graph data structure for the routing engine.
 *
 * Nodes represent physical locations (fulfillment centers or markets).
 * Edges represent transport connections between them, carrying a pre-computed
 * cost derived from the connection type and environmental conditions.
 *
 * @version 0.1
 * @date 2026-04-02
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef GRAPH_HPP
#define GRAPH_HPP

#include "log.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Value used as the cost of a blocked edge.
 *
 */
constexpr double BLOCKED_EDGE_COST = std::numeric_limits<double>::infinity();

/**
 * @brief Classifies a node as either a fulfillment center (warehouse) or a
 *        market (hub).
 *
 */
enum class NodeType
{
    FulfillmentCenter, ///< Warehouse node: holds inventory, used for flow and Hamiltonian calculations.
    Market             ///< Hub node: no inventory, used for shortest-path routing.
};

/**
 * @brief Represents a single location node in the supply-chain graph.
 *
 * Fields are populated directly from the JSON map uploaded via POST /map.
 *
 */
struct Node
{
    std::string id;   ///< Unique identifier (e.g. "W001", "H042").
    NodeType type;    ///< FulfillmentCenter or Market.
    double latitude;  ///< Geographic latitude of the node.
    double longitude; ///< Geographic longitude of the node.
    bool is_secure;   ///< False → treat as obstacle, exclude from graph.
    bool is_active;   ///< False → treat as obstacle, exclude from graph.
};

/**
 * @brief Represents a directed transport edge between two nodes.
 *
 * The cost field stores the final pre-computed value:
 *   cost = base_weight × type_modifier × Σ(condition_modifiers)
 *
 * Blocked edges (cost == BLOCKED_EDGE_COST) are never inserted into the
 * adjacency list.
 *
 */
struct Edge
{
    std::string from_id; ///< Source node identifier.
    std::string to_id;   ///< Destination node identifier.
    double cost;         ///< Pre-computed edge cost (may be BLOCKED_EDGE_COST).
    int capacity;        ///< Integer capacity for flow algorithms (= floor(base_weight)).
};

/**
 * @brief Thread-safe, immutable-after-build graph of the supply-chain network.
 *
 * The graph is rebuilt on every POST /map request. Algorithms receive a const reference.
 * This allows all query methods to be const and safe to call concurrently once build() returns.
 *
 */
class Graph
{
  public:
    /**
     * @brief Default constructor. Produces an empty, valid graph.
     */
    Graph() = default;

    /**
     * @brief Parses a JSON node array and populates the graph.
     *
     * @param mapJson JSON array of node objects as defined by the API schema.
     * @throws std::invalid_argument if mapJson is not a JSON array.
     * @throws std::invalid_argument if any node_id is duplicated.
     */
    void build(const nlohmann::json& mapJson);

    /**
     * @brief Returns all active nodes of the requested type.
     *
     * Only nodes that passed the is_secure and is_active filters during build()
     *
     * @param type NodeType::FulfillmentCenter or NodeType::Market.
     * @return Vector of matching Node structs.
     */
    std::vector<Node> getActiveNodes(NodeType type) const;

    /**
     * @brief Returns all non-blocked edges whose both endpoints share the
     *        requested node type.
     *
     * @param type NodeType::FulfillmentCenter or NodeType::Market.
     * @return Vector of matching Edge structs.
     */
    std::vector<Edge> getEdges(NodeType type) const;

    /**
     * @brief Checks whether a node with the given id exists in the active graph.
     *
     * @param id Node identifier to look up.
     * @return true if the node was loaded and passed the obstacle filter.
     */
    bool hasNode(const std::string& id) const;

    /**
     * @brief Returns the NodeType for a given node id.
     *
     * @param id Node identifier.
     * @return NodeType of the node.
     * @throws std::out_of_range if the id is not present in the active graph.
     */
    NodeType getNodeType(const std::string& id) const;

    /**
     * @brief Returns the total number of active nodes in the graph.
     */
    std::size_t nodeCount() const;

    /**
     * @brief Returns the total number of non-blocked edges in the graph.
     */
    std::size_t edgeCount() const;

    /**
     * @brief Returns true if the graph contains no active nodes.
     *
     * A freshly default-constructed Graph, or one built from an empty array, is empty.
     */
    bool empty() const;

  private:
    /**
     * @brief Parses a single node JSON object into a Node struct.
     *
     * @param nodeJson A single element from the map JSON array.
     * @return Populated Node struct.
     * @throws std::invalid_argument on missing or malformed required fields.
     */
    Node parseNode(const nlohmann::json& nodeJson) const;

    /**
     * @brief Computes the final edge cost from its components.
     *
     * Formula: base_weight × type_modifier × Σ(condition_modifiers)
     *
     * @param baseWeight   Raw base weight from JSON (must be > 0).
     * @param connType     Connection type string (e.g. "rail", "road").
     * @param conditions   List of condition strings (e.g. "foggy", "rain").
     * @return Computed edge cost, or BLOCKED_EDGE_COST.
     */
    double computeEdgeCost(double baseWeight, const std::string& connType,
                           const std::vector<std::string>& conditions) const;

    /**
     * @brief Returns the multiplicative modifier for a connection type.
     *
     * | Type      | Modifier |
     * |-----------|----------|
     * | rail      |   0.7    |
     * | waterway  |   0.9    |
     * | road      |   1.0    |
     * | tunnel    |   1.1    |
     * | drone     |   1.2    |
     * | trail     |   1.3    |
     * | bridge    |   1.4    |
     * | manual    |   1.6    |
     * | blocked   |   ∞      |
     *
     * @param connType Connection type string (case-sensitive).
     * @return Modifier value
     */
    double connectionTypeModifier(const std::string& connType) const;

    /**
     * @brief Returns the additive modifier sum for a list of conditions.
     *
     * | Condition          | Modifier |
     * |--------------------|----------|
     * | reinforced         |  −0.3    |
     * | cleared            |  −0.2    |
     * | foggy              |  +0.1    |
     * | rain               |  +0.2    |
     * | infected_activity  |  +0.3    |
     *
     * @param conditions List of condition strings.
     * @return Sum of all recognised condition modifiers.
     */
    double conditionModifiersSum(const std::vector<std::string>& conditions) const;

    // -----------------------------------------------------------------------
    // Internal storage
    // -----------------------------------------------------------------------

    /// Active and secure nodes keyed by node_id.
    std::unordered_map<std::string, Node> nodes_;

    /// Adjacency list: source node_id → list of outgoing valid edges.
    std::unordered_map<std::string, std::vector<Edge>> adj_;

    /// Count of all non-blocked edges across all adjacency lists.
    std::size_t edgeCount_ = 0;
};

#endif
