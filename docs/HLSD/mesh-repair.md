# Mesh repair — High Level Design

## Purpose and scope

Mesh repair turns a broken triangle mesh into closed, outward oriented shells that bound a solid, so that the
mesh slices predictably and the other mesh tools (booleans, cut, painting) get valid input. It runs on demand:
the *Fix Model* command of the context menu of an object or part and of the warning icon in the object list, the
prompt the Cut tool shows when a cut leaves open edges, and the *Repair and import* choice of texture import. It is built on CGAL and is the only repair
the application has, on every platform.

`repair_mesh()` in `src/libslic3r/MeshRepair.cpp` does the work on one `TriangleMesh`. The GUI side,
`fix_model_with_cgal_gui()` in `src/slic3r/Utils/FixModelByCgal.cpp`, runs it on a worker thread for each volume
of an object, or for one volume, behind a progress dialog that can cancel it between stages.

## What the repaired mesh is

The repair keeps what the slicer would print from the input. The slicer fills a layer where the winding number of
the input surface is not zero (`MeshSlicingParams::SlicingMode::Regular` uses Clipper's nonzero fill rule), so:

- Shells crossing each other print as their union, and the repair unites them.
- A shell inside a solid that is oriented like it adds nothing, and the repair removes it.
- A shell inside a solid that is oriented inward is a cavity, and stays one.
- A whole mesh turned inside out prints as a solid, and the repair turns it outward.

On top of that, it closes holes and cracks, gives inconsistently oriented triangles the orientation of their
neighbours, drops degenerate, duplicate and stray flat geometry, and removes self-intersections that fold a shell
through itself. Where none of this applies, it reports the mesh as unmodified and the caller keeps the original
mesh, its painting and its object id.

The result keeps the input's part structure: a volume is repaired in place and is never split into several parts.
Splitting by connectivity would turn a cavity into a separate solid part and fill it.

## Pipeline

The input is treated as a triangle soup. Every stage after the first only runs on what the previous ones could
not settle, so a valid mesh passes through quickly.

### Valid parts first

Parts of the input that are connected by vertex indices and are already closed, consistently oriented 2-manifolds
skip the soup stages and go straight to the shell stages. Merging their points with coincident points of other
parts would only make non-manifold junctions out of solids that touch, for example parts of an assembly exported
together or voxels written as separate cubes.

### Soup clean-up

The rest of the soup has its coincident points merged. Triangles with repeated corners are dropped. Coincident
triangles of opposite orientation are a wall between two solids touching face to face, so they cancel in pairs;
of the remaining copies of a triangle one is kept. A connected region that would vanish entirely through
cancelling is a two-sided export instead (every face written in both orientations), and keeps one copy of each
triangle.

Open edges then get their vertices welded within a tolerance of 1e-4 of the bounding box diagonal, which closes the
cracks some exporters leave between triangles that should share corners. A vertex is never welded further than a
quarter of its shortest edge, so welding cannot collapse fine detail. A soup that stays mostly open after welding has
cracks wider than that tolerance, and is welded again with ten and then a hundred times the tolerance.

### Building the mesh

`orient_polygon_soup()` makes the soup orientable, duplicating vertices where it is not manifold, and the soup
becomes a `Surface_mesh`. CGAL picks the orientation of each connected part arbitrarily; the repair reverses
the parts whose triangles were mostly flipped by that, so each part keeps the orientation the majority of its input
triangles had, weighted by area. That keeps inward oriented cavities inward.

### Closing

Compatible borders are stitched, parts that can be stitched once reversed are merged, and borders still open are
snapped onto each other within the weld tolerance, which closes T-junctions. Exactly degenerate faces are removed.

Holes are filled last. Planar border loops that face each other in one plane, one inside the other, bound a single
region, such as a missing annulus at the end of a tube or a cap with features standing on it. Such a group is
triangulated at once with a 2D constrained Delaunay triangulation that fills only the faces at odd nesting depth,
so the openings inside the region stay open. The patch is used only if it closes every loop with the orientation
of the triangles around it. Every other loop is filled on its own with `triangulate_hole()`, which uses a 2D
constrained Delaunay triangulation when the loop is planar; its exhaustive cubic fallback is skipped for loops of
more than 200 edges, and a fan around the loop's centroid closes a loop nothing else could.

### Shells

The closed mesh is split into its shells, together with the valid parts set aside at the start. Shells enclosing
no volume, which hole filling makes out of stray flat patches, are dropped.

Self-intersections inside a shell, such as folds and slivers, are removed locally with
`experimental::remove_self_intersections()`, which remeshes the region around them. It only ever sees the faces of
one shell, so crossings between shells are left to the union. A shell it cannot clean completely goes to the union
on its own.

Shells crossing others are found through their bounding boxes and an AABB tree per shell, and are made outward
oriented solids. For every other shell the winding number just outside it is summed from the orientation of the
shells containing it, tested from a point on its largest face. The shell bounds the solid only if the winding
number is zero on exactly one of its sides; it is then oriented so that the solid is inside, or dropped otherwise.

Each group of crossing shells is united at once with libigl's exact `mesh_boolean()`, which resolves any overlap,
coplanar contacts included, by winding number. CGAL's `corefine_and_compute_union()` is not used: on shells touching
along coplanar faces it can report success with a broken mesh, or crash, even when its documented preconditions hold.
The union has to be a valid closed mesh; a group whose union fails keeps its shells, which overlap, and slicing
handles that.

## Robustness

The repair avoids `experimental::autorefine_and_remove_self_intersections()` and corefinement, which can crash on
messy input, and uses only steps whose failures it can detect. A step that fails keeps its input. CGAL precondition failures surface
as `repair_mesh()` returning false with the message as the error. The output is written in float, as every mesh in
the application is, and rounding the vertices that unions create can make two of them coincide.

The result does not depend on thread scheduling. `libslic3r_cgal` is built with `CGAL_LINKED_WITH_TBB`, and the
repair runs only the detection of self-intersections in parallel; its result is put in order before it is used.
Snapping stays sequential, as its parallel version gives different results from run to run.

## Verification

`tests/libslic3r/test_mesh_repair.cpp` covers each kind of defect above on small meshes with known volumes.
