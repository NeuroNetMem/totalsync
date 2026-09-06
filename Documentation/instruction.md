# Installation

TotalSync is distributed as four Python packages, built with
[uv](https://docs.astral.sh/uv/); the older `setup.py` / conda workflow is no longer
used. Installing `totalsync` gets all of them:

| Distribution | Commands | What it is for |
| --- | --- | --- |
| `totalsync-webinterface` | `totalsync` | Recording: talk to the Teensy, serve the live browser interface |
| `totalsync-utils` | `totalsync-decode`, `totalsync-pinsheet` | Turning recordings into analysis-ready data |
| `totalsync-2p` | `totalsync-2p` | Aligning two-photon imaging with the telemetry |
| `totalsync` | — | Umbrella that depends on the three above |

Each is also installable on its own, which is worth doing on an analysis machine: only
`totalsync-webinterface` needs the serial port, the GUI toolkit and ZeroMQ.

It runs on Windows, macOS and Linux.

> **Note on folder names:** older versions of these instructions required the source
> folder to be named exactly `TotalSync`. That is no longer the case — you can clone or
> unpack the project anywhere, under any name.

---

## 1. Requirements

* **Python 3.11 or newer.**
* **Tkinter**, which the graphical dialogs of the `totalsync` command need — the
  analysis commands do not. It is part of the Python standard library but is *not*
  installable with `pip`, so it has to come from your Python build:
  * Python installed by **uv**, from **python.org**, or from **conda** — already included.
  * **Homebrew** Python (macOS) — `brew install python-tk`
  * **Debian / Ubuntu** — `sudo apt install python3-tk`
  * **Fedora** — `sudo dnf install python3-tkinter`
* A **Teensy** running the TotalSync firmware (see [Teensy firmware](#5-teensy-firmware)).
  You can install and try the software without one — see the `-D` flag under
  [Running TotalSync](#4-running-totalsync).

`windows-curses` is required only on Windows and is installed automatically there.

### Installing uv

Every command below uses `uv`. Install it once:

**macOS / Linux**
```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
```

**Windows (PowerShell)**
```powershell
powershell -ExecutionPolicy ByPass -c "irm https://astral.sh/uv/install.ps1 | iex"
```

Then check it:
```bash
uv --version
```

If you would rather not install uv, every step has a plain `pip` equivalent, given
alongside it.

---

## 2. Installing from PyPI

This is the route for **using** TotalSync rather than developing it.

> TotalSync is not published on PyPI yet. Until it is, use
> [Installing from source](#3-installing-from-source).

### Recommended: as a standalone tool

`uv tool install` puts TotalSync in its own isolated environment and places its
commands on your `PATH`, so there is no environment to activate before use:

```bash
uv tool install totalsync
```

That's it — `totalsync`, `totalsync-decode`, `totalsync-pinsheet` and `totalsync-2p` are
now available in any terminal.

On a machine that only analyses recordings, install just the part you need and skip the
serial, GUI and ZeroMQ dependencies:

```bash
uv tool install totalsync-utils      # totalsync-decode, totalsync-pinsheet
uv tool install totalsync-2p         # totalsync-2p
```

### Alternative: into a virtual environment

If you prefer a normal virtual environment, for instance to import TotalSync from your
own analysis scripts:

```bash
uv venv
uv pip install totalsync
```

<details>
<summary>Without uv (plain <code>pip</code>)</summary>

```bash
python -m venv .venv
# macOS / Linux:
source .venv/bin/activate
# Windows (PowerShell):
.venv\Scripts\Activate.ps1

pip install totalsync
```
</details>

---

## 3. Installing from source

Use this if you want to modify TotalSync, or before it is published to PyPI.

### Step 1 — Get the code

```bash
git clone <repository-url>
cd TotalSync
```

All four commands become available, not just `totalsync`:

```bash
uv run totalsync --help
uv run totalsync-decode --help
uv run totalsync-pinsheet --help
uv run totalsync-2p --help
```

### Step 2 — Create the environment and install

```bash
uv sync
```

`uv sync` does everything in one step: it picks a suitable Python, creates a `.venv/` in
the project folder, installs the dependencies at the exact versions recorded in
`uv.lock`, and installs all four TotalSync packages in **editable** mode — so your edits
to the source take effect immediately, with no reinstall.

The repository is a [uv workspace](https://docs.astral.sh/uv/concepts/projects/workspaces/):
the packages under `packages/` share one `uv.lock` and one `.venv/`, and are resolved
against each other from the source tree rather than from PyPI. There is nothing extra to
run per package — `uv sync` at the top covers all of them.

To work on a single package in isolation, `uv sync --package totalsync-utils`.

### Step 3 — Run it

```bash
uv run totalsync
```

`uv run` uses the project environment without you having to activate it, and re-syncs
first if dependencies have changed. If you prefer activating:

```bash
# macOS / Linux:
source .venv/bin/activate
# Windows (PowerShell):
.venv\Scripts\Activate.ps1

totalsync
```

<details>
<summary>Without uv (plain <code>pip</code>)</summary>

An editable install with `pip` works too. Note that it resolves dependencies fresh
rather than using `uv.lock`, so you may get different versions than other developers:

```bash
python -m venv .venv
source .venv/bin/activate          # Windows: .venv\Scripts\Activate.ps1
pip install -e .
totalsync
```
</details>

### Optional extras

`pyglet` and `sounddevice` are only needed for the visual-stimulus and audio parts. They
are grouped in an optional extra named `extras`:

```bash
uv sync --extra extras             # from source
uv tool install "totalsync[extras]"   # from PyPI, as a tool
uv pip install "totalsync[extras]"    # from PyPI, into a venv
```

With plain pip: `pip install -e ".[extras]"` or `pip install "totalsync[extras]"`.

---

## 4. Running TotalSync

Start it with:

```bash
totalsync
```

(or `uv run totalsync` from a source checkout). A startup window opens and asks which
serial port the Teensy is on. Useful command-line options:

| Option | Meaning |
| --- | --- |
| `-s`, `--serial_port` | Serial port of the Teensy. Skips the startup dialog. |
| `-D`, `--dummy` | Use a simulated Teensy — lets you try TotalSync with no hardware. |
| `-B`, `--binfile` | Also write a decoded binary dump alongside the base64 one. |
| `-C`, `--curses` | Show the text-mode status interface in the terminal. |
| `-H`, `--http_port` | Port for the web interface (default 8000). |
| `-w`, `--ws_port` | Port for the WebSocket server (default 5678). |
| `--pinsheet` | A `pinSheet.json` saying what each pin is wired to. Channels with a `"for"` entry are labelled with it in the browser interface instead of their generated names; the rest keep those names. |
| `-v`, `--verbose` | More logging detail. Logging is already at info level by default; `-v` switches it to debug. |

Run `totalsync --help` for the full list.

Serial port names differ per platform. The startup dialog offers a drop-down of the
ports it detects, and the same list is written to the terminal when TotalSync starts, so
you normally do not need to look them up yourself. For reference, they look like:

* **Windows** — `COM3`, `COM9`, …
* **macOS** — `/dev/cu.usbmodem14201`, …
* **Linux** — `/dev/ttyACM0`, `/dev/ttyUSB0`, …

On Linux you may need to be in the `dialout` group to open the port:
`sudo usermod -a -G dialout $USER` (then log out and back in).

To try things out without a Teensy attached:

```bash
totalsync -D
```

To see your own channel names in the interface rather than `digital_input_6`, pass a pin
sheet. `Documentation/pinSheet_2026.json` is the one for the OFL rig; generate your own
from the firmware with [`totalsync-pinsheet`](totalsync-pinsheet.md), then edit the
`"for"` fields to match how the rig is actually wired:

```bash
totalsync --pinsheet Documentation/pinSheet_2026.json
```

The browser console reports how many labels it received (`Channel labels from pin sheet:
29`), which is the quickest way to tell a missing `--pinsheet` from a sheet whose channel
names do not match.

For what to do once it is running, see the rest of the documentation linked from the
[README](../README.md) — in particular [Quick Start](start.md) and the pin assignments in
[Running instructions](Running_instructions.md).

---

## Updating

**Installed as a tool:**
```bash
uv tool upgrade totalsync
```

**Installed in a virtual environment:**
```bash
uv pip install --upgrade totalsync
```

**From source:** editable installs pick up code changes automatically, so you only need
to re-sync when dependencies change:
```bash
git pull
uv sync
```

## Uninstalling

```bash
uv tool uninstall totalsync      # if installed as a tool
uv pip uninstall totalsync       # if installed in a virtual environment
```

A source checkout is removed by deleting the folder, including its `.venv/`.

---

## For maintainers: building and publishing

Package metadata lives in `pyproject.toml`; there is no `setup.py` to maintain. Build
the distributions with:

```bash
uv build
```

This writes a wheel and a source archive to `dist/`. Check the wheel actually contains
the web interface assets, since they live outside the Python package and are easy to
lose:

```bash
uv run python -m zipfile -l dist/totalsync-*.whl | grep web/
```

Publish to PyPI with:

```bash
uv publish
```

`uv publish` will ask for an API token, or read one from `UV_PUBLISH_TOKEN`. Test the
whole thing against TestPyPI first:

```bash
uv publish --publish-url https://test.pypi.org/legacy/
```

Bump `version` in `pyproject.toml` before each release; PyPI refuses to accept the same
version twice.

When you change dependencies, commit the updated `uv.lock` along with `pyproject.toml`
— that lock file is what guarantees every machine in the lab gets identical versions.

---

## 5. Teensy firmware

Open `Teensy41_Totalsync/Teensy41_Totalsync.ino` in the Arduino IDE and upload it to the
Teensy. You can modify it as needed; see
[Extending Totalsync for new devices](update.md) and the setup-specific examples linked
from the [README](../README.md).
