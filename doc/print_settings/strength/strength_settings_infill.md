# Infill

Infill is the internal structure of a 3D print, providing strength and support. It can be adjusted to balance material usage, print time, and part strength.

- [Sparse infill density](#sparse-infill-density)
- [Fill Multiline](#fill-multiline)
  - [Use cases](#use-cases)
- [Direction and Rotation](#direction-and-rotation)
  - [Direction](#direction)
  - [Rotation](#rotation)
- [Infill Wall Overlap](#infill-wall-overlap)
- [Apply gap fill](#apply-gap-fill)
- [Anchor](#anchor)
- [Internal Solid Infill](#internal-solid-infill)
- [Sparse Infill Pattern](#sparse-infill-pattern)
  - [Rectilinear](#rectilinear)
  - [Aligned Rectilinear](#aligned-rectilinear)
  - [Zig Zag](#zig-zag)
  - [Cross Zag](#cross-zag)
  - [Locked Zag](#locked-zag)
  - [Line](#line)
  - [Grid](#grid)
  - [Triangles](#triangles)
  - [Tri-hexagon](#tri-hexagon)
  - [Cubic](#cubic)
  - [Adaptive Cubic](#adaptive-cubic)
  - [Quarter Cubic](#quarter-cubic)
  - [Support Cubic](#support-cubic)
  - [Lightning](#lightning)
  - [Honeycomb](#honeycomb)
  - [3D Honeycomb](#3d-honeycomb)
  - [2D Honeycomb](#2d-honeycomb)
  - [2D Lattice](#2d-lattice)
  - [Cross Hatch](#cross-hatch)
  - [TPMS-D](#tpms-d)
  - [Gyroid](#gyroid)
  - [Concentric](#concentric)
  - [Hilbert Curve](#hilbert-curve)
  - [Archimedean Chords](#archimedean-chords)
  - [Octagram Spiral](#octagram-spiral)

## Sparse infill density

Density usually should be calculated as a % of the total infill volume, not the total print volume.
Higher density increases strength but also material usage and print time. Lower density saves material and time but reduces strength.

Nevertheless, **not all patterns interpret density the same way**, so the actual material usage may vary. You can see each pattern's material usage in the [Sparse Infill Pattern](#sparse-infill-pattern) section.

## Fill Multiline

This setting allows you to generate your selected [infill pattern](#sparse-infill-pattern) using multiple parallel lines while preserving both the defined [infill density](#sparse-infill-density) and the overall material usage.

![infill-multiline-1-5](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-multiline-1-5.gif?raw=true)

> [!NOTE]
> Orca's approach is different from other slicers that simply multiply the number of lines and material usage, generating a denser infill than expected.
>
>| Infill   Density % | Infill Lines | Orca Density | Other Slicers Density |
>|--------------------|--------------|--------------|-----------------------|
>| 10%                | 2            | 10%          | 20%                   |
>| 25%                | 2            | 25%          | 50%                   |
>| 40%                | 2            | 40%          | 80%                   |
>| 10%                | 3            | 10%          | 30%                   |
>| 25%                | 3            | 25%          | 75%                   |
>| 40%                | 3            | 40%          | 120% *                |
>| 10%                | 5            | 10%          | 50%                   |
>| 25%                | 5            | 25%          | 125% *                |
>| 40%                | 5            | 40%          | 200% *                |
>
> *Other slicers may limit the result to 100%.

### Use cases

- Increasing the number of lines (e.g., 2 or 3) can improve part strength and print speed without increasing material usage.
- Fire-retardant applications: Some flame-resistant materials (like PolyMax PC-FR) require a minimum printed wall/infill thickness—often 1.5–3 mm—to comply with standards. Since infill contributes to overall part thickness, using multiple lines helps achieve the necessary thickness without switching to a large nozzle or printing with 100% infill. This is especially useful for high-temperature materials like PC, which are prone to warping when fully solid.
- Creating aesthetically pleasing infill patterns (like [Grid](#grid) or [Honeycomb](#honeycomb)) with multiple line widths—without relying on CAD modeling or being limited to a single extrusion width.

![infill-multiline-esthetic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-multiline-esthetic.gif?raw=true)

> [!WARNING]
> For self intersecting infills (e.g. [Cubic](#cubic), [Grid](#grid)) multiline count greater than 3 may cause layer shift, extruder clog or other issues due to overlapping of lines on intersection points.
>
> ![infill-multiline-overlapping](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-multiline-overlapping.gif?raw=true)

## Direction and Rotation

### Direction

Controls the direction of the infill lines to optimize or strengthen the print.

### Rotation

This parameter adds a rotation to the sparse infill direction for each layer according to the specified template. The template is a comma-separated list of angles in degrees.

For example:

```c++
0,90
```

The first layer uses 0°, the second uses 90°, and the pattern repeats for subsequent layers.

Other examples:

```c++
0,45,90
```

```c++
0,60,120,180
```

If there are more layers than angles, the sequence repeats.
> [!NOTE]
> Not all sparse infill patterns support rotation.

## Infill Wall Overlap

Infill area is enlarged slightly to overlap with wall for better bonding. The percentage value is relative to line width of sparse infill. Set this value to ~10-15% to minimize potential over extrusion and accumulation of material resulting in rough surfaces.

- **Infill Wall Overlap Off**

![InfillWallOverlapOff](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/InfillWallOverlapOff.svg?raw=true)

- **Infill Wall Overlap On**

![InfillWallOverlapOn](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/InfillWallOverlapOn.svg?raw=true)

## Apply gap fill

Enables gap fill for the selected solid surfaces. The minimum gap length that will be filled can be controlled from the filter out tiny gaps option.

1. **Everywhere:** Applies gap fill to top, bottom and internal solid surfaces for maximum strength.
2. **Top and Bottom surfaces:** Applies gap fill to top and bottom surfaces only, balancing print speed, reducing potential over extrusion in the solid infill and making sure the top and bottom surfaces have no pinhole gaps.
3. **Nowhere:** Disables gap fill for all solid infill areas.

Note that if using the [classic perimeter generator](quality_settings_wall_generator#classic), gap fill may also be generated between perimeters, if a full width line cannot fit between them.
That perimeter gap fill is not controlled by this setting.

If you would like all gap fill, including the classic perimeter generated one, removed, set the filter out tiny gaps value to a large number, like 999999.

However this is not advised, as gap fill between perimeters is contributing to the model's strength. For models where excessive gap fill is generated between perimeters, a better option would be to switch to the [arachne wall generator](quality_settings_wall_generator#arachne) and use this option to control whether the cosmetic top and bottom surface gap fill is generated.

## Anchor

Connect an infill line to an internal perimeter with a short segment of an additional perimeter. If expressed as percentage (example: 15%) it is calculated over infill extrusion width.
OrcaSlicer tries to connect two close infill lines to a short perimeter segment. If no such perimeter segment shorter than this parameter is found, the infill line is connected to a perimeter segment at just one side and the length of the perimeter segment taken is limited to infill_anchor, but no longer than this parameter. If set to 0, the old algorithm for infill connection will be used, it should create the same result as with 1000 & 0.

- **Anchor Off**

![InfillAnchorOff](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/InfillAnchorOff.png?raw=true)

- **Anchor On**

![InfillAnchorOn](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/InfillAnchorOn.png?raw=true)

## Internal Solid Infill

Line pattern of internal solid infill. If the [detect narrow internal solid infill](strength_settings_advanced#detect-narrow-internal-solid-infill) be enabled, the concentric pattern will be used for the small area.

## Sparse Infill Pattern

Infill patterns determine how material is distributed within a print. Different patterns can affect strength, flexibility, and print speed using the same density setting.

There is no one-size-fits-all solution, as the best pattern depends on the specific print and its requirements.

Many patterns may look similar and have similar overall specifications, but they can behave very differently in practice.
As most settings in 3D printing, experience is the best way to determine which pattern works best for your specific needs.

| SVG | Pattern | X-Y Strength | Z Strength | Material Usage | Print Time |
|---|---|---|---|---|---|
| ![param_rectilinear](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_rectilinear.svg?raw=true) | [Rectilinear](#rectilinear) | Normal-Low | Low | Normal | Normal-Low |
| ![param_alignedrectilinear](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_alignedrectilinear.svg?raw=true) | [Aligned   Rectilinear](#aligned-rectilinear) | Normal-Low | Normal | Normal | Normal-Low |
| ![param_zigzag](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_zigzag.svg?raw=true) | [Zig Zag](#zig-zag) | Normal-Low | Low | Normal | Normal |
| ![param_crosszag](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_crosszag.svg?raw=true) | [Cross   Zag](#cross-zag) | Normal | Low | Normal | Normal |
| ![param_lockedzag](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_lockedzag.svg?raw=true) | [Locked Zag](#locked-zag) | Normal-Low | Normal-Low | Normal-High | Extra-High |
| ![param_line](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_line.svg?raw=true) | [Line](#line) | Low | Low | Normal | Normal-Low |
| ![param_grid](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_grid.svg?raw=true) | [Grid](#grid) | High | High | Normal | Normal-Low |
| ![param_triangles](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_triangles.svg?raw=true) | [Triangles](#triangles) | High | Normal | Normal | Normal-Low |
| ![param_tri-hexagon](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_tri-hexagon.svg?raw=true) | [Tri-hexagon](#tri-hexagon) | High | Normal-High | Normal | Normal-Low |
| ![param_cubic](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_cubic.svg?raw=true) | [Cubic](#cubic) | High | High | Normal | Normal-Low |
| ![param_adaptivecubic](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_adaptivecubic.svg?raw=true) | [Adaptive   Cubic](#adaptive-cubic) | Normal-High | Normal-High | Low | Low |
| ![param_quartercubic](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_quartercubic.svg?raw=true) | [Quarter   Cubic](#quarter-cubic) | High | High | Normal | Normal-Low |
| ![param_supportcubic](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_supportcubic.svg?raw=true) | [Support Cubic](#support-cubic) | Low | Low | Extra-Low | Extra-Low |
| ![param_lightning](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_lightning.svg?raw=true) | [Lightning](#lightning) | Low | Low | Ultra-Low | Ultra-Low |
| ![param_honeycomb](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_honeycomb.svg?raw=true) | [Honeycomb](#honeycomb) | High | High | High | Ultra-High |
| ![param_3dhoneycomb](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_3dhoneycomb.svg?raw=true) | [3D   Honeycomb](#3d-honeycomb) | Normal-High | Normal-High | Normal-Low | High |
| ![param_2dhoneycomb](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_2dhoneycomb.svg?raw=true) | [2D Honeycomb](#2d-honeycomb) | Normal-Low | Normal-Low | Normal | Normal-Low |
| ![param_2dlattice](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_2dlattice.svg?raw=true) | [2D   Lattice](#2d-lattice) | Normal-Low | Low | Normal | Normal-Low |
| ![param_crosshatch](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_crosshatch.svg?raw=true) | [Cross Hatch](#cross-hatch) | Normal-High | Normal-High | Normal | Normal-High |
| ![param_tpmsd](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_tpmsd.svg?raw=true) | [TPMS-D](#tpms-d) | High | High | Normal | High |
| ![param_gyroid](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_gyroid.svg?raw=true) | [Gyroid](#gyroid) | High | High | Normal | Normal-High |
| ![param_concentric](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_concentric.svg?raw=true) | [Concentric](#concentric) | Low | Normal | Normal | Normal |
| ![param_hilbertcurve](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_hilbertcurve.svg?raw=true) | [Hilbert Curve](#hilbert-curve) | Low | Normal | Normal | High |
| ![param_archimedeanchords](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_archimedeanchords.svg?raw=true) | [Archimedean   Chords](#archimedean-chords) | Low | Normal | Normal | Normal-Low |
| ![param_octagramspiral](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_octagramspiral.svg?raw=true) | [Octagram   Spiral](#octagram-spiral) | Low | Normal | Normal | Normal |

> [!NOTE]
> You can download [infill_desc_calculator.xlsx](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/print_settings/strength/infill_desc_calculator.xlsx?raw=true) used to calculate the values above.

### Rectilinear

Parallel lines spaced according to infill density. Each layer is printed perpendicular to the previous, resulting in low vertical bonding. Consider using new [Zig Zag](#zig-zag) infill instead.

- **Horizontal Strength (X-Y):** Normal-Low
- **Vertical Strength (Z):** Low
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Material/Time (Higher better):** Normal

![infill-top-rectilinear](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-rectilinear.png?raw=true)

### Aligned Rectilinear

Parallel lines spaced by the infill spacing, each layer printed in the same direction as the previous layer. Good horizontal strength perpendicular to the lines, but terrible in parallel direction.
Recommended with layer anchoring to improve not perpendicular strength.

- **Horizontal Strength (X-Y):** Normal-Low
- **Vertical Strength (Z):** Normal
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Material/Time (Higher better):** Normal

![infill-top-aligned-rectilinear](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-aligned-rectilinear.png?raw=true)

### Zig Zag

Similar to [rectilinear](#rectilinear) with consistent pattern between layers. Allows you to add a Symmetric infill Y axis for models with two symmetric parts.

- **Horizontal Strength (X-Y):** Normal-Low
- **Vertical Strength (Z):** Low
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal
- **Material/Time (Higher better):** Normal

![infill-top-zig-zag](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-zig-zag.png?raw=true)

### Cross Zag

Similar to [Zig Zag](#zig-zag) but displacing each layer with Infill shift step parameter.

- **Horizontal Strength (X-Y):** Normal
- **Vertical Strength (Z):** Low
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal
- **Material/Time (Higher better):** Normal

![infill-top-cross-zag](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-cross-zag.png?raw=true)

### Locked Zag

Adaptive version of [Zig Zag](#zig-zag) adding an external skin texture to interlock layers and a low material skeleton.

- **Horizontal Strength (X-Y):** Normal-Low
- **Vertical Strength (Z):** Normal-Low
- **Density Calculation:** Same as [Zig Zag](#zig-zag) but increasing near walls
- **Material Usage:** Normal-High
- **Print Time:** Extra-High
- **Material/Time (Higher better):** Low

![infill-top-locked-zag](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-locked-zag.png?raw=true)

### Line

Similar to [rectilinear](#rectilinear), but each line is slightly rotated to improve print speed.

- **Horizontal Strength (X-Y):** Low
- **Vertical Strength (Z):** Low
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Material/Time (Higher better):** Normal-High

![infill-top-line](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-line.png?raw=true)

### Grid

Two-layer pattern of perpendicular lines, forming a grid. Overlapping points may cause noise or artifacts.

- **Horizontal Strength (X-Y):** High
- **Vertical Strength (Z):** High
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Material/Time (Higher better):** Normal

![infill-top-grid](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-grid.png?raw=true)

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

### Cubic

3D cube pattern with corners facing down, distributing force in all directions. Triangles in the horizontal plane provide good X-Y strength.

- **Horizontal Strength (X-Y):** High
- **Vertical Strength (Z):** High
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Material/Time (Higher better):** Normal-High

![infill-top-cubic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-cubic.png?raw=true)

### Adaptive Cubic

[Cubic](#cubic) pattern with adaptive density: denser near walls, sparser in the center. Saves material and time while maintaining strength, ideal for large prints.

- **Horizontal Strength (X-Y):** Normal-High
- **Vertical Strength (Z):** Normal-High
- **Density Calculation:** Same as [Cubic](#cubic) but reduced in the center
- **Material Usage:** Low
- **Print Time:** Low
- **Material/Time (Higher better):** Normal

![infill-top-adaptive-cubic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-adaptive-cubic.png?raw=true)

### Quarter Cubic

[Cubic](#cubic) pattern with extra internal divisions, improving X-Y strength.

- **Horizontal Strength (X-Y):** High
- **Vertical Strength (Z):** High
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Material/Time (Higher better):** Normal

![infill-top-quarter-cubic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-quarter-cubic.png?raw=true)

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
- **Material/Time (Higher better):** Low

![infill-top-lightning](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-lightning.png?raw=true)

### Honeycomb

Hexagonal pattern balancing strength and material use. Double walls in each hexagon increase material consumption.

- **Horizontal Strength (X-Y):** High
- **Vertical Strength (Z):** High
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** High
- **Print Time:** Ultra-High
- **Material/Time (Higher better):** Low

![infill-top-honeycomb](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-honeycomb.png?raw=true)

### 3D Honeycomb

This infill tries to generate a printable honeycomb structure by printing squares and octagons maintaining a vertical angle high enough to maintain contact with the previous layer.

- **Horizontal Strength (X-Y):** Normal-High
- **Vertical Strength (Z):** Normal-High
- **Density Calculation:** Unknown
- **Material Usage:** Normal-Low
- **Print Time:** High
- **Material/Time (Higher better):** Low

![infill-top-3d-honeycomb](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-3d-honeycomb.png?raw=true)

### 2D Honeycomb

Vertical Honeycomb pattern. Acceptable torsional stiffness. Developed for low densities structures like wings. Improve over [2D Lattice](#2d-lattice) offers same performance with lower densities.This infill includes a Overhang angle parameter to improve the point of contact between layers and reduce the risk of delamination.

- **Horizontal Strength (X-Y):** Normal-Low
- **Vertical Strength (Z):** Normal-Low
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Material/Time (Higher better):** Normal

![infill-top-2d-honeycomb](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-2d-honeycomb.png?raw=true)

### 2D Lattice

Low-strength pattern with good flexibility. You can adjust **Angle 1** and **Angle 2** to optimize the infill for your specific model. Each angle adjusts the plane of each layer generated by the pattern. 0° is vertical.

- **Horizontal Strength (X-Y):** Normal-Low
- **Vertical Strength (Z):** Low
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Material/Time (Higher better):** Normal

![infill-top-2d-lattice](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-2d-lattice.png?raw=true)

### Cross Hatch

Similar to [Gyroid](#gyroid) but with linear patterns, creating weak points at internal corners.
Easier to slice but consider using [TPMS-D](#tpms-d) or [Gyroid](#gyroid) for better strength and flexibility.

- **Horizontal Strength (X-Y):** Normal-High
- **Vertical Strength (Z):** Normal-High
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-High
- **Material/Time (Higher better):** Normal-Low

![infill-top-cross-hatch](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-cross-hatch.png?raw=true)

### TPMS-D

Triply Periodic Minimal Surface (Schwarz Diamond). Hybrid between [Cross Hatch](#cross-hatch) and [Gyroid](#gyroid), combining rigidity and smooth transitions. Isotropic and strong in all directions. This geometry is faster to slice than Gyroid, but slower than Cross Hatch.

- **Horizontal Strength (X-Y):** High
- **Vertical Strength (Z):** High
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** High
- **Material/Time (Higher better):** Normal-Low

![infill-top-tpms-d](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-tpms-d.png?raw=true)

### Gyroid

Mathematical, isotropic surface providing equal strength in all directions. Excellent for strong, flexible prints and resin filling due to its interconnected structure. This pattern may require more time to slice because of all the points needed to generate each curve. If your model has complex geometry, consider using a simpler infill pattern like [TPMS-D](#tpms-d) or [Cross Hatch](#cross-hatch).

- **Horizontal Strength (X-Y):** High
- **Vertical Strength (Z):** High
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-High
- **Material/Time (Higher better):** Normal-Low

![infill-top-gyroid](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-gyroid.png?raw=true)

### Concentric

Fills the area with progressively smaller versions of the outer contour, creating a concentric pattern. Ideal for 100% infill or flexible prints.

- **Horizontal Strength (X-Y):** Low
- **Vertical Strength (Z):** Normal
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal
- **Material/Time (Higher better):** Normal-High

![infill-top-concentric](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-concentric.png?raw=true)

### Hilbert Curve

Hilbert Curve is a space-filling curve that can be used to create a continuous infill pattern. It is known for its Esthetic appeal and ability to fill space efficiently.
Print speed is very low due to the complexity of the path, which can lead to longer print times. It is not recommended for structural parts but can be used for Esthetic purposes.

- **Horizontal Strength (X-Y):** Low
- **Vertical Strength (Z):** Normal
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** High
- **Material/Time (Higher better):** Low

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
- **Print Time:** Normal
- **Material/Time (Higher better):** Normal-Low

![infill-top-octagram-spiral](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-octagram-spiral.png?raw=true)
