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

## Fuzzy Skin Mode

### Contour

Use "Contour" to apply fuzzy skin only to the outer contour of the model.

### Contour and Hole

Use "Contour and Hole" to apply fuzzy skin to the outer contour and holes of the model. This is useful for models with internal features that you want to highlight.

### All Walls

Use "All Walls" to apply fuzzy skin to external and inner walls of the model.

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
