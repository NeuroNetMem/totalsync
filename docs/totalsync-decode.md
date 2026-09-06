# totalsync-decode

Decode TotalSync `.b64` files recorded by a Teensy microcontroller into analysis-ready formats.

## Synopsis

```
totalsync-decode <directory> [-o OUTPUT] [-f FORMAT] [-p PIN_JSON] [--concatenate] [-q]
```

## Arguments

| Argument | Description |
|---|---|
| `directory` | Directory containing `.b64` files to decode |
| `-o`, `--output DIR` | Output directory. If omitted, files are decoded but not saved. |
| `-f`, `--format FORMAT` | Output format: `mat` (default), `npy`, or `pynapple` |
| `-p`, `--pin-json FILE` | Path to a pin mapping JSON file (e.g. `pinSheet.json`) |
| `--concatenate` | Concatenate all files into one output (only with `--format pynapple`) |
| `-q`, `--quiet` | Suppress progress output |

## Raw data format

Each `.b64` file contains packets logged at ~1 kHz. Without a pin mapping, the decoded data has these fields:

| Field | Shape | Type | Description |
|---|---|---|---|
| `startTS` | `(n,)` | uint32 | Packet start timestamp in µs |
| `transmitTS` | `(n,)` | uint32 | Packet transmit timestamp in µs |
| `analog` | `(n, 8)` | uint16 | Analog input channels (0–7) |
| `digitalIn` | `(n, 16)` | uint8 | Digital input channels (0–15), binary |
| `digitalOut` | `(n, 16)` | uint8 | Digital output channels (0–15), binary |
| `longVar` | `(n, 8)` | uint32 | State variables (0–7) |
| `packetNums` | `(n,)` | uint32 | Packet sequence numbers |

## Pin mapping

Passing a `pinSheet.json` via `-p` replaces the raw indexed arrays with named channels. Only pins marked `"used": true` with a non-null `"for"` field are extracted.

```json
{
  "pins": [
    { "name": "digital_input_6",  "used": true,  "for": "Scanner Frame Clock (Input)" },
    { "name": "digital_output_4", "used": true,  "for": "Barcode (Scanner)" },
    { "name": "analog_input_1",   "used": true,  "for": "Lick Detection" }
  ],
  "states": [
    { "idx": 0, "name": "uncorrected_distance" },
    { "idx": 1, "name": "corrected_distance" }
  ]
}
```

With this mapping, the decoded dict will contain keys like `"Scanner Frame Clock (Input)"`, `"Lick Detection"`, `"uncorrected_distance"`, etc. instead of `digitalIn`, `analog`, and `longVar`.

See `docs/pinSheet_2026.json` for a full example.

## Output formats

### `mat` (default)

Saves one `.mat` file per decoded `.b64` file:

```
output/
  session1_decoded.mat
  session2_decoded.mat
```

### `npy`

Saves one `.npy` file per decoded `.b64` file (a structured NumPy array):

```
output/
  session1_decoded.npy
  session2_decoded.npy
```

### `pynapple`

Saves each channel as a [pynapple](https://pynapple.org) `Tsd` (1-D) or `TsdFrame` (2-D) object, time-indexed by the corrected TotalSync clock in µs. Requires `pynapple` to be installed (`pip install totalsync-utils[pynapple]`).

**Per-file** (default):

```
output/
  session1/behavior/
    Scanner_Frame_Clock_Input.npz
    Lick_Detection.npz
    uncorrected_distance.npz
    ...
  session2/behavior/
    ...
```

**Concatenated** (`--concatenate`): timestamps from successive files are shifted so the time axis is monotonically increasing. All files are merged into a single set of channel files:

```
output/behavior/
  Scanner_Frame_Clock_Input.npz
  Lick_Detection.npz
  ...
```

## Examples

```bash
# Decode all .b64 files in a directory and print a summary (no files saved)
totalsync-decode /data/session01

# Save as .mat files (default format)
totalsync-decode /data/session01 -o /output/session01

# Save as .mat with named channels from a pin sheet
totalsync-decode /data/session01 -o /output/session01 -p docs/pinSheet_2026.json

# Save as pynapple Tsd files with named channels
totalsync-decode /data/session01 -o /output/session01 -f pynapple -p docs/pinSheet_2026.json

# Concatenate multiple sessions into a single set of pynapple files
totalsync-decode /data/all_sessions -o /output/concatenated -f pynapple --concatenate -p docs/pinSheet_2026.json

# Save as .npy, suppress progress bar
totalsync-decode /data/session01 -o /output/session01 -f npy -q
```
