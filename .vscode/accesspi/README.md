# AccessPi

Lightweight Raspberry Pi runner for Systemair Access controllers.

## First run on the Pi

```bash
cd ~/accesspi
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt
chmod +x run-kiosk.sh install-commands.sh
./install-commands.sh
python accesspi.py scan
python accesspi.py connect
```

`connect` scans for the strongest Access controller, reads its hotspot
credentials over BLE, connects the Pi to the controller Wi-Fi with
NetworkManager, verifies EXOsocket, and prints the controller WebUI URL.
The controller hotspot SSID/password can change between sessions, so use
`connect` to re-read credentials instead of reusing an old Wi-Fi profile.

Open the WebUI from the Pi:

```bash
python accesspi.py launch --address AA:BB:CC:DD:EE:FF
```

Or use a specific controller address:

```bash
python accesspi.py connect --address AA:BB:CC:DD:EE:FF
```

After `install-commands.sh`, the short commands are:

```bash
access
accesslite
accessdirect
accessdirectlite
accesswebview
accesswebviewproxy
kiosk
kiosklite
proxy
scanaccess
iphone
statusaccess
reloadaccess
clearaccessbrowser
stopaccess
```

Command summary:

- `access`: BLE connect, join Access Controller Wi-Fi, wait for WebUI, then start Chromium.
- `accesslite`: Same as `access`, but starts Epiphany/GNOME Web. Requires `sudo apt install -y epiphany-browser`.
- `accessdirect`: Same as `access`, but opens `http://192.168.254.1/` directly without the local proxy.
- `accessdirectlite`: Same as `accessdirect`, but uses Epiphany/GNOME Web.
- `accesswebview`: Same as `accessdirect`, but uses a minimal GTK/WebKit WebView wrapper.
- `accesswebviewproxy`: Same as `accesswebview`, but via the local proxy on `http://127.0.0.1:8080`.
- `kiosk`: Start only the local proxy and Chromium. Use after the Pi is already on Access Wi-Fi.
- `kiosklite`: Start only the local proxy and Epiphany/GNOME Web.
- `proxy`: Start only the local proxy on `http://127.0.0.1:8080`.
- `scanaccess`: Scan for Access controllers over BLE.
- `iphone`: Reconnect Wi-Fi to the saved iPhone hotspot profile.
- `statusaccess`: Show active Wi-Fi/IP and test controller HTTP.
- `reloadaccess`: Send Ctrl+R to the Chromium window. Requires `sudo apt install -y xdotool`.
- `clearaccessbrowser`: Delete the saved Chromium profile/cache if the browser gets stuck.
- `stopaccess`: Close Chromium/Openbox, delete Access Controller profile, and reconnect to iPhone.

From Windows PowerShell, copy updates to the Pi with:

```powershell
.\accesspi\update-pi.ps1
```

For the minimal WebView wrapper, install one of the WebKit binding sets on the Pi:

```bash
sudo apt install -y python3-gi gir1.2-gtk-4.0 gir1.2-webkit-6.0
```

If that package set is unavailable, try:

```bash
sudo apt install -y python3-gi gir1.2-gtk-3.0 gir1.2-webkit2-4.1
```

For debugging, run only the local WebUI proxy:

```bash
python -B accesspi.py proxy
curl -v http://127.0.0.1:8080/
```
