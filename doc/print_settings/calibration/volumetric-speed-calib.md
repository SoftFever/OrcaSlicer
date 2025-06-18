# Max Volumetric speed

This is a test designed to calibrate the maximum volumetric speed of the specific filament. The generic or 3rd party filament types may not have the correct volumetric flow rate set in the filament. This test will help you to find the maximum volumetric speed of the filament.

You will be promted to enter the settings for the test: start volumetric speed, end volumentric speed, and step. It is recommended to use the default values (5mm³/s start, 20mm³/s end, with a step of 0.5), unless you already have an idea of the lower or upper limit for your filament. Select "OK", slice the plate, and send it to the printer.

Once printed, take note of where the layers begin to fail and where the quality begins to suffer. Pay attention to changes from matte to shiny as well.

![vmf_measurement_point](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images//vmf_measurement_point.jpg?raw=true)

Using calipers or a ruler, measure the height of the print at that point. Use the following calculation to determine the correct max flow value: `start + (height-measured * step)` . For example in the photo below, and using the default setting values, the print quality began to suffer at 19mm measured, so the calculation would be: `5 + (19 * 0.5)` , or `13mm³/s` using the default values. Enter your number into the "Max volumetric speed" value in the filament settings.

![caliper_sample_mvf](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images//caliper_sample_mvf.jpg?raw=true)

You can also return to OrcaSlicer in the "Preview" tab, make sure the color scheme "flow" is selected. Scroll down to the layer height that you measured, and click on the toolhead slider. This will indicate the max flow level for your filmanet.

![image](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/max_volumetric_flow.jpg?raw=true)

> [!NOTE]
> You may also choose to conservatively reduce the flow by 5-10% to ensure print quality.

> [!TIP]
> @ItsDeidara has made a html to help with the calculation. Check it out if those equations give you a headache [here](https://github.com/ItsDeidara/Orca-Slicer-Assistant).
