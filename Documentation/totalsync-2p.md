# totalsync-2p

Align ScanImage two-photon recordings with TotalSync behavioural telemetry, so that
every imaging frame carries a timestamp on the same clock as the behaviour.

## Synopsis

```
totalsync-2p --tif-files TIF [TIF ...] --b64-files B64 [B64 ...]
             --output-dir DIR --pin-sheet JSON
```

## Arguments

| Argument | Meaning |
| --- | --- |
| `--tif-files TIF [TIF ...]` | One or more ScanImage tif recordings. |
| `--b64-files B64 [B64 ...]` | The TotalSync `.b64` files, **in the same order** as `--tif-files`. |
| `--output-dir DIR` | Where to write the results. Created if it does not exist. |
| `--pin-sheet JSON` | Pin map naming the telemetry channels — see [totalsync-pinsheet](totalsync-pinsheet.md). |

Files are processed pairwise: the first tif with the first b64, and so on. The two lists
must be the same length. A pair that fails is reported and does not stop the others; the
command exits non-zero at the end if any pair failed.

## Required channels

The pin sheet has to name two channels the way this tool looks them up, in the pins'
`"for"` fields:

* **`Scanner Frame Clock (Input)`** — required. The frame clock the microscope emits;
  its rising edges are the imaging frames.
* **`Barcode (Scanner)`** — optional but strongly preferred. Gives an absolute alignment
  between the two systems.

With a barcode on both sides, the two barcode trains are cross-correlated and the
resulting shift aligns them. Without one, alignment falls back to counting frame-clock
edges, which assumes no frames were dropped — the tool warns when it takes that path,
and warns again if the telemetry also has clock gaps, because the two together can put
frames on the wrong timestamps silently.

> A pin sheet that puts `Barcode (Scanner)` on the wrong pin is worse than one that omits
> it: the tool will align against whatever that pin actually carried. See the note at the
> end of this page.

## Output

Written under `--output-dir`, with `{session}` taken from the `.b64` filename stem:

| Path | Contents |
| --- | --- |
| `{session}_frames_time_idx.npz` | pynapple `Tsd` mapping scanner time → tif frame index. This is the alignment result. |
| `{session}_barcode_data.npz` | The raw decoded telemetry arrays. |
| `{session}_behavior_sync_stats.pkl` | Synchronization statistics (see below). |
| `behavior/{channel}.npz` | One pynapple `Tsd` (1-D channels) or `TsdFrame` (2-D) per telemetry channel, time-indexed by the corrected TotalSync clock in µs. Channel names are lower-cased with punctuation turned into underscores. |

The statistics dictionary records `session`, `has_barcode`, `max_ts_gap` (largest
telemetry clock gap), `gap_locations`, and — when a barcode was used — `barcode_shift`
and `barcode_frame_matches`.

## Examples

```bash
# One session
totalsync-2p --tif-files rec01.tif --b64-files rec01.b64 \
             --output-dir out/rec01 --pin-sheet Documentation/pinSheet_2026.json

# A day's recordings, paired in order
totalsync-2p --tif-files day1/*.tif --b64-files day1/*.b64 \
             --output-dir out/day1 --pin-sheet Documentation/pinSheet_2026.json
```

Beware of shell globbing in the second form: it only pairs correctly if both patterns
expand to the same order, which they do when tif and b64 share a naming scheme, and do
not when they don't. Pass the files explicitly if you are unsure.

## Python API

```python
from totalsync_2p import synchronize

stats = synchronize(
    tif_file='rec01.tif',
    b64_file='rec01.b64',
    output_dir='out/rec01',
    pin_sheet_file='pinSheet.json',
    fill_gaps=False,       # interpolate across frame-clock gaps
    ignore_barcode=False,  # force frame-clock alignment even if a barcode exists
)
```

`fill_gaps` and `ignore_barcode` are not exposed on the command line; use the library
call if you need them.

## Related

* [totalsync-decode](totalsync-decode.md) — decode `.b64` files without the imaging side.
* [totalsync-pinsheet](totalsync-pinsheet.md) — generate the pin sheet this tool needs.
