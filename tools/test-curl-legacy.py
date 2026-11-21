#!/usr/bin/env python3
"""Test production libcurl options against a strict loopback Ownfoil fixture."""
import argparse
import base64
import ctypes.util
import gzip
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading

ROOT = Path(__file__).resolve().parent.parent


def compiler_path(compiler, path):
    value = Path(path).resolve().as_posix()
    if sys.platform == "win32" and "/usr/bin/" in compiler.replace("\\", "/").lower() and len(value) > 2 and value[1] == ":":
        return "/" + value[0].lower() + value[2:]
    return value


class OwnfoilFixture(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):
        pass

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def do_GET(self):
        expected = "Basic " + base64.b64encode(b"wire-user:wire-pass").decode()
        if self.path == "/compressed-metadata":
            payload = gzip.compress(b"x" * 8192)
            self.send_response(200)
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("Content-Encoding", "gzip")
            self.end_headers()
            self.wfile.write(payload)
            return
        if self.path.startswith("/wire/"):
            if self.headers.get("Authorization") != expected:
                self.send_error(401)
            elif self.path == "/wire/dead-alias":
                self.send_error(404)
            elif self.path == "/wire/gone":
                self.send_error(410)
            elif self.path == "/wire/denied":
                self.send_error(403)
            elif self.path == "/wire/modern" and not self.headers.get("User-Agent", "").startswith("Sphaira/"):
                self.send_error(404)
            elif self.path == "/wire/legacy" and self.headers.get("User-Agent"):
                self.send_error(404)
            else:
                data = b"0123456789abcdef"
                # Only the final 16 bytes are materialized for the large-file
                # fixture, so a 64-bit seek is exercised without a large transfer.
                first = (1 << 32) if self.path == "/wire/large" else 0
                total = first + len(data)
                requested = self.headers.get("Range", "")
                if (not requested.startswith("bytes=") or not requested.endswith("-")
                        or not requested[6:-1].isdigit() or not first <= int(requested[6:-1]) < total):
                    self.send_error(416)
                    return
                offset = int(requested[6:-1])
                reported = 0 if self.path == "/wire/wrong-range" else offset
                self.send_response(206)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Content-Range", f"bytes {reported}-{total-1}/{total}")
                self.send_header("Content-Length", str(total-offset))
                self.send_header("Connection", "close")
                self.end_headers()
                self.wfile.write(data[offset-first:])
            return
        headers = {name.lower() for name in self.headers}
        allowed = {"host", "accept", "accept-encoding", "authorization"}
        index = (headers == allowed and self.headers.get("Authorization") == expected
                 and self.headers.get("Accept") == "*/*"
                 and self.headers.get("Accept-Encoding") == "identity")
        payload = b'<table><tr><td><a href="catalog/">catalog</a></td></tr></table>' if index else b'<form action="/login" method="post"></form>'
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / ".local/curl-legacy-tests")
    parser.add_argument("--cxx", default=shutil.which("g++") or shutil.which("clang++"))
    sdk = Path(os.environ.get("DEVKITPRO", "C:/devkitPro" if sys.platform == "win32" else "/opt/devkitpro"))
    if sys.platform == "win32" and not sdk.is_dir():
        if len(str(sdk)) > 3 and str(sdk)[0] == "/" and str(sdk)[2] == "/":
            sdk = Path(str(sdk)[1] + ":" + str(sdk)[2:])
        elif Path("C:/devkitPro").is_dir():
            sdk = Path("C:/devkitPro")
    parser.add_argument("--curl-include", type=Path, default=sdk / "portlibs/switch/include")
    parser.add_argument("--curl-library", default=str(sdk / "msys2/usr/bin/msys-curl-4.dll") if sys.platform == "win32" else None)
    parser.add_argument("--curl-static", action="store_true", help="Use a native static libcurl build")
    parser.add_argument("--link-library", type=Path, action="append", default=[])
    parser.add_argument("--link-arg", action="append", default=[])
    parser.add_argument("--expected-curl-version", help="Reject a different host libcurl version")
    args = parser.parse_args()
    if not args.cxx or not (args.curl_include / "curl/curl.h").is_file():
        parser.error("A host C++ compiler and libcurl headers are required")
    if args.curl_library:
        library = compiler_path(args.cxx, args.curl_library)
    else:
        name = ctypes.util.find_library("curl") or ctypes.util.find_library("curl-gnutls")
        if not name:
            parser.error("A host libcurl runtime library is required")
        library = "-l:" + name
    args.output.mkdir(parents=True, exist_ok=True)
    source = (ROOT / "sphaira/source/download.cpp").read_text(encoding="utf-8")
    start = source.index("auto SetCommonCurlOptions(")
    end = source.index("\nauto DownloadInternal(", start)
    (args.output / "production_curl_options.inc").write_text(source[start:end], encoding="utf-8")
    start = source.index("auto WriteMemoryCallback(")
    end = source.index("\nauto WriteFileCallback(", start)
    (args.output / "production_memory_write.inc").write_text(source[start:end], encoding="utf-8")
    # Only hardware/platform dependencies are replaced. Build the reader from
    # the source also compiled for the console.
    fragments = []
    for file in ("sphaira/include/yati/source/base.hpp", "sphaira/include/yati/source/http.hpp",
                 "sphaira/source/yati/source/http.cpp"):
        source = (ROOT / file).read_text(encoding="utf-8")
        fragments.append("\n".join(line for line in source.splitlines()
                                   if not line.startswith(("#include ", "#pragma once"))))
    (args.output / "production_yati_http.inc").write_text("\n".join(fragments), encoding="utf-8")
    def compile_wire(name):
        out = args.output / (name + (".exe" if sys.platform == "win32" else ""))
        command = [args.cxx, "-std=c++20", "-O1", "-Wno-deprecated-declarations"]
        if args.curl_static:
            command.append("-DCURL_STATICLIB")
        for directory in (args.curl_include, ROOT / "sphaira/include", args.output):
            command += ["-I", compiler_path(args.cxx, directory)]
        command += [compiler_path(args.cxx, ROOT / f"tools/tests/{name}.cpp"),
                    library, "-o", compiler_path(args.cxx, out)]
        command += [compiler_path(args.cxx, item) for item in args.link_library] + args.link_arg
        subprocess.run(command, check=True)
        return str(out.resolve())

    executable = compile_wire("curl_legacy_wire")
    yati = compile_wire("yati_http_wire")
    server = ThreadingHTTPServer(("127.0.0.1", 0), OwnfoilFixture)
    server.daemon_threads = True
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        base = f"http://127.0.0.1:{server.server_port}/"
        result = subprocess.run([executable, base], capture_output=True, text=True, check=True)
        subprocess.run([yati, base], check=True, timeout=20)
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
    rows = [json.loads(line) for line in result.stdout.splitlines() if line.strip()]
    if args.expected_curl_version:
        assert rows and all(row["curl_version"] == args.expected_curl_version for row in rows), "Unexpected host libcurl version"
    fixed = [row for row in rows if row["case"] in ("fresh_legacy", "legacy_after_native", "legacy_repeat")]
    assert len(fixed) == 3 and all(row["shop_index"] for row in fixed), rows
    print(json.dumps({"cases": rows, "result": "PASS; production options and handle policy used; loopback only, synthetic credentials"}, indent=2))


if __name__ == "__main__":
    main()
