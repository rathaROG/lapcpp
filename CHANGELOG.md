# Changelog

## Unreleased

- Use float64 potentials, distances and reduced-cost arithmetic for float32
  rectangular and finite-limit assignments. Matrix storage stays float32;
  required square assignments already used float64 working arithmetic.
- Preserve cost conversion, represented half-limit rounding, original-input
  totals, APIs and initialization policies. Float64 working arithmetic improves
  numerical robustness without guaranteeing exactness for arbitrary cost ranges.
- Add exact-objective regressions for small differences beside large costs,
  including augmenting search and represented finite-limit rounding.

## 1.1.0 - 2026-10-26

- Bound row-reduction initialization on large rectangular required and finite-limit
  assignments to reduce congestion overhead.
- Use exact inline finite checks and stabilize minimum-scan code placement in
  native MSVC x64 builds.
- Preserve public APIs, precision options, and rejection semantics. Performance
  changes depend on the workload.

## 1.0.0 - 2026-10-23

First stable release.

### Package and API

- Standalone `lapcpp` distribution and import namespace for Python 3.9+.
- `lapjv` and `lapjv_batch` derived from the LAPX 0.11.1 `lapjvh` solver.
- Future solver development is led by lapcpp (C++) and laprust (Rust), with
  LAPX incorporating their fixes and improvements.
- Native rectangular assignment and implicit finite-limit rejection.
- Float32/float64 working costs, index or pair output, and reusable batch workers.
- Type annotations (`py.typed`).

### Solver and performance

- Rectangular and finite-limit search: reuse the root row's reduced costs for the
  first relaxation instead of resetting distances and predecessors separately.
- Parallel batches claim consecutive tiny problems in chunks (at most 64 problems
  and about 4096 costs per claim), reducing contention and false sharing.
- Batch results: convert counts once, and build per-problem views by row iteration
  when every problem is known to be fully matched (batches of 8 or more).
- Windows builds use MSVC `/std:c++17` instead of `/std:c++latest`, which was
  10-33% slower on medium rectangles and 256x256 batches with MSVC 14.44.
- Stricter native checks: a path walk-back that never reaches its root and a final
  matching with `y[x[i]] != i` report an internal error instead of returning.

### Testing and CI

- Numerical/API tests, historical LAPX regression fixtures, edge/ownership checks,
  and exhaustive references.
- SciPy-free ARM numerical tests, complete empty-result and batch-size boundaries.
- Native allocation/worker failure injection, adapter exception/recovery checks,
  and native sanitizer jobs in separate PR/manual workflows.
- Required cross-package concurrency and isolated editable-install tests in CI.
- Full standalone wheel testing and older NumPy runtime compatibility jobs.

### Benchmarks

- Five-candidate benchmarks: dedicated SciPy, lapcpp, laprust and pair-output LAPX
  JVX/JVS functions, following LAPX v0.11.1 settings and uniform input generation.
- Single/tracking use the same 39 matrix shapes, including 4000x3000 and other large
  rectangles; batch uses 65 tapered workloads with medium rectangles and small-batch
  overhead cases. Every mode includes tiny and small rectangular workloads.
- Tracking always solves first and post-filters every candidate; no solver cost_limit
  or rejection expansion, with the original unmatched-list construction and
  threshold sweep.
- Calls per sample are calibrated (at least 10 ms) and each solver is warmed right
  before its own sample; sub-millisecond cases use 7 rounds. `--reference-timing`
  keeps the pinned LAPX timing for comparison.
- Benchmark verdicts and ranked test-count/total-time summaries in text and JSON.
