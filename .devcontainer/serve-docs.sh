#!/usr/bin/env bash
# Sirve docs/docsV3 en http://127.0.0.1:8000 sin cache (ni Last-Modified),
# para que los cambios en los .md/.js se vean al recargar.
set -euo pipefail

cd /workspaces/nova64

if pgrep -f 'docsV3-server' >/dev/null 2>&1; then
  echo "El servidor de docs ya esta en marcha en :8000"
  exit 0
fi

nohup python3 -c 'exec("# docsV3-server\nimport functools,http.server as s\nclass H(s.SimpleHTTPRequestHandler):\n def end_headers(self):\n  self.send_header(\"Cache-Control\",\"no-store\")\n  super().end_headers()\n def send_header(self,k,v):\n  if k!=\"Last-Modified\": super().send_header(k,v)\ns.test(HandlerClass=functools.partial(H,directory=\"docs/docsV3\"),port=8000,bind=\"127.0.0.1\")")' \
  > /tmp/docsV3-server.log 2>&1 &

disown
echo "Servidor de docs arrancado en http://127.0.0.1:8000 (log: /tmp/docsV3-server.log)"
