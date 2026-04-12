/**
 * @file ford_fulkerson.hpp
 * @brief Ford-Fulkerson maximum-flow algorithm (Edmonds-Karp variant) for the
 *        fulfillment-center subgraph.
 *
 * Operates only on NodeType::FulfillmentCenter nodes. It calculetes the maximum integer
 * flow that can be pushed from the source node to the sink node through the network.
 *
 * @version 0.1
 * @date 2026-04-03
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef FORD_FULKERSON_HPP
#define FORD_FULKERSON_HPP

#include "graph.hpp"
#include "log.hpp"
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/**
 * @brief Computes maximum flow over the fulfillment-center subgraph using the
 *        Edmonds-Karp (BFS-based Ford-Fulkerson) algorithm.
 *
 */
class FordFulkerson
{
  public:
    /**
     * @brief Holds the complete output of a single Ford-Fulkerson execution.
     */
    struct Result
    {
        /**
         * @brief Total maximum flow pushed from source to sink.
         *
         * Equals the sum of flow on all edges leaving the source node, which by flow
         * conservation should be equal to the sum on all edges entering the sink node.
         */
        int max_flow = 0;

        /**
         * @brief Net flow on each original directed edge.
         *
         * flow_matrix[u][v] = net flow sent from u to v on the edge u→v.
         * A value of 0 means the edge exists but carries no flow.
         */
        std::unordered_map<std::string, std::unordered_map<std::string, int>> flow_matrix;
    };

    /**
     * @brief Runs Edmonds-Karp on the fulfillment-center subgraph.
     *
     * Steps performed:
     *   1. Extract FC edges from the graph and build the initial residual graph.
     *   2. Repeatedly find a shortest augmenting path (BFS) from source to sink.
     *   3. Determine the bottleneck capacity along the path.
     *   4. Update forward and backward residual capacities.
     *   5. Accumulate max_flow until no augmenting path exists.
     *   6. Derive the flow_matrix from the difference between original and
     *      residual capacities on forward edges.
     *
     * @param graph     Const reference to the built graph.
     * @param sourceId  Node id of the source fulfillment center.
     * @param sinkId    Node id of the sink fulfillment center.
     *
     * @return Result struct with max_flow and flow_matrix.
     *
     * @throws std::invalid_argument if sourceId is not present in the graph.
     * @throws std::invalid_argument if sinkId is not present in the graph.
     * @throws std::invalid_argument if sourceId is not a FulfillmentCenter node.
     * @throws std::invalid_argument if sinkId is not a FulfillmentCenter node.
     * @throws std::invalid_argument if sourceId == sinkId.
     * @throws std::invalid_argument if the graph contains no FC nodes.
     */
    Result compute(const Graph& graph, const std::string& sourceId, const std::string& sinkId);

  private:
    /**
     * @brief BFS augmenting-path search in the residual graph.
     *
     * Finds the shortest (fewest nodes) path from source to sink that has
     * positive residual capacity on every edge.
     *
     * @param residual  Current residual graph (capacity remaining per edge).
     * @param sourceId  BFS start node.
     * @param sinkId    BFS target node.
     * @param parent    Output map: parent[v] = u means u→v is on the path.
     *                  Cleared and repopulated on every call.
     *
     * @return true if an augmenting path from source to sink was found.
     */
    bool bfs(const std::unordered_map<std::string, std::unordered_map<std::string, int>>& residual,
             const std::string& sourceId, const std::string& sinkId,
             std::unordered_map<std::string, std::string>& parent);
};

#endif // FORD_FULKERSON_HPP
