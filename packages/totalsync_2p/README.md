# totalsync-2p

Two-photon synchronization for [TotalSync](https://github.com/*/TotalSync): aligns
ScanImage `.tif` recordings with TotalSync behavioural telemetry, using the barcode
signal where present and the frame clock otherwise.

Installs the `totalsync-2p` command.
[Reference](../../Documentation/totalsync-2p.md)

```bash
totalsync-2p --tif-files rec.tif --b64-files rec.b64 \
             --output-dir out/ --pin-sheet pinSheet.json
```
