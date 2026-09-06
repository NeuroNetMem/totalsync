# TotalSync

[![Documentation](https://readthedocs.org/projects/totalsync/badge/?version=latest)](https://totalsync.readthedocs.io/en/latest/)

TotalSync is a low-cost, open-source teensy-based synchronization system for multiple
recording techniques.

**[Read the documentation →](https://totalsync.readthedocs.io)**

<p align="left">
  <img src="docs/images/Module_totalsync_Figure1.png" width=50% height=50%><img src="docs/images/GUI_Figure1.png" width=50% height=50%>
 </p>

## Getting started

 * **[General Introduction to TotalSync](docs/introduction.md)**
 * **[Installation](docs/installation.md)**
 * **[Quick start](docs/quickstart.md)**
 * **[Pin usage](docs/pin-usage.md)**
 * **[Extending TotalSync for new devices](docs/extending.md)**
 * **[Videos and images](docs/images)**

## Command reference

 * **[`totalsync`](docs/totalsync.md)** — record a session and serve the live
   browser interface
 * **[`totalsync-decode`](docs/totalsync-decode.md)** — decode recorded `.b64`
   files into numpy, MATLAB or pynapple formats, with named channels
 * **[`totalsync-pinsheet`](docs/totalsync-pinsheet.md)** — generate the
   `pinSheet.json` channel map from the Teensy firmware source
 * **[`totalsync-2p`](docs/totalsync-2p.md)** — align ScanImage two-photon
   recordings with TotalSync telemetry

## Recording techniques

 * **[Voltage sensitive imaging](docs/vsi.md)**

Chapters for two-photon microscopy, electrophysiology / Neuropixel and virtual reality
are planned; the placeholder files under `docs/` are not published yet.

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

`Teensy41_Totalsync/` holds the firmware, and `docs/` the documentation above.

## Contribute

Please contact Morgane Audrain (morgane.audrain@donders.ru.nl) with questions or
concerns.
