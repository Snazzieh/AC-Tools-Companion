#!/bin/sh
xset -dpms
xset s off
xset s noblank

openbox-session &
sleep 2

URL="${ACCESSPI_WEBVIEW_URL:-http://127.0.0.1:8080/}"

cd /home/s/accesspi || exit 1
. .venv/bin/activate

python -B accesspi.py proxy --ip 192.168.254.1 --port 8080 &
PROXY_PID="$!"

cleanup() {
    kill "$PROXY_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

sleep 2
python3 /home/s/accesspi/access_webview.py "$URL"
