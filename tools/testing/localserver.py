# SPDX-License-Identifier: GPL-3.0-or-later
"""A local HTTPS server for the tests (TF-053; SPEC-150).

It listens on the loopback address with a certificate signed by an authority made for the run, serves the
files of a folder, and has a few addresses that misbehave on purpose:

    /redirect-to-http/NAME   answers with a redirect to plain http
    /endless                 a body that never ends
    /status/CODE             answers with that status

Every request is logged as "METHOD PATH" with its Range and Cookie headers, for a test to read.
"""

import http.server
import shutil
import ssl
import subprocess
import threading
from pathlib import Path


def make_authority(folder):
    """Makes a certificate authority and a certificate for 127.0.0.1 signed by it. Returns their paths."""
    folder = Path(folder)
    if shutil.which("openssl") is None:
        raise RuntimeError("openssl is needed for the local server")

    def openssl(*arguments):
        subprocess.run(["openssl", *arguments], cwd=folder, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    openssl("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", "ca.key", "-out", "ca.pem", "-days", "2",
            "-subj", "/CN=SidPlayZero test authority")
    openssl("req", "-newkey", "rsa:2048", "-nodes", "-keyout", "server.key", "-out", "server.csr", "-subj", "/CN=127.0.0.1")
    (folder / "san.cnf").write_text("subjectAltName=IP:127.0.0.1,DNS:localhost\n")
    openssl("x509", "-req", "-in", "server.csr", "-CA", "ca.pem", "-CAkey", "ca.key", "-CAcreateserial",
            "-out", "server.pem", "-days", "2", "-extfile", "san.cnf")
    # A second certificate that nobody signed, for the test that it is refused.
    openssl("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", "self.key", "-out", "self.pem", "-days", "2",
            "-subj", "/CN=127.0.0.1", "-addext", "subjectAltName=IP:127.0.0.1")
    return folder / "ca.pem", folder / "server.pem", folder / "server.key", folder / "self.pem", folder / "self.key"


class LocalServer:
    """with LocalServer(files_folder, work_folder) as server: server.url("/a.sid"), server.authority, server.log"""

    def __init__(self, files, work, self_signed=False):
        self.files = Path(files)
        self.work = Path(work)
        self.self_signed = self_signed
        self.log = []
        self.authority = None

    def url(self, path):
        return f"https://127.0.0.1:{self.port}{path}"

    def __enter__(self):
        ca, cert, key, self_cert, self_key = make_authority(self.work)
        self.authority = ca
        server = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass

            def do_GET(self):
                entry = f"GET {self.path}"
                for name in ("Range", "Cookie", "User-Agent"):
                    if self.headers.get(name):
                        entry += f" [{name}: {self.headers[name]}]"
                server.log.append(entry)
                if self.path.startswith("/redirect-to-http/"):
                    self.send_response(302)
                    self.send_header("Location", "http://127.0.0.1:1/" + self.path.rsplit("/", 1)[1])
                    self.end_headers()
                elif self.path == "/endless":
                    self.send_response(200)
                    self.end_headers()
                    try:
                        while True:
                            self.wfile.write(b"\0" * 65536)
                    except OSError:
                        pass
                elif self.path.startswith("/status/"):
                    self.send_response(int(self.path.rsplit("/", 1)[1]))
                    self.end_headers()
                else:
                    target = (server.files / self.path.lstrip("/")).resolve()
                    if server.files.resolve() not in target.parents or not target.is_file():
                        self.send_response(404)
                        self.end_headers()
                        return
                    body = target.read_bytes()
                    self.send_response(200)
                    self.send_header("Content-Length", str(len(body)))
                    self.end_headers()
                    self.wfile.write(body)

        class Quiet(http.server.ThreadingHTTPServer):
            def handle_error(self, request, client_address):
                pass    # a client that hangs up, or refuses the certificate, is what some tests are about

        self.httpd = Quiet(("127.0.0.1", 0), Handler)
        self.port = self.httpd.server_address[1]
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        if self.self_signed:
            context.load_cert_chain(self_cert, self_key)
        else:
            context.load_cert_chain(cert, key)
        self.httpd.socket = context.wrap_socket(self.httpd.socket, server_side=True)
        self.thread = threading.Thread(target=self.httpd.serve_forever, daemon=True)
        self.thread.start()
        return self

    def __exit__(self, *_):
        self.httpd.shutdown()
        self.httpd.server_close()
