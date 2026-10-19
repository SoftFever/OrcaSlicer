---
name: orca-cad-ai-assisted-design
description: Use when an agent designs or edits a parametric part in the Orca-CAD Design tab by driving its MCP socket, with every dimension bound to a variable (set_variable + set_feature_expr). Companion to orca-cad-modeling, which lists the verbs; this one is the parametric workflow and the traps found on the way.
---

# Designing a parametric part over MCP

The Design tab exposes its whole command surface on a unix socket (see `skills/orca-cad-modeling/SKILL.md` for the verbs; the app's own `describe_tools` is the authority on parameters). This skill is the working method.

## The rule
A part is parametric or it is not finished: every dimension is a variable (`set_variable`), every sketch dimension and every feature field is bound to it with `set_feature_expr` (the *fx* of the card fields). The feature tree then fills in as a real history and the part is changed from one place. No fixed numbers in geometry.

## Reaching the app
- The app opens the socket only when `ORCA_CAD_MCP` is set: `ORCA_CAD_MCP=1` gives `/tmp/orca-cad-mcp.sock`, any other value is taken as the path. `ORCA_CAD_MCP_TOKEN` makes every request carry a `token`; `ORCA_CAD_MCP_ALLOW` sets the roots `save`/`export`/`open` may touch (default `$HOME` and `/tmp`). The socket is mode 0600.
- Protocol: JSON-RPC 2.0, one request per line, e.g. `{"jsonrpc":"2.0","id":1,"method":"describe_scene","params":{}}`. `tools/orca_cad_mcp_bridge.py` serves the same tools to an MCP client.
- Do not restart the user's running app without being asked.

## Loop: understand, measure, build, validate
1. `describe_tools` (live schema), `describe_scene` (feature tree, `modeling_origin`, `generation`), `inspect_design` (features, bodies, variables, volume, `error_feature`).
2. Measure: `query_topology`, `measure`, `slice_body`, `mass_properties`.
3. Build with the verbs; every call returns the state after it (`ok`, `error`, created indexes).
4. Validate: `inspect_design` (`is_solid`, volume, bbox, `error_feature == -1`). Then a **sensitivity test**: change three or four variables with `set_parameter` (it reports how many features rebuilt), check the solid stays valid and the dimensions follow, put the values back.
5. Deliver: `save` (.3mf; the recipe travels inside), `export` (step | iges | stl | obj). Absolute paths under the allowed roots.

## Traps (each one verified against the app)
- **Indexes.** Sketches and features each take one index, in creation order. `sketch_commit` returns `sketch_index`; `extrude`, `pattern`, `fillet`... return `extrude_index`, `pattern_index`...
- **`extrude` without `sketch`** does NOT use the sketch you just committed: it builds a default 20 x 20 rectangle sketch of its own (and takes one more index). Always pass `sketch: <index>`.
- **Face and edge ids go stale** on every rebuild. Without `generation` an old id raises no error: it names a different edge. Pass the `generation` you read from `query_topology` / `describe_scene` on every call that takes an id; a changed model then answers `-32010 stale face/edge ids`. Choose faces and edges by geometry (normal, centroid, radius, length), not by number.
- **Coordinates** in a sketch are relative to `modeling_origin` (the plate centre; read it from `describe_scene`, it differs between sessions). XY, XZ and YZ pass through it. `sketch_begin {face, body}` puts the sketch on a planar face, but the reply says `plane: XY` whatever the face is.
- **Sketch constraints.** `sketch_add` infers coincident joints and H/V. `sketch_constrain` with `atomic:false` solves each constraint alone and lists the rejected ones; aim for `dof == 0`. `symmetric` is "points a and b about the LINE c" (there is no symmetry about a point); `symmetric_x|symmetric_y` use the axes. A constraint that adds nothing, such as symmetry of the two ends of a line that is already horizontal, is rejected as redundant with the message `1 inconsistent or redundant, conflicting: existing#N, new#0`. To centre on an axis use `vertical_points` / `horizontal_points` between `origin` and the centre (one degree of freedom less).
- **Binding.** After `sketch_commit`, `sketch_dimensions {sketch}` lists the dimensions; the field is `dim:<index>` and the indexes are not contiguous, so read the list. Then `set_feature_expr {feature: <sketch index>, field: "dim:2", expr: "base_L/2"}`.
- **Bindable feature fields** are an allow-list, listed in `docs/CAD/mcp_bindable_fields.md` (generated from the app by `scripts/CAD/gen_bindable_fields.py`) and in the `set_feature_expr` description of `describe_tools`. Mind the names: Fillet and Chamfer both use `dressup_size`; a pattern uses `pattern_spacing` and `pattern_count`; Extrude uses `distance` and `distance2`. The second direction of a grid pattern (`count2`, `spacing2`) cannot be bound yet. A field outside the list answers `unknown parameter: <field>` (translated in a localised app) and leaves the document unchanged.
- **Patterns.** `pattern {features:[idx], axis:[1,0,0], count, spacing, axis2, count2, spacing2}` replays features along world axes (`origin` is in model coordinates; at most 64 features, `count2` 1..500). `mirror:true` with `axis` and `origin` mirrors the features instead; it only applies together with `features`.
- **Extrude** `end`: `blind`, `symmetric`, `two_sided` (uses `distance2`), `through_all`, `up_to_face` (with `up_to_face: <face id>`); `boolean`: `new`, `union`, `subtract`, `intersect`.
- **`batch`** runs up to 10,000 calls and, with `rollback:true`, undoes the features and variables it started with if one fails. Calls inside an open sketch session are not rolled back: `sketch_commit` inside the batch.
- Check the volume before and after a cut: a cut that misses the body is refused ("removed no material").

## Hand-off
Say how many features and variables the part has, where the .3mf and .step are, and what was not checked on screen. Do not report as verified a look you did not see.
