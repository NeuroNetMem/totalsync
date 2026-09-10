# TotalSync

TotalSync is a low-cost, open-source synchronization system for multi-modal neuroscience
recordings.
It can record, decode, and visualize synchronization data and other low-sampling rate data (e.g. behavioral) at a sampling rate of 1 kHz.
The system is based on a  [Teensy 4.1](https://www.pjrc.com/store/teensy41.html) swith 32 digital channels (16 digital and 16 analog) and 8 analog input channels plus 8 state variables. A web-based interface shows them live, and the recorded files may be decode straight into
numpy, MATLAB or [pynapple](https://pynapple.org).

The system provides a general, customizable solution to the need of complex neuroscience setup, whenever sub-millisecond synchronization is not required, which includes many systems neuroscience applications.

The general idea is to provide a way to align all data, recorded by Totalsync and other data acquisition systems on a single timeline, which is given by the Teensy itself. After this is done, downstream analyses do not need to take care of synchronization issues.

A system to robustly synchronize two-photon imaging data with the data recorded by TotalSync. is provided, via the `totalsync-2p` recording and API. This targets in particular ScanImage systems, but could be adapted to other systems.

A full blown pipeline for the preprocessing and synchronization of two-photon data is provided by [batch2p](https://github.com/NeuroNetMem/batch2p).

The package comes with decoding functions that can save the outputs in `.npy`, `.mat`, and `.pkl` formats, and in Pynapple format.

```{image} images/Module_totalsync_Figure1.png
:alt: The TotalSync module
:width: 100%
```

We have successfully used TotalSync with two-photon imaging,
voltage-sensitive imaging, and Neuropixels electrophysiology, in combination with
head-fixed virtual reality. A simple API to  interface TotalSync with [Unity](https://unity.com/) are also provided. We have used Unity to program all of our virtual reality experiments.

## Customization

Totalsync is a suite of Python packages and applications with a web-based GUI, and a number of utilities to facilitate decoding and interpretation of the recorded data.  It also includes Teensy firmware, written in C++. We provide a template implementation with basic capabilities as a starting point for customization, alongside a few examples of full-blown configurations that are in use in our lab.
While the Python side may be used as-is in most cases, adapting to the needs of a specific experiment will in general require writing some C++ code, so some (relatively basic) experience with programming and microcontroller development is required.

## the PinSheet

The wiring of the Teensy is described in a human-readable JSON file,the pinSheet. That file can be generated directly from the Teensy firmware, and is used by the decoding utilities to format the output in a easily readable format.

## The six commands

Totalsync provi
::::{grid} 1 1 2 2
:gutter: 3

:::{grid-item-card} `totalsync`
Opens the web interface and records data. Reads the Teensy over USB serial, writes session data to disk, and serves the live
interface.
+++
{doc}`Reference <totalsync>`
:::

:::{grid-item-card} `totalsync-decode`
Turn a recorded `.b64` session into analysis-ready arrays, with channels named from a pinSheet.
+++
{doc}`Reference <totalsync-decode>`
:::

:::{grid-item-card} `totalsync-pinsheet`
Generate that pin sheet from the Teensy firmware source, so channel names track the code, reducing possibilities for human error
+++
{doc}`Reference <totalsync-pinsheet>`
:::

:::{grid-item-card} `totalsync-pinout`
Draw that pin sheet onto a picture of the board, to provide a reference for the wiring job at the bench.
+++
{doc}`Reference <totalsync-pinout>`
:::

:::{grid-item-card} `totalsync-firmware`
Provides the stub for  a PlatformIO project of your own for the Teensy firmware, ready to modify and
flash.
+++
{doc}`Reference <totalsync-firmware>`
:::

:::{grid-item-card} `totalsync-2p`
Align ScanImage two-photon recordings with the data recorded in totalsync.
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
firmware
pin-usage
extending
```

```{toctree}
:caption: Command reference
:maxdepth: 1

totalsync
totalsync-decode
totalsync-pinsheet
totalsync-pinout
totalsync-firmware
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
