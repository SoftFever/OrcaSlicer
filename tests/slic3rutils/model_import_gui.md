# Native plugin model import regression

Run in the built desktop app through an enabled plugin's Script `execute()` or
Page message callback. Use disposable model copies and the normal plugin file
permission prompts. The host unit tests do not construct a native Plater.

## Setup

- Start with an empty editable project. Select a printer with a 250 mm printable
  height and enough bed area for the fixtures. Record the printer, process,
  filament selections, layer height and source-file hashes.
- Prepare four visibly distinct STL files: `current-a.stl`, `current-b.stl`,
  `new-tall.stl` and `new-short.stl`. Make the first pair and the short model small
  enough to fit together on the bed. The tall model must be 113–150 mm high, so
  it fits the selected printer but exceeds the default Print height of 100 mm.

## Current and new plates

1. Call `import_models([current_a, current_b], plate="current", arrange=True)`
   with absolute paths. Check `Success`, no errors, the original plate index,
   and `arrange_started=True`. Match the returned object and instance IDs to
   the live scene; each `object_ids_by_file` entry must identify its input.
2. Wait for native arrangement to finish. Check both models are inside the
   requested plate and visibly separated. Record their IDs and instance
   offsets, rotations, scaling factors and mirrors.
3. Call `import_models([new_tall, new_short], plate="new", arrange=True)`.
   Check `Success`, no errors, a newly selected plate index, live returned IDs
   and the per-file ID mapping. The original pair must remain present.
4. Wait for native arrangement to finish. Both new instances must be inside
   the new plate, with neither listed under Outside. Arrangement must use the
   selected printer's 250 mm height and clearance settings, rather than fresh
   Print defaults. The original pair must retain its IDs and recorded
   transforms on the original plate.
5. Check that the model identity, selected presets, layer height and all source
   hashes are unchanged. `arrange_started` only confirms queueing; inspect the
   completed native job and scene before accepting these checks.
6. Exercise native Undo through the arrangement and import history. Undo the
   new batch until its objects and created plate are removed; the original pair
   must remain with its recorded transforms. Continue through the current
   batch's arrangement/import entries to restore the initial empty scene. Do
   not assume a fixed number of Undo actions for an asynchronous arrange job.

## Additional failure checks

- Deny a requested input-file permission. The batch must not create a plate or
  add geometry. Missing or unsupported inputs must likewise leave the scene
  unchanged.
- Cancel the native STEP options dialog, first with STEP alone and then after
  a valid STL in the same batch. Cancellation must stop later files. The result
  must report `Cancelled` when nothing was added, or `Partial` with the IDs of
  any retained geometry. Retained imports must be undoable normally.
- Invoke import while native slicing or an incompatible job is active. Expect
  a busy result without plate creation, geometry or settings changes.
- In a disposable project, use a debugger or test hook to reorder or delete an
  arranged import's target plate after queueing but before preparation. Repeat
  after processing but before finalisation. The captured target must be
  rejected, without moving objects on a replacement plate or applying stale
  arrangement results.
  Check the cancellation message and diagnostic reason, then dismiss any error
  dialog and run Arrange normally. No ongoing-arrangement notification or busy
  flag may remain, and the normal controls must still work.
- Cancel an arrangement through its normal progress control, then arrange
  again. Check that cancellation leaves object transforms unchanged and clears
  the ongoing-arrangement notification and busy state.
