#!/usr/bin/env python3
"""Test production libcurl options against a strict loopback Ownfoil fixture."""
import argparse
import base64
import ctypes.util
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
    executable = args.output / ("curl_legacy_wire.exe" if sys.platform == "win32" else "curl_legacy_wire")
    command = [args.cxx, "-std=c++20", "-O1", "-Wno-deprecated-declarations", "-I", compiler_path(args.cxx, args.curl_include), "-I", compiler_path(args.cxx, ROOT / "sphaira/include"), "-I", compiler_path(args.cxx, args.output), compiler_path(args.cxx, ROOT / "tools/tests/curl_legacy_wire.cpp"), library, "-o", compiler_path(args.cxx, executable)]
    subprocess.run(command, check=True)
    server = ThreadingHTTPServer(("127.0.0.1", 0), OwnfoilFixture)
    server.daemon_threads = True
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        result = subprocess.run([str(executable.resolve()), f"http://127.0.0.1:{server.server_port}/"], capture_output=True, text=True, check=True)
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
    rows = [json.loads(line) for line in result.stdout.splitlines() if line.strip()]
    fixed = [row for row in rows if row["case"] in ("fresh_legacy", "legacy_after_native", "legacy_repeat")]
    assert len(fixed) == 3 and all(row["shop_index"] for row in fixed), rows
    print(json.dumps({"cases": rows, "result": "PASS; production options and handle policy used; loopback only, synthetic credentials"}, indent=2))


if __name__ == "__main__":
    main()
