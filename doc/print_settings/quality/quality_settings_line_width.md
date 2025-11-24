# Line Width

These settings define how wide each extruded line of filament will be.  
Line width can be configured in two ways:

- Fixed value in millimeters (mm)
- Percentage of the nozzle diameter

> [!TIP]
> Using percentages allows the slicer to automatically adjust the line width when the nozzle size changes, helping maintain consistent print quality across different nozzle sizes.

A good starting point is setting the line width to **100% of the nozzle diameter**. Values below this may lead to poor adhesion, while values above **150%** can cause **over-extrusion**, resulting in blobs or poor surface quality.  
However, slightly wider lines generally improve **layer bonding** and **print strength**, especially for internal features like walls and infill.

> [!NOTE]
> **100% line width will extrude slightly narrower than the nozzle**, but once squished onto the layer below, it flattens to match the nozzle size.  
> You can read more on the flow math here: [Flow Math](https://manual.slic3r.org/advanced/flow-math).

> [!IMPORTANT]
> This will match only if using the [**Classic** wall generator](quality_settings_wall_generator#classic).  
> [**Arachne**](quality_settings_wall_generator#arachne) will adjust the line width dynamically based on the model's geometry, using this values as a reference.

## Line Types

In OrcaSlicer, you can assign different line widths to specific parts of the print. Each type can be customized:

### Default

Fallback value used when a specific line width is not set (set to `0`).

### First Layer

A wider first layer (with a higher [first layer height](quality_settings_layer_height#first-layer-height)) improves bed adhesion and compensates for uneven build surfaces.  
First layer line width also overrides [Brim's](others_settings_brim) and [Skirt's](others_settings_skirt) line width.

### Outer Wall

Controls dimensional accuracy and surface finish.  
Recommended: **105%–120%** of the nozzle diameter for clean overhangs and detail.

### Inner Wall

Can be set wider than the outer wall to enhance structural strength.  
Typical value: **≥120%**.

### Top Surface

Affects the quality of visible top layers.  
Recommended: **100%–105%** for smooth results without over-extrusion.

### Sparse Infill

Recommended to use a conservative value, typically around 115% to improve layer adhesion without getting near volumetric flow limitations.  
If you need stronger infill, it's recommended to use [infill line multiplier](strength_settings_infill#fill-multiline) when possible.

### Internal Solid Infill

Used for solid top/bottom layers or [100% infill](strength_settings_infill#sparse-infill-density).  
Recommended: **~110%** for good layer adhesion and visual quality.

### Support

Typically set to **100%** to balance material usage and functionality. Reducing it too much can lead to weak support structures that may not hold up during printing or break easily during removal leaving debris on the model.

### Default Line Width Multipliers
When any line width parameter is set to **0**, it will be automatically calculated based on the nozzle diameter using the following rules:

| Feature Type | Multiplier | Example (0.4mm nozzle) |
|--------------|-----------|----------------------|
| **Outer wall** | 1.125× | 0.45mm |
| **Inner wall** | 1.125× | 0.45mm |
| **Solid infill** | 1.125× | 0.45mm |
| **Sparse infill** | 1.125× | 0.45mm |
| **Top surface** | 1.0× | 0.40mm |
| **Support material** | 1.0× | 0.40mm |
| **Support interface** | 1.0× | 0.40mm |
### Examples

- **0.4mm nozzle**: Walls and infill → 0.45mm (0.4 × 1.125)
- **0.6mm nozzle**: Walls and infill → 0.675mm (0.6 × 1.125)

#### Displayed Precision

In the slicing preview's line width tab, values may display with rounding (0.675mm might show as 0.68mm due to precision limitations).
