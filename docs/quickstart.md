# Quick start

Once TotalSync is [installed](installation.md), a session looks like this.

## 1. Record

```bash
totalsync --pinsheet docs/pinSheet_2026.json
```

Pick the serial port in the startup window; the browser interface opens on
<http://localhost:8000>. See the [`totalsync` reference](totalsync.md) for the other
options, and use `-D` to try it with no Teensy attached.

## 2. Decode

```bash
totalsync-decode /data/session01 -o /output/session01 -f pynapple \
    -p docs/pinSheet_2026.json
```

See the [`totalsync-decode` reference](totalsync-decode.md) for the output formats.

## 3. Align with imaging, if there is any

```bash
totalsync-2p --tif-files rec01.tif --b64-files rec01.b64 \
    --output-dir out/rec01 --pin-sheet docs/pinSheet_2026.json
```

See the [`totalsync-2p` reference](totalsync-2p.md).

## Adding a Teensy module

To change which signals you synchronize, modify the Arduino sketch in
`Teensy41_Totalsync/`. After a firmware change, regenerate the pin sheet so the channel
names follow the code — [`totalsync-pinsheet`](totalsync-pinsheet.md) does this, and
`--merge` keeps the labels you have already written by hand.

Worked examples for particular setups are collected under
[Recording techniques](vsi.md); the virtual reality, two-photon and electrophysiology
chapters are still to be written.
