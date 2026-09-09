"""Hand out the Teensy firmware that ships with this package.

The firmware is a PlatformIO project, and users are expected to *modify* it: which pins
are sampled and what they are wired to is the experiment.  That makes an installed
package a bad place to keep it -- ``site-packages``, and the hashed directory
``uv tool install`` uses, are not somewhere to develop and are replaced wholesale on
upgrade.

So the copy inside the wheel is a template rather than a working tree, and
``totalsync-firmware init DIR`` copies it *out* to a directory the user names, the way
``django-admin startproject`` and ``cargo new`` do.  What comes out is a complete
PlatformIO project -- ``platformio.ini``, the three example experiments, the
``Experiment`` base class -- that ``pio run`` can build without anything else being
fetched, and that the user owns and can commit.

The bundled copy is also what lets :mod:`totalsync_utils.pinsheet` produce a pin sheet
with no checkout at all: see its ``--bundled`` option.  One consequence worth knowing in
development: the payload is staged at *build* time by
``packages/totalsync_utils/hatch_build.py``, so after editing ``firmware/`` in the
repository the bundled copy is one ``uv sync --reinstall-package totalsync-utils``
behind.  For live work, name the project directory instead of using ``--bundled``.
"""

from __future__ import annotations

import argparse
import configparser
import os
import re
import shutil
import sys
from importlib import resources
from pathlib import Path

__all__ = [
    'bundled_firmware_path',
    'list_experiments',
    'experiment_build_settings',
    'scaffold_firmware',
    'main',
]

#: The experiment a pin sheet is generated for, and the one copied by ``--experiment``,
#: unless told otherwise.  It is the documented starting point: a rodent-VR pinout with
#: no task logic.
DEFAULT_EXPERIMENT = 'template_experiment'

#: A PlatformIO environment name that is also usable as a directory name and as part of a
#: C++ identifier.
_NAME_RE = re.compile(r'^[A-Za-z][A-Za-z0-9_]*$')

#: ``${section.option}`` as PlatformIO writes it in ``build_flags``.  Not configparser
#: interpolation, so it has to be expanded by hand.
_FLAG_REF_RE = re.compile(r'\$\{([A-Za-z_][\w:.]*)\.([A-Za-z_]\w*)\}')

_TEMPLATE_CLASS = 'TemplateExperiment'


# ---------------------------------------------------------------------------
# Locating the bundled project
# ---------------------------------------------------------------------------

def bundled_firmware_path():
    """Path of the PlatformIO project bundled with this package.

    Returns
    -------
    Path

    Raises
    ------
    FileNotFoundError
        If the payload is missing or is not a PlatformIO project, which means the
        distribution was built without it.
    """
    # resources.files() is called here rather than at import time so that the module stays
    # importable outside a package context, and os.fspath() because callers copy trees and
    # run PlatformIO against the result, both of which want a real filesystem path. For a
    # normally installed wheel the traversable *is* one; if the package were ever
    # zipimported this raises instead of quietly handing out a path that does not exist.
    root = Path(os.fspath(resources.files(__package__) / 'data' / 'firmware'))
    if not (root / 'platformio.ini').is_file():
        raise FileNotFoundError(
            f'no firmware bundled with this installation of totalsync-utils '
            f'(expected a PlatformIO project at {root}). Install a release from PyPI, or '
            f'clone https://github.com/NeuroNetMem/totalsync and use firmware/ directly.'
        )
    return root


def _resolve_root(root):
    return bundled_firmware_path() if root is None else Path(root)


# ---------------------------------------------------------------------------
# Reading platformio.ini
# ---------------------------------------------------------------------------

def _read_ini(root):
    # interpolation=None: PlatformIO's ${env.build_flags} is not configparser syntax and
    # BasicInterpolation would trip over a stray %. strict=False: a hand edited
    # platformio.ini with a repeated key should not stop us reading the pin configuration
    # out of it.
    parser = configparser.ConfigParser(interpolation=None, strict=False)
    path = root / 'platformio.ini'
    if not parser.read(path, encoding='utf-8'):
        raise FileNotFoundError(f'not a PlatformIO project: no platformio.ini in {root}')
    return parser


def list_experiments(root=None):
    """Names of the PlatformIO environments in a project, in file order.

    Parameters
    ----------
    root : str or Path, optional
        The project.  Defaults to the bundled one.

    Returns
    -------
    list of str
    """
    parser = _read_ini(_resolve_root(root))
    return [name[len('env:'):] for name in parser.sections() if name.startswith('env:')]


def _build_flags(parser, environment):
    """``build_flags`` of one environment, with ``${section.option}`` expanded once.

    One level and non-recursive is enough for this file, and ``fallback=''`` makes an
    unresolvable reference such as ``${sysenv.HOME}`` vanish rather than raise.
    """
    raw = parser.get(f'env:{environment}', 'build_flags', fallback='')
    expanded = _FLAG_REF_RE.sub(
        lambda m: parser.get(m.group(1), m.group(2), fallback=''), raw
    )
    return expanded.split()


def experiment_build_settings(experiment, root=None):
    """The ``-D`` and ``-I`` a pin sheet for one experiment needs.

    ``main.cpp`` includes ``experiment_config.h``, which only exists inside the
    experiment's own directory, so the include directory is what makes the pin
    ``#define``\\ s visible at all.  The defines matter too: in ``slm_aatc``,
    ``-D SLM_DEBUG=1`` is what names two of the output pins, so a sheet generated without
    it would be wrong for the firmware PlatformIO actually builds.

    Parameters
    ----------
    experiment : str
        PlatformIO environment name.
    root : str or Path, optional
        The project.  Defaults to the bundled one.

    Returns
    -------
    tuple
        ``(defines, include_dirs)`` -- ``NAME=VALUE`` strings as ``-D`` would take, and
        absolute directories.

    Raises
    ------
    ValueError
        If ``experiment`` is not an environment in this project.
    """
    root = _resolve_root(root)
    parser = _read_ini(root)
    known = list_experiments(root)
    if experiment not in known:
        raise ValueError(
            f'no experiment {experiment!r} in {root / "platformio.ini"} '
            f'(have: {", ".join(known) or "none"})'
        )

    defines, include_dirs = [], []
    flags = _build_flags(parser, experiment)
    index = 0
    while index < len(flags):
        flag = flags[index]
        # Both spellings: PlatformIO files use the spaced form throughout, but -DFOO is
        # just as valid and costs one branch to accept.
        for prefix, sink in (('-D', defines), ('-I', include_dirs)):
            if flag == prefix and index + 1 < len(flags):
                sink.append(flags[index + 1])
                index += 1
                break
            if flag.startswith(prefix) and len(flag) > len(prefix):
                sink.append(flag[len(prefix):])
                break
        index += 1

    # -I is written relative to the project root in platformio.ini.
    resolved = [str(root / directory) for directory in include_dirs]
    # An experiment that declares no -I still keeps its config header in the conventional
    # place, so fall back to that rather than generating an empty sheet.
    if not resolved:
        candidate = root / 'src' / 'experiments' / experiment
        if candidate.is_dir():
            resolved.append(str(candidate))
    return defines, resolved


# ---------------------------------------------------------------------------
# Scaffolding
# ---------------------------------------------------------------------------

def _class_name(experiment):
    return ''.join(part.capitalize() for part in experiment.split('_')) + 'Experiment'


def _env_block(experiment):
    return (
        f'\n\n; created by totalsync-firmware init --experiment {experiment}\n'
        f'[env:{experiment}]\n'
        f'build_src_filter = +<*> -<experiments/> +<experiments/{experiment}/>\n'
        f'build_flags = ${{env.build_flags}} -I src/experiments/{experiment}\n'
    )


def _add_experiment(project, experiment):
    """Copy the template experiment inside ``project`` under a new name."""
    source = project / 'src' / 'experiments' / DEFAULT_EXPERIMENT
    target = project / 'src' / 'experiments' / experiment
    if not source.is_dir():
        raise FileNotFoundError(f'no {DEFAULT_EXPERIMENT} to copy from in {project}')
    target.mkdir(parents=True, exist_ok=True)

    class_name = _class_name(experiment)
    for path in sorted(source.iterdir()):
        if not path.is_file():
            continue
        text = path.read_text()
        # Cosmetic: the class lives in an anonymous namespace, private to its file, so the
        # only symbol the link needs is makeExperiment(). Renaming it just stops three
        # experiments in one tree from all being called TemplateExperiment.
        text = re.sub(rf'\b{_TEMPLATE_CLASS}\b', class_name, text)
        name = f'{class_name}.cpp' if path.name == f'{_TEMPLATE_CLASS}.cpp' else path.name
        (target / name).write_text(text)

    # Appended as text rather than written back through configparser, which would rewrite
    # the whole file and lose every comment and the ${env.build_flags} references.
    ini = project / 'platformio.ini'
    ini.write_text(ini.read_text().rstrip('\n') + _env_block(experiment))
    return target


def scaffold_firmware(destination, experiment=None, force=False):
    """Copy the bundled PlatformIO project to ``destination``.

    Parameters
    ----------
    destination : str or Path
        Directory to create.  Must not already contain anything unless ``force``.
    experiment : str, optional
        Also copy the template experiment to an experiment of this name and add a
        matching PlatformIO environment.
    force : bool, optional
        Write into a directory that is not empty, overwriting files that collide.
        Nothing is ever deleted.

    Returns
    -------
    Path
        The created project directory.

    Raises
    ------
    FileExistsError
        If ``destination`` is a non-empty directory and ``force`` is false.
    ValueError
        If ``experiment`` is not a usable name, or already exists in the project.
    """
    source = bundled_firmware_path()
    destination = Path(destination)

    if experiment is not None and not _NAME_RE.match(experiment):
        raise ValueError(
            f'{experiment!r} is not a usable experiment name: it has to start with a '
            f'letter and contain only letters, digits and underscores'
        )
    if experiment is not None and not force and experiment in list_experiments(source):
        raise ValueError(
            f'{experiment!r} is already an experiment in the bundled firmware; pick '
            f'another name'
        )

    if destination.exists() and any(destination.iterdir()):
        if not force:
            raise FileExistsError(
                f'{destination} is not empty (use --force to write into it anyway)'
            )

    shutil.copytree(source, destination, dirs_exist_ok=True)
    if experiment is not None:
        _add_experiment(destination, experiment)
    return destination


# ---------------------------------------------------------------------------
# Command line interface
# ---------------------------------------------------------------------------

def _cmd_path(args):
    # stdout and nothing else, so that "$(totalsync-firmware path)" composes.
    print(bundled_firmware_path())
    return 0


def _cmd_list(args):
    root = bundled_firmware_path()
    experiments = list_experiments(root)
    for experiment in experiments:
        print(experiment)
    if not args.quiet:
        print(f'{len(experiments)} experiment(s) in {root}', file=sys.stderr)
        for experiment in experiments:
            defines, includes = experiment_build_settings(experiment, root)
            # relpath, not Path.relative_to: a hand edited platformio.ini may carry an
            # absolute or ../ include, and this line is only for reading.
            flags = ' '.join([f'-D {d}' for d in defines]
                             + [f'-I {os.path.relpath(i, root)}' for i in includes])
            print(f'  {experiment:<22} {flags}', file=sys.stderr)
    return 0


def _cmd_init(args):
    project = scaffold_firmware(args.directory, experiment=args.experiment,
                                force=args.force)
    if args.quiet:
        return 0
    experiments = list_experiments(project)
    build = args.experiment or DEFAULT_EXPERIMENT
    print(f'Created a PlatformIO project in {project}', file=sys.stderr)
    print(f'  experiments: {", ".join(experiments)}', file=sys.stderr)
    print('Next:', file=sys.stderr)
    print(f'  cd {project}', file=sys.stderr)
    print(f'  pio run -e {build}              # build', file=sys.stderr)
    print(f'  pio run -e {build} -t upload    # flash the Teensy', file=sys.stderr)
    print('  totalsync-pinsheet . -o pinSheet.json       # channel map for this project',
          file=sys.stderr)
    return 0


def main(argv=None):
    """Entry point for ``totalsync-firmware``."""
    parser = argparse.ArgumentParser(
        prog='totalsync-firmware',
        description='Hand out the Teensy firmware that ships with totalsync-utils.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Start a firmware project of your own, without cloning anything
  totalsync-firmware init my-rig
  cd my-rig && pio run -e template_experiment -t upload

  # ...with an experiment of your own, copied from the template
  totalsync-firmware init my-rig --experiment my_task

  # Generate a pin sheet from the bundled firmware, without a project
  totalsync-pinsheet "$(totalsync-firmware path)/src/main.cpp" \\
      -I "$(totalsync-firmware path)/src/experiments/template_experiment"

  # ...which totalsync-pinsheet can do on its own
  totalsync-pinsheet --bundled --experiment slm_aatc

  # What experiments are in the bundled firmware?
  totalsync-firmware list
        """,
    )
    subparsers = parser.add_subparsers(dest='command', required=True, metavar='COMMAND')

    init = subparsers.add_parser(
        'init', help='copy the bundled PlatformIO project to a directory of your own',
        description='Copy the bundled PlatformIO project to DIRECTORY. Nothing is ever '
                    'deleted, including with --force.')
    init.add_argument('directory', help='Directory to create the project in')
    init.add_argument('--experiment', metavar='NAME',
                      help='Also copy the template experiment to an experiment called '
                           'NAME and add a matching PlatformIO environment')
    init.add_argument('--force', action='store_true',
                      help='Write into a directory that is not empty, overwriting files '
                           'that collide. Never deletes anything.')
    init.add_argument('-q', '--quiet', action='store_true',
                      help='Do not print what was created')
    init.set_defaults(handler=_cmd_init)

    path = subparsers.add_parser(
        'path', help='print where the bundled firmware is',
        description='Print the path of the bundled PlatformIO project, and nothing else, '
                    'so that it can be substituted into another command.')
    path.set_defaults(handler=_cmd_path)

    listing = subparsers.add_parser(
        'list', help='list the experiments in the bundled firmware',
        description='Print the PlatformIO environment names in the bundled firmware, one '
                    'per line, with their build settings on stderr.')
    listing.add_argument('-q', '--quiet', action='store_true',
                         help='Print only the names')
    listing.set_defaults(handler=_cmd_list)

    args = parser.parse_args(argv)

    try:
        return args.handler(args)
    except (OSError, ValueError) as exc:
        print(f'Error: {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
