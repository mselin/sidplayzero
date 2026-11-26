# SPDX-License-Identifier: GPL-3.0-or-later
"""The real HttpClient and answers that do not match the request (SPEC-147, SPEC-150).

A partial archive is carried on with a `Range` request (SPEC-147). The real HttpClient keeps a 206 only
as the rest of the file from the byte it asked for: a 206 that starts elsewhere, one to a request that
asked for no range, and one that stops short of the size its Content-Range gives all fail it, and the
partial file is left as it was or as far as it is right. And the http://127.0.0.1 that a test build
accepts beside HTTPS (SPEC-150) is that host only, not one named after an `@`.

These use `spz_tool fetch` (tests/integration/test_hvsc.py describes it), with `--resume-from BYTE` to ask
for the rest of a file from that byte; it then also prints `resumed = yes | no`.
"""

import http.server
import os
import shutil
import ssl
import subprocess
import sys
import tempfile
import threading
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from testing import localserver  # noqa: E402

TOOL = Path(os.environ.get("SPZ_SIM_BUILD", ROOT / "build" / "simulator")) / "src" / "spz_tool"
BODY = bytes((i * 7 + 3) % 251 for i in range(1000))
CUT = 400       # the partial file holds this many bytes of BODY


def tool(*arguments):
    process = subprocess.run([str(TOOL), *[str(a) for a in arguments]], text=True, errors="replace",
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    values = {}
    for line in process.stdout.split("\n"):
        key, equals, value = line.partition(" = ")
        if equals:
            values[key.strip()] = value.strip()
    return process.returncode, values, process.stdout


class RangeServer:
    """An HTTPS server on the loopback address whose answers to a Range request are chosen by the path:

        /right     the rest of BODY from the byte asked for, as a server should
        /elsewhere a 206 that always starts at byte 0, whatever was asked
        /unasked   a 206 of bytes 100 to 199, to any request, with a range or without
        /short     the rest from the byte asked for, but only ten bytes of it, its Content-Range saying so
    """

    def __init__(self, work, host="127.0.0.1", plain=False):
        self.work = Path(work)
        self.host = host
        self.plain = plain
        self.log = []

    def url(self, path):
        return f"{'http' if self.plain else 'https'}://{self.host}:{self.port}{path}"

    def __enter__(self):
        server = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass

            def partial(self, first, last):
                self.send_response(206)
                self.send_header("Content-Range", f"bytes {first}-{last}/{len(BODY)}")
                self.send_header("Content-Length", str(last + 1 - first))
                self.end_headers()
                self.wfile.write(BODY[first:last + 1])

            def do_GET(self):
                asked = self.headers.get("Range", "")
                server.log.append(f"GET {self.path} [{asked}]")
                first = int(asked[len("bytes="):].split("-")[0]) if asked.startswith("bytes=") else None
                if self.path == "/unasked":
                    self.partial(100, 199)
                elif first is None:
                    self.send_response(200)
                    self.send_header("Content-Length", str(len(BODY)))
                    self.end_headers()
                    self.wfile.write(BODY)
                elif self.path == "/right":
                    self.partial(first, len(BODY) - 1)
                elif self.path == "/elsewhere":
                    self.partial(0, len(BODY) - 1)
                elif self.path == "/short":
                    self.partial(first, first + 9)
                else:
                    self.send_response(404)
                    self.end_headers()

        class Quiet(http.server.ThreadingHTTPServer):
            def handle_error(self, request, client_address):
                pass

        self.httpd = Quiet((self.host, 0), Handler)
        self.port = self.httpd.server_address[1]
        if not self.plain:
            ca, cert, key, _, _ = localserver.make_authority(self.work)
            self.authority = ca
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(cert, key)
            self.httpd.socket = context.wrap_socket(self.httpd.socket, server_side=True)
        threading.Thread(target=self.httpd.serve_forever, daemon=True).start()
        return self

    def __exit__(self, *_):
        self.httpd.shutdown()
        self.httpd.server_close()


class Case(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="spz-ranges-"))
        self.partial = self.work / "archive.7z"

    def tearDown(self):
        shutil.rmtree(self.work, ignore_errors=True)

    def cut(self):
        self.partial.write_bytes(BODY[:CUT])

    def fetch(self, server, path, *extra):
        status, values, output = tool("fetch", server.url(path), self.partial, "--limit", 1 << 20,
                                      "--ca", server.authority, *extra)
        self.assertEqual(status, 0, output)
        return values, output


class Ranges(Case):
    def test_the_rest_is_kept(self):
        """SPEC-147 SPEC-150 the rest of a partial file, asked for from its end and answered from there, makes the whole file"""
        self.cut()
        with RangeServer(self.work) as server:
            values, output = self.fetch(server, "/right", "--resume-from", CUT)
        self.assertEqual((values.get("failure"), values.get("status"), values.get("resumed")), ("none", "206", "yes"), output)
        self.assertEqual(self.partial.read_bytes(), BODY)
        self.assertEqual(server.log, [f"GET /right [bytes={CUT}-]"])

    def test_a_206_from_another_byte_is_refused(self):
        """SPEC-147 SPEC-150 a 206 whose Content-Range starts at another byte than the one asked for fails the request, and the partial file is left as it was"""
        self.cut()
        with RangeServer(self.work) as server:
            values, output = self.fetch(server, "/elsewhere", "--resume-from", CUT)
        self.assertEqual((values.get("failure"), values.get("status")), ("network", "206"), output)
        self.assertEqual(self.partial.read_bytes(), BODY[:CUT], "the partial file was changed")

    def test_a_206_to_a_request_for_the_whole_file_is_refused(self):
        """SPEC-147 SPEC-150 a 206 to a request that asked for no range fails the request, and no file is made of the piece"""
        with RangeServer(self.work) as server:
            values, output = self.fetch(server, "/unasked")
        self.assertEqual((values.get("failure"), values.get("status")), ("network", "206"), output)
        self.assertFalse(self.partial.exists() and self.partial.stat().st_size > 0,
                         "a piece of the file was kept as the whole of it")

    def test_a_rest_that_stops_short_fails(self):
        """SPEC-147 SPEC-150 a 206 that ends before the size its Content-Range gives fails the request, keeping the bytes that are right for a later try"""
        self.cut()
        with RangeServer(self.work) as server:
            values, output = self.fetch(server, "/short", "--resume-from", CUT)
        self.assertEqual((values.get("failure"), values.get("status")), ("network", "206"), output)
        self.assertEqual(self.partial.read_bytes(), BODY[:CUT + 10])


class LoopbackOnly(unittest.TestCase):
    def setUp(self):
        self.work = Path(tempfile.mkdtemp(prefix="spz-loopback-"))

    def tearDown(self):
        shutil.rmtree(self.work, ignore_errors=True)

    def test_a_user_name_of_127_0_0_1_is_refused(self):
        """SPEC-150 a test build's http://127.0.0.1 is that host only: http://127.0.0.1:x@another.host/ is refused, and nothing reaches the other host"""
        to = self.work / "fetched"
        with RangeServer(self.work, host="127.0.0.2", plain=True) as other:
            address = other.url("/right").replace("http://", "http://127.0.0.1:x@")
            status, values, output = tool("fetch", address, to, "--limit", 1 << 20)
            self.assertEqual(status, 0, output)
            self.assertEqual(values.get("failure"), "tls", output)
            self.assertEqual(other.log, [])
            address = other.url("/right").replace("http://", "http://127.0.0.1:1@")
            status, values, output = tool("fetch", address, to, "--limit", 1 << 20)
            self.assertEqual(values.get("failure"), "tls", output)
            self.assertEqual(other.log, [])
        self.assertFalse(to.exists())

    def test_127_0_0_1_with_a_port_is_still_accepted(self):
        """SPEC-150 a test build still accepts http://127.0.0.1 with a port and a path"""
        to = self.work / "fetched"
        with RangeServer(self.work, plain=True) as server:
            status, values, output = tool("fetch", server.url("/right"), to, "--limit", 1 << 20)
        self.assertEqual((status, values.get("failure")), (0, "none"), output)
        self.assertEqual(to.read_bytes(), BODY)


if __name__ == "__main__":
    unittest.main()
