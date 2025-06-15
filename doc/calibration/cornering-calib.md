# Cornering

Cornering is a critical aspect of 3D printing that affects the quality and accuracy of prints. It refers to how the printer handles changes in direction during movement, particularly at corners and curves. Proper cornering settings can help reduce artifacts like ringing, ghosting, and overshooting, leading to cleaner and more precise prints.

## Jerk

TODO: Jerk calibration not implemented yet.

## Junction Deviation

Junction Deviation is the default method for controlling cornering speed in MarlinFW (Marlin2) printers.
Higher values result in more aggressive cornering speeds, while lower values produce smoother, more controlled cornering.
The default value in Marlin is typically set to 0.08mm, which may be too high for some printers, potentially causing ringing. Consider lowering this value to reduce ringing, but avoid setting it too low, as this could lead to excessively slow cornering speeds.

1. Pre-requisites:
   1. Check if your printer has Junction Deviation enabled. You can do this by sending the command `M503` to your printer and looking for the line `Junction deviation: 0.25`.
   2. In OrcaSlicer, set:
      1. Acceleration high enough to trigger ringing (e.g., 2000 mm/s²).
      2. Speed high enough to trigger ringing (e.g., 100 mm/s).
   3. Use an opaque, high-gloss filament to make the ringing more visible.
2. You need to print the Junction Deviation test.

   ![jd_first_menu](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/JunctionDeviation/jd_first_menu.png?raw=true)

   1. Measure the X and Y heights and read the frequency set at that point in Orca Slicer.

   ![jd_first_print_measure](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/JunctionDeviation/jd_first_print_measure.jpg?raw=true)
   ![jd_first_slicer_measure](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/JunctionDeviation/jd_first_slicer_measure.png?raw=true)

   2. It’s very likely that you’ll need to set values lower than 0.08 mm, as shown in the previous example. To determine a more accurate maximum JD value, you can print a new calibration tower with a maximum value set at the point where the corners start losing sharpness.
   3. Print the second Junction Deviation test with the new maximum value.

   ![jd_second_menu](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/JunctionDeviation/jd_second_menu.png?raw=true)

   4. Measure the X and Y heights and read the frequency set at that point in Orca Slicer.

   ![jd_second_print_measure](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/JunctionDeviation/jd_second_print_measure.jpg?raw=true)
   ![jd_second_slicer_measure](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/JunctionDeviation/jd_second_slicer_measure.png?raw=true)

3. Save the settings
   1. Set your Maximun Junction Deviation value in [Printer settings/Motion ability/Jerk limitation].
   2. Use the following G-code to set the mm:
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
      2. Check Classic Jerk is disabled (commented).
      ```cpp
         //#define CLASSIC_JERK
      ```
