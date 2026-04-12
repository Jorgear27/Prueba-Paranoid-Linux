#include "ford_fulkerson.hpp"

#include <stdexcept>

// ---------------------------------------------------------------------------
// Public methods
// ---------------------------------------------------------------------------

FordFulkerson::Result FordFulkerson::compute(const Graph& graph, const std::string& sourceId, const std::string& sinkId)
{
    // ------------------------------------------------------------------
    // Pre-condition checks.
    // ------------------------------------------------------------------
    if (!graph.hasNode(sourceId))
    {
        throw std::invalid_argument("[FordFulkerson] Source node not found in graph: " + sourceId);
    }

    if (!graph.hasNode(sinkId))
    {
        throw std::invalid_argument("[FordFulkerson] Sink node not found in graph: " + sinkId);
    }

    if (graph.getNodeType(sourceId) != NodeType::FulfillmentCenter)
    {
        throw std::invalid_argument("[FordFulkerson] Source node is not a fulfillment center: " + sourceId);
    }

    if (graph.getNodeType(sinkId) != NodeType::FulfillmentCenter)
    {
        throw std::invalid_argument("[FordFulkerson] Sink node is not a fulfillment center: " + sinkId);
    }

    if (sourceId == sinkId)
    {
        throw std::invalid_argument("[FordFulkerson] Source and sink must be different nodes: " + sourceId);
    }

    const std::vector<Node> fcNodes = graph.getActiveNodes(NodeType::FulfillmentCenter);

    if (fcNodes.empty())
    {
        throw std::invalid_argument("[FordFulkerson] Graph contains no active fulfillment-center nodes.");
    }

    // ------------------------------------------------------------------
    // Build the initial residual graph from the FC edge list.
    //
    // residual[u][v] starts at the original capacity of edge u→v.
    // We also pre-initialise residual[v][u] = 0 for every original edge
    // u→v so that BFS can traverse backward edges later on.
    // ------------------------------------------------------------------
    const std::vector<Edge> fcEdges = graph.getEdges(NodeType::FulfillmentCenter);

    // Residual capacity map: residual[u][v] = remaining capacity u→v.
    std::unordered_map<std::string, std::unordered_map<std::string, int>> residual;

    // original[u][v] = capacity of edge u→v as given by the graph.
    std::unordered_map<std::string, std::unordered_map<std::string, int>> original;

    for (const Edge& edge : fcEdges)
    {
        // Forward edge: add its capacity (accumulate if parallel edges exist).
        residual[edge.from_id][edge.to_id] += edge.capacity;
        original[edge.from_id][edge.to_id] += edge.capacity;

        // Backward edge: ensure entry exists with 0 if not already present.
        residual[edge.to_id][edge.from_id] += 0;
    }

    // ------------------------------------------------------------------
    // Main Edmonds-Karp loop.
    //
    // Each iteration:
    //   1. BFS finds the shortest augmenting path source → sink.
    //   2. We trace the path back through `parent` to find the bottleneck
    //   3. We push flow equal to the bottleneck along the path.
    //   4. We add the bottleneck to max_flow.
    // ------------------------------------------------------------------
    Result result;

    std::unordered_map<std::string, std::string> parent;

    while (bfs(residual, sourceId, sinkId, parent))
    {
        // ------------------------------------------------------------------
        // Trace path backwards, from sink back to source and find the bottleneck,
        // the minimum remaining capacity of any edge along the path.
        // ------------------------------------------------------------------
        int bottleneck = std::numeric_limits<int>::max();

        for (std::string v = sinkId; v != sourceId; v = parent.at(v))
        {
            const std::string& u = parent.at(v);
            bottleneck = std::min(bottleneck, residual.at(u).at(v));
        }

        // ------------------------------------------------------------------
        // Push flow along the path.
        // ------------------------------------------------------------------
        for (std::string v = sinkId; v != sourceId; v = parent.at(v))
        {
            const std::string& u = parent.at(v);
            residual.at(u).at(v) -= bottleneck; // Consume forward capacity.
            residual.at(v).at(u) += bottleneck; // Add backward capacity.
        }

        result.max_flow += bottleneck;
    }

    // ------------------------------------------------------------------
    // Derive the flow_matrix from original vs. residual capacities.
    //
    // For every original forward edge u→v:
    //   net_flow(u,v) = original_capacity(u,v) - residual_capacity(u,v)
    //
    // This value is always >= 0 because residual can never exceed original
    // on a forward edge (we only decrement forward and increment backward).
    //
    // We only emit entries for original edges, keeping the output clean.
    // ------------------------------------------------------------------
    for (const auto& [u, targets] : original)
    {
        for (const auto& [v, cap] : targets)
        {
            const int remaining = residual.count(u) != 0 && residual.at(u).count(v) != 0 ? residual.at(u).at(v) : cap;
            result.flow_matrix[u][v] = cap - remaining;
        }
    }

    // ------------------------------------------------------------------
    // Log summary.
    // ------------------------------------------------------------------
    Logger::getInstance().log("FordFulkerson", "[INFO] Completed. Source: " + sourceId + ", Sink: " + sinkId +
                                                   ", Max flow: " + std::to_string(result.max_flow) + ".");

    return result;
}

// ---------------------------------------------------------------------------
// Private methods
// ---------------------------------------------------------------------------

bool FordFulkerson::bfs(const std::unordered_map<std::string, std::unordered_map<std::string, int>>& residual,
                        const std::string& sourceId, const std::string& sinkId,
                        std::unordered_map<std::string, std::string>& parent)
{
    // Reset parent map from any previous call.
    parent.clear();

    // Visited set: a node is visited once its parent has been recorded.
    // The source is treated as visited from the start so we never loop back.
    std::unordered_set<std::string> visited;
    visited.insert(sourceId);

    std::queue<std::string> queue;
    queue.push(sourceId);

    while (!queue.empty())
    {
        const std::string u = queue.front();
        queue.pop();

        if (residual.count(u) == 0)
        {
            continue; // u has no outgoing residual edges.
        }

        // Iterate over all neighbours of u in the residual graph.
        for (const auto& [v, cap] : residual.at(u))
        {
            // Only follow edges with strictly positive residual capacity,
            // and only visit each node once (BFS shortest-path guarantee).
            if (visited.count(v) == 0 && cap > 0)
            {
                parent[v] = u;
                visited.insert(v);

                if (v == sinkId)
                {
                    return true; // Sink reached — augmenting path found.
                }

                queue.push(v);
            }
        }
    }

    return false; // Sink not reachable — no augmenting path exists.
}
