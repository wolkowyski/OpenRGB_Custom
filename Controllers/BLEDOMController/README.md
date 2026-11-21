# ELK-BLEDOM Bluetooth LE controller

Driver for the cheap Bluetooth LE LED strips that advertise as **ELK-BLEDOM**
(sold under many brands, e.g. *Lambo Tech*). Windows only for now, using the
WinRT Bluetooth LE GATT API (no extra dependencies, links `windowsapp.lib`).

## Setup

1. Find the strip's MAC address (e.g. with *nRF Connect* on your phone, or in
   the device properties after a scan).
2. In OpenRGB: **Settings → Manually Added Devices → Add → ELK-BLEDOM (Bluetooth LE)**.
3. Enter a name and the MAC, save and rescan.

Settings end up in `OpenRGB.json`:

```json
"BLEDOMDevices": {
    "devices": [
        { "name": "Desk strip", "mac": "FF:FF:10:E0:4D:84" }
    ]
}
```

The strip only accepts one BLE connection at a time: close the phone app (or
any other controller) before starting OpenRGB.

## Protocol

Commands are 9 byte frames written without response to characteristic
`0000fff3-0000-1000-8000-00805f9b34fb` (`0000ffe9-…` on some variants):

| Command      | Frame                          |
|--------------|--------------------------------|
| Static color | `7E 07 05 03 RR GG BB 10 EF`   |

## Design notes

- Detection is instant: each strip gets a worker thread that connects in the
  background and reconnects every 5 s if the link drops.
- Only the latest color is kept and writes are spaced ≥ 30 ms apart, so
  effects running at high FPS do not flood the strip.
- Brightness is applied by scaling the color; *Off* sends black.
