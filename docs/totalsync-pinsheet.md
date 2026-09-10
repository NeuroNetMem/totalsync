# totalsync-pinsheet

Generate a `pinSheet.json` pin map from the Teensy firmware source, so the map
`totalsync-decode` uses to name channels stays in step with the code that produces
the data.


## Synopsis

```
totalsync-pinsheet [<source>] [--bundled] [--experiment NAME] [-o OUTPUT]
                   [-D NAME[=VALUE]] [-U NAME] [-I DIR]
                   [--merge PINSHEET] [--prefer-existing]
                   [--exclude MACRO] [--acronym WORD]
                   [--title TITLE] [--updated YYYYMMDD]
                   [--state IDX=NAME] [--no-infer-states] [-q]
```

## Arguments

| Argument | Description |
|---|---|
| `source` | Teensy firmware source (`.cpp` / `.ino`), or a PlatformIO project directory, in which case the source and the build settings come from its `platformio.ini` |
| `--bundled` | Use the firmware bundled with this package instead of naming a source |
| `--experiment NAME` | PlatformIO environment to generate the sheet for. Needs `--bundled` or a project directory. Default: `template_experiment`. |
| `-o`, `--output FILE` | Where to write the sheet. `-` (the default) writes to stdout. |
| `-D`, `--define NAME[=VALUE]` | Define a macro, overriding any `#define` of it in the source. Repeatable. |
| `-U`, `--undef NAME` | Leave a macro undefined, ignoring any `#define` of it in the source. Repeatable. |
| `-I`, `--include DIR` | Directory to search for headers. Repeatable. Headers that are not found are skipped. |
| `--exclude MACRO` | Never read `MACRO` as a pin name. Repeatable. |
| `--acronym WORD` | Extra word to keep upper case in labels. Repeatable. |
| `--title TITLE` | `title` field (default: the experiment name for a project, otherwise the source file name) |
| `--updated YYYYMMDD` | `updated` field (default: today) |
| `--merge PINSHEET` | Existing sheet to take the title, states and otherwise unknown `for` labels from |
| `--prefer-existing` | With `--merge`, let the existing labels win over the macro names |
| `--state IDX=NAME` | Name of state vector slot `IDX`. Repeatable; overrides the guessed names. |
| `--no-infer-states` | Do not guess state vector names from the source |
| `-q`, `--quiet` | Do not print the summary of what was found |

## What it reads

The channel order comes from the firmware's pin arrays: `pinsDigitalIn`,
`pinsDigitalOut` and `pinsAnalogIn` give the `teensy_pin` of each
`digital_input_N` / `digital_output_N` / `analog_input_N` slot, and `pinsDigital`
— the array the `instUNITY` instruction indexes — gives the `unity` number. A
digital pin that is missing from `pinsDigital` gets `"unity": null`: Unity cannot
address it.

The `for` labels come from the `#define`s. A macro is read as a pin name when

* it is an object-like macro whose body evaluates to an integer,
* that integer appears in one of the arrays above, and
* it is not a compile-time configuration flag, i.e. the preprocessor never
  branches on it.

The middle rule leaves out pins that never reach a packet, such as the
`LOOP_INDICATOR 40` / `GATHER_INDICATOR 41` scope probes. The last one keeps
`#define SLM_DEBUG 1` from being read as a name for digital input pin 1. A plain
integer constant that happens to equal a sampled pin number will still be picked
up by mistake — use `--exclude` for those.

Macro names are decapitalised and underscores become spaces, so
`SCANNER_FRAME_CLOCK` becomes `Scanner Frame Clock`; known acronyms and single
letters stay upper case, so `PIN_SYNC_LED` becomes `Pin Sync LED` and `TRIGGER_C`
becomes `Trigger C`. Add your own with `--acronym`.

State vector names are guessed from the `packet.variables[i] = <variable>;`
assignments, since the firmware declares no names for them. Override them with
`--state`, keep the ones from an existing sheet with `--merge`, or drop them with
`--no-infer-states`.

## Build configurations

The source is run through a real C preprocessor, so only the branches the build
actually compiles are considered. In `docs/examples/main_example.cpp`, pin 35 is
`DEBUG_FRAME_CLOCK_OUT` under `SLM_DEBUG` and `TESTSHOCK` without it, and the
generated sheet says whichever the configuration selects.

Unlike a compiler's `-D` / `-U`, these options override the `#define`s in the
source, which is what lets you generate the sheet for a configuration without
editing the firmware.

## Naming a PlatformIO project

Because a pin sheet is only right for one build configuration, the easiest thing to name
is not a file but the firmware project itself:

```bash
totalsync-pinsheet firmware --experiment slm_aatc -o pinSheet.json
```

Given a directory, the source is taken to be its `src/main.cpp` and the `-I` and `-D` are
read out of `platformio.ini` for the environment `--experiment` names (default
`template_experiment`). That is more than a convenience: `main.cpp` includes
`experiment_config.h`, which only exists inside one experiment's directory, so without the
right include path *no* pins are named at all — and in `slm_aatc`, `-D SLM_DEBUG=1` is
what names two of the output pins, so a sheet generated without it would describe a
different build from the one PlatformIO produces.

Command-line `-D` and `-U` still win over `platformio.ini`, so a configuration can still
be explored without editing anything.

`--bundled` does the same for the firmware that ships inside `totalsync-utils`, which
needs no checkout at all — see {doc}`totalsync-firmware`.

## Examples

`docs/examples/main_example.cpp` is a single-file sketch kept alongside these docs, frozen
from before the firmware was split into per-experiment directories, because it exercises
the build-configuration handling described above in one file: `-U SLM_DEBUG` turns pins 32
and 35 from `SLM Debug Out` / `Debug Frame Clock Out` into `Preshock` / `Testshock`.

```bash
# The whole project, for one experiment
totalsync-pinsheet firmware --experiment slm_aatc -o pinSheet.json

# The firmware bundled with this package, with no checkout
totalsync-pinsheet --bundled --experiment slm_aatc -o pinSheet.json

# A single source file still works, if you supply the include path yourself
totalsync-pinsheet firmware/src/main.cpp -I firmware/src/experiments/slm_aatc

# Generate the sheet for the production build instead of the bench build
totalsync-pinsheet docs/examples/main_example.cpp -U SLM_DEBUG -o pinSheet.json

# Regenerate after a firmware change, keeping the hand written labels of an existing
# sheet wherever the firmware has nothing better to offer. This is the one case where
# writing back over a sheet you have edited is the intent rather than an accident.
totalsync-pinsheet firmware --experiment slm_aatc \
    --merge docs/pinSheet_2026.json -o docs/pinSheet_2026.json

# ...and feed the result straight to the decoder
totalsync-decode /data/session01 -o /output/session01 -p docs/pinSheet_2026.json
```

## Python API

```python
from totalsync_utils import generate_pin_sheet

sheet = generate_pin_sheet("firmware/src/main.cpp",
                           include_dirs=["firmware/src/experiments/slm_aatc"])
```

`parse_firmware()` returns the raw `FirmwarePinMap` (pin arrays, pin `#define`s,
configuration flags, guessed state names) if you want to inspect the firmware
rather than build a sheet.
