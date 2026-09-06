# TotalSync

TotalSync is a low-cost, open-source synchronization system for multi-modal neuroscience
recordings. A Teensy 4.1 samples 42 digital and analog channels plus 8 state variables at
1 kHz, a browser interface shows them live, and the recorded files decode straight into
numpy, MATLAB or [pynapple](https://pynapple.org).

```{image} images/Module_totalsync_Figure1.png
:alt: The TotalSync module
:width: 100%
```

It was built to be adaptable and inexpensive, and has been used with two-photon imaging,
voltage-sensitive imaging, and Neuropixel electrophysiology, in combination with
head-fixed virtual reality.

## The four commands

::::{grid} 1 1 2 2
:gutter: 3

:::{grid-item-card} `totalsync`
Record. Reads the Teensy over USB serial, writes the session, and serves the live
interface.
+++
{doc}`Reference <totalsync>`
:::

:::{grid-item-card} `totalsync-decode`
Turn a recorded `.b64` session into analysis-ready arrays, with channels named from a pin
sheet.
+++
{doc}`Reference <totalsync-decode>`
:::

:::{grid-item-card} `totalsync-pinsheet`
Generate that pin sheet from the Teensy firmware source, so channel names track the code.
+++
{doc}`Reference <totalsync-pinsheet>`
:::

:::{grid-item-card} `totalsync-2p`
Align ScanImage two-photon recordings with the behavioural telemetry.
+++
{doc}`Reference <totalsync-2p>`
:::

::::

New here? Start with {doc}`introduction`, then {doc}`installation`.

```{toctree}
:caption: Getting started
:maxdepth: 2

introduction
installation
quickstart
pin-usage
extending
```

```{toctree}
:caption: Command reference
:maxdepth: 1

totalsync
totalsync-decode
totalsync-pinsheet
totalsync-2p
```

```{toctree}
:caption: API reference
:maxdepth: 2

api/index
```

```{toctree}
:caption: Recording techniques
:maxdepth: 1

vsi
```
