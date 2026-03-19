#!/bin/bash
# Serve o2r files via Cloudflare tunnel and print a ready-to-use game URL.
# Usage: ./serve_o2r.sh [PlayerName]

set -euo pipefail

O2R_DIR="/tmp/otr-files/soh-nightly"
PORT=8787
GAME_URL="https://zalo.github.io/Shipwright/"
ROOM="soh-testroom"
PLAYER="${1:-Player1}"

# Verify files exist
if [ ! -f "$O2R_DIR/soh.o2r" ] || [ ! -f "$O2R_DIR/oot.o2r" ]; then
  echo "ERROR: soh.o2r and oot.o2r not found in $O2R_DIR" >&2
  exit 1
fi

# Kill any existing quick tunnels and file servers on our port
echo "Cleaning up old processes..." >&2
ps aux | grep 'cloudflared tunnel --config /dev/null' | grep -v grep | awk '{print $2}' | xargs kill 2>/dev/null || true
lsof -ti:$PORT | xargs kill 2>/dev/null || true
sleep 1

# Start CORS file server in background
python3 -c "
import http.server, os
os.chdir('$O2R_DIR')
class H(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'GET, OPTIONS')
        super().end_headers()
    def do_OPTIONS(self):
        self.send_response(200)
        self.end_headers()
    def log_message(self, fmt, *args):
        import sys; sys.stderr.write('[FileServer] %s\n' % (fmt % args))
http.server.HTTPServer(('0.0.0.0', $PORT), H).serve_forever()
" &
SERVER_PID=$!

# Wait for server to be ready
sleep 1
if ! kill -0 $SERVER_PID 2>/dev/null; then
  echo "ERROR: File server failed to start" >&2
  exit 1
fi

# Start cloudflare tunnel (--config /dev/null avoids named tunnel config)
# Use script -c to force PTY so cloudflared doesn't buffer output
TUNNEL_LOG=/tmp/cloudflared-o2r.log
: > "$TUNNEL_LOG"
script -qfc "cloudflared tunnel --config /dev/null --url http://127.0.0.1:$PORT 2>&1" "$TUNNEL_LOG" >/dev/null &
TUNNEL_PID=$!

# Wait for tunnel URL
echo "Starting Cloudflare tunnel..." >&2
TUNNEL_URL=""
for i in $(seq 1 60); do
  sleep 1
  TUNNEL_URL=$(sed 's/\x1b\[[0-9;]*m//g' "$TUNNEL_LOG" 2>/dev/null | grep -o 'https://[a-z0-9-]*\.trycloudflare\.com' | head -1 || true)
  if [ -n "$TUNNEL_URL" ]; then
    break
  fi
done

if [ -z "$TUNNEL_URL" ]; then
  echo "ERROR: Tunnel failed to start after 60s. Log:" >&2
  cat "$TUNNEL_LOG" >&2
  kill $SERVER_PID $TUNNEL_PID 2>/dev/null
  exit 1
fi

# Build game URL
LINK="${GAME_URL}#soh=${TUNNEL_URL}/soh.o2r&oot=${TUNNEL_URL}/oot.o2r&room=${ROOM}&name=${PLAYER}&color=FF0000&team=default"

echo ""
echo "======================================================================"
echo "  Tunnel:  $TUNNEL_URL"
echo "  Room:    $ROOM"
echo "  Player:  $PLAYER"
echo ""
echo "  $LINK"
echo "======================================================================"
echo ""
echo "Serving $O2R_DIR on port $PORT. Ctrl+C to stop."

# Cleanup on exit
cleanup() {
  echo "" >&2
  echo "Shutting down..." >&2
  kill $SERVER_PID $TUNNEL_PID 2>/dev/null
  exit 0
}
trap cleanup INT TERM

# Wait for either process to exit
wait $TUNNEL_PID $SERVER_PID 2>/dev/null
cleanup
