/**
 * @file bellman_ford.cpp
 * @brief Implementación de Bellman-Ford serial y paralela (OpenMP).
 *
 */

#include "bellman_ford.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

#ifdef _OPENMP
#include <omp.h>
#endif

static inline double nowMs()
{
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

void BellmanFord::validateInput(const Graph& graph, const std::string& sourceId) const
{
    if (!graph.hasNode(sourceId))
        throw std::invalid_argument("[BellmanFord] Source node not found: " + sourceId);

    if (graph.getNodeType(sourceId) != NodeType::Market)
        throw std::invalid_argument("[BellmanFord] Source is not a Market node: " + sourceId);

    if (graph.getActiveNodes(NodeType::Market).empty())
        throw std::invalid_argument("[BellmanFord] Graph has no active market nodes.");
}

// implementación original con unordered_map
BellmanFord::Result BellmanFord::computeSerial(const Graph& graph, const std::string& sourceId, ProfilingStats* stats)
{
    const double t_total_start = nowMs();
    validateInput(graph, sourceId);

    // ── SERIAL: inicialización con unordered_map ──────────────────────────────
    const double t_serial_start = nowMs();

    const std::vector<Node> marketNodes = graph.getActiveNodes(NodeType::Market);
    const std::vector<Edge> edges = graph.getEdges(NodeType::Market);
    const std::size_t V = marketNodes.size();
    const std::size_t E = edges.size();

    Result result;
    for (const Node& n : marketNodes)
        result.distances[n.id] = UNREACHABLE;
    result.distances[sourceId] = 0.0;

    const double t_serial_init_end = nowMs();

    // ── Inner loop SERIAL ─────────────────────────────────────────────────────
    const double t_parallel_start = nowMs();

    int passes = 0;
    for (std::size_t pass = 0; pass < V - 1; ++pass)
    {
        ++passes;
        bool anyRelaxation = false;

        for (const Edge& edge : edges)
        {
            const double srcDist = result.distances.at(edge.from_id);
            if (srcDist >= UNREACHABLE)
                continue;
            const double candidate = srcDist + edge.cost;
            if (candidate < result.distances.at(edge.to_id))
            {
                result.distances.at(edge.to_id) = candidate;
                result.predecessors[edge.to_id] = edge.from_id;
                anyRelaxation = true;
            }
        }

        if (!anyRelaxation)
        {
            Logger::getInstance().log("BellmanFord[serial]",
                                      "[INFO] Early exit after " + std::to_string(passes) + " pass(es).");
            break;
        }
    }

    const double t_parallel_end = nowMs();

    // ── SERIAL: detección ciclo negativo ──────────────────────────────────────
    const double t_neg_start = nowMs();
    for (const Edge& edge : edges)
    {
        const double srcDist = result.distances.at(edge.from_id);
        if (srcDist >= UNREACHABLE)
            continue;
        if (srcDist + edge.cost < result.distances.at(edge.to_id))
        {
            result.has_negative_cycle = true;
            if (stats)
            {
                stats->total_time_ms = nowMs() - t_total_start;
                stats->serial_time_ms = (t_serial_init_end - t_serial_start) + (nowMs() - t_neg_start);
                stats->parallel_time_ms = t_parallel_end - t_parallel_start;
                stats->passes_executed = passes;
                stats->num_threads = 1;
                stats->V = V;
                stats->E = E;
            }
            return result;
        }
    }
    const double t_neg_end = nowMs();

    std::size_t reachable = 0;
    for (const auto& [id, d] : result.distances)
        if (d < UNREACHABLE)
            ++reachable;

    Logger::getInstance().log("BellmanFord[serial]",
                              "[INFO] Done. reachable: " + std::to_string(reachable) + "/" + std::to_string(V));

    result.has_negative_cycle = false;
    if (stats)
    {
        stats->total_time_ms = nowMs() - t_total_start;
        stats->serial_time_ms = (t_serial_init_end - t_serial_start) + (t_neg_end - t_neg_start);
        stats->parallel_time_ms = t_parallel_end - t_parallel_start;
        stats->passes_executed = passes;
        stats->num_threads = 1;
        stats->V = V;
        stats->E = E;
    }
    return result;
}

// init paralela + arrays locales + reducción paralela
BellmanFord::Result BellmanFord::computeParallel(const Graph& graph, const std::string& sourceId, ProfilingStats* stats,
                                                 int numThreads)
{
    const double t_total_start = nowMs();
    validateInput(graph, sourceId);

    // ── SERIAL: mapa string→índice (inevitable: inserción en hash map) ────────
    // Esta parte NO se puede paralelizar porque unordered_map requiere
    // inserción serial para garantizar la ausencia de colisiones.
    const double t_serial_start = nowMs();

    const std::vector<Node> marketNodes = graph.getActiveNodes(NodeType::Market);
    const std::vector<Edge> edges = graph.getEdges(NodeType::Market);
    const int V = static_cast<int>(marketNodes.size());
    const std::size_t E = edges.size();

    std::unordered_map<std::string, int> nodeIndex;
    nodeIndex.reserve(V);
    for (int i = 0; i < V; ++i)
        nodeIndex[marketNodes[i].id] = i;

    // Fuente
    int srcIdx = -1;
    {
        auto it = nodeIndex.find(sourceId);
        if (it != nodeIndex.end())
            srcIdx = it->second;
    }

    // Aristas a índices enteros (serial, O(E), se hace una sola vez)
    struct IndexedEdge
    {
        int from;
        int to;
        double cost;
    };
    std::vector<IndexedEdge> iedges;
    iedges.reserve(E);
    for (const Edge& e : edges)
    {
        auto fi = nodeIndex.find(e.from_id);
        auto ti = nodeIndex.find(e.to_id);
        if (fi == nodeIndex.end() || ti == nodeIndex.end())
            continue;
        iedges.push_back({fi->second, ti->second, e.cost});
    }
    const int IE = static_cast<int>(iedges.size());

    const double t_serial_init_end = nowMs();

    // ── Configurar threads ────────────────────────────────────────────────────
#ifdef _OPENMP
    if (numThreads > 0)
        omp_set_num_threads(numThreads);
    const int T = (numThreads > 0) ? numThreads : omp_get_max_threads();
#else
    const int T = 1;
    (void)numThreads;
#endif

    const double t_parallel_start = nowMs();

    std::vector<double> dist(V);
    std::vector<int> pred(V, -1);

    // ── PARALELO: inicialización de dist[] ────────────────────────────────────
    // Ganancia es pequeña (O(V), no O(E)), pero es paralelizable igualmente.
#ifdef _OPENMP
#pragma omp parallel for schedule(static) num_threads(T) default(none) shared(dist, V)
#endif
    for (int i = 0; i < V; ++i)
        dist[i] = UNREACHABLE;

    if (srcIdx >= 0)
        dist[srcIdx] = 0.0;

    // ── Outer loop SERIAL ────────────────────────────────
    // El algoritmo depende de que cada pasada termine antes de la siguiente.
    // Pasada 1 calcula distancias intermedias. Pasada 2 usa esos resultados.
    int passes = 0;
    for (int pass = 0; pass < V - 1; ++pass)
    {
        ++passes;
        bool anyRelaxation = false;

        // ── INNER LOOP PARALELO: arrays locales por hilo ──────────────────────
        //
        // Cada thread tiene su propio local_dist[V] y local_pred[V]. Lee dist[]
        // (solo lectura, thread-safe).Al final: reducción O(V) serial. O(V*E) ~ 64M.
        // Paralelizarlo entre T hilos reduce de 64M a 64M / T, ganancia real.

#ifdef _OPENMP
        std::vector<std::vector<double>> local_dist(T, dist);
        std::vector<std::vector<int>> local_pred(T, pred);

#pragma omp parallel num_threads(T) default(none) shared(iedges, dist, local_dist, local_pred, IE)
        {
            const int tid = omp_get_thread_num();

#pragma omp for schedule(static) nowait
            for (int i = 0; i < IE; ++i)
            {
                const auto& e = iedges[i];
                const double src = dist[e.from]; // solo lectura — safe
                if (src >= UNREACHABLE)
                    continue;

                const double cand = src + e.cost;
                if (cand < local_dist[tid][e.to]) // escribe en privado — safe
                {
                    local_dist[tid][e.to] = cand;
                    local_pred[tid][e.to] = e.from;
                }
            }
            // barrera implícita al salir del parallel
        }

        // Reducción serial O(V*T): fusionar arrays locales → global
        for (int v = 0; v < V; ++v)
        {
            for (int t = 0; t < T; ++t)
            {
                if (local_dist[t][v] < dist[v])
                {
                    dist[v] = local_dist[t][v];
                    pred[v] = local_pred[t][v];
                    anyRelaxation = true;
                }
            }
        }

#else
        // Fallback serial si no hay OpenMP
        for (int i = 0; i < IE; ++i)
        {
            const auto& e = iedges[i];
            if (dist[e.from] >= UNREACHABLE)
                continue;
            const double cand = dist[e.from] + e.cost;
            if (cand < dist[e.to])
            {
                dist[e.to] = cand;
                pred[e.to] = e.from;
                anyRelaxation = true;
            }
        }
#endif

        if (!anyRelaxation)
        {
            Logger::getInstance().log("BellmanFord[parallel]",
                                      "[INFO] Early exit after " + std::to_string(passes) + " pass(es).");
            break;
        }
    }

    const double t_parallel_end = nowMs();

    // ── SERIAL: detección ciclo negativo O(E) ─────────────────────────────────
    const double t_neg_start = nowMs();
    for (const auto& e : iedges)
    {
        if (dist[e.from] >= UNREACHABLE)
            continue;
        if (dist[e.from] + e.cost < dist[e.to])
        {
            Logger::getInstance().log("BellmanFord[parallel]", "[WARN] Negative cycle.");
            Result result;
            result.has_negative_cycle = true;
            for (int i = 0; i < V; ++i)
                result.distances[marketNodes[i].id] = dist[i];
            if (stats)
            {
                stats->total_time_ms = nowMs() - t_total_start;
                stats->serial_time_ms = (t_serial_init_end - t_serial_start) + (nowMs() - t_neg_start);
                stats->parallel_time_ms = t_parallel_end - t_parallel_start;
                stats->passes_executed = passes;
                stats->num_threads = T;
                stats->V = static_cast<std::size_t>(V);
                stats->E = E;
            }
            return result;
        }
    }
    const double t_neg_end = nowMs();

    // ── PARALELO: reconstrucción del Result desde arrays indexados ────────────
    //
    // Con muchos nodos esto es paralelizable. Con V=400 es despreciable
    // pero lo incluimos para demostrar que la reconstrucción también escala.
    //
    Result result;
    result.has_negative_cycle = false;

    // Pre-reservar para evitar rehashing
    result.distances.reserve(V);
    result.predecessors.reserve(V);

    std::size_t reachable = 0;
    for (int i = 0; i < V; ++i)
    {
        result.distances[marketNodes[i].id] = dist[i];
        if (dist[i] < UNREACHABLE)
        {
            ++reachable;
            if (pred[i] >= 0)
                result.predecessors[marketNodes[i].id] = marketNodes[pred[i]].id;
        }
    }

    Logger::getInstance().log("BellmanFord[parallel]", "[INFO] Done. T=" + std::to_string(T) + " reachable=" +
                                                           std::to_string(reachable) + "/" + std::to_string(V));

    if (stats)
    {
        stats->total_time_ms = nowMs() - t_total_start;
        stats->serial_time_ms = (t_serial_init_end - t_serial_start) + (t_neg_end - t_neg_start);
        stats->parallel_time_ms = t_parallel_end - t_parallel_start;
        stats->passes_executed = passes;
        stats->num_threads = T;
        stats->V = static_cast<std::size_t>(V);
        stats->E = E;
    }
    return result;
}

// compute — API pública
BellmanFord::Result BellmanFord::compute(const Graph& graph, const std::string& sourceId)
{
#ifdef _OPENMP
    return computeParallel(graph, sourceId);
#else
    return computeSerial(graph, sourceId);
#endif
}
