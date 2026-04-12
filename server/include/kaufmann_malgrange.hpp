/**
 * @file kaufmann_malgrange.hpp
 * @brief Kaufmann-Malgrange Hamiltonian circuit for the fulfillment-center subgraph.
 *
 * Determines whether a Hamiltonian circuit exists in the directed subgraph
 * of NodeType::FulfillmentCenter nodes.
 *
 * The algorithm has two phases:
 *
 *   Phase 1 — Ore's theorem pre-check (fast-fail guard):
 *     Ore's theorem states that in a simple undirected graph of n ≥ 3 vertices,
 *     if every pair of NON-ADJACENT vertices u, v satisfies
 *       deg(u) + deg(v) ≥ n,
 *     then a Hamiltonian circuit is guaranteed to exist.
 *
 *   Phase 2 — Backtracking search:
 *     Starting from an arbitrary node, the algorithm extends a path one node
 *     at a time, only following directed edges. At depth V (all nodes visited),
 *     it checks whether a directed edge back to the start node exists. The
 *     search backtracks when it reaches a dead end. The first valid circuit
 *     found is returned immediately.
 *
 * @version 0.1
 * @date 2026-04-03
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef KAUFMANN_MALGRANGE_HPP
#define KAUFMANN_MALGRANGE_HPP

#include "graph.hpp"
#include "log.hpp"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/**
 * @brief Checks Hamiltonian circuit existence for the fulfillment-center
 *        subgraph using backtracking with an Ore's theorem pre-check guard.
 *
 */
class KaufmannMalgrange
{
  public:
    /**
     * @brief Hard node-count limit for the FC subgraph.
     *
     * This prevents accidental O(V!) explosion on large graphs.
     */
    static constexpr int MAX_NODES = 20;

    /**
     * @brief Holds the complete output of a single KM execution.
     */
    struct Result
    {
        /**
         * @brief True if a Hamiltonian circuit was found in the FC subgraph.
         *
         * False when no circuit exists, the graph is empty, or an error occurred.
         */
        bool feasible = false;

        /**
         * @brief The Hamiltonian circuit as an ordered sequence of node ids.
         *
         * When feasible is true, path contains V + 1 entries: the V nodes of
         * the circuit in visitation order, with path.front() == path.back().
         */
        std::vector<std::string> path;

        /**
         * @brief Human-readable error description.
         *
         * Non-empty only when compute() cannot run the algorithm due to a
         * pre-condition violation (e.g. graph too large, no FC nodes).
         */
        std::string error;
    };

    /**
     * @brief Checks whether the FC subgraph has a Hamiltonian circuit.
     *
     * Steps performed:
     *   0. Guard: return error if FC node count > MAX_NODES or == 0.
     *   1. Build directed adjacency set for the FC subgraph.
     *   2. Ore's pre-check on the undirected projection of the subgraph.
     *   3. Backtracking search from every possible starting node until a
     *      circuit is found or all starting nodes are exhausted.
     *
     * @param graph  Const reference to the built graph.
     *
     * @return Result struct with feasible flag, circuit path, and error string.
     */
    Result compute(const Graph& graph);

  private:
    /**
     * @brief Recursive backtracking step.
     *
     * Attempts to extend the current partial path by one directed edge.
     *
     * @param path          Current partial path
     * @param visited       Set of node ids already on the path.
     * @param adj           Directed adjacency sets for the FC subgraph.
     * @param current       The last node added to path (extension point).
     * @param startId       The node the path began from (circuit must close back to this).
     * @param targetDepth   Number of FC nodes
     *
     * @return true if a complete Hamiltonian circuit was found and recorded in path
     */
    bool hamiltonian(std::vector<std::string>& path, std::unordered_set<std::string>& visited,
                     const std::unordered_map<std::string, std::unordered_set<std::string>>& adj,
                     const std::string& current, const std::string& startId, int targetDepth);

    /**
     * @brief Computes the undirected degree of every node in the FC subgraph.
     *
     * @param nodes  Active FC nodes.
     * @param edges  Active FC edges (directed).
     *
     * @return Map from node id to its undirected degree.
     */
    std::unordered_map<std::string, int> computeUndirectedDegrees(const std::vector<Node>& nodes,
                                                                  const std::vector<Edge>& edges) const;

    /**
     * @brief Checks Ore's sufficient condition for the undirected projection.
     *
     * @param nodes    Active FC nodes.
     * @param degrees  Undirected degree map from computeUndirectedDegrees().
     * @param adjUnd   Undirected adjacency sets for the FC subgraph.
     *
     * @return true if Ore's sufficient condition is satisfied.
     */
    bool oresTheoremHolds(const std::vector<Node>& nodes, const std::unordered_map<std::string, int>& degrees,
                          const std::unordered_map<std::string, std::unordered_set<std::string>>& adjUnd) const;
};

#endif // KAUFMANN_MALGRANGE_HPP
