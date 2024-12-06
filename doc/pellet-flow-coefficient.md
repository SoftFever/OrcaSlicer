# Pellet Flow Coefficient
## Introduction
Pellet 3D printers, commonly used for print volumes around 1m³, rely on technology similar to FDM printing. However, instead of using filaments, these printers utilize pellets as the printing material.

The main difference compared to filament-based printing lies in the fact that, while filament_diameter is a fixed value used to calculate the volume of extruded material, pellets require a specific coefficient: the pellet_flow_coefficient. This coefficient is empirically derived for a particular type of pellet and printer configuration.

## Pellet Flow Coefficient
The pellet_flow_coefficient is a value about pellets extrusion capacity, influenced by factors such as shape, material, and individual material viscosity. This value determines how much material is extruded per turn of the printer's feeding mechanism.

Internally, the pellet_flow_coefficient is translated into an equivalent filament_diameter value to maintain compatibility with traditional volumetric calculations used in FDM printing. The formula is:

*filament_diameter = sqrt( (4 \* pellet_flow_coefficient) / PI )*

### Relationship Between Pellet Flow Coefficient and Material Flow
- Higher packing density → More material extruded per turn → Higher pellet_flow_coefficient → Simulated as a filament with a larger diameter.
- Reducing the pellet_flow_coefficient by 20% is equivalent to decreasing the extrusion flow by 20%.

### Advantages of Pellet Flow Coefficient
- If the rotation distance is fixed, the pellet_flow_coefficient allows control over the extruder’s flow percentage without modifying the simulated filament diameter.
- For printers that support dynamic modification of the rotation distance (e.g., with Klipper using the `SET_ROTATION_DISTANCE` command), the pellet_flow_coefficient can be set to 1, and the rotation distance becomes the reference value for extruded volume.

## Extruder Rotation Distance
The rotation distance represents the volume of material extruded (in mm³) for each full turn of the extruder motor. This parameter is essential for accurately configuring the printer to achieve precise prints.

### Usage
To use this value during printing, it must be explicitly called within the startup script `START_PRINT`.

## Mixing Stepper Rotation Distance
The mixing stepper rotation distance is the value that controls how much material is actively fed into the extruder by the feeding mechanism.

### Usage
This parameter also needs to be explicitly called in the startup script `START_PRINT` to correctly configure the material flow during printing.

