# Infill corner smoothing — High Level Design

## Purpose and scope

`sparse_infill_smooth_factor` rounds the corners of the sparse infill paths of
the patterns `is_smoothable_infill_pattern()` lists, so the toolhead does not
slow to near a stop in every corner. `sparse_infill_smooth_curve` picks the
curve. `FillCornerSmoothing` does the rounding for every pattern; the fills only
call it on their paths before clipping and before `multiline_fill()` copies them.

The design is shaped by how firmware plans speed. Klipper, Marlin 2 with
junction deviation and OrcaSlicer's estimator limit the speed at the junction of
two moves by how much the path turns there, and by the length of the moves for
the centripetal part. A curve is printed as straight moves, so its speed depends
on how it is cut into moves as much as on its shape, and the cut decides the size
of the G-code.

## The curves

Each corner is replaced by a curve that runs from `reach` before it along the
incoming leg to `reach` after it along the outgoing one, `reach` being the factor
times half of the shorter leg, so the curves of two corners never overlap.

- G1 is the circular arc tangent to both legs. It has the lowest peak curvature
  for a reach, but its curvature jumps from zero at both legs.
- G2 is a quintic Bézier curve whose first three control points on each leg are
  equally spaced, which makes its curvature zero at the legs.
- G3 is a septic Bézier curve with four control points on each leg, which also
  makes the rate of its curvature zero there.

The control points are 0.3 (quintic) and 0.2 (septic) of the reach apart. Tuning
the spacing to the turn angle lowers the peak curvature of sharp turns but made
no measurable difference to print time.

## The vertices

A corner is printed as the polygon of the curve's tangents taken at equal steps
of direction: every vertex turns by the same angle, which spends each vertex
where the planner needs it. Uniform steps of the curve parameter put vertices
where the curve is nearly straight and leave the sharpest turn in the middle.

The number of vertices is the smaller of two counts:

- **What the printer needs.** The step at which a junction allows the speed the
  curve allows at its peak curvature, from the junction deviation the infill is
  planned with (`infill_junction_deviation()` in `Fill.cpp`: Klipper's from the
  square corner velocity and the infill acceleration, Marlin 2's from the machine
  limits). The step is never finer than lets each vertex stand out of the chord
  of its neighbours by the G-code resolution, as `LayerRegion::simplify_*` would
  drop it again. Without a junction deviation, the polygon keeps within the
  resolution of the curve.
- **The G-code budget.** Rounded corners may take a number of vertices per area
  of infill (`smoothed_infill_vertex_density()`). The curves of a pattern grow
  with the distance between its lines, and a curve flattened to a fixed chord
  times sagitta takes a vertex about every cbrt(radius) along it, so a path laid
  at the density may take a vertex about every cbrt(0.176 mm² × distance): 1.2 mm
  at 5% and 0.55 mm at 50% density with 0.45 mm lines. Each fill divides the
  budget by the path it prints per area (`Fill::corner_smoothing()`), so a
  pattern laying more path gets fewer vertices per mm. A corner owns half of each
  leg; what it leaves of its budget goes on to the next corners of the path, so
  corners whose own share is short of two vertices take turns.

When the budget is the smaller count, the curve is tightened to the radius at
which its vertices are as fast as the curve, `max(8 × deviation, 2 ×
resolution) / step²`. A few vertices spread over a large curve are slower than
the sharp corner: every vertex is a slowdown and the short moves between them
leave no room to accelerate.

## Constraints

- Rounding must not inflate the G-code where corners are dense. Splitting every
  corner as finely as the junction deviation asks would take several times the
  G-code of the sharp path there; with the budget, Honeycomb takes about twice
  and Hilbert Curve three times the G-code of its sharp path at 100%.
- With the factor at 0 the paths are unchanged, and the fills keep their order of
  operations, so that rounding to the integer grid does not move a point.
- The plane-path fills (Hilbert Curve, Octagram Spiral) produce their path point
  by point in units of the line distance, so `CornerSmoother` takes points one
  at a time, and their budget follows from their density, which they lay
  exactly.
