# Development

Use Python 3.9+ and a C++17 compiler. Use the compiler for your operating system:

- On Windows, use MSVC Build Tools.
- On Linux, use GCC or Clang.
- On macOS, use Xcode Command Line Tools.

Cross-package tests and benchmarks also use `laprust`. Building `laprust` from
source requires Rust 1.83+ and Cargo. Those CI jobs use stable Rust.

Windows builds use MSVC's `/std:c++17` language mode. `/std:c++latest` generated
measurably slower rectangular code with MSVC 14.44.
Linux and macOS builds use `-std=c++17`.

```sh
python -m venv .venv
# Activate .venv using the command appropriate for your shell.
python -m pip install -e ".[dev]"
cd tests
python -m pytest -q -m "not cross_package"
cd ..
python -m pytest -q .github/tests -m "not editable"
python -m pyright tests/typing/return_types.py
python -m build
python .github/tests/check_dist.py
```

`python -m build` builds the wheel from the source archive by default.
To verify an installed wheel:

1. Uninstall any editable installation.
2. Install the wheel in a fresh environment.
3. Run `python .github/tests/check_wheel.py`.

Run the solver tests from the `tests` directory to avoid importing the source
package accidentally.

The Cython wrapper is in `src/_lapjv`. Git ignores generated `_lapjv.cpp`,
binaries, build directories and benchmark output. When you change native source,
preserve the source{d}/lapjv and LAPX license notices.

Build controls use the `LAPCPP_` prefix:

- `LAPCPP_BASEOPTS=0` disables added base optimizations and LTO.
- `LAPCPP_LTO=0` disables LTO, including inherited compiler defaults.
- `LAPCPP_NATIVE=1` enables host CPU tuning where supported. For portable wheels, leave this variable unset.

Strict floating-point behavior is always retained. There is no fast-math option.
The C++ compiler target must match Python's extension architecture. Release
builds use one architecture at a time.

## Solver development and LAPX

lapcpp 1.0.0 derives from LAPX 0.11.1's `lapjvh` family. lapcpp and laprust
are the lead repositories for C++ and Rust solver development, respectively.

Develop solver fixes, improvements, and regression tests in the corresponding
standalone repository first. Keep the common numerical and API contracts aligned
between lapcpp and laprust. Then port relevant changes into LAPX's corresponding
solver families. Preserve the LAPX and original upstream attribution in `NOTICE`.

## Workflows

- `tests.yaml` builds wheels from source archives. It runs separate solver and
  build/tool tests, typing checks, a SciPy-free core suite, and older NumPy
  runtime compatibility checks.
- `native.yaml` tests native resources, failures and Cython exception translation.
  It also runs Linux address/undefined-behavior/thread sanitizer checks.
- `integration.yaml` requires cross-package concurrency checks and isolated
  editable installs in default/strict/compat modes. It runs on
  Windows/Linux/macOS and Python 3.9/3.15.
- `prepublish.yaml` runs manual cibuildwheel smoke builds for CPython 3.9 and 3.15.
  To build all Python versions (3.9-3.15), select `full`.
  Reusable calls default to the full set. Builds cover Windows AMD64/ARM64,
  Linux x86_64/aarch64/armv7l (manylinux and musllinux), and macOS x86_64/arm64.
  Coverage depends on the combinations that cibuildwheel supports.
- `publish.yaml` runs on tags `v*`. It checks the version, builds the full wheel
  matrix, runs installed-wheel smoke checks, then uses PyPI Trusted Publishing,
  matching LAPX. Integration, native and sanitizer suites run separately on PRs
  or manual dispatch.
- `benchmark_single.yaml`, `benchmark_batch.yaml`, and `benchmark_tracking.yaml`
  run the five-candidate comparison manually and produce JSON artifacts.

Before publishing, configure the PyPI Trusted Publisher for this repository,
`publish.yaml`, and environment `pypi`.
Tag releases as `v<version>`, matching `lapcpp.__version__`.
The manual prepublish workflow creates artifacts without uploading them to PyPI.

Benchmark workflows accept a counterpart repository ref and install
`lapx>=0.13.0` from PyPI. Record both standalone source revisions and the installed
package versions when reporting comparisons. For unreleased local changes,
install the two standalone checkouts and `lapx>=0.13.0` from PyPI, as described in
[benchmark methodology](benchmarks/README.md).

ARMv7 manylinux builds on Python 3.12-3.15 use
`.github/constraints/numpy-armv7-build.txt` for both build and test dependencies.
The constraints match LAPX's GCC 9-compatible NumPy range (`>=2,<2.5`).
They do not change the installed package's NumPy requirement.
Every runnable wheel target uses the NumPy-only installed-wheel smoke check in
`.github/tests/check_wheel.py`.
The full solver and build/tool suites run in `tests.yaml`.

Wheel jobs separate Windows architectures, manylinux/musllinux targets,
and macOS architectures. Full builds also split Python 3.9-3.11 and
3.12-3.15 into parallel groups, so each job builds at most four wheels.
Windows and macOS reuse cached cibuildwheel downloads between runs.
Release compiler optimizations and platform coverage are unchanged.

See the [solver coverage inventory](tests/README.md) for the LAPX case mapping
and cross-package checks.
See [build and packaging checks](.github/tests/README.md) for isolated installation
checks, benchmark-tool tests, and CI scope.

See [native failure and sanitizer checks](tests/README.md#portable-and-native-failure-checks)
for local commands and test-only fault injection details.
