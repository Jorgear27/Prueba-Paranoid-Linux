/**
 * @file bellman_ford.hpp
 * @brief Bellman-Ford shortest-path algorithm for the market (hub) subgraph.
 *
 * Operates opnly on NodeType::Market nodes. Given a source market node, calculates the
 * minimum-cost path from that source to every other market node in the graph.
 *
 * @version 0.1
 * @date 2026-04-03
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef BELLMAN_FORD_HPP
#define BELLMAN_FORD_HPP

#include "graph.hpp"
#include "log.hpp"
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

/**
 * @brief Represents an unreachable node.
 *
 */
constexpr double UNREACHABLE = std::numeric_limits<double>::infinity();

/**
 * @brief Calculates shortest paths over the market subgraph using the Bellman-Ford algorithm.
 *
 */
class BellmanFord
{
  public:
    /**
     * @brief Represents the output of a single Bellman-Ford execution.
     *
     */
    struct Result
    {
        /**
         * @brief True if a negative-weight cycle reachable from the source
         *        was detected during the (V)-th relaxation pass.
         *
         * A negative cycle makes shortest-path distances undefined because
         * costs can always be decreased by going around the cycle again.
         */
        bool has_negative_cycle = false;

        /**
         * @brief Maps each reachable market node id to its minimum cost from the source.
         *
         */
        std::unordered_map<std::string, double> distances;

        /**
         * @brief Maps each reachable market node id to the id of its
         *        predecessor on the shortest path from the source.
         *
         */
        std::unordered_map<std::string, std::string> predecessors;
    };

    /**
     * @brief Runs Bellman-Ford on the market subgraph of the given graph.
     *
     * Steps performed:
     *   1. Extract market nodes and market edges from the graph.
     *   2. Initialise source distance to 0.0; all others to UNREACHABLE.
     *   3. Relax all edges (V − 1) times, where V (vertices) is the number of nodes.
     *   4. Run one additional relaxation pass to detect negative cycles.
     *
     * @param graph     Const reference to the built graph.
     * @param sourceId  Node id of the starting market node.
     *
     * @return Result struct with distances, predecessors, and cycle flag.
     *
     * @throws std::invalid_argument if sourceId is not present in the graph.
     * @throws std::invalid_argument if sourceId is not a NodeType::Market node.
     * @throws std::invalid_argument if the graph contains no market nodes.
     */
    Result compute(const Graph& graph, const std::string& sourceId);
};

#endif // BELLMAN_FORD_HPP
