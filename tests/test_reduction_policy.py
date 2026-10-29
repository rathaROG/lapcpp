"""Objective regressions for tied and congested row-reduction initialization.

Separable binary-fraction costs give independent exact optima at every matching
cardinality, including rejection. No particular tied assignment or timing is
required.
"""
import math

import numpy as np
import pytest


@pytest.mark.parametrize("dtype", [np.float32, np.float64])
@pytest.mark.parametrize("transpose", [False, True])
@pytest.mark.parametrize("tied", [False, True])
def test_separable_reduction_chains(solver, dtype, transpose, tied):
    for rows, cols in ((7, 9), (128, 129), (129, 145), (17, 17)):
        row_cost = (np.arange(rows) % 7 - 3) / 16
        col_cost = np.arange(cols) / 8
        if tied:
            col_cost = (np.arange(cols) // 4) / 8
        if rows < cols:
            col_cost[-1] = np.inf
        cost = (row_cost[:, None] + col_cost[None, :]).astype(dtype)
        if transpose:
            cost = cost.T
        original = cost.copy()
        cost.setflags(write=False)
        sorted_rows, sorted_cols = sorted(row_cost), sorted(col_cost)

        for limit in (np.inf, .5, 0., -.5):
            half = float(dtype(limit / 2))
            counts = range(rows + 1) if np.isfinite(limit) else (rows,)
            optimum = min(
                math.fsum(sorted_rows[:k]) + math.fsum(sorted_cols[:k])
                + ((rows + cols - 2 * k) * half if np.isfinite(limit) else 0.)
                for k in counts
            )

            def check(result):
                total, pairs = result
                assert pairs.dtype == np.int64
                assert pairs.ndim == 2 and pairs.shape[1] == 2
                ii, jj = pairs.T
                assert np.all((ii >= 0) & (ii < cost.shape[0]))
                assert np.all((jj >= 0) & (jj < cost.shape[1]))
                assert len(set(ii)) == len(ii) == len(set(jj))
                assert np.all(ii[1:] > ii[:-1])
                assert np.isfinite(cost[ii, jj]).all()
                selected = math.fsum(float(cost[i, j]) for i, j in pairs)
                assert total == selected
                if np.isfinite(limit):
                    assert selected + (rows + cols - 2 * len(pairs)) * half == optimum
                else:
                    assert len(pairs) == rows
                    assert selected == optimum

            options = dict(extend_cost=True, cost_limit=limit, precision="native",
                           output="pairs", return_cost=True)
            check(solver.lapjv(cost, **options))
            batch = np.stack((cost, cost))
            totals, pairs = solver.lapjv_batch(batch, n_threads=2, **options)
            for result in zip(totals, pairs):
                check(result)
            np.testing.assert_array_equal(batch, np.stack((original, original)))
        np.testing.assert_array_equal(cost, original)
