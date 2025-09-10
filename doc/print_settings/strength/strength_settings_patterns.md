# Patterns

Patterns determine how material is distributed within a print. Different patterns can affect strength, flexibility and print speed using the same density setting.  
The infill pattern also impacts the uniformity of the layer times, since the patterns may be constant, or present significant variations between adjacent layers.

There is no one-size-fits-all solution, as the best pattern depends on the specific print and its requirements.

Many patterns may look similar and have similar overall specifications, but they can behave very differently in practice.  
As most settings in 3D printing, experience is the best way to determine which pattern works best for your specific needs.

## Patterns Quick Reference

| | Pattern | Applies to | X-Y Strength | Z Strength | Material Usage | Print Time |
|---|---|---|---|---|---|---|
| ![param_monotonic](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_monotonic.svg?raw=true) | [Monotonic](#monotonic) | - **[Solid Infill](strength_settings_infill#internal-solid-infill)** - **[Surface](strength_settings_top_bottom_shells)** | Normal | Normal | Normal-High | Normal-Low |
| ![param_monotonicline](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_monotonicline.svg?raw=true) | [Monotonic line](#monotonic-line) | - **[Solid Infill](strength_settings_infill#internal-solid-infill)** - **[Surface](strength_settings_top_bottom_shells)** | Normal | Normal | Normal | Normal |
| ![param_rectilinear](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_rectilinear.svg?raw=true) | [Rectilinear](#rectilinear) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** - **[Solid Infill](strength_settings_infill#internal-solid-infill)** - **[Surface](strength_settings_top_bottom_shells)** - **[Ironing](quality_settings_ironing)** | Normal-Low | Low | Normal | Normal-Low |
| ![param_alignedrectilinear](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_alignedrectilinear.svg?raw=true) | [Aligned Rectilinear](#aligned-rectilinear) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** - **[Solid Infill](strength_settings_infill#internal-solid-infill)** - **[Surface](strength_settings_top_bottom_shells)** | Normal-Low | Normal | Normal | Normal-Low |
| ![param_zigzag](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_zigzag.svg?raw=true) | [Zig Zag](#zig-zag) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** | Normal-Low | Low | Normal | Normal-Low |
| ![param_crosszag](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_crosszag.svg?raw=true) | [Cross Zag](#cross-zag) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** | Normal | Low | Normal | Normal-Low |
| ![param_lockedzag](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_lockedzag.svg?raw=true) | [Locked Zag](#locked-zag) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** | Normal-Low | Normal-Low | Low | Extra-High |
| ![param_line](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_line.svg?raw=true) | [Line](#line) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** | Low | Low | Normal-High | Normal-Low |
| ![param_grid](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_grid.svg?raw=true) | [Grid](#grid) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** | High | High | Normal-High | Normal-Low |
| ![param_triangles](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_triangles.svg?raw=true) | [Triangles](#triangles) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** | High | Normal | Normal-High | Normal-Low |
| ![param_tri-hexagon](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_tri-hexagon.svg?raw=true) | [Tri-hexagon](#tri-hexagon) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** | High | Normal-High | Normal-High | Normal-Low |
| ![param_cubic](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_cubic.svg?raw=true) | [Cubic](#cubic) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** | High | High | Normal-High | Normal-Low |
| ![param_adaptivecubic](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_adaptivecubic.svg?raw=true) | [Adaptive Cubic](#adaptive-cubic) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** | Normal-High | Normal-High | Normal | Low |
| ![param_quartercubic](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_quartercubic.svg?raw=true) | [Quarter Cubic](#quarter-cubic) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** | High | High | Normal-High | Normal-Low |
| ![param_supportcubic](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_supportcubic.svg?raw=true) | [Support Cubic](#support-cubic) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** | Low | Low | Normal | Extra-Low |
| ![param_lightning](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_lightning.svg?raw=true) | [Lightning](#lightning) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** | Low | Low | Low | Ultra-Low |
| ![param_honeycomb](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_honeycomb.svg?raw=true) | [Honeycomb](#honeycomb) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** | High | High | Low | Ultra-High |
| ![param_3dhoneycomb](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_3dhoneycomb.svg?raw=true) | [3D Honeycomb](#3d-honeycomb) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** | Normal-High | Normal-High | Low | High |
| ![param_lateral-honeycomb](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_lateral-honeycomb.svg?raw=true) | [Lateral Honeycomb](#lateral-honeycomb) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** | Normal-Low | Normal-Low | Normal-High | Normal-Low |
| ![param_lateral-lattice](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_lateral-lattice.svg?raw=true) | [Lateral Lattice](#lateral-lattice) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** | Normal-Low | Low | Normal-High | Normal-Low |
| ![param_crosshatch](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_crosshatch.svg?raw=true) | [Cross Hatch](#cross-hatch) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** | Normal-High | Normal-High | Normal-Low | Normal-High |
| ![param_tpmsd](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_tpmsd.svg?raw=true) | [TPMS-D](#tpms-d) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** | High | High | Normal-Low | High |
| ![param_tpmsfk](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_tpmsfk.svg?raw=true) | [TPMS-FK](#tpms-fk) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** | Normal-High | Normal-High | Low | High |
| ![param_gyroid](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_gyroid.svg?raw=true) | [Gyroid](#gyroid) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** | High | High | Normal-Low | Normal-High |
| ![param_concentric](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_concentric.svg?raw=true) | [Concentric](#concentric) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** - **[Solid Infill](strength_settings_infill#internal-solid-infill)** - **[Surface](strength_settings_top_bottom_shells)** - **[Ironing](quality_settings_ironing)** | Low | Normal | Normal-High | Normal-Low |
| ![param_hilbertcurve](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_hilbertcurve.svg?raw=true) | [Hilbert Curve](#hilbert-curve) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** - **[Solid Infill](strength_settings_infill#internal-solid-infill)** - **[Surface](strength_settings_top_bottom_shells)** | Low | Normal | Low | High |
| ![param_archimedeanchords](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_archimedeanchords.svg?raw=true) | [Archimedean Chords](#archimedean-chords) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** - **[Solid Infill](strength_settings_infill#internal-solid-infill)** - **[Surface](strength_settings_top_bottom_shells)** | Low | Normal | Normal-High | Normal-Low |
| ![param_octagramspiral](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_octagramspiral.svg?raw=true) | [Octagram Spiral](#octagram-spiral) | - **[Sparse Infill](strength_settings_infill#sparse-infill-density)** - **[Solid Infill](strength_settings_infill#internal-solid-infill)** - **[Surface](strength_settings_top_bottom_shells)** | Low | Normal | Normal-Low | Normal |

> [!NOTE]
> You can download [infill_desc_calculator.xlsx](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/print_settings/strength/infill_desc_calculator.xlsx?raw=true) used to calculate the values above.

## Monotonic

[Rectilinear](#rectilinear) in a uniform direction for a smoother visual surface.

- **Horizontal Strength (X-Y):** Normal
- **Vertical Strength (Z):** Normal
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Layer time Variability:** Not noticeable
- **Material/Time (Higher better):** Normal-High
- **Applies to:**
  - **[Solid Infill](strength_settings_infill#internal-solid-infill)**
  - **[Surface](strength_settings_top_bottom_shells)**

![infill-top-monotonic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-monotonic.png?raw=true)

## Monotonic line

[Monotonic](#monotonic) but avoids overlapping with the perimeter, reducing excess material at joints. May introduce visible seams and increase print time.

- **Horizontal Strength (X-Y):** Normal
- **Vertical Strength (Z):** Normal
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal
- **Layer time Variability:** None
- **Material/Time (Higher better):** Normal
- **Applies to:**
  - **[Solid Infill](strength_settings_infill#internal-solid-infill)**
  - **[Surface](strength_settings_top_bottom_shells)**

![infill-top-monotonic-line](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-monotonic-line.png?raw=true)

## Rectilinear

Parallel lines spaced according to infill density. Each layer is printed perpendicular to the previous, resulting in low vertical bonding. Consider using new [Zig Zag](#zig-zag) infill instead.

- **Horizontal Strength (X-Y):** Normal-Low
- **Vertical Strength (Z):** Low
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Layer time Variability:** Not noticeable
- **Material/Time (Higher better):** Normal
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**  - **[Solid Infill](strength_settings_infill#internal-solid-infill)**
  - **[Surface](strength_settings_top_bottom_shells)**
  - **[Ironing](quality_settings_ironing)**

![infill-top-rectilinear](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-rectilinear.png?raw=true)

## Aligned Rectilinear

Parallel lines spaced by the infill spacing, each layer printed in the same direction as the previous layer. Good horizontal strength perpendicular to the lines, but terrible in parallel direction.
Recommended with layer anchoring to improve not perpendicular strength.

- **Horizontal Strength (X-Y):** Normal-Low
- **Vertical Strength (Z):** Normal
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Layer time Variability:** Not noticeable
- **Material/Time (Higher better):** Normal
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**  - **[Solid Infill](strength_settings_infill#internal-solid-infill)**
  - **[Surface](strength_settings_top_bottom_shells)**

![infill-top-aligned-rectilinear](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-aligned-rectilinear.png?raw=true)

## Zig Zag

Similar to [rectilinear](#rectilinear) with consistent pattern between layers. Allows you to add a Symmetric infill Y axis for models with two symmetric parts.

- **Horizontal Strength (X-Y):** Normal-Low
- **Vertical Strength (Z):** Low
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Layer time Variability:** Not noticeable
- **Material/Time (Higher better):** Normal
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**

![infill-top-zig-zag](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-zig-zag.png?raw=true)

## Cross Zag

Similar to [Zig Zag](#zig-zag) but displacing each layer with Infill shift step parameter.

- **Horizontal Strength (X-Y):** Normal
- **Vertical Strength (Z):** Low
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Layer time Variability:** Not noticeable
- **Material/Time (Higher better):** Normal
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**

![infill-top-cross-zag](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-cross-zag.png?raw=true)

## Locked Zag

Adaptive version of [Zig Zag](#zig-zag) adding an external skin texture to interlock layers and a low material skeleton.

- **Horizontal Strength (X-Y):** Normal-Low
- **Vertical Strength (Z):** Normal-Low
- **Density Calculation:** Same as [Zig Zag](#zig-zag) but increasing near walls
- **Material Usage:** Normal-High
- **Print Time:** Extra-High
- **Layer time Variability:** Not noticeable
- **Material/Time (Higher better):** Low
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**

![infill-top-locked-zag](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-locked-zag.png?raw=true)

## Line

Similar to [rectilinear](#rectilinear), but each line is slightly rotated to improve print speed.

- **Horizontal Strength (X-Y):** Low
- **Vertical Strength (Z):** Low
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Layer time Variability:** None
- **Material/Time (Higher better):** Normal-High
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**

![infill-top-line](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-line.png?raw=true)

## Grid

Two-layer pattern of perpendicular lines, forming a grid. Overlapping points may cause noise or artifacts.

- **Horizontal Strength (X-Y):** High
- **Vertical Strength (Z):** High
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Layer time Variability:** Not noticeable
- **Material/Time (Higher better):** Normal-High
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**

![infill-top-grid](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-grid.png?raw=true)

## Triangles

Triangle-based grid, offering strong X-Y strength but with triple overlaps at intersections.

- **Horizontal Strength (X-Y):** High
- **Vertical Strength (Z):** Normal
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Layer time Variability:** None
- **Material/Time (Higher better):** Normal-High
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**

![infill-top-triangles](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-triangles.png?raw=true)

## Tri-hexagon

Similar to the [triangles](#triangles) pattern but offset to prevent triple overlaps at intersections. This design combines triangles and hexagons, providing excellent X-Y strength.

- **Horizontal Strength (X-Y):** High
- **Vertical Strength (Z):** Normal-High
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Layer time Variability:** None
- **Material/Time (Higher better):** Normal-High
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**

![infill-top-tri-hexagon](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-tri-hexagon.png?raw=true)

## Cubic

3D cube pattern with corners facing down, distributing force in all directions. Triangles in the horizontal plane provide good X-Y strength.

- **Horizontal Strength (X-Y):** High
- **Vertical Strength (Z):** High
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Layer time Variability:** Not noticeable
- **Material/Time (Higher better):** Normal-High
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**

![infill-top-cubic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-cubic.png?raw=true)

## Adaptive Cubic

[Cubic](#cubic) pattern with adaptive density: denser near walls, sparser in the center. Saves material and time while maintaining strength, ideal for large prints.

- **Horizontal Strength (X-Y):** Normal-High
- **Vertical Strength (Z):** Normal-High
- **Density Calculation:** Same as [Cubic](#cubic) but reduced in the center
- **Material Usage:** Low
- **Print Time:** Low
- **Layer time Variability:** Not noticeable
- **Material/Time (Higher better):** Normal
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**

![infill-top-adaptive-cubic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-adaptive-cubic.png?raw=true)

## Quarter Cubic

[Cubic](#cubic) pattern with extra internal divisions, improving X-Y strength.

- **Horizontal Strength (X-Y):** High
- **Vertical Strength (Z):** High
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Layer time Variability:** Not noticeable
- **Material/Time (Higher better):** Normal-High
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**

![infill-top-quarter-cubic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-quarter-cubic.png?raw=true)

## Support Cubic

Support |Cubic is a variation of the [Cubic](#cubic) infill pattern that is specifically designed for support top layers. Will use more material than Lightning infill but will provide better strength. Nevertheless, it is still a low-density infill pattern.

- **Horizontal Strength (X-Y):** Low
- **Vertical Strength (Z):** Low
- **Density Calculation:** % of layer before top shell layers
- **Material Usage:** Extra-Low
- **Print Time:** Extra-Low
- **Layer time Variability:** Highly noticeable
- **Material/Time (Higher better):** Normal
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**

![infill-top-support-cubic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-support-cubic.png?raw=true)

## Lightning

Ultra-fast, ultra-low material infill. Designed for speed and efficiency, ideal for quick prints or non-structural prototypes.

- **Horizontal Strength (X-Y):** Low
- **Vertical Strength (Z):** Low
- **Density Calculation:** % of layer before top shell layers
- **Material Usage:** Ultra-Low
- **Print Time:** Ultra-Low
- **Layer time Variability:** Highly noticeable
- **Material/Time (Higher better):** Low
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**

![infill-top-lightning](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-lightning.png?raw=true)

## Honeycomb

Hexagonal pattern balancing strength and material use. Double walls in each hexagon increase material consumption.

- **Horizontal Strength (X-Y):** High
- **Vertical Strength (Z):** High
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** High
- **Print Time:** Ultra-High
- **Layer time Variability:** None
- **Material/Time (Higher better):** Low
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**

![infill-top-honeycomb](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-honeycomb.png?raw=true)

## 3D Honeycomb

This infill tries to generate a printable honeycomb structure by printing squares and octagons maintaining a vertical angle high enough to maintain contact with the previous layer.

- **Horizontal Strength (X-Y):** Normal-High
- **Vertical Strength (Z):** Normal-High
- **Density Calculation:** Unknown
- **Material Usage:** Normal-Low
- **Print Time:** High
- **Layer time Variability:** Noticeable
- **Material/Time (Higher better):** Low
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**

![infill-top-3d-honeycomb](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-3d-honeycomb.png?raw=true)

## Lateral Honeycomb

Vertical Honeycomb pattern. Acceptable torsional stiffness. Developed for low densities structures like wings. Improve over [Lateral Lattice](#lateral-lattice) offers same performance with lower densities.This infill includes a Overhang angle parameter to improve the point of contact between layers and reduce the risk of delamination.

- **Horizontal Strength (X-Y):** Normal-Low
- **Vertical Strength (Z):** Normal-Low
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Layer time Variability:** Noticeable
- **Material/Time (Higher better):** Normal-High
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**

![infill-top-lateral-honeycomb](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-lateral-honeycomb.png?raw=true)

## Lateral Lattice

Low-strength pattern with good flexibility. You can adjust **Angle 1** and **Angle 2** to optimize the infill for your specific model. Each angle adjusts the plane of each layer generated by the pattern. 0° is vertical.

- **Horizontal Strength (X-Y):** Normal-Low
- **Vertical Strength (Z):** Low
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Layer time Variability:** Not noticeable
- **Material/Time (Higher better):** Normal-High
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**

![infill-top-lateral-lattice](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-lateral-lattice.png?raw=true)

## Cross Hatch

Similar to [Gyroid](#gyroid) but with linear patterns, creating weak points at internal corners.
Easier to slice but consider using [TPMS-D](#tpms-d) or [Gyroid](#gyroid) for better strength and flexibility.

- **Horizontal Strength (X-Y):** Normal-High
- **Vertical Strength (Z):** Normal-High
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-High
- **Layer time Variability:** Highly noticeable
- **Material/Time (Higher better):** Normal-Low
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**

![infill-top-cross-hatch](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-cross-hatch.png?raw=true)

## TPMS-D

Triply Periodic Minimal Surface (Schwarz Diamond). Hybrid between [Cross Hatch](#cross-hatch) and [Gyroid](#gyroid), combining rigidity and smooth transitions. Isotropic and strong in all directions. This geometry is faster to slice than Gyroid, but slower than Cross Hatch.

- **Horizontal Strength (X-Y):** High
- **Vertical Strength (Z):** High
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** High
- **Layer time Variability:** Highly noticeable
- **Material/Time (Higher better):** Normal-Low
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**

![infill-top-tpms-d](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-tpms-d.png?raw=true)

## TPMS-FK

Triply Periodic Minimal Surface (Fischer–Koch S) pattern. Its smooth, continuous geometry resembles trabecular bone microstructure, offering a balance between rigidity and energy absorption. Compared to [TPMS-D](#tpms-d), it has more complex curvature, which can improve load distribution and shock absorption in functional parts.

- **Horizontal Strength (X-Y):** Normal-High
- **Vertical Strength (Z):** Normal-High
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** High
- **Layer time Variability:** Noticeable
- **Material/Time (Higher better):** Low
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**

![infill-top-tpms-fk](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-tpms-fk.png?raw=true)

## Gyroid

Mathematical, isotropic surface providing equal strength in all directions. Excellent for strong, flexible prints and resin filling due to its interconnected structure. This pattern may require more time to slice because of all the points needed to generate each curve. If your model has complex geometry, consider using a simpler infill pattern like [TPMS-D](#tpms-d) or [Cross Hatch](#cross-hatch).

- **Horizontal Strength (X-Y):** High
- **Vertical Strength (Z):** High
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-High
- **Layer time Variability:** Highly noticeable
- **Material/Time (Higher better):** Normal-Low
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**

![infill-top-gyroid](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-gyroid.png?raw=true)

## Concentric

Fills the area with progressively smaller versions of the outer contour, creating a concentric pattern. Ideal for 100% infill or flexible prints.

- **Horizontal Strength (X-Y):** Low
- **Vertical Strength (Z):** Normal
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Layer time Variability:** None
- **Material/Time (Higher better):** Normal-High
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**  - **[Solid Infill](strength_settings_infill#internal-solid-infill)**
  - **[Surface](strength_settings_top_bottom_shells)**
  - **[Ironing](quality_settings_ironing)**

![infill-top-concentric](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-concentric.png?raw=true)

## Hilbert Curve

Hilbert Curve is a space-filling curve that can be used to create a continuous infill pattern. It is known for its aesthetic appeal and ability to fill space efficiently.
Print speed is very low due to the complexity of the path, which can lead to longer print times. It is not recommended for structural parts but can be used for aesthetic purposes.

- **Horizontal Strength (X-Y):** Low
- **Vertical Strength (Z):** Normal
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** High
- **Layer time Variability:** None
- **Material/Time (Higher better):** Low
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**  - **[Solid Infill](strength_settings_infill#internal-solid-infill)**
  - **[Surface](strength_settings_top_bottom_shells)**

![infill-top-hilbert-curve](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-hilbert-curve.png?raw=true)

## Archimedean Chords

Spiral pattern that fills the area with concentric arcs, creating a smooth and continuous infill. Can be filled with resin thanks to its interconnected hollow structure, which allows the resin to flow through it and cure properly.

- **Horizontal Strength (X-Y):** Low
- **Vertical Strength (Z):** Normal
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal-Low
- **Layer time Variability:** Not noticeable
- **Material/Time (Higher better):** Normal-High
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**  - **[Solid Infill](strength_settings_infill#internal-solid-infill)**
  - **[Surface](strength_settings_top_bottom_shells)**

![infill-top-archimedean-chords](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-archimedean-chords.png?raw=true)

## Octagram Spiral

Aesthetic pattern with low strength and high print time.

- **Horizontal Strength (X-Y):** Low
- **Vertical Strength (Z):** Normal
- **Density Calculation:**  % of  total infill volume
- **Material Usage:** Normal
- **Print Time:** Normal
- **Layer time Variability:** None
- **Material/Time (Higher better):** Normal-Low
- **Applies to:**
  - **[Sparse Infill](strength_settings_infill#sparse-infill-density)**  - **[Solid Infill](strength_settings_infill#internal-solid-infill)**
  - **[Surface](strength_settings_top_bottom_shells)**

![infill-top-octagram-spiral](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/fill/infill-top-octagram-spiral.png?raw=true)
