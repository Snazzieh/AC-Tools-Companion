#!/bin/sh
xset -dpms
xset s off
xset s noblank

openbox-session &
sleep 2

URL="${ACCESSPI_DIRECT_URL:-http://192.168.254.1/}"
BROWSER="${ACCESSPI_BROWSER:-chromium}"

if [ "$BROWSER" = "epiphany" ]; then
    PROFILE="$HOME/.config/accesspi/epiphany-direct-profile"
    pkill -f epiphany || true
    rm -rf "$PROFILE"
    mkdir -p "$PROFILE/config" "$PROFILE/cache" "$PROFILE/data"
    if command -v epiphany-browser >/dev/null 2>&1; then
        exec env \
            DBUS_SESSION_BUS_ADDRESS= \
            XDG_CONFIG_HOME="$PROFILE/config" \
            XDG_CACHE_HOME="$PROFILE/cache" \
            XDG_DATA_HOME="$PROFILE/data" \
            epiphany-browser "$URL"
    fi
    exec env \
        DBUS_SESSION_BUS_ADDRESS= \
        XDG_CONFIG_HOME="$PROFILE/config" \
        XDG_CACHE_HOME="$PROFILE/cache" \
        XDG_DATA_HOME="$PROFILE/data" \
        epiphany "$URL"
fi

PROFILE="$HOME/.config/accesspi/chromium-direct-profile"
mkdir -p "$PROFILE"

if command -v chromium-browser >/dev/null 2>&1; then
    CHROMIUM=chromium-browser
else
    CHROMIUM=chromium
fi

exec "$CHROMIUM" \
    --no-memcheck \
    --ozone-platform=x11 \
    --no-first-run \
    --no-default-browser-check \
    --disable-session-crashed-bubble \
    --disable-component-update \
    --disable-features=Translate,AutofillServerCommunication,OptimizationHints \
    --password-store=basic \
    --user-data-dir="$PROFILE" \
    --start-fullscreen \
    --disable-dev-shm-usage \
    --noerrdialogs \
    --disable-infobars \
    --disable-background-networking \
    --disable-background-timer-throttling \
    --disable-renderer-backgrounding \
    --disable-extensions \
    --window-size=1280,720 \
    --window-position=0,0 \
    --new-window \
    "$URL"
