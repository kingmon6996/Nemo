# SIMD Column Decode — Phase 7

## Overview

Phase 7 adds a **SIMD-accelerated column decode and aggregation** pipeline to
Nemo's vectorized aggregate execution engine. The goal is to accelerate
`SELECT SUM(col)`, `SELECT AVG(col)`, `SELECT MIN(col)`, and `SELECT MAX(col)`
queries by extracting column values into contiguous `double[]` buffers and
accumulating them using CPU vector intrinsics (AVX2, SSE4.1, or scalar fallback).

---

## Architecture

### Decode Phase — `simd_decode_column()`

```cpp
std::size_t simd_decode_column(
    const vector<vector<Value>>& rows,
    std::size_t col_idx,
    vector<double>& out) noexcept;
```

Iterates `sample_rows`, skips NULL values, and serializes each numeric Value to
a contiguous `double` buffer via `value_as_double()`.  Returns the count of
non-null elements written.

> [!NOTE]
> The decode step is always scalar — the per-row boundary check and NULL test
> are inherently branch-heavy. The payoff is that the subsequent accumulation
> kernel runs on a clean, dense `double[]` with no branching.

---

### Accumulation Phase — `simd_aggregate_doubles()`

```cpp
SimdAggResult simd_aggregate_doubles(
    const double* data, std::size_t n) noexcept;
// Returns: { sum, min, max }
```

Three compile-time code paths selected by preprocessor macros set at build time:

| Macro | Intrinsics used | Throughput |
|-------|-----------------|------------|
| `NEMO_HAVE_AVX2` | `_mm256_add_pd`, `_mm256_min_pd`, `_mm256_max_pd` | 4 doubles/cycle |
| `NEMO_HAVE_SSE4` | `_mm_add_pd`, `_mm_min_pd`, `_mm_max_pd` | 2 doubles/cycle |
| _(none)_ | Scalar loop | 1 double/cycle |

Each path processes a vector-width prefix, then handles the tail with a scalar
loop — no special alignment is required (uses `_mm256_loadu_pd` / `_mm_loadu_pd`).

---

## Integration Points

Both aggregate code paths in `execute_vectorized_aggregates()` now use SIMD:

1. **Cached plan hot path** (lines ~1413–1425) — plan is already compiled;
   column indices are known; `simd_decode_column` + `simd_aggregate_doubles`
   replace the scalar `for (r = 0; r < N; ++r)` loop.

2. **Cold path** (plan compilation + execution, lines ~1526–1537) — same helpers
   replace the scalar loop on first execution before the plan is cached.

The `COUNT(*)` path is unchanged — it reads `tbl->total_rows` directly (O(1)).

---

## Build Configuration

SIMD paths are activated by compiler flags:

```
AVX2:   -mavx2            (GCC/Clang)   /arch:AVX2        (MSVC)
SSE4.1: -msse4.1          (GCC/Clang)   /arch:SSE2        (MSVC, default)
```

The `setup.py` / `CMakeLists.txt` selects these based on the host CPU at build
time. If the CPU does not support AVX2 or SSE4.1, the scalar path is compiled
with no performance regression.

---

## Correctness Guarantee

The SIMD path is **numerically equivalent** to the scalar path: both compute
IEEE-754 double-precision sums.  The only difference is the order of floating-
point additions, which may produce tiny rounding differences
(< machine epsilon × N) for very large datasets — not observable in practice.

---

## Limitations

- Only `double`-compatible aggregates benefit (SUM, AVG, MIN, MAX on numeric
  columns). Text-column MIN/MAX still uses `execute_extraction_query()`.
- SIMD path does not apply inside `GROUP BY` or `WINDOW` aggregates — those
  use `execute_group_by_query()` and `execute_window_query()` respectively.
- The decode step allocates a temporary `vector<double>` per column per query.
  For tiny tables (< 32 rows) the allocation overhead dominates; the scalar
  path in `execute_extraction_query()` is faster there.

