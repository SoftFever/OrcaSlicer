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

![printer-preset](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/gui/printer-preset.png)

- [Air filtration/Exhaust fan handling](air-filtration)
- [Auxiliary fan handling](Auxiliary-fan)
- [Chamber temperature control](chamber-temperature)
- [Adaptive Bed Mesh](adaptive-bed-mesh)
- [Using different bed types in Orca](bed-types)

## Material Settings

![filament-preset](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/gui/filament-preset.png)

- [Single Extruder Multimaterial](semm)
- [Pellet Printers (pellet flow coefficient)](pellet-flow-coefficient)

## Prepare

First steps to prepare your model/s for printing.

- [STL Transformation](stl-transformation)

## Process Settings

![process-preset](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/gui/process-preset.png)

The below sections provide a detailed settings explanation as well as tips and tricks in setting these for optimal print results.

### Quality Settings

![process-quality](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/gui/process/process-quality.png?raw=true)

- [Layer Height Settings](quality_settings_layer_height)
- [Line Width Settings](quality_settings_line_width)
- [Seam Settings](quality_settings_seam)
- [Precision](quality_settings_precision)
  - [Precise Wall](quality_settings_precision#precise-wall)
  - [Precise Z Height](quality_settings_precision#precise-z-height)
  - [Slice gap closing radius](quality_settings_precision#slice-gap-closing-radius)
  - [Resolution](quality_settings_precision#resolution)
  - [Arc fitting](quality_settings_precision#arc-fitting)
  - [X-Y Compensation](quality_settings_precision#x-y-compensation)
  - [Elephant foot compensation](quality_settings_precision#elephant-foot-compensation)
  - [Precise wall](quality_settings_precision#precise-wall)
  - [Precise Z Height](quality_settings_precision#precise-z-height)
  - [Polyholes](quality_settings_precision#polyholes)
- [Wall generator](quality_settings_wall_generator)

### Strength Settings

![process-strength](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/gui/process/process-strength.png?raw=true)

- [Top and Bottom Shells](strength_settings_top_bottom_shells)
- [Infill](strength_settings_infill)

### Speed Settings

![process-speed](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/gui/process/process-speed.png?raw=true)

- [Extrusion rate smoothing](speed_settings_extrusion_rate_smoothing)

### Support Settings

![process-support](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/gui/process/process-support.png?raw=true)

WIP...

### Multimaterial Settings

![process-multimaterial](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/gui/process/process-multimaterial.png?raw=true)

WIP...

### Others Settings

![process-others](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/gui/process/process-others.png?raw=true)

WIP...

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

## Developer Section

This is a documentation from someone exploring the code and is by no means complete or even completely accurate. Please edit the parts you might find inaccurate. This is probably going to be helpful nonetheless.

- [How to build Orca Slicer](How-to-build)
- [Localization and translation guide](Localization_guide)
- [How to create profiles](How-to-create-profiles)
- [How to contribute to the wiki](How-to-wiki)
- [Preset, PresetBundle and PresetCollection](Preset-and-bundle)
- [Plater, Sidebar, Tab, ComboBox](plater-sidebar-tab-combobox)
- [Slicing Call Hierarchy](slicing-hierarchy)
