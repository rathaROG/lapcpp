"""Public API, independence, and installed typing checks."""
import doctest
import importlib
import inspect
import subprocess
import sys
from pathlib import Path

import lapcpp


def test_only_two_public_solvers():
    assert lapcpp.__all__ == ["lapjv", "lapjv_batch"]
    assert [name for name in dir(lapcpp) if not name.startswith("_")] == ["lapjv", "lapjv_batch"]
    assert lapcpp.lapjv.__module__ == "lapcpp._lapjv"
    assert list(inspect.signature(lapcpp.lapjv).parameters) == ["cost", "extend_cost", "cost_limit", "return_cost", "precision", "output"]
    assert list(inspect.signature(lapcpp.lapjv_batch).parameters) == ["costs", "extend_cost", "cost_limit", "return_cost", "n_threads", "precision", "output"]


def test_installed_typing():
    directory = Path(lapcpp.__file__).parent
    assert (directory / "py.typed").is_file()
    assert (directory / "_lapjv.pyi").is_file()
    assert (directory / "__init__.pyi").is_file()


def test_documented_examples():
    assert doctest.testmod(importlib.import_module("lapcpp._lapjv")).failed == 0


def test_no_other_solver_package_required():
    code = """
import importlib.abc
import sys
class BlockOtherPackages(importlib.abc.MetaPathFinder):
    def find_spec(self, fullname, path=None, target=None):
        if fullname.split('.')[0] in ('lap', 'laprust'):
            raise AssertionError('Unexpected dependency: ' + fullname)
sys.meta_path.insert(0, BlockOtherPackages())
import lapcpp
assert lapcpp.lapjv([[2., 1.], [1., 2.]])[0] == 2.
assert lapcpp.lapjv_batch([[[2., 1.], [1., 2.]]])[0].tolist() == [2.]
"""
    subprocess.run([sys.executable, "-c", code], check=True)
