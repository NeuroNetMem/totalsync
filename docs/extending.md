# Extending TotalSync

TotalSync adapts to a lot of different hardware. To add support for something new, you
change the firmware: which pins are sampled and in what order is declared in
`firmware/src/main.cpp`, in the `pinsAnalogIn`, `pinsDigitalIn` and `pinsDigitalOut`
arrays, and what each pin is *wired to* is declared in the `experiment_config.h` of one
experiment under `firmware/src/experiments/`.

Most rigs need only the second of those. An experiment is a pin map plus a subclass of
`Experiment` with the task logic in it, and adding one takes a single command:

```bash
totalsync-firmware init my-rig --experiment my_task
```

See {doc}`firmware` for the toolchain, the five methods an experiment implements, and how
to build and flash. {doc}`totalsync-firmware` covers the scaffolding command.

Two things follow from a firmware change:

1. **Regenerate the pin sheet.** The channel names the interface and the decoder use come
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

2. **Check the channel count.** The packet layout is fixed at 8 analog inputs, 16 digital
   inputs, 16 digital outputs and 8 state variables. The shipped firmware samples 7 of
   the 8 analog slots (pins 16–22), so there is one spare; the digital arrays are full.
   See [Pin usage](pin-usage.md) for the current assignment, and note that pins 40 and 41
   are reserved scope probes that never reach a packet.
