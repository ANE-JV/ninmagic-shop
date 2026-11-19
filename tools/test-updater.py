#!/usr/bin/env python3
"""Exercise the production update transaction with injected filesystem failures."""
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
    parser.add_argument("--yyjson", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / ".local/updater-tests")
    parser.add_argument("--cxx", default=shutil.which("g++") or shutil.which("clang++"))
    parser.add_argument("--cc", default=shutil.which("gcc") or shutil.which("clang"))
    args = parser.parse_args()
    if not args.cxx or not args.cc:
        parser.error("Install host C and C++ compilers: GCC or Clang")
    if not (args.yyjson / "yyjson.c").is_file():
        parser.error("--yyjson must point to the configured dependency's src directory")
    args.output.mkdir(parents=True, exist_ok=True)
    obj = args.output / "yyjson.o"
    executable = args.output / ("shop_update_test.exe" if sys.platform == "win32" else "shop_update_test")
    subprocess.run([args.cc, "-std=c11", "-O2", "-c", compiler_path(args.cc, args.yyjson / "yyjson.c"), "-o", compiler_path(args.cc, obj)], check=True)
    subprocess.run([args.cxx, "-std=c++23", "-O2", "-Wall", "-Wextra", "-Werror", "-DNMSHOP_UPDATE_HOST_TEST", "-I", compiler_path(args.cxx, ROOT / "sphaira/include"), "-I", compiler_path(args.cxx, args.yyjson), compiler_path(args.cxx, ROOT / "sphaira/source/shop_update.cpp"), compiler_path(args.cxx, ROOT / "tools/tests/shop_update_test.cpp"), compiler_path(args.cxx, obj), "-o", compiler_path(args.cxx, executable)], check=True)
    subprocess.run([str(executable.resolve())], check=True)


if __name__ == "__main__":
    main()
