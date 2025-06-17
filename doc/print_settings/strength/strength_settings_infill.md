# Infill

WIP...

## Sparse infill density

WIP...

## Sparse Infill Pattern

WIP...

### Concentric

Concentric will fill the area with smaller versions of the outer contour, creating a concentric pattern. Useful in 100% infills and some flexible print cases.

- Horizontal Strength (X-Y): Low
- Vertical Strength (Z): Normal
- Density Calculation: by layer
- Material Usage: Normal
- Time: Normal
- Material/Time (Higher better): Normal-High

![infill-top-concentric](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-concentric.png?raw=true)
![infill-iso-concentric](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-iso-concentric.png?raw=true)

### Rectilinear

Parallel lines spaced by the infill spacing. Each layer is printed in perpendicular direction to the previous layer causing low vertical point of contact.

- Horizontal Strength (X-Y): Normal-Low
- Vertical Strength (Z): Low
- Density Calculation: by layer
- Material Usage: Normal
- Time: Normal
- Material/Time (Higher better): Normal

![infill-top-rectilinear](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-rectilinear.png?raw=true)
![infill-iso-rectilinear](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-iso-rectilinear.png?raw=true)

### Grid

Grid pattern, self repeating by layer. Printed in pararllel lines causing overlapping points that may cause noises and artifacts in the print.

- Horizontal Strength (X-Y): High
- Vertical Strength (Z): High
- Density Calculation: Total infill material usage.
- Material Usage: Normal
- Time: Normal
- Material/Time (Higher better): Normal

![infill-top-grid](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-grid.png?raw=true)
![infill-iso-grid](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-iso-grid.png?raw=true)

### 2D Lattice

Aesthetic, Low strength pattern, with good flexibility.

- Horizontal Strength (X-Y): Normal
- Vertical Strength (Z): Low
- Density Calculation: by layer
- Material Usage: Normal
- Time: Normal
- Material/Time (Higher better): Normal

![infill-top-2d-lattice](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-2d-lattice.png?raw=true)
![infill-iso-2d-lattice](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-iso-2d-lattice.png?raw=true)

### Line

Similar to rectilinear but each line is slightly rotated to improve print time.

- Horizontal Strength (X-Y): Low
- Vertical Strength (Z): Low
- Density Calculation: by layer
- Material Usage: Normal
- Time: Normal
- Material/Time (Higher better): Normal-High

![infill-top-line](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-line.png?raw=true)
![infill-iso-line](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-iso-line.png?raw=true)

### Cubic

Cubes with one corner facing down distribuiting force in all directions. This generates triangles in the horizontal plane, which gives it a good strength in the X-Y plane.

- Horizontal Strength (X-Y): High
- Vertical Strength (Z): High
- Density Calculation: by layer
- Material Usage: Normal
- Time: Normal
- Material/Time (Higher better): Normal-High

![infill-top-cubic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-cubic.png?raw=true)
![infill-iso-cubic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-iso-cubic.png?raw=true)

### Triangles

Triangles printed in lines (like #Grid) giving a good strength in the X-Y plane but generating a triple overlapping point when it intersects with the previous extrusion.

- Horizontal Strength (X-Y): High
- Vertical Strength (Z): Normal
- Density Calculation: by layer
- Material Usage: Normal
- Time: Normal
- Material/Time (Higher better): Normal-High

![infill-top-triangles](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-triangles.png?raw=true)
![infill-iso-triangles](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-iso-triangles.png?raw=true)

### Tri-hexagon

Like triangles but displaced to avoid triple overlapping points. This pattern is a combination of triangles and hexagons, which gives it a good strength in the X-Y plane.

- Horizontal Strength (X-Y): High
- Vertical Strength (Z): Normal-High
- Density Calculation: by layer
- Material Usage: Normal
- Time: Normal-Low
- Material/Time (Higher better): Normal-High

![infill-top-tri-hexagon](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-tri-hexagon.png?raw=true)
![infill-iso-tri-hexagon](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-iso-tri-hexagon.png?raw=true)

### Gyroid

Gyroid is a mathematical surface that is used to create a strong and flexible infill pattern. It is isotropic, meaning that it has the same strength in all directions. This makes it a good choice for prints that need to be strong and flexible.
This shape can be filled with resing thanks to its interconnected hollow structure, which allows the resin to flow through it and cure properly.

- Horizontal Strength (X-Y): High
- Vertical Strength (Z): High
- Density Calculation: by layer
- Material Usage: Normal
- Time: High
- Material/Time (Higher better): Low

![infill-top-gyroid](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-gyroid.png?raw=true)
![infill-iso-gyroid](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-iso-gyroid.png?raw=true)

### TPMS-D

TPMS-D (Triply Periodic Minimal Surface - D) is a mathematical surface that is used to create a strong and flexible infill pattern. It is isotropic, meaning that it has the same strength in all directions. This pattern is a hybrid between CrossHatch and Gyroid, combining the structural rigidity and consistency of CrossHatch with the smooth, organic transitions characteristic of Gyroid infill.

- Horizontal Strength (X-Y): High
- Vertical Strength (Z): High
- Density Calculation: by layer
- Material Usage: Normal
- Time: High
- Material/Time (Higher better): Low

![infill-top-tpms-d](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-tpms-d.png?raw=true)
![infill-iso-tpms-d](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-iso-tpms-d.png?raw=true)

### Honeycomb

Hexagonal pattern that provides a good balance between strength and material usage.
It's optimized for printing lines making double 2 of 6 walls in each hexagon causing an increasing material usage.

- Horizontal Strength (X-Y): High
- Vertical Strength (Z): High
- Density Calculation: by layer
- Material Usage: High
- Time: Ultra High
- Material/Time (Higher better): Extra Low

![infill-top-honeycomb](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-honeycomb.png?raw=true)
![infill-iso-honeycomb](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-iso-honeycomb.png?raw=true)

### Adaptive Cubic

Adaptive Cubic is a variation of the Cubic infill pattern that adjusts the density of the infill based on the geometry of the model. It uses a cubic pattern near the outer walls and a more sparse pattern in the center, which reduces material usage while maintaining acceptable strength. This pattern is particularly useful for large prints where material and time savings are important.

- Horizontal Strength (X-Y): Normal-High
- Vertical Strength (Z): Normal-High
- Density Calculation: by layer but reduced in the center
- Material Usage: Low
- Time: Low
- Material/Time (Higher better): Normal

![infill-top-adaptive-cubic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-adaptive-cubic.png?raw=true)
![infill-iso-adaptive-cubic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-iso-adaptive-cubic.png?raw=true)

### Aligned Rectilinear

Parallel lines spaced by the infill spacing, each layer printed in the same direction as the previous layer. Good horizontal strength perpendicular to the lines, but terrible in parallel direction.
Recommended with layer anchoring to improve not perpendicular strength.

- Horizontal Strength (X-Y): Normal-Low
- Vertical Strength (Z): Normal
- Density Calculation: by layer
- Material Usage: Normal
- Time: Normal
- Material/Time (Higher better): Normal

![infill-top-aligned-rectilinear](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-aligned-rectilinear.png?raw=true)
![infill-iso-aligned-rectilinear](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-iso-aligned-rectilinear.png?raw=true)

### 3D Honeycomb

This infill tries to generate a printable honeycomb structure by printing squares and octagons mantaining a vertical angle high enough to mantian contact with the previous layer.

- Horizontal Strength (X-Y): Normal
- Vertical Strength (Z): Normal-High
- Density Calculation: by layer reduced
- Material Usage: Normal-Low
- Time: High
- Material/Time (Higher better): Extra Low

![infill-top-3d-honeycomb](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-3d-honeycomb.png?raw=true)
![infill-iso-3d-honeycomb](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-iso-3d-honeycomb.png?raw=true)

### Hilbert Curve

Hilbert Curve is a space-filling curve that can be used to create a continuous infill pattern. It is known for its aesthetic appeal and ability to fill space efficiently.
Print speed is very low due to the complexity of the path, which can lead to longer print times. It is not recommended for structural parts but can be used for aesthetic purposes.

- Horizontal Strength (X-Y): Low
- Vertical Strength (Z): Normal
- Density Calculation: by layer
- Material Usage: Normal
- Time: Extra High
- Material/Time (Higher better): Extra Low

![infill-top-hilbert-curve](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-hilbert-curve.png?raw=true)
![infill-iso-hilbert-curve](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-iso-hilbert-curve.png?raw=true)

### Archimedean Chords

Spiral pattern that fills the area with concentric arcs, creating a smooth and continuous infill. Can be filled with resin thanks to its interconnected hollow structure, which allows the resin to flow through it and cure properly.

- Horizontal Strength (X-Y): Low
- Vertical Strength (Z): Normal
- Density Calculation: by layer
- Material Usage: Normal
- Time: Normal
- Material/Time (Higher better): Normal-High

![infill-top-archimedean-chords](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-archimedean-chords.png?raw=true)
![infill-iso-archimedean-chords](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-iso-archimedean-chords.png?raw=true)

### Octagram Spiral

Aesthetic pattern with low strength and high print time.

- Horizontal Strength (X-Y): Low
- Vertical Strength (Z): Normal
- Density Calculation: by layer
- Material Usage: Normal
- Time: High
- Material/Time (Higher better): Normal

![infill-top-octagram-spiral](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-octagram-spiral.png?raw=true)
![infill-iso-octagram-spiral](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-iso-octagram-spiral.png?raw=true)

### Support Cubic

Support Cubic is a variation of the Cubic infill pattern that is specifically designed for support top layers. Will use more material than Lightning infill but will provide better strength. Nevertheless, it is still a low-density infill pattern.

- Horizontal Strength (X-Y): Low
- Vertical Strength (Z): Low
- Density Calculation: before top shell layers
- Material Usage: Extra Low
- Time: Extra Low
- Material/Time (Higher better): Normal

![infill-top-support-cubic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-support-cubic.png?raw=true)
![infill-iso-support-cubic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-iso-support-cubic.png?raw=true)

### Lightning

Lightning is an ultra-fast infill pattern that uses a minimal amount of material. It is designed for speed and efficiency, making it ideal for quick prints or prototypes with no structural requirements.

- Horizontal Strength (X-Y): Low
- Vertical Strength (Z): Low
- Density Calculation: before top shell layers
- Material Usage: Ultra Low
- Time: Ultra Low
- Material/Time (Higher better): Extra Low

![infill-top-lightning](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-lightning.png?raw=true)
![infill-iso-lightning](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-iso-lightning.png?raw=true)

### Cross Hatch

Cross Hatch can be compared to Gyroid but with linear patterns causing weak points in each corner it generates internally.

- Horizontal Strength (X-Y): Normal-High
- Vertical Strength (Z): Normal-High
- Density Calculation: by layer
- Material Usage: Normal
- Time: High
- Material/Time (Higher better): Low

![infill-top-cross-hatch](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-cross-hatch.png?raw=true)
![infill-iso-cross-hatch](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-iso-cross-hatch.png?raw=true)

### Quarter Cubic

Similar to Cubic but adding an extra division to the cubic pattern adding internal walls to improve the strength in the X-Y plane.

- Horizontal Strength (X-Y): High
- Vertical Strength (Z): High
- Density Calculation: by layer
- Material Usage: Normal
- Time: Normal
- Material/Time (Higher better): Normal

![infill-top-quarter-cubic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-quarter-cubic.png?raw=true)
![infill-iso-quarter-cubic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-iso-quarter-cubic.png?raw=true)

> [!NOTE]
> infill_desc_calculator.xlsx was used to calculate the values above.