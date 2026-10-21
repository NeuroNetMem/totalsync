# totalsync-pinout

Draw a labelled Teensy 4.1 pinout from a `pinSheet.json`, so a wiring job can be
checked against a picture of the board instead of a 39-entry JSON list.

```{image} images/pinout_example.png
:alt: A Teensy 4.1 pinout with every sampled pad labelled with its function and channel name
:width: 100%
```

## Synopsis

```
totalsync-pinout <sheet> [-o OUTPUT] [--format FORMAT] [--base IMAGE] [--dpi DPI]
                         [--font-size PT] [--no-widen]
                         [--geometry FILE] [--dump-geometry FILE]
                         [--describe] [-q]
```

## Arguments

| Argument | Description |
|---|---|
| `sheet` | `pinSheet.json` to draw. `-` reads one from stdin. |
| `-o`, `--output FILE` | Where to write the image. `.png`, `.pdf` and `.svg` are all understood; `-` writes to stdout. Default: `pinout.png`. |
| `--format FORMAT` | Output format, overriding the suffix of `--output`. Needed when writing to stdout as anything but PNG. |
| `--base IMAGE` | Base artwork to label (default: the card shipped with the package) |
| `--dpi DPI` | Resolution of a raster output (default: 200) |
| `--font-size PT` | Size of the `for` labels in points (default: 7.5). The channel name underneath is drawn at 70% of it. |
| `--no-widen` | Do not extend the canvas for labels too long to fit beside the board |
| `--geometry FILE` | Row positions to use instead of measuring them off the base image |
| `--dump-geometry FILE` | Write the measured row positions as JSON and exit |
| `--describe` | Print the row position and label of every header pin |
| `-q`, `--quiet` | Do not print the summary of what was drawn |

## What it reads

Each of the 42 header rows is looked up by `teensy_pin` in the sheet's `pins`, and
drawn one of three ways:

* a pin with a `for` label gets it, with the channel name underneath in smaller
  grey — `Wheel Enc Pina` over `digital_input_2`;
* a pin that is in the sheet but unassigned gets the channel name alone, in grey,
  so the diagram doubles as a map of what is still free;
* a pin with no sheet entry at all is left blank. Pins 23, 40 and 41 are not
  sampled by the firmware, so those three rows are usually empty.

Labels are coloured by kind — digital in blue, digital out orange, analog in
green, unassigned grey. The colours are from the Okabe–Ito set, so they stay
distinguishable to colour-blind readers and separate by lightness in greyscale
print.

The sheet's `title` and `updated` fields, and a legend of the four colours, go in
a strip below the board.

## The base artwork

The board is drawn from a Teensy 4.1 pinout card whose original coloured function
labels have been whited out. It ships inside the package, so an installed wheel
carries it; `docs/images/teensy_pinout.pdf` is the editable source it is rendered
from.

Where the labels go is measured off the image on every run rather than
hardcoded: the two grey number columns are found by colour, the bars within them
by their solid grey edge, and the row centres from the dark digit blobs inside
each bar. The result has to come to 13 + 9 + 11 + 9 = 42 rows on a uniform pitch,
or the command fails rather than drawing labels against the wrong pins. Point
`--base` at an unrelated image and you get

```
Error: expected 2 grey pin-number columns in the base image, found 0
```

`--dump-geometry` and `--geometry` are the escape hatch if you replace the
artwork with something the detection cannot read.

The photograph embedded in the card is only 685×632, so `--dpi` much above 200
enlarges the board without adding detail. The labels are drawn as vectors and
stay sharp at any resolution — and in a `.pdf` or `.svg` they stay selectable
text.

## Long labels

Every label is measured before anything is drawn. If the widest one will not fit
in the space beside the board, the canvas is extended outward on that side — the
board stays where it is — so a label is never silently truncated. `--no-widen`
turns that off, and `--font-size` is the other way to make a crowded sheet fit.

## Examples

```bash
# Label the board from a pin sheet
totalsync-pinout docs/pinSheet_example.json -o pinout.png

# Straight from the firmware, without keeping the sheet: totalsync-pinsheet
# writes to stdout by default, and "-" reads it back
totalsync-pinsheet firmware/src/main.cpp -I firmware/src/experiments/slm_aatc \
    | totalsync-pinout - -o pinout.png

# For printing, and for dropping into a figure
totalsync-pinout docs/pinSheet_example.json -o pinout.pdf
totalsync-pinout docs/pinSheet_example.json -o pinout.svg

# Cross-check the drawing against the sheet rather than by eye: this prints the
# detected row centre and the label placed at it for all 42 pins
totalsync-pinout docs/pinSheet_example.json -o pinout.png --describe
```

## Python API

```python
from totalsync_utils import render_pinout

geometry, labels = render_pinout("docs/pinSheet_example.json", "pinout.png")
```

`render_pinout()` takes a sheet dict — as {func}`~totalsync_utils.generate_pin_sheet`
returns — as readily as a path, so the two commands compose in Python too:

```python
from totalsync_utils import generate_pin_sheet, render_pinout

sheet = generate_pin_sheet("firmware/src/main.cpp",
                           include_dirs=["firmware/src/experiments/slm_aatc"])
render_pinout(sheet, "pinout.pdf")
```

It returns the geometry it used and the labels it placed, so a caller can check
what went where. {func}`~totalsync_utils.detect_geometry` measures the row
positions on their own if that is all you need.
