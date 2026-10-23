"""Run after installing lapcpp."""
import numpy as np
import lapcpp

cost = np.array([[3., 1.], [1., 4.]])
total, rows, cols = lapcpp.lapjv(cost)
print("single:", total, rows, cols)
totals, rows, cols = lapcpp.lapjv_batch(np.stack([cost, cost]), n_threads=2)
print("batch:", totals, rows, cols)
cost = np.array([[.1, .8, .9], [.9, .7, .8]], dtype=np.float32)
print("rejection:", lapcpp.lapjv(cost, cost_limit=.5))
