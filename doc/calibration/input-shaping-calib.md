# Input Shaping

During high-speed movements, vibrations can cause a phenomenon called "ringing," where periodic ripples appear on the print surface. Input Shaping provides an effective solution by counteracting these vibrations, improving print quality and reducing wear on components without needing to significantly lower print speeds.

> [!IMPORTANT]
> RepRap can only set one frequency for both X and Y axes so you will need to select a frequency that works well for both axes.

- [Types](#types)
- [Calibration Steps](#calibration-steps)
  - [Fixed-Time Motion](#fixed-time-motion)
- [Credits](#credits)

## Types

It is usually recommended to use MZV, EI (specially for Delta printers) or ZV as a simple and effective solution.  
Not all Input Shaping types are available in all firmwares and their performance may vary depending on the firmware implementation and the printer's mechanics.

The following table summarizes the available types and their compatibility:

| Type             | Name                             | [Klipper](https://www.klipper3d.org/Resonance_Compensation.html#technical-details) | [RepRap](https://docs.duet3d.com/User_manual/Reference/Gcodes#m593-configure-input-shaping) | [Marlin 2](https://marlinfw.org/docs/features/ft_motion.html#more-complexity-zv-input-shaper) | Marlin Legacy |
|------------------|----------------------------------|---------|--------|----------|---------------|
| MZV              | Modified Zero Vibration          | >=0.9.0 | >=3.4  | -        | -             |
| ZV               | Zero Vibration                   | >=0.9.0 | 3.5    | >2.1.2   | -             |
| ZVD              | Zero Vibration Derivative        | >=0.9.0 | >=3.4  | -        | -             |
| ZVDD             | Zero Vibration Double Derivative | -       | >=3.4  | -        | -             |
| ZVDDD            | Zero Vibration Triple Derivative | -       | >=3.4  | -        | -             |
| EI               | Extra Insensitive                | >=0.9.0 | -      | -        | -             |
| 2HUMP_EI / EI2   | Two-Hump Extra Insensitive       | >=0.9.0 | >=3.4  | -        | -             |
| 3HUMP_EI   / EI3 | Three-Hump Extra Insensitive     | >=0.9.0 | >=3.4  | -        | -             |
| [FT_MOTION](https://marlinfw.org/docs/features/ft_motion.html#fixed-time-motion-by-ulendo)        | Fixed-Time Motion                | -       | -      | >2.1.3   | -             |
| DAA              | Damped Anti-Resonance            | -       | < 3.4  | -        | -             |

## Calibration Steps

0. Pre-requisites:
   1. Use an opaque, high-gloss filament to make the ringing more visible.
   2. In OrcaSlicer, set:
      1. Acceleration high enough to trigger ringing (e.g., 20000 mm/s²).
      2. Speed high enough to trigger ringing (e.g., 200 mm/s).

> [!IMPORTANT]
> It's recommended to use the fastest [acceleration](speed_settings_acceleration), [speed](speed_settings_other_layers_speed) and [Jerk/Junction Deviation](speed_settings_jerk_xy) your printer can handle without losing steps.  
> This test **will set the values to high values** limited by your printer's motion ability and the filament's max volumetric speed (avoid materials below 10 mm³/s).

1. Select the Test Model ´Ringing Tower´ (Recommended) or ´Fast Tower´ (Reduced version useful for printers with high ringing).
2. Select the [Input Shaper Type](#types) you want to use test. Each firmware has different types available and each type has different performance.
3. Select a range of frequencies to test. The Default 15hz to 110hz range is usually a good start.
4. Select your damping. Usually, a value between 0.1 and 0.2 is a good start but you can change it to 0 and your printer will use the firmware default value (if available).  
![IS_freq_menu](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/InputShaping/IS_freq_menu.png?raw=true)
   1. Measure the X and Y heights and read the frequency set at that point in OrcaSlicer.  
   ![IS_freq_marlin_print_measure](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/InputShaping/IS_freq_marlin_print_measure.jpg?raw=true)  
   - Marlin:  
   ![IS_freq_marlin_slicer_measure](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/InputShaping/IS_freq_marlin_slicer_measure.png?raw=true)  
   - Klipper:  
   ![IS_freq_klipper_slicer_measure](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/InputShaping/IS_freq_klipper_slicer_measure.png?raw=true)
   2. If not a clear result, you can measure a X and Y min and max acceptable heights and repeat the test with that min and max value.
5. Print the Damping test setting your X and Y frequency to the value you found in the previous step.  
   ![IS_damp_menu](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/InputShaping/IS_damp_menu.png?raw=true)
   1. Measure the X and Y heights and read the damping set at that point in OrcaSlicer.  
   ![IS_damp_marlin_print_measure](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/InputShaping/IS_damp_marlin_print_measure.jpg?raw=true)  
   - Marlin:  
   ![IS_damp_marlin_slicer_measure](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/InputShaping/IS_damp_marlin_slicer_measure.png?raw=true)
   - Klipper:  
   ![IS_damp_klipper_slicer_measure](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/InputShaping/IS_damp_klipper_slicer_measure.png?raw=true)

6. Restore your 3D Printer settings to avoid keep using high acceleration and jerk values.
7. Save the settings
   - Klipper:  
   You need to go to the printer settings and set the SHAPER_TYPE, X and Y frequency and damp to the value you found in the previous step.
   - Marlin:  
   1. Restore your 3D Printer settings to avoid keep using high acceleration and jerk values.
      1. Reboot your printer.
      2. Use the following G-code to restore your printer settings:
      ```gcode
      M501
      ```
   1. Save the settings
      1. You need to go to the printer settings and set the X and Y frequency and damp to the value you found in the previous step.
      2. Use the following G-code to set the frequency:
      ```gcode
      M593 X F#Xfrequency D#XDamping
      M593 Y F#Yfrequency D#YDamping
      M500
      ```
      Example
      ```gcode
      M593 X F37.25 D0.16
      M593 Y F37.5 D0.06
      M500
      ```

### Fixed-Time Motion

TODO: This calibration test is currently under development. See the [Marlin documentation](https://marlinfw.org/docs/gcode/M493.html) for more information.

## Credits

- **Input Shaping Calibration:** [@IanAlexis](https://github.com/IanAlexis) and [@RF47](https://github.com/RF47)
- **Klipper testing:** [@ShaneDelmore](https://github.com/ShaneDelmore)
