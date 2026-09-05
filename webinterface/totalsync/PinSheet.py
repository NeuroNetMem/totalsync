"""Read a pinSheet.json and turn it into display labels for the web interface.

The same file describes a recording for the .b64 reader utilities, so the channel
names used here are the ones in the sheet's "name" fields ("digital_input_0",
"analog_input_3", ...). Those happen to be exactly the names the web interface
generates for its traces, which is what makes the substitution a plain lookup.

A pin's "name" only says where it sits on the Teensy; "for" says what it is wired
to in a given rig, and that is what the labels should show. "for" is null for the
pins that are not used, and those keep their generated name.
"""
import json
import logging
from pathlib import Path


def load_pin_labels(path):
    """Map channel name -> display label for every channel the pin sheet describes.

    Channels the sheet has no meaning for are simply absent from the result, so
    the web interface falls back to its own name for them.

    Raises SystemExit if the file cannot be read or is not a pin sheet. The
    option is only ever given explicitly, so a mistyped path should be reported
    rather than quietly ignored; individual unusable entries are only warned
    about, since one bad line should not cost you the other labels.
    """
    path = Path(path)
    try:
        with path.open() as f:
            sheet = json.load(f)
    except OSError as e:
        logging.error(f'Cannot read pin sheet {path}: {e}')
        raise SystemExit(1)
    except json.JSONDecodeError as e:
        logging.error(f'Pin sheet {path} is not valid JSON: {e}')
        raise SystemExit(1)

    if not isinstance(sheet, dict):
        logging.error(f'Pin sheet {path} should hold a JSON object, found {type(sheet).__name__}.')
        raise SystemExit(1)

    labels = _pin_labels(sheet.get('pins'), path)
    labels.update(_state_labels(sheet.get('states'), path))

    logging.info(f'Pin sheet {path} ({sheet.get("title", "untitled")}): '
                 f'{len(labels)} channels labelled.')
    return labels


def _pin_labels(pins, path):
    """Labels from the "pins" section, e.g. name 'digital_input_2', for 'Wheel Encoder A'."""
    labels = {}
    if pins is None:
        logging.warning(f'Pin sheet {path} has no "pins" section.')
        return labels
    if not isinstance(pins, list):
        logging.warning(f'Pin sheet {path}: "pins" should be a list, found '
                        f'{type(pins).__name__}. Ignoring it.')
        return labels

    for entry in pins:
        if not isinstance(entry, dict):
            logging.warning(f'Pin sheet {path}: skipping non-object pin entry {entry!r}.')
            continue
        name, meaning = entry.get('name'), entry.get('for')
        if not isinstance(name, str) or not name:
            logging.warning(f'Pin sheet {path}: skipping pin entry without a "name": {entry!r}.')
            continue
        # An unused pin has "for": null, which is not an error: it just means the
        # interface should go on calling that channel by its own name.
        if isinstance(meaning, str) and meaning.strip():
            labels[name] = meaning.strip()
    return labels


def _state_labels(states, path):
    """Labels from the "states" section, e.g. idx 0, name 'uncorrected_distance'.

    States have no "for" field: unlike a pin, a state's name already *is* its
    meaning. The web interface numbers these channels states_0 ... states_7.
    """
    labels = {}
    if states is None:
        return labels
    if not isinstance(states, list):
        logging.warning(f'Pin sheet {path}: "states" should be a list, found '
                        f'{type(states).__name__}. Ignoring it.')
        return labels

    for entry in states:
        if not isinstance(entry, dict):
            logging.warning(f'Pin sheet {path}: skipping non-object state entry {entry!r}.')
            continue
        idx, name = entry.get('idx'), entry.get('name')
        # bool is an int subclass, and "idx": true is not an index.
        if not isinstance(idx, int) or isinstance(idx, bool) or not isinstance(name, str) or not name.strip():
            logging.warning(f'Pin sheet {path}: skipping unusable state entry {entry!r}.')
            continue
        labels[f'states_{idx}'] = name.strip()
    return labels
