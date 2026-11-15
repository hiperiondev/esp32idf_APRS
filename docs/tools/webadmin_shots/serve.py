import sys, os, http.server, socketserver
site = sys.argv[1]; port = int(sys.argv[2])
routes = {}
for line in open(os.path.join(site, 'manifest.tsv')):
    u, f, ct = line.rstrip('\n').split('\t')
    routes[u] = (os.path.join(site, f), ct)
class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, body, ct, code=200):
        self.send_response(code); self.send_header('Content-Type', ct); self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
    def do_GET(self):
        p = self.path.split('?')[0]
        if p == '/igate_traffic' and 'since=' in self.path and not self.path.endswith('since=0'):
            import json
            d = json.load(open(routes[p][0])); d['items'] = []
            return self.send(json.dumps(d).encode(), 'application/json')
        if p in routes:
            fn, ct = routes[p]; self.send(open(fn, 'rb').read(), ct)
        else: self.send(b'not found', 'text/plain', 404)
    def do_POST(self):
        n = int(self.headers.get('Content-Length') or 0); self.rfile.read(n)
        self.send(b'{}', 'application/json')
socketserver.TCPServer.allow_reuse_address = True
with socketserver.ThreadingTCPServer(('127.0.0.1', port), H) as s: s.serve_forever()
