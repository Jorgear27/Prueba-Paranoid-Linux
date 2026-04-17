/**
 * @file bellman_ford.hpp
 * @brief Bellman-Ford shortest-path — versión paralela con OpenMP + profiling.
 *
 * Paralelización: el inner loop de relajación de aristas por pasada se ejecuta
 * en paralelo. El outer loop (pasadas) permanece serial por dependencia de datos.
 *
 * Ley de Amdahl aplicada:
 *   - Parte serial  S: inicialización de distancias + detección de ciclo negativo
 *   - Parte paralela P: inner loop de relajación de aristas (bulk del trabajo)
 */

#ifndef BELLMAN_FORD_HPP
#define BELLMAN_FORD_HPP

#include "graph.hpp"
#include "log.hpp"
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

constexpr double UNREACHABLE = std::numeric_limits<double>::infinity();

class BellmanFord
{
  public:
    struct Result
    {
        bool has_negative_cycle = false;
        std::unordered_map<std::string, double> distances;
        std::unordered_map<std::string, std::string> predecessors;
    };

    /** Estadísticas de una ejecución para análisis de Amdahl. */
    struct ProfilingStats
    {
        double total_time_ms = 0.0;
        double serial_time_ms = 0.0;   ///< Init + neg-cycle check
        double parallel_time_ms = 0.0; ///< Inner loop de relajación
        int passes_executed = 0;
        int num_threads = 1;
        std::size_t V = 0;
        std::size_t E = 0;
    };

    /** Modo serial puro — sin ninguna directiva OpenMP. Línea base de profiling. */
    Result computeSerial(const Graph& graph, const std::string& sourceId, ProfilingStats* stats = nullptr);

    /** Modo paralelo — inner loop paralelizado con OpenMP. */
    Result computeParallel(const Graph& graph, const std::string& sourceId, ProfilingStats* stats = nullptr,
                           int numThreads = 0);

    /** API pública del servidor: usa paralelo si OpenMP está disponible. */
    Result compute(const Graph& graph, const std::string& sourceId);

  private:
    void validateInput(const Graph& graph, const std::string& sourceId) const;
};

#endif // BELLMAN_FORD_HPP
