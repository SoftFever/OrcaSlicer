# Precision

TODO: WIP

## Slice gap closing radius

Cracks smaller than 2x gap closing radiusCracks smaller than 2x gap closing radius are being filled during the triangle mesh slicing. The gap closing operation may reduce the final print resolution, therefore it is advisable to keep the value reasonably low.

## Resolution

The G-code path is generated after simplifying the contour of models to avoid too many points and G-code lines. Smaller value means higher resolution and more time to slice.

## Arc fitting

Enable this to get a G-code file which has G2 and G3 moves. The fitting tolerance is same as the resolution.
Note: For Klipper machines, this option is recommended to be disabled.
Klipper does not benefit from arc commands as these are split again into line segments by the firmware. This results in a reduction in surface quality as line segments are converted to arcs by the slicer and then back to line segments by the firmware.

## X-Y Compensation


## Elephant foot compensation

## Precise wall

TODO: Traer de Precise-wall
TODO: BUSCAR REFERENCIAS AL ANTIGUO Y MODIFICARLO PARA ACÁ

## Precise Z height

TODO: Traer de precise-z-height
TODO: BUSCAR REFERENCIAS AL ANTIGUO Y MODIFICARLO PARA ACÁ

## Polyholes