#!/bin/bash
# =============================================================================
# run_profiling.sh — Profiling instrumental: Serial vs OpenMP
#
# Ejecuta tres tipos de análisis:
#   1. Benchmark propio (ProfilingStats): serial vs paralelo, Ley de Amdahl
#   2. perf stat: contadores de hardware (ciclos, cache misses, IPC)
#   3. Valgrind callgrind: call graph y tiempo por función
#
# Uso (desde el directorio build/):
#   bash ../profiling/run_profiling.sh [num_nodes] [num_runs]
#   bash ../profiling/run_profiling.sh 400 10
# =============================================================================

set -euo pipefail

NODES=${1:-300}
RUNS=${2:-8}
BENCH="./benchmark_bf"
OUT_DIR="../profiling/results"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)

mkdir -p "$OUT_DIR"

if [ ! -f "$BENCH" ]; then
    echo "ERROR: $BENCH no existe. Compilar primero:"
    echo "  cd build && make benchmark_bf"
    exit 1
fi

echo "============================================================"
echo "  PROFILING BELLMAN-FORD: Serial vs OpenMP"
echo "  Nodos: $NODES | Corridas: $RUNS | $(date)"
echo "============================================================"

# ─────────────────────────────────────────────────────────────────
# 1. Benchmark propio — ProfilingStats (siempre disponible)
# ─────────────────────────────────────────────────────────────────
echo ""
echo "[PASO 1/3] Benchmark propio (ProfilingStats + Ley de Amdahl)"
echo "─────────────────────────────────────────────────────────────"

echo ">>> ANTES (serial, 1 thread forzado):"
OMP_NUM_THREADS=1 $BENCH $NODES $RUNS | tee "$OUT_DIR/ANTES_serial_${TIMESTAMP}.txt"

echo ""
echo ">>> DESPUES (paralelo, max threads):"
$BENCH $NODES $RUNS | tee "$OUT_DIR/DESPUES_paralelo_${TIMESTAMP}.txt"

# ─────────────────────────────────────────────────────────────────
# 2. perf stat — contadores de hardware
# ─────────────────────────────────────────────────────────────────
echo ""
echo "[PASO 2/3] perf stat — contadores de hardware"
echo "─────────────────────────────────────────────────────────────"

if command -v perf &>/dev/null; then
    PERF_EVENTS="cycles,instructions,cache-misses,cache-references,branch-misses,context-switches"

    echo ">>> ANTES (serial):"
    OMP_NUM_THREADS=1 perf stat -e $PERF_EVENTS \
        $BENCH $NODES $RUNS 2>&1 | tee "$OUT_DIR/perf_ANTES_${TIMESTAMP}.txt"

    echo ""
    echo ">>> DESPUES (paralelo):"
    perf stat -e $PERF_EVENTS \
        $BENCH $NODES $RUNS 2>&1 | tee "$OUT_DIR/perf_DESPUES_${TIMESTAMP}.txt"

    # Extraer IPC (instructions per cycle) de los dos archivos
    echo ""
    echo ">>> Comparación IPC:"
    echo -n "Serial   IPC: "
    grep "insn per cycle" "$OUT_DIR/perf_ANTES_${TIMESTAMP}.txt"   | awk '{print $1}' || echo "N/A"
    echo -n "Paralelo IPC: "
    grep "insn per cycle" "$OUT_DIR/perf_DESPUES_${TIMESTAMP}.txt" | awk '{print $1}' || echo "N/A"

else
    echo "perf no disponible. Usando /usr/bin/time -v como alternativa."
    echo "(Instalar con: sudo apt install linux-perf  o  sudo apt install perf-tools-unstable)"
    echo ""

    echo ">>> ANTES (serial):"
    { OMP_NUM_THREADS=1 /usr/bin/time -v $BENCH $NODES $RUNS ; } 2>&1 \
        | tee "$OUT_DIR/ANTES_time_${TIMESTAMP}.txt"

    echo ""
    echo ">>> DESPUES (paralelo):"
    { /usr/bin/time -v $BENCH $NODES $RUNS ; } 2>&1 \
        | tee "$OUT_DIR/DESPUES_time_${TIMESTAMP}.txt"
fi

# ─────────────────────────────────────────────────────────────────
# 3. Valgrind callgrind — call graph detallado
# ─────────────────────────────────────────────────────────────────
echo ""
echo "[PASO 3/3] Valgrind callgrind — call graph"
echo "─────────────────────────────────────────────────────────────"

if command -v valgrind &>/dev/null; then
    # Callgrind es ~20x más lento que la ejecución nativa.
    # Usamos un grafo pequeño para que termine en tiempo razonable.
    CGNODES=60

    echo "Corriendo con $CGNODES nodos (reducido para callgrind)..."

    echo ">>> ANTES (serial):"
    OMP_NUM_THREADS=1 valgrind \
        --tool=callgrind \
        --callgrind-out-file="$OUT_DIR/callgrind_ANTES_${TIMESTAMP}.out" \
        --collect-systime=yes \
        --simulate-cache=yes \
        $BENCH $CGNODES 2 2>&1 | grep -E "I refs|Ir|==.*==" | tail -8

    echo ""
    echo ">>> DESPUES (paralelo):"
    valgrind \
        --tool=callgrind \
        --callgrind-out-file="$OUT_DIR/callgrind_DESPUES_${TIMESTAMP}.out" \
        --collect-systime=yes \
        --simulate-cache=yes \
        $BENCH $CGNODES 2 2>&1 | grep -E "I refs|Ir|==.*==" | tail -8

    echo ""
    echo "Archivos callgrind generados:"
    echo "  $OUT_DIR/callgrind_ANTES_${TIMESTAMP}.out"
    echo "  $OUT_DIR/callgrind_DESPUES_${TIMESTAMP}.out"
    echo ""
    echo "Para analizar en texto:"
    echo "  callgrind_annotate $OUT_DIR/callgrind_ANTES_${TIMESTAMP}.out   | head -60"
    echo "  callgrind_annotate $OUT_DIR/callgrind_DESPUES_${TIMESTAMP}.out | head -60"
    echo ""
    echo "Para visualizar con GUI:"
    echo "  kcachegrind $OUT_DIR/callgrind_ANTES_${TIMESTAMP}.out"
else
    echo "Valgrind no disponible. Instalar con: sudo apt install valgrind"
    echo "Alternativa: usar gprof (requiere recompilar con -pg)."
    echo ""
    echo "Para usar gprof:"
    echo "  1. En CMakeLists.txt agregar:  set(CMAKE_CXX_FLAGS \"\${CMAKE_CXX_FLAGS} -pg\")"
    echo "  2. Recompilar y ejecutar el benchmark"
    echo "  3. gprof ./benchmark_bf gmon.out | head -40"
fi

# ─────────────────────────────────────────────────────────────────
# 4. Resumen final
# ─────────────────────────────────────────────────────────────────
echo ""
echo "============================================================"
echo "  Resultados guardados en: $OUT_DIR/"
echo ""
ls -1 "$OUT_DIR/"*"${TIMESTAMP}"* 2>/dev/null | sed 's/^/  /' || true
echo "============================================================"
