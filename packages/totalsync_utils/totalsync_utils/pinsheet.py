"""Generate a pinSheet.json pin map from the Teensy firmware source.

The decoding utilities in :mod:`totalsync_utils.decoder` need a pinSheet.json to
turn the raw packet columns into named channels.  Everything that file records
already exists in the firmware: the ``pinsAnalogIn`` / ``pinsDigitalIn`` /
``pinsDigitalOut`` arrays say which pads are sampled and in which order, and the
``#define``\\ s just below them say what each pad is wired to.

This module runs a real C preprocessor over the sketch (pcpp), so only the
branches selected by the current compile-time configuration are considered - a
pin that is ``SLM_DEBUG_OUT`` in one configuration and ``PRESHOCK`` in another is
reported as whichever the build actually uses.  ``-D`` / ``-U`` let you generate
the sheet for a configuration other than the one hard-coded in the source.

A macro is taken to name a pin when all of the following hold:

* it is an object-like macro whose body evaluates to an integer;
* that integer appears in one of the sampled pin arrays (so ``LOOP_INDICATOR 40``,
  a scope probe that never reaches a packet, is left out);
* it is not a compile-time configuration flag, i.e. it is never tested by the
  preprocessor (this is what keeps ``#define SLM_DEBUG 1`` from being read as a
  name for digital input pin 1).
"""

from __future__ import annotations

import argparse
import ast
import datetime
import io
import json
import re
import sys
import warnings
from dataclasses import dataclass, field
from pathlib import Path

from pcpp import Action, OutputDirective, Preprocessor

__all__ = [
    'FirmwarePinMap',
    'PinDefine',
    'generate_pin_sheet',
    'parse_firmware',
    'build_pin_sheet',
    'prettify_macro_name',
    'main',
]

#: Words that stay upper case when a macro name is turned into a ``for`` label.
DEFAULT_ACRONYMS = frozenset({
    'AATC', 'ADC', 'AI', 'CRC', 'CS', 'DAC', 'GND', 'ID', 'IO', 'IR', 'LED',
    'PWM', 'RGB', 'SLM', 'TTL', 'USB',
})

#: Arrays of pin numbers recognised in the firmware, and the channel kind each
#: one describes.  ``pinsDigital`` is not a channel: it is the addressing order
#: used by the ``instUNITY`` instruction, and gives the ``unity`` index.
_CHANNEL_ARRAYS = (
    ('pinsDigitalIn', 'digital_input'),
    ('pinsDigitalOut', 'digital_output'),
    ('pinsAnalogIn', 'analog_input'),
)
_UNITY_ARRAY = 'pinsDigital'

_ARRAY_RE = re.compile(
    r'\b(?:const|constexpr|static|volatile)\s+'
    r'(?:(?:unsigned|signed)\s+)?'
    r'(?:int|long|short|char|byte|u?int(?:8|16|32)_t)\s+'
    r'(?P<name>\w+)\s*\[[^\]]*\]\s*=\s*\{(?P<body>[^}]*)\}\s*;',
    re.S,
)
_SCALAR_RE = r'\b(?:const|constexpr|static)\s+(?:(?:unsigned|signed)\s+)?\w+\s+{name}\s*=\s*([^;]+);'
_STATE_ASSIGN_RE = re.compile(
    r'\bpacket\s*\.\s*variables\s*\[\s*(\d+)\s*\]\s*=\s*([A-Za-z_]\w*)\s*;'
)
_IFDEF_RE = re.compile(r'^[ \t]*#[ \t]*(?:ifdef|ifndef)[ \t]+(\w+)', re.M)
_IF_RE = re.compile(r'^[ \t]*#[ \t]*(?:if|elif)\b([^\n]*(?:\\\n[^\n]*)*)', re.M)
_IDENT_RE = re.compile(r'\b[A-Za-z_]\w*\b')
_INT_SUFFIX_RE = re.compile(r'\b(0[xX][0-9a-fA-F]+|\d+)[uUlL]+\b')


# ---------------------------------------------------------------------------
# Preprocessing
# ---------------------------------------------------------------------------

class _FirmwarePreprocessor(Preprocessor):
    """Preprocessor tailored to reading an Arduino sketch out of its context.

    Two departures from a compiler front end:

    * The sketch includes library headers that are not part of this repository.
      Their contents are irrelevant here - the pin map lives in the sketch - so a
      missing header is passed over instead of aborting the run.
    * Macros given on the command line are *locked*: the sketch's own ``#define``
      and ``#undef`` of those names are dropped.  A compiler's ``-U`` only clears
      definitions the compiler itself made, which would be useless here, where
      the configuration switches (``SLM_DEBUG`` and friends) are written into the
      source.  Locking them is what lets the sheet be generated for a build
      configuration without editing the firmware.
    """

    def __init__(self, locked=()):
        super().__init__()
        self.line_directive = None
        self.missing_includes = []
        self.locked = set(locked)

    def on_include_not_found(self, is_malformed, is_system_include, curdir, includepath):
        self.missing_includes.append(includepath)
        raise OutputDirective(Action.IgnoreAndPassThrough)

    def on_directive_handle(self, directive, toks, ifpassthru, precedingtoks):
        if directive.value in ('define', 'undef') and toks and toks[0].value in self.locked:
            raise OutputDirective(Action.IgnoreAndRemove)
        return super().on_directive_handle(directive, toks, ifpassthru, precedingtoks)

    def on_error(self, file, line, msg):
        warnings.warn(f'{file}:{line}: {msg}')


def _preprocess(source_text, source_path, defines=(), undefines=(), include_dirs=()):
    """Run the preprocessor and return ``(expanded_text, macros, missing_includes)``."""
    defines = [macro.strip() for macro in defines]
    undefines = [macro.strip() for macro in undefines]
    locked = {macro.split('=', 1)[0].strip() for macro in defines} | set(undefines)
    pp = _FirmwarePreprocessor(locked=locked)
    for directory in include_dirs:
        pp.add_path(str(directory))
    for macro in defines:
        pp.define(macro.replace('=', ' ', 1) if '=' in macro else f'{macro} 1')
    for macro in undefines:
        pp.undef(macro)

    pp.parse(source_text, str(source_path))
    sink = io.StringIO()
    pp.write(sink)
    return sink.getvalue(), pp.macros, pp.missing_includes


# ---------------------------------------------------------------------------
# Small C expression evaluation
# ---------------------------------------------------------------------------

_BIN_OPS = {
    ast.Add: lambda a, b: a + b,
    ast.Sub: lambda a, b: a - b,
    ast.Mult: lambda a, b: a * b,
    ast.FloorDiv: lambda a, b: a // b,
    ast.Mod: lambda a, b: a % b,
    ast.LShift: lambda a, b: a << b,
    ast.RShift: lambda a, b: a >> b,
    ast.BitAnd: lambda a, b: a & b,
    ast.BitOr: lambda a, b: a | b,
    ast.BitXor: lambda a, b: a ^ b,
}


def _eval_int(text):
    """Evaluate a fully macro-expanded integer expression, or return ``None``.

    Only integer arithmetic is accepted; anything with a name in it (``Serial1``,
    ``LED_BUILTIN``, a function call) is not a pin number we can resolve.
    """
    text = _INT_SUFFIX_RE.sub(r'\1', text.strip())
    if not text:
        return None
    try:
        node = ast.parse(text, mode='eval').body
    except (SyntaxError, ValueError):
        return None
    return _eval_node(node)


def _eval_node(node):
    if isinstance(node, ast.Constant):
        return node.value if isinstance(node.value, int) and not isinstance(node.value, bool) else None
    if isinstance(node, ast.UnaryOp):
        operand = _eval_node(node.operand)
        if operand is None:
            return None
        if isinstance(node.op, ast.USub):
            return -operand
        if isinstance(node.op, ast.UAdd):
            return operand
        if isinstance(node.op, ast.Invert):
            return ~operand
        return None
    if isinstance(node, ast.BinOp):
        op = _BIN_OPS.get(type(node.op))
        left, right = _eval_node(node.left), _eval_node(node.right)
        if op is None or left is None or right is None:
            return None
        try:
            return op(left, right)
        except (ZeroDivisionError, ValueError):
            return None
    return None


# ---------------------------------------------------------------------------
# Firmware parsing
# ---------------------------------------------------------------------------

@dataclass
class PinDefine:
    """A ``#define`` that names a pin in the active configuration."""

    name: str
    pin: int
    source: str = ''
    lineno: int = 0


@dataclass
class FirmwarePinMap:
    """Everything read out of one firmware source file."""

    path: Path
    arrays: dict = field(default_factory=dict)
    defines: list = field(default_factory=list)
    config_flags: set = field(default_factory=set)
    inferred_states: dict = field(default_factory=dict)
    n_states: int = 0
    missing_includes: list = field(default_factory=list)

    def names_for(self, pin):
        """Macro names attached to ``pin``, in order of definition."""
        return [d.name for d in self.defines if d.pin == pin]


def _parse_arrays(text):
    arrays = {}
    for match in _ARRAY_RE.finditer(text):
        values = []
        for element in match.group('body').split(','):
            element = element.strip()
            if not element:
                continue
            value = _eval_int(element)
            if value is None:
                values = None
                break
            values.append(value)
        if values:
            arrays[match.group('name')] = values
    return arrays


def _parse_scalar(text, name):
    match = re.search(_SCALAR_RE.format(name=re.escape(name)), text)
    return _eval_int(match.group(1)) if match else None


def _config_flags(raw_text):
    """Macros the preprocessor branches on - configuration switches, not pins."""
    flags = set(_IFDEF_RE.findall(raw_text))
    for expression in _IF_RE.findall(raw_text):
        flags.update(_IDENT_RE.findall(expression))
    flags.discard('defined')
    return flags


def _infer_states(text, n_states):
    """Best-effort state names, from ``packet.variables[i] = <identifier>;``.

    The firmware has no declared names for the state vector, so this reads the
    variable each slot is filled from.  A later assignment to the same slot wins,
    matching what the compiled code does.
    """
    states = {}
    for index, identifier in _STATE_ASSIGN_RE.findall(text):
        index = int(index)
        if n_states and index >= n_states:
            continue
        states[index] = _snake_case(identifier)
    return states


def parse_firmware(source, defines=(), undefines=(), include_dirs=(), exclude=()):
    """Read the pin configuration out of a Teensy firmware source file.

    Parameters
    ----------
    source : str or Path
        Path to the ``.cpp`` / ``.ino`` firmware source.
    defines : sequence of str
        Extra macros to define, ``NAME`` or ``NAME=VALUE``, as if passed to the
        compiler with ``-D``.  Use these to generate the sheet for a build
        configuration other than the one selected in the source.
    undefines : sequence of str
        Macros to leave undefined, as if passed with ``-U``.
    include_dirs : sequence of str or Path
        Directories searched for ``#include``\\ d headers.  Headers that are not
        found are skipped rather than treated as an error.
    exclude : sequence of str
        Macro names never to treat as pin names.

    Returns
    -------
    FirmwarePinMap
    """
    source = Path(source)
    raw_text = source.read_text()
    text, macros, missing = _preprocess(raw_text, source, defines, undefines, include_dirs)

    arrays = _parse_arrays(text)
    known_pins = set()
    for array_name, _ in _CHANNEL_ARRAYS:
        known_pins.update(arrays.get(array_name, ()))

    # -D on the command line is a configuration choice by definition.
    flags = _config_flags(raw_text) | {d.split('=', 1)[0] for d in defines} | set(exclude)

    pin_defines = []
    for name, macro in macros.items():
        # Leading underscores mark identifiers reserved for the implementation:
        # __FILE__, pcpp's own __PCPP__, and anything a real toolchain predefines.
        if name.startswith('_') or name in flags or macro.arglist is not None:
            continue
        value = _eval_int(''.join(token.value for token in macro.value))
        if value is None or value not in known_pins:
            continue
        pin_defines.append(PinDefine(name=name, pin=value,
                                     source=getattr(macro, 'source', '') or '',
                                     lineno=getattr(macro, 'lineno', 0) or 0))
    pin_defines.sort(key=lambda d: (d.lineno, d.name))

    n_states = _parse_scalar(text, 'nStates') or 0

    return FirmwarePinMap(
        path=source,
        arrays=arrays,
        defines=pin_defines,
        config_flags=flags,
        inferred_states=_infer_states(text, n_states),
        n_states=n_states,
        missing_includes=missing,
    )


# ---------------------------------------------------------------------------
# Name formatting
# ---------------------------------------------------------------------------

def _split_words(name):
    words = []
    for chunk in name.split('_'):
        words.extend(re.findall(r'[A-Z]+(?![a-z])|[A-Z][a-z]+|[a-z]+|\d+', chunk))
    return words


def prettify_macro_name(name, acronyms=DEFAULT_ACRONYMS):
    """Turn a macro name into a human readable label.

    ``SCANNER_FRAME_CLOCK`` becomes ``Scanner Frame Clock`` and ``PIN_SYNC_LED``
    becomes ``Pin Sync LED``: underscores become spaces, words are decapitalised,
    and known acronyms and single letters are kept upper case.
    """
    acronyms = {a.upper() for a in acronyms}
    words = []
    for word in _split_words(name):
        if word.isdigit():
            words.append(word)
        elif word.upper() in acronyms or len(word) == 1:
            words.append(word.upper())
        else:
            words.append(word.capitalize())
    return ' '.join(words) or name


def _snake_case(name):
    return '_'.join(word.lower() for word in _split_words(name)) or name


# ---------------------------------------------------------------------------
# Sheet construction
# ---------------------------------------------------------------------------

def _kind_of(pin_name):
    for _, kind in _CHANNEL_ARRAYS:
        if pin_name.startswith(kind + '_'):
            return kind
    return None


def _load_existing(path):
    """``{(kind, teensy_pin): for}`` plus title and states of an existing sheet."""
    sheet = json.loads(Path(path).read_text())
    existing = {}
    for pin in sheet.get('pins', []):
        kind = _kind_of(pin.get('name', ''))
        if kind is not None and pin.get('for'):
            existing[(kind, pin.get('teensy_pin'))] = pin['for']
    return existing, sheet.get('title'), sheet.get('states', [])


def _unique(label, taken, what):
    """Keep channel labels distinct: the decoder keys a flat dict on them."""
    if label not in taken:
        taken.add(label)
        return label
    suffix = 2
    while f'{label} ({suffix})' in taken:
        suffix += 1
    unique = f'{label} ({suffix})'
    warnings.warn(f'duplicate {what} name {label!r}, renamed to {unique!r}')
    taken.add(unique)
    return unique


def build_pin_sheet(firmware, title=None, updated=None, acronyms=DEFAULT_ACRONYMS,
                    existing=None, prefer_existing=False, states=None,
                    infer_states=True):
    """Build the pinSheet dictionary from a parsed :class:`FirmwarePinMap`."""
    unity_order = {pin: index for index, pin in enumerate(firmware.arrays.get(_UNITY_ARRAY, ()))}
    existing = existing or {}
    taken = set()
    pins = []
    fallback_unity = 0

    for array_name, kind in _CHANNEL_ARRAYS:
        for index, pin in enumerate(firmware.arrays.get(array_name, ())):
            names = firmware.names_for(pin)
            if len(names) > 1:
                warnings.warn(
                    f'pin {pin} is named by several macros in this configuration: '
                    f'{", ".join(names)}'
                )
            label = ' / '.join(prettify_macro_name(n, acronyms) for n in names) or None
            inherited = existing.get((kind, pin))
            if inherited and (label is None or prefer_existing):
                label = inherited
            if label is not None:
                label = _unique(label, taken, 'channel')

            # Analog channels are not addressable from Unity.  For the digital
            # ones the index is the position in pinsDigital, which is what the
            # instUNITY instruction indexes; a pin missing from that array cannot
            # be addressed at all.  Without the array, fall back to input-then-
            # output order, which is how the firmware builds it.
            if kind == 'analog_input':
                unity = None
            elif unity_order:
                unity = unity_order.get(pin)
            else:
                unity = fallback_unity
                fallback_unity += 1

            pins.append({
                'name': f'{kind}_{index}',
                'unity': unity,
                'teensy_pin': pin,
                'used': label is not None,
                'for': label,
            })

    if states is None:
        states = [{'idx': idx, 'name': firmware.inferred_states[idx]}
                  for idx in sorted(firmware.inferred_states)] if infer_states else []
    for state in states:
        state['name'] = _unique(state['name'], taken, 'state')

    return {
        'title': title or f'Teensy-TotalSync pin information: {firmware.path.name}',
        'updated': updated or datetime.date.today().strftime('%Y%m%d'),
        'pins': pins,
        'states': states,
    }


def generate_pin_sheet(source, defines=(), undefines=(), include_dirs=(), exclude=(),
                       title=None, updated=None, acronyms=DEFAULT_ACRONYMS,
                       merge=None, prefer_existing=False, states=None, infer_states=True):
    """Parse ``source`` and return the corresponding pinSheet dictionary.

    Convenience wrapper around :func:`parse_firmware` and :func:`build_pin_sheet`;
    see those for the meaning of the arguments.  ``merge`` is the path to an
    existing pinSheet.json whose hand written ``for`` labels, title and states are
    carried over for anything the firmware does not describe.
    """
    firmware = parse_firmware(source, defines=defines, undefines=undefines,
                              include_dirs=include_dirs, exclude=exclude)
    existing, existing_title, existing_states = ({}, None, None)
    if merge:
        existing, existing_title, existing_states = _load_existing(merge)
    if states is None and existing_states:
        states = [dict(state) for state in existing_states]
    return build_pin_sheet(
        firmware,
        title=title or existing_title,
        updated=updated,
        acronyms=acronyms,
        existing=existing,
        prefer_existing=prefer_existing,
        states=states,
        infer_states=infer_states,
    )


# ---------------------------------------------------------------------------
# Command line interface
# ---------------------------------------------------------------------------

def _parse_state_option(value):
    if '=' not in value:
        raise argparse.ArgumentTypeError(f'expected IDX=NAME, got {value!r}')
    index, name = value.split('=', 1)
    try:
        index = int(index)
    except ValueError:
        raise argparse.ArgumentTypeError(f'state index must be an integer, got {index!r}') from None
    return {'idx': index, 'name': name}


def _describe(firmware, sheet, stream):
    print(f'{firmware.path}', file=stream)
    if firmware.missing_includes:
        print(f'  headers not found (skipped): {", ".join(firmware.missing_includes)}', file=stream)
    for array_name, _kind in _CHANNEL_ARRAYS:
        pins = firmware.arrays.get(array_name)
        if pins is None:
            print(f'  {array_name}: not found', file=stream)
        else:
            print(f'  {array_name}: {len(pins)} pins ({min(pins)}-{max(pins)})', file=stream)
    named = sum(1 for pin in sheet['pins'] if pin['used'])
    print(f'  {named}/{len(sheet["pins"])} channels named by a #define', file=stream)
    for pin in sheet['pins']:
        flag = '*' if pin['used'] else ' '
        print(f'  {flag} {pin["name"]:<18} teensy {pin["teensy_pin"]:<3} '
              f'unity {"-" if pin["unity"] is None else pin["unity"]:<4} {pin["for"] or ""}',
              file=stream)
    for state in sheet['states']:
        print(f'  * state[{state["idx"]}]        {state["name"]}', file=stream)


def main(argv=None):
    """Entry point for ``totalsync-pinsheet``."""
    parser = argparse.ArgumentParser(
        prog='totalsync-pinsheet',
        description='Generate a pinSheet.json pin map from the Teensy firmware source.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Print the pin sheet for the configuration selected in the source
  totalsync-pinsheet arduino/Teensy41_totalsync.ino

  # Write it out
  totalsync-pinsheet docs/main_example.cpp -o docs/pinSheet.json

  # Generate the sheet for a different build configuration
  totalsync-pinsheet docs/main_example.cpp -U SLM_DEBUG -D SLM_EXPERIMENT=1

  # Regenerate after a firmware change, keeping hand written labels where the
  # firmware has nothing better to offer
  totalsync-pinsheet docs/main_example.cpp --merge docs/pinSheet.json -o docs/pinSheet.json
        """,
    )
    parser.add_argument('source', help='Teensy firmware source (.cpp / .ino)')
    parser.add_argument('-o', '--output', default='-',
                        help='Output pinSheet.json ("-" for stdout, the default)')
    parser.add_argument('-D', '--define', action='append', default=[], metavar='NAME[=VALUE]',
                        help='Define a macro, overriding any #define of it in the '
                             'source (repeatable)')
    parser.add_argument('-U', '--undef', action='append', default=[], metavar='NAME',
                        help='Leave a macro undefined, ignoring any #define of it in '
                             'the source (repeatable)')
    parser.add_argument('-I', '--include', action='append', default=[], metavar='DIR',
                        help='Directory to search for headers (repeatable)')
    parser.add_argument('--exclude', action='append', default=[], metavar='MACRO',
                        help='Never read MACRO as a pin name (repeatable)')
    parser.add_argument('--acronym', action='append', default=[], metavar='WORD',
                        help='Extra word to keep upper case in labels (repeatable)')
    parser.add_argument('--title', help='Title field (default: derived from the source file name)')
    parser.add_argument('--updated', help='Updated field, YYYYMMDD (default: today)')
    parser.add_argument('--merge', metavar='PINSHEET',
                        help='Existing pinSheet.json to take title, states and otherwise '
                             'unknown "for" labels from')
    parser.add_argument('--prefer-existing', action='store_true',
                        help='With --merge, let the existing labels win over the macro names')
    parser.add_argument('--state', action='append', default=[], type=_parse_state_option,
                        metavar='IDX=NAME', help='Name of state vector slot IDX (repeatable). '
                                                 'Overrides the names guessed from the source.')
    parser.add_argument('--no-infer-states', action='store_true',
                        help='Do not guess state vector names from the source')
    parser.add_argument('-q', '--quiet', action='store_true',
                        help='Do not print the summary of what was found')
    args = parser.parse_args(argv)

    # Ambiguities in the source are reported as warnings; on a command line they
    # read better without the "file:line: UserWarning:" apparatus around them.
    warnings.showwarning = lambda message, *a, **k: print(f'warning: {message}', file=sys.stderr)

    source = Path(args.source)
    if not source.is_file():
        print(f'Error: source file not found: {source}', file=sys.stderr)
        return 1
    if args.merge and not Path(args.merge).is_file():
        print(f'Error: pin sheet to merge not found: {args.merge}', file=sys.stderr)
        return 1

    try:
        firmware = parse_firmware(source, defines=args.define, undefines=args.undef,
                                  include_dirs=args.include, exclude=args.exclude)
    except Exception as exc:
        print(f'Error parsing {source}: {exc}', file=sys.stderr)
        return 1

    if not any(name in firmware.arrays for name, _ in _CHANNEL_ARRAYS):
        print(f'Error: no pin arrays ({", ".join(n for n, _ in _CHANNEL_ARRAYS)}) '
              f'found in {source}', file=sys.stderr)
        return 1

    existing, existing_title, existing_states = ({}, None, None)
    if args.merge:
        try:
            existing, existing_title, existing_states = _load_existing(args.merge)
        except (OSError, ValueError) as exc:
            print(f'Error reading {args.merge}: {exc}', file=sys.stderr)
            return 1

    states = args.state or ([dict(s) for s in existing_states] if existing_states else None)

    sheet = build_pin_sheet(
        firmware,
        title=args.title or existing_title,
        updated=args.updated,
        acronyms=DEFAULT_ACRONYMS | {a.upper() for a in args.acronym},
        existing=existing,
        prefer_existing=args.prefer_existing,
        states=states,
        infer_states=not args.no_infer_states,
    )

    text = json.dumps(sheet, indent=2) + '\n'
    if args.output == '-':
        sys.stdout.write(text)
    else:
        Path(args.output).write_text(text)
        if not args.quiet:
            print(f'Wrote {args.output}', file=sys.stderr)

    if not args.quiet:
        _describe(firmware, sheet, sys.stderr)

    return 0


if __name__ == '__main__':
    sys.exit(main())
