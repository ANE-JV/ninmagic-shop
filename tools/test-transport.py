#!/usr/bin/env python3
"""Run HTTP range corruption regressions with a host C++ compiler."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def compiler_path(compiler, path):
    value = Path(path).resolve().as_posix()
    if sys.platform == "win32" and "/usr/bin/" in compiler.replace("\\", "/").lower() and len(value) > 2 and value[1] == ":":
        return "/" + value[0].lower() + value[2:]
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / ".local/transport-tests")
    parser.add_argument("--cxx", default=shutil.which("clang++") or shutil.which("g++"))
    args = parser.parse_args()
    if not args.cxx:
        parser.error("Install a host C++ compiler: Clang or GCC")
    args.output.mkdir(parents=True, exist_ok=True)
    executable = args.output / ("test_http_policy.exe" if sys.platform == "win32" else "test_http_policy")
    subprocess.run([args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1", "-I", compiler_path(args.cxx, ROOT / "sphaira/include"), compiler_path(args.cxx, ROOT / "tools/tests/test_http_policy.cpp"), "-o", compiler_path(args.cxx, executable)], check=True)
    subprocess.run([str(executable.resolve())], check=True)


if __name__ == "__main__":
    main()
