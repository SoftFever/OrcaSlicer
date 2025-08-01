# Other layers speed

## Speed limitations

> [!IMPORTANT]
> Every speed setting is limited by several parameters like:
> - [Maximum Volumetric Speed](volumetric-speed-calib)
> - Machine / Motion ability
> - [Acceleration](speed-settings-acceleration)
> - [Jerk settings](speed-settings-jerk)

- [Speed limitations](#speed-limitations)
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

Speed of outer wall which is outermost and visible. It's used to be slower than [inner wall speed](#inner-wall) to get better quality and good layer adhesion.
This setting is also limited by [Machine / Motion ability / Resonance avoidance speed settings](vfa-calib).

## Inner wall

Speed of inner wall which is printed faster than outer wall to reduce print time but is still recommended to be slower than the [maximum volumetric speed](volumetric-speed-calib) to ensure good layer adhesion and reduce material internal stresses.

## Small perimeters

Speed of outer wall with theoretical radius <= [small perimeters threshold](#small-perimeters-threshold).
Any shape (not only circles) will be considered as a small perimeter.

If expressed as percentage (for example: 80%) it will be calculated on the [outer wall speed](#outer-wall).

> [!NOTE]
> Zero will use [50%](https://github.com/SoftFever/OrcaSlicer/blob/7d2a12aa3cbf2e7ca5d0523446bf1d1d4717f8d1/src/libslic3r/GCode.cpp#L4698) of [outer wall speed](#outer-wall).

### Small perimeters threshold

**Radius** in millimeters below which the speed of perimeters will be reduced to the [small perimeters speed](#small-perimeters).  
To know the length of the perimeter, you can use the formula:

```math
\frac{\text{Perimeter Length}}{2\pi} \leq \text{Threshold}
```

For example, if the threshold is set to 5 mm, then the perimeter length must be less than or equal to 31.4 mm (2 * π * 5 mm) to be considered a small perimeter.

- A Circle with a diameter of 10 mm will have a perimeter length of approximately 31.4 mm, which is equal to the threshold, so it will be considered a small perimeter.
- A Cube of 10mm x 10mm will have a perimeter length of 40 mm, which is greater than the threshold, so it will not be considered a small perimeter.
- A Cube of 5mm x 5mm will have a perimeter length of 20 mm, which is less than the threshold, so it will be considered a small perimeter.

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
