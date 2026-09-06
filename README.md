# TotalSync

TotalSync is a low-cost, open-source teensy-based synchronization system for multiple
recording techniques.

<p align="left">
  <img src="Documentation/images/Module_totalsync_Figure1.png" width=50% height=50%><img src="Documentation/images/GUI_Figure1.png" width=50% height=50%>
 </p>

## Getting started

 * **[General Introduction to TotalSync](Documentation/introduction.md)**
 * **[Installation](Documentation/instruction.md)**
 * **[Quick Start](Documentation/start.md)**
 * **[Pin usage](Documentation/Running_instructions.md)**
 * **[Extending TotalSync for new devices](Documentation/update.md)**
 * **[Videos and images](Documentation/images)**

## Analysis tools

 * **[`totalsync-decode`](Documentation/totalsync-decode.md)** — decode recorded `.b64`
   files into numpy, MATLAB or pynapple formats, with named channels
 * **[`totalsync-pinsheet`](Documentation/totalsync-pinsheet.md)** — generate the
   `pinSheet.json` channel map from the Teensy firmware source
 * **[`totalsync-2p`](Documentation/totalsync-2p.md)** — align ScanImage two-photon
   recordings with TotalSync telemetry

## Recording techniques

 * **[Two-photon microscopy](Documentation/2p.md)**
 * **[Voltage sensitive imaging](Documentation/vsi.md)**
 * **[Electrophysiology / Neuropixel](Documentation/electrophysiology.md)**
 * **[Virtual reality](Documentation/vr.md)**

## Repository layout

This is a [uv workspace](https://docs.astral.sh/uv/concepts/projects/workspaces/) of
four distributions. `pip install totalsync` gets all of them; each is also installable on
its own, so a machine that only analyses recordings need not pull in the serial and GUI
dependencies.

| Path | Distribution | Provides |
| --- | --- | --- |
| `packages/totalsync_webinterface` | `totalsync-webinterface` | `totalsync` — acquisition, recording and the live browser interface |
| `packages/totalsync_utils` | `totalsync-utils` | `totalsync-decode`, `totalsync-pinsheet` |
| `packages/totalsync_2p` | `totalsync-2p` | `totalsync-2p` |
| `.` | `totalsync` | umbrella that depends on the three above |

`Teensy41_Totalsync/` holds the firmware, and `Documentation/` the documentation above.

## Contribute

Please contact Morgane Audrain (morgane.audrain@donders.ru.nl) with questions or
concerns.
