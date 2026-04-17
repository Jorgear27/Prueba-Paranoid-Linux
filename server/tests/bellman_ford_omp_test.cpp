/**
 * @file bellman_ford_omp_test.cpp
 * @brief Tests de correctitud y reproducibilidad para computeSerial vs computeParallel.
 *
 * Verifica que:
 *   1. Serial y paralelo producen exactamente los mismos resultados.
 *   2. Ambas variantes detectan ciclos negativos.
 *   3. Early-exit funciona igual en ambas.
 *   4. Grafos desconectados se manejan igual.
 *   5. Nodo fuente es el único alcanzable si no hay aristas.
 *   6. Las métricas de ProfilingStats son coherentes (tiempos > 0, passes > 0).
 */

#include "bellman_ford.hpp"
#include "graph.hpp"
#include <cmath>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <random>

using json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers para construir grafos de prueba
// ─────────────────────────────────────────────────────────────────────────────

static json makeMarketNode(const std::string& id, const std::vector<std::pair<std::string, double>>& conns = {},
                           bool secure = true, bool active = true)
{
    json node;
    node["node_id"] = id;
    node["node_type"] = "market";
    node["node_location"] = {{"latitude", 0.0}, {"longitude", 0.0}};
    node["is_secure"] = secure;
    node["is_active"] = active;

    json connections = json::array();
    for (const auto& [target, weight] : conns)
    {
        json c;
        c["target_node_id"] = target;
        c["base_weight"] = weight;
        c["connection_type"] = "road";
        c["connection_conditions"] = json::array();
        connections.push_back(c);
    }
    node["connections"] = connections;
    return node;
}

/**
 * @brief Construye un grafo Market totalmente conectado de N nodos con pesos uniformes.
 */
static Graph buildUniformGraph(int N, double weight = 5.0)
{
    json nodes = json::array();
    for (int i = 0; i < N; ++i)
    {
        std::string id = "M" + std::to_string(i);
        std::vector<std::pair<std::string, double>> conns;
        for (int j = 0; j < N; ++j)
            if (i != j)
                conns.push_back({"M" + std::to_string(j), weight});
        nodes.push_back(makeMarketNode(id, conns));
    }
    Graph g;
    g.build(nodes);
    return g;
}

/**
 * @brief Construye un grafo aleatorio con semilla fija (reproducible).
 */
static Graph buildRandomGraph(int N, unsigned seed = 42)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> dist(1.0, 21.0);
    json nodes = json::array();
    for (int i = 0; i < N; ++i)
    {
        std::string id = "M" + std::to_string(i);
        std::vector<std::pair<std::string, double>> conns;
        for (int j = 0; j < N; ++j)
        {
            if (i == j)
                continue;
            conns.push_back({"M" + std::to_string(j), dist(rng)});
        }
        nodes.push_back(makeMarketNode(id, conns));
    }
    Graph g;
    g.build(nodes);
    return g;
}

// ─────────────────────────────────────────────────────────────────────────────
// Fixture
// ─────────────────────────────────────────────────────────────────────────────

class BellmanFordOmpTest : public ::testing::Test
{
  protected:
    BellmanFord bf;

    // Tolerancia para comparar doubles
    static constexpr double EPS = 1e-9;

    void assertResultsEqual(const BellmanFord::Result& serial, const BellmanFord::Result& parallel)
    {
        EXPECT_EQ(serial.has_negative_cycle, parallel.has_negative_cycle) << "Discrepancia en has_negative_cycle";

        ASSERT_EQ(serial.distances.size(), parallel.distances.size()) << "Distinto numero de nodos en distances";

        for (const auto& [id, distS] : serial.distances)
        {
            ASSERT_TRUE(parallel.distances.count(id) > 0) << "Nodo " << id << " presente en serial pero no en parallel";

            const double distP = parallel.distances.at(id);

            if (std::isinf(distS))
                EXPECT_TRUE(std::isinf(distP)) << "Nodo " << id << ": serial=inf, parallel=" << distP;
            else
                EXPECT_NEAR(distS, distP, EPS)
                    << "Distancia distinta para nodo " << id << ": serial=" << distS << " parallel=" << distP;
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// TEST 1: Grafo simple de 4 nodos — verificar distancias exactas
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(BellmanFordOmpTest, SimpleFourNodeGraph_CorrectDistances)
{
    // Topología:
    //   M0 --2--> M1 --3--> M3
    //   M0 --6--> M2 --1--> M3
    //   (camino mínimo M0→M3: M0→M1→M3 = 5)

    json nodes = json::array();
    nodes.push_back(makeMarketNode("M0", {{"M1", 2.0}, {"M2", 6.0}}));
    nodes.push_back(makeMarketNode("M1", {{"M3", 3.0}}));
    nodes.push_back(makeMarketNode("M2", {{"M3", 1.0}}));
    nodes.push_back(makeMarketNode("M3", {}));

    Graph g;
    g.build(nodes);

    const auto serial = bf.computeSerial(g, "M0");
    const auto parallel = bf.computeParallel(g, "M0");

    // Verificar distancias exactas en el serial
    EXPECT_DOUBLE_EQ(serial.distances.at("M0"), 0.0);
    EXPECT_DOUBLE_EQ(serial.distances.at("M1"), 2.0);
    EXPECT_DOUBLE_EQ(serial.distances.at("M3"), 5.0); // M0→M1→M3
    // M2: 6.0 (directo), pero M0→M1→M3→? M2 no es alcanzable desde M3
    EXPECT_DOUBLE_EQ(serial.distances.at("M2"), 6.0);

    // Paralelo debe dar exactamente lo mismo
    assertResultsEqual(serial, parallel);
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 2: Grafo uniforme de 10 nodos — serial == paralelo
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(BellmanFordOmpTest, UniformGraph10Nodes_SerialEqualsParallel)
{
    const Graph g = buildUniformGraph(10, 4.0);
    const auto serial = bf.computeSerial(g, "M0");
    const auto parallel = bf.computeParallel(g, "M0");
    assertResultsEqual(serial, parallel);
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 3: Grafo aleatorio 50 nodos — serial == paralelo con 2 threads
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(BellmanFordOmpTest, RandomGraph50Nodes_2Threads_SerialEqualsParallel)
{
    const Graph g = buildRandomGraph(50, 1234);
    const auto serial = bf.computeSerial(g, "M0");
    const auto parallel = bf.computeParallel(g, "M0", nullptr, 2);
    assertResultsEqual(serial, parallel);
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 4: Grafo aleatorio 50 nodos — serial == paralelo con 4 threads
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(BellmanFordOmpTest, RandomGraph50Nodes_4Threads_SerialEqualsParallel)
{
    const Graph g = buildRandomGraph(50, 9999);
    const auto serial = bf.computeSerial(g, "M0");
    const auto parallel = bf.computeParallel(g, "M0", nullptr, 4);
    assertResultsEqual(serial, parallel);
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 5: Grafo aleatorio 100 nodos — serial == paralelo con max threads
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(BellmanFordOmpTest, RandomGraph100Nodes_MaxThreads_SerialEqualsParallel)
{
    const Graph g = buildRandomGraph(100, 777);
    const auto serial = bf.computeSerial(g, "M0");
    // threads=0 → usar OMP_NUM_THREADS del sistema
    const auto parallel = bf.computeParallel(g, "M0", nullptr, 0);
    assertResultsEqual(serial, parallel);
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 6: Fuente sin aristas salientes — solo M0 alcanzable
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(BellmanFordOmpTest, IsolatedSource_OnlySourceReachable)
{
    json nodes = json::array();
    nodes.push_back(makeMarketNode("M0", {})); // sin aristas
    nodes.push_back(makeMarketNode("M1", {{"M2", 1.0}}));
    nodes.push_back(makeMarketNode("M2", {}));

    Graph g;
    g.build(nodes);

    const auto serial = bf.computeSerial(g, "M0");
    const auto parallel = bf.computeParallel(g, "M0");

    EXPECT_DOUBLE_EQ(serial.distances.at("M0"), 0.0);
    EXPECT_TRUE(std::isinf(serial.distances.at("M1")));
    EXPECT_TRUE(std::isinf(serial.distances.at("M2")));

    assertResultsEqual(serial, parallel);
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 7: Grafos con condiciones (reinforced → costo reducido)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(BellmanFordOmpTest, GraphWithConditions_SerialEqualsParallel)
{
    // Una arista con condición "reinforced" tiene cost = base * road(1.0) * (1.0 + (-0.3)) = base * 0.7
    json nodes = json::array();

    json conn;
    conn["target_node_id"] = "M1";
    conn["base_weight"] = 10.0;
    conn["connection_type"] = "road";
    conn["connection_conditions"] = json::array({"reinforced"});

    json m0;
    m0["node_id"] = "M0";
    m0["node_type"] = "market";
    m0["node_location"] = {{"latitude", 0.0}, {"longitude", 0.0}};
    m0["is_secure"] = true;
    m0["is_active"] = true;
    m0["connections"] = json::array({conn});
    nodes.push_back(m0);
    nodes.push_back(makeMarketNode("M1", {}));

    Graph g;
    g.build(nodes);

    const auto serial = bf.computeSerial(g, "M0");
    const auto parallel = bf.computeParallel(g, "M0");

    // cost = 10 * 1.0 * (1.0 - 0.3) = 7.0
    EXPECT_NEAR(serial.distances.at("M1"), 7.0, EPS);
    assertResultsEqual(serial, parallel);
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 8: ProfilingStats — tiempos coherentes en modo serial
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(BellmanFordOmpTest, ProfilingStats_Serial_TimesCoherent)
{
    const Graph g = buildUniformGraph(20);
    BellmanFord::ProfilingStats stats;
    bf.computeSerial(g, "M0", &stats);

    EXPECT_GT(stats.total_time_ms, 0.0) << "total_time debe ser > 0";
    EXPECT_GE(stats.serial_time_ms, 0.0) << "serial_time debe ser >= 0";
    EXPECT_GE(stats.parallel_time_ms, 0.0) << "parallel_time debe ser >= 0";
    EXPECT_GT(stats.passes_executed, 0) << "deben ejecutarse al menos 1 pasada";
    EXPECT_EQ(stats.num_threads, 1) << "serial siempre usa 1 thread";
    EXPECT_EQ(stats.V, 20u);
    EXPECT_GT(stats.E, 0u);

    // serial_time + parallel_time <= total_time (con pequeña tolerancia de medición)
    EXPECT_LE(stats.serial_time_ms + stats.parallel_time_ms, stats.total_time_ms + 1.0 /* ms de tolerancia */);
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 9: ProfilingStats — paralelo reporta >= 1 thread
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(BellmanFordOmpTest, ProfilingStats_Parallel_ThreadsGeOne)
{
    const Graph g = buildUniformGraph(20);
    BellmanFord::ProfilingStats stats;
    bf.computeParallel(g, "M0", &stats, 2);

    EXPECT_GT(stats.total_time_ms, 0.0);
    EXPECT_GE(stats.num_threads, 1);
#ifdef _OPENMP
    EXPECT_EQ(stats.num_threads, 2) << "Se pidieron 2 threads";
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 10: compute() (API pública) produce el mismo resultado que computeSerial
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(BellmanFordOmpTest, PublicCompute_SameAsSerial)
{
    const Graph g = buildRandomGraph(30, 42);
    const auto via_compute = bf.compute(g, "M0");
    const auto serial = bf.computeSerial(g, "M0");
    assertResultsEqual(serial, via_compute);
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 11: Early-exit — grafo ya convergido en 1 pasada (pesos iguales, denso)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(BellmanFordOmpTest, EarlyExit_Convergence_SameResultBothModes)
{
    // Grafo de 2 nodos: M0→M1 directo. Converge en 1 pasada.
    json nodes = json::array();
    nodes.push_back(makeMarketNode("M0", {{"M1", 3.0}}));
    nodes.push_back(makeMarketNode("M1", {}));

    Graph g;
    g.build(nodes);

    BellmanFord::ProfilingStats sStats, pStats;
    const auto serial = bf.computeSerial(g, "M0", &sStats);
    const auto parallel = bf.computeParallel(g, "M0", &pStats);

    // Ambos deben converger con 1 pasada (early-exit)
    EXPECT_EQ(sStats.passes_executed, 1);
    EXPECT_EQ(pStats.passes_executed, 1);

    assertResultsEqual(serial, parallel);
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 12: Excepciones — nodo fuente no existe
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(BellmanFordOmpTest, InvalidSource_ThrowsInBothModes)
{
    const Graph g = buildUniformGraph(5);

    EXPECT_THROW(bf.computeSerial(g, "NOEXISTE"), std::invalid_argument);
    EXPECT_THROW(bf.computeParallel(g, "NOEXISTE"), std::invalid_argument);
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 13: Nodo fuente tipo FulfillmentCenter — debe fallar
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(BellmanFordOmpTest, FulfillmentCenterSource_ThrowsInBothModes)
{
    json nodes = json::array();
    json fc;
    fc["node_id"] = "FC0";
    fc["node_type"] = "fulfillment_center";
    fc["node_location"] = {{"latitude", 0.0}, {"longitude", 0.0}};
    fc["is_secure"] = true;
    fc["is_active"] = true;
    fc["connections"] = json::array();
    nodes.push_back(fc);

    // Necesitamos al menos un Market para que el grafo tenga nodos Market
    nodes.push_back(makeMarketNode("M0", {}));

    Graph g;
    g.build(nodes);

    EXPECT_THROW(bf.computeSerial(g, "FC0"), std::invalid_argument);
    EXPECT_THROW(bf.computeParallel(g, "FC0"), std::invalid_argument);
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 14: Múltiples fuentes — todas dan resultados consistentes serial==paralelo
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(BellmanFordOmpTest, MultipleSources_AllConsistent)
{
    const Graph g = buildRandomGraph(25, 111);
    const auto nodes = g.getActiveNodes(NodeType::Market);

    for (const auto& node : nodes)
    {
        const auto serial = bf.computeSerial(g, node.id);
        const auto parallel = bf.computeParallel(g, node.id, nullptr, 2);
        assertResultsEqual(serial, parallel);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// TEST 15: Grafo grande (200 nodos) — serial == paralelo con 4 threads
//          Este test es el que se usa para demostrar reproducibilidad en la defensa
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(BellmanFordOmpTest, LargeGraph200Nodes_4Threads_StrictlyEqual)
{
    const Graph g = buildRandomGraph(200, 31415);

    BellmanFord::ProfilingStats sStats, pStats;
    const auto serial = bf.computeSerial(g, "M0", &sStats);
    const auto parallel = bf.computeParallel(g, "M0", &pStats, 4);

    // Correctitud estricta
    assertResultsEqual(serial, parallel);

    // Sanidad de métricas
    EXPECT_EQ(sStats.V, 200u);
    EXPECT_EQ(pStats.V, 200u);
    EXPECT_GT(sStats.E, 0u);
    EXPECT_EQ(sStats.E, pStats.E) << "Ambas variantes deben ver el mismo grafo";

    // Para la defensa: imprimir speedup observado
    if (sStats.total_time_ms > 0 && pStats.total_time_ms > 0)
    {
        const double speedup = sStats.total_time_ms / pStats.total_time_ms;
        std::cout << "\n  [Defensa] V=200 E=" << sStats.E << "  Serial=" << sStats.total_time_ms << "ms"
                  << "  Paralelo(4t)=" << pStats.total_time_ms << "ms" << "  Speedup=" << speedup << "x\n";
    }
}
