# Changelog

## 1.2.1 - 2026-11-08

- Require LAPX 0.13.0 or newer for all package benchmarks.
- Record and check the installed LAPX version.
- Include laprust in the benchmark installation extra.
- Refresh installation, development and paper links.
- Distinguish the package benchmarks from the paper's reproducible experiment
  suite.
- Add package, license, paper and total/monthly download badges.
- Add documentation and support links to the package metadata.

## 1.2.0 - 2026-10-28

- Use a four-reduction budget per processed free row in rectangular and
  finite-limit assignments.
- Prefer a free column among equal reduced-cost minima in rectangular and
  finite-limit assignments.
- Stop further price reductions after a sustained displacement chain in required
  rectangular assignments. Finite-limit assignments retain their reduction
  chains. This avoids the optional regressions seen with a window in both modes.
- Preserve APIs, precision conversion, rejection semantics and the required-square
  solver. Performance remains workload dependent. See the
  [initialization policy and tradeoffs](benchmarks/required-window-policy.md).

## 1.1.1 - 2026-10-28

- Use float64 potentials, distances and reduced-cost arithmetic for float32
  rectangular and finite-limit assignments. Matrix storage stays float32.
  Required square assignments already used float64 working arithmetic.
- Preserve cost conversion, represented half-limit rounding, original-input
  totals, APIs and initialization policies. Float64 working arithmetic improves
  numerical robustness without guaranteeing exactness for arbitrary cost ranges.
- Add exact-objective regressions for small differences beside large costs,
  including augmenting search and represented finite-limit rounding.

## 1.1.0 - 2026-10-26

- Bound row-reduction initialization on large rectangular required and finite-limit
  assignments to reduce congestion overhead.
- Use exact inline finite checks in native MSVC x64 builds.
- Stabilize minimum-scan code placement in native MSVC x64 builds.
- Preserve public APIs, precision options, and rejection semantics. Performance
  changes depend on the workload.

## 1.0.0 - 2026-10-23

This is the first stable release.

### Package and API

- Provide a standalone `lapcpp` distribution and import namespace for Python 3.9+.
- Derive `lapjv` and `lapjv_batch` from the LAPX 0.11.1 `lapjvh` solver.
- lapcpp (C++) and laprust (Rust) lead future solver development.
  LAPX incorporates their fixes and improvements.
- Support native rectangular assignment and implicit finite-limit rejection.
- Support float32/float64 working costs and index or pair output.
- Reuse batch workers.
- Provide type annotations (`py.typed`).

### Solver and performance

- Reuse the root row's reduced costs for the first relaxation in rectangular
  and finite-limit search. This replaces separate resets of distances and predecessors.
- Parallel batches claim consecutive tiny problems in chunks. Each claim
  includes at most 64 problems and about 4096 costs. This reduces contention and
  false sharing.
- Convert batch result counts once. For batches of 8 or more, build per-problem
  views by row iteration when every problem is known to be fully matched.
- Windows builds use MSVC `/std:c++17` instead of `/std:c++latest`, which was
  10-33% slower on medium rectangles and 256x256 batches with MSVC 14.44.
- If a path walk-back never reaches its root, report an internal error instead
  of returning a result.
- If a final matching has `y[x[i]] != i`, report an internal error instead of
  returning a result.

### Testing and CI

- Add numerical/API tests, historical LAPX regression fixtures, edge/ownership
  checks, and exhaustive references.
- Add SciPy-free ARM numerical tests.
- Cover all empty-result and batch-size boundaries.
- Inject native allocation/worker failures in tests.
- Check adapter exceptions and recovery.
- Add native sanitizer jobs in separate PR/manual workflows.
- Require cross-package concurrency and isolated editable-install tests in CI.
- Run full tests on standalone wheels.
- Add compatibility jobs for older NumPy runtimes.

### Benchmarks

- Compare five candidates through dedicated SciPy, lapcpp, laprust and pair-output
  LAPX JVX/JVS functions. Use LAPX v0.11.1 settings and uniform input generation.
- Use the same 39 matrix shapes for single/tracking benchmarks, including
  4000x3000 and other large rectangles.
- Use 65 tapered batch workloads with medium rectangles and small-batch
  overhead cases. Every mode includes tiny and small rectangular workloads.
- In tracking benchmarks, solve first and post-filter every candidate.
  Do not pass a solver cost_limit or expand the matrix for rejection.
  Preserve the original unmatched-list construction and threshold sweep.
- Calibrate calls per sample to target at least 10 ms.
  Warm each solver immediately before its own sample. Use 7 rounds for
  sub-millisecond cases. `--reference-timing` keeps the pinned LAPX timing for
  comparison.
- Report benchmark verdicts and ranked test-count/total-time summaries in text
  and JSON.
