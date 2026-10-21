"""Put the PlatformIO firmware tree into the totalsync-utils distributions.

The firmware lives at the root of the repository, at ``firmware/``, because that is where
CLion and ``pio run`` expect a project to be and because it arrived as a unit, by
``git subtree``.  It also has to travel inside the wheel, so that
``totalsync-firmware init`` can hand a complete project to somebody who installed from
PyPI and has no checkout.  Rather than keep two copies in the source tree, this hook
stages a filtered copy at ``totalsync_utils/data/firmware/`` before each build and hands
that path to the build target.

Two things make it a hook rather than a ``force-include`` in pyproject.toml:

* ``uv build`` builds the sdist first and then the wheel *from the unpacked sdist*, where
  ``../../firmware`` does not exist -- and hatchling raises ``Forced include not found``
  on a missing source (``recurse_forced_files`` in
  ``hatchling/builders/plugin/interface.py``).  A hook can look, and do nothing when there
  is nothing to do: exactly the sdist case, where the payload is already inside the
  archive.  hatchling force-includes ``hatch_build.py`` into the sdist itself
  (``get_default_build_data`` in ``hatchling/builders/sdist.py``), so the hook is there to
  be that no-op.
* ``build_data['force_include']`` rather than letting the ordinary file walk find the
  staged copy.  Forced files skip the include/exclude machinery, so an ignore pattern
  cannot drop the payload -- and hatchling honours a ``.gitignore`` found by walking *up*
  from the project root, matches it against paths relative to that root, and ships a copy
  of it in the sdist to be applied all over again when the wheel is built from it.
  Forcing keeps the checkout build and the sdist build on one code path.

The staged copy is also what makes an *editable* install work.  ``uv sync`` installs the
workspace members with PEP 660, which puts ``packages/totalsync_utils`` on ``sys.path``,
so ``resources.files('totalsync_utils')`` resolves to the real source directory -- the
payload has to physically be there for ``bundled_firmware_path()`` to find it.  Without it
development and installed use would exercise different code paths.
"""

from __future__ import annotations

import filecmp
import shutil
from pathlib import Path

from hatchling.builders.hooks.plugin.interface import BuildHookInterface

#: Where the payload goes, relative to the project root.  ``totalsync_utils.firmware``
#: resolves the same path with importlib.resources at run time.
PAYLOAD = Path('totalsync_utils') / 'data' / 'firmware'

#: What a scaffolded PlatformIO project consists of, as top level entries of ``firmware/``.
#: An allow list rather than a deny list, so that the next editor to leave a directory
#: there (``.idea/``, ``.claude/``, ``.ai/``, ``.vscode/``, ...) and the next ``pio run``
#: to leave a ``.pio/`` build tree stay out of the wheel without this list having to learn
#: their names -- while a new source directory under ``src/`` is picked up without editing
#: it either.
#:
#: ``.gitignore`` is in: it says ``.pio``, which is what a project the user is about to
#: build in wants.  The stock ``include/README``, ``lib/README`` and ``test/README`` are in
#: because they are what makes the copy a well formed PlatformIO project -- git cannot
#: store an empty directory, and PlatformIO expects those three to exist.
MANIFEST = (
    '.gitignore',
    'README.md',
    'platformio.ini',
    'include',
    'lib',
    'matlab',
    'src',
    'test',
)

#: Never copied, at any depth inside a MANIFEST entry.
JUNK = frozenset({'.pio', '.git', '.idea', '.claude', '.ai', '__pycache__', '.DS_Store'})


class FirmwareBuildHook(BuildHookInterface):
    PLUGIN_NAME = 'firmware'

    def initialize(self, version, build_data):
        root = Path(self.root)
        payload = root / PAYLOAD
        # parents[1] of packages/totalsync_utils is the repository root.  The
        # platformio.ini test is what stops an unrelated `firmware` directory two levels
        # above an unpacked sdist from being mistaken for the real one.
        external = root.parents[1] / 'firmware'

        if (external / 'platformio.ini').is_file():
            _stage(external, payload)
        elif not (payload / 'platformio.ini').is_file():
            message = (
                f'no firmware to package: neither {external} nor {payload} is a '
                f'PlatformIO project. Build totalsync-utils from a checkout of '
                f'https://github.com/NeuroNetMem/totalsync or from its sdist.'
            )
            raise FileNotFoundError(message)

        build_data['force_include'][str(payload)] = str(PAYLOAD)


def _wanted(source):
    """``{path relative to source: absolute path}`` for everything MANIFEST names."""
    wanted = {}
    for entry in MANIFEST:
        origin = source / entry
        if origin.is_file():
            wanted[Path(entry)] = origin
        elif origin.is_dir():
            for path in origin.rglob('*'):
                relative = path.relative_to(source)
                if path.is_file() and not JUNK.intersection(relative.parts):
                    wanted[relative] = path
    return wanted


def _stage(source, payload):
    """Make ``payload`` a copy of the wanted part of ``source``, touching as little as
    possible.

    Content is compared rather than copied unconditionally: the payload lives inside the
    source tree, so rewriting files that have not changed would move their mtimes on every
    build and give uv reason to think the package needs rebuilding again.  After the first
    build this is a no-op that reads 93 KB.
    """
    wanted = _wanted(source)

    # Anything MANIFEST no longer names, or that has gone from firmware/.  Deepest first,
    # so a directory is considered after the files in it have gone.
    if payload.is_dir():
        for path in sorted(payload.rglob('*'), reverse=True):
            if path.is_file():
                if path.relative_to(payload) not in wanted:
                    path.unlink()
            elif path.is_dir() and not any(path.iterdir()):
                path.rmdir()

    for relative, origin in wanted.items():
        target = payload / relative
        if target.is_file() and filecmp.cmp(origin, target, shallow=False):
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(origin, target)
