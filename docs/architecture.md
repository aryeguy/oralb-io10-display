# Architecture

## `BrushSnapshot`

A single authoritative packet/event from a source.

Sources can be:

- passive BLE advertisements
- mock generator
- direct GATT notifications through the Mac bridge

## `BrushModel`

Merges snapshots into durable UI state.

Responsibilities:

- remember last values
- smooth elapsed time locally between packets
- keep timed pacer data separate from physical mouth coverage
- optionally generate coverage only in explicit demo mode

## `BrushState`

Everything the UI needs.

The UI never parses Bluetooth bytes.

## `BrushUi`

LVGL view for the 368 × 448 board.

The current geometry is deliberately simple and will be redesigned later.

## Mac bridge

`macos/backend.py` owns CoreBluetooth through Bleak and serves the simulator
assets over loopback HTTP. A WebSocket pushes each durable state update to all
browser tabs. BLE callbacks and byte parsing stay behind the state model, so
the browser never handles Bluetooth protocol bytes directly.

The backend has two live paths:

- passive manufacturer advertisements, which do not occupy the brush slot;
- direct GATT, which provides higher-rate notifications and raw FF0D motion.

It also provides a mock source that drives the same browser state contract.

## Position classification

The Mac path deduplicates the overlapping FF0D Comino records into a roughly
27 Hz sample stream. A guided pass collects four seconds on each of the 16 UI
surfaces, builds 26-sample feature windows, and saves a normalized local
k-nearest-neighbour model under `data/`. Prediction uses overlapping windows
and a short majority vote so transition movements do not immediately paint a
surface.

This model is calibrated to the user's brush orientation. It is independent of
the proprietary vendor GRU assets and remains completely local.

## Why mouth coverage is 16 entries right now

The current simulator geometry is:

- upper-left corner: outside / chewing / inside
- upper-center: outside / inside
- upper-right corner: inside / chewing / outside
- lower-left corner: outside / chewing / inside
- lower-center: outside / inside
- lower-right corner: inside / chewing / outside

That is 16 visual surfaces.

This is a UI-model choice, **not a claim that the iO BLE protocol contains 16
position values**. FF0D is raw inertial data, and a future validated classifier
is free to map its output into these surfaces without changing the transport or
browser interface.
