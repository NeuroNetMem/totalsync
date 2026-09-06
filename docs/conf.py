"""Sphinx configuration for the TotalSync documentation.

The narrative chapters are Markdown, read by MyST-Parser; the API reference is generated
by autodoc from the docstrings of the two library packages. See .readthedocs.yaml for how
the published site is built.
"""

from importlib.metadata import PackageNotFoundError, version as pkg_version

project = 'TotalSync'
author = 'Ronny Eichler, Francesco P. Battaglia, Morgane Audrain'
copyright = '2026, Donders Institute'

# Tracks the umbrella distribution rather than a second copy of the number that would
# drift away from pyproject.toml. Docs can be built without an install, so fall back.
try:
    release = pkg_version('totalsync')
except PackageNotFoundError:
    release = '0.0.0+unknown'
version = '.'.join(release.split('.')[:2])

extensions = [
    'myst_parser',
    'sphinx.ext.autodoc',
    # The docstrings are NumPy-style ("Parameters\n----------"), which autodoc cannot
    # read on its own.
    'sphinx.ext.napoleon',
    'sphinx.ext.intersphinx',
    'sphinx.ext.viewcode',
    'sphinx_copybutton',
    'sphinx_design',
]

# These chapters are placeholders for documentation that has not been written: three are
# empty files and demo.md says "under construction". They stay in the repository as a
# record of what is intended, but publishing empty pages is worse than not linking them,
# and excluding them here also keeps them out of the "not in any toctree" warnings.
exclude_patterns = [
    '_build',
    'Thumbs.db',
    '.DS_Store',
    '2p.md',
    'demo.md',
    'electrophysiology.md',
    'vr.md',
]

# -- MyST ------------------------------------------------------------------------------

# Not needed for the same-page anchors in installation.md -- Sphinx's own section ids
# already cover those. It is here so that a cross-document link of the form
# [text](other.md#some-heading) resolves, which MyST does not support otherwise.
myst_heading_anchors = 3

# No 'linkify': it needs linkify-it-py, and every URL in these pages is either an
# explicit [text](url) link, an <autolink>, or inside a code block where auto-linking
# would be wrong anyway.
myst_enable_extensions = [
    'colon_fence',
    'deflist',
    'attrs_inline',
]

# -- Cross-project references ----------------------------------------------------------

# Lets np.ndarray, nap.Tsd and friends in the docstrings resolve to the upstream docs
# instead of rendering as plain text.
intersphinx_mapping = {
    'python': ('https://docs.python.org/3', None),
    'numpy': ('https://numpy.org/doc/stable', None),
    'scipy': ('https://docs.scipy.org/doc/scipy', None),
    'pandas': ('https://pandas.pydata.org/docs', None),
    'pynapple': ('https://pynapple.org', None),
}

# -- linkcheck ------------------------------------------------------------------------

linkcheck_ignore = [
    # Where the interface is served while totalsync is running; nothing to connect to at
    # documentation build time.
    r'http://localhost:\d+',
]

# -- autodoc ---------------------------------------------------------------------------

autodoc_member_order = 'bysource'
autodoc_typehints = 'description'
napoleon_use_rtype = False

# -- HTML ------------------------------------------------------------------------------

html_theme = 'furo'
html_title = f'TotalSync {version}'
html_static_path = ['_static']
html_theme_options = {
    'source_repository': 'https://github.com/fpbattaglia/totalsync/',
    'source_branch': 'main',
    'source_directory': 'docs/',
}

# Bare "$ " and ">>> " prompts should not end up on the clipboard.
copybutton_prompt_text = r'\$ |>>> |\.\.\. '
copybutton_prompt_is_regexp = True
