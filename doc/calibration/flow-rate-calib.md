# Flow rate

The Flow Ratio determines how much filament is extruded and plays a key role in achieving high-quality prints. A properly calibrated flow ratio ensures consistent layer adhesion and accurate dimensions. If the flow ratio is too low, under-extrusion may occur, leading to gaps, weak layers, and poor structural integrity. On the other hand, a flow ratio that is too high can cause over-extrusion, resulting in excess material, rough surfaces, and dimensional inaccuracies.

- [OrcaSlicer \> 2.3.0 Archimedean chords](#orcaslicer--230-archimedean-chords)
- [OrcaSlicer \<= 2.3.0 Monotonic Line](#orcaslicer--230-monotonic-line)

> [!WARNING]
> **Bambulab Printers:** make sure you do not select the 'Flow calibration' option.
> ![flowrate-Bambulab-uncheck](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/Flow-Rate/flowrate-Bambulab-uncheck.png?raw=true)

> [!IMPORTANT]
> PASS 1 and PASS 2 follow the older flow ratio formula `FlowRatio_old*(100 + modifier)/100`.  
> YOLO (Recommended) and YOLO (perfectionist version) use a new system that is very simple `FlowRatio_old±modifier`.

## OrcaSlicer > 2.3.0 Archimedean chords

![flowrate-pass1](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/Flow-Rate/flowrate-pass1.jpg?raw=true)

## OrcaSlicer <= 2.3.0 Monotonic Line

![flow-calibration-monotonic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/Flow-Rate/monotonic-flow-rate/flow-calibration-monotonic.gif?raw=true)

Calibrating the flow rate involves a two-step process.

1. Select the printer, filament, and process you would like to use for the test.
2. Select `Pass 1` in the `Calibration` menu
3. A new project consisting of nine blocks will be created, each with a different flow rate modifier. Slice and print the project.
4. Examine the blocks and determine which one has the smoothest top surface.
   ![flowrate-pass1-monotonic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/Flow-Rate/monotonic-flow-rate/flowrate-pass1-monotonic.jpg?raw=true)

   ![flowrate-0-5-monotonic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/Flow-Rate/monotonic-flow-rate/flowrate-0-5-monotonic.jpg?raw=true)

5. Update the flow ratio in the filament settings using the following equation: `FlowRatio_old*(100 + modifier)/100`. If your previous flow ratio was `0.98` and you selected the block with a flow rate modifier of `+5`, the new value should be calculated as follows: `0.98x(100+5)/100 = 1.029`. **Remember** to save the filament profile.
6. Perform the `Pass 2` calibration. This process is similar to `Pass 1`, but a new project with ten blocks will be generated. The flow rate modifiers for this project will range from `-9 to 0`.
7. Repeat steps 4. and 5. In this case, if your previous flow ratio was 1.029 and you selected the block with a flow rate modifier of -6, the new value should be calculated as follows: `1.029x(100-6)/100 = 0.96726`. **Remember** to save the filament profile.

![flowrate-pass2-monotonic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/Flow-Rate/monotonic-flow-rate/flowrate-pass2-monotonic.jpg?raw=true)

![flowrate-6-monotonic](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/Flow-Rate/monotonic-flow-rate/flowrate-6-monotonic.jpg?raw=true)

![flowcalibration_update_flowrate](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/Flow-Rate/flowcalibration_update_flowrate.png?raw=true)

> [!TIP]
> @ItsDeidara has made a html to help with the calculation. Check it out if those equations give you a headache [here](https://github.com/ItsDeidara/Orca-Slicer-Assistant).
