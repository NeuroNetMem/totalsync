# Teensy firmware

The Teensy 4.1 is what actually samples the rig: 8 analog and 32 digital channels plus
8 state variables, every millisecond, emitted as COBS-framed packets over USB serial.
The firmware is a [PlatformIO](https://platformio.org) project, and it is meant to be
modified — which pins are sampled and what they are wired to *is* the experiment.

## Getting the project

If you installed TotalSync from PyPI, the firmware came with it. Copy it out to a
directory of your own:

```bash
totalsync-firmware init my-rig
cd my-rig
```

That gives you a complete PlatformIO project you own and can commit — see
{doc}`totalsync-firmware`. If you work from a checkout of the repository instead, it is
already there, in `firmware/`.

Either way, nothing else needs fetching: PlatformIO downloads the toolchain and the three
libraries (`Encoder`, `FastCRC`, `PacketSerial`) on the first build.

## Building and flashing

Install PlatformIO — either the [CLI](https://docs.platformio.org/en/latest/core/installation/)
(`pip install platformio`) or the plugin for CLion or VS Code, which reads
`platformio.ini` directly.

```bash
pio run -e my_rig              # build (replace my_rig with the name of your experiment)
pio run -e my_rig -t upload    # build and flash the Teensy
```

`upload_protocol = teensy-cli` means flashing goes through the Teensy Loader, so the
board needs to be plugged in and, the first time, may need its program button pressed.

Three environments ship as examples, one per experiment:

| Environment | What it is |
| --- | --- |
| `template_experiment` | A documented starting point: a rodent-VR pinout with no task logic |
| `slm_aatc` | Spatial-light-modulator stimulation with an AATC trigger |
| `ofl_shock` | Observational fear learning, with shock and pre-shock phases |
| `widefield_generic` | A generic widefield rig, with no task logic |

There is no `default_envs`, so a bare `pio run` builds all three.

:::{note}
`-D USB_TRIPLE_SERIAL` in the shared `[env]` section is required, not a preference:
`main.cpp` drives `packetSerialA` and `packetSerialB` on `SerialUSB1` / `SerialUSB2`,
which only exist under the triple-serial USB type. Removing it will not compile.
:::

## What an experiment is

`src/main.cpp` owns the acquisition loop and should never need editing. Everything
experiment-specific sits behind two files in `src/experiments/<name>/`:

* **`experiment_config.h`** — the pin map. One `#define` per pin, saying what it is wired
  to. The name must stay exactly `experiment_config.h`, because `main.cpp` includes it by
  that name and the build selects *which* one with an include path.
* **a `.cpp`** — a subclass of `Experiment` (`src/Experiment.h`) plus a
  `makeExperiment()` factory that returns one.

`Experiment` is five virtual methods, all `void method()`, with state kept in
private members:

| Method | When it runs |
| --- | --- |
| `setup()` | Once, from `setup()`, at power-on. Hardware initialised here must already be powered. |
| `loopMicro()` | Every microsecond, from `loop()`. For timing-sensitive hardware only — keep it light. |
| `loopMilliPre()` | Every millisecond, before the serial and pin update |
| `loopMilliPost()` | Every millisecond, after it. Most task logic belongs here. |
| `reset()` | On a reset command |

The class goes in an anonymous namespace, private to its file; `makeExperiment()` is the
only symbol the rest of the firmware links against.

## Adding your own experiment

The quick way:

```bash
totalsync-firmware init my-rig --experiment my_task
```

That copies the template experiment to `src/experiments/my_task/`, renames the class, and
adds a matching `[env:my_task]` to `platformio.ini`. Then edit
`src/experiments/my_task/experiment_config.h` for your wiring and put the task logic in
`MyTaskExperiment`.

By hand, in an existing project, it is three steps:

1. Copy `src/experiments/template_experiment/` to `src/experiments/my_task/`.
2. Rename the class in the `.cpp` (cosmetic, but three classes called
   `TemplateExperiment` in one tree gets confusing). Keep `experiment_config.h` named
   exactly that.
3. Add an environment to `platformio.ini`:

   ```ini
   [env:my_task]
   build_src_filter = +<*> -<experiments/> +<experiments/my_task/>
   build_flags = ${env.build_flags} -I src/experiments/my_task
   ```

The `build_src_filter` is what keeps the other experiments out of the build. Without it
every `makeExperiment()` in the tree is compiled and the link fails on a duplicate
symbol; the `-I` is what makes your `experiment_config.h` the one `main.cpp` finds.

## The pin budget

The packet layout is fixed, and the arrays in `src/main.cpp` say which pads fill it:

| | Pins              | Slots in the packet |
| --- |-------------------| --- |
| `pinsAnalogIn` | 16–23  | 8 |
| `pinsDigitalIn` | 0–15              | 16 |
| `pinsDigitalOut` | 24–39             | 16 |
| state variables | —                 | 8 |

Pins 40 and 41 (`LOOP_INDICATOR`, `GATHER_INDICATOR`) are reserved: the firmware pulses
them so the acquisition loop can be timed on a scope, and they never reach a packet.
Pin 23 is wired to the header but not sampled by default.

`pinsDigital` is a separate array — the addressing order the `instUNITY` instruction
indexes, which is what gives each digital channel its `unity` number. A digital pin
missing from it cannot be addressed from Unity.

Pins are assigned a function in the experiment_config.h file in the experiment directory, by way of preprocessor macros. The pins defined in `template_experiment/experiment_config.h` are used for basic functionalities that are implemented in main.cpp. This includes common elements of virtual reality neuroscience experiments: for example

- a liquid reward electrovalve is controlled by pin `VALVE`. Its state can also be controlled externally, by Unity, from the Totalsync GUI or from the logic in the experiment class via the pin `REWARD`.
- A wheel rotary encoder is controlled via the pins `WHEEL_ENC_PINA` and `WHEEL_ENC_PINB`. As the encoder ticks to displacement conversion depends on the encoder model and the overall setup configuration, the conversion factor has to be provided in the experiment class (`experiment->EncoderConversion`).
- a standard implementation of lick detection for a IR sensor is provided, taking input from the sensor pin `LICK` and reporting the state via the pin `LICKDETECT`. This implementation may be overridden in the experiment class.
- connectivity with a Pi Camera is also implemented via `PIN_CAMERA_FSTROBE`, `PIN_SYNC_LED`
- Synchronization with e.g. Neuropixels hardware or a two-photon microscope takes place via a random barcode signal that is output on pin `EPHYS_SYNC`. Two-photon synchronization additionally uses the `SCANNER_FRAME_CLOCK` from the microscope. {doc}`totalsync-2p` provides a way to realign TIFF files on the Totalsync timeline in a robust way.


### Provided examples

- `slm-aatc` includes code for controlling a SLM optogenetic stimulator in scanimage, generating triggers timed on the imaging scanner flyback (to avoid light contamination in the imaging) and using a 1-wire serial protocol to communicate to scanimage which stimulation pattern must be emitted
- `ofl-shock` includes code for fear conditioning experiments
- `widefield-generic` includes code for controlling frame acquisition from two Basler cameras, enabling ratiometric imaging. In addition, it controls alternate activation of light sources for simultaneous fluorescence and reflectance imaging.

## After a firmware change

The channel names the recorder and the decoder use come from a `pinSheet.json`, and it is
generated from the firmware rather than maintained by hand, so the names cannot drift away
from the code that produces the data:

```bash
totalsync-pinsheet . -o pinSheet.json          # reads platformio.ini for the -I and -D
totalsync-pinout pinSheet.json -o pinout.png   # ...and draw it, to check the wiring
```

Naming the project directory rather than `src/main.cpp` is what lets
{doc}`totalsync-pinsheet` read the include path and the defines out of `platformio.ini`.
That matters: in `slm_aatc`, `-D SLM_DEBUG=1` is what names two of the output pins, so a
sheet generated without it would describe a different build from the one you flashed. Use
`--experiment` to pick the environment.

See {doc}`extending` for what else follows from a firmware change, and
{doc}`totalsync-pinsheet` for the details of how the labels are derived.
