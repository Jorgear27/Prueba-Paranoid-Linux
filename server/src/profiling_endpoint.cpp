/**
 * @file profiling_endpoint.cpp
 * @brief Handler HTTP para GET /profiling-bf
 *
 */

#include "bellman_ford.hpp"
#include "graph.hpp"

#include <nlohmann/json.hpp>
#include <string>

#ifdef _OPENMP
#include <omp.h>
#endif

using json = nlohmann::json;

/**
 * @brief Ejecuta ambas variantes de BF sobre el grafo y devuelve JSON de profiling.
 *
 * @param graph      Snapshot inmutable del grafo.
 * @param sourceId   Nodo fuente (debe ser Market).
 * @param threads    Threads para la variante paralela (0 = máximo del sistema).
 * @param statusCode Se escribe el código HTTP de respuesta.
 * @return JSON string con los resultados.
 */
std::string handleProfilingBf(const Graph& graph, const std::string& sourceId, int threads, int& statusCode)
{
    BellmanFord bf;
    BellmanFord::ProfilingStats serialStats, parallelStats;

    // ── Warm-up: una corrida descartada para poblar cachés ────────────────────
    try
    {
        bf.computeSerial(graph, sourceId, nullptr);
        bf.computeParallel(graph, sourceId, nullptr, threads);
    }
    catch (const std::exception& e)
    {
        statusCode = 400;
        return json{{"status", "error"}, {"message", e.what()}}.dump();
    }

    // ── Corridas de medición ──────────────────────────────────────────────────
    constexpr int RUNS = 5;
    double sumSerial = 0.0;
    double sumParallel = 0.0;

    for (int r = 0; r < RUNS; ++r)
    {
        BellmanFord::ProfilingStats s, p;
        bf.computeSerial(graph, sourceId, &s);
        bf.computeParallel(graph, sourceId, &p, threads);
        sumSerial += s.total_time_ms;
        sumParallel += p.total_time_ms;
        if (r == RUNS - 1)
        {
            serialStats = s;
            parallelStats = p;
        }
    }

    const double meanSerial = sumSerial / RUNS;
    const double meanParallel = sumParallel / RUNS;

    const double speedup = (meanParallel > 0) ? meanSerial / meanParallel : 1.0;
    const double efficiency = speedup / static_cast<double>(parallelStats.num_threads);

    // Fracción serial (Amdahl): de cuánto del tiempo serial es parte no paralelizable
    const double fracSerial = (meanSerial > 0) ? serialStats.serial_time_ms / meanSerial : 0.0;
    const double amdahlMax = (fracSerial > 0) ? 1.0 / fracSerial : 999.0;

#ifdef _OPENMP
    const int maxSystemThreads = omp_get_max_threads();
#else
    const int maxSystemThreads = 1;
#endif

    json resp;
    resp["graph"]["V"] = static_cast<int>(serialStats.V);
    resp["graph"]["E"] = static_cast<int>(serialStats.E);
    resp["graph"]["source_id"] = sourceId;

    resp["serial"]["total_ms"] = meanSerial;
    resp["serial"]["serial_fraction_ms"] = serialStats.serial_time_ms;
    resp["serial"]["parallel_fraction_ms"] = serialStats.parallel_time_ms;
    resp["serial"]["passes"] = serialStats.passes_executed;
    resp["serial"]["threads"] = 1;

    resp["parallel"]["total_ms"] = meanParallel;
    resp["parallel"]["serial_fraction_ms"] = parallelStats.serial_time_ms;
    resp["parallel"]["parallel_fraction_ms"] = parallelStats.parallel_time_ms;
    resp["parallel"]["passes"] = parallelStats.passes_executed;
    resp["parallel"]["threads"] = parallelStats.num_threads;

    resp["speedup"] = speedup;
    resp["efficiency_pct"] = efficiency * 100.0;
    resp["serial_fraction_pct"] = fracSerial * 100.0;
    resp["parallel_fraction_pct"] = (1.0 - fracSerial) * 100.0;
    resp["amdahl_max_speedup"] = amdahlMax;
    resp["system_max_threads"] = maxSystemThreads;
    resp["openmp_available"] =
#ifdef _OPENMP
        true;
#else
        false;
#endif
    resp["runs_averaged"] = RUNS;

    statusCode = 200;
    return resp.dump();
}
