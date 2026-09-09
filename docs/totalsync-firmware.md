# totalsync-firmware

Hand out the Teensy firmware that ships with `totalsync-utils` — copy it out to a
PlatformIO project of your own, or say where the bundled copy is.

## Synopsis

```
totalsync-firmware init DIRECTORY [--experiment NAME] [--force] [-q]
totalsync-firmware path
totalsync-firmware list [-q]
```

## Why it exists

The firmware is meant to be modified, which makes an installed Python package a bad place
to keep it: `site-packages`, and the hashed directory `uv tool install` uses, are not
somewhere to develop, and they are replaced wholesale on upgrade.

So the copy inside the wheel is a template rather than a working tree, and `init` copies
it *out* to a directory you name and own — the same arrangement as
`django-admin startproject` or `cargo new`. What comes out is a complete PlatformIO
project that `pio run` can build immediately.

## `init`

```bash
totalsync-firmware init my-rig
```

| Argument | Description |
|---|---|
| `DIRECTORY` | Directory to create the project in. Parents are created as needed. |
| `--experiment NAME` | Also copy the template experiment to an experiment called `NAME` and add a matching PlatformIO environment |
| `--force` | Write into a directory that is not empty, overwriting files that collide. **Never deletes anything.** |
| `-q`, `--quiet` | Do not print what was created |

A non-empty directory is refused unless `--force`, so a mistyped path cannot quietly
scatter files into an existing project. `--force` overwrites files that collide and leaves
everything else alone — nothing is ever removed, so it is not a way to lose work.

No `git init` is run. Scaffolding into an existing repository is the common case, and a
nested repository there would be a quiet surprise.

With `--experiment`, three things happen: the template experiment directory is copied
under the new name, the class inside it is renamed (`my_task` → `MyTaskExperiment`), and
an `[env:my_task]` block is appended to `platformio.ini`. The block is appended as text
rather than written back through a config parser, so the comments and the
`${env.build_flags}` references in the file survive. The class rename is cosmetic — the
class lives in an anonymous namespace and `makeExperiment()` is the only symbol that
matters — but three classes called `TemplateExperiment` in one tree gets confusing.

## `path` and `list`

`path` prints the bundled project's location and nothing else, so it substitutes into
other commands:

```bash
totalsync-pinsheet "$(totalsync-firmware path)/src/main.cpp" \
    -I "$(totalsync-firmware path)/src/experiments/template_experiment"
```

`list` prints the PlatformIO environment names one per line on stdout, with their build
settings on stderr, so it is both readable and pipeable:

```console
$ totalsync-firmware list
3 experiment(s) in .../totalsync_utils/data/firmware
  slm_aatc               -D USB_TRIPLE_SERIAL -D SLM_DEBUG=1 -I src/experiments/slm_aatc
  ofl_shock              -D USB_TRIPLE_SERIAL -I src/experiments/ofl_shock
  template_experiment    -D USB_TRIPLE_SERIAL -I src/experiments/template_experiment
slm_aatc
ofl_shock
template_experiment
```

## Examples

```bash
# Start a firmware project of your own, without cloning anything
totalsync-firmware init my-rig
cd my-rig && pio run -e template_experiment -t upload

# ...with an experiment of your own, copied from the template
totalsync-firmware init my-rig --experiment my_task
cd my-rig && pio run -e my_task

# A pin sheet straight from the bundled firmware, no project needed
totalsync-pinsheet --bundled --experiment slm_aatc -o pinSheet.json

# ...and a diagram of it
totalsync-pinsheet --bundled -q | totalsync-pinout - -o pinout.png
```

## The bundled copy and the repository

The payload is staged into the package at *build* time from the repository's `firmware/`
directory, which stays the single source of truth. One consequence in development: after
editing `firmware/` in a checkout, the bundled copy is one
`uv sync --reinstall-package totalsync-utils` behind. For live work, name the project
directory instead of using `--bundled` — `totalsync-pinsheet firmware --experiment slm_aatc`
reads the tree you are actually editing.

## Python API

```python
from totalsync_utils import scaffold_firmware, bundled_firmware_path, list_experiments

project = scaffold_firmware("my-rig", experiment="my_task")
print(bundled_firmware_path(), list_experiments())
```

`experiment_build_settings(name, root)` returns the `(defines, include_dirs)` an
environment needs, read out of `platformio.ini` — this is what
{doc}`totalsync-pinsheet` uses to generate a sheet for the right build configuration.

See {doc}`firmware` for what the firmware itself does and how to write an experiment.
