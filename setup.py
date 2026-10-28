# Copyright (c) 2026 Ratha SIV | MIT License
from pathlib import Path
import sys
from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext

class BuildExt(build_ext):
    """Add portable optimization flags for the compiler and linker.

    Environment variables control these options:
    - LAPCPP_BASEOPTS=0 disables the base flags that LAPCPP adds and link-time
      optimization (LTO). Other compiler defaults may still apply.
    - LAPCPP_NATIVE=1 enables -march=native -mtune=native.
    - LAPCPP_LTO=0 disables LTO, including inherited LTO defaults.

    LAPCPP_BASEOPTS=0 does not guarantee an unoptimized or debug build.
    Native tuning is disabled by default. Strict floating-point arithmetic is always used.
    """
    def has_flag(self, flag, link_flag=None):
        import tempfile, os
        with tempfile.TemporaryDirectory() as tmpdir:
            fname = os.path.join(tmpdir, 'flagcheck.cpp')
            with open(fname, 'w') as f:
                f.write("int main(){return 0;}")
            try:
                objects = self.compiler.compile(
                    [fname], output_dir=tmpdir, extra_postargs=[flag], debug=self.debug)
                if link_flag is not None:
                    self.compiler.link_shared_object(
                        objects,
                        os.path.join(tmpdir, 'flagcheck' + self.compiler.shared_lib_extension),
                        extra_postargs=[link_flag], target_lang='c++', debug=self.debug)
            except Exception:
                return False
        return True

    def build_extensions(self):
        import os, sys

        base_enabled = (os.environ.get('LAPCPP_BASEOPTS', '1').strip() != '0')
        env_native   = (os.environ.get('LAPCPP_NATIVE',   '').strip() == '1')
        env_lto_on   = (os.environ.get('LAPCPP_LTO',      '1').strip() == '1')

        print("\n[+] LAPCPP -> Build options")
        print(f"    - LAPCPP_BASEOPTS  :  {'Enabled' if base_enabled else 'Disabled'}")
        print(f"    - LAPCPP_NATIVE    :  {'Enabled' if env_native else 'Disabled'}")
        print(f"    - LTO LAPCPP_LTO   :  {'Enabled' if (base_enabled and env_lto_on) else 'Disabled'}\n")

        ctype = self.compiler.compiler_type
        is_msvc = (ctype == 'msvc')

        compile_opts = []
        link_opts = []
        lto_enabled = False

        if is_msvc:
            # Base optimizations on MSVC
            if base_enabled:
                compile_opts += ['/O2', '/DNDEBUG']
                # Respect LAPCPP_LTO on MSVC when base opts are enabled
                if env_lto_on and self.has_flag('/GL', '/LTCG'):
                    compile_opts += ['/GL']
                    link_opts += ['/LTCG']
                    lto_enabled = True
        else:
            # Base optimizations on GCC/Clang
            if base_enabled:
                compile_opts += ['-O3', '-DNDEBUG']
                if sys.version_info >= (3, 9) and self.has_flag('-fvisibility=hidden'):
                    compile_opts += ['-fvisibility=hidden']
                if self.has_flag('-fno-math-errno'):
                    compile_opts += ['-fno-math-errno']
                # Link-time optimization (prefer ThinLTO when available)
                if env_lto_on:
                    if self.has_flag('-flto=thin', '-flto=thin'):
                        compile_opts += ['-flto=thin']; link_opts += ['-flto=thin']
                        lto_enabled = True
                    elif self.has_flag('-flto', '-flto'):
                        compile_opts += ['-flto']; link_opts += ['-flto']
                        lto_enabled = True
                # Minor call overhead reduction on Linux/glibc (if supported)
                if sys.platform.startswith('linux') and self.has_flag('-fno-plt'):
                    compile_opts += ['-fno-plt']
            # Optional native tuning (opt-in; avoid for portable wheels)
            if env_native:
                if self.has_flag('-march=native'):
                    compile_opts += ['-march=native']
                if self.has_flag('-mtune=native'):
                    compile_opts += ['-mtune=native']

        # Override inherited LTO defaults when disabled or when the probe fails.
        if not lto_enabled:
            compile_opts += ['/GL-' if is_msvc else '-fno-lto']
            link_opts += ['/LTCG:OFF' if is_msvc else '-fno-lto']

        # Apply to all extensions (always)
        for ext in self.extensions:
            prev_cargs = list(getattr(ext, 'extra_compile_args', []) or [])
            prev_largs = list(getattr(ext, 'extra_link_args', []) or [])
            ext.extra_compile_args = prev_cargs + compile_opts
            ext.extra_link_args = prev_largs + link_opts
            if ext.name == 'lapcpp._lapjv':
                # Conversion and forbidden-edge checks require strict arithmetic.
                ext.extra_compile_args += ['/fp:precise' if is_msvc else '-fno-fast-math']

        # Show final compiler/linker args for debugging
        for ext in self.extensions:
            print(f"\n[+] LAPCPP -> Extension << {ext.name} >>")
            print(f"    - extra_compile_args : {ext.extra_compile_args}")
            print(f"    - extra_link_args    : {ext.extra_link_args}")
            if ext == self.extensions[-1]:
                print()

        super().build_extensions()


def main_setup():
    import numpy as np
    from Cython.Build import cythonize
    extension = Extension(
        "lapcpp._lapjv", sources=['src/_lapjv/_lapjv.pyx', 'src/_lapjv/solver.cpp', 'src/_lapjv/completion.cpp'], language="c++",
        include_dirs=[np.get_include(), "src/_lapjv"],
        define_macros=[("NPY_NO_DEPRECATED_API", "NPY_1_7_API_VERSION")],
        extra_compile_args=["/std:c++17"] if sys.platform == "win32" else ["-std=c++17", "-pthread", "-ffp-contract=off"],
        extra_link_args=[] if sys.platform == "win32" else ["-pthread"],
        depends=["src/_lapjv/lapjv.h"],
    )
    version = next(line.split('"')[1] for line in Path("lapcpp/__init__.py").read_text().splitlines() if line.startswith("__version__"))
    setup(
        name="lapcpp", version=version,
        description="Native C++ linear assignment solvers with implicit rejection and batch solving.",
        long_description=Path("README.md").read_text(encoding="utf-8"),
        long_description_content_type="text/markdown",
        author="Ratha SIV", maintainer="rathaROG", license="MIT",
        url="https://github.com/rathaROG/lapcpp",
        license_files=['LICENSE', 'NOTICE', 'tests/LICENSE', 'src/_lapjv/LICENSE'],
        packages=["lapcpp"], package_data={"lapcpp": ["*.pyi", "py.typed"]},
        include_package_data=True,
        python_requires=">=3.9", install_requires=["numpy>=1.21.6"],
        extras_require={"test": ["pytest>=7", "scipy>=1.9", "pytest-timeout"],
                        "benchmark": ["scipy>=1.9", "lapx==0.11.1"], "dev": ["build", "setuptools>=77", "wheel", "pytest>=7", "scipy>=1.9", "pytest-timeout", "pyright"]},
        classifiers=['Development Status :: 5 - Production/Stable',
                     'Environment :: Console',
                     'Intended Audience :: Developers',
                     'Intended Audience :: Education',
                     'Intended Audience :: Science/Research',
                     'Programming Language :: Python :: 3',
                     'Programming Language :: Python :: 3.9',
                     'Programming Language :: Python :: 3.10',
                     'Programming Language :: Python :: 3.11',
                     'Programming Language :: Python :: 3.12',
                     'Programming Language :: Python :: 3.13',
                     'Programming Language :: Python :: 3.14',
                     'Programming Language :: Python :: 3.15',
                     'Topic :: Education',
                     'Topic :: Education :: Testing',
                     'Topic :: Scientific/Engineering',
                     'Topic :: Scientific/Engineering :: Mathematics',
                     'Topic :: Software Development',
                     'Topic :: Software Development :: Libraries',
                     'Operating System :: Microsoft :: Windows',
                     'Operating System :: POSIX',
                     'Operating System :: Unix',
                     'Operating System :: MacOS',],
        ext_modules=cythonize([extension], compiler_directives={
            "language_level": 3, "boundscheck": False, "wraparound": False,
            "nonecheck": False, "initializedcheck": False, "cdivision": True,
            "infer_types": True, "profile": False, "linetrace": False}),
        cmdclass={"build_ext": BuildExt},
    )


if __name__ == "__main__":
    """Use :py:mod:`build` to build the package without changes to the current environment.

    >>> pip install build
    >>> python -m build --sdist
    >>> python -m build --wheel

    The build applies safe base optimizations automatically:
    - /O2 on MSVC or -O3 on GCC and Clang enables compiler optimizations.
    - -DNDEBUG disables debug assertions.
    - Link-time optimization (LTO) applies when the compiler and linker support it.
    - Strict floating-point arithmetic (/fp:precise or -fno-fast-math) is always used;
      there is no fast-math option.
    - Windows builds use /std:c++17; /std:c++latest generated slower solver code with MSVC 14.44.

    Environment variables control additional options:
    - LAPCPP_BASEOPTS=0 disables the base flags that LAPCPP adds and LTO.
      Other compiler defaults may still apply.
    - LAPCPP_NATIVE=1 enables -march=native -mtune=native on GCC and Clang only.
    - LAPCPP_LTO=0 disables LTO, including inherited LTO defaults.

    LAPCPP_BASEOPTS=0 does not guarantee an unoptimized or debug build.
    Native tuning is disabled by default. Keep it disabled for portable release wheels.

    To build with native tuning on Linux or macOS, use this command:
    >>> LAPCPP_NATIVE=1 python -m build --wheel

    To build without LTO in the Windows command prompt (CMD), use this command:
    >>> set "LAPCPP_LTO=0" && python -m build --wheel

    The build applies Cython compiler directives by default for Cython modules.
    Examples include boundscheck=False, wraparound=False, and cdivision=True.
    """
    main_setup()
