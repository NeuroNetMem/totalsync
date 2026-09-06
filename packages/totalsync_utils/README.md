# totalsync-utils

Decoding utilities for [TotalSync](https://github.com/*/TotalSync) recordings.

* `totalsync-decode` — turn recorded `.b64` files into analysis-ready formats (numpy,
  MATLAB, [pynapple](https://pynapple.org)), with channels named from a `pinSheet.json`.
  [Reference](../../Documentation/totalsync-decode.md)
* `totalsync-pinsheet` — generate that `pinSheet.json` from the Teensy firmware source,
  so the channel names stay in step with the code that produced the data.
  [Reference](../../Documentation/totalsync-pinsheet.md)

Importable as a library too: `decode_b64_files`, `load_pin_mapping`,
`generate_pin_sheet`, `parse_firmware`.
