# Special Mode

- [Slicing Mode](#slicing-mode)
  - [Regular](#regular)
  - [Close Holes](#close-holes)
  - [Even Odd](#even-odd)
- [Print Sequence](#print-sequence)
  - [By Layer](#by-layer)
    - [Intra-layer order](#intra-layer-order)
  - [By Object](#by-object)
- [Spiral vase](#spiral-vase)
  - [Smooth Spiral](#smooth-spiral)
    - [Max XY Smoothing](#max-xy-smoothing)
  - [Spiral starting flow ratio](#spiral-starting-flow-ratio)
  - [Spiral finishing flow ratio](#spiral-finishing-flow-ratio)
- [Timelapse](#timelapse)
- [Fuzzy Skin](#fuzzy-skin)
  - [Fuzzy Skin Mode](#fuzzy-skin-mode)
    - [Contour](#contour)
    - [Contour and Hole](#contour-and-hole)
    - [All Walls](#all-walls)
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

## Slicing Mode

The slicing mode determines how the model is sliced into layers and how the G-code is generated. Different modes can be used to achieve various printing effects or to optimize the print process.

### Regular

This is the default slicing mode. It slices the model layer by layer, generating G-code for each layer.

### Close Holes

Use "Close holes" to close all holes in the model.

### Even Odd

Use "Even-odd" for 3DLabPrint airplane models.

## Print Sequence

How multiple objects are printed in a single print job.

### By Layer

This option prints all objects layer by layer, one layer at a time.

#### Intra-layer order

Print order within a single layer.

- **Default**: Prints objects based on their position in the bed and travel distance.
- **As object list**: Prints objects in the order they are listed in the object list.

### By Object

This option prints each object completely before moving on to the next object.

## Spiral vase

Spiralize smooths out the z moves of the outer contour. And turns a solid model into a single walled print with solid bottom layers. The final generated model has no seam.

### Smooth Spiral

Smooth Spiral smooths out X and Y moves as well, resulting in no visible seam at all, even in the XY directions on walls that are not vertical.

#### Max XY Smoothing

Maximum distance to move points in XY to try to achieve a smooth spiral. If expressed as a %, it will be computed over nozzle diameter.

### Spiral starting flow ratio

Sets the starting flow ratio while transitioning from the last bottom layer to the spiral. Normally the spiral transition scales the flow ratio from 0% to 100% during the first loop which can in some cases lead to under extrusion at the start of the spiral.

### Spiral finishing flow ratio

Sets the finishing flow ratio while ending the spiral. Normally the spiral transition scales the flow ratio from 100% to 0% during the last loop which can in some cases lead to under extrusion at the end of the spiral.

## Timelapse

WIP...

## Fuzzy Skin

Randomly jitter while printing the wall, so that the surface has a rough look. This setting controls the fuzzy position.

### Fuzzy Skin Mode

#### Contour

Use "Contour" to apply fuzzy skin only to the outer contour of the model.

#### Contour and Hole

Use "Contour and Hole" to apply fuzzy skin to the outer contour and holes of the model. This is useful for models with internal features that you want to highlight.

#### All Walls

Use "All Walls" to apply fuzzy skin to external and inner walls of the model.

### Fuzzy Skin Generator Mode

Determines how the fuzzy skin effect will be reproduced:

#### Displacement

The classic method is when the pattern on the walls is achieved by shifting the printhead perpendicular to the wall. It gives a predictable result, but decreases the strength entire shells and open the pores inside the walls. It also increases the mechanical stress on the kinematics of the printer. The speed of general printing is slowing down.

<img width="160" height="160" alt="image" align="bottom" src="https://github.com/user-attachments/assets/a9d75ded-ad79-4f0d-b0b4-2531c746f775" />

#### Extrusion

The fuzzy skin condition is obtained by changing the amount of extruded plastic as the print head moves linearly. There is no extra load on the kinematics, there is no decrease in the printing speed, the pores do not open, but the drawing turns out to be smoother by a factor of 2. It is suitable for creating "loose" walls to reduce internal stress into extruded plastic, or masking printing defects on the side walls - a matte effect.

> [!CAUTION]
> The "Fuzzy skin thicknesses" parameter cannot be more than about 70%-125% (selected individually for different conditions) of the nozzle diameter! This is a complex condition that also depends on the height of the layer, and determines how thin the lines can be extruded.
> Arachine wall generator mode should also be enabled.

<img width="160" height="160" alt="image" align="bottom" src="https://github.com/user-attachments/assets/b75b848e-f2fd-44cc-a4c4-e40ea717a97f" />

#### Combined

This is a combination of Displacement and Extrusion modes. The clarity of the drawing is the same in the classic mode, but the walls remain strong and tight. The load on the kinematics is 2 times lower. The printing speed is faster than in Displacement mode, but the elapsed time will still be longer.

> [!WARNING]
> The limits on line thickness are the same as in the Extrusion mode.

<img width="160" height="160" alt="image" align="bottom" src="https://github.com/user-attachments/assets/7a0c9a7a-cdda-49b9-b074-0e17a0641b33" />

### Noise Type

Noise type to use for fuzzy skin generation.

#### Classic

Classic uniform random noise.

#### Perlin

Perlin noise, which gives a more consistent texture.

#### Billow

Billow noise is similar to Perlin noise, but has a clumpier appearance. It can create more pronounced features and is often used for natural textures.

#### Ridged Multifractal

Ridged noise with sharp, jagged features. Creates marble-like textures.

#### Voronoi

Voronoi noise divides the surface into voronoi cells, and displaces each one by a random amount. Creates a patchwork texture.

### Point distance

average distance between the random points introduced on each line segment.

### Skin thickness

The width within which to jitter. It's advised to be below outer wall line width."

### Skin feature size

The base size of the coherent noise features, in mm. Higher values will result in larger features.

### Skin Noise Octaves

The number of octaves of coherent noise to use. Higher values increase the detail of the noise, but also increase computation time.

### Skin Noise Persistence

The decay rate for higher octaves of the coherent noise. Lower values will result in smoother noise.

### Apply fuzzy skin to first layer

Whether to apply fuzzy skin on the first layer.
