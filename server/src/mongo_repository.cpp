#include "mongo_repository.hpp"

#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/exception/exception.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/options/find.hpp>
#include <mongocxx/uri.hpp>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Process-wide mongocxx instance.
//
// Mongo C++ driver requires a single global runtime object so
// mongocxx::instance must be constructed once per process before any
// mongocxx::client is created, and must outlive all clients.
// ---------------------------------------------------------------------------
static mongocxx::instance& getMongocxxInstance()
{
    static mongocxx::instance instance{};
    return instance;
}

// ---------------------------------------------------------------------------
// We use a single static unique_ptr<mongocxx::client> defined in this file.
// There is exactly one client object (s_client) meant to be owned in one place.
// When program/static teardown happens, the managed client is destroyed automatically.
// ---------------------------------------------------------------------------
static std::unique_ptr<mongocxx::client> s_client;

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

MongoRepository::MongoRepository(const std::string& uri) : uri_(uri)
{
    // Ensure the mongocxx driver instance exists before creating a client.
    getMongocxxInstance();

    try
    {
        s_client = std::make_unique<mongocxx::client>(mongocxx::uri{uri_});

        // Verify connectivity with a ping command.
        auto admin = (*s_client)["admin"];
        auto cmd = bsoncxx::builder::stream::document{} << "ping" << 1 << bsoncxx::builder::stream::finalize;
        admin.run_command(cmd.view());

        connected_ = true;
        Logger::getInstance().log("MongoRepository", "[INFO] Connected to MongoDB at " + uri_ + ".");
        std::cout << "[INFO] Connected to MongoDB at " << uri_ << ".\n";
    }
    catch (const mongocxx::exception& e)
    {
        connected_ = false;
        Logger::getInstance().log("MongoRepository",
                                  "[ERROR] Failed to connect to MongoDB at " + uri_ + ": " + e.what());
        std::cerr << "[ERROR] Failed to connect to MongoDB at " << uri_ << ": " << e.what() << "\n";
    }
}

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------

MongoRepository& MongoRepository::getInstance()
{
    // Read URI from environment variable.
    const char* envUri = std::getenv("MONGO_URI");
    const std::string uri =
        (envUri != nullptr && envUri[0] != '\0') ? std::string(envUri) : std::string(MONGO_URI_DEFAULT);

    static MongoRepository instance(uri);
    return instance;
}

// ---------------------------------------------------------------------------
// is_connected
// ---------------------------------------------------------------------------

bool MongoRepository::is_connected() const
{
    return connected_ && s_client != nullptr;
}

// ---------------------------------------------------------------------------
// saveResult
// ---------------------------------------------------------------------------

void MongoRepository::saveResult(const std::string& resultId, const std::string& algorithm, const nlohmann::json& data)
{
    if (!is_connected())
    {
        Logger::getInstance().log("MongoRepository",
                                  "[WARN] saveResult called but not connected to MongoDB. Result not persisted: " +
                                      resultId);
        return;
    }

    try
    {
        auto collection = (*s_client)[MONGO_DB_NAME][MONGO_COLLECTION_NAME];

        // Convert the nlohmann::json data payload to a BSON document via
        // its JSON string representation
        const bsoncxx::document::value dataDoc = bsoncxx::from_json(data.dump());

        auto docBuilder = bsoncxx::builder::stream::document{};
        docBuilder << "_id" << resultId << "algorithm" << algorithm << "timestamp" << currentTimestamp() << "data"
                   << dataDoc;

        collection.insert_one(docBuilder << bsoncxx::builder::stream::finalize);

        Logger::getInstance().log("MongoRepository", "[INFO] Result saved to MongoDB. result_id: " + resultId +
                                                         ", algorithm: " + algorithm + ".");
    }
    catch (const mongocxx::exception& e)
    {
        // A duplicate _id throws a write exception — log and continue.
        Logger::getInstance().log("MongoRepository", "[ERROR] Failed to save result '" + resultId + "': " + e.what());
        std::cerr << "[ERROR] MongoRepository::saveResult: " << e.what() << "\n";
    }
}

// ---------------------------------------------------------------------------
// getResults
// ---------------------------------------------------------------------------

std::vector<nlohmann::json> MongoRepository::getResults(const std::string& algorithmFilter, int limit)
{
    std::vector<nlohmann::json> results;

    if (!is_connected())
    {
        Logger::getInstance().log("MongoRepository",
                                  "[WARN] getResults called but not connected to MongoDB. Returning empty list.");
        return results;
    }

    try
    {
        auto collection = (*s_client)[MONGO_DB_NAME][MONGO_COLLECTION_NAME];

        // Build filter document.
        bsoncxx::document::value filterDoc = [&]() {
            auto builder = bsoncxx::builder::stream::document{};
            if (!algorithmFilter.empty())
            {
                builder << "algorithm" << algorithmFilter;
            }
            return builder << bsoncxx::builder::stream::finalize;
        }();

        // Apply limit option.
        mongocxx::options::find opts;
        opts.limit(static_cast<int64_t>(clampLimit(limit)));

        auto cursor = collection.find(filterDoc.view(), opts);

        for (auto&& doc : cursor)
        {
            // Convert each BSON document back to nlohmann::json via JSON string.
            const std::string jsonStr = bsoncxx::to_json(doc);
            nlohmann::json entry = nlohmann::json::parse(jsonStr);

            // Remap _id → result_id for the API response shape.
            if (entry.contains("_id"))
            {
                entry["result_id"] = entry["_id"];
                entry.erase("_id");
            }

            results.push_back(std::move(entry));
        }

        Logger::getInstance().log("MongoRepository", "[INFO] getResults returned " + std::to_string(results.size()) +
                                                         " document(s). Filter: '" + algorithmFilter + "'.");
    }
    catch (const mongocxx::exception& e)
    {
        Logger::getInstance().log("MongoRepository", "[ERROR] getResults failed: " + std::string(e.what()));
        std::cerr << "[ERROR] MongoRepository::getResults: " << e.what() << "\n";
    }

    return results;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

std::string MongoRepository::currentTimestamp() const
{
    const std::time_t now = std::time(nullptr);
    std::tm utc{};

#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif

    std::ostringstream oss;
    oss << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

int MongoRepository::clampLimit(int limit)
{
    if (limit < 1)
        return 1;
    if (limit > 100)
        return 100;
    return limit;
}
