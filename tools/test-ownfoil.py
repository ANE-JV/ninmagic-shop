#!/usr/bin/env python3
"""Build production JSON/HTML/platform parser fixtures with a host compiler."""
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
    parser.add_argument("--yyjson", type=Path, required=True, help="Pinned yyjson src containing yyjson.c and yyjson.h")
    parser.add_argument("--output", type=Path, default=ROOT / ".local/ownfoil-tests")
    parser.add_argument("--cxx", default=shutil.which("g++") or shutil.which("clang++"))
    parser.add_argument("--cc", default=shutil.which("gcc") or shutil.which("clang"))
    parser.add_argument("--private-fixture", type=Path, help="Optional local HTML snapshot: prints only aggregate counts")
    parser.add_argument("--private-metadata", type=Path, help="Optional local web metadata JSON: aggregate counts only")
    args = parser.parse_args()
    if not args.cxx or not args.cc:
        parser.error("A host C and C++ compiler is required")
    if not (args.yyjson / "yyjson.c").is_file() or not (args.yyjson / "yyjson.h").is_file():
        parser.error("--yyjson must contain yyjson.c and yyjson.h")
    args.output.mkdir(parents=True, exist_ok=True)
    obj = args.output / "yyjson.o"
    exe = args.output / ("test_ownfoil_index.exe" if sys.platform == "win32" else "test_ownfoil_index")
    subprocess.run([args.cc, "-std=c99", "-O1", "-c", compiler_path(args.cc, args.yyjson / "yyjson.c"), "-o", compiler_path(args.cc, obj)], check=True)
    subprocess.run([args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1", "-I", compiler_path(args.cxx, ROOT / "sphaira/include"), "-I", compiler_path(args.cxx, args.yyjson), compiler_path(args.cxx, ROOT / "tools/tests/test_ownfoil_index.cpp"), compiler_path(args.cxx, obj), "-o", compiler_path(args.cxx, exe)], check=True)
    command = [str(exe.resolve())]
    if args.private_fixture:
        command.append(compiler_path(args.cxx, args.private_fixture))
        if args.private_metadata:
            command.append(compiler_path(args.cxx, args.private_metadata))
    subprocess.run(command, check=True)

if __name__ == "__main__":
    main()
