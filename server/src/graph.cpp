#include "graph.hpp"

#include <limits>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Public methods
// ---------------------------------------------------------------------------

void Graph::build(const nlohmann::json& mapJson)
{
    if (!mapJson.is_array())
    {
        throw std::invalid_argument("[Graph] Map payload must be a JSON array of node objects.");
    }

    // Reset all state
    nodes_.clear();
    adj_.clear();
    edgeCount_ = 0;

    // -----------------------------------------------------------------------
    // Step 1: parse and validate every node, discard obstacles.
    // -----------------------------------------------------------------------
    for (const auto& nodeJson : mapJson)
    {
        Node node = parseNode(nodeJson);

        // Obstacle check. Skip insecure and inactive nodes.
        if (!node.is_secure || !node.is_active)
        {
            Logger::getInstance().log("Graph", "[INFO] Skipping inactive/insecure node: " + node.id);
            continue;
        }

        // Duplicate node_id check.
        if (nodes_.count(node.id) != 0)
        {
            throw std::invalid_argument("[Graph] Duplicate node_id detected: " + node.id);
        }

        nodes_.emplace(node.id, node);
        adj_.emplace(node.id, std::vector<Edge>{}); // Ensure an entry exists even for leaf nodes.
    }

    // -----------------------------------------------------------------------
    // Step 2: build edges now that the full node set is known.
    //
    // Dangling connections (target not in active node set) are discarded.
    // Blocked edges (cost == BLOCKED_EDGE_COST) are discarded.
    // -----------------------------------------------------------------------
    for (const auto& nodeJson : mapJson)
    {
        // Skip nodes that were filtered out in step 1.
        if (!nodeJson.value("is_secure", true) || !nodeJson.value("is_active", true))
        {
            continue;
        }

        const std::string fromId = nodeJson.at("node_id").get<std::string>();

        // This node may have been a duplicate that threw an exception in step 1. We
        // have to make sure the node is present in the active set before processing its connections
        if (nodes_.count(fromId) == 0)
        {
            continue;
        }

        if (!nodeJson.contains("connections") || !nodeJson.at("connections").is_array())
        {
            continue; // A node with no connections is valid.
        }

        for (const auto& connJson : nodeJson.at("connections"))
        {
            const std::string toId = connJson.at("target_node_id").get<std::string>();

            // Dangling connection: target was filtered out or does not exist.
            if (nodes_.count(toId) == 0)
            {
                Logger::getInstance().log("Graph", "[INFO] Skipping dangling connection: " + fromId + " -> " + toId);
                continue;
            }

            const double baseWeight = connJson.at("base_weight").get<double>();
            if (baseWeight <= 0.0)
            {
                Logger::getInstance().log("Graph", "[WARN] Non-positive base_weight on connection " + fromId + " -> " +
                                                       toId + ". Skipping.");
                continue;
            }

            const std::string connType = connJson.value("connection_type", "road");

            // Parse conditions array (optional field).
            std::vector<std::string> conditions;
            if (connJson.contains("connection_conditions") && connJson.at("connection_conditions").is_array())
            {
                for (const auto& cond : connJson.at("connection_conditions"))
                {
                    conditions.push_back(cond.get<std::string>());
                }
            }

            const double cost = computeEdgeCost(baseWeight, connType, conditions);

            // Blocked edges are discarded
            if (cost >= BLOCKED_EDGE_COST)
            {
                Logger::getInstance().log("Graph", "[INFO] Discarding blocked edge: " + fromId + " -> " + toId);
                continue;
            }

            Edge edge;
            edge.from_id = fromId;
            edge.to_id = toId;
            edge.cost = cost;
            edge.capacity = static_cast<int>(baseWeight); // Truncate for flow algorithms.

            adj_.at(fromId).push_back(edge);
            ++edgeCount_;
        }
    }

    Logger::getInstance().log("Graph", "[INFO] Graph built: " + std::to_string(nodes_.size()) + " active nodes, " +
                                           std::to_string(edgeCount_) + " edges.");
}

// ---------------------------------------------------------------------------

std::vector<Node> Graph::getActiveNodes(NodeType type) const
{
    std::vector<Node> result;
    result.reserve(nodes_.size());

    for (const auto& [id, node] : nodes_)
    {
        if (node.type == type)
        {
            result.push_back(node);
        }
    }

    return result;
}

// ---------------------------------------------------------------------------

std::vector<Edge> Graph::getEdges(NodeType type) const
{
    std::vector<Edge> result;
    result.reserve(edgeCount_);

    for (const auto& [fromId, edges] : adj_)
    {
        // Filter by source node type only.
        if (nodes_.count(fromId) == 0 || nodes_.at(fromId).type != type)
        {
            continue;
        }

        for (const Edge& edge : edges)
        {
            // Double-check the target also belongs to the requested type.
            if (nodes_.count(edge.to_id) != 0 && nodes_.at(edge.to_id).type == type)
            {
                result.push_back(edge);
            }
        }
    }

    return result;
}

// ---------------------------------------------------------------------------

bool Graph::hasNode(const std::string& id) const
{
    return nodes_.count(id) != 0;
}

// ---------------------------------------------------------------------------

NodeType Graph::getNodeType(const std::string& id) const
{
    return nodes_.at(id).type; // Throws std::out_of_range if id not present.
}

// ---------------------------------------------------------------------------

std::size_t Graph::nodeCount() const
{
    return nodes_.size();
}

// ---------------------------------------------------------------------------

std::size_t Graph::edgeCount() const
{
    return edgeCount_;
}

// ---------------------------------------------------------------------------

bool Graph::empty() const
{
    return nodes_.empty();
}

// ---------------------------------------------------------------------------
// Private methods
// ---------------------------------------------------------------------------

Node Graph::parseNode(const nlohmann::json& nodeJson) const
{
    Node node;
    node.id = nodeJson.at("node_id").get<std::string>();

    const std::string typeStr = nodeJson.at("node_type").get<std::string>();
    if (typeStr == "fulfillment_center")
    {
        node.type = NodeType::FulfillmentCenter;
    }
    else if (typeStr == "market")
    {
        node.type = NodeType::Market;
    }
    else
    {
        throw std::invalid_argument("[Graph] Unknown node_type '" + typeStr + "' for node: " + node.id);
    }

    const auto& location = nodeJson.at("node_location");
    node.latitude = location.at("latitude").get<double>();
    node.longitude = location.at("longitude").get<double>();

    // Optional obstacle flags — default to true (active and secure).
    node.is_secure = nodeJson.value("is_secure", true);
    node.is_active = nodeJson.value("is_active", true);

    return node;
}

// ---------------------------------------------------------------------------

double Graph::computeEdgeCost(double baseWeight, const std::string& connType,
                              const std::vector<std::string>& conditions) const
{
    const double typeModifier = connectionTypeModifier(connType);

    // A blocked type modifier.
    if (typeModifier >= BLOCKED_EDGE_COST)
    {
        return BLOCKED_EDGE_COST;
    }

    const double condSum = conditionModifiersSum(conditions);

    // Formula: base_weight × type_modifier × Σ(conditions).
    // We add 1.0 to the condition modifiers, because in case there are no
    // conditions the cost should be just base_weight × type_modifier.
    const double totalModifier = 1.0 + condSum;

    // If accumulated condition modifiers are below or equal to zero, treat as blocked.
    if (totalModifier <= 0.0)
    {
        Logger::getInstance().log("Graph", "[WARN] Condition modifiers produced a non-positive total modifier (" +
                                               std::to_string(totalModifier) + "). Treating edge as blocked.");
        return BLOCKED_EDGE_COST;
    }

    return baseWeight * typeModifier * totalModifier;
}

// ---------------------------------------------------------------------------

double Graph::connectionTypeModifier(const std::string& connType) const
{
    // Modifier table as specified in the feature requirements.
    static const std::unordered_map<std::string, double> modifiers = {
        {"rail", 0.7},   {"waterway", 0.9}, {"road", 1.0},
        {"tunnel", 1.1}, {"drone", 1.2},    {"trail", 1.3},
        {"bridge", 1.4}, {"manual", 1.6},   {"blocked", BLOCKED_EDGE_COST},
    };

    const auto it = modifiers.find(connType);
    if (it == modifiers.end())
    {
        Logger::getInstance().log("Graph", "[WARN] Unknown connection_type '" + connType +
                                               "'. Defaulting to road modifier (1.0).");
        return 1.0;
    }

    return it->second;
}

// ---------------------------------------------------------------------------

double Graph::conditionModifiersSum(const std::vector<std::string>& conditions) const
{
    // Additive modifier table as specified in the feature requirements.
    static const std::unordered_map<std::string, double> modifiers = {
        {"reinforced", -0.3}, {"cleared", -0.2}, {"foggy", +0.1}, {"rain", +0.2}, {"infected_activity", +0.3},
    };

    double sum = 0.0;
    for (const std::string& cond : conditions)
    {
        const auto it = modifiers.find(cond);
        if (it == modifiers.end())
        {
            Logger::getInstance().log("Graph", "[WARN] Unknown connection_condition '" + cond + "'. Ignoring.");
            continue;
        }
        sum += it->second;
    }

    return sum;
}
