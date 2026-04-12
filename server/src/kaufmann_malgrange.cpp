#include "kaufmann_malgrange.hpp"

// ---------------------------------------------------------------------------
// Public methods
// ---------------------------------------------------------------------------

KaufmannMalgrange::Result KaufmannMalgrange::compute(const Graph& graph)
{
    Result result;

    // ------------------------------------------------------------------
    // Guard 0: no FC nodes at all.
    // ------------------------------------------------------------------
    const std::vector<Node> fcNodes = graph.getActiveNodes(NodeType::FulfillmentCenter);

    if (fcNodes.empty())
    {
        result.error = "[KaufmannMalgrange] Graph contains no active fulfillment-center nodes.";
        Logger::getInstance().log("KaufmannMalgrange", result.error);
        return result;
    }

    // ------------------------------------------------------------------
    // Guard 1: node-count limit — reject before any O(V!) work begins.
    // ------------------------------------------------------------------
    const int V = static_cast<int>(fcNodes.size());

    if (V > MAX_NODES)
    {
        result.error = "[KaufmannMalgrange] FC subgraph exceeds MAX_NODES limit (" + std::to_string(V) + " > " +
                       std::to_string(MAX_NODES) + "). Refusing to run NP-complete backtracking.";
        Logger::getInstance().log("KaufmannMalgrange", result.error);
        return result;
    }

    // ------------------------------------------------------------------
    // Special case: a single node trivially forms a Hamiltonian circuit
    // ------------------------------------------------------------------
    if (V == 1)
    {
        result.feasible = true;
        result.path = {fcNodes[0].id, fcNodes[0].id};
        Logger::getInstance().log("KaufmannMalgrange", "[INFO] Single-node FC subgraph — trivial Hamiltonian circuit.");
        return result;
    }

    // ------------------------------------------------------------------
    // Build the directed adjacency set for the FC subgraph.
    // adj[u] = set of v such that directed edge u→v exists in the FC graph.
    // ------------------------------------------------------------------
    const std::vector<Edge> fcEdges = graph.getEdges(NodeType::FulfillmentCenter);

    std::unordered_map<std::string, std::unordered_set<std::string>> adjDir;

    // Ensure every node has an entry, even those with no outgoing edges.
    for (const Node& node : fcNodes)
    {
        adjDir.emplace(node.id, std::unordered_set<std::string>{});
    }

    for (const Edge& edge : fcEdges)
    {
        adjDir.at(edge.from_id).insert(edge.to_id);
    }

    // ------------------------------------------------------------------
    // Build the undirected adjacency set and degree map for Ore's check.
    // ------------------------------------------------------------------
    std::unordered_map<std::string, std::unordered_set<std::string>> adjUnd;
    for (const Node& node : fcNodes)
    {
        adjUnd.emplace(node.id, std::unordered_set<std::string>{});
    }
    for (const Edge& edge : fcEdges)
    {
        adjUnd.at(edge.from_id).insert(edge.to_id);
        adjUnd.at(edge.to_id).insert(edge.from_id);
    }

    const std::unordered_map<std::string, int> degrees = computeUndirectedDegrees(fcNodes, fcEdges);

    // ------------------------------------------------------------------
    // Phase 1 — Ore's theorem pre-check.
    //
    // Ore's is sufficient but not necessary, so its failure does not prove infeasibility.
    // ------------------------------------------------------------------
    if (oresTheoremHolds(fcNodes, degrees, adjUnd))
    {
        Logger::getInstance().log("KaufmannMalgrange",
                                  "[INFO] Ore's theorem condition satisfied — Hamiltonian circuit is guaranteed.");
    }
    else
    {
        Logger::getInstance().log("KaufmannMalgrange",
                                  "[INFO] Ore's theorem condition not satisfied — backtracking required.");
    }

    // ------------------------------------------------------------------
    // Phase 2 — Backtracking search.
    //
    // We try every node as the starting point. The first complete circuit
    // found is returned immediately.
    // ------------------------------------------------------------------
    for (const Node& startNode : fcNodes)
    {
        std::vector<std::string> path = {startNode.id};
        std::unordered_set<std::string> visited = {startNode.id};

        if (hamiltonian(path, visited, adjDir, startNode.id, startNode.id, V))
        {
            // Close the circuit by appending the start node.
            path.push_back(startNode.id);

            result.feasible = true;
            result.path = std::move(path);

            Logger::getInstance().log("KaufmannMalgrange", "[INFO] Hamiltonian circuit found. Length: " +
                                                               std::to_string(result.path.size() - 1) + " nodes.");
            return result;
        }
    }

    // ------------------------------------------------------------------
    // No circuit found from any starting node.
    // ------------------------------------------------------------------
    result.feasible = false;
    Logger::getInstance().log("KaufmannMalgrange", "[INFO] No Hamiltonian circuit exists in the FC subgraph.");

    return result;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool KaufmannMalgrange::hamiltonian(std::vector<std::string>& path, std::unordered_set<std::string>& visited,
                                    const std::unordered_map<std::string, std::unordered_set<std::string>>& adj,
                                    const std::string& current, const std::string& startId, int targetDepth)
{
    // Base case: all nodes are on the path.
    // Check whether a directed edge from current back to startId exists.
    if (static_cast<int>(path.size()) == targetDepth)
    {
        const auto it = adj.find(current);
        return it != adj.end() && it->second.count(startId) != 0;
    }

    // Recursive case: extend the path along every unvisited neighbour.
    const auto it = adj.find(current);
    if (it == adj.end())
    {
        return false; // current has no outgoing edges.
    }

    for (const std::string& neighbour : it->second)
    {
        if (visited.count(neighbour) != 0)
        {
            continue; // Already on the path — skip to avoid revisiting.
        }

        // Extend.
        path.push_back(neighbour);
        visited.insert(neighbour);

        if (hamiltonian(path, visited, adj, neighbour, startId, targetDepth))
        {
            return true; // Circuit found — propagate success immediately.
        }

        // Backtrack.
        path.pop_back();
        visited.erase(neighbour);
    }

    return false; // No extension from current led to a circuit.
}

// ---------------------------------------------------------------------------

std::unordered_map<std::string, int> KaufmannMalgrange::computeUndirectedDegrees(const std::vector<Node>& nodes,
                                                                                 const std::vector<Edge>& edges) const
{
    std::unordered_map<std::string, int> degrees;

    for (const Node& node : nodes)
    {
        degrees[node.id] = 0;
    }

    // Track which undirected pairs have already been counted to avoid
    // double-counting antiparallel edges (u→v and v→u = one undirected edge).
    std::unordered_set<std::string> counted;

    for (const Edge& edge : edges)
    {
        // Build a canonical key for the undirected pair {u, v}.
        const std::string& u = edge.from_id;
        const std::string& v = edge.to_id;
        const std::string key = u < v ? (u + "|" + v) : (v + "|" + u);

        if (counted.count(key) != 0)
        {
            continue; // Already counted this undirected edge.
        }

        counted.insert(key);
        degrees.at(u)++;
        degrees.at(v)++;
    }

    return degrees;
}

// ---------------------------------------------------------------------------

bool KaufmannMalgrange::oresTheoremHolds(
    const std::vector<Node>& nodes, const std::unordered_map<std::string, int>& degrees,
    const std::unordered_map<std::string, std::unordered_set<std::string>>& adjUnd) const
{
    const int n = static_cast<int>(nodes.size());

    // Ore's theorem applies only to graphs with n >= 3.
    // For n < 3, skip the check (backtracking handles those cases directly).
    if (n < 3)
    {
        return false;
    }

    for (int i = 0; i < n; ++i)
    {
        for (int j = i + 1; j < n; ++j)
        {
            const std::string& u = nodes[i].id;
            const std::string& v = nodes[j].id;

            // Only check NON-ADJACENT pairs.
            const bool adjacent = adjUnd.count(u) != 0 && adjUnd.at(u).count(v) != 0;

            if (adjacent)
            {
                continue;
            }

            // Non-adjacent pair found — check the degree sum.
            const int degU = degrees.count(u) != 0 ? degrees.at(u) : 0;
            const int degV = degrees.count(v) != 0 ? degrees.at(v) : 0;

            if (degU + degV < n)
            {
                return false; // Ore's condition violated for this pair.
            }
        }
    }

    return true; // Condition satisfied for all non-adjacent pairs.
}
