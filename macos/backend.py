#!/usr/bin/env python3
"""Local macOS BLE-to-browser bridge for an Oral-B iO toothbrush."""

from __future__ import annotations

import argparse
import asyncio
import contextlib
import copy
from collections import deque
from datetime import datetime, timezone
import json
import logging
from pathlib import Path
import time
from typing import Any
import webbrowser

from aiohttp import WSMsgType, web
from bleak import BleakClient, BleakScanner

from oralb_protocol import ORALB_COMPANY_ID, parse_advertisement, parse_gatt_value
from position_classifier import PositionEngine


ROOT = Path(__file__).resolve().parents[1]
STATIC_DIR = ROOT / "simulator"

UUIDS = {
    "device_info": "a0f0ff02-5047-4d53-8208-4f72616c2d42",
    "state": "a0f0ff04-5047-4d53-8208-4f72616c2d42",
    "battery": "a0f0ff05-5047-4d53-8208-4f72616c2d42",
    "mode": "a0f0ff07-5047-4d53-8208-4f72616c2d42",
    "brushing_time": "a0f0ff08-5047-4d53-8208-4f72616c2d42",
    "sector": "a0f0ff09-5047-4d53-8208-4f72616c2d42",
    "pressure": "a0f0ff0b-5047-4d53-8208-4f72616c2d42",
    "motion": "a0f0ff0d-5047-4d53-8208-4f72616c2d42",
    "pacer_config": "a0f0ff26-5047-4d53-8208-4f72616c2d42",
}

NOTIFY_NAMES = ("state", "battery", "mode", "brushing_time", "sector", "pressure", "motion")
READ_NAMES = (
    "device_info",
    "state",
    "battery",
    "mode",
    "brushing_time",
    "sector",
    "pressure",
    "motion",
    "pacer_config",
)


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def initial_brush() -> dict[str, Any]:
    return {
        "valid": False,
        "source": "none",
        "name": None,
        "device_id": None,
        "state": "unknown",
        "state_raw": None,
        "brushing": False,
        "elapsed_seconds": 0,
        "pressure": "normal",
        "pressure_raw": None,
        "mode": "daily_clean",
        "mode_raw": None,
        "battery": None,
        "rssi": None,
        "pacer_sector": 0,
        "pacer_sector_count": None,
        "pacer_sector_timer": None,
        "target_seconds": 120,
        "received_at": None,
        "packet_age_seconds": None,
        "motion_packet_count": 0,
        "motion_rate_hz": 0.0,
        "motion_payload_hex": None,
        "motion_format": None,
        "motion_samples": [],
        "position": {
            "status": "motion_only",
            "active_surface": None,
            "coverage": [0.0] * 16,
        },
    }


class OralBBridge:
    def __init__(self, start_mode: str = "live") -> None:
        self.mode = start_mode
        self.connection: dict[str, Any] = {
            "status": "starting",
            "device_id": None,
            "device_name": None,
            "error": None,
            "direct_requested": False,
        }
        self.brush = initial_brush()
        self.position_engine = PositionEngine(ROOT / "data" / "io10_position_model.json")
        self.brush["position"] = self.position_engine.position_state()
        self.devices: dict[str, dict[str, Any]] = {}
        self._ble_devices: dict[str, Any] = {}
        self._scanner: BleakScanner | None = None
        self._client: BleakClient | None = None
        self._loop: asyncio.AbstractEventLoop | None = None
        self._websockets: set[web.WebSocketResponse] = set()
        self._events: deque[dict[str, Any]] = deque(maxlen=20000)
        self._publish_handle: asyncio.TimerHandle | None = None
        self._ticker_task: asyncio.Task[None] | None = None
        self._mock_task: asyncio.Task[None] | None = None
        self._connect_lock = asyncio.Lock()
        self._direct_requested = False
        self._target_device_id: str | None = None
        self._timer_anchor: tuple[int, float] | None = None
        self._motion_times: deque[float] = deque(maxlen=240)
        self._mock_elapsed = 0.0
        self._mock_started_at = 0.0
        self._mock_running = False

    async def start(self) -> None:
        self._loop = asyncio.get_running_loop()
        self._ticker_task = asyncio.create_task(self._ticker())
        if self.mode == "mock":
            await self.set_mode("mock")
        else:
            await self.start_scanner()

    async def stop(self) -> None:
        self._direct_requested = False
        if self._mock_task:
            self._mock_task.cancel()
        if self._ticker_task:
            self._ticker_task.cancel()
        await self.stop_scanner()
        if self._client and self._client.is_connected:
            with contextlib.suppress(Exception):
                await self._client.disconnect()
        for task in (self._mock_task, self._ticker_task):
            if task:
                with contextlib.suppress(asyncio.CancelledError):
                    await task

    async def _ticker(self) -> None:
        while True:
            await asyncio.sleep(1)
            self.schedule_publish(0)

    def _on_detection(self, device: Any, advertisement: Any) -> None:
        manufacturer = advertisement.manufacturer_data.get(ORALB_COMPANY_ID)
        name = advertisement.local_name or device.name or ""
        is_oralb_name = "oral-b" in name.casefold() or "oral b" in name.casefold()
        if manufacturer is None and not is_oralb_name:
            return

        now = utc_now()
        self._ble_devices[device.address] = device
        self.devices[device.address] = {
            "id": device.address,
            "name": name or "Oral-B Toothbrush",
            "rssi": advertisement.rssi,
            "last_seen": now,
        }

        if manufacturer is not None:
            decoded = parse_advertisement(manufacturer)
            self._record("advertisement", "manufacturer", manufacturer, device.address)
            if decoded:
                self._apply_update(
                    decoded,
                    source="advertisement",
                    name=name or "Oral-B Toothbrush",
                    device_id=device.address,
                    rssi=advertisement.rssi,
                )

        if self._direct_requested and self._target_device_id == device.address:
            if self.connection["status"] in {"scanning", "reconnecting", "error"}:
                self.connection["status"] = "connecting"
                self._spawn(self.connect(device.address))
        self.schedule_publish()

    def _spawn(self, coroutine: Any) -> None:
        if self._loop:
            self._loop.call_soon_threadsafe(lambda: asyncio.create_task(coroutine))

    async def start_scanner(self) -> None:
        if self.mode != "live" or self._scanner is not None:
            return
        self.connection["status"] = "reconnecting" if self._direct_requested else "scanning"
        if not self._direct_requested:
            self.connection["error"] = None
        try:
            scanner = BleakScanner(detection_callback=self._on_detection)
            await scanner.start()
            self._scanner = scanner
        except Exception as exc:
            self.connection.update({"status": "error", "error": self._friendly_ble_error(exc)})
        await self.publish()

    async def stop_scanner(self) -> None:
        scanner, self._scanner = self._scanner, None
        if scanner:
            with contextlib.suppress(Exception):
                await scanner.stop()

    @staticmethod
    def _friendly_ble_error(exc: Exception) -> str:
        message = str(exc).strip() or exc.__class__.__name__
        lower = message.casefold()
        if "permission" in lower or "not authorized" in lower:
            return "Bluetooth permission is off. Enable it for Terminal or Python in System Settings → Privacy & Security → Bluetooth."
        if "powered off" in lower:
            return "Bluetooth is turned off on this Mac."
        return message

    async def connect(self, device_id: str) -> bool:
        async with self._connect_lock:
            if self.mode != "live":
                await self.set_mode("live")
            if self._client and self._client.is_connected:
                return True
            device = self._ble_devices.get(device_id)
            if device is None:
                self.connection.update({"status": "error", "error": "That brush is no longer in scan range."})
                await self.publish()
                return False

            self._direct_requested = True
            self._target_device_id = device_id
            self.connection.update(
                {
                    "status": "connecting",
                    "device_id": device_id,
                    "device_name": self.devices[device_id]["name"],
                    "error": None,
                    "direct_requested": True,
                }
            )
            await self.publish()
            await self.stop_scanner()

            try:
                client = BleakClient(device, disconnected_callback=self._on_disconnected, timeout=12)
                await client.connect()
                self._client = client
                self.connection["status"] = "connected"
                await self._subscribe(client)
                await self._initial_reads(client)
                self.brush.update(
                    {
                        "valid": True,
                        "source": "direct_gatt",
                        "device_id": device_id,
                        "name": self.devices[device_id]["name"],
                    }
                )
                await self.publish()
                return True
            except Exception as exc:
                self._client = None
                self.connection.update({"status": "reconnecting", "error": self._friendly_ble_error(exc)})
                await self.start_scanner()
                return False

    async def disconnect(self) -> None:
        self._direct_requested = False
        self._target_device_id = None
        self.connection["direct_requested"] = False
        client, self._client = self._client, None
        if client and client.is_connected:
            with contextlib.suppress(Exception):
                await client.disconnect()
        if self.mode == "live":
            await self.start_scanner()

    def _on_disconnected(self, client: BleakClient) -> None:
        self._spawn(self._handle_disconnected(client))

    async def _handle_disconnected(self, client: BleakClient) -> None:
        if self._client is not client:
            return
        self._client = None
        self.brush["source"] = "advertisement"
        self.connection["status"] = "reconnecting" if self._direct_requested else "scanning"
        await self.start_scanner()

    async def _subscribe(self, client: BleakClient) -> None:
        for name in NOTIFY_NAMES:
            try:
                await asyncio.wait_for(
                    client.start_notify(UUIDS[name], self._notification_handler(name)),
                    timeout=6,
                )
            except Exception as exc:
                self._record_error(f"subscribe_{name}", exc)

    def _notification_handler(self, name: str):
        def handler(_sender: Any, data: bytearray) -> None:
            self._spawn(self._handle_gatt(name, bytes(data), "notify"))

        return handler

    async def _initial_reads(self, client: BleakClient) -> None:
        for name in READ_NAMES:
            try:
                value = await asyncio.wait_for(client.read_gatt_char(UUIDS[name]), timeout=6)
                await self._handle_gatt(name, bytes(value), "read")
            except Exception as exc:
                self._record_error(f"read_{name}", exc)

    async def _handle_gatt(self, name: str, payload: bytes, event_source: str) -> None:
        configured = self.brush.get("pacer_sector_count")
        decoded = parse_gatt_value(name, payload, configured)
        if name == "sector" and not self.brush.get("brushing"):
            decoded["pacer_sector"] = 0
        self._record(event_source, name, payload, self._target_device_id)
        self._apply_update(
            decoded,
            source="direct_gatt",
            name=self.connection.get("device_name"),
            device_id=self._target_device_id,
        )
        if name == "motion":
            now = time.monotonic()
            self._motion_times.append(now)
            while self._motion_times and now - self._motion_times[0] > 2:
                self._motion_times.popleft()
            self.brush["motion_packet_count"] += 1
            self.brush["motion_rate_hz"] = round(len(self._motion_times) / 2, 1)
            self.position_engine.ingest(
                decoded.get("motion_samples", []), bool(self.brush.get("brushing"))
            )
            self.brush["position"] = self.position_engine.position_state()
        self.schedule_publish(0.08 if name == "motion" else 0)

    def _apply_update(
        self,
        values: dict[str, Any],
        *,
        source: str,
        name: str | None,
        device_id: str | None,
        rssi: int | None = None,
    ) -> None:
        now = utc_now()
        previous_brushing = bool(self.brush.get("brushing"))
        # Some iO10 firmware sessions leave FF04 in selection-menu (8) while
        # FF08 still advances during an active brush.  Treat an advancing
        # elapsed timer as authoritative so FF0D windows reach the classifier.
        if "elapsed_seconds" in values and source == "direct_gatt":
            previous_elapsed = int(self.brush.get("elapsed_seconds") or 0)
            current_elapsed = int(values["elapsed_seconds"])
            if current_elapsed > previous_elapsed:
                values.setdefault("brushing", True)
                values.setdefault("state", "running")
                values.setdefault("state_raw", 3)
            elif current_elapsed == 0 and previous_elapsed > 0:
                values.setdefault("brushing", False)
        self.brush.update(values)
        self.brush.update(
            {
                "valid": True,
                "source": source,
                "name": name or self.brush.get("name"),
                "device_id": device_id or self.brush.get("device_id"),
                "received_at": now,
            }
        )
        if rssi is not None:
            self.brush["rssi"] = rssi
        if "elapsed_seconds" in values:
            self._timer_anchor = (int(values["elapsed_seconds"]), time.monotonic())
        if values.get("brushing") is False:
            self._timer_anchor = None
        if "brushing" in values and bool(values["brushing"]) != previous_brushing:
            self.position_engine.set_brushing(bool(values["brushing"]))
        if self.mode == "live":
            self.brush["position"] = self.position_engine.position_state()

    def _record(self, event_source: str, name: str, payload: bytes, device_id: str | None) -> None:
        self._events.append(
            {
                "time": utc_now(),
                "source": event_source,
                "name": name,
                "device_id": device_id,
                "hex": payload.hex(),
                "bytes": list(payload),
            }
        )

    def _record_error(self, operation: str, exc: Exception) -> None:
        logging.debug("%s failed: %s", operation, exc)
        self._events.append(
            {"time": utc_now(), "source": "backend", "name": operation, "error": str(exc)}
        )

    async def set_mode(self, mode: str) -> None:
        if mode not in {"live", "mock"}:
            raise ValueError("mode must be live or mock")
        if mode == self.mode and ((mode == "mock" and self._mock_task) or (mode == "live" and self._scanner)):
            return
        self.mode = mode
        if mode == "mock":
            await self.disconnect()
            await self.stop_scanner()
            self.brush = initial_brush()
            self.connection.update(
                {
                    "status": "demo",
                    "device_id": "mock-brush",
                    "device_name": "Virtual iO 10",
                    "error": None,
                    "direct_requested": False,
                }
            )
            self._mock_elapsed = 0
            self._mock_running = False
            if self._mock_task:
                self._mock_task.cancel()
            self._mock_task = asyncio.create_task(self._mock_loop())
        else:
            if self._mock_task:
                self._mock_task.cancel()
                self._mock_task = None
            self.brush = initial_brush()
            self.brush["position"] = self.position_engine.position_state()
            self.connection.update(
                {
                    "status": "scanning",
                    "device_id": None,
                    "device_name": None,
                    "error": None,
                    "direct_requested": False,
                }
            )
            await self.start_scanner()
        await self.publish()

    async def mock_action(self, action: str) -> None:
        if self.mode != "mock":
            await self.set_mode("mock")
        self._refresh_mock_elapsed()
        if action == "start":
            if self._mock_elapsed >= 120:
                self._mock_elapsed = 0
            self._mock_running = True
            self._mock_started_at = time.monotonic()
        elif action == "pause":
            self._mock_running = False
        elif action == "reset":
            self._mock_running = False
            self._mock_elapsed = 0
        else:
            raise ValueError("action must be start, pause, or reset")
        self._render_mock()
        await self.publish()

    def _refresh_mock_elapsed(self) -> None:
        if self._mock_running:
            now = time.monotonic()
            self._mock_elapsed += now - self._mock_started_at
            self._mock_started_at = now
            if self._mock_elapsed >= 120:
                self._mock_elapsed = 120
                self._mock_running = False

    async def _mock_loop(self) -> None:
        while True:
            self._refresh_mock_elapsed()
            self._render_mock()
            await self.publish()
            await asyncio.sleep(0.2)

    def _render_mock(self) -> None:
        elapsed = self._mock_elapsed
        duration = 120 / 16
        active = min(16, int(elapsed / duration) + 1)
        coverage = [max(0.0, min(100.0, (elapsed - i * duration) / duration * 100)) for i in range(16)]
        pressure = "high" if self._mock_running and int(elapsed) % 29 in range(22, 26) else "normal"
        self.brush.update(
            {
                "valid": True,
                "source": "mock",
                "name": "Virtual iO 10",
                "device_id": "mock-brush",
                "state": "running" if self._mock_running else "idle",
                "state_raw": 3 if self._mock_running else 2,
                "brushing": self._mock_running,
                "elapsed_seconds": int(elapsed),
                "pressure": pressure,
                "pressure_raw": 2 if pressure == "high" else 1,
                "mode": "daily_clean",
                "mode_raw": 0,
                "battery": 87,
                "rssi": -48,
                "pacer_sector": min(6, int(elapsed / 20) + 1) if self._mock_running else 0,
                "pacer_sector_count": 6,
                "pacer_sector_timer": int(elapsed) % 20,
                "received_at": utc_now(),
                "position": {
                    "status": "simulated",
                    "active_surface": active,
                    "coverage": coverage,
                },
            }
        )

    def public_state(self) -> dict[str, Any]:
        brush = copy.deepcopy(self.brush)
        if self.mode == "live" and brush.get("brushing") and self._timer_anchor:
            anchor, observed = self._timer_anchor
            brush["elapsed_seconds"] = anchor + int(time.monotonic() - observed)
        if brush.get("received_at"):
            try:
                received = datetime.fromisoformat(brush["received_at"])
                brush["packet_age_seconds"] = round(
                    max(0.0, (datetime.now(timezone.utc) - received).total_seconds()), 1
                )
            except ValueError:
                pass
        return {
            "type": "state",
            "mode": self.mode,
            "connection": copy.deepcopy(self.connection),
            "brush": brush,
            "calibration": self.position_engine.calibration_state(
                bool(self.brush.get("brushing"))
            ),
            "devices": sorted(self.devices.values(), key=lambda item: item["last_seen"], reverse=True),
            "events": list(self._events)[-30:],
            "server_time": utc_now(),
        }

    async def start_calibration(self) -> None:
        if self.mode != "live":
            await self.set_mode("live")
        if not self._client or not self._client.is_connected:
            device_id = self._target_device_id
            if not device_id and self.devices:
                device_id = next(iter(self.devices))
            if not device_id or not await self.connect(device_id):
                raise RuntimeError("Connect the toothbrush before calibration")
        self.position_engine.start_calibration()
        self.brush["position"] = self.position_engine.position_state()
        await self.publish()

    async def cancel_calibration(self) -> None:
        self.position_engine.cancel_calibration()
        self.brush["position"] = self.position_engine.position_state()
        await self.publish()

    def schedule_publish(self, delay: float = 0.1) -> None:
        if not self._loop or self._publish_handle:
            return

        def fire() -> None:
            self._publish_handle = None
            asyncio.create_task(self.publish())

        self._publish_handle = self._loop.call_later(delay, fire)

    async def publish(self) -> None:
        if not self._websockets:
            return
        payload = json.dumps(self.public_state())
        stale = []
        for websocket in self._websockets:
            try:
                await websocket.send_str(payload)
            except Exception:
                stale.append(websocket)
        for websocket in stale:
            self._websockets.discard(websocket)


def create_app(bridge: OralBBridge, open_browser: bool = False, port: int = 8765) -> web.Application:
    app = web.Application(client_max_size=64 * 1024)
    app["bridge"] = bridge

    async def index(_request: web.Request) -> web.FileResponse:
        return web.FileResponse(STATIC_DIR / "index.html")

    async def state(request: web.Request) -> web.Response:
        return web.json_response(request.app["bridge"].public_state())

    async def connect(request: web.Request) -> web.Response:
        active: OralBBridge = request.app["bridge"]
        body = await request.json()
        device_id = body.get("device_id")
        if not device_id and active.devices:
            device_id = next(iter(active.devices))
        if not device_id:
            raise web.HTTPConflict(text="No Oral-B brush has been discovered yet")
        connected = await active.connect(str(device_id))
        return web.json_response({"ok": connected, "state": active.public_state()})

    async def disconnect(request: web.Request) -> web.Response:
        active: OralBBridge = request.app["bridge"]
        await active.disconnect()
        return web.json_response({"ok": True, "state": active.public_state()})

    async def mode(request: web.Request) -> web.Response:
        active: OralBBridge = request.app["bridge"]
        body = await request.json()
        try:
            await active.set_mode(str(body.get("mode", "")))
        except ValueError as exc:
            raise web.HTTPBadRequest(text=str(exc)) from exc
        return web.json_response({"ok": True, "state": active.public_state()})

    async def mock(request: web.Request) -> web.Response:
        active: OralBBridge = request.app["bridge"]
        body = await request.json()
        try:
            await active.mock_action(str(body.get("action", "")))
        except ValueError as exc:
            raise web.HTTPBadRequest(text=str(exc)) from exc
        return web.json_response({"ok": True, "state": active.public_state()})

    async def start_calibration(request: web.Request) -> web.Response:
        active: OralBBridge = request.app["bridge"]
        try:
            await active.start_calibration()
        except RuntimeError as exc:
            raise web.HTTPConflict(text=str(exc)) from exc
        return web.json_response({"ok": True, "state": active.public_state()})

    async def cancel_calibration(request: web.Request) -> web.Response:
        active: OralBBridge = request.app["bridge"]
        await active.cancel_calibration()
        return web.json_response({"ok": True, "state": active.public_state()})

    async def capture(request: web.Request) -> web.Response:
        active: OralBBridge = request.app["bridge"]
        body = "".join(json.dumps(row) + "\n" for row in active._events)
        return web.Response(
            text=body,
            content_type="application/x-ndjson",
            headers={"Content-Disposition": 'attachment; filename="oralb-live-capture.jsonl"'},
        )

    async def websocket(request: web.Request) -> web.WebSocketResponse:
        active: OralBBridge = request.app["bridge"]
        ws = web.WebSocketResponse(heartbeat=20)
        await ws.prepare(request)
        active._websockets.add(ws)
        await ws.send_json(active.public_state())
        try:
            async for message in ws:
                if message.type == WSMsgType.TEXT and message.data == "state":
                    await ws.send_json(active.public_state())
        finally:
            active._websockets.discard(ws)
        return ws

    async def on_startup(_app: web.Application) -> None:
        await bridge.start()
        if open_browser:
            asyncio.get_running_loop().call_later(0.7, webbrowser.open, f"http://127.0.0.1:{port}")

    async def on_cleanup(_app: web.Application) -> None:
        await bridge.stop()

    app.router.add_get("/", index)
    app.router.add_get("/api/state", state)
    app.router.add_post("/api/connect", connect)
    app.router.add_post("/api/disconnect", disconnect)
    app.router.add_post("/api/mode", mode)
    app.router.add_post("/api/mock", mock)
    app.router.add_post("/api/calibration/start", start_calibration)
    app.router.add_post("/api/calibration/cancel", cancel_calibration)
    app.router.add_get("/api/capture", capture)
    app.router.add_get("/ws", websocket)
    app.router.add_static("/", STATIC_DIR, show_index=False)
    app.on_startup.append(on_startup)
    app.on_cleanup.append(on_cleanup)
    return app


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--mock", action="store_true", help="start with the virtual brush")
    parser.add_argument("--open", action="store_true", help="open the browser after startup")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO)
    bridge = OralBBridge("mock" if args.mock else "live")
    app = create_app(bridge, open_browser=args.open, port=args.port)
    web.run_app(app, host=args.host, port=args.port, print=lambda line: print(f"Oral-B Live: {line}"))


if __name__ == "__main__":
    main()
