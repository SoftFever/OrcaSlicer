- [Flow rate](#flow-rate)
- [Pressure Advance](#pressure-advance)
    - [Line method](#line-method)
    - [Pattern method](#pattern-method)
    - [Tower method](#tower-method)
- [Temp tower](#temp-tower)
- [Retraction test](#retraction-test)
- [Orca Tolerance Test](#orca-tolerance-test)
- [Advanced Calibration](#advanced-calibration)
  - [Max Volumetric speed](#max-volumetric-speed)
  - [Input Shaping](#input-shaping)
    - [Klipper](#klipper)
    - [Resonance Compensation](#resonance-compensation)
    - [Marlin](#marlin)
      - [ZV Input Shaping](#zv-input-shaping)
      - [Fixed-Time Motion](#fixed-time-motion)
    - [Junction Deviation](#junction-deviation)
  - [VFA](#vfa)

> [!IMPORTANT]
> After completing the calibration process, remember to create a new project in order to exit the calibration mode.

> [!TIP]
> @ItsDeidara has made a webpage to help with the calculation. Check it out if those equations give you a headache [here](https://orcalibrate.com/).

# Flow rate
> [!WARNING]
> For Bambulab X1/X1C users, make sure you do not select the 'Flow calibration' option.
> 
> ![uncheck](https://user-images.githubusercontent.com/103989404/221345187-3c317a46-4d85-4221-99b9-adb5c7f48026.jpeg)  

![flowrate](./images/flow-calibration.gif)

Calibrating the flow rate involves a two-step process.  
Steps
1. Select the printer, filament, and process you would like to use for the test.
2. Select `Pass 1` in the `Calibration` menu
3. A new project consisting of nine blocks will be created, each with a different flow rate modifier. Slice and print the project.
4. Examine the blocks and determine which one has the smoothest top surface.
![flowrate-pass1_resize](https://user-images.githubusercontent.com/103989404/210138585-98821729-b19e-4452-a08d-697f147d36f0.jpg)
![0-5](https://user-images.githubusercontent.com/103989404/210138714-63daae9c-6778-453a-afa9-9a976d61bfd5.jpg)

5. Update the flow ratio in the filament settings using the following equation: `FlowRatio_old*(100 + modifier)/100`. If your previous flow ratio was `0.98` and you selected the block with a flow rate modifier of `+5`, the new value should be calculated as follows: `0.98x(100+5)/100 = 1.029`.** Remember** to save the filament profile.
6. Perform the `Pass 2` calibration. This process is similar to `Pass 1`, but a new project with ten blocks will be generated. The flow rate modifiers for this project will range from `-9 to 0`.
7. Repeat steps 4. and 5. In this case, if your previous flow ratio was 1.029 and you selected the block with a flow rate modifier of -6, the new value should be calculated as follows: `1.029x(100-6)/100 = 0.96726`. **Remember** to save the filament profile.  

![pass2](https://user-images.githubusercontent.com/103989404/210139072-f2fa91a6-4e3b-4d2a-81f2-c50155e1ff6d.jpg)
![-6](https://user-images.githubusercontent.com/103989404/210139131-ee224146-b242-4c1c-ac96-35ef0ca591f1.jpg)  
![image](./images/flowcalibration_update_flowrate.jpg)  

# Pressure Advance

Orca Slicer includes three approaches for calibrating the pressure advance value. Each method has its own advantages and disadvantages. It is important to note that each method has two versions: one for a direct drive extruder and one for a Bowden extruder. Make sure to select the appropriate version for your test.

> [!WARNING]
> For Marlin: Linear advance must be enabled in firmware (M900). **Not all printers have it enabled by default.**

> [!WARNING]
> For Bambulab X1/X1C users, make sure you do not select the 'Flow calibration' option when printings.
> 
> ![uncheck](https://user-images.githubusercontent.com/103989404/221345187-3c317a46-4d85-4221-99b9-adb5c7f48026.jpeg)

### Line method

The line method is quick and straightforward to test. However, its accuracy highly depends on your first layer quality. It is suggested to turn on the bed mesh leveling for this test.
Steps:
  1. Select the printer, filament, and process you would like to use for the test.
  2. Print the project and check the result. You can select the value of the most even line and update your PA value in the filament settings.
  3. In this test, a PA value of `0.016` appears to be optimal.
![pa_line](https://user-images.githubusercontent.com/103989404/210139630-8fd189e7-aa6e-4d03-90ab-84ab0e781f81.gif)

<img width="1003" alt="Screenshot 2022-12-31 at 12 11 10 PM" src="https://user-images.githubusercontent.com/103989404/210124449-dd828da8-a7e4-46b8-9fa2-8bed5605d9f6.png">

![line_0 016](https://user-images.githubusercontent.com/103989404/210140046-dc5adf6a-42e8-48cd-950c-5e81558da967.jpg)
![image](https://user-images.githubusercontent.com/103989404/210140079-61a4aba4-ae01-4988-9f8e-2a45a90cdb7d.png)

### Pattern method

The pattern method is adapted from [Andrew Ellis' pattern method generator](https://ellis3dp.com/Pressure_Linear_Advance_Tool/), which was itself derived from the [Marlin pattern method](https://marlinfw.org/tools/lin_advance/k-factor.html) developed by [Sineos](https://github.com/Sineos/k-factorjs).

[Instructions for using and reading the pattern method](https://ellis3dp.com/Print-Tuning-Guide/articles/pressure_linear_advance/pattern_method.html) are provided in [Ellis' Print Tuning Guide](https://ellis3dp.com/Print-Tuning-Guide/), with only a few Orca Slicer differences to note.

Test configuration window allow user to generate one or more tests in a single projects. Multiple tests will be placed on each plate with extra plates added if needed.

1. Single test \
![PA pattern single test](./images/pa/pa-pattern-single.png)
2. Batch mode testing (multiple tests on a sinle plate) \
![PA pattern batch mode](./images/pa/pa-pattern-batch.png)

Once test generated, one or more small rectangular prisms could be found on the plate, one for each test case. This object serves a few purposes:

1. The test pattern itself is added in as custom G-Code at each layer, same as you could do by hand actually. The rectangular prism gives us the layers in which to insert that G-Code. This also means that **you'll see the full test pattern when you move to the Preview pane**:
![PA pattern batch mode plater](./images/pa/pa-pattern-batch-plater.png)
2. The prism acts as a handle, enabling you to move the test pattern wherever you'd like on the plate by moving the prism
3. Each test object is pre-configured with target parameters which are reflected in the objects name. However, test parameters may be adjusted for each prism individually by referring to the object list pane:
![PA pattern batch mode object list](./images/pa/pa-pattern-batch-objects.png)

Next, Ellis' generator provided the ability to adjust specific printer, filament, and print profile settings. You can make these same changes in Orca Slicer by adjusting the settings in the Prepare pane as you would with any other print. When you initiate the calibration test, Ellis' default settings are applied. A few things to note about these settings:

1. Ellis specified line widths as a percent of filament diameter. The Orca pattern method does the same to provide its suggested defaults, making use of Ellis' percentages in combination with your specified nozzle diameter
2. In terms of line width, the pattern only makes use of the `Default` and `First layer` widths
3. In terms of speed, the pattern only uses the `First layer speed -> First layer` and `Other layers speed -> Outer wall` speeds
4. The infill pattern beneath the numbers cannot be changed becuase it's not actually an infill pattern pulled from the settings. All of the pattern G-Code is custom written, so that "infill" is, effectively, hand-drawn and so not processed through the usual channels that would enable Orca to recognize it as infill

### Tower method

The tower method may take a bit more time to complete, but it does not rely on the quality of the first layer. 
The PA value for this test will be increased by 0.002 for every 1 mm increase in height.  (**NOTE** 0.02 for Bowden)  
Steps:
 1. Select the printer, filament, and process you would like to use for the test.
 2. Examine each corner of the print and mark the height that yields the best overall result.
 3. I selected a height of 8 mm for this case, so the pressure advance value should be calculated as `PressureAdvanceStart+(PressureAdvanceStep x measured)` example: `0+(0.002 x 8) = 0.016`.
![tower](https://user-images.githubusercontent.com/103989404/210140231-e886b98d-280a-4464-9781-c74ed9b7d44e.jpg)

![tower_measure](https://user-images.githubusercontent.com/103989404/210140232-885b549b-e3b8-46b9-a24c-5229c9182408.jpg)

# Temp tower 
![image](./images/temp_tower_test.gif)  
Temp tower is a straightforward test. The temp tower is a vertical tower with multiple blocks, each printed at a different temperature. Once the print is complete, we can examine each block of the tower and determine the optimal temperature for the filament. The optimal temperature is the one that produces the highest quality print with the least amount of issues, such as stringing, layer adhesion, warping (overhang), and bridging.  
![temp_tower](https://user-images.githubusercontent.com/103989404/221344534-40e1a629-450c-4ad5-a051-8e240e261a51.jpeg)  

# Retraction test
![image](./images/retraction_test.gif)  
This test generates a retraction tower automatically. The retraction tower is a vertical structure with multiple notches, each printed at a different retraction length. After the print is complete, we can examine each section of the tower to determine the optimal retraction length for the filament. The optimal retraction length is the shortest one that produces the cleanest tower.  
![image](./images/retraction_test_dlg.png)  
In the dialog, you can select the start and end retraction length, as well as the retraction length increment step. The default values are 0mm for the start retraction length, 2mm for the end retraction length, and 0.1mm for the step. These values are suitable for most direct drive extruders. However, for Bowden extruders, you may want to increase the start and end retraction lengths to 1mm and 6mm, respectively, and set the step to 0.2mm.

**Note**: When testing filaments such as PLA or ABS that have minimal oozing, the retraction settings can be highly effective. You may find that the retraction tower appears clean right from the start. In such situations, setting the retraction length to 0.2mm - 0.4mm using Orca Slicer should suffice.
On the other hand, if there is still a lot of stringing at the top of the tower, it is recommended to dry your filament and ensure that your nozzle is properly installed without any leaks.  
![image](./images/retraction_test_print.jpg)  

# Orca Tolerance Test
This tolerance test is specifically designed to assess the dimensional accuracy of your printer and filament. The model comprises a base and a hexagon tester. The base contains six hexagon hole, each with a different tolerance: 0.0mm, 0.05mm, 0.1mm, 0.2mm, 0.3mm, and 0.4mm. The dimensions of the hexagon tester are illustrated in the image.  
![image](./images/tolerance_hole.jpg) 

You can assess the tolerance using either an M6 Allen key or the printed hexagon tester.  
![image](./images/OrcaToleranceTes_m6.jpg)  
![image](./images/OrcaToleranceTest_print.jpg)  

# Advanced Calibration

## Max Volumetric speed
This is a test designed to calibrate the maximum volumetric speed of the specific filament. The generic or 3rd party filament types may not have the correct volumetric flow rate set in the filament. This test will help you to find the maximum volumetric speed of the filament.

You will be promted to enter the settings for the test: start volumetric speed, end volumentric speed, and step. It is recommended to use the default values (5mm³/s start, 20mm³/s end, with a step of 0.5), unless you already have an idea of the lower or upper limit for your filament. Select "OK", slice the plate, and send it to the printer. 

Once printed, take note of where the layers begin to fail and where the quality begins to suffer. Pay attention to changes from matte to shiny as well. 

![image](./images/vmf_measurement_point.jpg)

Using calipers or a ruler, measure the height of the print at that point. Use the following calculation to determine the correct max flow value: `start + (height-measured * step)` . For example in the photo below, and using the default setting values, the print quality began to suffer at 19mm measured, so the calculation would be: `5 + (19 * 0.5)` , or `13mm³/s` using the default values. Enter your number into the  "Max volumetric speed" value in the filament settings.

![image](./images/caliper_sample_mvf.jpg)

You can also return to OrcaSlicer in the "Preview" tab, make sure the color scheme "flow" is selected. Scroll down to the layer height that you measured, and click on the toolhead slider. This will indicate the max flow level for your filmanet. 

![image](./images/max_volumetric_flow.jpg)

> [!NOTE]
> You may also choose to conservatively reduce the flow by 5-10% to ensure print quality.

## Input Shaping

During high-speed movements, vibrations can cause a phenomenon called "ringing," where periodic ripples appear on the print surface. Input Shaping provides an effective solution by counteracting these vibrations, improving print quality and reducing wear on components without needing to significantly lower print speeds.

### Klipper

### Resonance Compensation

The Klipper Resonance Compensation is a set of Input Shaping modes that can be used to reduce ringing and improve print quality.
Ussualy the recommended values modes are ``MZV`` or ``EI`` for Delta printers.

1. Pre-requisites:
   1. In OrcaSlicer, set:
      1. Acceleration high enough to trigger ringing (e.g., 2000 mm/s²).
      2. Speed high enough to trigger ringing (e.g., 100 mm/s).
      3. Jerk [Klipper Square Corner Velocity](https://www.klipper3d.org/Kinematics.html?h=square+corner+velocity#look-ahead) to 5 or a high value (e.g., 20).
   2. In printer settigs:
      1. Set the Shaper Type to ``MZV`` or ``EI``.
         ```
         SET_INPUT_SHAPER SHAPER_TYPE=MZV
         ```  
      2. Disable [Minimun Cruise Ratio](https://www.klipper3d.org/Kinematics.html#minimum-cruise-ratio) with:
            ```
            SET_VELOCITY_LIMIT MINIMUM_CRUISE_RATIO=0
            ```
   3. Use a high gloss filament to make the ringing more visible.
2. Print the Input Shaping Frequency test with a range of frequencies.
   1. Measure the X and Y heights and read the frequency set at that point in Orca Slicer.
   2. If not a clear result, you can measure a X and Y min and max acceptable heights and repeat the test with that min and max value.
   
   **Note**: There is a chance you will need to set higher than 60Hz frequencies. Some printers with very rigid frames and excellent mechanics may exhibit frequencies exceeding 100Hz.
3. Print the Damping test setting your X and Y frequency to the value you found in the previous step.
   1. Measure the X and Y heights and read the damping set at that point in Orca Slicer.
   **Note**: Not all Resonance Compensation modes support damping
1. Restore your 3D Printer settings to avoid keep using high acceleration and jerk values.
2. Save the settings
   1. You need to go to the printer settings and set the X and Y frequency and damp to the value you found in the previous step.

### Marlin

#### ZV Input Shaping

ZV Input Shaping introduces an anti-vibration signal into the stepper motion for the X and Y axes. It works by splitting the step count into two halves: the first at half the frequency and the second as an "echo," delayed by half the ringing interval. This simple approach effectively reduces vibrations, improving print quality and allowing for higher speeds.

1. Pre-requisites:
   1. In OrcaSlicer, set:
      1. Acceleration high enough to trigger ringing (e.g., 2000 mm/s²).
      2. Speed high enough to trigger ringing (e.g., 100 mm/s).
      3. Jerk
         1. If using [Classic Jerk](https://marlinfw.org/docs/configuration/configuration.html#jerk-) use a high value (e.g., 20).
         2. If using [Junction Deviation](https://marlinfw.org/docs/features/junction_deviation.html) (new Marlin default mode) this test will use 0.25 (high enough to most printers).
   2. Use a high gloss filament to make the ringing more visible.
2. Print the Input Shaping Frequency test with a range of frequencies.
   1. Measure the X and Y heights and read the frequency set at that point in Orca Slicer.
   2. If not a clear result, you can measure a X and Y min and max acceptable heights and repeat the test with that min and max value.
   
   **Note**: There is a chance you will need to set higher than 60Hz frequencies. Some printers with very rigid frames and excellent mechanics may exhibit frequencies exceeding 100Hz.
3. Print the Damping test setting your X and Y frequency to the value you found in the previous step.
   1. Measure the X and Y heights and read the damping set at that point in Orca Slicer.
4. Restore your 3D Printer settings to avoid keep using high acceleration and jerk values.
   1. Reboot your printer.
   2. Use the following G-code to restore your printer settings:
   ```gcode
   M501
   ```
5. Save the settings
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

#### Fixed-Time Motion

TODO This calibration test is currently under development.

### Junction Deviation

Junction Deviation is the default method for controlling cornering speed in MarlinFW printers.
Higher values result in more aggressive cornering speeds, while lower values produce smoother, more controlled cornering.
The default value in Marlin is typically set to 0.08mm, which may be too high for some printers, potentially causing ringing. Consider lowering this value to reduce ringing, but avoid setting it too low, as this could lead to excessively slow cornering speeds.

1. Pre-requisites:
   1. Check if your printer has Junction Deviation enabled. You can do this by sending the command `M503` to your printer and looking for the line `Junction deviation: 0.25`.
   2. In OrcaSlicer, set:
      1. Acceleration high enough to trigger ringing (e.g., 2000 mm/s²).
      2. Speed high enough to trigger ringing (e.g., 100 mm/s).
   3. Use a high gloss filament to make the ringing more visible.
2. You need to print the Junction Deviation test.
   1. Measure the X and Y heights and read the frequency set at that point in Orca Slicer.
   2. If not a clear result, you can measure a X and Y min and max acceptable heights and repeat the test with that min and max value.
3. Save the settings
   1. Use the following G-code to set the frequency:
   ```gcode
   M205 J#JunctionDeviationValue
   M500
   ```
   Example
   ```gcode
   M205 J0.013
   M500
   ```
   2. Set it in your Marlin Compilation.

## VFA

Vertical Fine Artifacts (VFA) are small artifacts that can occur on the surface of a 3D print, particularly in areas where there are sharp corners or changes in direction. These artifacts can be caused by a variety of factors, including mechanical vibrations, resonance, and other factors that can affect the quality of the print.
Because of the nature of these artifacts the methods to reduce them can be mechanical such as changing motors, belts and pulleys or with advanced calibrations such as Jerk/[Juction Deviation](#junction-deviation) corrections or [Input Shaping](#input-shaping).


***
*Credits:*  
- *The Flowrate test and retraction test is inspired by [SuperSlicer](https://github.com/supermerill/SuperSlicer)*  
- *The PA Line method is inspired by [K-factor Calibration Pattern](https://marlinfw.org/tools/lin_advance/k-factor.html)*     
- *The PA Tower method is inspired by [Klipper](https://www.klipper3d.org/Pressure_Advance.html)*
- *The temp tower model is remixed from [Smart compact temperature calibration tower](https://www.thingiverse.com/thing:2729076)
- *The max flowrate test was inspired by Stefan(CNC Kitchen), and the model used in the test is a remix of his [Extrusion Test Structure](https://www.printables.com/model/342075-extrusion-test-structure).
- *ZV Input Shaping is inspired by [Marlin Input Shaping](https://marlinfw.org/docs/features/input_shaping.html) and [Ringing Tower 3D STL](https://marlinfw.org/assets/stl/ringing_tower.stl)
- *ChatGPT* ;)
