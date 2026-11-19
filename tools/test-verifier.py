#!/usr/bin/env python3
"""Reject malformed release candidates without modifying the produced NRO."""
import argparse
import importlib.util
from pathlib import Path
import struct
from types import SimpleNamespace

spec = importlib.util.spec_from_file_location("verify_shop", Path(__file__).with_name("verify.py"))
validator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validator)


class Candidate:
    name = "candidate.nro"

    def __init__(self, data, size=None):
        self.data = data
        self.size = len(data) if size is None else size

    def stat(self):
        return SimpleNamespace(st_size=self.size)

    def read_bytes(self):
        return self.data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("nro", type=Path)
    args = parser.parse_args()
    validator.verify(args.nro)
    original = args.nro.read_bytes()
    cases = []
    for offset, value in ((0x24, 0), (0x20, 0x80), (0x30, 0)):
        data = bytearray(original)
        struct.pack_into("<I", data, offset, value)
        cases.append(Candidate(data))
    nro_size = struct.unpack_from("<I", original, 0x18)[0]
    data = bytearray(original)
    struct.pack_into("<Q", data, nro_size + 8 + 16, 0x38)
    cases.append(Candidate(data))
    nacp_offset = struct.unpack_from("<Q", original, nro_size + 8 + 16)[0]
    for offset in (nro_size + nacp_offset, nro_size + nacp_offset + 0x200):
        data = bytearray(original)
        data[offset] = ord("X")
        cases.append(Candidate(data))
    cases.append(Candidate(original[:-100]))
    cases.append(Candidate(original, 64 * 1024 * 1024 + 1))
    for index, candidate in enumerate(cases):
        try:
            validator.verify(candidate)
        except ValueError:
            continue
        raise AssertionError(f"Malformed release candidate {index} was accepted")
    print(f"Release verifier: valid NRO accepted, {len(cases)} malformed candidates rejected in memory.")


if __name__ == "__main__":
    main()
