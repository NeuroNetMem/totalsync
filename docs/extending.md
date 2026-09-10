# Extending TotalSync

TotalSync adapts to a lot of different hardware. To add support for something new, you
change the firmware: which pins are sampled  is declared in
`firmware/src/main.cpp`, in the `pinsAnalogIn`, `pinsDigitalIn` and `pinsDigitalOut`
arrays (which you won't change), and what each pin is *wired to* is declared in the `experiment_config.h` of one
experiment under `firmware/src/experiments/`.

## customizing an experiment

1. create a firmware project stub with {doc}`totalsync-firware`. Open it in your favorite editor or IDE (with platformio plugin).

2. write your own pinout: add or modify PINs definitions by editing/adding preprocessor macro definitions (`#define`) in `your_experiment/experiment_config.h`

3. write your own experiment logic as a subclass of `Experiment` in `your_experiment/your_experiment.cpp`. The `Experiment` API includes five entry points to the Teensy workflow:
- `setup()` is run when the Teensy starts up and may be used for initialization, for example custom pin setups (eg. with a pull-up resistor). Initialization will depend on the state of the pins (and the attached devices) *at the time of Teensy startup* so care must be taken to avoid eg. switch-on order issues. In general, probably best to keep custom operations in here at a minimum
- `reset()` is run when the Teensy is reset, for example by pressing the reset button. It is useful for resetting the state of the experiment, for example to clear a buffer or reset a counter.
- `loopMicro()` is run in the Teensy main loop, which runs every microsecond. The loop (in `main.cpp`) is mostly used to control serial port communication and low-level synchronization tasks. Because of processing speed limitations, ti is best to keep custom operations in here at a minimum as well.
- `loopMilliPre()` and `loopMilliPost()` are run in a Teensy timer loop, which runs every millisecond (and calls the function `gather()` in `main.cpp`). Here, the core of the experiment is implemented. Use data members in the experiment class to maintain information on the state of the experiment, for example counters that keep track of the duration of timed events. State of pins can be changed here, and this will be recorded in the output data and visualized in the GUI. `loopMilliPre()` runs in `gather()` before serial port operations and `loopMilliPost()` after it, enabling the user to choose their preferred timing behavior. These are the two functions that you will find yourself mostly working on.


See {doc}`firmware` for the toolchain, the five methods an experiment implements, and how
to build and flash. {doc}`totalsync-firmware` covers the scaffolding command.

After a firmware change, **Regenerate the pin sheet.** The channel names the interface and the decoder use come
   from a `pinSheet.json`, and [`totalsync-pinsheet`](totalsync-pinsheet.md) derives one
   from the firmware source, so the names cannot drift away from the code that produces
   the data. Point it at the project directory rather than a single file, so the include
   path and the `-D` flags come out of `platformio.ini`:

   ```bash
   totalsync-pinsheet my-rig --experiment my_task -o pinSheet.json
   ```

   Use `--merge` to keep hand-written labels where the firmware has nothing better to
   offer, and [`totalsync-pinout`](totalsync-pinout.md) to draw the result onto a picture
   of the board — which is the quickest way to check a wiring job against what the
   firmware thinks is connected.
Keep the pinsheet together with the recorded data as it will be needed to decode them,


