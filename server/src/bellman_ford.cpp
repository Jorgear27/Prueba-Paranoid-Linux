#include "bellman_ford.hpp"

#include <stdexcept>

BellmanFord::Result BellmanFord::compute(const Graph& graph, const std::string& sourceId)
{
    // ------------------------------------------------------------------
    // Pre-condition checks: source node must exist, be a market node,
    // and there must be at least one market node in the graph.
    // ------------------------------------------------------------------
    if (!graph.hasNode(sourceId))
    {
        throw std::invalid_argument("[BellmanFord] Source node not found in graph: " + sourceId);
    }

    if (graph.getNodeType(sourceId) != NodeType::Market)
    {
        throw std::invalid_argument("[BellmanFord] Source node is not a market node: " + sourceId);
    }

    const std::vector<Node> marketNodes = graph.getActiveNodes(NodeType::Market);

    if (marketNodes.empty())
    {
        throw std::invalid_argument("[BellmanFord] Graph contains no active market nodes.");
    }

    // ------------------------------------------------------------------
    // Retrieve the filtered edge list. Graph::getEdges(Market) returns
    // valid edges, where both endpoints are active market nodes.
    // ------------------------------------------------------------------
    const std::vector<Edge> edges = graph.getEdges(NodeType::Market);

    const std::size_t V = marketNodes.size();

    // ------------------------------------------------------------------
    // Initialisation: source → 0, everything else → UNREACHABLE.
    // We prefer unordered_map instead of an index-based array.
    // ------------------------------------------------------------------
    Result result;

    for (const Node& node : marketNodes)
    {
        result.distances[node.id] = UNREACHABLE;
    }

    result.distances[sourceId] = 0.0;

    // ------------------------------------------------------------------
    // Main relaxation loop: at most V − 1 passes over all edges.
    // ------------------------------------------------------------------
    for (std::size_t pass = 0; pass < V - 1; ++pass)
    {
        bool anyRelaxation = false; // Early-exit optimisation.

        for (const Edge& edge : edges)
        {
            const double srcDist = result.distances.at(edge.from_id);

            // Skip edges from unreachable nodes.
            if (srcDist >= UNREACHABLE)
            {
                continue;
            }

            const double candidate = srcDist + edge.cost;

            if (candidate < result.distances.at(edge.to_id))
            {
                result.distances.at(edge.to_id) = candidate;
                result.predecessors[edge.to_id] = edge.from_id;
                anyRelaxation = true;
            }
        }

        // If no distance changed in this full pass, the algorithm has
        // already found all shortest paths and can exit early.
        if (!anyRelaxation)
        {
            Logger::getInstance().log("BellmanFord",
                                      "[INFO] Converged early after " + std::to_string(pass + 1) + " pass(es).");
            result.has_negative_cycle = false;
            return result;
        }
    }

    // ------------------------------------------------------------------
    // Negative-cycle detection: one additional (V-th) relaxation pass.
    //
    // If any distance can still be reduced after V − 1 passes, it can only
    // be because there is a negative-weight cycle reachable from the source
    // ------------------------------------------------------------------
    for (const Edge& edge : edges)
    {
        const double srcDist = result.distances.at(edge.from_id);

        if (srcDist >= UNREACHABLE)
        {
            continue;
        }

        if (srcDist + edge.cost < result.distances.at(edge.to_id))
        {
            Logger::getInstance().log("BellmanFord",
                                      "[WARN] Negative cycle detected reachable from source: " + sourceId);

            result.has_negative_cycle = true;
            return result;
        }
    }

    // ------------------------------------------------------------------
    // Log amount of reachable nodes before from the source.
    // ------------------------------------------------------------------
    std::size_t reachable = 0;
    for (const auto& [id, dist] : result.distances)
    {
        if (dist < UNREACHABLE)
        {
            ++reachable;
        }
    }

    Logger::getInstance().log("BellmanFord", "[INFO] Completed. Source: " + sourceId + ", reachable market nodes: " +
                                                 std::to_string(reachable) + " / " + std::to_string(V) + ".");

    result.has_negative_cycle = false;
    return result;
}
