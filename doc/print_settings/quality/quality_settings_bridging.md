# Bridging

"Bridging" is a 3D printing technique that allows you to print structures across gaps or voids without direct support underneath. OrcaSlicer provides several parameters to optimize bridge quality, minimizing filament sag and improving the appearance of top layers.

- [Flow ratio](#flow-ratio)
- [Bridge density](#bridge-density)
- [Thick bridges](#thick-bridges)
- [Extra bridge layers](#extra-bridge-layers)
- [Filter out small internal bridges](#filter-out-small-internal-bridges)
- [Bridge Counterbore hole](#bridge-counterbore-hole)

## Flow ratio

This value governs the thickness of the bridge layer.

For external bridges, when material tends to sag, reducing the flow ratio can help improve appearance.  
For internal bridges, increasing the flow ratio can enhance strength and upper layer quality.

- Values above 1.0: Increase the amount of material while maintaining line spacing. This can improve line contact and strength.
- Values below 1.0: Reduce the amount of material while adjusting line spacing to maintain contact. This can improve sagging.

The actual bridge flow used is calculated by multiplying this value with the filament flow ratio, and if set, the object's flow ratio.

## Bridge density

Controls the density (spacing) of external bridge lines. Default is 100%.  
Theoretically, 100% means a solid bridge, but due to the tendency of bridge extrusions to sag, 100% may not be sufficient.

- Higher than 100% density (**Recommended**):
  - Pros: Produce smoother bridge surfaces, as overlapping lines provide additional support during printing.
  - Cons: Can cause overextrusion, decreasing lower and upper surface quality.
- Lower than 100% density:
  - Pros: Can create a string like the first layer. Quick and with better cooling, as there is more space for air to circulate around the extruded bridge.
  - Cons: May lead to sagging and poor surface finish.

## Thick bridges

When enabled, bridge extrusion uses a line height equal to the nozzle diameter.  
![thick-bridges](https://github.com/OrcaSlicer/OrcaSlicer/blob/main/doc/images/bridging/thick-bridges.png?raw=true)  
This improves the strength and reliability of bridges, allowing you to span longer distances; however, it may produce a rougher surface finish.  
Disabling this option can improve bridge appearance, but it is recommended only for shorter spans or when using large nozzles.  
We recommend enabling this option for internal bridges, as it increases their reliability when printed over sparse infill.

## Extra bridge layers

This option enables the generation of an extra bridge layer over bridges.

Extra bridge layers help improve bridge appearance and reliability, as the solid infill is better supported. This is especially useful in fast printers, where the bridge and solid infill speeds vary greatly. The extra bridge layer results in reduced pillowing on top surfaces, as well as reduced separation of the external bridge layer from its surrounding perimeters.

It is generally recommended to set this to at least **External bridge only**, unless specific issues with the sliced model are found.

**Options:**

- **Disabled** - does not generate second bridge layers. This is the default and is set for compatibility purposes.
- **External bridge only** - generates second bridge layers for external-facing bridges only. Please note that small bridges that are shorter or narrower than the set number of perimeters will be skipped as they would not benefit from a second bridge layer. If generated, the second bridge layer will be extruded parallel to the first bridge layer to reinforce the bridge strength.
- **Internal bridge only** - generates second bridge layers for internal bridges over sparse infill only. Please note that the internal bridges count towards the top shell layer count of your model. The second internal bridge layer will be extruded as close to perpendicular to the first as possible. If multiple regions in the same island, with varying bridge angles are present, the last region of that island will be selected as the angle reference.
- **Apply to all** - generates second bridge layers for both internal and external-facing bridges.

## Filter out small internal bridges

This option can help reduce pillowing on top surfaces in heavily slanted or curved models.

By default, small internal bridges are filtered out and the internal solid infill is printed directly over the sparse infill. This works well in most cases, speeding up printing without too much compromise on top surface quality.

However, in heavily slanted or curved models, especially where too low a sparse infill density is used, this may result in curling of the unsupported solid infill, causing pillowing.

Enabling limited filtering or no filtering will print internal bridge layer over slightly unsupported internal solid infill. The options below control the sensitivity of the filtering, i.e. they control where internal bridges are created:

- **Filter:** enables this option. This is the default behavior and works well in most cases.
- **Limited filtering:** creates internal bridges on heavily slanted surfaces while avoiding unnecessary bridges. This works well for most difficult models.
- **No filtering:** creates internal bridges on every potential internal overhang. This option is useful for heavily slanted top surface models; however, in most cases, it creates too many unnecessary bridges.

## Bridge Counterbore hole

When printing counterbore holes, the unsupported area can lead to sagging or poor surface quality. Also the perimeters of the counterbore hole may not be printed correctly, leading to gaps or weak points in the structure.

This option creates bridges for counterbore holes, allowing them to be printed without support.  
Available modes include:

- **None:** No bridge is created.  
  ![bridge-counterbore-none](https://github.com/OrcaSlicer/OrcaSlicer/blob/main/doc/images/bridging/bridge-counterbore-none.png?raw=true)
- **Partially Bridged:** Only a part of the unsupported area will be bridged, creating a supporting layer for the next layer.  
  ![bridge-counterbore-partially](https://github.com/OrcaSlicer/OrcaSlicer/blob/main/doc/images/bridging/bridge-counterbore-partially.gif?raw=true)
- **Sacrificial Layer:** A full sacrificial bridge layer is created. This will close the counterbore hole, allowing the next layer to be printed without sagging. The sacrificial layer must be broken through after printing.  
  ![bridge-counterbore-sacrificial](https://github.com/OrcaSlicer/OrcaSlicer/blob/main/doc/images/bridging/bridge-counterbore-sacrificial.png?raw=true)
