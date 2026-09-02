# Oral-B iO 10 Live Display

A real-time Oral-B iO dashboard with two targets:

- a **macOS Bluetooth bridge + browser display** that works now;
- ESP-IDF firmware for the **Waveshare ESP32-S3-Touch-AMOLED-1.8**.

Both targets use the same display model: timer, pressure, mode, battery, the
six timed pacer sectors, and a separate 16-surface mouth visualization.

## Run it on a Mac

Double-click:

```text
start_macos.command
```

The first launch creates a local Python environment, installs the required
packages, starts the Bluetooth bridge, and opens:

```text
http://127.0.0.1:8765
```

The Comino runtime is optional. On a Mac or Windows machine with the private
model files, install it with:

```bash
.venv/bin/python -m pip install -r macos/requirements-comino.txt
```

When macOS asks, allow Bluetooth access for Terminal or Python. If macOS blocks
the script because it was downloaded, Control-click it in Finder and choose
**Open** once.

The page begins in **Live brush** mode:

1. Wake the brush or lift it off the charger.
2. Passive advertisements appear automatically when the brush sends them.
3. Select the discovered brush and choose **Connect direct** for the fastest
   timer, all three pressure states, battery, and raw motion notifications.
4. The macOS bridge uses the official Comino model locally when its model
   assets are installed; no calibration is required for that path. A guided
   calibration remains available as a fallback for development.
5. Choose **Release brush** when finished so the phone app or iO Sense can use
   the brush again.

Direct mode uses the toothbrush's single BLE client slot. Close the Oral-B app
and, if necessary, unplug the iO Sense charger while making the direct
connection.

### Terminal alternative

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -r macos/requirements.txt
.venv/bin/python macos/backend.py --open
```

Useful options:

```bash
.venv/bin/python macos/backend.py --mock --open
.venv/bin/python macos/backend.py --port 9000 --open
.venv/bin/python macos/backend.py --lan       # serve clients on the LAN
```

For Raspberry Pi and Windows server setup, see
[docs/network-deployment.md](docs/network-deployment.md). The bridge is the
server and any browser on the same network is a client; the browser needs no
Bluetooth access of its own.

## What the Mac version includes

- native macOS BLE through Bleak/CoreBluetooth;
- automatic passive Oral-B discovery;
- optional direct GATT connection with reconnect behavior;
- real-time WebSocket updates to one or more browser tabs;
- timer, brush state, mode, battery, pressure, pacer sector and signal strength;
- raw FF0D inertial packets, decoded into timestamped motion/gyro samples when
  their layout is recognized;
- the official Comino motion classifier extracted from a user-provided Oral-B
  APK (kept local and excluded from this repository);
- a public brush-mounted IMU prior and guided 16-surface calibration fallback;
- locally accumulated per-surface coverage after calibration;
- an in-memory live packet viewer and downloadable JSONL capture;
- the complete virtual-brush simulator as a backend mode;
- a standalone demo when `simulator/index.html` is opened without the backend.

## The 16 mouth surfaces

The UI geometry is intentionally:

- three surfaces for each of the four corner groups;
- two surfaces for each of the two middle groups.

That is **16 visual surfaces**. It is not a claim that the toothbrush sends a
16-value packet.

An important protocol correction: characteristic `FF0D`
(`a0f0ff0d-5047-4d53-8208-4f72616c2d42`) contains raw inertial samples, not a
ready-made physical zone. The Oral-B application applies a proprietary
classifier to a motion window to infer detailed mouth position. This project
does not distribute or imitate those assets. Instead it uses the openly
available brush-mounted IMU dataset as a general motion prior, then aligns that
prior with one guided pass using the user's own brush and grip. Until that pass
is complete, the live mouth stays neutral and never relabels the timed pacer as
physical position. In demo mode it exercises all 16 surfaces.

## Project structure

```text
macos/
  backend.py             BLE scanner/GATT client, HTTP API and WebSocket
  oralb_protocol.py      pure advertisement, GATT and motion parsers
  official_comino.py local runner for the APK's Comino model
  position_classifier.py official-model integration and calibration fallback
  requirements.txt
  tests/
simulator/
  index.html             shared browser and embedded-display interface
  styles.css
  app.js
firmware/                ESP-IDF firmware for the Waveshare board
tools/                   focused BLE capture and validation utilities
docs/                    architecture and protocol notes
start_macos.command      one-click Mac launcher
```

## Validate the project

```bash
python3 -m unittest discover -s macos/tests -v
python3 tools/validate_project.py
node --check simulator/app.js
```

## Local Comino model (not included)

The official Oral-B Comino model is proprietary and is deliberately excluded
from GitHub by `.gitignore`. To enable it for private testing, place the model
and its matching initial-state file at:

```text
data/comino_models/20210420-102833.tflite
data/comino_models/20210420-102833.json
```

The application then reports `official_comino_apk` as its position model. The
repository contains the integration code and the exact 20-zone-to-16-surface
mapping, but not the vendor model binary or state data.

## ESP32 firmware

Install ESP-IDF 5.5.x, then:

```bash
cd firmware
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

To switch the firmware between mock and passive BLE input, edit
`firmware/main/app_config.hpp` and change `kUseMockBrush`.

## Protocol references

The passive parser follows the open-source
[oralb-ble](https://github.com/Bluetooth-Devices/oralb-ble) packet layout. The
direct characteristic and motion notes are cross-checked against the captured
and reconstructed evidence in
[Oral-B Live's protocol reference](https://github.com/thomasgregg/oralb-ha/blob/main/docs/protocol.md).
