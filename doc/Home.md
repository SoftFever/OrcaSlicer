# Welcome to the OrcaSlicer WIKI!

Orca slicer is a powerful open source slicer for FFF (FDM) 3D Printers. This wiki page aims to provide an detailed explanation of the slicer settings, how to get the most out of them as well as how to calibrate and setup your printer.

- [Printer Settings](#printer-settings)
- [Material Settings](#material-settings)
- [Prepare](#prepare)
- [Process Settings](#process-settings)
  - [Quality Settings](#quality-settings)
  - [Strength Settings](#strength-settings)
  - [Speed Settings](#speed-settings)
  - [Support Settings](#support-settings)
  - [Multimaterial Settings](#multimaterial-settings)
  - [Others Settings](#others-settings)
- [Calibrations](#calibrations)
- [Developer Section](#developer-section)

> [!NOTE]
> The Wiki is **Work In Progress** so bear with us while we get it up and running!  
> Please consider contributing to the wiki following the [How to contribute to the wiki](How-to-wiki) guide.

## Printer Settings

![printer-preset](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/GUI/printer-preset.png?raw=true)

- [Air filtration/Exhaust fan handling](air-filtration)
- [Auxiliary fan handling](Auxiliary-fan)
- [Chamber temperature control](chamber-temperature)
- [Adaptive Bed Mesh](adaptive-bed-mesh)
- [Using different bed types in Orca](bed-types)

## Material Settings

![filament-preset](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/GUI/filament-preset.png?raw=true)

- [Single Extruder Multimaterial](semm)
- [Pellet Printers (pellet flow coefficient)](pellet-flow-coefficient)

## Prepare

First steps to prepare your model/s for printing.

- [STL Transformation](stl-transformation)

## Process Settings

![process-preset](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/GUI/process-preset.png?raw=true)

The below sections provide a detailed settings explanation as well as tips and tricks in setting these for optimal print results.

### Quality Settings

![process-quality](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/GUI/process/process-quality.png?raw=true)

- ![param_layer_height](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_layer_height.svg?raw=true) [Layer Height Settings](quality_settings_layer_height)
- ![param_line_width](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_line_width.svg?raw=true) [Line Width Settings](quality_settings_line_width)
- ![param_seam](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_seam.svg?raw=true) [Seam Settings](quality_settings_seam)
- ![param_precision](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_precision.svg?raw=true) [Precision](quality_settings_precision)
- ![param_ironing](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_ironing.svg?raw=true) [Ironing](quality_settings_ironing)
- ![param_wall_generator](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_wall_generator.svg?raw=true) [Wall generator](quality_settings_wall_generator)
- ![param_wall_surface](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_wall_surface.svg?raw=true) [Walls and surfaces](quality_settings_wall_and_surfaces)
- ![param_bridge](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_bridge.svg?raw=true) [Bridging](quality_settings_bridging)
- ![param_overhang](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_overhang.svg?raw=true) [Overhangs](quality_settings_overhangs)

### Strength Settings

![process-strength](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/GUI/process/process-strength.png?raw=true)

- ![param_wall](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_wall.svg?raw=true) [Walls](strength_settings_walls)
- ![param_shell](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_shell.svg?raw=true) [Top and Bottom Shells](strength_settings_top_bottom_shells)
- ![param_infill](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_infill.svg?raw=true) [Infill](strength_settings_infill)
  - ![param_gcode](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_gcode.svg?raw=true) [Template Metalanguage for infill rotation](strength_settings_infill_rotation_template_metalanguage)
- ![param_advanced](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_advanced.svg?raw=true) [Advanced](strength_settings_advanced)

### Speed Settings

![process-speed](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/GUI/process/process-speed.png?raw=true)

- ![param_speed_first](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_speed_first.svg?raw=true) [Initial Layer Speed](speed_settings_initial_layer_speed)
- ![param_speed](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_speed.svg?raw=true) [Other Layers Speed](speed_settings_other_layers_speed)
- ![param_overhang_speed](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_overhang_speed.svg?raw=true) [Overhang Speed](speed_settings_overhang_speed)
- ![param_travel_speed](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_travel_speed.svg?raw=true) [Travel Speed](speed_settings_travel)
- ![param_acceleration](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_acceleration.svg?raw=true) [Acceleration](speed_settings_acceleration)
- ![param_jerk](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_jerk.svg?raw=true) [Jerk (XY)](speed_settings_jerk_xy)
- ![param_advanced](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_advanced.svg?raw=true) [Advanced / Extrusion rate smoothing](speed_settings_advanced)

### Support Settings

![process-support](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/GUI/process/process-support.png?raw=true)

- ![param_support](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_support.svg?raw=true) [Support](support_settings_support)
- ![param_raft](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_raft.svg?raw=true) [Raft](support_settings_raft)
- ![param_support_filament](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_support_filament.svg?raw=true) [Support Filament](support_settings_filament)
- ![param_ironing](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_ironing.svg?raw=true) [Support Ironing](support_settings_ironing)
- ![param_advanced](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_advanced.svg?raw=true) [Advanced](support_settings_advanced)
- ![param_support_tree](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_support_tree.svg?raw=true) [Tree Supports](support_settings_tree)

### Multimaterial Settings

![process-multimaterial](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/GUI/process/process-multimaterial.png?raw=true)

- ![param_tower](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_tower.svg?raw=true) [Prime Tower](multimaterial_settings_prime_tower)
- ![param_filament_for_features](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_filament_for_features.svg?raw=true) [Filament for Features](multimaterial_settings_filament_for_features)
- ![param_ooze_prevention](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_ooze_prevention.svg?raw=true) [Ooze Prevention](multimaterial_settings_ooze_prevention)
- ![param_flush](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_flush.svg?raw=true) [Flush Options](multimaterial_settings_flush_options)
- ![param_advanced](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_advanced.svg?raw=true) [Advanced](multimaterial_settings_advanced)

### Others Settings

![process-others](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/GUI/process/process-others.png?raw=true)

- ![param_skirt](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_skirt.svg?raw=true) [Skirt](others_settings_skirt)
- ![param_adhension](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_adhension.svg?raw=true) [Brim](others_settings_brim)
- ![param_special](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_special.svg?raw=true) [Special Mode](others_settings_special_mode)
- ![fuzzy_skin](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/fuzzy_skin.svg?raw=true) [Fuzzy Skin](others_settings_fuzzy_skin)
- ![param_gcode](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_gcode.svg?raw=true) [G-Code Output](others_settings_g_code_output)
- ![param_gcode](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_gcode.svg?raw=true) [Post Processing Scripts](others_settings_post_processing_scripts)
- ![note](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/note.svg?raw=true) [Notes](others_settings_notes)

## Calibrations

The [Calibration Guide](Calibration) outlines Orca’s key calibration tests and their suggested order of execution.

- [Temperature](temp-calib)
- [Flow Rate](flow-rate-calib)
- [Pressure Advance](pressure-advance-calib)
  - [Adaptive Pressure Advance Guide](adaptive-pressure-advance-calib)
- [Retraction](retraction-calib)
- [Tolerance](tolerance-calib)
- Advanced:
  - [Volumetric Speed](volumetric-speed-calib)
  - [Cornering (Jerk & Junction Deviation)](cornering-calib)
  - [Input Shaping](input-shaping-calib)
  - [VFA](vfa-calib)

## Developer Section

This is a documentation from someone exploring the code and is by no means complete or even completely accurate. Please edit the parts you might find inaccurate. This is probably going to be helpful nonetheless.

- [How to build Orca Slicer](How-to-build)
- [Localization and translation guide](Localization_guide)
- [How to create profiles](How-to-create-profiles)
- [How to contribute to the wiki](How-to-wiki)
- [Preset, PresetBundle and PresetCollection](Preset-and-bundle)
- [Plater, Sidebar, Tab, ComboBox](plater-sidebar-tab-combobox)
- [Slicing Call Hierarchy](slicing-hierarchy)
