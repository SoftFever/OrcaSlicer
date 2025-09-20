# Welcome to the OrcaSlicer WIKI!

OrcaSlicer is a powerful open source slicer for FFF (FDM) 3D Printers. This wiki page aims to provide an detailed explanation of the slicer settings, how to get the most out of them as well as how to calibrate and setup your printer.

- [Printer Settings](#printer-settings)
- [Material Settings](#material-settings)
- [Process Settings](#process-settings)
  - [Quality Settings](#quality-settings)
  - [Strength Settings](#strength-settings)
  - [Speed Settings](#speed-settings)
  - [Support Settings](#support-settings)
  - [Multimaterial Settings](#multimaterial-settings)
  - [Others Settings](#others-settings)
- [Prepare](#prepare)
- [Calibrations](#calibrations)
- [Developer Section](#developer-section)

> [!WARNING]
> This wiki is community-maintained.  
> Some pages may be **outdated** while others may be **newer** and present only in [nightly build](https://github.com/SoftFever/OrcaSlicer/releases/tag/nightly-builds) or [latest release](https://github.com/SoftFever/OrcaSlicer/releases).

> [!NOTE]
> Please consider contributing to the wiki following the [How to contribute to the wiki](developer-reference/How-to-wiki.md) guide.

## Printer Settings

![printer-preset](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/GUI/printer-preset.png?raw=true)

![printer](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/printer.svg?raw=true) Settings related to the 3D printer hardware and its configuration.

- [Air filtration/Exhaust fan handling](printer_settings/air-filtration.md)
- [Auxiliary fan handling](printer_settings/Auxiliary-fan.md)
- [Chamber temperature control](printer_settings/chamber-temperature.md)
- [Adaptive Bed Mesh](printer_settings/adaptive-bed.md-mesh)
- [Using different bed types in Orca](printer_settings/bed-types.md)

## Material Settings

![filament-preset](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/GUI/filament-preset.png?raw=true)

![filament](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/filament.svg?raw=true) Settings related to the 3D printing material.

- [Single Extruder Multimaterial](material_settings/semm.md)
- [Pellet Printers (pellet flow coefficient)](material_settings/pellet-flow-coefficient.md)

## Process Settings

![process-preset](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/GUI/process-preset.png?raw=true)

![process](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/process.svg?raw=true) Settings related to the 3D printing process.

### Quality Settings

![custom-gcode_quality](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/custom-gcode_quality.svg?raw=true) Settings related to print quality and aesthetics.  
![process-quality](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/GUI/process/process-quality.png?raw=true)

- ![param_layer_height](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_layer_height.svg?raw=true) [Layer Height Settings](print_settings/quality/quality_settings_layer_height.md)
- ![param_line_width](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_line_width.svg?raw=true) [Line Width Settings](print_settings/quality/quality_settings_line_width.md)
- ![param_seam](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_seam.svg?raw=true) [Seam Settings](print_settings/quality/quality_settings_seam.md)
- ![param_precision](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_precision.svg?raw=true) [Precision](print_settings/quality/quality_settings_precision.md)
- ![param_ironing](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_ironing.svg?raw=true) [Ironing](print_settings/quality/quality_settings_ironing.md)
- ![param_wall_generator](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_wall_generator.svg?raw=true) [Wall generator](print_settings/quality/quality_settings_wall_generator.md)
- ![param_wall_surface](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_wall_surface.svg?raw=true) [Walls and surfaces](print_settings/quality/quality_settings_wall_and_surfaces.md)
- ![param_bridge](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_bridge.svg?raw=true) [Bridging](print_settings/quality/quality_settings_bridging.md)
- ![param_overhang](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_overhang.svg?raw=true) [Overhangs](print_settings/quality/quality_settings_overhangs.md)

### Strength Settings

![custom-gcode_strength](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/custom-gcode_strength.svg?raw=true) Settings related to print strength and durability.  
![process-strength](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/GUI/process/process-strength.png?raw=true)

- ![param_wall](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_wall.svg?raw=true) [Walls](print_settings/strength/strength_settings_walls.md)
- ![param_shell](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_shell.svg?raw=true) [Top and Bottom Shells](print_settings/strength/strength_settings_top_bottom_shells.md)
- ![param_infill](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_infill.svg?raw=true) [Infill](print_settings/strength/strength_settings_infill.md)
  - ![param_concentric](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_concentric.svg?raw=true) [Fill Patterns](print_settings/strength/strength_settings_patterns.md)
  - ![param_gcode](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_gcode.svg?raw=true) [Template Metalanguage for infill rotation](print_settings/strength/strength_settings_infill_rotation_template_metalanguage.md)
- ![param_advanced](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_advanced.svg?raw=true) [Advanced](print_settings/strength/strength_settings_advanced.md)

### Speed Settings

![custom-gcode_speed](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/custom-gcode_speed.svg?raw=true) Settings related to print speed and movement.  
![process-speed](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/GUI/process/process-speed.png?raw=true)

- ![param_speed_first](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_speed_first.svg?raw=true) [Initial Layer Speed](print_settings/speed/speed_settings_initial_layer_speed.md)
- ![param_speed](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_speed.svg?raw=true) [Other Layers Speed](print_settings/speed/speed_settings_other_layers_speed.md)
- ![param_overhang_speed](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_overhang_speed.svg?raw=true) [Overhang Speed](print_settings/speed/speed_settings_overhang_speed.md)
- ![param_travel_speed](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_travel_speed.svg?raw=true) [Travel Speed](print_settings/speed/speed_settings_travel.md)
- ![param_acceleration](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_acceleration.svg?raw=true) [Acceleration](print_settings/speed/speed_settings_acceleration.md)
- ![param_jerk](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_jerk.svg?raw=true) [Jerk (XY)](print_settings/speed/speed_settings_jerk_xy.md)
- ![param_advanced](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_advanced.svg?raw=true) [Advanced / Extrusion rate smoothing](print_settings/speed/speed_settings_advanced.md)

### Support Settings

![custom-gcode_support](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/custom-gcode_support.svg?raw=true) Settings related to support structures and their properties.  
![process-support](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/GUI/process/process-support.png?raw=true)

- ![param_support](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_support.svg?raw=true) [Support](print_settings/support/support_settings_support.md)
- ![param_raft](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_raft.svg?raw=true) [Raft](print_settings/support/support_settings_raft.md)
- ![param_support_filament](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_support_filament.svg?raw=true) [Support Filament](print_settings/support/support_settings_filament.md)
- ![param_ironing](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_ironing.svg?raw=true) [Support Ironing](print_settings/support/support_settings_ironing.md)
- ![param_advanced](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_advanced.svg?raw=true) [Advanced](print_settings/support/support_settings_advanced.md)
- ![param_support_tree](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_support_tree.svg?raw=true) [Tree Supports](print_settings/support/support_settings_tree.md)

### Multimaterial Settings

![custom-gcode_multi_material](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/custom-gcode_multi_material.svg?raw=true) Settings related to multimaterial printing.  
![process-multimaterial](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/GUI/process/process-multimaterial.png?raw=true)

- ![param_tower](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_tower.svg?raw=true) [Prime Tower](print_settings/multimaterial/multimaterial_settings_prime_tower.md)
- ![param_filament_for_features](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_filament_for_features.svg?raw=true) [Filament for Features](print_settings/multimaterial/multimaterial_settings_filament_for_features.md)
- ![param_ooze_prevention](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_ooze_prevention.svg?raw=true) [Ooze Prevention](print_settings/multimaterial/multimaterial_settings_ooze_prevention.md)
- ![param_flush](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_flush.svg?raw=true) [Flush Options](print_settings/multimaterial/multimaterial_settings_flush_options.md)
- ![param_advanced](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_advanced.svg?raw=true) [Advanced](print_settings/multimaterial/multimaterial_settings_advanced.md)

### Others Settings

![custom-gcode_other](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/custom-gcode_other.svg?raw=true) Settings related to various other print settings.  
![process-others](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/GUI/process/process-others.png?raw=true)

- ![param_skirt](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_skirt.svg?raw=true) [Skirt](print_settings/others/others_settings_skirt.md)
- ![param_adhension](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_adhension.svg?raw=true) [Brim](print_settings/others/others_settings_brim.md)
- ![param_special](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_special.svg?raw=true) [Special Mode](print_settings/others/others_settings_special_mode.md)
- ![fuzzy_skin](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/fuzzy_skin.svg?raw=true) [Fuzzy Skin](print_settings/others/others_settings_fuzzy_skin.md)
- ![param_gcode](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_gcode.svg?raw=true) [G-Code Output](print_settings/others/others_settings_g_code_output.md)
- ![param_gcode](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/param_gcode.svg?raw=true) [Post Processing Scripts](print_settings/others/others_settings_post_processing_scripts.md)
- ![note](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/note.svg?raw=true) [Notes](print_settings/others/others_settings_notes.md)

## Prepare

![tab_3d_active](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/tab_3d_active.svg?raw=true) First steps to prepare your model/s for printing.

- [STL Transformation](print_prepare/stl-transformation.md)

## Calibrations

![tab_calibration_active](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/tab_calibration_active.svg?raw=true) The [Calibration Guide](calibration/Calibration.md) outlines Orca’s key calibration tests and their suggested order of execution.

- [Temperature](calibration/temp-calib.md)
- [Flow Rate](calibration/flow-rate-calib.md)
- [Pressure Advance](calibration/pressure-advance-calib.md)
  - [Adaptive Pressure Advance Guide](calibration/adaptive-pressure-advance-calib.md)
- [Retraction](calibration/retraction-calib.md)
- [Tolerance](calibration/tolerance-calib.md)
- Advanced:
  - [Volumetric Speed](calibration/volumetric-speed-calib.md)
  - [Cornering (cJerk & Junction Deviation)](calibration/cornering-calib.md)
  - [Input Shaping](calibration/input-shaping-calib.md)
  - [VFA](calibration/vfa-calib.md)

## Developer Section

![im_code](https://github.com/SoftFever/OrcaSlicer/blob/main/resources/images/im_code.svg?raw=true) This is a documentation from someone exploring the code and is by no means complete or even completely accurate. Please edit the parts you might find inaccurate. This is probably going to be helpful nonetheless.

- [How to build OrcaSlicer](developer-reference/How-to-build.md)
- [Localization and translation guide](developer-reference/Localization_guide.md)
- [How to create profiles](developer-reference/How-to-create-profiles.md)
- [How to contribute to the wiki](developer-reference/How-to-wiki.md)
- [Preset, PresetBundle and PresetCollection](developer-reference/Preset-and-bundle.md)
- [Plater, Sidebar, Tab, ComboBox](developer-reference/plater-sidebar-tab-combobox.md)
- [Slicing Call Hierarchy](developer-reference/slicing-hierarchy.md)
