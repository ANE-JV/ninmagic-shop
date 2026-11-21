#!/usr/bin/env python3
"""Reject a Switch ELF whose translation units disagree on the layout of curl::Api."""
import argparse
import os
import re
import subprocess
from pathlib import Path


# Binutils can skip member DIEs when scanning the full ELF. Read the members
# only for the small number of types named Api (one copy per compilation unit).
TYPE = re.compile(
    rb" <3><([0-9a-f]+)>: Abbrev Number: \d+ \(DW_TAG_(?:class|structure)_type\)\r?\n"
    rb"[^\n]*DW_AT_name\s*:\s*(?:\([^\n]*\): )?Api\r?\n"
    rb"[^\n]*DW_AT_byte_size\s*:\s*(\d+)\r?\n"
)
DIE = re.compile(rb"^ <4><[^>]+>: Abbrev Number: [^\r\n]+", re.MULTILINE)
MEMBER_NAME = re.compile(rb"DW_AT_name\s*:\s*(?:\([^\n]*\): )?(\w+)")
OFFSET = re.compile(rb"DW_AT_data_member_location\s*:\s*(\d+)")


def find_types(reader, elf):
    definitions = {}
    with subprocess.Popen([reader, "--debug-dump=info", "--dwarf-depth=4", elf],
                          stdout=subprocess.PIPE) as proc:
        overlap = b""
        while chunk := proc.stdout.read(1024 * 1024):
            block = overlap + chunk
            for die, size in TYPE.findall(block):
                definitions[int(die, 16)] = int(size)
            # A definition may straddle chunks; its three lines fit in 1 KiB.
            overlap = block[-1024:]
        if proc.wait():
            raise SystemExit("Could not read the linked Switch debug information.")
    return definitions


def layout(reader, elf, die, size):
    info = subprocess.check_output([reader, "--debug-dump=info", f"--dwarf-start={die}",
                                    "--dwarf-depth=5", elf])
    # Binutils can print a following sibling as well. Its fields belong to
    # another class, even when they occur at the same nesting depth.
    info = re.split(rb"\n <[0-3]><", info.lstrip(), maxsplit=1)[0]
    # Qualify the type using the Itanium linkage name of its constructor.
    if b"_ZN7sphaira4curl3ApiC" not in info:
        return None
    members = []
    entries = list(DIE.finditer(info))
    for index, entry in enumerate(entries):
        if b"DW_TAG_member)" not in entry.group():
            continue
        stop = entries[index + 1].start() if index + 1 < len(entries) else len(info)
        details = info[entry.end():stop]
        name, offset = MEMBER_NAME.search(details), OFFSET.search(details)
        if not name or not offset:
            raise SystemExit(f"Incomplete member information at DWARF offset {die:#x}.")
        members.append((name.group(1).decode("ascii"), int(offset.group(1))))
    if not {"m_url", "m_stop_source", "m_stoken"}.issubset(dict(members)):
        raise SystemExit(f"Incomplete curl::Api layout at DWARF offset {die:#x}.")
    return size, tuple(members)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path, help="unstripped sphaira.elf for the packaged NRO")
    parser.add_argument("--readelf", default="aarch64-none-elf-readelf")
    args = parser.parse_args()
    os.environ["LC_ALL"] = "C"
    layouts = {}
    for die, size in find_types(args.readelf, args.elf).items():
        signature = layout(args.readelf, args.elf, die, size)
        if signature is not None:
            layouts.setdefault(signature, []).append(die)
    count = sum(map(len, layouts.values()))
    if count < 2:
        raise SystemExit("Cannot compare curl::Api across compilation units in this ELF.")
    if len(layouts) != 1:
        for (size, members), offsets in layouts.items():
            print(f"{len(offsets)} units: sizeof(Api)={size}, "
                  f"m_stop_source={dict(members)['m_stop_source']}, example DIE {offsets[0]:#x}")
        raise SystemExit("Incompatible curl::Api layouts in the linked Switch program.")
    print(f"Verified identical curl::Api member offsets and size in {count} compilation units.")


if __name__ == "__main__":
    main()
