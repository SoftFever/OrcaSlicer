# Flow rate

The Flow Ratio determines how much filament is extruded and plays a key role in achieving high-quality prints. A properly calibrated flow ratio ensures consistent layer adhesion and accurate dimensions.

- Too **low** flow ratio will cause under-extrusion, leading to gaps, weak layers, and poor structural integrity.
- Too **high** flow ratio can cause over-extrusion, resulting in excess material, rough surfaces, and dimensional inaccuracies.

- [Calibration Types](#calibration-types)
  - [OrcaSlicer \> 2.3.0 Archimedean chords + YOLO (Recommended)](#orcaslicer--230-archimedean-chords--yolo-recommended)
  - [OrcaSlicer \<= 2.3.0 Monotonic Line + 2-Pass Calibration](#orcaslicer--230-monotonic-line--2-pass-calibration)
- [Credits](#credits)

> [!WARNING]
> **Bambulab Printers:** make sure you do not select the 'Flow calibration' option.
> ![flowrate-Bambulab-uncheck](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/Flow-Rate/flowrate-Bambulab-uncheck.png?raw=true)

> [!NOTE]
> After v2.3.0, the [Top Pattern](strength_settings_top_bottom_shells#surface-pattern) changed to [Archimedean chords](strength_settings_infill#archimedean-chords) from Monotonic Line.

## Calibration Types

- **2-Pass Calibration:** Old method using two passes to determine the optimal flow rate using the formula `FlowRatio_old*(100 + modifier)/100`.
- **YOLO:** Simplified method that adjusts the flow rate in a single pass using the formula `FlowRatio_old±modifier`.
  - **Recommended:**  Calibration range `[-0.05, +0.05]`, flow rate step is `0.01`.
  - **Perfectionist Version:** Calibration range `[-0.04, +0.035]`, flow rate step is `0.005`.

### OrcaSlicer > 2.3.0 Archimedean chords + YOLO (Recommended)

This example demonstrates the use of [Archimedean chords](strength_settings_infill#archimedean-chords) for flow rate calibration using the YOLO (Recommended) method.

WIP...

![flowcalibration-yolo](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/Flow-Rate/flowcalibration-yolo.gif?raw=true)

### OrcaSlicer <= 2.3.0 Monotonic Line + 2-Pass Calibration

This example demonstrates the use of Monotonic Line for flow rate calibration using the 2-Pass Calibration method.

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

## Credits

- **[Archimedean Chords Idea](https://makerworld.com/es/models/189543-improved-flow-ratio-calibration-v3#profileId-209504)**: [Jimcorner](https://makerworld.com/es/@jimcorner)
