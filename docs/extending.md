# Extending TotalSync

TotalSync adapts to a lot of different hardware. To add support for something new, modify
the Arduino sketch in `Teensy41_Totalsync/` — which pins are sampled, and in what order,
is declared there in the `pinsAnalogIn`, `pinsDigitalIn` and `pinsDigitalOut` arrays.

Two things follow from a firmware change:

1. **Regenerate the pin sheet.** The channel names the interface and the decoder use come
   from a `pinSheet.json`, and [`totalsync-pinsheet`](totalsync-pinsheet.md) derives one
   from the firmware source, so the names cannot drift away from the code that produces
   the data. Use `--merge` to keep hand-written labels where the firmware has nothing
   better to offer.
2. **Check the channel count.** The packet layout is fixed at 8 analog inputs, 16 digital
   inputs, 16 digital outputs and 8 state variables; see
   [Pin usage](pin-usage.md) for the current assignment.
