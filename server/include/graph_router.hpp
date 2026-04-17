/**
 * @file graph_router.hpp
 * @brief REST request dispatcher and graph engine coordinator.
 *
 * GraphRouter coordinates REST request dispatch for graph operations.
 *
 * It routes incoming API calls to the corresponding handlers, executes
 * graph algorithms, and persists produced results through MongoRepository.
 *
 * @version 0.1
 * @date 2026-04-04
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef GRAPH_ROUTER_HPP
#define GRAPH_ROUTER_HPP

#include "bellman_ford.hpp"
#include "database.hpp"
#include "ford_fulkerson.hpp"
#include "graph.hpp"
#include "kaufmann_malgrange.hpp"
#include "log.hpp"
#include "mongo_repository.hpp"
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <shared_mutex>
#include <string>

/**
 * @brief Coordinates graph algorithm execution and result persistence.
 *
 * GraphRouter is stateless (as the algorithms) between requests except
 * for the shared Graph pointer and the MongoRepository reference.
 */
class GraphRouter
{
  public:
    /**
     * @brief Construct a GraphRouter with injected dependencies.
     *
     * @param mongo  Reference to the MongoRepository for result persistence.
     */
    explicit GraphRouter(MongoRepository& mongo = MongoRepository::getInstance());

    /**
     * @brief Returns the process-wide singleton GraphRouter instance.
     *
     * Used by InventoryManager and Authentication to trigger live graph refreshes
     * without holding a reference to GraphRouter themselves.The singleton is
     * initialised on first call and reuses the same MongoRepository singleton.
     *
     * @return Reference to the singleton GraphRouter.
     */
    static GraphRouter& getInstance();

    /**
     * @brief Route an HTTP request to the appropriate handler.
     *
     * Parses the method and path, delegates to the correct handler, and
     * returns the JSON response body. Sets statusCode to 200 or 400.
     *
     * Supported routes:
     *   POST /map                → handlePostMap()
     *   POST /markets-path       → handleMarketsPath()
     *   POST /fulfillment-flow   → handleFulfillmentFlow()
     *   POST /fulfillment-circuit→ handleFulfillmentCircuit()
     *   GET  /results/           → handleGetResults()
     *
     * Unknown routes return 404.
     *
     * @param method      HTTP method string ("GET", "POST").
     * @param path        HTTP path string (e.g. "/map").
     * @param body        HTTP request body (JSON string, may be empty).
     * @param statusCode  Output: HTTP status code to send in the response.
     *
     * @return JSON response body string.
     */
    virtual std::string route(const std::string& method, const std::string& path, const std::string& body,
                              int& statusCode);

    /**
     * @brief Rebuild the live graph from PostgreSQL state.
     *
     * Calls db.buildGraphSnapshot() to produce a fresh node array from the
     * current online users. Atomically swaps the shared Graph pointer under the
     * existing write lock — the same path used by POST /map.
     *
     * Called automatically from Authentication and InventoryManager whenever
     * a client connects, disconnects, or its inventory changes. Also callable
     * via POST /map/refresh for manual operator-triggered refreshes.
     *
     * @param db  Database reference (injected for testability via MockDatabase).
     */
    void refreshFromDatabase(Database& db);

    /**
     * @brief Handle GET /results/ — retrieve stored results from MongoDB.
     *
     * @param algorithmFilter  Filter by algorithm name, or "" for all.
     * @param limit            Maximum number of results to return.
     * @param statusCode       Always set to 200.
     * @return JSON response body.
     */
    std::string handleGetResults(const std::string& algorithmFilter, int limit, int& statusCode);

  private:
    // ------------------------------------------------------------------
    // Endpoint handlers — each returns the JSON response body and sets
    // statusCode. They are called only from route().
    // ------------------------------------------------------------------

    /**
     * @brief Handle POST /map — parse and load a new graph.
     *
     * Atomically replaces the current graph.
     *
     * @param body        JSON array of node objects.
     * @param statusCode  Set to 200 on success, 400 on validation failure.
     * @return JSON response body.
     */
    std::string handlePostMap(const std::string& body, int& statusCode);

    /**
     * @brief Handle POST /markets-path — run Bellman-Ford.
     *
     * Expects body: { "source_id": "H001" }
     *
     * @param body        JSON request body.
     * @param statusCode  200 on success, 400 on error.
     * @return JSON response body.
     */
    std::string handleMarketsPath(const std::string& body, int& statusCode);

    /**
     * @brief Handle POST /fulfillment-flow — run Ford-Fulkerson.
     *
     * Expects body: { "source_id": "W001", "sink_id": "W005" }
     *
     * @param body        JSON request body.
     * @param statusCode  200 on success, 400 on error.
     * @return JSON response body.
     */
    std::string handleFulfillmentFlow(const std::string& body, int& statusCode);

    /**
     * @brief Handle POST /fulfillment-circuit — run Kaufmann-Malgrange.
     *
     * Expects body: {} (no parameters)
     *
     * @param body        JSON request body (ignored).
     * @param statusCode  200 on success, 400 on error.
     * @return JSON response body.
     */
    std::string handleFulfillmentCircuit(const std::string& body, int& statusCode);

    /**
     * @brief Handle POST /map/refresh — rebuild the graph from PostgreSQL.
     *
     * Triggers refreshFromDatabase() and returns the new node/edge counts.
     *
     * @param db          Database reference used to query live state.
     * @param statusCode  200 on success, 400 if no nodes are returned.
     * @return JSON response body.
     */
    std::string handleMapRefresh(Database& db, int& statusCode);

    // ------------------------------------------------------------------
    // Utilities
    // ------------------------------------------------------------------

    /**
     * @brief Atomically retrieve a shared snapshot of the current graph.
     *
     * Acquires a shared (read) lock, it copies graph_ (which is a shared_ptr),
     * incrementing a reference count of the Graph object, then releases the lock.
     * Changes to graph_ after this call do not affect the returned snapshot, and
     * the caller can safely use the snapshot without holding any locks.
     *
     * @return Shared pointer to the current immutable Graph, or nullptr if
     *         no graph has been loaded yet.
     */
    std::shared_ptr<const Graph> snapshotGraph() const;

    /**
     * @brief Generates a unique result ID for the given algorithm prefix.
     *
     * Format: "<prefix>_<unix_epoch_seconds>"
     * Examples: "bf_1712345678", "ff_1712345679", "km_1712345680"
     *
     * @param prefix  Two-character prefix: "bf", "ff", or "km".
     * @return Result ID string.
     */
    static std::string generateResultId(const std::string& prefix);

    /**
     * @brief Builds a standard JSON error response body.
     *
     * @param message  Human-readable error description.
     * @return JSON string: { "status": "error", "message": "..." }
     */
    static std::string errorResponse(const std::string& message);

    // ------------------------------------------------------------------
    // State
    // ------------------------------------------------------------------

    /// Current graph, protected by graphMutex_.
    std::shared_ptr<const Graph> graph_;

    /// Protects graph_. Shared (read) lock for algorithm calls;
    mutable std::shared_mutex graphMutex_;

    /// Stateless algorithm objects — reused across all requests.
    BellmanFord bellmanFord_;
    FordFulkerson fordFulkerson_;
    KaufmannMalgrange kaufmannMalgrange_;

    /// MongoDB persistence layer.
    MongoRepository& mongo_;

    /// Serialises access to MongoRepository when using a shared mongocxx client.
    mutable std::mutex mongoMutex_;
    // Handler para GET /profiling-bf
    std::string handleProfilingBf(const std::string& query, int& statusCode);
};

// Forward declaration del helper externo (profiling_endpoint.cpp)
std::string handleProfilingBf(const Graph& graph, const std::string& sourceId, int threads, int& statusCode);

#endif // GRAPH_ROUTER_HPP
