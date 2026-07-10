#!/bin/sh
xset -dpms
xset s off
xset s noblank

openbox-session &
sleep 2

cd /home/s/accesspi || exit 1
. .venv/bin/activate

if command -v dbus-run-session >/dev/null 2>&1; then
    exec dbus-run-session -- python -B accesspi.py kiosk
fi

exec python -B accesspi.py kiosk
