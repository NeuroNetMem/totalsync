# totalsync

Record a session: read packets from a Teensy 4.1 running the TotalSync firmware, write
them to disk, and serve a live view of every channel to the browser.

## Synopsis

```
totalsync [-s PORT] [-o DIR] [--no-browser] [-D] [-B] [-C]
          [-H HTTP_PORT] [-w WS_PORT] [--pinsheet PATH] [-v]
```

TotalSync is run from a single window. From a source checkout, prefix with `uv run`.

* **Session** — the serial port the Teensy is on (a drop-down of the ports found; *Refresh*
  looks again, and a port can also be typed in) and the directory to record into
  (*Browse…*; the working directory by default).
* **Play** starts the session and opens the browser interface once the servers are up.
  If the port cannot be opened you are told so and can pick another one.
* **Reset** resets the Teensy and reopens the browser interface; **Reload** only reopens
  it; **Quit** ends the session (so does closing the window, or Ctrl-C in the terminal).
* **Data logger** — the address of the browser interface, with *Copy* to put it on the
  clipboard, for opening it in another browser or on another machine.
* The **status bar** shows the port, the packets received and decoded, the packet rate and
  the output directory, once a second. It turns red when nothing has arrived since the
  last update; that warning also goes to the terminal log.

Given both `-s` and `-o`, the session starts straight away without waiting for Play.

## Options

| Option | Meaning |
| --- | --- |
| `-s`, `--serial_port` | Serial port of the Teensy, filled into the window. With `-o` as well, the session starts without waiting for Play. |
| `-o`, `--output-dir` | Directory to write the recording into, filled into the window; created if it does not exist. |
| `--no-browser` | Do not open the browser at startup. For scripted or headless runs. |
| `-D`, `--dummy` | Use a simulated Teensy — lets you try TotalSync with no hardware. |
| `-B`, `--binfile` | Also write a decoded binary dump alongside the base64 one. |
| `-C`, `--curses` | Show the text-mode status interface in the terminal. |
| `-H`, `--http_port` | Port for the browser interface (default 8000). |
| `-w`, `--ws_port` | Port for the WebSocket server (default 5678). The browser client currently hardcodes 5678, so changing this stops the page receiving data. |
| `--pinsheet PATH` | A `pinSheet.json` saying what each pin is wired to. See [Channel labels](#channel-labels). |
| `-v`, `--verbose` | More logging detail. Logging is already at info level; `-v` switches it to debug. |

Run `totalsync --help` for the authoritative list.

## Serial ports

Port names differ per platform. The window offers a drop-down of the ports it detects,
and the same list is written to the terminal, so you do not normally need to look them
up. For reference:

* **Windows** — `COM3`, `COM9`, …
* **macOS** — `/dev/cu.usbmodem14201`, …
* **Linux** — `/dev/ttyACM0`, `/dev/ttyUSB0`, …

On Linux you may need to be in the `dialout` group to open the port:

```bash
sudo usermod -a -G dialout $USER    # then log out and back in
```

To try things out with no Teensy attached:

```bash
totalsync -D
```

## Channel labels

Without a pin sheet the interface calls every channel by its position —
`digital_input_6`, `analog_input_1`, `states_0`. If a pinsheet.json file is passedeach channel with a `"for"`
entry is labelled with what it is actually wired to; the rest keep their generated names.

```bash
totalsync --pinsheet docs/pinSheet_2026.json
```

`docs/pinSheet_2026.json` is an exmaple. To make your own, generate it
from the firmware with [`totalsync-pinsheet`](totalsync-pinsheet.md) and then edit the
`"for"` fields — the generator can only name what the firmware names, so the result
almost always wants a pass by hand.

The browser console reports how many labels arrived:

```
Channel labels from pin sheet: 29
```

which is the quickest way to tell a forgotten `--pinsheet` (reports `0`) from a sheet
whose channel names do not match the ones the interface generates.

:::{tip}
The interface is served with cache-busting URLs, so a changed `interface.js` or
`style.css` is picked up on the next load. If you are editing the browser code and see a
stale page anyway, one hard reload clears whatever was cached before this behaviour
existed.
:::

## Output

`totalsync` writes `<chosen directory>/<timestamp>.b64`, and with `-B` a decoded `.bin`
alongside it. Decode the `.b64` with [`totalsync-decode`](totalsync-decode.md).

## Related

* [Installation](installation.md) — getting the command in the first place
* [Pin usage](pin-usage.md) — what the Teensy pins are wired to
* [`totalsync-decode`](totalsync-decode.md) — reading the recordings back
