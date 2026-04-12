#include "graph.hpp"
#include "graph_router.hpp"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using ::testing::_;
using ::testing::Return;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Mock classes
// ---------------------------------------------------------------------------
class MockMongoRepository : public MongoRepository
{
  public:
    MOCK_METHOD(bool, is_connected, (), (const, override));
    MOCK_METHOD(void, saveResult, (const std::string&, const std::string&, const nlohmann::json&), (override));
    MOCK_METHOD(std::vector<nlohmann::json>, getResults, (const std::string&, int), (override));
};

class MockDatabase : public Database
{
  public:
    MOCK_METHOD(nlohmann::json, buildGraphSnapshot, (), (override));
    MOCK_METHOD(bool, replaceConnections, (const std::string&, const nlohmann::json&), (override));
    MOCK_METHOD(bool, updateInventoryStockLevel, (const std::string&, int, int), (override));
    MOCK_METHOD(bool, insertOrUpdateUser, (const std::string&, double, double), (override));
    MOCK_METHOD(bool, updateUserOnlineStatus, (const std::string&, bool), (override));
    MOCK_METHOD(bool, insertOrUpdateInventory, (const std::string&, int, int, int), (override));
    MOCK_METHOD(std::string, findWarehouseForItem, (int, int), (override));
};

// ---------------------------------------------------------------------------
// Helper: build a minimal valid map JSON string
// Two markets H001→H002 and two FCs W001→W002.
// ---------------------------------------------------------------------------
namespace
{

std::string minimalMapBody()
{
    return R"([
        {
            "node_id": "H001", "node_type": "market",
            "node_location": { "latitude": 0, "longitude": 0 },
            "is_secure": true, "is_active": true,
            "connections": [
                { "target_node_id": "H002", "base_weight": 5.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "H002", "node_type": "market",
            "node_location": { "latitude": 1, "longitude": 0 },
            "is_secure": true, "is_active": true,
            "connections": []
        },
        {
            "node_id": "W001", "node_type": "fulfillment_center",
            "node_location": { "latitude": 2, "longitude": 0 },
            "is_secure": true, "is_active": true,
            "connections": [
                { "target_node_id": "W002", "base_weight": 8.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        },
        {
            "node_id": "W002", "node_type": "fulfillment_center",
            "node_location": { "latitude": 3, "longitude": 0 },
            "is_secure": true, "is_active": true,
            "connections": [
                { "target_node_id": "W001", "base_weight": 8.0,
                  "connection_type": "road", "connection_conditions": [] }
            ]
        }
    ])";
}

json twoNodeSnapshot()
{
    return json::parse(R"([
        {
            "node_id": "W001", "node_type": "fulfillment_center",
            "node_location": {"latitude": 0, "longitude": 0},
            "is_secure": true, "is_active": true,
            "connections": [
                {"target_node_id": "W002", "base_weight": 8.0,
                 "connection_type": "road", "connection_conditions": []}
            ]
        },
        {
            "node_id": "W002", "node_type": "fulfillment_center",
            "node_location": {"latitude": 1, "longitude": 0},
            "is_secure": true, "is_active": true,
            "connections": [
                {"target_node_id": "W001", "base_weight": 8.0,
                 "connection_type": "road", "connection_conditions": []}
            ]
        }
    ])");
}

json marketSnapshot()
{
    return json::parse(R"([
        {
            "node_id": "H001", "node_type": "market",
            "node_location": {"latitude": 0, "longitude": 0},
            "is_secure": true, "is_active": true,
            "connections": [
                {"target_node_id": "H002", "base_weight": 5.0,
                 "connection_type": "road", "connection_conditions": []}
            ]
        },
        {
            "node_id": "H002", "node_type": "market",
            "node_location": {"latitude": 1, "longitude": 0},
            "is_secure": true, "is_active": true,
            "connections": []
        }
    ])");
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------
class GraphRouterTest : public ::testing::Test
{
  protected:
    MockMongoRepository mockMongo;
    MockDatabase mockDb;
    GraphRouter router{mockMongo};
    int statusCode = 0;

    // Helper: POST /map with the minimal map.
    void loadMinimalMap()
    {
        EXPECT_CALL(mockMongo, saveResult(_, _, _)).Times(::testing::AnyNumber());
        router.route("POST", "/map", minimalMapBody(), statusCode);
        ASSERT_EQ(statusCode, 200);
    }
};

// ===========================================================================
// POST /map
// ===========================================================================

TEST_F(GraphRouterTest, PostMap_ValidPayload_Returns200)
{
    router.route("POST", "/map", minimalMapBody(), statusCode);
    EXPECT_EQ(statusCode, 200);
}

TEST_F(GraphRouterTest, PostMap_ValidPayload_ResponseHasStatusOk)
{
    const std::string body = router.route("POST", "/map", minimalMapBody(), statusCode);
    const json resp = json::parse(body);
    EXPECT_EQ(resp["status"], "ok");
}

TEST_F(GraphRouterTest, PostMap_ValidPayload_ReportsNodeCount)
{
    const std::string body = router.route("POST", "/map", minimalMapBody(), statusCode);
    const json resp = json::parse(body);
    EXPECT_EQ(resp["nodes_loaded"], 4);
}

TEST_F(GraphRouterTest, PostMap_ValidPayload_ReportsEdgeCount)
{
    const std::string body = router.route("POST", "/map", minimalMapBody(), statusCode);
    const json resp = json::parse(body);
    EXPECT_EQ(resp["edges_loaded"], 3); // H001→H002, W001→W002, W002→W001
}

TEST_F(GraphRouterTest, PostMap_InvalidJson_Returns400)
{
    router.route("POST", "/map", "not json at all", statusCode);
    EXPECT_EQ(statusCode, 400);
}

TEST_F(GraphRouterTest, PostMap_InvalidJson_ResponseHasError)
{
    const std::string body = router.route("POST", "/map", "not json", statusCode);
    const json resp = json::parse(body);
    EXPECT_EQ(resp["status"], "error");
    EXPECT_TRUE(resp.contains("message"));
}

TEST_F(GraphRouterTest, PostMap_DuplicateNodeId_Returns400)
{
    const std::string dupMap = R"([
        { "node_id": "W001", "node_type": "fulfillment_center",
          "node_location": {"latitude":0,"longitude":0},
          "is_secure":true,"is_active":true,"connections":[] },
        { "node_id": "W001", "node_type": "fulfillment_center",
          "node_location": {"latitude":1,"longitude":0},
          "is_secure":true,"is_active":true,"connections":[] }
    ])";

    router.route("POST", "/map", dupMap, statusCode);
    EXPECT_EQ(statusCode, 400);
}

TEST_F(GraphRouterTest, PostMap_IsIdempotent_SecondCallReplacesGraph)
{
    router.route("POST", "/map", minimalMapBody(), statusCode);
    EXPECT_EQ(statusCode, 200);

    // Second call with a single-node map.
    const std::string singleNode = R"([
        { "node_id": "H001", "node_type": "market",
          "node_location": {"latitude":0,"longitude":0},
          "is_secure":true,"is_active":true,"connections":[] }
    ])";

    const std::string body = router.route("POST", "/map", singleNode, statusCode);
    EXPECT_EQ(statusCode, 200);
    const json resp = json::parse(body);
    EXPECT_EQ(resp["nodes_loaded"], 1);
}

// ===========================================================================
// POST /markets-path
// ===========================================================================

TEST_F(GraphRouterTest, MarketsPath_NoMapLoaded_Returns400)
{
    // No POST /map first.
    router.route("POST", "/markets-path", R"({"source_id":"H001"})", statusCode);
    EXPECT_EQ(statusCode, 400);
}

TEST_F(GraphRouterTest, MarketsPath_ValidSource_Returns200)
{
    loadMinimalMap();
    EXPECT_CALL(mockMongo, saveResult(_, "bellman_ford", _)).Times(1);

    router.route("POST", "/markets-path", R"({"source_id":"H001"})", statusCode);
    EXPECT_EQ(statusCode, 200);
}

TEST_F(GraphRouterTest, MarketsPath_ValidSource_ResponseHasAlgorithmField)
{
    loadMinimalMap();
    EXPECT_CALL(mockMongo, saveResult(_, _, _)).Times(1);

    const std::string body = router.route("POST", "/markets-path", R"({"source_id":"H001"})", statusCode);
    const json resp = json::parse(body);
    EXPECT_EQ(resp["algorithm"], "bellman_ford");
}

TEST_F(GraphRouterTest, MarketsPath_ValidSource_ResponseHasDistances)
{
    loadMinimalMap();
    EXPECT_CALL(mockMongo, saveResult(_, _, _)).Times(1);

    const std::string body = router.route("POST", "/markets-path", R"({"source_id":"H001"})", statusCode);
    const json resp = json::parse(body);
    EXPECT_TRUE(resp.contains("distances"));
    // H001→H002: base=5, road, no conditions → cost=5.0
    EXPECT_NEAR(resp["distances"]["H002"].get<double>(), 5.0, 1e-9);
}

TEST_F(GraphRouterTest, MarketsPath_ValidSource_ResponseHasResultId)
{
    loadMinimalMap();
    EXPECT_CALL(mockMongo, saveResult(_, _, _)).Times(1);

    const std::string body = router.route("POST", "/markets-path", R"({"source_id":"H001"})", statusCode);
    const json resp = json::parse(body);
    EXPECT_TRUE(resp.contains("result_id"));
    const std::string rid = resp["result_id"].get<std::string>();
    EXPECT_EQ(rid.substr(0, 3), "bf_"); // Correct prefix.
}

TEST_F(GraphRouterTest, MarketsPath_NonMarketSource_Returns400)
{
    loadMinimalMap();

    // W001 is a fulfillment center, not a market.
    router.route("POST", "/markets-path", R"({"source_id":"W001"})", statusCode);
    EXPECT_EQ(statusCode, 400);
}

TEST_F(GraphRouterTest, MarketsPath_UnknownSource_Returns400)
{
    loadMinimalMap();

    router.route("POST", "/markets-path", R"({"source_id":"H999"})", statusCode);
    EXPECT_EQ(statusCode, 400);
}

TEST_F(GraphRouterTest, MarketsPath_MissingSourceId_Returns400)
{
    loadMinimalMap();

    router.route("POST", "/markets-path", R"({})", statusCode);
    EXPECT_EQ(statusCode, 400);
}

TEST_F(GraphRouterTest, MarketsPath_PersistsResultToMongo)
{
    loadMinimalMap();

    json savedData;
    EXPECT_CALL(mockMongo, saveResult(_, "bellman_ford", _)).Times(1).WillOnce(::testing::SaveArg<2>(&savedData));

    router.route("POST", "/markets-path", R"({"source_id":"H001"})", statusCode);

    EXPECT_EQ(savedData["source_id"], "H001");
    EXPECT_TRUE(savedData.contains("distances"));
}

// ===========================================================================
// POST /fulfillment-flow
// ===========================================================================

TEST_F(GraphRouterTest, FulfillmentFlow_NoMapLoaded_Returns400)
{
    router.route("POST", "/fulfillment-flow", R"({"source_id":"W001","sink_id":"W002"})", statusCode);
    EXPECT_EQ(statusCode, 400);
}

TEST_F(GraphRouterTest, FulfillmentFlow_ValidSourceSink_Returns200)
{
    loadMinimalMap();
    EXPECT_CALL(mockMongo, saveResult(_, "ford_fulkerson", _)).Times(1);

    router.route("POST", "/fulfillment-flow", R"({"source_id":"W001","sink_id":"W002"})", statusCode);
    EXPECT_EQ(statusCode, 200);
}

TEST_F(GraphRouterTest, FulfillmentFlow_ResponseHasAlgorithmField)
{
    loadMinimalMap();
    EXPECT_CALL(mockMongo, saveResult(_, _, _)).Times(1);

    const std::string body =
        router.route("POST", "/fulfillment-flow", R"({"source_id":"W001","sink_id":"W002"})", statusCode);
    const json resp = json::parse(body);
    EXPECT_EQ(resp["algorithm"], "ford_fulkerson");
}

TEST_F(GraphRouterTest, FulfillmentFlow_ResponseHasMaxFlow)
{
    loadMinimalMap();
    EXPECT_CALL(mockMongo, saveResult(_, _, _)).Times(1);

    const std::string body =
        router.route("POST", "/fulfillment-flow", R"({"source_id":"W001","sink_id":"W002"})", statusCode);
    const json resp = json::parse(body);
    EXPECT_TRUE(resp.contains("max_flow"));
    EXPECT_EQ(resp["max_flow"], 8); // Single edge W001→W002 capacity=8.
}

TEST_F(GraphRouterTest, FulfillmentFlow_ResultIdHasFfPrefix)
{
    loadMinimalMap();
    EXPECT_CALL(mockMongo, saveResult(_, _, _)).Times(1);

    const std::string body =
        router.route("POST", "/fulfillment-flow", R"({"source_id":"W001","sink_id":"W002"})", statusCode);
    const json resp = json::parse(body);
    EXPECT_EQ(resp["result_id"].get<std::string>().substr(0, 3), "ff_");
}

TEST_F(GraphRouterTest, FulfillmentFlow_MarketSourceNode_Returns400)
{
    loadMinimalMap();

    router.route("POST", "/fulfillment-flow", R"({"source_id":"H001","sink_id":"W002"})", statusCode);
    EXPECT_EQ(statusCode, 400);
}

TEST_F(GraphRouterTest, FulfillmentFlow_SameSourceAndSink_Returns400)
{
    loadMinimalMap();

    router.route("POST", "/fulfillment-flow", R"({"source_id":"W001","sink_id":"W001"})", statusCode);
    EXPECT_EQ(statusCode, 400);
}

TEST_F(GraphRouterTest, FulfillmentFlow_MissingSinkId_Returns400)
{
    loadMinimalMap();

    router.route("POST", "/fulfillment-flow", R"({"source_id":"W001"})", statusCode);
    EXPECT_EQ(statusCode, 400);
}

// ===========================================================================
// POST /fulfillment-circuit
// ===========================================================================

TEST_F(GraphRouterTest, FulfillmentCircuit_NoMapLoaded_Returns400)
{
    router.route("POST", "/fulfillment-circuit", "{}", statusCode);
    EXPECT_EQ(statusCode, 400);
}

TEST_F(GraphRouterTest, FulfillmentCircuit_ValidFCSubgraph_Returns200)
{
    loadMinimalMap();
    EXPECT_CALL(mockMongo, saveResult(_, "kaufmann_malgrange", _)).Times(1);

    router.route("POST", "/fulfillment-circuit", "{}", statusCode);
    EXPECT_EQ(statusCode, 200);
}

TEST_F(GraphRouterTest, FulfillmentCircuit_ResponseHasAlgorithmField)
{
    loadMinimalMap();
    EXPECT_CALL(mockMongo, saveResult(_, _, _)).Times(1);

    const std::string body = router.route("POST", "/fulfillment-circuit", "{}", statusCode);
    const json resp = json::parse(body);
    EXPECT_EQ(resp["algorithm"], "kaufmann_malgrange");
}

TEST_F(GraphRouterTest, FulfillmentCircuit_ResponseHasFeasibleField)
{
    loadMinimalMap();
    EXPECT_CALL(mockMongo, saveResult(_, _, _)).Times(1);

    const std::string body = router.route("POST", "/fulfillment-circuit", "{}", statusCode);
    const json resp = json::parse(body);
    EXPECT_TRUE(resp.contains("feasible"));
}

TEST_F(GraphRouterTest, FulfillmentCircuit_ResponseHasPathField)
{
    loadMinimalMap();
    EXPECT_CALL(mockMongo, saveResult(_, _, _)).Times(1);

    const std::string body = router.route("POST", "/fulfillment-circuit", "{}", statusCode);
    const json resp = json::parse(body);
    EXPECT_TRUE(resp.contains("path"));
}

TEST_F(GraphRouterTest, FulfillmentCircuit_ResultIdHasKmPrefix)
{
    loadMinimalMap();
    EXPECT_CALL(mockMongo, saveResult(_, _, _)).Times(1);

    const std::string body = router.route("POST", "/fulfillment-circuit", "{}", statusCode);
    const json resp = json::parse(body);
    EXPECT_EQ(resp["result_id"].get<std::string>().substr(0, 3), "km_");
}

// ===========================================================================
// GET /results/
// ===========================================================================

TEST_F(GraphRouterTest, GetResults_NoFilter_CallsMongoWithEmptyFilter)
{
    EXPECT_CALL(mockMongo, getResults("", 20)).WillOnce(Return(std::vector<json>{}));

    router.handleGetResults("", 20, statusCode);
    EXPECT_EQ(statusCode, 200);
}

TEST_F(GraphRouterTest, GetResults_WithFilter_PassesFilterToMongo)
{
    EXPECT_CALL(mockMongo, getResults("bellman_ford", 20)).WillOnce(Return(std::vector<json>{}));

    router.handleGetResults("bellman_ford", 20, statusCode);
    EXPECT_EQ(statusCode, 200);
}

TEST_F(GraphRouterTest, GetResults_ResponseHasResultsArray)
{
    EXPECT_CALL(mockMongo, getResults(_, _)).WillOnce(Return(std::vector<json>{}));

    const std::string body = router.handleGetResults("", 20, statusCode);
    const json resp = json::parse(body);
    EXPECT_TRUE(resp.contains("results"));
    EXPECT_TRUE(resp["results"].is_array());
}

TEST_F(GraphRouterTest, GetResults_MongoReturnsDocuments_AppearsInResponse)
{
    const std::vector<json> mockDocs = {{{"result_id", "bf_001"},
                                         {"algorithm", "bellman_ford"},
                                         {"timestamp", "2025-07-15T10:00:00Z"},
                                         {"data", json::object()}}};

    EXPECT_CALL(mockMongo, getResults("", 20)).WillOnce(Return(mockDocs));

    const std::string body = router.handleGetResults("", 20, statusCode);
    const json resp = json::parse(body);
    ASSERT_EQ(resp["results"].size(), 1u);
    EXPECT_EQ(resp["results"][0]["result_id"], "bf_001");
}

TEST_F(GraphRouterTest, UnknownRoute_ResponseHasErrorStatus)
{
    const std::string body = router.route("DELETE", "/map", "", statusCode);
    const json resp = json::parse(body);
    EXPECT_EQ(resp["status"], "error");
}

// ===========================================================================
// refreshFromDatabase()
// ===========================================================================

TEST_F(GraphRouterTest, Refresh_ValidSnapshot_LoadsGraph)
{
    EXPECT_CALL(mockDb, buildGraphSnapshot()).WillOnce(Return(twoNodeSnapshot()));

    // Should not throw and should update the internal graph.
    EXPECT_NO_THROW(router.refreshFromDatabase(mockDb));
}

TEST_F(GraphRouterTest, Refresh_MakesAlgorithmsUseLiveData)
{
    // After refresh with two FC nodes, fulfillment-flow should work without
    // needing a prior POST /map call.
    EXPECT_CALL(mockDb, buildGraphSnapshot()).WillOnce(Return(twoNodeSnapshot()));
    EXPECT_CALL(mockMongo, saveResult(_, "ford_fulkerson", _)).Times(1);

    router.refreshFromDatabase(mockDb);

    const std::string body =
        router.route("POST", "/fulfillment-flow", R"({"source_id":"W001","sink_id":"W002"})", statusCode);

    EXPECT_EQ(statusCode, 200);
    const json resp = json::parse(body);
    EXPECT_EQ(resp["algorithm"], "ford_fulkerson");
    EXPECT_GE(resp["max_flow"].get<int>(), 0);
}

TEST_F(GraphRouterTest, Refresh_EmptySnapshot_GraphBecomesEmpty)
{
    // First load a real graph.
    EXPECT_CALL(mockDb, buildGraphSnapshot()).WillOnce(Return(twoNodeSnapshot())).WillOnce(Return(json::array()));

    router.refreshFromDatabase(mockDb);
    router.refreshFromDatabase(mockDb); // Second call with empty snapshot.

    // Algorithms should now report no map loaded.
    const std::string body =
        router.route("POST", "/fulfillment-flow", R"({"source_id":"W001","sink_id":"W002"})", statusCode);

    EXPECT_EQ(statusCode, 400);
}

TEST_F(GraphRouterTest, Refresh_ReplacesManuallyUploadedMap)
{
    // Upload a market-only map manually.
    router.route("POST", "/map", marketSnapshot().dump(), statusCode);
    ASSERT_EQ(statusCode, 200);

    // Then refresh from DB with FC nodes — FC algorithms should now work.
    EXPECT_CALL(mockDb, buildGraphSnapshot()).WillOnce(Return(twoNodeSnapshot()));
    EXPECT_CALL(mockMongo, saveResult(_, "ford_fulkerson", _)).Times(1);

    router.refreshFromDatabase(mockDb);

    router.route("POST", "/fulfillment-flow", R"({"source_id":"W001","sink_id":"W002"})", statusCode);
    EXPECT_EQ(statusCode, 200);
}

TEST_F(GraphRouterTest, UnknownRoute_StillReturns404)
{
    // Ensure we didn't accidentally remove the 404 path.
    router.route("GET", "/nonexistent", "", statusCode);
    EXPECT_EQ(statusCode, 404);
}
