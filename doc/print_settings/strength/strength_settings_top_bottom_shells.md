# Top and Bottom Shells

Controls how the top and bottom solid layers (shells) are generated in the print.

![top-bottom-shells](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/top-bottom-shells/top-bottom-shells.png?raw=true)

## Shells Layers

This is the number of solid layers of shell, including the surface layer. When the thickness calculated by this value is thinner than shell thickness, the shell layers will be increased.

## Shell Thickness

The number of solid layers is increased when slicing if the thickness calculated by shell layers is thinner than this value. This can avoid having too thin shell when layer height is small. 0 means that this setting is disabled and thickness of shell is absolutely determined by shell layers.

## Surface Density

This setting controls the density of the top and bottom surfaces. A value of 100% means a solid surface, while lower values create a sparse surface. This can be used for esthetic purposes, gripping or making interfaces with the bed.

## Infill/Wall Overlap

Top solid infill area is enlarged slightly to overlap with wall for better bonding and to minimize the appearance of pinholes where the infill meets the walls. A value of 25-30% is a good starting point, minimizing the appearance of pinholes. The percentage value is relative to line width of sparse infill.

## Surface Pattern

This setting controls the pattern of the surface. The options are:

- **[Concentric](strength_settings_infill#concentric):** Fills the surface with inward-tracing loops that follow the outer contour.
- **[Rectilinear](strength_settings_infill#rectilinear):** Fills the surface with straight lines alternating direction per layer.
- **Monotonic:** Prints lines in a uniform direction for a smoother visual surface.
- **Monotonic Lines:** Similar to Monotonic but avoids overlapping with the perimeter, reducing excess material at joints. May introduce visible seams.
- **[Aligned Rectilinear](strength_settings_infill#aligned-rectilinear):** The surface is printed with rectilinear lines that are aligned with the infill pattern.
- **[Hilbert Curve](strength_settings_infill#hilbert-curve):** Applies a space-filling curve for even material distribution and a unique appearance. Slow to print but useful in esthetic applications.
- **[Archimedean Chords](strength_settings_infill#archimedean-chords):** Generates concentric arc-like lines, promoting uniform material spread.
- **[Octagram Spiral](strength_settings_infill#octagram-spiral):** Creates an octagonal spiral for decorative, esthetic surfaces.

> [!NOTE]
> Some patterns may have further information in its [Sparse Infill Pattern Wiki](strength_settings_infill#sparse-infill-pattern)
