/**
 * @file benchmark_bf.cpp
 * @brief Benchmark standalone de Bellman-Ford serial vs paralelo.
 *
 */

#include "bellman_ford.hpp"
#include "graph.hpp"
#include "log.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#ifdef _OPENMP
#include <omp.h>
#endif

using json = nlohmann::json;

// Generador de grafo sintético

/**
 * @brief Construye un JSON de mapa con `numNodes` nodos Market totalmente
 *        conectados (grafo denso) con pesos aleatorios positivos.
 *
 * Grafo denso → maximiza el trabajo del inner loop → mejor para medir speedup.
 */
/**
 * @brief Construye un grafo Market totalmente conectado con pesos que fuerzan
 *        muchas pasadas de Bellman-Ford (cadena de mínimos crecientes).
 *
 * Para maximizar el trabajo paralelo necesitamos que BF ejecute muchas pasadas.
 * Si los pesos son aleatorios uniformes, BF converge en ~log(V) pasadas.
 * Con pesos en cadena (M0→M1→...→MN) BF necesita N-1 pasadas → más trabajo.
 * Mezclamos ambos estilos: aristas de cadena con peso bajo + densas con peso alto.
 */
json buildSyntheticMarketGraph(int numNodes, unsigned seed = 42)
{
    std::mt19937                          rng(seed);
    std::uniform_real_distribution<double> weightHigh(10.0, 20.0); // aristas densas
    std::uniform_real_distribution<double> weightLow(0.1,   1.0);  // cadena mínima

    json nodes = json::array();

    for (int i = 0; i < numNodes; ++i)
    {
        json node;
        node["node_id"]       = "M" + std::to_string(i);
        node["node_type"]     = "market";
        node["node_location"] = {
            {"latitude",  static_cast<double>(i)},
            {"longitude", static_cast<double>(i)}
        };
        node["is_secure"] = true;
        node["is_active"] = true;

        json connections = json::array();
        for (int j = 0; j < numNodes; ++j)
        {
            if (i == j) continue;
            json conn;
            conn["target_node_id"]        = "M" + std::to_string(j);
            // Aristas de cadena (i→i+1) tienen peso bajo → BF las descubre tarde
            // y necesita más pasadas para propagar el camino mínimo completo
            if (j == i + 1)
                conn["base_weight"] = weightLow(rng);  // cadena mínima
            else
                conn["base_weight"] = weightHigh(rng); // aristas densas altas
            conn["connection_type"]       = "road";
            conn["connection_conditions"] = json::array();
            connections.push_back(conn);
        }
        node["connections"] = connections;
        nodes.push_back(node);
    }
    return nodes;
}

// Formato de tabla
static void printSeparator(int width = 80)
{
    std::cout << std::string(width, '-') << "\n";
}

static void printHeader()
{
    printSeparator();
    std::cout << "  BENCHMARK BELLMAN-FORD: SERIAL vs PARALELO (OpenMP)\n";
    printSeparator();
#ifdef _OPENMP
    std::cout << "  OpenMP disponible. Max threads del sistema: "
              << omp_get_max_threads() << "\n";
#else
    std::cout << "  OpenMP NO disponible. Solo modo serial.\n";
#endif
    printSeparator();
}

// Corre múltiples veces y retorna estadísticas
struct RunStats
{
    double mean_ms  = 0.0;
    double min_ms   = 0.0;
    double max_ms   = 0.0;
    double stddev   = 0.0;
    BellmanFord::ProfilingStats last; // stats de la última corrida
};

RunStats runSerial(BellmanFord& bf, const Graph& graph,
                   const std::string& source, int runs)
{
    std::vector<double> times;
    BellmanFord::ProfilingStats ps;
    for (int r = 0; r < runs; ++r)
        bf.computeSerial(graph, source, &ps), times.push_back(ps.total_time_ms);

    RunStats rs;
    rs.last    = ps;
    rs.min_ms  = *std::min_element(times.begin(), times.end());
    rs.max_ms  = *std::max_element(times.begin(), times.end());
    rs.mean_ms = std::accumulate(times.begin(), times.end(), 0.0) / runs;
    double sq  = 0.0;
    for (double t : times) sq += (t - rs.mean_ms) * (t - rs.mean_ms);
    rs.stddev  = std::sqrt(sq / runs);
    return rs;
}

RunStats runParallel(BellmanFord& bf, const Graph& graph,
                     const std::string& source, int runs, int threads)
{
    std::vector<double> times;
    BellmanFord::ProfilingStats ps;
    for (int r = 0; r < runs; ++r)
        bf.computeParallel(graph, source, &ps, threads),
        times.push_back(ps.total_time_ms);

    RunStats rs;
    rs.last    = ps;
    rs.min_ms  = *std::min_element(times.begin(), times.end());
    rs.max_ms  = *std::max_element(times.begin(), times.end());
    rs.mean_ms = std::accumulate(times.begin(), times.end(), 0.0) / runs;
    double sq  = 0.0;
    for (double t : times) sq += (t - rs.mean_ms) * (t - rs.mean_ms);
    rs.stddev  = std::sqrt(sq / runs);
    return rs;
}

// main
int main(int argc, char* argv[])
{
    // Silenciar el logger para no contaminar la salida del benchmark
    Logger::getInstance().setEnabled(false);

    int numNodes = (argc > 1) ? std::atoi(argv[1]) : 300;
    int numRuns  = (argc > 2) ? std::atoi(argv[2]) : 8;
    if (numNodes < 10)  numNodes = 10;
    if (numRuns  < 1)   numRuns  = 1;

    printHeader();
    std::cout << "  Nodos: " << numNodes << "  |  "
              << "Aristas: " << numNodes * (numNodes - 1) << "  |  "
              << "Corridas por config: " << numRuns << "\n";
    printSeparator();

    // Construir grafo
    const json mapJson = buildSyntheticMarketGraph(numNodes);
    Graph graph;
    graph.build(mapJson);

    const std::string source = "M0";

    BellmanFord bf;

    // ── 1. Línea base serial ──────────────────────────────────────────────────
    std::cout << "\n[1/3] Midiendo SERIAL (warm-up incluido)...\n";
    // Warm-up: una corrida descartada
    {
        BellmanFord::ProfilingStats tmp;
        bf.computeSerial(graph, source, &tmp);
    }
    RunStats serialStats = runSerial(bf, graph, source, numRuns);

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "  Tiempo medio   : " << serialStats.mean_ms << " ms\n";
    std::cout << "  Min / Max      : " << serialStats.min_ms  << " / "
                                       << serialStats.max_ms  << " ms\n";
    std::cout << "  Desviación std : " << serialStats.stddev  << " ms\n";
    std::cout << "  Pasadas (last) : " << serialStats.last.passes_executed << "\n";
    std::cout << "  Tiempo serial (init+neg-cycle): "
              << serialStats.last.serial_time_ms << " ms\n";
    std::cout << "  Tiempo paralelo (inner loop)  : "
              << serialStats.last.parallel_time_ms << " ms\n";

    const double fracSerial = (serialStats.mean_ms > 0)
        ? serialStats.last.serial_time_ms / serialStats.mean_ms
        : 0.0;
    const double fracParallel = 1.0 - fracSerial;

    std::cout << "\n  ── Fracciones de tiempo (Ley de Amdahl) ──\n";
    std::cout << "  Fracción serial   S = " << std::setprecision(4)
              << fracSerial * 100.0 << "%\n";
    std::cout << "  Fracción paralela P = " << fracParallel * 100.0 << "%\n";

    // ── 2. Paralelo con distintos números de threads ──────────────────────────
#ifdef _OPENMP
    const int maxThreads = omp_get_max_threads();
    std::vector<int> threadCounts;
    for (int t = 1; t <= maxThreads; t *= 2)
        threadCounts.push_back(t);
    if (threadCounts.back() != maxThreads)
        threadCounts.push_back(maxThreads);

    std::cout << "\n[2/3] Midiendo PARALELO con " << threadCounts.size()
              << " configuraciones de threads...\n\n";

    std::cout << std::setw(10) << "Threads"
              << std::setw(14) << "Tiempo(ms)"
              << std::setw(12) << "Speedup"
              << std::setw(14) << "Eficiencia"
              << std::setw(20) << "Speedup Amdahl"
              << "\n";
    printSeparator(70);

    for (int t : threadCounts)
    {
        // Warm-up
        {
            BellmanFord::ProfilingStats tmp;
            bf.computeParallel(graph, source, &tmp, t);
        }
        RunStats ps = runParallel(bf, graph, source, numRuns, t);

        const double speedup    = serialStats.mean_ms / ps.mean_ms;
        const double efficiency = speedup / static_cast<double>(t);

        // Speedup teórico de Amdahl: 1 / (S + P/t)
        const double amdahl = 1.0 / (fracSerial + fracParallel / static_cast<double>(t));

        std::cout << std::setw(10) << t
                  << std::setw(14) << std::setprecision(3) << ps.mean_ms
                  << std::setw(12) << std::setprecision(3) << speedup
                  << std::setw(13) << std::setprecision(3) << efficiency * 100.0 << "%"
                  << std::setw(18) << std::setprecision(3) << amdahl
                  << "\n";
    }
    printSeparator(70);

    // ── 3. Resumen de Amdahl ──────────────────────────────────────────────────
    std::cout << "\n[3/3] Análisis Ley de Amdahl\n";
    std::cout << "  Fracción serial medida : " << std::setprecision(4)
              << fracSerial * 100.0 << "%\n";
    const double maxSpeedup = (fracSerial > 0) ? 1.0 / fracSerial : 999.9;
    std::cout << "  Speedup máximo teórico : " << std::setprecision(2)
              << maxSpeedup << "x  (con infinitos threads)\n";
    std::cout << "  Interpretación: aunque se agreguen N threads, el speedup\n"
              << "  nunca superará " << std::setprecision(1) << maxSpeedup << "x\n"
              << "  por la fracción serial (init + detección ciclo negativo).\n";

#else
    std::cout << "\n[2/3] OpenMP no disponible — se omite benchmark paralelo.\n";
    std::cout << "[3/3] Recompilá con -fopenmp para obtener resultados.\n";
#endif

    // ── 4. Tabla de escalabilidad con distintos tamaños de grafo ────────────────
    std::cout << "\n[4/4] Escalabilidad por tamaño de grafo (serial vs paralelo max threads)\n";
    std::cout << "      Demuestra que el speedup crece con el tamaño del problema\n\n";

    std::cout << std::setw(8)  << "Nodos"
              << std::setw(12) << "Aristas"
              << std::setw(14) << "Serial(ms)"
              << std::setw(16) << "Paralelo(ms)"
              << std::setw(12) << "Speedup"
              << std::setw(10) << "Pasadas"
              << "\n";
    printSeparator(72);

    for (int n : {50, 100, 200, 400, 600, 800})
    {
        const json   mj = buildSyntheticMarketGraph(n);
        Graph        g2;
        g2.build(mj);

        BellmanFord::ProfilingStats s2, p2;
        // warm-up
        bf.computeSerial(g2,   "M0", nullptr);
        bf.computeParallel(g2, "M0", nullptr, 0);

        // medir
        double ts = 0, tp = 0;
        const int R2 = 5;
        for (int r = 0; r < R2; ++r)
        {
            bf.computeSerial(g2,   "M0", &s2); ts += s2.total_time_ms;
            bf.computeParallel(g2, "M0", &p2); tp += p2.total_time_ms;
        }
        ts /= R2; tp /= R2;
        const double sp = (tp > 0) ? ts / tp : 1.0;

        std::cout << std::setw(8)  << n
                  << std::setw(12) << (n * (n-1))
                  << std::setw(14) << std::setprecision(2) << ts
                  << std::setw(16) << std::setprecision(2) << tp
                  << std::setw(12) << std::setprecision(2) << sp
                  << std::setw(10) << p2.passes_executed
                  << "\n";
    }
    printSeparator(72);

    printSeparator();
    std::cout << "  Benchmark completado.\n";
    printSeparator();

    return 0;
}
