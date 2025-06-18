# Infill

Infill is the internal structure of a 3D print, providing strength and support. It can be adjusted to balance material usage, print time, and part strength.

## Sparse infill density

Density usually should be calculated as a % of the total infill volume, not the total print volume.
Higher density increases strength but also material usage and print time. Lower density saves material and time but reduces strength.

Nevertheless, **not all patterns interpret density the same way**, so the actual material usage may vary. You can see each pattern's material usage in the [Sparse Infill Pattern](#sparse-infill-pattern) section.

## Sparse Infill Pattern

Infill patterns determine how material is distributed within a print. Different patterns can affect strength, flexibility, and print speed using the same density setting.

There is no one-size-fits-all solution, as the best pattern depends on the specific print and its requirements.

Many patterns may look similar and have similar overall specifications, but they can behave very differently in practice.
As most settings in 3D printing, experience is the best way to determine which pattern works best for your specific needs.

| Infill                                      | X-Y Strength | Z Strength  | Material Usage | Print Time  |
|---------------------------------------------|--------------|-------------|----------------|-------------|
| [Concentric](#concentric)                   | Low          | Normal      | Normal         | Normal      |
| [Rectilinear](#rectilinear)                 | Normal-Low   | Low         | Normal         | Normal      |
| [Grid](#grid)                               | High         | High        | Normal         | Normal      |
| [2D Lattice](#2d-lattice)                   | Normal-Low   | Low         | Normal         | Normal      |
| [Line](#line)                               | Low          | Low         | Normal         | Normal-Low  |
| [Cubic](#cubic)                             | High         | High        | Normal         | Normal-Low  |
| [Triangles](#triangles)                     | High         | Normal      | Normal         | Normal-Low  |
| [Tri-hexagon](#tri-hexagon)                 | High         | Normal-High | Normal         | Normal-Low  |
| [Gyroid](#gyroid)                           | High         | High        | Normal         | Normal-High |
| [TPMS-D](#tpms-d)                           | High         | High        | Normal         | High        |
| [Honeycomb](#honeycomb)                     | High         | High        | High           | Ultra-High  |
| [Adaptive Cubic](#adaptive-cubic)           | Normal-High  | Normal-High | Low            | Low         |
| [Aligned Rectilinear](#aligned-rectilinear) | Normal-Low   | Normal      | Normal         | Normal      |
| [2D Honeycomb](#2d-honeycomb)               | Normal-Low   | Normal-Low  | Normal         | Normal-Low  |
| [3D Honeycomb](#3d-honeycomb)               | Normal-High  | Normal-High | Normal-Low     | High        |
| [Hilbert Curve](#hilbert-curve)             | Low          | Normal      | Normal         | High        |
| [Archimedean Chords](#archimedean-chords)   | Low          | Normal      | Normal         | Normal-Low  |
| [Octagram Spiral](#octagram-spiral)         | Low          | Normal      | Normal         | Normal-High |
| [Support Cubic](#support-cubic)             | Low          | Low         | Extra-Low      | Extra-Low   |
| [Lightning](#lightning)                     | Low          | Low         | Ultra-Low      | Ultra-Low   |
| [Cross Hatch](#cross-hatch)                 | Normal-High  | Normal-High | Normal         | Normal-High |
| [Quarter Cubic](#quarter-cubic)             | High         | High        | Normal         | Normal-Low  |

> [!NOTE]
> You can download [infill_desc_calculator.xlsx](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/print_settings/strength/infill_desc_calculator.xlsx?raw=true) used to calculate the values above.

### Concentric

Fills the area with progressively smaller versions of the outer contour, creating a concentric pattern. Ideal for 100% infill or flexible prints.

- **Horizontal Strength (X-Y):** Low
- **Vertical Strength (Z):** Normal
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal
- **Material/Time (Higher better):** Normal-High

![infill-top-concentric](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-concentric.png?raw=true)

### Rectilinear

Parallel lines spaced according to infill density. Each layer is printed perpendicular to the previous, resulting in low vertical bonding.

- **Horizontal Strength (X-Y):** Normal-Low
- **Vertical Strength (Z):** Low
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal
- **Material/Time (Higher better):** Normal-High

![infill-top-rectilinear](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-rectilinear.png?raw=true)

### Grid

Two-layer pattern of perpendicular lines, forming a grid. Overlapping points may cause noise or artifacts.

- **Horizontal Strength (X-Y):** High
- **Vertical Strength (Z):** High
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal
- **Material/Time (Higher better):** Normal

![infill-top-grid](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-grid.png?raw=true)

### 2D Lattice

Low-strength pattern with good flexibility. Angle 1 and angle 2 TBD.

- **Horizontal Strength (X-Y):** Normal-Low
- **Vertical Strength (Z):** Low
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal
- **Material/Time (Higher better):** Normal

![infill-top-2d-lattice](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-2d-lattice.png?raw=true)

### Line

Similar to [rectilinear](#rectilinear), but each line is slightly rotated to improve print speed.

- **Horizontal Strength (X-Y):** Low
- **Vertical Strength (Z):** Low
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Material/Time (Higher better):** Normal-High

![infill-top-line](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-line.png?raw=true)

### Cubic

3D cube pattern with corners facing down, distributing force in all directions. Triangles in the horizontal plane provide good X-Y strength.

- **Horizontal Strength (X-Y):** High
- **Vertical Strength (Z):** High
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Material/Time (Higher better):** Normal-High

![infill-top-cubic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-cubic.png?raw=true)

### Triangles

Triangle-based grid, offering strong X-Y strength but with triple overlaps at intersections.

- **Horizontal Strength (X-Y):** High
- **Vertical Strength (Z):** Normal
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Material/Time (Higher better):** Normal-High

![infill-top-triangles](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-triangles.png?raw=true)

### Tri-hexagon

Similar to the [triangles](#triangles) pattern but offset to prevent triple overlaps at intersections. This design combines triangles and hexagons, providing excellent X-Y strength.

- **Horizontal Strength (X-Y):** High
- **Vertical Strength (Z):** Normal-High
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Material/Time (Higher better):** Normal-High

![infill-top-tri-hexagon](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-tri-hexagon.png?raw=true)

### Gyroid

Mathematical, isotropic surface providing equal strength in all directions. Excellent for strong, flexible prints and resin filling due to its interconnected structure.

- **Horizontal Strength (X-Y):** High
- **Vertical Strength (Z):** High
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-High
- **Material/Time (Higher better):** Low

![infill-top-gyroid](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-gyroid.png?raw=true)

### TPMS-D

Triply Periodic Minimal Surface - D. Hybrid between [Cross Hatch](#cross-hatch) and [Gyroid](#gyroid), combining rigidity and smooth transitions. Isotropic and strong in all directions.

- **Horizontal Strength (X-Y):** High
- **Vertical Strength (Z):** High
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** High
- **Material/Time (Higher better):** Low

![infill-top-tpms-d](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-tpms-d.png?raw=true)

### Honeycomb

Hexagonal pattern balancing strength and material use. Double walls in each hexagon increase material consumption.

- **Horizontal Strength (X-Y):** High
- **Vertical Strength (Z):** High
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** High
- **Print Time:** Ultra-High
- **Material/Time (Higher better):** Extra Low

![infill-top-honeycomb](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-honeycomb.png?raw=true)

### Adaptive Cubic

[Cubic](#cubic) pattern with adaptive density: denser near walls, sparser in the center. Saves material and time while maintaining strength, ideal for large prints.

- **Horizontal Strength (X-Y):** Normal-High
- **Vertical Strength (Z):** Normal-High
- **Density Calculation:** Same as [Cubic](#cubic) but reduced in the center
- **Material Usage:** Low
- **Print Time:** Low
- **Material/Time (Higher better):** Normal

![infill-top-adaptive-cubic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-adaptive-cubic.png?raw=true)

### Aligned Rectilinear

Parallel lines spaced by the infill spacing, each layer printed in the same direction as the previous layer. Good horizontal strength perpendicular to the lines, but terrible in parallel direction.
Recommended with layer anchoring to improve not perpendicular strength.

- **Horizontal Strength (X-Y):** Normal-Low
- **Vertical Strength (Z):** Normal
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal
- **Material/Time (Higher better):** Normal

![infill-top-aligned-rectilinear](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-aligned-rectilinear.png?raw=true)

### 2D Honeycomb

Vertical Honeycomb pattern. Acceptable torsional stiffness. Developed for low densities structures like wings. Improve over [2D Lattice](#2d-lattice) offers same performance with lower densities.This infill includes a Overhang angle parameter to improve interlayer point of contact and reduce the risk of delamination.

- **Horizontal Strength (X-Y):** Normal-Low
- **Vertical Strength (Z):** Normal-Low
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Material/Time (Higher better):** Normal

![infill-top-2d-honeycomb](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-2d-honeycomb.png?raw=true)

### 3D Honeycomb

This infill tries to generate a printable honeycomb structure by printing squares and octagons mantaining a vertical angle high enough to mantian contact with the previous layer.

- **Horizontal Strength (X-Y):** Normal-High
- **Vertical Strength (Z):** Normal-High
- **Density Calculation:** Unknown
- **Material Usage:** Normal-Low
- **Print Time:** High
- **Material/Time (Higher better):** Extra Low

![infill-top-3d-honeycomb](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-3d-honeycomb.png?raw=true)

### Hilbert Curve

Hilbert Curve is a space-filling curve that can be used to create a continuous infill pattern. It is known for its Esthetic appeal and ability to fill space efficiently.
Print speed is very low due to the complexity of the path, which can lead to longer print times. It is not recommended for structural parts but can be used for Esthetic purposes.

- **Horizontal Strength (X-Y):** Low
- **Vertical Strength (Z):** Normal
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** High
- **Material/Time (Higher better):** Extra Low

![infill-top-hilbert-curve](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-hilbert-curve.png?raw=true)

### Archimedean Chords

Spiral pattern that fills the area with concentric arcs, creating a smooth and continuous infill. Can be filled with resin thanks to its interconnected hollow structure, which allows the resin to flow through it and cure properly.

- **Horizontal Strength (X-Y):** Low
- **Vertical Strength (Z):** Normal
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Material/Time (Higher better):** Normal-High

![infill-top-archimedean-chords](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-archimedean-chords.png?raw=true)

### Octagram Spiral

Esthetic pattern with low strength and high print time.

- **Horizontal Strength (X-Y):** Low
- **Vertical Strength (Z):** Normal
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-High
- **Material/Time (Higher better):** Normal

![infill-top-octagram-spiral](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-octagram-spiral.png?raw=true)

### Support Cubic

Support |Cubic is a variation of the [Cubic](#cubic) infill pattern that is specifically designed for support top layers. Will use more material than Lightning infill but will provide better strength. Nevertheless, it is still a low-density infill pattern.

- **Horizontal Strength (X-Y):** Low
- **Vertical Strength (Z):** Low
- **Density Calculation:** % of layer before top shell layers
- **Material Usage:** Extra-Low
- **Print Time:** Extra-Low
- **Material/Time (Higher better):** Normal

![infill-top-support-cubic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-support-cubic.png?raw=true)

### Lightning

Ultra-fast, ultra-low material infill. Designed for speed and efficiency, ideal for quick prints or non-structural prototypes.

- **Horizontal Strength (X-Y):** Low
- **Vertical Strength (Z):** Low
- **Density Calculation:** % of layer before top shell layers
- **Material Usage:** Ultra-Low
- **Print Time:** Ultra-Low
- **Material/Time (Higher better):** Extra Low

![infill-top-lightning](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-lightning.png?raw=true)

### Cross Hatch

Similar to [Gyroid](#gyroid) but with linear patterns, creating weak points at internal corners.

- **Horizontal Strength (X-Y):** Normal-High
- **Vertical Strength (Z):** Normal-High
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-High
- **Material/Time (Higher better):** Low

![infill-top-cross-hatch](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-cross-hatch.png?raw=true)

### Quarter Cubic

[Cubic](#cubic) pattern with extra internal divisions, improving X-Y strength.

- **Horizontal Strength (X-Y):** High
- **Vertical Strength (Z):** High
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Material/Time (Higher better):** Normal

![infill-top-quarter-cubic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-quarter-cubic.png?raw=true)
