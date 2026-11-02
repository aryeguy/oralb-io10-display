# Display regression checks

The October 2026 fixes address three reproduced faults:

- Raw BLE snapshots contain cached advertisement state and zero coverage.
  In server mode, applying them to `BrushModel` overwrote the last server frame
  whenever a server poll was skipped or failed. Only snapshots marked as
  server state are accepted by the server-configured display model now.
- Repeated equal timer values reset the server's clock anchor every half
  second, preventing the timer from advancing. Equal/stale relay values now
  preserve the running anchor. The regression test fails with the old logic.
- Persistent curve arrays initially lived in a `BrushUi` object on the main
  stack, causing a confirmed startup stack overflow. The UI uses static
  storage; the main task has an 8 KiB budget for nested HTTP/JSON work and
  application compilation rejects individual frames larger than 2 KiB.

The earlier assertion that canvas clearing alone caused the flash was not
proven. Server logs cannot establish what the device actually rendered.

## QSPI transport correction (pending physical verification)

The bundled Waveshare BSP registers the CO5300 QSPI panel through
`lvgl_port_add_disp_rgb`. The installed LVGL port uses RGB event registration
and releases RGB draw buffers immediately, whereas the SPI interface uses
the panel IO's `on_color_trans_done` callback to release them after transfer.
`qspi_display.cpp` now uses the BSP's low-level panel/touch setup and registers
the display through `lvgl_port_add_disp`, with a DMA-capable RGB565 buffer.
It retains touch-dependent panel offsets and even-coordinate rounding.
This corrects an observed API mismatch; physical visibility must still be
confirmed before attributing the blank screen entirely to that mismatch.

## Automated tests

From the repository root:

```sh
.venv/bin/python -m unittest discover -s macos/tests -v
```

The suite compiles and executes the real C++ `BrushModel` using the host C++
compiler. It replays raw idle packets during missing server polls and checks
brushing state, timer, pressure, active surface, and coverage. Python tests
exercise reconnects, explicit idle state, long timeouts, and frequent equal
timer packets using a controlled clock. These replace the former tests that
only searched the renderer source for particular strings.

A second host C++ test executes the real QSPI initialization against API
doubles, checking SPI registration, handle wiring, DMA/byte-swap settings,
area rounding, initialization failures, and operation without touch. It does
not emulate the vendor driver, DMA timing, or the physical panel.

## Physical verification after flashing

```sh
.venv/bin/python tools/check_display_health.py --seconds 180
```

Every display poll includes the last frame's brushing state, timer, brightness,
main-task stack headroom, uptime, render count, LVGL refresh count, and lock
failures. The latest report is available as `display_diagnostics` in
`GET /api/state`; timestamped reports are also in the Pi's HTTP access logs.

The health check fails on stale diagnostics, reboot, stalled rendering, lock
failures, low stack headroom, or zero brightness while rendering brushing.
It uses Wi-Fi so opening a USB monitor cannot inadvertently reset the board.
It proves software progress, not the appearance of physical pixels; confirm
visibility on the panel as well. Check active brushing, idle dimming/off, and
wake-up separately. A successful compile/upload alone is insufficient.
