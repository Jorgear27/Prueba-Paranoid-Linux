/**
 * @file mongo_repository_stub.cpp
 * @brief Test-only stub implementation of MongoRepository.
 *
 * This file provides a linkable MongoRepository body that does NOT depend on
 * the mongocxx driver. It is compiled exclusively by test targets in
 * tests/CMakeLists.txt in place of the real src/mongo_repository.cpp.
 *
 * All virtual methods have no-op bodies here; tests that need specific
 * behaviour use MockMongoRepository (which overrides every virtual method).
 */

#include "mongo_repository.hpp"

MongoRepository::MongoRepository(const std::string& uri) : connected_(false), uri_(uri)
{
    // Stub: no real connection attempt. Tests override all virtual methods.
}

MongoRepository& MongoRepository::getInstance()
{
    static MongoRepository instance(MONGO_URI_DEFAULT);
    return instance;
}

bool MongoRepository::is_connected() const
{
    return connected_;
}

void MongoRepository::saveResult(const std::string& /*resultId*/, const std::string& /*algorithm*/,
                                 const nlohmann::json& /*data*/)
{
    // Stub: no-op. Overridden by MockMongoRepository in tests.
}

std::vector<nlohmann::json> MongoRepository::getResults(const std::string& /*algorithmFilter*/, int /*limit*/)
{
    // Stub: returns empty. Overridden by MockMongoRepository in tests.
    return {};
}

std::string MongoRepository::currentTimestamp() const
{
    return "1970-01-01T00:00:00Z";
}

int MongoRepository::clampLimit(int limit)
{
    if (limit < 1)
        return 1;
    if (limit > 100)
        return 100;
    return limit;
}
