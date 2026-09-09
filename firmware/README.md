# TotalSync Teensy firmware

The firmware for a [TotalSync](https://github.com/NeuroNetMem/totalsync) module: a
Teensy 4.1 samples 7 analog and 32 digital channels plus 8 state variables every
millisecond and emits them as COBS-framed packets over USB serial.

This is a [PlatformIO](https://platformio.org) project. Open the folder in CLion or
VS Code with the PlatformIO plugin, or use the CLI:

```bash
pio run -e template_experiment              # build
pio run -e template_experiment -t upload    # build and flash the Teensy
```

Three environments ship as examples, one per experiment:

| Environment | What it is |
| --- | --- |
| `template_experiment` | A documented starting point: a rodent-VR pinout with no task logic |
| `slm_aatc` | Spatial-light-modulator stimulation with an AATC trigger |
| `ofl_shock` | Observational fear learning, with shock and pre-shock phases |

There is no `default_envs`, so a bare `pio run` builds all three.

## Where the pin map lives

`src/experiments/<env>/experiment_config.h` is the single place a pin is named. The
sampled pins and their order are declared in `src/main.cpp` as `pinsAnalogIn`,
`pinsDigitalIn` and `pinsDigitalOut`; the `#define`s in `experiment_config.h` say what
each one is wired to.

After changing either, regenerate the channel map that the recorder and the decoder use:

```bash
totalsync-pinsheet . -o pinSheet.json     # reads platformio.ini for the -I and -D
totalsync-pinout pinSheet.json -o pinout.png
```

## Adding an experiment

An experiment is a subclass of `Experiment` (`src/Experiment.h`) implementing five
methods, plus a `makeExperiment()` factory that returns one:

| Method | When it runs |
| --- | --- |
| `setup()` | Once, from `setup()`, at power-on |
| `loopMicro()` | Every microsecond — keep it light |
| `loopMilliPre()` | Every millisecond, before the serial and pin update |
| `loopMilliPost()` | Every millisecond, after it — most task logic goes here |
| `reset()` | On a reset command |

The quickest way:

```bash
totalsync-firmware init my-rig --experiment my_task
```

By hand: copy `src/experiments/template_experiment/` to
`src/experiments/my_task/`, rename the class, keep `experiment_config.h` named exactly
that, and add an environment to `platformio.ini`:

```ini
[env:my_task]
build_src_filter = +<*> -<experiments/> +<experiments/my_task/>
build_flags = ${env.build_flags} -I src/experiments/my_task
```

The `build_src_filter` is what keeps the other experiments out of the build — without it
every `makeExperiment()` in the tree is compiled and the link fails on a duplicate symbol.

## Notes

* `-D USB_TRIPLE_SERIAL` is required, not optional: `main.cpp` drives `packetSerialA` and
  `packetSerialB` on `SerialUSB1` / `SerialUSB2`, which only exist with the triple-serial
  USB type.
* Pins 40 and 41 (`LOOP_INDICATOR`, `GATHER_INDICATOR`) are reserved scope probes for
  timing the acquisition loop, and are not sampled into packets.
* `matlab/` holds host-side counterparts to the firmware, including the decoder for the
  one-wire `SLM_STIM_SELECT` protocol.

Full documentation: <https://totalsync.readthedocs.io>
