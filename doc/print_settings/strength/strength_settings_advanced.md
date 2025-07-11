# Strength Advanced

- [Bridge infill direction](#bridge-infill-direction)
- [Minimum sparse infill threshold](#minimum-sparse-infill-threshold)
- [Infill Combination](#infill-combination)
  - [Max layer height](#max-layer-height)
- [Detect narrow internal solid infill](#detect-narrow-internal-solid-infill)
- [Ensure vertical shell thickness](#ensure-vertical-shell-thickness)

## Bridge infill direction

Bridging angle override. If left to zero, the bridging angle will be calculated automatically. Otherwise the provided angle will be used for bridges. Use 180° for zero angle.

## Minimum sparse infill threshold

Sparse infill area which is smaller than threshold value is replaced by internal solid infill.

## Infill Combination

Automatically Combine sparse infill of several layers to print together to reduce time. Wall is still printed with original layer height.

### Max layer height

Maximum layer height for the combined sparse infill.  
Set it to 0 or 100% to use the nozzle diameter (for maximum reduction in print time) or a value of ~80% to maximize sparse infill strength.

The number of layers over which infill is combined is derived by dividing this value with the layer height and rounded down to the nearest decimal.

Use either absolute mm values (eg. 0.32mm for a 0.4mm nozzle) or % values (eg 80%). This value must not be larger than the nozzle diameter.

## Detect narrow internal solid infill

This option will auto-detect narrow internal solid infill areas. If enabled, the concentric pattern will be used for the area to speed up printing. Otherwise, the rectilinear pattern will be used by default.

## Ensure vertical shell thickness

Add solid infill near sloping surfaces to guarantee the vertical shell thickness (top+bottom solid layers).

- **None**: No solid infill will be added anywhere. Caution: Use this option carefully if your model has sloped surfaces.
- **Critical Only**: Avoid adding solid infill for walls.
- **Moderate**: Add solid infill for heavily sloping surfaces only.
- **All (default)**: Add solid infill for all suitable sloping surfaces.
