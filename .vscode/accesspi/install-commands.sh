#!/bin/sh
set -eu

mkdir -p "$HOME/.local/bin"

cat > "$HOME/.local/bin/access" <<'EOF'
#!/bin/sh
set -eu
cd $HOME/accesspi || exit 1
. .venv/bin/activate
python -B accesspi.py connect --skip-exosocket "$@"
python -B accesspi.py wait-web --ip 192.168.254.1 --timeout 120
exec startx $HOME/accesspi/run-kiosk.sh
EOF

cat > "$HOME/.local/bin/accesslite" <<'EOF'
#!/bin/sh
set -eu
cd $HOME/accesspi || exit 1
. .venv/bin/activate
python -B accesspi.py connect --skip-exosocket "$@"
python -B accesspi.py wait-web --ip 192.168.254.1 --timeout 120
exec env ACCESSPI_BROWSER=epiphany startx $HOME/accesspi/run-kiosk.sh
EOF

cat > "$HOME/.local/bin/accessdirect" <<'EOF'
#!/bin/sh
set -eu
cd $HOME/accesspi || exit 1
. .venv/bin/activate
python -B accesspi.py connect --skip-exosocket "$@"
python -B accesspi.py wait-web --ip 192.168.254.1 --timeout 120
exec startx $HOME/accesspi/run-direct.sh
EOF

cat > "$HOME/.local/bin/accessdirectlite" <<'EOF'
#!/bin/sh
set -eu
cd $HOME/accesspi || exit 1
. .venv/bin/activate
python -B accesspi.py connect --skip-exosocket "$@"
python -B accesspi.py wait-web --ip 192.168.254.1 --timeout 120
exec env ACCESSPI_BROWSER=epiphany startx $HOME/accesspi/run-direct.sh
EOF

cat > "$HOME/.local/bin/accesswebview" <<'EOF'
#!/bin/sh
set -eu
cd $HOME/accesspi || exit 1
. .venv/bin/activate
python -B accesspi.py connect --skip-exosocket "$@"
python -B accesspi.py wait-web --ip 192.168.254.1 --timeout 120
exec env ACCESSPI_WEBVIEW_URL=http://192.168.254.1/ startx $HOME/accesspi/run-webview.sh
EOF

cat > "$HOME/.local/bin/accesswebviewproxy" <<'EOF'
#!/bin/sh
set -eu
cd $HOME/accesspi || exit 1
. .venv/bin/activate
python -B accesspi.py connect --skip-exosocket "$@"
python -B accesspi.py wait-web --ip 192.168.254.1 --timeout 120
exec env ACCESSPI_WEBVIEW_URL=http://127.0.0.1:8080/ ACCESSPI_BROWSER=webview startx $HOME/accesspi/run-webview-proxy.sh
EOF

cat > "$HOME/.local/bin/kiosk" <<'EOF'
#!/bin/sh
cd $HOME/accesspi || exit 1
. .venv/bin/activate
exec startx $HOME/accesspi/run-kiosk.sh
EOF

cat > "$HOME/.local/bin/kiosklite" <<'EOF'
#!/bin/sh
cd $HOME/accesspi || exit 1
. .venv/bin/activate
exec env ACCESSPI_BROWSER=epiphany startx $HOME/accesspi/run-kiosk.sh
EOF

cat > "$HOME/.local/bin/proxy" <<'EOF'
#!/bin/sh
cd $HOME/accesspi || exit 1
. .venv/bin/activate
exec python -B accesspi.py proxy "$@"
EOF

cat > "$HOME/.local/bin/scanaccess" <<'EOF'
#!/bin/sh
cd $HOME/accesspi || exit 1
. .venv/bin/activate
exec python -B accesspi.py scan "$@"
EOF

cat > "$HOME/.local/bin/iphone" <<'EOF'
#!/bin/sh
sudo nmcli connection up iPhone
EOF

cat > "$HOME/.local/bin/statusaccess" <<'EOF'
#!/bin/sh
echo "Active connections:"
nmcli connection show --active
echo
echo "wlan0:"
ip -4 addr show wlan0
echo
echo "Controller HTTP:"
curl -I --connect-timeout 3 http://192.168.254.1/ || true
EOF

cat > "$HOME/.local/bin/reloadaccess" <<'EOF'
#!/bin/sh
if ! command -v xdotool >/dev/null 2>&1; then
  echo "xdotool is missing. Install it with: sudo apt install -y xdotool"
  exit 1
fi
DISPLAY=:0 xdotool search --onlyvisible --class chromium windowactivate --sync key ctrl+r
EOF

cat > "$HOME/.local/bin/clearaccessbrowser" <<'EOF'
#!/bin/sh
pkill -f chromium || true
pkill -f epiphany || true
rm -rf "$HOME/.config/accesspi/chromium-profile"
rm -rf "$HOME/.config/accesspi/epiphany-profile"
rm -rf "$HOME/.config/accesspi/chromium-direct-profile"
rm -rf "$HOME/.config/accesspi/epiphany-direct-profile"
echo "AccessPi browser profiles cleared."
EOF

cat > "$HOME/.local/bin/stopaccess" <<'EOF'
#!/bin/sh
pkill -f chromium || true
pkill -f epiphany || true
pkill -f access_webview.py || true
pkill -f openbox || true
pkill -f startx || true
pkill -f xinit || true
sudo nmcli connection delete "Access Controller" || true
sudo nmcli connection up iPhone || true
EOF

chmod +x \
  "$HOME/.local/bin/access" \
  "$HOME/.local/bin/accesslite" \
  "$HOME/.local/bin/accessdirect" \
  "$HOME/.local/bin/accessdirectlite" \
  "$HOME/.local/bin/accesswebview" \
  "$HOME/.local/bin/accesswebviewproxy" \
  "$HOME/.local/bin/kiosk" \
  "$HOME/.local/bin/kiosklite" \
  "$HOME/.local/bin/proxy" \
  "$HOME/.local/bin/scanaccess" \
  "$HOME/.local/bin/iphone" \
  "$HOME/.local/bin/statusaccess" \
  "$HOME/.local/bin/reloadaccess" \
  "$HOME/.local/bin/clearaccessbrowser" \
  "$HOME/.local/bin/stopaccess"

case ":$PATH:" in
  *":$HOME/.local/bin:"*) ;;
  *) echo 'export PATH="$HOME/.local/bin:$PATH"' >> "$HOME/.bashrc" ;;
esac

echo "Installed commands: access, accesslite, accessdirect, accessdirectlite, accesswebview, accesswebviewproxy, kiosk, kiosklite, proxy, scanaccess, iphone, statusaccess, reloadaccess, clearaccessbrowser, stopaccess"
