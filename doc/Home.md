# Welcome to the OrcaSlicer WIKI!

Orca slicer is a powerful open source slicer for FFF (FDM) 3D Printers. This wiki page aims to provide an detailed explanation of the slicer settings, how to get the most out of them as well as how to calibrate and setup your printer.

- [Prepare](#prepare)
- [Print Settings, Tips and Tricks](#print-settings-tips-and-tricks)
  - [Quality Settings](#quality-settings)
  - [Speed Settings](#speed-settings)
  - [Strength Settings](#strength-settings)
- [Material Settings](#material-settings)
  - [Printer Settings](#printer-settings)
- [Printer Calibration](#printer-calibration)
- [Developer Section](#developer-section)

> [!NOTE]
> The Wiki is **Work In Progress** so bear with us while we get it up and running!  
> Please consider contributing to the wiki following the [How to contribute to the wiki](How-to-wiki) guide.

## Prepare

First steps to prepare your model/s for printing.

- [STL Transformation](stl-transformation)

## Print Settings, Tips and Tricks

The below sections provide a detailed settings explanation as well as tips and tricks in setting these for optimal print results.

### Quality Settings

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

### Speed Settings

- [Extrusion rate smoothing](speed_extrusion_rate_smoothing)

### Strength Settings

- [Infill](strength_settings_infill)

## Material Settings

- [Single Extruder Multimaterial](semm)
- [Pellet Printers (pellet flow coefficient)](pellet-flow-coefficient)

### Printer Settings

- [Air filtration/Exhaust fan handling](air-filtration)
- [Auxiliary fan handling](Auxiliary-fan)
- [Chamber temperature control](chamber-temperature)
- [Adaptive Bed Mesh](adaptive-bed-mesh)
- [Using different bed types in Orca](bed-types)

## Printer Calibration

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
