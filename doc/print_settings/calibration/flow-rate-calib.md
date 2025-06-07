# Flow rate

The Flow Ratio determines how much filament is extruded and plays a key role in achieving high-quality prints. A properly calibrated flow ratio ensures consistent layer adhesion and accurate dimensions. If the flow ratio is too low, under-extrusion may occur, leading to gaps, weak layers, and poor structural integrity. On the other hand, a flow ratio that is too high can cause over-extrusion, resulting in excess material, rough surfaces, and dimensional inaccuracies.

> [!WARNING]
> For Bambulab X1/X1C users, make sure you do not select the 'Flow calibration' option.

> ![uncheck](https://user-images.githubusercontent.com/103989404/221345187-3c317a46-4d85-4221-99b9-adb5c7f48026.jpeg)

> [!IMPORTANT]
> PASS 1 and PASS 2 follow the older flow ratio formula `FlowRatio_old*(100 + modifier)/100`. YOLO (Recommended) and YOLO (perfectist version) use a new system that is very simple `FlowRatio_old±modifier`.

![flowrate](../../images/flow-calibration.gif)

Calibrating the flow rate involves a two-step process.

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
![image](../../images/flowcalibration_update_flowrate.png)

> [!TIP]
> @ItsDeidara has made a html to help with the calculation. Check it out if those equations give you a headache [here](https://github.com/ItsDeidara/Orca-Slicer-Assistant).