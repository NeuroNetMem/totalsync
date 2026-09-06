# totalsync-webinterface

Acquisition side of [TotalSync](https://github.com/*/TotalSync): reads packets from a
Teensy 4.1 running the TotalSync firmware over USB serial, records them, and serves a
live view of all 42 digital/analog channels plus 8 state variables to the browser at
1 kHz.

Installs the `totalsync` command. See the
[installation guide](../../Documentation/instruction.md) and
[running instructions](../../Documentation/Running_instructions.md).

```bash
totalsync --pinsheet Documentation/pinSheet_2026.json
totalsync -D                 # simulated Teensy, no hardware needed
```

Needs `tkinter` for its startup dialogs, which is stdlib but not pip-installable — see
the installation guide if your Python build lacks it.
