# Bridging

"Bridging" is a 3D printing technique that allows you to print structures across gaps or voids without direct support underneath. OrcaSlicer provides several parameters to optimize bridge quality, minimizing filament sag and improving the appearance of top layers.

- [Flow ratio](#flow-ratio)
- [Bridge density](#bridge-density)
- [Thick bridges](#thick-bridges)
- [Extra bridge layers](#extra-bridge-layers)
- [Filter out small internal bridges](#filter-out-small-internal-bridges)
- [Bridge Counterbore hole](#bridge-counterbore-hole)

## Flow ratio

Decrease this value slightly (for example 0.9) to reduce the amount of material for bridge, to improve sag.

The actual bridge flow used is calculated by multiplying this value with the filament flow ratio, and if set, the object's flow ratio.

## Bridge density

This value governs the thickness of the bridge layer. This is the first layer over sparse infill. Decrease this value slightly (for example 0.9) to improve surface quality over sparse infill.

The actual internal bridge flow used is calculated by multiplying this value with the bridge flow ratio, the filament flow ratio, and if set, the object's flow ratio.

## Thick bridges

When enabled, thick bridges increase the reliability and strength of bridges, allowing you to span longer distances. However, this may result in a rougher surface finish. Disabling this option can improve the visual quality of bridges, but is recommended only for shorter spans or when using large nozzle sizes.

## Extra bridge layers

This option enables the generation of an extra bridge layer over internal and/or external bridges.

Extra bridge layers help improve bridge appearance and reliability, as the solid infill is better supported. This is especially useful in fast printers, where the bridge and solid infill speeds vary greatly. The extra bridge layer results in reduced pillowing on top surfaces, as well as reduced separation of the external bridge layer from its surrounding perimeters.

It is generally recommended to set this to at least 'External bridge only', unless specific issues with the sliced model are found.

**Options:**

1. **Disabled** - does not generate second bridge layers. This is the default and is set for compatibility purposes.
2. **External bridge only** - generates second bridge layers for external-facing bridges only. Please note that small bridges that are shorter or narrower than the set number of perimeters will be skipped as they would not benefit from a second bridge layer. If generated, the second bridge layer will be extruded parallel to the first bridge layer to reinforce the bridge strength.
3. **Internal bridge only** - generates second bridge layers for internal bridges over sparse infill only. Please note that the internal bridges count towards the top shell layer count of your model. The second internal bridge layer will be extruded as close to perpendicular to the first as possible. If multiple regions in the same island, with varying bridge angles are present, the last region of that island will be selected as the angle reference.
4. **Apply to all** - generates second bridge layers for both internal and external-facing bridges.

## Filter out small internal bridges

This option can help reduce pillowing on top surfaces in heavily slanted or curved models.

By default, small internal bridges are filtered out and the internal solid infill is printed directly over the sparse infill. This works well in most cases, speeding up printing without too much compromise on top surface quality.

However, in heavily slanted or curved models, especially where too low a sparse infill density is used, this may result in curling of the unsupported solid infill, causing pillowing.

Enabling limited filtering or no filtering will print internal bridge layer over slightly unsupported internal solid infill. The options below control the sensitivity of the filtering, i.e. they control where internal bridges are created:

1. **Filter** - enables this option. This is the default behavior and works well in most cases.
2. **Limited filtering** - creates internal bridges on heavily slanted surfaces while avoiding unnecessary bridges. This works well for most difficult models.
3. **No filtering** - creates internal bridges on every potential internal overhang. This option is useful for heavily slanted top surface models; however, in most cases, it creates too many unnecessary bridges.

## Bridge Counterbore hole

This option creates bridges for counterbore holes, allowing them to be printed without support. Available modes include:

1. **None**: No bridge is created.
2. **Partially Bridged**: Only a part of the unsupported area will be bridged.
3. **Sacrificial Layer**: A full sacrificial bridge layer is created.