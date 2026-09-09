# totalsync-webinterface

Acquisition side of [TotalSync](https://github.com/NeuroNetMem/totalsync): reads packets from a
Teensy 4.1 running the TotalSync firmware over USB serial, records them, and serves a
live view of all 42 digital/analog channels plus 8 state variables to the browser at
1 kHz.

Installs the `totalsync` command. See the
[installation guide](../../docs/installation.md) and
[running instructions](../../docs/pin-usage.md).

```bash
totalsync --pinsheet docs/pinSheet_2026.json
totalsync -D                 # simulated Teensy, no hardware needed
```

Needs `tkinter` for its startup dialogs, which is stdlib but not pip-installable — see
the installation guide if your Python build lacks it.
