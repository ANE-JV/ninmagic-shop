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
    parser.add_argument("--private-game-folders", type=Path, help="Optional local selected-folder captures and expected.json: aggregate counts only")
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
    diagnostic_exe = args.output / ("test_ownfoil_diagnostic.exe" if sys.platform == "win32" else "test_ownfoil_diagnostic")
    subprocess.run([args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1", "-I", compiler_path(args.cxx, ROOT / "sphaira/include"), compiler_path(args.cxx, ROOT / "tools/tests/test_ownfoil_diagnostic.cpp"), "-o", compiler_path(args.cxx, diagnostic_exe)], check=True)
    subprocess.run([str(diagnostic_exe.resolve())], check=True)
    menu = (ROOT / "sphaira/source/ui/menus/ownfoil.cpp").read_text(encoding="utf-8")
    start = menu.index("auto Menu::SearchScope(")
    end = menu.index("\nvoid Menu::", menu.index("void Menu::ShowSearch(", start) + 1)
    (args.output / "production_show_search.inc").write_text(menu[start:end], encoding="utf-8")
    search_exe = args.output / ("test_shop_search.exe" if sys.platform == "win32" else "test_shop_search")
    subprocess.run([args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1",
        "-I", compiler_path(args.cxx, args.output), compiler_path(args.cxx, ROOT / "tools/tests/test_shop_search.cpp"),
        "-o", compiler_path(args.cxx, search_exe)], check=True)
    subprocess.run([str(search_exe.resolve())], check=True)
    title = (ROOT / "sphaira/source/ui/menus/ownfoil_title.cpp").read_text(encoding="utf-8")
    # Exercise the production handlers with a manually released package provider.
    def definition(source, signature):
        start = source.index(signature)
        return source[start:source.index("\n}", start) + 2]
    handlers = [(menu, "void Menu::UpdatePreview("), (menu, "void Menu::OpenSelected(")]
    handlers += [(title, signature) for signature in (
        "bool HasDownload(", "auto FormatVersion(", "auto FormatInstallVersion(", "void TitleMenu::OpenFullPage(",
        "void TitleMenu::EnsureSelection(", "void TitleMenu::ChoosePanelVersion(", "auto TitleMenu::HasBasePackage(",
        "auto TitleMenu::HasUpdatePackage(", "auto TitleMenu::HasPackage(", "auto TitleMenu::PollPreview(",
        "void TitleMenu::OpenInstallOptions(", "auto TitleMenu::DetailActionAvailable(",
        "void TitleMenu::ActivateDetailAction(", "auto TitleMenu::HandleDetailTouch(",
        "auto TitleMenu::HandleDetailController(", "void TitleMenu::UpdateActions(")]
    (args.output / "production_install_entry.inc").write_text(
        "\n".join(definition(source, signature) for source, signature in handlers), encoding="utf-8")
    install_exe = args.output / ("test_shop_install_entry.exe" if sys.platform == "win32" else "test_shop_install_entry")
    subprocess.run([args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1",
        "-I", compiler_path(args.cxx, args.output), compiler_path(args.cxx, ROOT / "tools/tests/test_shop_install_entry.cpp"),
        "-o", compiler_path(args.cxx, install_exe)], check=True)
    subprocess.run([str(install_exe.resolve())], check=True)
    api = (ROOT / "sphaira/source/utils/ownfoil_api.cpp").read_text(encoding="utf-8")
    (args.output / "production_snapshot.inc").write_text("\n".join(definition(api, signature) for signature in (
        "auto CatalogSnapshotPath(", "auto HeaderValue(", "auto Validators(", "auto Cacheable(",
        "auto FormValue(", "auto FetchWebArtwork(")), encoding="utf-8")
    snapshot_exe = args.output / ("test_ownfoil_snapshot.exe" if sys.platform == "win32" else "test_ownfoil_snapshot")
    subprocess.run([args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1",
        "-I", compiler_path(args.cxx, args.output), "-I", compiler_path(args.cxx, ROOT / "sphaira/include"),
        "-I", compiler_path(args.cxx, args.yyjson), compiler_path(args.cxx, ROOT / "tools/tests/test_ownfoil_snapshot.cpp"),
        compiler_path(args.cxx, obj), "-o", compiler_path(args.cxx, snapshot_exe)], check=True)
    snapshot_command = [str(snapshot_exe.resolve())]
    if args.private_metadata:
        snapshot_command.append(compiler_path(args.cxx, args.private_metadata))
    subprocess.run(snapshot_command, check=True)
    catalog_exe = args.output / ("test_ownfoil_catalog.exe" if sys.platform == "win32" else "test_ownfoil_catalog")
    subprocess.run([args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1", "-I", compiler_path(args.cxx, ROOT / "sphaira/include"), "-I", compiler_path(args.cxx, args.yyjson), compiler_path(args.cxx, ROOT / "tools/tests/test_ownfoil_catalog.cpp"), compiler_path(args.cxx, obj), "-o", compiler_path(args.cxx, catalog_exe)], check=True)
    api = (ROOT / "sphaira/source/utils/ownfoil_api.cpp").read_text(encoding="utf-8")
    native_types = api[api.index("struct NativePlatformGroups {"):api.index("std::mutex g_public_artwork_mutex;")]
    native_scope = api[api.index("static auto FetchScopedNativePage("):api.index("\nauto FetchApps(")]
    (args.output / "production_native_groups.inc").write_text(native_types + native_scope, encoding="utf-8")
    native_exe = args.output / ("test_native_platforms.exe" if sys.platform == "win32" else "test_native_platforms")
    subprocess.run([args.cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-O1",
        "-I", compiler_path(args.cxx, ROOT / "sphaira/include"), "-I", compiler_path(args.cxx, args.yyjson),
        "-I", compiler_path(args.cxx, args.output), compiler_path(args.cxx, ROOT / "tools/tests/test_native_platforms.cpp"),
        "-o", compiler_path(args.cxx, native_exe)], check=True)
    subprocess.run([str(native_exe.resolve())], check=True)
    catalog_command = [str(catalog_exe.resolve())]
    if args.private_fixture and args.private_metadata:
        catalog_command.extend([compiler_path(args.cxx, args.private_fixture), compiler_path(args.cxx, args.private_metadata)])
        if args.private_game_folders:
            catalog_command.append(compiler_path(args.cxx, args.private_game_folders))
    subprocess.run(catalog_command, check=True)

if __name__ == "__main__":
    main()
