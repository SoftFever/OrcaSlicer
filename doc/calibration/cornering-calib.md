# Cornering

Cornering is a critical aspect of 3D printing that affects print quality and accuracy. It's how the printer handles changes in direction during movement, particularly at corners and curves. Proper cornering settings can reduce artifacts such as ringing, ghosting, and overshooting, resulting in cleaner and more precise prints.

## Jerk

TODO: Jerk calibration not implemented yet.

## Junction Deviation

Junction Deviation is the default method for controlling cornering speed in **Marlin firmware (Marlin 2.x)**.  
Higher values allow more aggressive cornering, while lower values produce smoother, more controlled corners.  
The default value in Marlin is often `0.08mm`, which may be too high for some printers and may cause ringing. Consider lowering this value to reduce ringing, but avoid setting it too low that could lead to excessively slow cornering speed.

```math
JD = 0.4 \cdot \frac{\text{Jerk}^2}{\text{Acceleration}}
```

1. Pre-requisites:
   1. Check if your printer has Junction Deviation enabled. Look for `Junction deviation` in the printer's advanced settings.
   2. In OrcaSlicer, set:
      1. Acceleration high enough to trigger ringing (e.g., 2000 mm/s²).
      2. Speed high enough to trigger ringing (e.g., 100 mm/s).
   3. Use an opaque, high-gloss filament to make ringing more visible.
2. You need to print the Junction Deviation test.  
   ![jd_first_menu](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/JunctionDeviation/jd_first_menu.png?raw=true)
   1. Measure the X and Y heights and read the frequency set at that point in OrcaSlicer.  
      ![jd_first_print_measure](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/JunctionDeviation/jd_first_print_measure.jpg?raw=true)  
      ![jd_first_slicer_measure](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/JunctionDeviation/jd_first_slicer_measure.png?raw=true)
   2. You will likely need values lower than `0.08mm`, as in the example. To find a better maximum JD value, print a new calibration tower with a maximum set near the point where corners start losing sharpness.
   3. Print the second Junction Deviation test with the new maximum value.  
      ![jd_second_menu](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/JunctionDeviation/jd_second_menu.png?raw=true)
   4. Measure the X and Y heights and read the frequency set at that point in OrcaSlicer.  
      ![jd_second_print_measure](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/JunctionDeviation/jd_second_print_measure.jpg?raw=true)  
      ![jd_second_slicer_measure](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/JunctionDeviation/jd_second_slicer_measure.png?raw=true)
3. Save the settings
   1. Set your Maximum Junction Deviation value in [Printer settings/Motion ability/Jerk limitation].  
      ![jd_printer_jerk_limitation](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/JunctionDeviation/jd_printer_jerk_limitation.png?raw=true)
   2. Use the following G-code to set the value:

   ```gcode
   M205 J#JunctionDeviationValue
   M500
   ```

   Example

   ```gcode
   M205 J0.012
   M500
   ```

   3. Recompile your MarlinFW
      1. In Configuration.h uncomment and set:

      ```cpp
      #define JUNCTION_DEVIATION_MM 0.012  // (mm) Distance from real junction edge
      ```

      2. Ensure Classic Jerk is disabled (commented out):

      ```cpp
      //#define CLASSIC_JERK
      ```

## Credits

- **Junction Deviation Machine Limit** [@RF47](https://github.com/RF47)
- **Junction Deviation Calibration** [@IanAlexis](https://github.com/IanAlexis)
- **Fast tower model** [@RF47](https://github.com/RF47)
