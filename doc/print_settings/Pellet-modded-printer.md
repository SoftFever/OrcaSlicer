# Pellet printer
Pellet 3D printers, commonly used for large prints (usually larger than 1m³), are based on a technology similar to FDM printing. However, instead of using filaments, these printers use pellets as the printing material.
The use of pellets requires a different extrusion management compared to traditional filament printers, due to:

- The presence of air between the pellets.
- Variable particle size depending on the material.
- Need for active feeding systems.

This requires additional parameters for a correct extrusion configuration.

# Pellet Flow Coefficient
Large format printers with print volumes in the order of 1m³ generally use pellets for printing.  
The overall tech is very similar to FDM printing — it *is* FDM printing, but instead of filaments, it uses pellets.

The **pellet flow coefficient** is a parameter that represents the extrusion capacity of the pellets, influenced by factors such as:

- Shape of the pellet.
- Type of material.
- Viscosity of the material.

This coefficient is empirically derived for a particular type of pellet and printer. While filament-based systems rely on a known `filament_diameter` to calculate volume, pellet-based systems use this **flow coefficient** instead.

`pellet_flow_coefficient` is essentially a measure of the **packing density** of a particular pellet. Shape, material, and density of each pellet affect this packing density. For practical 3D printing, what ultimately matters is how much material is extruded per single turn of the feeding mechanism (e.g., a gear or screw).

To integrate pellets seamlessly into existing FDM workflows, the `pellet_flow_coefficient` is translated into an equivalent `filament_diameter`, allowing all other calculations (e.g., slicing) to remain unchanged:

```math
\text{filament\_diameter} = \sqrt{\frac{4 \times \text{pellet\_flow\_coefficient}}{\pi}}
```
This formula ensures a linear relationship between the flow coefficient and the extruded volume.

Higher packing density → more material extruded per turn → higher pellet_flow_coefficient → treated as a filament with a larger diameter.

All other printing parameters can then remain unchanged in slicing software.

This approach allows pellet-based printing to integrate smoothly with existing filament-based workflows with only minimal adjustments.

### Operation
This value defines the amount of material extruded for each revolution of the feeding mechanism. Internally, the coefficient is converted to an equivalent value of `filament_diameter` to ensure compatibility with standard FDM volumetric calculations.

The formula used is: *filament_diameter = sqrt( (4 \* pellet_flow_coefficient) / PI )*

- Higher infill density → More extruded material → Higher flow coefficient → Simulation of a larger diameter filament.
- A 20% reduction in the coefficient corresponds to a 20% linear reduction in flow.

In printers where it is not possible to adjust the rotation distance of the extruder motor, the pellet_flow_coefficient allows you to control the flow rate by changing the diameter of the simulated filament.

> [!WARNING]
> Modulating the extrusion by changing the diameter of the virtual filament causes an error in reading the quantity extruded by the printer within the firmware used.
> For example, on a klipper firmware you will have a different reading of the print flow on the screen than the one actually in progress.
> Using this method also causes problems with the retraction and unretraction value, as the conversion with the virtual filament alters the real values.

## Extruder Rotation Volume
The extruder rotation volume represents the volume of material extruded (in mm³) for each complete revolution of the extruder motor. This parameter offers greater precision in the configuration compared to the pellet flow coefficient, eliminating calculation errors due to the simulation of the virtual filament diameter.

### Configuration via Virtual Filament with Area of ​​1 mm²
To further simplify the calculation of the extruded volume and synchronize the printer parameters with the volume values ​​in the G-code, it is possible to simulate a virtual filament with a cross-sectional area of ​​1 mm². In this way, the extrusion values ​​𝐸 in the G-code directly represent the volume in mm³.

#### Implementation steps
1. **Setting the Pellet Flow Coefficient:** Simulate a filament with a cross-sectional area of ​​1 mm² by setting the pellet flow coefficent to 1.
2. **Setting the Extruder Rotation Volume:** Determines how many mm³ of material are extruded for each complete revolution of the extruder motor. This parameter depends on the geometry of the extruder screw or gear and can be calibrated experimentally.
Enter the obtained value in the material configuration as extruder rotation volume.
3. **Set the filament_diameter** equal to 1.1284 within the firmware of your printer

### System Benefits
- Elimination of conversion errors: It is not necessary to simulate filaments of different diameters, reducing errors.
- Greater precision: Using the direct extruded volume in the G-code makes calibration easier and more accurate.
- Dynamic Compatibility: With firmware like Klipper, you can update parameters in real time without having to rewrite the G-code.

> [!IMPORTANT]
> This feature is currently only supported by printers with Klipper firmware. Make sure to check compatibility before proceeding with the configuration.

## Mixing Stepper Rotation Volume
The mixing stepper rotation volume indicates the volume of material (in mm³) actively fed into the extruder via a dedicated motor.

### Usage
To use this feature, you need to:

- Enable the option in the printer settings.
- Define the identifying name of the motor that manages the feeding in the extruder settings.
- Configure the value of mixing_stepper_rotation_volume in the material settings.

> [!IMPORTANT]
> This feature is currently only supported by printers with Klipper firmware. Make sure to check compatibility before proceeding with the configuration.