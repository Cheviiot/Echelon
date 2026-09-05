#!/usr/bin/env python3
"""Small HTTPS fixture server for resumable Arsenal package downloads."""

import argparse
import http.server
import ssl
from pathlib import Path


class RangeHandler(http.server.BaseHTTPRequestHandler):
    package: bytes = b""
    log_path: Path

    def log_message(self, _format: str, *_args: object) -> None:
        return

    def _etag(self) -> str:
        return "new-etag" if self.path == "/changed.zip" else "test-etag"

    def _record(self, method: str) -> None:
        range_value = self.headers.get("Range", "none")
        with self.log_path.open("a", encoding="utf-8") as output:
            output.write(f"{method} {self.path} range={range_value}\n")

    def do_HEAD(self) -> None:
        self._record("HEAD")
        self.send_response(200)
        self.send_header("Content-Length", str(len(self.package)))
        self.send_header("ETag", f'"{self._etag()}"')
        self.end_headers()

    def do_GET(self) -> None:
        self._record("GET")
        etag = self._etag()
        if_match = self.headers.get("If-Match")
        if if_match and if_match != f'"{etag}"':
            self.send_response(412)
            self.end_headers()
            return
        range_value = self.headers.get("Range")
        if range_value and self.path != "/ignore-range.zip":
            try:
                offset = int(range_value.removeprefix("bytes=").split("-", 1)[0])
            except ValueError:
                self.send_response(416)
                self.end_headers()
                return
            body = self.package[offset:]
            self.send_response(206)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Content-Range", f"bytes {offset}-{len(self.package) - 1}/{len(self.package)}")
        else:
            body = self.package
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
        self.send_header("ETag", f'"{etag}"')
        self.end_headers()
        self.wfile.write(body)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package", required=True, type=Path)
    parser.add_argument("--certificate", required=True)
    parser.add_argument("--key", required=True)
    parser.add_argument("--port-file", required=True, type=Path)
    parser.add_argument("--log", required=True, type=Path)
    arguments = parser.parse_args()

    RangeHandler.package = arguments.package.read_bytes()
    RangeHandler.log_path = arguments.log
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), RangeHandler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(arguments.certificate, arguments.key)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    arguments.port_file.write_text(str(server.server_port), encoding="ascii")
    server.serve_forever()


if __name__ == "__main__":
    main()
