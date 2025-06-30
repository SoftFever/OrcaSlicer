# Other layers speed

- [Outer wall](#outer-wall)
- [Inner wall](#inner-wall)
- [Small perimeters](#small-perimeters)
  - [Small perimeters threshold](#small-perimeters-threshold)
- [Sparse infill](#sparse-infill)
- [Internal solid infill](#internal-solid-infill)
- [Top surface](#top-surface)
- [Gap infill](#gap-infill)
- [Ironing speed](#ironing-speed)
- [Support](#support)
- [Support interface](#support-interface)

## Outer wall

Speed of outer wall which is outermost and visible. It's used to be slower than inner wall speed to get better quality.

## Inner wall

Speed of inner wall which is printed faster than outer wall to reduce print time.

## Small perimeters

This separate setting will affect the speed of perimeters having radius <= small_perimeter_threshold (usually holes). If expressed as percentage (for example: 80%) it will be calculated on the outer wall speed setting above.  
Set to zero for auto.

### Small perimeters threshold

This sets the threshold for small perimeter length.  
Default threshold is 0mm.

## Sparse infill

Speed of sparse infill which is printed faster than solid infill to reduce print time.

## Internal solid infill

Speed of internal solid infill which is printed faster than top surface speed to reduce print time.

## Top surface

Speed of top surface which is printed slower than internal solid infill to get better quality.

## Gap infill

Speed of gap infill which is printed faster than top surface speed to reduce print time.

## Ironing speed

Ironing speed, typically slower than the top surface speed to ensure a smooth finish.

## Support

Speed of support material which is printed slower than the main model to ensure proper adhesion and prevent sagging.

## Support interface

Speed of support interface material which is printed slower than the main support material to ensure proper adhesion and prevent sagging.
