#!/usr/bin/env python3
"""Simple HTTP server for Shipwright web build with CORS and proper MIME types."""

import http.server
import os
import sys
import shutil

PORT = 8080
BUILD_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "build-web", "soh")
ROM_SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "test-assets", "oot.z64")
ROM_DEST = os.path.join(BUILD_DIR, "rom.z64")
SOH_O2R_SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "soh", "soh", "web", "soh.o2r")
SOH_O2R_DEST = os.path.join(BUILD_DIR, "soh.o2r")


class CORSHandler(http.server.SimpleHTTPRequestHandler):
    """HTTP handler with CORS headers and correct MIME types for WASM."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=BUILD_DIR, **kwargs)

    def end_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "*")
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(204)
        self.end_headers()

    def guess_type(self, path):
        if path.endswith(".wasm"):
            return "application/wasm"
        if path.endswith(".data"):
            return "application/octet-stream"
        if path.endswith(".z64"):
            return "application/octet-stream"
        if path.endswith(".o2r"):
            return "application/octet-stream"
        return super().guess_type(path)


def main():
    if not os.path.isdir(BUILD_DIR):
        print(f"Error: Build directory not found: {BUILD_DIR}")
        print("Run ./build_web.sh first.")
        sys.exit(1)

    # Symlink or copy ROM into build directory for serving
    if os.path.exists(ROM_SRC) and not os.path.exists(ROM_DEST):
        print(f"Symlinking ROM into build dir: {ROM_DEST}")
        os.symlink(ROM_SRC, ROM_DEST)

    # Copy soh.o2r into build directory for serving
    if os.path.exists(SOH_O2R_SRC) and not os.path.exists(SOH_O2R_DEST):
        print(f"Copying soh.o2r into build dir: {SOH_O2R_DEST}")
        shutil.copy2(SOH_O2R_SRC, SOH_O2R_DEST)

    print(f"Serving from: {BUILD_DIR}")
    print(f"ROM available: {os.path.exists(ROM_DEST)}")
    print(f"soh.o2r available: {os.path.exists(SOH_O2R_DEST)}")
    print(f"")
    print(f"  Local:  http://localhost:{PORT}/soh.html#rom=rom.z64")
    print(f"")
    print(f"Start cloudflared tunnel in another terminal:")
    print(f"  cloudflared tunnel --url http://localhost:{PORT}")
    print()

    server = http.server.HTTPServer(("0.0.0.0", PORT), CORSHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nShutting down.")
        server.shutdown()


if __name__ == "__main__":
    main()
