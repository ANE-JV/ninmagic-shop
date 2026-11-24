"""Exercise the production rating functions against a temporary minIni file."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def compiler_path(executable, path):
    value = path.resolve().as_posix()
    if sys.platform == "win32" and "/usr/bin/" in executable.replace("\\", "/").lower() and value[1:2] == ":":
        return "/" + value[0].lower() + value[2:]
    return value


def run(minini, cc, cxx):
    source = (ROOT / "sphaira/source/utils/ownfoil.cpp").read_text(encoding="utf-8")
    functions = []
    for name in ("RatingKey", "GetGameRating", "SetGameRating"):
        start = source.index("auto " + name + "(")
        functions.append(source[start:source.index("\n}", start) + 2])
    with tempfile.TemporaryDirectory(prefix="ninmagic-ratings-") as directory:
        tmp = Path(directory)
        harness = tmp / "ratings.cpp"
        harness.write_text(r'''
#include <minIni.h>
#include <algorithm>
#include <cassert>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#define R_FAILED(result) ((result) != 0)
#define R_SUCCEEDED(result) ((result) == 0)
const char* RATINGS_PATH = "ratings.ini";
namespace fs {
struct FsNativeSd {
    int CreateDirectoryRecursively(const char*) { return 0; }
    int Commit() { return 0; }
};
}
''' + "\n".join(functions) + r'''
int main(int argc, char**) {
    const std::string game = "051A0000001B0000", other = "0100000000001000";
    if (argc > 1) {
        assert(GetGameRating("profile-a", game) == 5);
        assert(GetGameRating("profile-b", game) == 2);
        assert(GetGameRating("profile-a", other) == 4);
        assert(GetGameRating("profile-b", other) == 0);
        assert(GetGameRating("profile-a", "package-0000000000000aBc") == 3);
        return 0;
    }
    assert(GetGameRating("profile-a", game) == 0);
    assert(SetGameRating("profile-a", game, 1));
    assert(SetGameRating("profile-a", "051a0000001b0000", 5));
    assert(SetGameRating("profile-a", other, 4));
    assert(SetGameRating("profile-b", game, 2));
    assert(SetGameRating("profile-a", "package-0000000000000abc", 3));
    for (const int value : {-1, 0, 6, 15})
        assert(!SetGameRating("profile-a", game, value));
    for (const auto* id : {"", "0000000000000000", "../x", "package-mario", "prefix0100000000001000", "010000000000100G"})
        assert(!GetGameRating("profile-a", id) && !SetGameRating("profile-a", id, 3));
    for (const std::string uid : {"", "]", "x\ny", "x=key"})
        assert(!GetGameRating(uid, game) && !SetGameRating(uid, game, 3));
    for (const auto* bad : {"6", "03", "2ignored"}) {
        assert(ini_puts("profile-b", other.c_str(), bad, RATINGS_PATH));
        assert(GetGameRating("profile-b", other) == 0);
    }
    RATINGS_PATH = "missing-directory/ratings.ini";
    assert(!SetGameRating("profile-a", game, 3));
}
''', encoding="utf-8")
        obj = tmp / "minIni.o"
        exe = tmp / ("ratings.exe" if sys.platform == "win32" else "ratings")
        subprocess.run([cc, "-std=c99", "-include", "strings.h", "-DMININI_USE_NX=0", "-DMININI_USE_STDIO=1",
                        "-I", compiler_path(cc, minini / "include"), "-c", compiler_path(cc, minini / "source/minIni.c"),
                        "-o", compiler_path(cc, obj)], check=True)
        subprocess.run([cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-I", compiler_path(cxx, minini / "include"),
                        compiler_path(cxx, harness), compiler_path(cxx, obj), "-o", compiler_path(cxx, exe)], check=True)
        subprocess.run([str(exe)], cwd=tmp, check=True)
        subprocess.run([str(exe), "reopen"], cwd=tmp, check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--minini", type=Path, required=True)
    parser.add_argument("--cc", default=shutil.which("gcc"))
    parser.add_argument("--cxx", default=shutil.which("g++"))
    args = parser.parse_args()
    run(args.minini, args.cc, args.cxx)
    print("PASS: ratings, edits, restart, profile isolation, malformed values and write failures")
