# Node code reference

These pages are generated from the comments in the Node's code. Nobody edits
them by hand: change the comment in the code, and the page follows at the next
docs build.

| Section | Generated from | Tool |
|---|---|---|
| C API | `/** */` comments in `node/capture/src/*.h` | Doxygen, rendered by the `mkdoxy` plugin |
| Python API | Google-style docstrings in `node/exporter/sr2es.py`, `node/capture/tools/web_ui/args.py` and `node/lib/node_config.py` | the `mkdocstrings` plugin |

Only the C headers are read, because they are the public interface of each
module. A `.c` file's private helpers do not appear here.

## Where to start

- [`burst.h`](c_api/burst_8h.md): the burst state machine behind monitor mode.
- [`record.h`](c_api/record_8h.md): recording to disk, continuous or one file
  per burst.
- [`capture.h`](c_api/capture_8h.md): the PIO capture itself, one-shot or
  streaming.
- [`sr2es`](python_api/sr2es.md): turning captures into Elasticsearch
  documents.
- [All C files](c_api/files.md), [all C structs](c_api/annotated.md),
  [all C functions](c_api/functions.md), [all C macros](c_api/macros.md).

## Build and preview

```bash
node/scripts/docs.sh build    # Doxygen warning check, then the whole site into site/
node/scripts/docs.sh serve --watch node   # live preview on http://127.0.0.1:8000
```

`--watch node` rebuilds the preview when a header or a Python file changes.

`docs.sh build` fails when Doxygen reports a warning, for example a public
declaration with no doc comment. How to write the comments is described in
the coding rules, under "Writing doc comments".
