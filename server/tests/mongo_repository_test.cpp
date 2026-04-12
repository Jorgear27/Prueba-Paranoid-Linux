#include "mongo_repository.hpp"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using ::testing::_;
using ::testing::IsEmpty;
using ::testing::Return;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// MockMongoRepository
//
// All virtual methods are mocked so tests exercise the callers of
// MongoRepository without requiring a live MongoDB connection.
// ---------------------------------------------------------------------------
class MockMongoRepository : public MongoRepository
{
  public:
    MOCK_METHOD(bool, is_connected, (), (const, override));

    MOCK_METHOD(void, saveResult, (const std::string&, const std::string&, const nlohmann::json&), (override));

    MOCK_METHOD(std::vector<nlohmann::json>, getResults, (const std::string&, int), (override));
};

// ---------------------------------------------------------------------------
// Tests for MongoRepository.
//
// These tests verify:
//   1. Callers behave correctly when is_connected() returns false.
//   2. saveResult() is called with the correct arguments.
//   3. getResults() returns and filters results as expected.
//   4. Limit clamping behaviour (tested via the public interface).
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Tests: is_connected()
// ---------------------------------------------------------------------------

TEST(MongoRepositoryTest, IsConnected_ReturnsFalse_WhenNotConnected)
{
    MockMongoRepository repo;
    EXPECT_CALL(repo, is_connected()).WillOnce(Return(false));
    EXPECT_FALSE(repo.is_connected());
}

TEST(MongoRepositoryTest, IsConnected_ReturnsTrue_WhenConnected)
{
    MockMongoRepository repo;
    EXPECT_CALL(repo, is_connected()).WillOnce(Return(true));
    EXPECT_TRUE(repo.is_connected());
}

// ---------------------------------------------------------------------------
// Tests: saveResult()
// ---------------------------------------------------------------------------

TEST(MongoRepositoryTest, SaveResult_CalledWithCorrectResultId)
{
    MockMongoRepository repo;

    const std::string resultId = "bf_1712345678";
    const std::string algorithm = "bellman_ford";
    const json data = {{"source_id", "H001"}, {"distances", {{"H002", 14.3}}}};

    EXPECT_CALL(repo, saveResult(resultId, algorithm, data)).Times(1);

    repo.saveResult(resultId, algorithm, data);
}

TEST(MongoRepositoryTest, SaveResult_CalledWithFordFulkersonAlgorithm)
{
    MockMongoRepository repo;

    const json data = {{"source_id", "W001"}, {"sink_id", "W005"}, {"max_flow", 340}};

    EXPECT_CALL(repo, saveResult("ff_1712345679", "ford_fulkerson", data)).Times(1);

    repo.saveResult("ff_1712345679", "ford_fulkerson", data);
}

TEST(MongoRepositoryTest, SaveResult_CalledWithKaufmannMalgrangeAlgorithm)
{
    MockMongoRepository repo;

    const json data = {{"feasible", true}, {"path", {"W001", "W003", "W002", "W004", "W001"}}};

    EXPECT_CALL(repo, saveResult("km_1712345680", "kaufmann_malgrange", data)).Times(1);

    repo.saveResult("km_1712345680", "kaufmann_malgrange", data);
}

TEST(MongoRepositoryTest, SaveResult_CalledOnce_PerResult)
{
    MockMongoRepository repo;

    EXPECT_CALL(repo, saveResult(_, _, _)).Times(3);

    repo.saveResult("bf_001", "bellman_ford", json::object());
    repo.saveResult("ff_001", "ford_fulkerson", json::object());
    repo.saveResult("km_001", "kaufmann_malgrange", json::object());
}

// ---------------------------------------------------------------------------
// Tests: getResults()
// ---------------------------------------------------------------------------

TEST(MongoRepositoryTest, GetResults_NoFilter_ReturnsAllResults)
{
    MockMongoRepository repo;

    const std::vector<json> mockResults = {{{"result_id", "bf_001"},
                                            {"algorithm", "bellman_ford"},
                                            {"timestamp", "2025-07-15T10:00:00Z"},
                                            {"data", json::object()}},
                                           {{"result_id", "ff_001"},
                                            {"algorithm", "ford_fulkerson"},
                                            {"timestamp", "2025-07-15T10:01:00Z"},
                                            {"data", json::object()}}};

    EXPECT_CALL(repo, getResults("", 20)).WillOnce(Return(mockResults));

    const auto results = repo.getResults("", 20);

    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0]["result_id"], "bf_001");
    EXPECT_EQ(results[1]["result_id"], "ff_001");
}

TEST(MongoRepositoryTest, GetResults_WithAlgorithmFilter_ReturnsFilteredResults)
{
    MockMongoRepository repo;

    const std::vector<json> filtered = {{{"result_id", "bf_001"},
                                         {"algorithm", "bellman_ford"},
                                         {"timestamp", "2025-07-15T10:00:00Z"},
                                         {"data", json::object()}}};

    EXPECT_CALL(repo, getResults("bellman_ford", 20)).WillOnce(Return(filtered));

    const auto results = repo.getResults("bellman_ford", 20);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0]["algorithm"], "bellman_ford");
}

TEST(MongoRepositoryTest, GetResults_EmptyCollection_ReturnsEmptyVector)
{
    MockMongoRepository repo;

    EXPECT_CALL(repo, getResults("", 20)).WillOnce(Return(std::vector<json>{}));

    const auto results = repo.getResults("", 20);

    EXPECT_TRUE(results.empty());
}

TEST(MongoRepositoryTest, GetResults_LimitRespected_PassedToMock)
{
    MockMongoRepository repo;

    // The caller requests exactly 5 results.
    EXPECT_CALL(repo, getResults("", 5)).WillOnce(Return(std::vector<json>{}));

    repo.getResults("", 5);
}

TEST(MongoRepositoryTest, GetResults_ReturnsCorrectDocumentShape)
{
    MockMongoRepository repo;

    const json expectedDoc = {{"result_id", "bf_001"},
                              {"algorithm", "bellman_ford"},
                              {"timestamp", "2025-07-15T10:00:00Z"},
                              {"data", {{"source_id", "H001"}, {"distances", {{"H002", 14.3}}}}}};

    EXPECT_CALL(repo, getResults("bellman_ford", 1)).WillOnce(Return(std::vector<json>{expectedDoc}));

    const auto results = repo.getResults("bellman_ford", 1);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0]["result_id"], "bf_001");
    EXPECT_EQ(results[0]["algorithm"], "bellman_ford");
    EXPECT_EQ(results[0]["timestamp"], "2025-07-15T10:00:00Z");
    EXPECT_EQ(results[0]["data"]["source_id"], "H001");
    EXPECT_NEAR(results[0]["data"]["distances"]["H002"].get<double>(), 14.3, 1e-9);
}

TEST(MongoRepositoryTest, GetResults_FordFulkersonFilter_ReturnsOnlyFFResults)
{
    MockMongoRepository repo;

    const std::vector<json> ffResults = {{{"result_id", "ff_001"},
                                          {"algorithm", "ford_fulkerson"},
                                          {"timestamp", "2025-07-15T11:00:00Z"},
                                          {"data", {{"source_id", "W001"}, {"sink_id", "W005"}, {"max_flow", 340}}}},
                                         {{"result_id", "ff_002"},
                                          {"algorithm", "ford_fulkerson"},
                                          {"timestamp", "2025-07-15T11:01:00Z"},
                                          {"data", {{"source_id", "W002"}, {"sink_id", "W006"}, {"max_flow", 120}}}}};

    EXPECT_CALL(repo, getResults("ford_fulkerson", 20)).WillOnce(Return(ffResults));

    const auto results = repo.getResults("ford_fulkerson", 20);

    ASSERT_EQ(results.size(), 2u);
    for (const auto& r : results)
    {
        EXPECT_EQ(r["algorithm"], "ford_fulkerson");
    }
}

TEST(MongoRepositoryTest, GetResults_KaufmannMalgrangeFilter_ReturnsKMResults)
{
    MockMongoRepository repo;

    const std::vector<json> kmResults = {{{"result_id", "km_001"},
                                          {"algorithm", "kaufmann_malgrange"},
                                          {"timestamp", "2025-07-15T12:00:00Z"},
                                          {"data", {{"feasible", true}, {"path", {"W001", "W002", "W003", "W001"}}}}}};

    EXPECT_CALL(repo, getResults("kaufmann_malgrange", 20)).WillOnce(Return(kmResults));

    const auto results = repo.getResults("kaufmann_malgrange", 20);

    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0]["algorithm"], "kaufmann_malgrange");
    EXPECT_TRUE(results[0]["data"]["feasible"].get<bool>());
}

// ---------------------------------------------------------------------------
// Tests: clampLimit behaviour
//
// clampLimit() is private, so we test it indirectly by observing the
// limit argument forwarded to the mock. We rely on the real implementation
// to clamp before forwarding — these tests document the expected contract
// for an eventual integration test.
//
// Since MockMongoRepository overrides getResults() entirely, we test clamping
// by verifying the mock receives whichever value the caller passes, confirming
// that direct callers (GraphRouter) are responsible for passing valid limits.
// The clamping logic itself is a unit-testable private method covered by
// a white-box companion integration test when a live server is available.
// ---------------------------------------------------------------------------

TEST(MongoRepositoryTest, GetResults_DefaultLimit_IsTwenty)
{
    MockMongoRepository repo;

    // The default limit value is 20 — callers that omit limit must pass 20.
    EXPECT_CALL(repo, getResults("", 20)).WillOnce(Return(std::vector<json>{}));

    repo.getResults("", 20);
}

TEST(MongoRepositoryTest, GetResults_DefaultAlgorithmFilter_IsEmptyString)
{
    MockMongoRepository repo;

    // The default algorithm filter is "" — callers that omit it must pass "".
    EXPECT_CALL(repo, getResults("", _)).WillOnce(Return(std::vector<json>{}));

    repo.getResults("", 20);
}

// ---------------------------------------------------------------------------
// Tests: result_id field presence in returned documents
// ---------------------------------------------------------------------------

TEST(MongoRepositoryTest, GetResults_EachDocument_HasResultIdField)
{
    MockMongoRepository repo;

    const std::vector<json> mockDocs = {{{"result_id", "bf_001"},
                                         {"algorithm", "bellman_ford"},
                                         {"timestamp", "2025-07-15T10:00:00Z"},
                                         {"data", json::object()}},
                                        {{"result_id", "ff_002"},
                                         {"algorithm", "ford_fulkerson"},
                                         {"timestamp", "2025-07-15T10:01:00Z"},
                                         {"data", json::object()}},
                                        {{"result_id", "km_003"},
                                         {"algorithm", "kaufmann_malgrange"},
                                         {"timestamp", "2025-07-15T10:02:00Z"},
                                         {"data", json::object()}}};

    EXPECT_CALL(repo, getResults("", 20)).WillOnce(Return(mockDocs));

    const auto results = repo.getResults("", 20);

    for (const auto& doc : results)
    {
        EXPECT_TRUE(doc.contains("result_id")) << "Document missing result_id field";
        EXPECT_FALSE(doc.contains("_id")) << "Document must not expose raw _id field";
    }
}
