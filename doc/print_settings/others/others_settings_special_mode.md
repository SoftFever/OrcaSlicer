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
