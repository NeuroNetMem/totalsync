# Installation

TotalSync is distributed as four Python packages, built with
[uv](https://docs.astral.sh/uv/); the older `setup.py` / conda workflow is no longer
used. Installing `totalsync` gets all of them:

| Distribution | Commands | What it is for |
| --- | --- | --- |
| `totalsync-webinterface` | `totalsync` | Recording: talk to the Teensy, serve the live browser interface |
| `totalsync-utils` | `totalsync-decode`, `totalsync-pinsheet`, `totalsync-pinout`, `totalsync-firmware` | Turning recordings into analysis-ready data, and the Teensy firmware |
| `totalsync-2p` | `totalsync-2p` | Aligning two-photon imaging with the telemetry |
| `totalsync` | — | Umbrella that depends on the three above |

Each is also installable on its own, which is worth doing on an analysis machine: only
`totalsync-webinterface` needs the serial port, the GUI toolkit and ZeroMQ.

It runs on Windows, macOS and Linux.

> **Note on folder names:** older versions of these instructions required the source
> folder to be named exactly `TotalSync`. That is no longer the case — you can clone or
> unpack the project anywhere, under any name.

---

## Requirements

* **Python 3.11 or newer.**
* **Tkinter**, which the graphical dialogs of the `totalsync` command need — the
  analysis commands do not. It is part of the Python standard library but is *not*
  installable with `pip`, so it has to come from your Python build:
  * Python installed by **uv**, from **python.org**, or from **conda** — already included.
  * **Homebrew** Python (macOS) — `brew install python-tk`
  * **Debian / Ubuntu** — `sudo apt install python3-tk`
  * **Fedora** — `sudo dnf install python3-tkinter`
* A **Teensy** running the TotalSync firmware (see [Teensy firmware](firmware.md)).
  You can install and try the software without one — see the `-D` flag under
  [Running TotalSync](#running-totalsync).
* [**PlatformIO**](https://platformio.org/) a cross-platform IDE and build system for embedded development, providing a more powerful and flexible environment for Teensy development than the Aruino IDE, with interfaces and plugins for many popular editors and IDEs, including CLion and VSCode. At a minimum, [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html) is required. Installation of the IDE or the plugins for VSCode or CLion are recommended
* `windows-curses` is required only on Windows and is installed automatically there.

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

## Installing from PyPI

This is the route for **using** TotalSync rather than developing it (besides firmware customization).

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

which can also be done separately on the analysis packages

```bash
uv tool install totalsync-utils      # totalsync-decode, totalsync-pinsheet
uv tool install totalsync-2p         # totalsync-2p
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

## Installing from source

Use this if you want to modify TotalSync. To just use it, install
[from PyPI](#installing-from-pypi) instead.

### Step 1 — Get the code

```bash
git clone https://github.com/NeuroNetMem/totalsync.git
cd totalsync
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
All six commands become available, not just `totalsync`:

```bash
uv run totalsync --help
uv run totalsync-decode --help
uv run totalsync-pinsheet --help
uv run totalsync-pinout --help
uv run totalsync-firmware --help
uv run totalsync-2p --help
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

## Running TotalSync

Start it with:

```bash
totalsync
```

(or `uv run totalsync` from a source checkout). A startup window opens and asks which
serial port the Teensy is on, and the ports it detected are listed in the terminal.

To check the install without a Teensy attached:

```bash
totalsync -D
```

The command-line options, the platform-specific serial port names, and how to get your
own channel names into the interface are in the [`totalsync` reference](totalsync.md).
The analysis commands have their own pages too: [`totalsync-decode`](totalsync-decode.md),
[`totalsync-pinsheet`](totalsync-pinsheet.md) and [`totalsync-2p`](totalsync-2p.md).

Once it is running, see [Quick Start](quickstart.md) and the pin assignments in
[Pin usage](pin-usage.md).

---

## Updating

**Installed as a tool:** name whichever package you installed — `totalsync` covers all
six commands, `totalsync-utils` and `totalsync-2p` upgrade separately if you installed
them on their own.
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
```

A tool install is self-contained, so that removes everything. In a virtual environment,
uninstalling the umbrella leaves the three packages it depended on behind:

```bash
uv pip uninstall totalsync totalsync-webinterface totalsync-utils totalsync-2p
```

A source checkout is removed by deleting the folder, including its `.venv/`.

---

## For maintainers: building and publishing

Package metadata lives in the `pyproject.toml` files; there is no `setup.py` to maintain.
The repository publishes **four distributions**, so most of the commands below need to be
told to act on all of them rather than just the one at the root.

### Versions

Each distribution carries its own `version`, and PyPI refuses to accept a version twice —
even if the upload is otherwise identical, and even after a release is deleted. So bump
the `version` of every package you actually changed before building:

| Distribution | Where |
| --- | --- |
| `totalsync` | `pyproject.toml` |
| `totalsync-webinterface` | `packages/totalsync_webinterface/pyproject.toml` |
| `totalsync-utils` | `packages/totalsync_utils/pyproject.toml` |
| `totalsync-2p` | `packages/totalsync_2p/pyproject.toml` |

The umbrella `totalsync` is the one to watch: it has no code of its own, so it is easy to
forget, but it is the package most people install.

### Build

```bash
uv build --all-packages
```

`--all-packages` is not optional. Plain `uv build` at the root builds only the umbrella
distribution and silently leaves the other three at whatever version was in `dist/`
before. The result is eight files in `dist/` — a wheel and a source archive each.

Then check the two things that can go missing without the build failing. The browser
interface ships inside `totalsync-webinterface`, not the umbrella:

```bash
uv run python -m zipfile -l dist/totalsync_webinterface-*.whl | grep web/
```

Fourteen files should be listed; if it comes up empty the wheel would install a blank
interface. The Teensy firmware ships inside `totalsync-utils`:

```bash
uv run python -m zipfile -l dist/totalsync_utils-*.whl | grep -c 'data/firmware/'
```

Unlike the interface, this payload *is* generated at build time:
`packages/totalsync_utils/hatch_build.py` stages a filtered copy of the repository's
`firmware/` directory into the package. So it is worth checking that the two builds
`uv build` chains together — the source distribution, and then the wheel *from* that
source distribution — both carry it:

```bash
tar -tzf dist/totalsync_utils-*.tar.gz | grep -c 'data/firmware/'   # same number of files
```

If they ever disagree, `uv build --package totalsync-utils --sdist --wheel` builds both
from the checkout instead of chaining them, which isolates which of the two is at fault.

### Test against TestPyPI first

TestPyPI is a separate registry with its own account and its own API tokens; register at
<https://test.pypi.org> before the first upload. Nothing you do there affects PyPI.

```bash
uv publish --publish-url https://test.pypi.org/legacy/ --dry-run   # look before you leap
uv publish --publish-url https://test.pypi.org/legacy/
```

Installing from TestPyPI to check the result needs one extra flag, because none of the
dependencies (numpy, scipy, pandas, pynapple, tifffile, …) are published there:

```bash
uv pip install \
    --index-url https://test.pypi.org/simple/ \
    --extra-index-url https://pypi.org/simple/ \
    --index-strategy unsafe-best-match \
    totalsync
```

`--index-strategy unsafe-best-match` matters: uv otherwise stops at the first index that
carries a name at all, and TestPyPI holds stale placeholder uploads of common package
names that it would then prefer over the real ones.

### Publish

```bash
uv publish
```

`uv publish` reads a token from `UV_PUBLISH_TOKEN`, or asks for one. Two flags are worth
knowing:

* `--dry-run` does everything except the upload.
* `--check-url https://pypi.org/simple/` skips files that are already on the index instead
  of failing the whole batch on one duplicate — useful when only some of the four
  distributions changed.

All eight files upload in one call and the order does not matter. Installation order does:
`totalsync-2p` depends on `totalsync-utils`, and the umbrella depends on all three, so
publish them together rather than leaving a release half-done.

:::{tip}
For releases from CI, [Trusted Publishing](https://docs.pypi.org/trusted-publishers/)
(`uv publish --trusted-publishing automatic`) exchanges a short-lived OIDC token for
upload rights, so no long-lived API token has to be stored anywhere. It is registered per
project on PyPI, so that is four registrations here — in exchange for four fewer tokens.
:::

### The lock file

When you change dependencies, commit the updated `uv.lock` along with the `pyproject.toml`
— that lock file is what guarantees every machine in the lab, and the ReadTheDocs builder,
gets identical versions.

---

## Teensy firmware

The firmware is a [PlatformIO](https://platformio.org) project. It ships inside
`totalsync-utils`, so a PyPI install already has it — copy it out to a directory of your
own and build:

```bash
totalsync-firmware init my-rig
cd my-rig
pio run -e template_experiment -t upload
```

From a source checkout it is already there, in `firmware/`.

[Teensy firmware](firmware.md) covers the toolchain, the three example experiments and how
to write your own; see also
[Extending TotalSync for new devices](extending.md) and the setup-specific examples under
[Recording techniques](vsi.md).
