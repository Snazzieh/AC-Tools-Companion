#!/bin/sh
xset -dpms
xset s off
xset s noblank

openbox-session &
sleep 2

URL="${ACCESSPI_WEBVIEW_URL:-http://192.168.254.1/}"

cd /home/s/accesspi || exit 1
exec python3 /home/s/accesspi/access_webview.py "$URL"
