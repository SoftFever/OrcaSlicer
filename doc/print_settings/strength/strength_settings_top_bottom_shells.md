# Top and Bottom Shells

Controls how the top and bottom solid layers (shells) are generated.

![top-bottom-shells](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/top-bottom-shells/top-bottom-shells.png?raw=true)

## Shell Layers

This is the number of solid shell layers, including the surface layer.  
When the thickness calculated from this value is less than [shell thickness](#shell-thickness), the shell layers will be increased.

These layers are printed over the [sparse infill](strength_settings_infill), so increasing **shell layers** will increase overall part strength and top surface quality.
It's usually recommended to have at least 3 shell layers for most prints.

## Shell Thickness

The number of solid layers is increased during slicing if the thickness calculated from shell layers is thinner than this value. This avoids having too thin a shell when layer height is small.  
0 means this setting is disabled and shell thickness is determined entirely by [shell layers](#shell-layers).

## Surface Density

This setting controls the density of the top and bottom surfaces. A value of 100% means a solid surface, while lower values create a sparse surface.  
This can be used for aesthetic purposes, improving grip or creating interfaces.

## Patchwork surfaces

Patchwork surfaces are a pattern generation mode where you can break up the surface into individual tiles, similar to a patchwork quilt or brickwork.
You can choose which surface this patchwork pattern can be applied to:
- Nothere - the patchwork tiling off
- Bottom - only bottom surface will be modified
- Topmost - only topmost surface will be modified
- Topmost and bottom 
- All Upper - topmost and any upper surfaces
- Everywhere - topmost, any upper and bottom surfaces

### Patchwork direction

Sets the rotation of the patchwork on the surface.

### Alternate tiles direction

It sets an additional/alternate rotation angle of the tiles inside the masonry.
Keep in mind that the initial angle is set by the parameter _Solid Infill Direction_.
The _Alternate tiles direction_ field allows you to perform several tricks using various instructions:
- when you enter a unsigned number, such as '15', this angle will be added to the existing angle.
- when you enter a signed number, such as '+15' or '-15', each subsequent tile will rotate by that range
- when you enter the instruction '+0' or '360', each subsequent tile will rotate by a random angle

### Height and Width of tiles

Parameters that set the tile size. This size is defined in standard infill lines.
For all upper surfaces, it is calculated from the line width of _Top surface_, and for the bottom surface it calculated from the line width of _First layer_.

For example, if the line width is 0.4 mm, then 20 lines are 8 mm in the natural size.

### Width of the horizontal and vertical patchwork joint

This is the size of the intermediate padding between the tiles. It is measured in lines. When the value is positive, the gap is filled with solid lines. When the value is negative, the gap is formed but not filled with lines. A zero value can be used, in which case there is no gap.

### Joints flow ratio

If you want to create less filled seams according to the infill density.

### Patchwork centering

If this option is enabled, the tile will be placed in the center of the surface. Otherwise, the seam will be placed.

### Patchwork subway tiling

Option to overlap the adjacent row by half a tile.

## Center of surface pattern

Align assembly/model center to the top and bottom surfaces of polar patterns such as Octagram Spirals or Archimedean Chords. Need for aesthetic purpose.
There are 3 alignment options:
- Each Surface - each surface will contain its own center
- Each Model - the common center will be located in the geometric outline of all surfaces in the model
- Each Assemply - the common center will be located in the geometric contour of all independent models of one assembly

> [!NOTE]
> For patchwork, these options will apply to a portion of the tile, the entire tile, and the entire surface divided into tiles.

## Anisotropic surfaces

Anisotropic patterns on the top and bottom surfaces.
Co-directional printing mode will be applied. For certain patterns, omni-directional filling provides color dispersion when using multi-colored or silk plastics.

## Infill/Wall Overlap

The top solid infill area is slightly enlarged to overlap with walls for better bonding and to minimize pinholes where the infill meets the walls.  
A value of 25-30% is a good starting point. The percentage value is relative to the line width of the sparse infill.

> [!TIP]
> Check [Monotonic Line](strength_settings_patterns#monotonic-line) to learn about its overlaying differences with [Monotonic](strength_settings_patterns#monotonic) and [Rectilinear](strength_settings_patterns#rectilinear).

## Surface Pattern

This setting controls the pattern of the surfaces.  
If [Shell Layers](#shell-layers) is greater than 1, the surface pattern will be applied to the outermost shell layer only and the rest will use [Internal Solid Infill Pattern](strength_settings_infill#internal-solid-infill).

> [!TIP]
> See [Infill Patterns Wiki List](strength_settings_patterns) with **detailed specifications**, including their strengths and weaknesses.

 The surface patterns are:

- **[Concentric](strength_settings_patterns#concentric)**
- **[Rectilinear](strength_settings_patterns#rectilinear)**
- **[Monotonic](strength_settings_patterns#monotonic)**
- **[Monotonic Line](strength_settings_patterns#monotonic-line)** Usually Recommended for Top.
- **[Aligned Rectilinear](strength_settings_patterns#aligned-rectilinear)**
- **[Hilbert Curve](strength_settings_patterns#hilbert-curve)**
- **[Archimedean Chords](strength_settings_patterns#archimedean-chords)**
- **[Octagram Spiral](strength_settings_patterns#octagram-spiral)**
