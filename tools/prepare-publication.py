#!/usr/bin/env python3
"""Prepare immutable source and binary artifacts, without uploading anything."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import zipfile

ROOT = Path(__file__).resolve().parent.parent
FILES = ("README.md", "LICENSE", ".gitignore", "CMakeLists.txt", "CMakePresets.json")
FOLDERS = ("sphaira", "hbl", "cmake", "tools", "docs", "third_party", "assets/embed", "assets/romfs", ".github/workflows")
ASSETS = ("assets/icon.jpg", "assets/ninmagic-shop-master.png", "assets/cacert-provenance.md", "assets/design-source.json")
SKIP_PARTS = {".git", ".local", "private", "__pycache__", "node_modules", "dist", "build", "out"}
SKIP_SUFFIXES = {".pyc", ".exe", ".elf", ".nro", ".nacp", ".o", ".a", ".log", ".tmp"}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def source_files() -> list[Path]:
    paths = [ROOT / name for name in FILES + ASSETS]
    for name in FOLDERS:
        folder = ROOT / name
        if folder.is_dir():
            paths.extend(p for p in folder.rglob("*") if p.is_file())
    result = []
    for path in sorted(set(paths)):
        relative = path.relative_to(ROOT)
        if any(part in SKIP_PARTS or part.startswith("build-") for part in relative.parts):
            continue
        if path.suffix.lower() in SKIP_SUFFIXES:
            continue
        if path.is_symlink() or not path.is_file():
            raise ValueError(f"Source input is missing or is a link: {relative}")
        result.append(path)
    return result


def install_frozen(source: Path, destination: Path) -> None:
    if destination.exists():
        if digest(destination.read_bytes()) != digest(source.read_bytes()):
            raise ValueError(f"Refusing to overwrite a different version artifact: {destination.name}")
        return
    shutil.copyfile(source, destination)


def main() -> None:
    spec = importlib.util.spec_from_file_location("verify_shop", ROOT / "tools/verify.py")
    validator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(validator)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nro", type=Path, default=ROOT / "dist/NinMagic-Shop.nro")
    parser.add_argument("--zip", type=Path)
    args = parser.parse_args()
    version = validator.project_version()
    package = args.zip or ROOT / f"dist/NinMagic-Shop-{version}.zip"
    validated = validator.verify(args.nro, version)
    with zipfile.ZipFile(package) as archive:
        if archive.testzip() is not None:
            raise ValueError("The install ZIP has a CRC error")
        entries = archive.namelist()
        if not entries or any(not name.startswith("switch/NinMagicShop/") or ".." in Path(name).parts for name in entries):
            raise ValueError("The install ZIP is outside the isolated application directory")
        if digest(archive.read("switch/NinMagicShop/NinMagic-Shop.nro")) != validated["sha256"]:
            raise ValueError("The ZIP contains a different NRO")
    files = source_files()
    temporary = ROOT / "dist/source.pending.zip"
    with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in files:
            name = path.relative_to(ROOT).as_posix()
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (0o100755 if path.suffix == ".sh" else 0o100644) << 16
            archive.writestr(info, path.read_bytes())
    with zipfile.ZipFile(temporary) as archive:
        if archive.testzip() is not None or len(archive.namelist()) != len(files):
            raise ValueError("The source ZIP failed read-back")
    output = ROOT / "dist/publication/releases" / version
    output.mkdir(parents=True, exist_ok=True)
    install_frozen(args.nro, output / "NinMagic-Shop.nro")
    install_frozen(package, output / f"NinMagic-Shop-{version}.zip")
    source_zip = output / f"NinMagic-Shop-{version}-source.zip"
    install_frozen(temporary, source_zip)
    artifacts = {p.name: {"bytes": p.stat().st_size, "sha256": digest(p.read_bytes())}
                 for p in sorted(output.iterdir()) if p.is_file() and p.name != "SHA256SUMS"}
    checksums = "".join(f"{metadata['sha256']}  {name}\n" for name, metadata in artifacts.items())
    checksum_path = output / "SHA256SUMS"
    if checksum_path.exists() and checksum_path.read_text(encoding="ascii") != checksums:
        raise ValueError("The frozen checksum file differs")
    checksum_path.write_text(checksums, encoding="ascii")
    inventory = {path.relative_to(ROOT).as_posix(): {"bytes": path.stat().st_size, "sha256": digest(path.read_bytes())}
                 for path in files}
    receipt = {"version": version, "binary": validated, "artifacts": artifacts, "sources": inventory,
               "source_count": len(files), "install_entries": entries,
               "console_execution_tested": False, "console_install_tested": False}
    (ROOT / "dist/publication-validation.json").write_text(json.dumps(receipt, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({"version": version, "source_count": len(files), "artifacts": artifacts}, ensure_ascii=False))


if __name__ == "__main__":
    main()
