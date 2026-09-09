"""Draw a labelled Teensy 4.1 pinout from a pinSheet.json.

``pinSheet.json`` says what every Teensy pad is wired to, and
:mod:`totalsync_utils.pinsheet` generates it from the firmware - but it is JSON.
To check a wiring job at the bench, or to hand someone a diagram of the rig, you
would otherwise read a 39-entry list and map pin numbers onto a board in your
head.  This module fills the labels in on a picture of the board instead.

The base artwork is a Teensy 4.1 pinout card whose original coloured function
labels have been whited out, leaving the two grey columns of pin numbers and
clear space either side of them.  ``data/teensy41_pinout.png`` is that card
rasterised from ``docs/images/teensy_pinout.pdf``, which stays in the repository
as the editable source.  To refresh the asset after editing the PDF::

    gs -q -dNOPAUSE -dBATCH -sDEVICE=png16m -r192 -dGraphicsAlphaBits=4 \\
       -sOutputFile=packages/totalsync_utils/totalsync_utils/data/teensy41_pinout.png \\
       docs/images/teensy_pinout.pdf

192 dpi gives 1370x1264.  The photograph embedded in the PDF is only 685x632, so
rendering the base much above ~200 dpi enlarges the board without adding detail;
the *labels* are drawn as vectors and stay sharp at any ``--dpi``.

Where the labels go is measured from the base image on every run rather than
hardcoded, so replacing the artwork fails loudly instead of quietly drawing
labels in the wrong places: the two grey number columns are found by colour, the
bars within them by their solid grey edge, and the row centres from the dark
digit blobs inside each bar.  The result has to come to 13 + 9 + 11 + 9 = 42
rows - the Teensy 4.1 header - on a uniform pitch, or :func:`detect_geometry`
raises.  ``--dump-geometry`` and ``--geometry`` are the escape hatch.

Which pin sits in which row is the only Teensy-4.1-specific knowledge here; see
:data:`PIN_BLOCKS`.  Pins 23, 40 and 41 are not sampled by the firmware and so
have no pin sheet entry: those three rows are always blank.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from importlib import resources
from pathlib import Path

import numpy as np
from matplotlib.backends.backend_agg import FigureCanvasAgg
from matplotlib.figure import Figure

__all__ = [
    'PinoutGeometry',
    'PIN_BLOCKS',
    'KIND_COLOURS',
    'detect_geometry',
    'render_pinout',
    'main',
]

#: The base artwork, shipped inside the package so that a wheel carries it.
BASE_IMAGE = resources.files(__package__) / 'data' / 'teensy41_pinout.png'

#: Size of the artwork's page, in points.  Everything below is in these units:
#: the base image is scaled to this rectangle, so a font size in points comes
#: out at the size the card was designed for whatever ``--dpi`` asks for.
PAGE_SIZE_PT = (513.75, 474.0)

#: Teensy pin in each row, in the top-to-bottom order the bars are detected in:
#: left column upper block, left lower, right upper, right lower.  The right
#: column is numbered upwards from the bottom of the board, hence the descending
#: ranges, and starts two rows lower than the left because ``Vin`` / ``GND`` /
#: ``3.3V`` precede pin 23 while only ``GND`` precedes pin 0.
PIN_BLOCKS = (
    ('left', tuple(range(0, 13))),        # 13 rows, pins 0-12
    ('left', tuple(range(24, 33))),       #  9 rows, pins 24-32
    ('right', tuple(range(23, 12, -1))),  # 11 rows, pins 23-13
    ('right', tuple(range(41, 32, -1))),  #  9 rows, pins 41-33
)

#: Okabe-Ito colours, so the diagram survives colour-blind readers and greyscale
#: printing, where the three kinds still separate by lightness.
KIND_COLOURS = {
    'digital_input': '#0072B2',
    'digital_output': '#D55E00',
    'analog_input': '#009E73',
    None: '#999999',
}

#: Human readable names for the kinds, for the legend.
_KIND_LABELS = {
    'digital_input': 'digital in',
    'digital_output': 'digital out',
    'analog_input': 'analog in',
    None: 'unassigned',
}

# Free white space either side of the grey number columns, measured off the
# artwork and verified pixel-clean.  The right edge is 10 pt clear of the bar
# rather than 7.5 pt because the retained label between the two right-hand
# blocks reaches to x ~353.3 pt.
LEFT_LABEL_EDGE_PT = 171.0
RIGHT_LABEL_EDGE_PT = 353.5

#: Strip added below the board for the title, the date and the legend.  The bars
#: stop 30 pt above the page bottom, which is not enough room for both.
FOOTER_PT = 44.0

_DEFAULT_FONT_SIZE = 7.5
#: The channel name under the ``for`` label, as a fraction of the label size.
_SECONDARY_RATIO = 0.7


# ---------------------------------------------------------------------------
# Geometry
# ---------------------------------------------------------------------------

@dataclass
class PinoutGeometry:
    """Where the pin rows are on the base image, in points.

    Attributes
    ----------
    size : tuple of float
        ``(width, height)`` of the page.
    bands : tuple of tuple of float
        ``(x0, x1)`` of the left and right grey number columns.
    rows : dict
        ``teensy_pin -> (side, y)``, ``side`` being ``'left'`` or ``'right'``
        and ``y`` the vertical centre of the row.
    """

    size: tuple
    bands: tuple
    rows: dict

    def to_json(self):
        """A JSON-serialisable form of this geometry."""
        return {
            'size': list(self.size),
            'bands': [list(band) for band in self.bands],
            'rows': [{'teensy_pin': pin, 'side': side, 'y': y}
                     for pin, (side, y) in sorted(self.rows.items())],
        }

    @classmethod
    def from_json(cls, data):
        """Rebuild a geometry from :meth:`to_json` output."""
        return cls(
            size=tuple(data['size']),
            bands=tuple(tuple(band) for band in data['bands']),
            rows={row['teensy_pin']: (row['side'], row['y']) for row in data['rows']},
        )


def _runs(flags, min_length=1):
    """Start/stop index pairs of the runs of true values in ``flags``."""
    flags = np.asarray(flags)
    edges = np.diff(np.concatenate(([0], flags.astype(np.int8), [0])))
    starts = np.flatnonzero(edges == 1)
    stops = np.flatnonzero(edges == -1)
    return [(a, b) for a, b in zip(starts, stops) if b - a >= min_length]


def detect_geometry(image):
    """Measure the pin row positions off the base image.

    Parameters
    ----------
    image : ndarray
        The base artwork as ``(H, W, 3)`` or ``(H, W, 4)`` floats in 0-1, as
        :func:`matplotlib.image.imread` returns.

    Returns
    -------
    PinoutGeometry

    Raises
    ------
    ValueError
        If the image does not look like the expected artwork - the wrong number
        of grey columns, of bars, or of digit rows, or a pitch that is not
        uniform.  This is deliberate: a wrong guess here would draw every label
        against the wrong pin, which is far worse than not drawing at all.
    """
    image = np.asarray(image, dtype=float)
    if image.ndim != 3 or image.shape[2] < 3:
        raise ValueError(f'expected an (H, W, 3) image, got shape {image.shape}')
    image = image[..., :3]
    if image.max() > 1.0:
        image = image / 255.0
    height, width = image.shape[:2]

    page_width, page_height = PAGE_SIZE_PT
    px_per_pt = width / page_width

    red, green, blue = image[..., 0], image[..., 1], image[..., 2]
    # The number columns are a flat mid-light neutral grey; the board itself is
    # green, the silkscreen white, the digits near-black.
    grey = ((np.abs(red - green) < 10 / 255) & (np.abs(green - blue) < 10 / 255)
            & (np.abs(red - blue) < 10 / 255) & (red > 190 / 255) & (red < 240 / 255))
    dark = image.max(axis=2) < 120 / 255

    # Two columns tall enough and wide enough to be the number strips.  The
    # narrow runs of board grey in between are rejected by the width test.
    bands = _runs(grey.sum(axis=0) > 0.05 * height, min_length=40)
    if len(bands) != 2:
        raise ValueError(
            f'expected 2 grey pin-number columns in the base image, found {len(bands)}'
            f'{" at x " + str([(round(a / px_per_pt, 1), round(b / px_per_pt, 1)) for a, b in bands]) if bands else ""}'
        )

    blocks = []
    for index, (x0, x1) in enumerate(bands):
        # An 8 px strip at the column's outer edge is clear of the digits, so a
        # bar reads as solid grey there and the space between bars does not.
        strip = (x0, x0 + 8) if index == 0 else (x1 - 8, x1)
        solid = grey[:, strip[0]:strip[1]].mean(axis=1) >= 0.75
        bars = _runs(solid, min_length=20)
        for y0, y1 in bars:
            rows = _runs(dark[y0:y1, x0:x1].any(axis=1), min_length=6)
            centres = [(y0 + a + y0 + b) / 2 for a, b in rows]
            blocks.append(('left' if index == 0 else 'right', centres))

    expected = [(side, len(pins)) for side, pins in PIN_BLOCKS]
    found = [(side, len(centres)) for side, centres in blocks]
    if found != expected:
        raise ValueError(
            f'the base image does not look like the Teensy 4.1 pinout card: '
            f'expected pin-number blocks {expected}, found {found}'
        )

    rows = {}
    for (side, pins), (_, centres) in zip(PIN_BLOCKS, blocks):
        # A least-squares straight line through the blob centres is the row grid;
        # comparing the centres against it is the uniformity check.
        index = np.arange(len(centres))
        pitch, offset = np.polyfit(index, centres, 1)
        drift = np.abs(np.asarray(centres) - (pitch * index + offset))
        if drift.max() > 0.2 * abs(pitch):
            raise ValueError(
                f'the {side} pin-number rows are not on a uniform pitch: '
                f'{drift.max():.1f} px off a {pitch:.1f} px grid'
            )
        for pin, centre in zip(pins, centres):
            rows[pin] = (side, centre / px_per_pt)

    for edge, (x0, x1) in zip((LEFT_LABEL_EDGE_PT, RIGHT_LABEL_EDGE_PT), bands):
        x0, x1 = x0 / px_per_pt, x1 / px_per_pt
        if x0 <= edge <= x1:
            raise ValueError(
                f'label edge x={edge} pt falls inside a pin-number column '
                f'(x {x0:.1f}-{x1:.1f} pt)'
            )

    return PinoutGeometry(
        size=(page_width, height / px_per_pt),
        bands=tuple((x0 / px_per_pt, x1 / px_per_pt) for x0, x1 in bands),
        rows=rows,
    )


# ---------------------------------------------------------------------------
# Pin sheet reading
# ---------------------------------------------------------------------------

def _kind_of(channel_name):
    for kind in ('digital_input', 'digital_output', 'analog_input'):
        if channel_name.startswith(kind + '_'):
            return kind
    return None


def _load_sheet(sheet):
    """Accept a sheet dict, a path, or ``'-'`` for stdin, and return the dict."""
    if isinstance(sheet, dict):
        return sheet
    if str(sheet) == '-':
        return json.loads(sys.stdin.read())
    return json.loads(Path(sheet).read_text())


def _labels_for(sheet, geometry):
    """``teensy_pin -> (primary, secondary, colour)`` for the rows to draw.

    A pin with a ``for`` label gets it as the primary text and the channel name
    underneath; a pin that is in the sheet but unassigned gets the channel name
    alone, in grey, so the drawing doubles as a map of what is still free.  A pin
    with no sheet entry at all is left out.
    """
    labels = {}
    for pin in sheet.get('pins', []):
        number = pin.get('teensy_pin')
        if number not in geometry.rows:
            continue
        name = pin.get('name') or ''
        label = pin.get('for')
        if label:
            labels[number] = (label, name, KIND_COLOURS[_kind_of(name)])
        else:
            labels[number] = (None, name, KIND_COLOURS[None])
    return labels


# ---------------------------------------------------------------------------
# Drawing
# ---------------------------------------------------------------------------

def _text_widths(strings, size):
    """Width in points of each string at ``size`` points, as laid out."""
    # A figure at 72 dpi makes the renderer's pixels points, so the measured
    # extent needs no conversion.  Measuring is what lets a long label widen the
    # canvas instead of running off the edge of it.
    figure = Figure(figsize=(1, 1), dpi=72)
    FigureCanvasAgg(figure)
    renderer = figure.canvas.get_renderer()
    widths = {}
    for string in strings:
        if not string:
            widths[string] = 0.0
            continue
        artist = figure.text(0, 0, string, fontsize=size)
        widths[string] = artist.get_window_extent(renderer).width
        artist.remove()
    return widths


def _load_base(base):
    from matplotlib import image as mpimg

    if base is None:
        with BASE_IMAGE.open('rb') as stream:
            return mpimg.imread(stream, format='png')
    base = Path(base)
    with base.open('rb') as stream:
        return mpimg.imread(stream, format=base.suffix.lstrip('.').lower() or 'png')


def render_pinout(sheet, output, base=None, dpi=200, font_size=_DEFAULT_FONT_SIZE,
                  geometry=None, widen=True, format=None):
    """Draw a labelled Teensy 4.1 pinout and write it to ``output``.

    Parameters
    ----------
    sheet : dict or str or Path
        A pin sheet as returned by
        :func:`totalsync_utils.generate_pin_sheet`, or the path to a
        ``pinSheet.json``; ``'-'`` reads one from stdin.
    output : str or Path or file-like
        Where to write the image.  The format is taken from the suffix, so
        ``.png``, ``.pdf`` and ``.svg`` all work; ``'-'`` writes to stdout, as
        PNG unless ``format`` says otherwise.
    base : str or Path, optional
        Base artwork to label.  Defaults to the card shipped with the package.
    dpi : int, optional
        Resolution of a raster output.  The labels are vectors, so this only
        limits the sharpness of the photograph underneath.
    font_size : float, optional
        Size of the ``for`` labels in points; the channel name underneath is
        drawn at 70% of it.
    geometry : PinoutGeometry, optional
        Row positions to use instead of measuring them off the image.
    widen : bool, optional
        Extend the canvas sideways when a label is too long for the space beside
        the board, rather than letting it overrun.  On by default.
    format : str, optional
        Output format, overriding the suffix of ``output``.

    Returns
    -------
    tuple
        ``(geometry, labels)``, the geometry used and the
        ``teensy_pin -> (primary, secondary, colour)`` mapping drawn, so a caller
        can report or check what was placed where.
    """
    sheet = _load_sheet(sheet)
    image = _load_base(base)
    if geometry is None:
        geometry = detect_geometry(image)

    labels = _labels_for(sheet, geometry)
    page_width, page_height = geometry.size
    secondary_size = font_size * _SECONDARY_RATIO

    # How far the widest label in each column reaches past the space it has.
    primary_widths = _text_widths([row[0] for row in labels.values() if row[0]], font_size)
    secondary_widths = _text_widths([row[1] for row in labels.values() if row[1]], secondary_size)

    def width_of(pin):
        primary, secondary, _ = labels[pin]
        return max(primary_widths.get(primary, 0.0), secondary_widths.get(secondary, 0.0))

    available = {'left': LEFT_LABEL_EDGE_PT, 'right': page_width - RIGHT_LABEL_EDGE_PT}
    overrun = {'left': 0.0, 'right': 0.0}
    for pin, (side, _) in geometry.rows.items():
        if pin in labels:
            overrun[side] = max(overrun[side], width_of(pin) - available[side])
    pad = {side: max(0.0, value) + 2.0 if value > 0 and widen else 0.0
           for side, value in overrun.items()}

    # One axes filling the figure, with the limits in points and the figure
    # exactly that many points across, so a data unit is a point in both
    # directions by construction and no aspect handling is needed.
    total_width = pad['left'] + page_width + pad['right']
    total_height = page_height + FOOTER_PT
    figure = Figure(figsize=(total_width / 72, total_height / 72), dpi=dpi)
    FigureCanvasAgg(figure)
    axes = figure.add_axes((0, 0, 1, 1))
    axes.set_axis_off()
    axes.set_facecolor('white')
    figure.patch.set_facecolor('white')

    # y grows downward, so image rows and row centres are the same coordinate.
    axes.imshow(image, extent=(0.0, page_width, page_height, 0.0),
                aspect='auto', interpolation='antialiased', zorder=0)
    axes.set_xlim(-pad['left'], page_width + pad['right'])
    axes.set_ylim(total_height, 0.0)

    # Baseline offsets from the row centre for the two stacked lines: cap
    # heights and the gap between them, centred on the row.
    cap, cap_secondary = 0.70 * font_size, 0.70 * secondary_size
    stack = cap + 0.20 * font_size + cap_secondary
    primary_offset = cap - stack / 2
    secondary_offset = stack / 2

    (left_band, right_band) = geometry.bands
    for pin, (side, y) in sorted(geometry.rows.items()):
        if pin not in labels:
            continue
        primary, secondary, colour = labels[pin]
        if side == 'left':
            x, align = LEFT_LABEL_EDGE_PT, 'right'
            leader = (LEFT_LABEL_EDGE_PT + 1.0, left_band[0] - 0.5)
        else:
            x, align = RIGHT_LABEL_EDGE_PT, 'left'
            leader = (right_band[1] + 0.5, RIGHT_LABEL_EDGE_PT - 1.0)

        # For a stacked pair the leader sits in the gap between the two lines:
        # at the row centre it would coincide with the primary's baseline and
        # read as an underscore after a label that fills its column.
        leader_y = y + (primary_offset + 0.10 * font_size if primary else 0.0)
        axes.plot(leader, (leader_y, leader_y), color=colour, linewidth=0.6,
                  alpha=0.55, solid_capstyle='butt', zorder=2)

        if primary:
            # Both lines are placed by their baselines, not their bounding
            # boxes, so a label with a descender ("Wheel Enc Pina") lines up
            # with one without ("Blick") instead of riding above it.  The pair
            # is centred on the row by cap height, and the leader line passes
            # through the gap between the two.
            axes.text(x, y + primary_offset, primary, fontsize=font_size,
                      color=colour, ha=align, va='baseline', zorder=3)
            axes.text(x, y + secondary_offset, secondary, fontsize=secondary_size,
                      color=KIND_COLOURS[None], ha=align, va='baseline', zorder=3)
        else:
            axes.text(x, y, secondary, fontsize=secondary_size,
                      color=colour, ha=align, va='center_baseline', zorder=3)

    _draw_footer(axes, sheet, page_width, page_height, pad, font_size)

    target = output
    if output == '-':
        target = sys.stdout.buffer
        format = format or 'png'
    elif hasattr(output, 'write'):
        format = format or 'png'
    figure.savefig(target, dpi=dpi, format=format, facecolor='white')
    return geometry, labels


def _draw_footer(axes, sheet, page_width, page_height, pad, font_size):
    """Title, date and colour legend in the strip below the board."""
    baseline = page_height + FOOTER_PT * 0.38
    title = sheet.get('title') or 'Teensy-TotalSync pin information'
    updated = sheet.get('updated')
    axes.text(-pad['left'] + 6.0, baseline, title, fontsize=font_size + 1.5,
              color='#222222', ha='left', va='center_baseline')
    if updated:
        axes.text(page_width + pad['right'] - 6.0, baseline, f'updated {updated}',
                  fontsize=font_size, color='#666666', ha='right', va='center_baseline')

    y = page_height + FOOTER_PT * 0.72
    x = -pad['left'] + 6.0
    legend_size = font_size * _SECONDARY_RATIO
    widths = _text_widths(list(_KIND_LABELS.values()), legend_size)
    for kind, label in _KIND_LABELS.items():
        axes.plot((x, x + 9.0), (y, y), color=KIND_COLOURS[kind], linewidth=2.0,
                  solid_capstyle='butt')
        axes.text(x + 12.0, y, label, fontsize=legend_size,
                  color='#444444', ha='left', va='center_baseline')
        x += 12.0 + widths[label] + 14.0


# ---------------------------------------------------------------------------
# Command line interface
# ---------------------------------------------------------------------------

def _describe(geometry, labels, stream):
    print(f'{len(labels)}/{len(geometry.rows)} header rows labelled '
          f'({len(geometry.rows) - len(labels)} pins not sampled by the firmware)',
          file=stream)
    for pin, (side, y) in sorted(geometry.rows.items()):
        primary, secondary, _ = labels.get(pin, (None, '', ''))
        flag = '*' if primary else ' '
        print(f'  {flag} teensy {pin:<3} {side:<5} y {y:7.2f}  '
              f'{secondary:<18} {primary or ""}', file=stream)


def main(argv=None):
    """Entry point for ``totalsync-pinout``."""
    parser = argparse.ArgumentParser(
        prog='totalsync-pinout',
        description='Draw a labelled Teensy 4.1 pinout from a pinSheet.json.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Label the board from a pin sheet
  totalsync-pinout docs/pinSheet_example.json -o pinout.png

  # Straight from the firmware, without keeping the sheet
  totalsync-pinsheet firmware/src/main.cpp -I firmware/src/experiments/slm_aatc \\
      | totalsync-pinout - -o pinout.png

  # For printing, and for dropping into a figure
  totalsync-pinout docs/pinSheet_example.json -o pinout.pdf
  totalsync-pinout docs/pinSheet_example.json -o pinout.svg

  # Check what was drawn against the sheet
  totalsync-pinout docs/pinSheet_example.json -o pinout.png --describe
        """,
    )
    parser.add_argument('sheet', help='pinSheet.json to draw ("-" for stdin)')
    parser.add_argument('-o', '--output', default='pinout.png',
                        help='Output image; .png, .pdf and .svg are all understood '
                             '("-" writes to stdout). Default: pinout.png')
    parser.add_argument('--format', help='Output format, overriding the suffix of --output')
    parser.add_argument('--base', metavar='IMAGE',
                        help='Base artwork to label (default: the card shipped with '
                             'this package)')
    parser.add_argument('--dpi', type=int, default=200,
                        help='Resolution of a raster output (default: 200). The labels '
                             'are vectors; this only affects the photograph under them.')
    parser.add_argument('--font-size', type=float, default=_DEFAULT_FONT_SIZE,
                        help=f'Label size in points (default: {_DEFAULT_FONT_SIZE})')
    parser.add_argument('--no-widen', action='store_true',
                        help='Do not extend the canvas for labels too long to fit '
                             'beside the board')
    parser.add_argument('--geometry', metavar='FILE',
                        help='Row positions to use instead of measuring them off the '
                             'base image')
    parser.add_argument('--dump-geometry', metavar='FILE',
                        help='Write the measured row positions as JSON and exit')
    parser.add_argument('--describe', action='store_true',
                        help='Print the row position and label of every header pin')
    parser.add_argument('-q', '--quiet', action='store_true',
                        help='Do not print the summary of what was drawn')
    args = parser.parse_args(argv)

    if args.dump_geometry:
        try:
            geometry = detect_geometry(_load_base(args.base))
        except (OSError, ValueError) as exc:
            print(f'Error reading the base image: {exc}', file=sys.stderr)
            return 1
        text = json.dumps(geometry.to_json(), indent=2) + '\n'
        if args.dump_geometry == '-':
            sys.stdout.write(text)
        else:
            Path(args.dump_geometry).write_text(text)
            print(f'Wrote {args.dump_geometry}', file=sys.stderr)
        return 0

    if args.sheet != '-' and not Path(args.sheet).is_file():
        print(f'Error: pin sheet not found: {args.sheet}', file=sys.stderr)
        return 1
    if args.base and not Path(args.base).is_file():
        print(f'Error: base image not found: {args.base}', file=sys.stderr)
        return 1

    geometry = None
    if args.geometry:
        try:
            geometry = PinoutGeometry.from_json(json.loads(Path(args.geometry).read_text()))
        except (OSError, ValueError, KeyError) as exc:
            print(f'Error reading {args.geometry}: {exc}', file=sys.stderr)
            return 1

    try:
        sheet = _load_sheet(args.sheet)
    except (OSError, ValueError) as exc:
        print(f'Error reading {args.sheet}: {exc}', file=sys.stderr)
        return 1
    if not sheet.get('pins'):
        print(f'Error: no pins in {args.sheet}', file=sys.stderr)
        return 1

    try:
        geometry, labels = render_pinout(
            sheet, args.output, base=args.base, dpi=args.dpi,
            font_size=args.font_size, geometry=geometry,
            widen=not args.no_widen, format=args.format,
        )
    except (OSError, ValueError) as exc:
        print(f'Error: {exc}', file=sys.stderr)
        return 1

    if args.output != '-' and not args.quiet:
        print(f'Wrote {args.output}', file=sys.stderr)
    if args.describe:
        _describe(geometry, labels, sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
