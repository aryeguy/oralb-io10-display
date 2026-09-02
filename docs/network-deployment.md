# Network deployment

The bridge is the server. It owns Bluetooth and publishes the same HTTP page,
REST API, and WebSocket state stream to browser clients. A client only needs a
browser; Bluetooth permissions are required on the server machine, not on the
client device.

## Raspberry Pi

Use Raspberry Pi OS 64-bit with Bluetooth enabled:

```bash
sudo apt update
sudo apt install -y python3-venv bluetooth
git clone https://github.com/aryeguy/oralb-io10-display.git
cd oralb-io10-display
python3 -m venv .venv
.venv/bin/python -m pip install -r macos/requirements.txt
.venv/bin/python macos/backend.py --lan
```

Find the Pi's address with `hostname -I`, then open
`http://PI_ADDRESS:8765` from another computer or phone. Direct GATT access
may require the user running the service to have BlueZ permissions.

## Windows

Install Python 3.11 or newer, clone the repository in PowerShell, and run:

```powershell
py -m venv .venv
.venv\Scripts\python -m pip install -r macos\requirements.txt
.venv\Scripts\python macos\backend.py --lan
```

Open `http://WINDOWS_ADDRESS:8765` from the client browser. Allow Python
through Windows Defender Firewall on private networks when prompted. Bleak
uses Windows' native Bluetooth stack; close the Oral-B phone app when using
direct GATT because the brush permits only one active client.

## Server/client behavior

The browser automatically connects to `/ws` on the host that served the page,
so no client-side IP address or configuration is needed. Use `--host
127.0.0.1` for local-only access (the default), `--lan` for all interfaces, or
`--host 192.168.1.50` to bind to one specific interface. The default port is
`8765`; change it with `--port`.

The ESP32 display is a future client of this same state contract. The current
firmware remains a standalone BLE display and does not yet fetch the WebSocket
stream; implementing a small HTTP/WebSocket client will let it display the
server's classified zones without putting the proprietary model on the board.

## Model assets

The official Comino model is intentionally not committed. Each server that
needs official position classification must have the private files documented
in the README under `data/comino_models/`; otherwise the public/calibration
fallback remains available.
