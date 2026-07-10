import argparse
import asyncio
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from urllib.error import URLError
from urllib.request import Request, urlopen

from aiohttp import ClientSession, WSMsgType, web
import msgpack
import websockets
from bleak import BleakClient, BleakScanner
from Crypto.Cipher import AES
from Crypto.Hash import SHA256


SYSTEMAIR_MANUFACTURER_ID = 2447
UART_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
UART_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"
HOTSPOT_SECRET = b"Can I haz cheeseburger for lunch?"

DATA_AUTH = 3
DATA_HOTSPOT = 4
SLIP_END = 0xC0
SLIP_ESC = 0xDB
SLIP_ESC_END = 0xDC
SLIP_ESC_ESC = 0xDD

STATE_DIR = Path.home() / ".config" / "accesspi"
STATE_FILE = STATE_DIR / "state.json"
WEBSOCKET_PATCH = """
<script>
(function() {
    const NativeWebSocket = window.WebSocket;

    function fixUrl(url) {
        try {
            if (typeof url === "string" && url.startsWith("ws://")) {
                const parsed = new URL(url);
                const proto = window.location.protocol === "https:" ? "wss://" : "ws://";
                return proto + window.location.hostname + ":" + window.location.port + parsed.pathname + parsed.search + parsed.hash;
            }
        } catch (e) {
            console.log("[AccessPi] WebSocket patch error:", e);
        }
        return url;
    }

    window.WebSocket = function(url, protocols) {
        url = fixUrl(url);
        let ws;
        if (protocols !== undefined) {
            ws = new NativeWebSocket(url, protocols);
        } else {
            ws = new NativeWebSocket(url);
        }
        return ws;
    };

    window.WebSocket.prototype = NativeWebSocket.prototype;
    window.WebSocket.CONNECTING = NativeWebSocket.CONNECTING;
    window.WebSocket.OPEN = NativeWebSocket.OPEN;
    window.WebSocket.CLOSING = NativeWebSocket.CLOSING;
    window.WebSocket.CLOSED = NativeWebSocket.CLOSED;
})();
</script>
"""


class AccessPiError(Exception):
    pass


@dataclass
class Controller:
    name: str
    address: str
    rssi: int


@dataclass
class HotspotCredentials:
    ssid: str
    password: str
    ip: str = "192.168.10.1"


def clean_text(text):
    if not text:
        return None
    text = text.replace("\x00", "").strip()
    return text or None


def extract_name_piece(data):
    raw = bytes(data)
    parts = []
    current = bytearray()

    for byte in raw:
        if 32 <= byte <= 126:
            current.append(byte)
        else:
            if len(current) >= 2:
                parts.append(bytes(current))
            current = bytearray()

    if len(current) >= 2:
        parts.append(bytes(current))

    if not parts:
        return None

    return clean_text(max(parts, key=len).decode("utf-8", errors="ignore"))


async def scan_controllers(seconds=4.0):
    found = {}

    def callback(device, adv):
        if SYSTEMAIR_MANUFACTURER_ID not in adv.manufacturer_data:
            return

        address = device.address
        piece = extract_name_piece(adv.manufacturer_data[SYSTEMAIR_MANUFACTURER_ID])

        found.setdefault(address, {"address": address, "rssi": adv.rssi, "parts": []})
        found[address]["rssi"] = adv.rssi
        if piece and piece not in found[address]["parts"]:
            found[address]["parts"].append(piece)

    scanner = BleakScanner(callback)
    await scanner.start()
    await asyncio.sleep(seconds)
    await scanner.stop()

    controllers = []
    for item in found.values():
        name = "".join(item["parts"]).replace("  ", " ").strip()
        if name:
            controllers.append(Controller(name=name, address=item["address"], rssi=item["rssi"]))

    controllers.sort(key=lambda controller: controller.rssi, reverse=True)
    return controllers


def slip_encode(raw):
    out = bytearray()
    for byte in raw:
        if byte == SLIP_END:
            out.extend([SLIP_ESC, SLIP_ESC_END])
        elif byte == SLIP_ESC:
            out.extend([SLIP_ESC, SLIP_ESC_ESC])
        else:
            out.append(byte)
    out.append(SLIP_END)
    return bytes(out)


def slip_decode(data):
    raw = bytes(data)
    if raw.endswith(bytes([SLIP_END])):
        raw = raw[:-1]

    out = bytearray()
    index = 0
    while index < len(raw):
        byte = raw[index]
        if byte == SLIP_ESC and index + 1 < len(raw):
            next_byte = raw[index + 1]
            if next_byte == SLIP_ESC_END:
                out.append(SLIP_END)
            elif next_byte == SLIP_ESC_ESC:
                out.append(SLIP_ESC)
            else:
                out.append(next_byte)
            index += 2
            continue
        out.append(byte)
        index += 1
    return bytes(out)


def make_frame(data_type, payload=b"", frame_id=0):
    return slip_encode(bytes([data_type, frame_id]) + payload)


def parse_frame(data):
    raw = slip_decode(data)
    if len(raw) < 2:
        return None, None, b""
    return raw[0] & 0x3F, raw[1], raw[2:]


class HotspotGetter:
    def __init__(self):
        self.client = None
        self.done = asyncio.Event()
        self.sent_auth_response = False
        self.sent_hotspot = False
        self.hotspot = None

    async def send_auth_init(self):
        await self.client.write_gatt_char(UART_RX_UUID, make_frame(DATA_AUTH, b"\x00"), response=False)

    async def send_auth_response(self, challenge):
        if self.sent_auth_response:
            return
        self.sent_auth_response = True
        digest = hashlib.sha256(HOTSPOT_SECRET + challenge).digest()
        await self.client.write_gatt_char(UART_RX_UUID, make_frame(DATA_AUTH, digest), response=False)

    async def send_hotspot(self):
        if self.sent_hotspot:
            return
        self.sent_hotspot = True
        await self.client.write_gatt_char(UART_RX_UUID, make_frame(DATA_HOTSPOT, b"\x00"), response=False)

    def notify(self, _sender, data):
        data_type, _frame_id, payload = parse_frame(data)

        if data_type == DATA_AUTH:
            if payload == b"\x01":
                asyncio.create_task(self.send_hotspot())
            elif len(payload) == 32:
                asyncio.create_task(self.send_auth_response(payload))
            return

        if data_type == DATA_HOTSPOT:
            try:
                decoded = msgpack.unpackb(payload, raw=False)
                self.hotspot = HotspotCredentials(
                    ssid=decoded.get("SSID"),
                    password=decoded.get("Password"),
                    ip=decoded.get("ip") or "192.168.10.1",
                )
            except Exception:
                self.hotspot = None
            self.done.set()


async def get_hotspot_credentials(address):
    getter = HotspotGetter()

    async with BleakClient(address) as client:
        getter.client = client
        await client.start_notify(UART_TX_UUID, getter.notify)
        await asyncio.sleep(0.5)
        await getter.send_auth_init()
        await asyncio.wait_for(getter.done.wait(), timeout=25)

    if not getter.hotspot or not getter.hotspot.ssid or not getter.hotspot.password:
        raise AccessPiError("Could not read Wi-Fi credentials from controller.")

    return getter.hotspot


def run(command, check=True):
    result = subprocess.run(command, capture_output=True, text=True)
    if check and result.returncode != 0:
        details = (result.stderr or result.stdout or "").strip()
        command_text = " ".join(command)
        raise AccessPiError(f"Command failed ({result.returncode}): {command_text}\n{details}")
    return result


def require_command(name):
    if not shutil.which(name):
        raise AccessPiError(f"Missing required command: {name}")


def connect_linux_wifi(credentials):
    require_command("nmcli")
    run(["sudo", "nmcli", "radio", "wifi", "on"], check=False)
    run(["sudo", "nmcli", "connection", "delete", "Access Controller"], check=False)
    run(["sudo", "nmcli", "device", "wifi", "rescan"], check=False)
    run(
        [
            "sudo",
            "nmcli",
            "connection",
            "add",
            "type",
            "wifi",
            "ifname",
            "wlan0",
            "con-name",
            "Access Controller",
            "ssid",
            credentials.ssid,
        ]
    )
    run(["sudo", "nmcli", "connection", "modify", "Access Controller", "wifi.hidden", "yes"])
    run(["sudo", "nmcli", "connection", "modify", "Access Controller", "wifi-sec.key-mgmt", "wpa-psk"])
    run(["sudo", "nmcli", "connection", "modify", "Access Controller", "wifi-sec.psk", credentials.password])
    run(["sudo", "nmcli", "connection", "up", "Access Controller"])


def wait_for_controller_ip(controller_ip, timeout=30):
    deadline = time.time() + timeout
    while time.time() < deadline:
        result = run(["ip", "-4", "addr"], check=False)
        if controller_ip.rsplit(".", 1)[0] in result.stdout:
            return True
        time.sleep(1)
    return False


def wait_for_http(url, timeout=120, interval=3):
    deadline = time.time() + timeout
    last_error = ""

    while time.time() < deadline:
        try:
            request = Request(url, method="HEAD")
            with urlopen(request, timeout=5) as response:
                if 200 <= response.status < 400:
                    return True
        except Exception as exc:
            last_error = str(exc)

        print(f"Waiting for controller WebUI: {url}")
        time.sleep(interval)

    raise AccessPiError(f"Controller WebUI did not become ready: {url}\nLast error: {last_error}")


class ExoSocketClient:
    def __init__(self, ip="192.168.10.1", context=12345, timeout=60):
        self.ip = ip
        self.context = context
        self.timeout = timeout
        self.url = f"ws://{ip}"
        self.ws = None
        self.encryptor = None
        self.decryptor = None
        self.send_pcbc_acc = None
        self.receive_pcbc_acc = None
        self.crypto_on = False

    async def connect(self):
        self.ws = await websockets.connect(
            self.url,
            subprotocols=["EXOsocket"],
            ping_interval=None,
            close_timeout=2,
            max_size=None,
            compression=None,
        )
        await self.version_offer()

    async def version_offer(self):
        await self.send({"method": "versionOffer", "params": {"version": 1, "featureLevel": 0, "capabilities": 0}})
        reply = await self.receive()
        if reply.get("method") != "versionAck":
            raise AccessPiError(f"Expected versionAck, got: {reply}")

    async def close(self):
        if self.ws:
            await self.ws.close()
            self.ws = None

    async def send_raw(self, data):
        await asyncio.wait_for(self.ws.send(data), timeout=self.timeout)

    async def receive_raw(self):
        data = await asyncio.wait_for(self.ws.recv(), timeout=self.timeout)
        return data.encode("utf-8") if isinstance(data, str) else data

    async def send(self, payload):
        text = json.dumps(payload, separators=(",", ":")) if isinstance(payload, dict) else str(payload)
        encoded = text.encode("utf-8")
        max_payload_size = 65536 - 16 - (16 if self.crypto_on else 0)

        for index in range(0, len(encoded), max_payload_size):
            chunk = encoded[index:index + max_payload_size]
            if self.crypto_on:
                await asyncio.wait_for(self.ws.send(self.encrypt_data(chunk)), timeout=self.timeout)
            else:
                await asyncio.wait_for(self.ws.send(chunk.decode("utf-8", errors="ignore")), timeout=self.timeout)

    async def receive(self):
        data = await asyncio.wait_for(self.ws.recv(), timeout=self.timeout)
        if self.crypto_on:
            text = self.decrypt_data(data.encode("utf-8") if isinstance(data, str) else data)
        else:
            text = data.decode("utf-8", errors="ignore") if isinstance(data, bytes) else data
        return json.loads(text)

    def _u32_le(self, data, offset):
        return struct.unpack_from("<I", data, offset)[0]

    def _put_u32_le(self, data, offset, value):
        data[offset:offset + 4] = struct.pack("<I", value & 0xFFFFFFFF)

    async def login(self, user, password):
        nonce_bytes = os.urandom(4)
        nonce_half1 = struct.unpack_from("<H", nonce_bytes, 0)[0]
        nonce_half2 = struct.unpack_from("<H", nonce_bytes, 2)[0]

        await self.send({"method": "getChallenge", "params": {"clientNonce1": nonce_half1}})
        challenge = await self.receive()
        if challenge.get("method") != "authChallenge":
            raise AccessPiError(f"Unexpected challenge reply: {challenge}")

        server_nonce = int(challenge["params"]["serverNonce"])
        login_bytes = (user.lower() + "\0" + password + "\0").encode("utf-8")
        digest = bytearray(SHA256.new(login_bytes).digest())
        self._put_u32_le(digest, 20, self._u32_le(digest, 20) ^ nonce_half1)
        self._put_u32_le(digest, 24, self._u32_le(digest, 24) ^ server_nonce)
        self._put_u32_le(digest, 28, self._u32_le(digest, 28) ^ nonce_half2)

        for index in range(16):
            digest[index] ^= digest[index + 16]

        cipher = AES.new(bytes(digest[:16]), AES.MODE_ECB)
        encrypted_data = cipher.encrypt(bytes(digest[16:32]))
        challenge_response = struct.unpack_from("<I", encrypted_data, 0)[0]
        expected_confirmation = struct.unpack_from("<I", encrypted_data, 4)[0]

        await self.send(
            {
                "method": "authenticate",
                "params": {
                    "user": user,
                    "clientNonce2": nonce_half2,
                    "challengeResponse": challenge_response,
                },
            }
        )
        auth_reply = await self.receive()
        params = auth_reply.get("params", {})

        if auth_reply.get("method") != "authenticateReply":
            raise AccessPiError(f"Unexpected auth reply: {auth_reply}")
        if int(params.get("userLimited", 0)) == 1:
            raise AccessPiError("User limited / default password must be changed.")
        if int(params.get("confirmation", -1)) != expected_confirmation:
            raise AccessPiError("Authentication confirmation mismatch.")

        self.encryptor = cipher
        self.decryptor = cipher
        out_iv = os.urandom(16)
        await self.send_raw(out_iv)
        self.send_pcbc_acc = bytearray(out_iv)
        in_iv = await self.receive_raw()
        if len(in_iv) != 16:
            raise AccessPiError(f"Invalid receive IV length: {len(in_iv)}")
        self.receive_pcbc_acc = bytearray(in_iv)
        self.crypto_on = True
        return await self.receive()

    def encrypt_data(self, msg):
        padded_len = ((len(msg) + 16) // 16) * 16
        data = bytearray(padded_len)
        data[-16:] = os.urandom(16)
        data[:len(msg)] = msg
        data[-1] = (data[-1] & 0xF0) | (len(msg) % 16)
        out = bytearray(padded_len)

        for block_start in range(0, padded_len, 16):
            plain_block = data[block_start:block_start + 16]
            for index in range(16):
                self.send_pcbc_acc[index] ^= plain_block[index]
            cipher_block = self.encryptor.encrypt(bytes(self.send_pcbc_acc))
            for index in range(16):
                self.send_pcbc_acc[index] = cipher_block[index] ^ plain_block[index]
                out[block_start + index] = cipher_block[index]
        return bytes(out)

    def decrypt_data(self, response):
        out = bytearray(len(response))
        for block_start in range(0, len(response), 16):
            cipher_block = response[block_start:block_start + 16]
            decrypted = self.decryptor.decrypt(cipher_block)
            plain_block = bytearray(16)
            for index in range(16):
                plain_block[index] = decrypted[index] ^ self.receive_pcbc_acc[index]
            for index in range(16):
                self.receive_pcbc_acc[index] = cipher_block[index] ^ plain_block[index]
                out[block_start + index] = plain_block[index]

        strip_len = 16 - (out[-1] & 0x0F)
        return out[:len(out) - strip_len].decode("utf-8", errors="ignore")


async def verify_exosocket(ip):
    client = ExoSocketClient(ip=ip, timeout=15)
    try:
        await client.connect()
        return True
    finally:
        await client.close()


def save_state(**state):
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    STATE_FILE.write_text(json.dumps(state, indent=2), encoding="utf-8")


def load_state():
    if not STATE_FILE.exists():
        return {}
    return json.loads(STATE_FILE.read_text(encoding="utf-8"))


async def command_scan(args):
    controllers = await scan_controllers(args.seconds)
    if not controllers:
        print("No Access controllers found.")
        return 1

    for index, controller in enumerate(controllers, start=1):
        print(f"{index}. {controller.name}  {controller.address}  RSSI {controller.rssi}")
    return 0


async def command_connect(args):
    address = args.address
    controller = None

    if not address:
        controllers = await scan_controllers(args.seconds)
        if not controllers:
            raise AccessPiError("No Access controllers found.")
        controller = controllers[0]
        address = controller.address
        print(f"Selected: {controller.name} ({controller.address}, RSSI {controller.rssi})")

    print("Reading controller hotspot credentials over BLE...")
    credentials = await get_hotspot_credentials(address)
    print(f"Controller Wi-Fi: {credentials.ssid}  controller IP: {credentials.ip}")

    if args.no_wifi:
        print("Skipping Wi-Fi connect because --no-wifi was set.")
    else:
        print("Connecting Pi Wi-Fi to controller hotspot...")
        connect_linux_wifi(credentials)
        wait_for_controller_ip(credentials.ip, timeout=30)

    if not args.skip_exosocket:
        print("Verifying EXOsocket...")
        await verify_exosocket(credentials.ip)

    web_url = f"http://{credentials.ip}"
    save_state(
        controller=asdict(controller) if controller else {"address": address},
        hotspot=asdict(credentials),
        web_url=web_url,
    )
    print(f"Ready: {web_url}")
    return 0


async def command_wait_web(args):
    state = load_state()
    ip = args.ip or state.get("hotspot", {}).get("ip") or "192.168.254.1"
    wait_for_http(f"http://{ip}/", timeout=args.timeout, interval=args.interval)
    print(f"WebUI ready: http://{ip}/")
    return 0


async def command_check(args):
    state = load_state()
    ip = args.ip or state.get("hotspot", {}).get("ip") or "192.168.10.1"
    await verify_exosocket(ip)
    print(f"EXOsocket OK: ws://{ip}")
    return 0


async def command_open(args):
    state = load_state()
    url = args.url or state.get("web_url")
    if not url:
        raise AccessPiError("No WebUI URL saved yet. Run connect first.")

    browser = shutil.which("chromium-browser") or shutil.which("chromium") or shutil.which("xdg-open")
    if not browser:
        print(url)
        raise AccessPiError("No browser launcher found. Install chromium-browser or use the printed URL.")

    if Path(browser).name.startswith("chromium"):
        subprocess.Popen([browser, "--kiosk" if args.kiosk else "--new-window", url])
    else:
        subprocess.Popen([browser, url])

    print(f"Opened {url}")
    return 0


def inject_websocket_patch(body):
    marker = b"</head>"
    patch = WEBSOCKET_PATCH.encode("utf-8")
    index = body.lower().find(marker)
    if index >= 0:
        return body[:index] + patch + body[index:]
    return patch + body


def content_type_for_path(path, upstream_content_type):
    lower_path = path.lower()
    if lower_path.endswith(".css"):
        return "text/css", None
    if lower_path.endswith(".js"):
        return "application/javascript", None
    if lower_path.endswith(".html") or lower_path == "/" or "text/html" in upstream_content_type.lower():
        return "text/html", "UTF-8"
    return upstream_content_type or "application/octet-stream", None


async def websocket_proxy(request):
    controller_ip = request.app["controller_ip"]
    upstream_url = f"ws://{controller_ip}{request.path_qs}"
    protocols = request.headers.getall("Sec-WebSocket-Protocol", [])

    downstream = web.WebSocketResponse(protocols=protocols)
    await downstream.prepare(request)

    async with request.app["client"].ws_connect(
        upstream_url,
        protocols=protocols or None,
        max_msg_size=0,
        compress=0,
        heartbeat=None,
    ) as upstream:
        async def client_to_controller():
            async for msg in downstream:
                if msg.type == WSMsgType.TEXT:
                    await upstream.send_str(msg.data)
                elif msg.type == WSMsgType.BINARY:
                    await upstream.send_bytes(msg.data)
                elif msg.type == WSMsgType.CLOSE:
                    await upstream.close()

        async def controller_to_client():
            async for msg in upstream:
                if msg.type == WSMsgType.TEXT:
                    await downstream.send_str(msg.data)
                elif msg.type == WSMsgType.BINARY:
                    await downstream.send_bytes(msg.data)
                elif msg.type == WSMsgType.CLOSE:
                    await downstream.close()

        done, pending = await asyncio.wait(
            [asyncio.create_task(client_to_controller()), asyncio.create_task(controller_to_client())],
            return_when=asyncio.FIRST_COMPLETED,
        )
        for task in pending:
            task.cancel()
        for task in done:
            task.result()

    return downstream


async def http_proxy(request):
    try:
        if request.headers.get("Upgrade", "").lower() == "websocket":
            return await websocket_proxy(request)

        controller_ip = request.app["controller_ip"]
        upstream_url = f"http://{controller_ip}{request.path_qs}"
        headers = {
            key: value
            for key, value in request.headers.items()
            if key.lower() not in {"host", "accept-encoding", "content-length", "connection"}
        }
        body = await request.read()

        print(f"Proxy {request.method} {request.path_qs} -> {upstream_url}", flush=True)
        async with request.app["client"].request(
            request.method,
            upstream_url,
            headers=headers,
            data=body if body else None,
            allow_redirects=False,
        ) as upstream:
            response_body = await upstream.read()
            response_headers = {
                key: value
                for key, value in upstream.headers.items()
                if key.lower() not in {"content-length", "content-encoding", "transfer-encoding", "connection", "content-type"}
            }

            content_type = upstream.headers.get("Content-Type", "")
            if "text/html" in content_type.lower():
                response_body = inject_websocket_patch(response_body)
            response_content_type, response_charset = content_type_for_path(request.path, content_type)

            return web.Response(
                status=upstream.status,
                headers=response_headers,
                body=response_body,
                content_type=response_content_type,
                charset=response_charset,
            )
    except Exception as exc:
        print(f"Proxy error for {request.method} {request.path_qs}: {exc!r}", flush=True)
        return web.Response(
            status=502,
            text=f"AccessPi proxy error:\n{type(exc).__name__}: {exc}\n",
            content_type="text/plain",
        )


async def start_proxy(controller_ip, host, port):
    app = web.Application()
    app["controller_ip"] = controller_ip
    app["client"] = ClientSession(auto_decompress=True)
    app.router.add_route("*", "/{tail:.*}", http_proxy)

    runner = web.AppRunner(app)
    await runner.setup()
    site = web.TCPSite(runner, host, port)
    await site.start()
    return runner


async def command_kiosk(args):
    state = load_state()
    controller_ip = args.ip or state.get("hotspot", {}).get("ip") or "192.168.254.1"
    browser_name = args.browser or os.environ.get("ACCESSPI_BROWSER", "chromium")

    print(f"Starting WebUI proxy for controller {controller_ip} on http://127.0.0.1:{args.port}")
    runner = await start_proxy(controller_ip, "127.0.0.1", args.port)
    url = f"http://127.0.0.1:{args.port}/"

    if browser_name == "epiphany":
        browser = shutil.which("epiphany-browser") or shutil.which("epiphany")
        if not browser:
            await runner.cleanup()
            raise AccessPiError("Epiphany was not found. Install it with: sudo apt install -y epiphany-browser")

        profile_dir = STATE_DIR / "epiphany-profile"
        shutil.rmtree(profile_dir, ignore_errors=True)
        profile_dir.mkdir(parents=True, exist_ok=True)
        command = [
            "env",
            f"XDG_CONFIG_HOME={profile_dir / 'config'}",
            f"XDG_CACHE_HOME={profile_dir / 'cache'}",
            f"XDG_DATA_HOME={profile_dir / 'data'}",
            browser,
            url,
        ]
    else:
        browser = shutil.which("chromium-browser") or shutil.which("chromium")
        if not browser:
            await runner.cleanup()
            raise AccessPiError("Chromium was not found.")

        profile_dir = STATE_DIR / "chromium-profile"
        profile_dir.mkdir(parents=True, exist_ok=True)
        command = [
            browser,
            "--no-memcheck",
            "--ozone-platform=x11",
            "--no-first-run",
            "--no-default-browser-check",
            "--disable-session-crashed-bubble",
            "--disable-component-update",
            "--disable-features=Translate,AutofillServerCommunication,OptimizationHints",
            "--password-store=basic",
            f"--user-data-dir={profile_dir}",
            "--start-fullscreen",
            "--disable-dev-shm-usage",
            "--noerrdialogs",
            "--disable-infobars",
            "--disable-background-networking",
            "--disable-background-timer-throttling",
            "--disable-renderer-backgrounding",
            "--disable-extensions",
            "--window-size=1280,720",
            "--window-position=0,0",
            "--new-window",
            url,
        ]

    print(f"Opening {browser_name}: {url}")
    process = await asyncio.create_subprocess_exec(*command)
    try:
        return await process.wait()
    finally:
        await runner.cleanup()


async def command_proxy(args):
    state = load_state()
    controller_ip = args.ip or state.get("hotspot", {}).get("ip") or "192.168.254.1"

    print(f"Starting WebUI proxy for controller {controller_ip} on http://127.0.0.1:{args.port}")
    print("Press Ctrl+C to stop the proxy.")
    runner = await start_proxy(controller_ip, "127.0.0.1", args.port)
    try:
        while True:
            await asyncio.sleep(3600)
    finally:
        await runner.cleanup()


async def command_launch(args):
    address = args.address
    controller = None

    for attempt in range(1, args.retries + 1):
        try:
            if not address:
                controllers = await scan_controllers(args.seconds)
                if not controllers:
                    raise AccessPiError("No Access controllers found.")
                controller = controllers[0]
                address = controller.address
                print(f"Selected: {controller.name} ({controller.address}, RSSI {controller.rssi})")

            print(f"Launch attempt {attempt}/{args.retries}")
            print("Reading controller hotspot credentials over BLE...")
            credentials = await get_hotspot_credentials(address)
            print(f"Controller Wi-Fi: {credentials.ssid}  controller IP: {credentials.ip}")

            print("Connecting Pi Wi-Fi to controller hotspot...")
            connect_linux_wifi(credentials)
            wait_for_controller_ip(credentials.ip, timeout=30)

            web_url = f"http://{credentials.ip}"
            save_state(
                controller=asdict(controller) if controller else {"address": address},
                hotspot=asdict(credentials),
                web_url=web_url,
            )

            wait_for_http(web_url + "/", timeout=args.web_timeout, interval=3)
            kiosk_args = argparse.Namespace(ip=credentials.ip, port=args.port)
            return await command_kiosk(kiosk_args)
        except Exception as exc:
            print(f"Launch attempt failed: {exc}")
            if attempt == args.retries:
                raise
            await asyncio.sleep(2)

    return 1


async def main_async(argv):
    parser = argparse.ArgumentParser(description="Raspberry Pi runner for Access controller WebUI.")
    sub = parser.add_subparsers(dest="command", required=True)

    scan_parser = sub.add_parser("scan", help="Scan for Access controllers over BLE.")
    scan_parser.add_argument("--seconds", type=float, default=4.0)
    scan_parser.set_defaults(func=command_scan)

    connect_parser = sub.add_parser("connect", help="Connect to controller hotspot and verify EXOsocket.")
    connect_parser.add_argument("--address", help="BLE address. If omitted, the strongest controller is used.")
    connect_parser.add_argument("--seconds", type=float, default=4.0)
    connect_parser.add_argument("--no-wifi", action="store_true", help="Read credentials and verify only if already connected.")
    connect_parser.add_argument("--skip-exosocket", action="store_true", help="Skip EXOsocket verification after Wi-Fi connect.")
    connect_parser.set_defaults(func=command_connect)

    wait_parser = sub.add_parser("wait-web", help="Wait until the controller WebUI returns HTTP success.")
    wait_parser.add_argument("--ip")
    wait_parser.add_argument("--timeout", type=int, default=120)
    wait_parser.add_argument("--interval", type=int, default=3)
    wait_parser.set_defaults(func=command_wait_web)

    check_parser = sub.add_parser("check", help="Verify EXOsocket against a controller IP.")
    check_parser.add_argument("--ip")
    check_parser.set_defaults(func=command_check)

    open_parser = sub.add_parser("open", help="Open the controller WebUI.")
    open_parser.add_argument("--url")
    open_parser.add_argument("--kiosk", action="store_true")
    open_parser.set_defaults(func=command_open)

    kiosk_parser = sub.add_parser("kiosk", help="Run a local patched WebUI proxy and open Chromium kiosk.")
    kiosk_parser.add_argument("--ip", help="Controller IP. Defaults to saved state or 192.168.254.1.")
    kiosk_parser.add_argument("--port", type=int, default=8080)
    kiosk_parser.add_argument("--browser", choices=["chromium", "epiphany"], help="Browser to open. Defaults to ACCESSPI_BROWSER or chromium.")
    kiosk_parser.set_defaults(func=command_kiosk)

    proxy_parser = sub.add_parser("proxy", help="Run only the local patched WebUI proxy without opening Chromium.")
    proxy_parser.add_argument("--ip", help="Controller IP. Defaults to saved state or 192.168.254.1.")
    proxy_parser.add_argument("--port", type=int, default=8080)
    proxy_parser.set_defaults(func=command_proxy)

    launch_parser = sub.add_parser("launch", help="BLE connect, join controller Wi-Fi, wait for WebUI, then open kiosk.")
    launch_parser.add_argument("--address", help="BLE address. If omitted, the strongest controller is used.")
    launch_parser.add_argument("--seconds", type=float, default=4.0)
    launch_parser.add_argument("--retries", type=int, default=3)
    launch_parser.add_argument("--web-timeout", type=int, default=120)
    launch_parser.add_argument("--port", type=int, default=8080)
    launch_parser.set_defaults(func=command_launch)

    args = parser.parse_args(argv)
    return await args.func(args)


def main():
    try:
        return asyncio.run(main_async(sys.argv[1:]))
    except KeyboardInterrupt:
        return 130
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
