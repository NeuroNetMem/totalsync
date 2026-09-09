# Introduction

TotalSync is a low-cost, open-source, Teensy-based synchronization system. A Teensy 4.1
is coupled to a web-based Python interface and produces a data file recording 42
analog/digital channels plus 8 long variables — the latter able to hold more complex
data — at a rate of 1 kHz. The result is full control over all the hardware and triggers
used in a setup, with a user-friendly interface on top.

We built it to be adaptable and inexpensive: TotalSync can synchronize as many modules as
you choose. To show that adaptability we tested it in combination with head-fixed virtual
reality and several recording techniques:

* a two-photon microscope
* a voltage-sensitive microscope
* electrophysiology / Neuropixel

This repository holds all the code needed to run TotalSync, and the use cases above.

```{image} images/GUI_Figure1.png
:alt: The TotalSync browser interface, showing analog, state and digital channels
:width: 100%
```

## How the pieces fit together

| | |
| --- | --- |
| **Firmware** | `firmware/` — the PlatformIO project that samples the pins and emits packets |
| **Recording** | The `totalsync` command reads those packets, writes a `.b64` session file, and serves the live interface above |
| **Analysis** | `totalsync-decode` turns the session into numpy, MATLAB or [pynapple](https://pynapple.org) objects; `totalsync-2p` aligns it with two-photon imaging |
| **Channel names** | A `pinSheet.json` maps pin positions to what they are wired to, and is used by both the interface and the decoder |

Next: [Installation](installation.md).
