# OBSBOT Tiny 2 / Tiny 2 Lite: UVC extension-unit protocol notes

This is our own summary of facts published by other open-source projects. **No
OBSBOT SDK code or header text is copied here, and `libdev.dll` must never be
decompiled or disassembled.** Facts (byte values, offsets) are not copyrightable.
We only copy *code* from projects whose licence allows it (MIT); we never copy
code from unlicensed or copyleft projects.

## Sources

| Short name | Project | Licence | Tested on |
|---|---|---|---|
| lxman | [lxman/obsbot-mcp](https://github.com/lxman/obsbot-mcp) (`PROTOCOL.md`, `tiny2_specification.md`, `src/codec/*.ts`) | MIT | Tiny 2 (PID `FEF8`), Windows + Linux |
| cgevans | [cgevans/tiny2](https://github.com/cgevans/tiny2) (`src/frame.rs`, `src/lib.rs`) | EUPL-1.2 (facts only, no code copied) | Tiny 2 |
| mitchell | [mitchelloharawild/obsbot-tiny-2-control](https://github.com/mitchelloharawild/obsbot-tiny-2-control) | MIT | Tiny 2 (captures from OBSBOT Center via USBPcap) |
| nod | [me-tony/nod](https://github.com/me-tony/nod) | none stated (facts only, no code copied) | **Tiny 2 Lite** (PID `FEF9`), macOS |

## USB identity

- Vendor ID `0x3564`. Tiny 2 = PID `0xFEF8`, Tiny 2 Lite = PID `0xFEF9`.
- Standard UVC Camera Terminal controls are available for zoom (`CT_ZOOM_ABSOLUTE`)
  and pan/tilt (`CT_PANTILT_ABSOLUTE`, arc-seconds; Tiny 2 range pan ±468000,
  tilt ±324000). On Windows these are `IAMCameraControl`. Image controls are
  `IAMVideoProcAmp`.

## Extension unit (XU)

- GUID `{9A1E7291-6843-4683-6D92-39BC7906EE49}`, unit ID 2.
- Selectors 1–19 are 60 bytes each (GET_LEN = 60, GET_INFO = 0x03).
- Windows access: `IKsTopologyInfo` → find the `KSNODETYPE_DEV_SPECIFIC` node
  that answers our GUID → `IKsControl::KsProperty` with a `KSP_NODE`
  (`Set` = XU GUID, `Id` = selector, `Flags` = GET or SET | TOPOLOGY).
  Implemented in `src/camera/CameraDevice.cpp`.

| Selector | Use |
|---|---|
| 2 | CRC-framed "V3" commands (SET) and reply mailbox (GET) |
| 6 | Simple tag commands (SET) and the 60-byte status block (GET) |
| 8 | ASCII product name |
| 12 / 13 | Preset list / preset entry cursor (not used yet) |

## Selector 6: simple tag commands

`[tag, length, value...]`, zero-padded to 60 bytes, no CRC, no reply.

| Function | Bytes | Sources | Works on Tiny 2 Lite? |
|---|---|---|---|
| AI off | `16 02 00 00` | all | yes\* |
| AI normal (whole body) | `16 02 02 00` | all | yes\* |
| AI upper body | `16 02 02 01` | all | yes\* |
| AI close-up | `16 02 02 02` | all | yes\* |
| AI headless | `16 02 02 03` | all | yes\* |
| AI lower body | `16 02 02 04` | all | yes\* |
| AI group | `16 02 01 00` | mitchell, cgevans, lxman | yes\* |
| AI hand | `16 02 03 00` | all | yes\* |
| AI whiteboard | `16 02 04 00` | all | yes\* |
| AI desk | `16 02 05 00` | lxman (mitchell marks it broken) | yes\* |
| HDR on / off | `01 01 01` / `01 01 00` | all | yes\* |
| FOV wide / medium / narrow (**Tiny 2 codes**) | `04 01 00` / `01` / `02` | lxman, cgevans | n/a |
| FOV wide / medium / narrow (**Lite codes**) | `04 01 01` / `02` / `03` | nod | yes\* |
| Sleep / wake (**Lite**) | `02 01 01` / `02 01 00` | nod | yes\* |
| Recentre (**Lite**) | `16 01 00 00` | nod | centre works\*\* |

\* Confirmed on a real Tiny 2 Lite with v0.1.0 (5/10/2026): AI tracking, HDR, field
of view, sleep/wake and gimbal movement were all reported working. Results per AI
mode weren't recorded individually.

\*\* Centring works, but the log of which command (framed `0x00C3` or this simple
fallback) the camera accepted wasn't captured.

The sources **disagree** on FOV codes and on sleep/recentre for the Lite. The
app picks codes by model ("protocol variant", which can be overridden on the
Developer tab; turn it on under Camera → Advanced). On the Lite, sleep uses the
simple command. Recentre always tries the framed command first and falls back to
the simple one when the camera doesn't reply.

## Selector 2: framed "V3" commands

```
off 0     0xAA magic
off 1     flags: 0x25 = SET with payload, 0x01 = header-only GET
off 2-3   sequence number, u16 little-endian
off 4-5   0x000C
off 6-7   header CRC (u16 LE) over bytes [0, 12) with 6-7 zeroed
off 8     sender 0x0A (host)
off 9     receiver: 0x02 camera, 0x03 gimbal, 0x04 AI
off 10-11 command, u16 LE
-- only when there is a payload --
off 12-13 payload length, u16 LE
off 14-15 payload CRC (u16 LE) over [12, 16 + len) with 14-15 zeroed
off 16..  payload (max 44 bytes), rest zero
```

- CRC-16/USB: reflected poly `0xA001`, init `0xFFFF`, xorout `0xFFFF`
  (check value of `"123456789"` is `0xB4C8`).
- After a SET, poll GET on selector 2 until the reply's sequence and command
  match (the mailbox keeps the previous reply). Replies take 50–100 ms.
- SETs with the wrong payload size are silently ignored. SETs while asleep are
  ignored (except wake).
- `tests/protocol_tests.cpp` checks our encoder against real captured frames.

| Function | Command | Receiver | Payload |
|---|---|---|---|
| Wake / sleep | `0xA0C2` | 0x02 | u32: 0 wake, 1 sleep |
| Recentre gimbal | `0x00C3` | 0x03 | 6 zero bytes |
| Gimbal speed | `0x6484` | 0x04 | 3 × f32 [roll, pitch, yaw] in °/s; all zero = stop |
| Gimbal move to angle | `0x6444` | 0x04 | 3 × f32 [roll, pitch, yaw] in degrees (not used yet) |
| Tracking speed | `0x0CC4` | 0x04 | u8: 0 standard, 2 sport |
| Zoom with speed | `0x1942` | 0x02 | u32 speed (1–10), u32 ratio × 100 (not used yet) |
| Exposure | `0x2982` | 0x02 | u8 mode, u32 value (not used yet) |

**Hazard (lxman):** sweeping through unknown GET opcodes once knocked the camera
off the USB bus. Don't fuzz. Upgrade (0x0D), system and BLE subsystems are off limits.

Sign convention for yaw is unclear (lxman: positive yaw turns towards the
camera's own left). The app has "Invert left/right" and "Invert up/down" settings.

## Selector 6 GET: status block

| Offset | Meaning |
|---|---|
| `0x02` | 1 = asleep (lxman) |
| `0x04–05` | zoom %, u16 LE |
| `0x06` | HDR on |
| `0x07` | face auto-exposure |
| `0x09` | device status: 1 run, 3 sleep, 4 privacy (cgevans) |
| `0x11` | FOV code (see above; 3 = custom zoom on Tiny 2) |
| `0x18`, `0x1C` | AI mode bytes (m, n), same as the set command; `(6, 0)` is a brief transient while switching |
| `0x24` | tracking speed: 0 standard, 2 sport |

## Finding unknown commands

If a feature doesn't work on the Tiny 2 Lite, the legitimate way to learn the
right bytes is a USB capture of the official OBSBOT Center app doing that action:
install [USBPcap](https://desowin.org/usbpcap/) + Wireshark on Windows, capture
while toggling the feature, and filter on `usb.setup.bRequest == 1` (SET_CUR) to
the camera. Then try the bytes on the app's Developer tab and record results here.
