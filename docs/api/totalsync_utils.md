# `totalsync_utils`

```{eval-rst}
.. automodule:: totalsync_utils
   :no-index:
```

## Decoding

```{eval-rst}
.. currentmodule:: totalsync_utils

.. autofunction:: decode_b64_files
.. autofunction:: decode_single_file
.. autofunction:: load_pin_mapping
.. autofunction:: apply_pin_mapping
```

## Pin sheets

The pin sheet is what turns the raw packet columns into named channels. These build one
from the firmware source; see {doc}`../totalsync-pinsheet` for the command-line front end
and for what the fields mean.

```{eval-rst}
.. currentmodule:: totalsync_utils

.. autofunction:: generate_pin_sheet
.. autofunction:: parse_firmware
.. autofunction:: build_pin_sheet
```

### Supporting types

```{eval-rst}
.. currentmodule:: totalsync_utils.pinsheet

.. autoclass:: FirmwarePinMap
   :members:

.. autoclass:: PinDefine
   :members:

.. autofunction:: prettify_macro_name
```
