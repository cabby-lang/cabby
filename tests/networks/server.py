# A small web server for run_networks_tests.sh: says back what it was sent,
# answers with any status asked, and gives big answers for progress.
#   /echo        what came: the method, some headers, and the body (JSON)
#   /status/N    status N, with "status N" as the answer
#   /big?n=N     N bytes, with their length said first
#   /moved       a redirect to /echo
import json, sys
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def answer(self, code, body, kind="text/plain", extra=None):
        data = body if isinstance(body, bytes) else body.encode()
        self.send_response(code)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("X-Cabby-Test", "yes")
        for k, v in (extra or {}).items():
            self.send_header(k, v)
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)

    def any(self):
        u = urlparse(self.path)
        n = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(n) if n else b""
        if u.path == "/echo":
            said = {"method": self.command, "body": body.decode("utf-8", "replace"),
                    "type": self.headers.get("Content-Type", ""),
                    "auth": self.headers.get("Authorization", ""),
                    "agent": self.headers.get("User-Agent", "")}
            self.answer(200, json.dumps(said, sort_keys=True), "application/json")
        elif u.path.startswith("/status/"):
            code = int(u.path.split("/")[2])
            self.answer(code, f"status {code}")
        elif u.path == "/big":
            n = int(parse_qs(u.query).get("n", ["0"])[0])
            self.answer(200, bytes(i % 251 for i in range(n)), "application/octet-stream")
        elif u.path == "/moved":
            self.send_response(302)
            self.send_header("Location", "/echo")
            self.send_header("Content-Length", "0")
            self.end_headers()
        else:
            self.answer(404, "not here")

    do_GET = do_POST = do_PUT = do_DELETE = do_PATCH = do_HEAD = any

server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
print(server.server_address[1], flush=True)
server.serve_forever()
