# Plugin model import

`orca.host.plater().import_models(paths, *, plate="current", arrange=True)` imports
local model files through OrcaSlicer's native geometry loader. It is intended for
library, model-search and generator plugins following a user action. It appends
geometry to the existing project and keeps its active printer and process
presets. Native geometry import may add filament slots to preserve imported
colours. It does not open or replace a project.

```python
result = orca.host.plater().import_models(
    ["/absolute/path/bracket.stl", "/absolute/path/spacer.3mf"],
    plate="new",
    arrange=True,
)
if result.status == orca.host.ModelImportStatus.Success:
    print(result.object_ids, result.instance_ids)
else:
    print(result.status, result.errors, result.object_ids)
```

## Import behaviour

- `paths` is a non-empty list of file paths. Each file is imported separately in
  the supplied order; separate files are not combined into one multipart object.
  Unsupported or missing paths reject the batch before changing the scene.
- `plate` is `"current"` or `"new"`. A new plate uses the project's normal plate
  creation behaviour. Its active project settings are applied synchronously
  before loading models. An empty new plate is removed if nothing is imported.
- 3MF files supply model geometry, rather than changing project presets or
  replacing the scene. Native unit, geometry, multipart and texture decisions
  remain with Orca's loader. ZIP archive selection and G-code preview are not
  supported by this geometry-import API.
- Import and plate creation use the existing Undo mechanism. Cancellation stops
  the batch; objects already imported are retained and can be undone normally.
- Input files and referenced resources follow the native format loaders' normal
  behaviour. The API does not modify the source files.

## Result

`ModelImportResult` is a value snapshot with read-only attributes:

| Attribute | Meaning |
| --- | --- |
| `status` | `ModelImportStatus.Success`, `Partial`, `Cancelled` or `Failed` |
| `object_ids` | IDs of the model objects actually added to the scene |
| `object_ids_by_file` | One list of object IDs per supplied path, in input order; empty for files that added nothing or were not attempted |
| `instance_ids` | IDs of those objects' instances |
| `plate_index` | Zero-based import target, or `-1` when no target was selected or an empty new plate was removed |
| `arrange_started` | Whether a native arrangement job was queued |
| `errors` | Import failure or cancellation details |

`Partial` means that objects were added, but an import failed, the user cancelled,
or requested arrangement could not be queued. Callers must inspect the status and
errors even when IDs are present. IDs have the same meaning as `ModelObject.id()` and
`ModelInstance.id()`; they are not indices into the model's object list and are
not persistent library identifiers. `object_ids_by_file` provides source association
even when a 3MF stores different embedded input paths; repeated input paths still
have separate entries. No live model references are returned.

With `arrange=True`, the existing native arranger is queued for the captured
target plate after import. It may reposition other objects on that plate and
handle overflow using Orca's normal arrangement behaviour. `arrange_started`
reports queueing, not completion: cancellation or an arrangement failure does
not undo a successful import. `arrange=False` keeps the native loader's initial
placement. Imported layouts that are larger than the bed can remain out of bounds;
the API does not guarantee that every instance fits on a single plate.

## Threading and access

Call this method on the UI thread, such as from a Script's `execute()` or a Page
message callback. Calls from worker threads or slicing hooks are rejected. Busy
or reentrant imports are rejected before changing the scene.

The binding raises Python's normal read-access audit event for every supplied
path before creating a plate or importing anything. Permission denial leaves the
scene unchanged. This audits the supplied top-level files. Materials, textures
and buffers referenced by those files, external STEP documents and ModelIO
dependencies are read by the existing native loaders; those C++ reads do not
raise separate Python audit events. The GIL is released
during native loading and dialogs, then reacquired before converting the result
to Python. The operation is synchronous;
the import result is returned independently of queued arrangement.

[Native GUI regression procedure](../../tests/slic3rutils/model_import_gui.md).
