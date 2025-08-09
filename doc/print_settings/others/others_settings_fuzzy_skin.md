# Fuzzy Skin

Randomly jitter while printing the wall, so that the surface has a rough look. This setting controls the fuzzy position.

- [Fuzzy Skin Mode](#fuzzy-skin-mode)
  - [Contour](#contour)
  - [Contour and Hole](#contour-and-hole)
  - [All Walls](#all-walls)
  - [Fuzzy Skin Generator Mode](#fuzzy-skin-generator-mode)
  - [Displacement](#displacement)
  - [Extrusion](#extrusion)
  - [Combined](#combined)
- [Noise Type](#noise-type)
  - [Classic](#classic)
  - [Perlin](#perlin)
  - [Billow](#billow)
  - [Ridged Multifractal](#ridged-multifractal)
  - [Voronoi](#voronoi)
- [Point distance](#point-distance)
- [Skin thickness](#skin-thickness)
- [Skin feature size](#skin-feature-size)
- [Skin Noise Octaves](#skin-noise-octaves)
- [Skin Noise Persistence](#skin-noise-persistence)
- [Apply fuzzy skin to first layer](#apply-fuzzy-skin-to-first-layer)
- [Credits](#credits)

## Fuzzy Skin Mode

### Contour

Use "Contour" to apply fuzzy skin only to the outer contour of the model.

### Contour and Hole

Use "Contour and Hole" to apply fuzzy skin to the outer contour and holes of the model. This is useful for models with internal features that you want to highlight.

### All Walls

Use "All Walls" to apply fuzzy skin to external and inner walls of the model.

### Fuzzy Skin Generator Mode

Determines how the fuzzy skin effect will be reproduced:

### Displacement

![Fuzzy-skin-Displacement-mode](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/Fuzzy-skin/Fuzzy-skin-Displacement-mode.png?raw=true)

The classic method is when the pattern on the walls is achieved by shifting the printhead perpendicular to the wall.  
It gives a predictable result, but decreases the strength entire shells and open the pores inside the walls. It also increases the mechanical stress on the kinematics of the printer. The speed of general printing is slowing down.

### Extrusion

![Fuzzy-skin-Extrusion-mode](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/Fuzzy-skin/Fuzzy-skin-Extrusion-mode.png?raw=true)

The fuzzy skin condition is obtained by changing the amount of extruded plastic as the print head moves linearly. There is no extra load on the kinematics, there is no decrease in the printing speed, the pores do not open, but the drawing turns out to be smoother by a factor of 2. It is suitable for creating "loose" walls to reduce internal stress into extruded plastic, or masking printing defects on the side walls - a matte effect.

> [!CAUTION]
> The "Fuzzy skin thicknesses" parameter cannot be more than about 70%-125% (selected individually for different conditions) of the nozzle diameter! This is a complex condition that also depends on the height of the layer, and determines how thin the lines can be extruded.  
> [Arachne](quality_settings_wall_generator#arachne) wall generator mode should also be enabled.

### Combined

![Fuzzy-skin-Combined-mode](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/Fuzzy-skin/Fuzzy-skin-Combined-mode.png?raw=true)

This is a combination of Displacement and Extrusion modes. The clarity of the drawing is the same in the classic mode, but the walls remain strong and tight. The load on the kinematics is 2 times lower. The printing speed is faster than in Displacement mode, but the elapsed time will still be longer.

> [!WARNING]
> The limits on line thickness are the same as in the Extrusion mode.

## Noise Type

Noise type to use for fuzzy skin generation.

### Classic

Classic uniform random noise.

### Perlin

Perlin noise, which gives a more consistent texture.

### Billow

Billow noise is similar to Perlin noise, but has a clumpier appearance. It can create more pronounced features and is often used for natural textures.

### Ridged Multifractal

Ridged noise with sharp, jagged features. Creates marble-like textures.

### Voronoi

Voronoi noise divides the surface into voronoi cells, and displaces each one by a random amount. Creates a patchwork texture.

## Point distance

average distance between the random points introduced on each line segment.

## Skin thickness

The width within which to jitter. It's advised to be below outer wall line width."

## Skin feature size

The base size of the coherent noise features, in mm. Higher values will result in larger features.

## Skin Noise Octaves

The number of octaves of coherent noise to use. Higher values increase the detail of the noise, but also increase computation time.

## Skin Noise Persistence

The decay rate for higher octaves of the coherent noise. Lower values will result in smoother noise.

## Apply fuzzy skin to first layer

Whether to apply fuzzy skin on the first layer.

## Credits

- **Generator Mode author:** [@pi-squared-studio](https://github.com/pi-squared-studio).
