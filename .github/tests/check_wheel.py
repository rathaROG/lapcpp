"""Run from outside the repository package directory after installing a wheel."""
from pathlib import Path
import importlib.metadata
import numpy as np
import lapcpp

root = Path(__file__).resolve().parents[2]
installed = Path(lapcpp.__file__).resolve()
assert root / "lapcpp" not in installed.parents, installed
assert importlib.metadata.version("lapcpp") == lapcpp.__version__
assert lapcpp.__all__ == ["lapjv", "lapjv_batch"]
a = np.array([[.1, 5., 2.], [3., .2, 4.]], dtype=np.float32)
total, rows, cols = lapcpp.lapjv(a, cost_limit=.5)
assert rows.tolist() == cols.tolist() == [0, 1]
np.testing.assert_allclose(total, .3)
totals, batch_rows, batch_cols = lapcpp.lapjv_batch(np.stack([a, a]), cost_limit=.5, n_threads=2)
np.testing.assert_allclose(totals, [.3, .3])
assert all(r.tolist() == c.tolist() == [0, 1] for r, c in zip(batch_rows, batch_cols))
assert (installed.parent / "py.typed").is_file()
print("Verified", installed, lapcpp.__version__)
