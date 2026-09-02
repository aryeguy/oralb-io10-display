# Oral-B protocol notes used by this project

## Passive manufacturer advertisements

Oral-B uses manufacturer ID:

```text
0x00DC
```

The currently documented iO advertisement payload is 9 or 11 bytes.

For the known format:

| Payload byte | Meaning |
|---:|---|
| 0 | protocol version |
| 1 | model type |
| 3 | toothbrush state |
| 4 | pressure/status flags |
| 5 | brushing minutes |
| 6 | brushing seconds |
| 7 | mode |
| 8 | timed pacer sector code |
| 9 | sector timer, if 11-byte payload |
| 10 | number of pacer sectors, if 11-byte payload |

Running state is value `3`.

The low three bits of byte 8 represent the pacer sector. Values 1–6 are
concrete sectors, 0 means no sector and 7 is a "last sector" sentinel that
uses the configured sector count.

## Important distinction

The advertisement's `sector` value is a **timed pacer interval**.

It is *not* the same data as the Oral-B app's physical 3-surface mouth
position.

## Known GATT service and characteristics

Main service:

```text
a0f0ff00-5047-4d53-8208-4f72616c2d42
```

Useful characteristics:

```text
battery       a0f0ff05-5047-4d53-8208-4f72616c2d42
status        a0f0ff04-5047-4d53-8208-4f72616c2d42
mode          a0f0ff07-5047-4d53-8208-4f72616c2d42
brushing time a0f0ff08-5047-4d53-8208-4f72616c2d42
sector        a0f0ff09-5047-4d53-8208-4f72616c2d42
pressure      a0f0ff0b-5047-4d53-8208-4f72616c2d42
motion        a0f0ff0d-5047-4d53-8208-4f72616c2d42
pacer config  a0f0ff26-5047-4d53-8208-4f72616c2d42
```

Active pressure has observed values:

```text
0 = low
1 = normal
2 = high
```

### Correction: FF0D is motion, not a position enum

Current protocol evidence shows that FF0D carries timestamped inertial samples.
On the observed Comino layout it includes signed gyroscope and motion axes; the
other known 20-byte layout contains four timestamped three-axis motion samples.

The vendor application runs windows of these samples through a proprietary
classifier to infer physical mouth position. The classifier assets cannot be
distributed with this project, so FF0D must not be relabelled as a surface or
zone value. The Mac bridge exposes and records the raw samples, then offers a
separate user-calibrated local classifier. Its labels come only from the guided
calibration—not from the timed pacer characteristic.

## Why the firmware starts passive

Passive mode is useful for first bring-up because it:

- requires no pairing
- avoids fighting the phone for the brush's active BLE connection
- already gives timer, state, mode, pressure-high flag and pacer sector
- lets us validate the display, parser and reconnect behavior independently

The Mac backend now implements the direct-GATT source for known values and raw
motion. A future validated position classifier can implement the same
`BrushSnapshot` / `BrushState` interface without changing the UI.
