#include "graph_router.hpp"

#include "database.hpp"
#include <chrono>
#include <stdexcept>

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

GraphRouter::GraphRouter(MongoRepository& mongo) : graph_(nullptr), mongo_(mongo)
{
}

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

GraphRouter& GraphRouter::getInstance()
{
    static GraphRouter instance(MongoRepository::getInstance());
    return instance;
}

// ---------------------------------------------------------------------------
// Public: route()
// ---------------------------------------------------------------------------

std::string GraphRouter::route(const std::string& method, const std::string& path, const std::string& body,
                               int& statusCode)
{
    // POST /map/refresh — rebuild graph from live PostgreSQL state
    if (method == "POST" && path == "/map/refresh")
    {
        return handleMapRefresh(Database::getInstance(), statusCode);
    }

    // POST /map
    if (method == "POST" && path == "/map")
    {
        return handlePostMap(body, statusCode);
    }

    // POST /markets-path
    if (method == "POST" && path == "/markets-path")
    {
        return handleMarketsPath(body, statusCode);
    }

    // POST /fulfillment-flow
    if (method == "POST" && path == "/fulfillment-flow")
    {
        return handleFulfillmentFlow(body, statusCode);
    }

    // POST /fulfillment-circuit
    if (method == "POST" && path == "/fulfillment-circuit")
    {
        return handleFulfillmentCircuit(body, statusCode);
    }

    // GET /results/ (with optional query params already stripped by HttpServer)
    if (method == "GET" && path == "/results/")
    {
        return handleGetResults("", 20, statusCode);
    }

    // Unknown route.
    statusCode = 404;
    return errorResponse("Unknown route: " + method + " " + path);
}

// ---------------------------------------------------------------------------
// Additional route overload for GET /results/ with parsed query params.
// Called directly by HttpServer after extracting ?algorithm=...&limit=...
// ---------------------------------------------------------------------------

std::string GraphRouter::handleGetResults(const std::string& algorithmFilter, int limit, int& statusCode)
{
    std::vector<json> results;
    {
        std::lock_guard<std::mutex> lock(mongoMutex_);
        results = mongo_.getResults(algorithmFilter, limit);
    }

    json response;
    response["results"] = json::array();
    for (const json& doc : results)
    {
        response["results"].push_back(doc);
    }

    statusCode = 200;
    return response.dump();
}

// ---------------------------------------------------------------------------
// Private: handlePostMap
// ---------------------------------------------------------------------------

std::string GraphRouter::handlePostMap(const std::string& body, int& statusCode)
{
    try
    {
        const json payload = json::parse(body);

        // Build the new graph on the stack
        auto newGraph = std::make_shared<Graph>();
        newGraph->build(payload);

        // Atomically swap under exclusive lock.
        {
            std::unique_lock<std::shared_mutex> lock(graphMutex_);
            graph_ = newGraph;
        }

        Logger::getInstance().log("GraphRouter", "[INFO] Map loaded: " + std::to_string(newGraph->nodeCount()) +
                                                     " nodes, " + std::to_string(newGraph->edgeCount()) + " edges.");

        // Persist the map to MongoDB
        const std::string mapId = generateResultId("map");
        json mapData;
        mapData["nodes"] = payload;
        {
            std::lock_guard<std::mutex> lock(mongoMutex_);
            mongo_.saveResult(mapId, "map", mapData);
        }

        statusCode = 200;
        return json{{"status", "ok"},
                    {"nodes_loaded", static_cast<int>(newGraph->nodeCount())},
                    {"edges_loaded", static_cast<int>(newGraph->edgeCount())},
                    {"map_id", mapId}}
            .dump();
    }
    catch (const json::parse_error& e)
    {
        statusCode = 400;
        return errorResponse("Invalid JSON: " + std::string(e.what()));
    }
    catch (const std::invalid_argument& e)
    {
        statusCode = 400;
        return errorResponse(e.what());
    }
    catch (const std::exception& e)
    {
        statusCode = 400;
        return errorResponse(e.what());
    }
}

// ---------------------------------------------------------------------------
// Private: handleMarketsPath
// ---------------------------------------------------------------------------

std::string GraphRouter::handleMarketsPath(const std::string& body, int& statusCode)
{
    // Snapshot the graph under shared lock.
    const std::shared_ptr<const Graph> g = snapshotGraph();

    if (!g)
    {
        statusCode = 400;
        return errorResponse("No map loaded. POST /map first.");
    }

    try
    {
        const json req = json::parse(body);
        const std::string sourceId = req.at("source_id").get<std::string>();

        // Run algorithm on the immutable snapshot — no lock held.
        const BellmanFord::Result result = bellmanFord_.compute(*g, sourceId);

        if (result.has_negative_cycle)
        {
            statusCode = 400;
            return errorResponse("Negative cycle detected in the market subgraph.");
        }

        // Build response
        json distances = json::object();
        json predecessors = json::object();
        for (const auto& [nodeId, dist] : result.distances)
        {
            if (dist < UNREACHABLE)
            {
                distances[nodeId] = dist;
            }
        }
        for (const auto& [nodeId, predId] : result.predecessors)
        {
            predecessors[nodeId] = predId;
        }

        const std::string resultId = generateResultId("bf");

        json response;
        response["algorithm"] = "bellman_ford";
        response["source_id"] = sourceId;
        response["distances"] = distances;
        response["predecessors"] = predecessors;
        response["result_id"] = resultId;

        // Persist — include source_id in the data payload.
        json data;
        data["source_id"] = sourceId;
        data["distances"] = distances;
        data["predecessors"] = predecessors;
        {
            std::lock_guard<std::mutex> lock(mongoMutex_);
            mongo_.saveResult(resultId, "bellman_ford", data);
        }

        statusCode = 200;
        return response.dump();
    }
    catch (const std::invalid_argument& e)
    {
        statusCode = 400;
        return errorResponse(e.what());
    }
    catch (const json::exception& e)
    {
        statusCode = 400;
        return errorResponse("Invalid request body: " + std::string(e.what()));
    }
    catch (const std::exception& e)
    {
        statusCode = 400;
        return errorResponse(e.what());
    }
}

// ---------------------------------------------------------------------------
// Private: handleFulfillmentFlow
// ---------------------------------------------------------------------------

std::string GraphRouter::handleFulfillmentFlow(const std::string& body, int& statusCode)
{
    const std::shared_ptr<const Graph> g = snapshotGraph();

    if (!g)
    {
        statusCode = 400;
        return errorResponse("No map loaded. POST /map first.");
    }

    try
    {
        const json req = json::parse(body);
        const std::string sourceId = req.at("source_id").get<std::string>();
        const std::string sinkId = req.at("sink_id").get<std::string>();

        const FordFulkerson::Result result = fordFulkerson_.compute(*g, sourceId, sinkId);

        const std::string resultId = generateResultId("ff");

        json response;
        response["algorithm"] = "ford_fulkerson";
        response["source_id"] = sourceId;
        response["sink_id"] = sinkId;
        response["max_flow"] = result.max_flow;
        response["result_id"] = resultId;

        json data;
        data["source_id"] = sourceId;
        data["sink_id"] = sinkId;
        data["max_flow"] = result.max_flow;
        {
            std::lock_guard<std::mutex> lock(mongoMutex_);
            mongo_.saveResult(resultId, "ford_fulkerson", data);
        }

        statusCode = 200;
        return response.dump();
    }
    catch (const std::invalid_argument& e)
    {
        statusCode = 400;
        return errorResponse(e.what());
    }
    catch (const json::exception& e)
    {
        statusCode = 400;
        return errorResponse("Invalid request body: " + std::string(e.what()));
    }
    catch (const std::exception& e)
    {
        statusCode = 400;
        return errorResponse(e.what());
    }
}

// ---------------------------------------------------------------------------
// Private: handleFulfillmentCircuit
// ---------------------------------------------------------------------------

std::string GraphRouter::handleFulfillmentCircuit(const std::string& body, int& statusCode)
{
    const std::shared_ptr<const Graph> g = snapshotGraph();

    if (!g)
    {
        statusCode = 400;
        return errorResponse("No map loaded. POST /map first.");
    }

    try
    {
        if (!body.empty())
        {
            const json parsed = json::parse(body); // Throws on malformed JSON.
            (void)parsed;                          // Result not needed.
        }

        const KaufmannMalgrange::Result result = kaufmannMalgrange_.compute(*g);

        if (!result.error.empty())
        {
            statusCode = 400;
            return errorResponse(result.error);
        }

        const std::string resultId = generateResultId("km");

        json response;
        response["algorithm"] = "kaufmann_malgrange";
        response["feasible"] = result.feasible;
        response["path"] = result.path;
        response["result_id"] = resultId;

        json data;
        data["feasible"] = result.feasible;
        data["path"] = result.path;
        {
            std::lock_guard<std::mutex> lock(mongoMutex_);
            mongo_.saveResult(resultId, "kaufmann_malgrange", data);
        }

        statusCode = 200;
        return response.dump();
    }
    catch (const json::exception& e)
    {
        statusCode = 400;
        return errorResponse("Invalid request body: " + std::string(e.what()));
    }
    catch (const std::exception& e)
    {
        statusCode = 400;
        return errorResponse(e.what());
    }
}

// ---------------------------------------------------------------------------
// Public: refreshFromDatabase()
// ---------------------------------------------------------------------------

void GraphRouter::refreshFromDatabase(Database& db)
{
    // Build the snapshot outside the lock — the query may take tens of ms
    // and we must not block concurrent algorithm reads for that duration.
    const nlohmann::json snapshot = db.buildGraphSnapshot();

    auto newGraph = std::make_shared<Graph>();

    if (!snapshot.empty())
    {
        try
        {
            newGraph->build(snapshot);
        }
        catch (const std::exception& e)
        {
            Logger::getInstance().log("GraphRouter",
                                      "[ERROR] refreshFromDatabase — Graph::build() failed: " + std::string(e.what()));
            return; // Keep the previous graph intact.
        }
    }

    // Atomically swap — identical code path to handlePostMap().
    {
        std::unique_lock<std::shared_mutex> lock(graphMutex_);
        graph_ = newGraph;
    }

    Logger::getInstance().log("GraphRouter",
                              "[INFO] Graph refreshed from PostgreSQL: " + std::to_string(newGraph->nodeCount()) +
                                  " nodes, " + std::to_string(newGraph->edgeCount()) + " edges.");
}

// ---------------------------------------------------------------------------
// Private: handleMapRefresh
// ---------------------------------------------------------------------------

std::string GraphRouter::handleMapRefresh(Database& db, int& statusCode)
{
    const nlohmann::json snapshot = db.buildGraphSnapshot();

    if (snapshot.empty())
    {
        statusCode = 400;
        return errorResponse("No online nodes found in PostgreSQL. "
                             "Connect at least one warehouse or hub first.");
    }

    auto newGraph = std::make_shared<Graph>();
    try
    {
        newGraph->build(snapshot);
    }
    catch (const std::exception& e)
    {
        statusCode = 400;
        return errorResponse("Graph build failed: " + std::string(e.what()));
    }

    {
        std::unique_lock<std::shared_mutex> lock(graphMutex_);
        graph_ = newGraph;
    }

    Logger::getInstance().log("GraphRouter", "[INFO] POST /map/refresh: " + std::to_string(newGraph->nodeCount()) +
                                                 " nodes, " + std::to_string(newGraph->edgeCount()) + " edges.");

    statusCode = 200;
    return nlohmann::json{{"status", "ok"},
                          {"nodes_loaded", static_cast<int>(newGraph->nodeCount())},
                          {"edges_loaded", static_cast<int>(newGraph->edgeCount())},
                          {"source", "postgresql"}}
        .dump();
}

// ---------------------------------------------------------------------------
// Private: utilities
// ---------------------------------------------------------------------------

std::shared_ptr<const Graph> GraphRouter::snapshotGraph() const
{
    std::shared_lock<std::shared_mutex> lock(graphMutex_);
    return graph_; // Copies the shared_ptr then releases lock.
}

std::string GraphRouter::generateResultId(const std::string& prefix)
{
    const auto epoch =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return prefix + "_" + std::to_string(epoch);
}

std::string GraphRouter::errorResponse(const std::string& message)
{
    return json{{"status", "error"}, {"message", message}}.dump();
}
