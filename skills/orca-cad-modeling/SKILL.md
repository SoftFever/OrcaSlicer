---
name: orca-cad-modeling
description: Use when building or editing a parametric solid in Orca-CAD (the Design tab) through its MCP socket - sketches, extrude/revolve/sweep/loft, holes, fillets, shell, booleans, constraints, export. Triggers - "model this in Orca CAD", "orca-cad mcp", "build a bracket/enclosure/part over MCP".
---

# Orca-CAD modeling over MCP

Tools come from the app's own `describe_tools` (the bridge `tools/orca_cad_mcp_bridge.py` builds its list only from it): call `describe_tools` once and trust it over this file. The app runs with `ORCA_CAD_MCP=<socket>`; the bridge takes the socket path as its argument.

## Workflow
1. `new_document`, then build. `inspect_design` after every few steps: bodies (volume, area, centre of mass, bbox, face/edge counts), variables, sketch DoF, undo depth.
2. Group dependent steps in one `batch {commands:[{method,params}...], rollback:true}`: it stops at the first error and undoes what it did. A failed single call leaves the document unchanged.
3. `undo` / `redo` are one step per call; `save {path.3mf}` / `open {path, discard}` / `export {path.step}` / `screenshot {path.png, view, fit}`.

## Sketch-first modeling (the reliable path)
`sketch_begin {plane: XY|XZ|YZ|datum:N}` -> `sketch_add {entities | rect, construction, infer}` -> `sketch_constrain {constraints:[{type,a,b,value,orientation}], atomic}` -> `sketch_commit` returns `sketch_index` -> `extrude/revolve {sketch: idx, ...}`, `sweep {profile, path}`, `loft {profiles:[...]}`.
- Entity refs in constraints: `"3"` (entity), `"3.start"|".end"|".center"` (point), `"origin"`.
- `sketch_add` infers exact relations (coincident joints, horizontal/vertical, origin). If you will state your own constraints on the same geometry pass `infer:false`: a redundant constraint is reported by the solver as a FAILED solve and is rejected.
- Aim for DoF 0 (`sketch_describe`). `sketch_constrain atomic:false` keeps the consistent ones and names the rejected (`rejected[]`, with the solver result: 1 inconsistent/redundant, 2 did not converge).
- Horizontal/vertical distances are signed (b - a); aligned distance is >= 0; angle is between line directions.

## Frames and units (the traps)
- mm, degrees. **Every plane passes through `modeling_origin`** (`describe_scene`; the bed centre, e.g. 128,128,0): sketch coordinates are relative to it, world = origin + local. Datum planes, holes, mirror do the same.
- App XZ plane: x = X, y = +Z, normal +Y. App YZ plane: x = Y, y = Z, normal +X. XY: x = X, y = Y, normal +Z. Extrude goes along the plane normal; `flip:true` reverses.
- A datum plane faces +axis; to drill down from a top face use `plane` with `tilt:180` (the hole tool drills along the plane normal).
- `hole {points:[[x,y],...]}` drills many in ONE undo step. Blind holes are flat-bottomed.
- A sketch with several DISJOINT regions cannot be extruded in one feature (kernel refuses): one sketch per region.

## Ids
Face/edge ids from `query_topology` are indices into the CURRENT topology and expire when anything rebuilds. Re-read before each id-taking call, or pass the `generation` back (the call is refused if stale). Curved edges carry `mid` and `samples` points for picking; a closed intersection curve arrives split at its seams.

## Refusals
`-32002` `error.data` = `{owner, transient, hint}`: `rebuild` is waited out (15 s); `feature_card`, `text_dialog`, `constrain`, `sketch` need you to hand the state back (`sketch_commit`/`sketch_cancel`, or the GUI). `-32003` = token required (`ORCA_CAD_MCP_TOKEN`). Paths must be absolute and under `$HOME`/`/tmp` (or `ORCA_CAD_MCP_ALLOW`).

## Known kernel limits (OCCT)
Fillet of a cylinder/cylinder intersection and two fillets whose radii sum to the gap between their edges are refused. Variable-radius/chord fillets, multi-face shell/draft, feature patterns, sheet metal are not available yet.
