/**
 * @file mongo_repository.hpp
 * @brief MongoDB persistence layer for routing engine computation results.
 *
 * All algorithm results are persisted to the `routing_results` collection in
 * the `paranoid_routing` MongoDB db immediately after each computation completes.
 *
 * @version 0.1
 * @date 2026-04-04
 *
 * @copyright Copyright (c) 2026
 *
 */

#ifndef MONGO_REPOSITORY_HPP
#define MONGO_REPOSITORY_HPP

#include "log.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

/**
 * @brief Default MongoDB connection URI.
 *
 * Overridable at runtime via the MONGO_URI environment variable.
 */
#define MONGO_URI_DEFAULT "mongodb://localhost:27017"

/**
 * @brief MongoDB database name used by the routing engine.
 */
#define MONGO_DB_NAME "paranoid_routing"

/**
 * @brief MongoDB collection name for computation results.
 */
#define MONGO_COLLECTION_NAME "routing_results"

/**
 * @brief Manages persistence of routing computation results to MongoDB.
 *
 * Wraps the mongocxx driver to provide a clean interface for saving and
 * retrieving algorithm results. All public methods are virtual to enable
 * mocking in unit tests without a live MongoDB connection.
 */
class MongoRepository
{
  public:
    /**
     * @brief Construct a new MongoRepository and connect to MongoDB.
     *
     * Reads the connection URI from the MONGO_URI environment variable.
     * Falls back to MONGO_URI_DEFAULT if the variable is not set.
     *
     * @param uri  MongoDB connection URI. Defaults to MONGO_URI_DEFAULT.
     */
    explicit MongoRepository(const std::string& uri = MONGO_URI_DEFAULT);

    /**
     * @brief Destructor. Closes the MongoDB connection if open.
     */
    virtual ~MongoRepository() = default;

    // Singletons are non-copyable and non-movable.
    MongoRepository(const MongoRepository&) = delete;
    MongoRepository& operator=(const MongoRepository&) = delete;

    /**
     * @brief Returns the singleton instance of MongoRepository.
     *
     * @return Reference to the singleton MongoRepository instance.
     */
    static MongoRepository& getInstance();

    /**
     * @brief Returns true if the MongoDB connection is active.
     *
     */
    virtual bool is_connected() const;

    /**
     * @brief Persist a computation result to the routing_results collection.
     *
     * Inserts a document with the following fields:
     *   - _id:        result_id (must be unique; duplicate inserts are logged
     *                 and silently dropped)
     *   - algorithm:  algorithm name string
     *   - timestamp:  ISO 8601 UTC timestamp of the call
     *   - data:       the full algorithm result as a BSON document
     *
     * @param resultId   Unique result identifier (e.g. "bf_1712345678").
     * @param algorithm  Algorithm name ("bellman_ford", "ford_fulkerson","kaufmann_malgrange").
     * @param data       JSON object containing the algorithm-specific result fields.
     *
     * @note This method is a no-op if is_connected() returns false.
     */
    virtual void saveResult(const std::string& resultId, const std::string& algorithm, const nlohmann::json& data);

    /**
     * @brief Retrieve stored computation results from MongoDB.
     *
     * Queries the routing_results collection, optionally filtering by
     * algorithm name. Results are returned in insertion order (most recent
     * last, unless a TTL index or sort is applied server-side).
     *
     * @param algorithmFilter  Documents whose `algorithm` field matches this are returned.
     * @param limit            Maximum number of documents to return. Defaults to 20.
     *
     * @return Vector of JSON objects, each shaped as:
     * @code{.json}
     * {
     *   "result_id": "bf_1712345678",
     *   "algorithm": "bellman_ford",
     *   "timestamp": "2025-07-15T10:00:00Z",
     *   "data":      { ... }
     * }
     * @endcode
     */
    virtual std::vector<nlohmann::json> getResults(const std::string& algorithmFilter = "", int limit = 20);

  private:
    /**
     * @brief Generates an ISO 8601 UTC timestamp for the current moment.
     *
     * Format: "YYYY-MM-DDTHH:MM:SSZ"
     *
     * @return Timestamp string.
     */
    std::string currentTimestamp() const;

    /**
     * @brief Clamps limit to the valid range [1, 100].
     *
     * @param limit  Raw limit value from the caller.
     * @return       Clamped value.
     */
    static int clampLimit(int limit);

    /**
     * @brief Connection status flag set during construction.
     *
     */
    bool connected_ = false;

    /**
     * @brief The resolved MongoDB URI used to open the connection.
     *
     * Stored for logging and diagnostic purposes.
     */
    std::string uri_;

    // ------------------------------------------------------------------
    // mongocxx objects are declared and defined only in the .cpp file which
    // includes the mongocxx headers. This keeps the header free of mongocxx
    // includes so that files that depend only on the interface (tests) can
    // include this header without requiring the mongocxx library.
    // ------------------------------------------------------------------
};

#endif // MONGO_REPOSITORY_HPP
