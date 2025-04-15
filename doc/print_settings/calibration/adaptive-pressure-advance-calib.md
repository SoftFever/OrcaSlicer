# Adaptive Pressure Advance

This feature aims to dynamically adjust the printer’s pressure advance to better match the conditions the toolhead is facing during a print. Specifically, to more closely align to the ideal values as flow rate, acceleration, and bridges are encountered.  
This wiki page aims to explain how this feature works, the prerequisites required to get the most out of it as well as how to calibrate it and set it up.

## Settings Overview

This feature introduces the below options under the filament settings:

1. **Enable adaptive pressure advance:** This is the on/off setting switch for adaptive pressure advance.
2. **Enable adaptive pressure advance for overhangs:** Enable adaptive PA for overhangs as well as when flow changes within the same feature. This is an experimental option because if the PA profile is not set accurately, it will cause uniformity issues on the external surfaces before and after overhangs. It is recommended to start with this option switched off and enable it after the core adaptive pressure advance feature is calibrated correctly.
3. **Pressure advance for bridges:** Sets the desired pressure advance value for bridges. Set it to 0 to disable this feature. Experiments have shown that a lower PA value when printing bridges helps reduce the appearance of slight under extrusion immediately after a bridge, which is caused by the pressure drop in the nozzle when printing in the air. Therefore, a lower pressure advance value helps counteract this. A good starting point is approximately half your usual PA value.
4. **Adaptive pressure advance measurements:** This field contains the calibration values used to generate the pressure advance profile for the nozzle/printer. Input sets of pressure advance (PA) values and the corresponding volumetric flow speeds and accelerations they were measured at, separated by a comma. Add one set of values per line. More information on how to calibrate the model follows in the sections below.
5. **Pressure advance:** The old field is still needed and is required to be populated with a PA value. A “good enough” median PA value should be entered here, as this will act as a fallback value when performing tool changes, printing a purge/wipe tower for multi-color prints as well as a fallback in case the model fails to identify an appropriate value (unlikely but it’s the ultimate backstop).

<img width="452" alt="Adaptive PA settings" src="https://github.com/user-attachments/assets/68c46885-54c7-4123-afa0-762d3995185f">


## Pre-Requisites

This feature has been tested with Klipper-based printers. While it may work with Marlin or Bambu lab printers, it is currently untested with them. It shouldn’t adversely affect the machine; however, the quality results from enabling it are not validated.  

**Older versions of Klipper used to stutter when pressure advance was changed while the toolhead was in motion. This has been fixed with the latest Klipper firmware releases. Therefore, make sure your Klipper installation is updated to the latest version before enabling this feature, in order to avoid any adverse quality impacts.**

Klipper firmware released after July 11th, 2024 (version greater than approximately v0.12.0-267) contains the above fix and is compatible with adaptive pressure advance. If you are upgrading from an older version, make sure you update both your Klipper installation as well as reflash the printer MCU’s (main board and toolhead board if present).

## Use case (what to expect)

Following experimentation, it has been noticed that the optimal pressure advance value is less:

1. The faster you print (hence the higher the volumetric flow rate requested from the toolhead).
2. The larger the layer height (hence the higher the volumetric flow rate requested from the toolhead).
3. The higher the print acceleration is.

What this means is that we never get ideal PA values for each print feature, especially when they vary drastically in speed and acceleration. We can tune PA for a faster print speed (flow) but compromise on corner sharpness for slower speeds or tune PA for corner sharpness and deal with slight corner-perimeter separation in faster speeds. The same goes for accelerations as well as different layer heights.  

This compromise usually means that we settle for tuning an "in-between" PA value between slower external features and faster internal features so we don't get gaps, but also not get too much bulging in external perimeters. 

**However, what this also means is that if you are printing with a single layer height, single speed, and acceleration, there is no need to enable this feature.**

Adaptive pressure advance aims to address this limitation by implementing a completely different method of setting pressure advance. **Following a set of PA calibration tests done at different flow rates (speeds and layer heights) and accelerations, a pressure advance model is calculated by the slicer.** Then that model is used to emit the best fit PA for any arbitrary feature flow rate (speed) and acceleration used in the print process.  

In addition, it means that you only need to tune this feature once and print across different layer heights with good PA performance.  

Finally, if during calibration you notice that there is little to no variance between the PA tests, this feature is redundant for you. **From experiments, high flow nozzles fitted on high-speed core XY printers appear to benefit the most from this feature as they print with a larger range of flow rates and at a larger range of accelerations.**

### Expected results:

With this feature enabled there should be absolutely no bulge in the corners, just the smooth rounding caused by the square corner velocity of your printer.  
![337601149-cbd96b75-a49f-4dde-ab5a-9bbaf96eae9c](https://github.com/user-attachments/assets/01234996-0528-4462-90c6-43828a246e41)
In addition, seams should appear smooth with no bulging or under extrusion.  
![337601500-95e2350f-cffd-4af5-9c7a-e8f60870db7b](https://github.com/user-attachments/assets/46e16f2a-cf52-4862-ab06-12883b909615)
Solid infill should have no gaps, pinholes, or separation from the perimeters.  
![337616471-9d949a67-c8b3-477e-9f06-c429d4e40be0](https://github.com/user-attachments/assets/3b8ddbff-47e7-48b5-9576-3d9e7fb24a9d)
Compared to with this feature disabled, where the internal solid infill and external-internal perimeters show signs of separation and under extrusion, when PA is tuned for optimal external perimeter performance as shown below.
![337621601-eacc816d-cff0-42e4-965d-fb5c00d34205](https://github.com/user-attachments/assets/82edfd96-d870-48fe-91c7-012e8c0d9ed0)


## How to calibrate the adaptive pressure advance model

### Defining the calibration sets

Firstly, it is important to understand your printer speed and acceleration limits in order to set meaningful boundaries for the calibrations:

1. **Upper acceleration range:** Do not attempt to calibrate adaptive PA for an acceleration that is larger than what the Klipper input shaper calibration tool recommends for your selected shaper. For example, if Klipper recommends an EI shaper with 4k maximum acceleration for your slowest axis (usually the Y axis), don’t calibrate adaptive PA beyond that value. This is because after 4k the input shaper smoothing is magnified and the perimeter separations that appear like PA issues are caused by the input shaper smoothing the shape of the corner. Basically, you’d be attempting to compensate for an input shaper artefact with PA.
2. **Upper print speed range:** The Ellis PA pattern test has been proven to be the most efficient and effective test to run to calibrate adaptive PA. It is fast and allows for a reasonably accurate and easy-to-read PA value. However, the size of the line segments is quite small, which means that for the faster print speeds and slower accelerations, the toolhead will not be able to reach the full flow rate that we are calibrating against. It is therefore generally not recommended to attempt calibration with a print speed of higher than ~200-250mm/sec and accelerations slower than 1k in the PA pattern test. If your lowest acceleration is higher than 1k, then proportionally higher maximum print speeds can be used.

**Remember:** With the calibration process, we aim to create a PA – Flow Rate – Acceleration profile for the toolhead. As we cannot directly control flow rate, we use print speed as a proxy (higher speed -> higher flow).  

With the above in mind, let’s create a worked example to identify the optimal number of PA tests to calibrate the adaptive PA model. 

**The below starting points are recommended for the majority of Core XY printers:**

1. **Accelerations:** 1k, 2k, 4k
2. **Print speeds:** 50mm/sec, 100mm/sec, 150mm/sec, 200mm/sec.

**That means we need to run 3x4 = 12 PA tests and identify the optimal PA for them.**

Finally, if the maximum acceleration given by input shaper is materially higher than 4k, run a set of tests with the higher accelerations. For example, if input shaper allows a 6k value, run PA tests as below:

1. **Accelerations:** 1k, 2k, 4k, 6k
2. **Print speeds:** 50mm/sec, 100mm/sec, 150mm/sec, 200mm/sec.

Similarly, if the maximum value recommended is 12k, run PA tests as below:

1. **Accelerations:** 1k, 2k, 4k, 8k, 12k
2. **Print speeds:** 50mm/sec, 100mm/sec, 150mm/sec, 200mm/sec.

So, at worst case you will need to run 5x4 = 20 PA tests if your printer acceleration is on the upper end! In essence, you want enough granularity of data points to create a meaningful model while also not overdoing it with the number of tests. So, doubling the speed and acceleration is a good compromise to arrive at the optimal number of tests.  
For this example, let’s assume that the baseline number of tests is adequate for your printer:

1. **Accelerations:** 1k, 2k, 4k
2. **Print speeds:** 50mm/sec, 100mm/sec, 150mm/sec, 200mm/sec.

We, therefore, need to run 12 PA tests as below:

**Speed – Acceleration**
  1. 50 – 1k
  2. 100 – 1k
  3. 150 – 1k
  4. 200 – 1k
  5. 50 – 2k
  6. 100 – 2k
  7. 150 – 2k
  8. 200 – 2k
  9. 50 – 4k
  10. 100 – 4k
  11. 150 – 4k
  12. 200 – 4k

### Identifying the flow rates from the print speed

#### OrcaSlicer 2.2.0 and later

Test parameters needed to build adaptive PA table are printed on the test sample:

<img width="452" alt="pa batch mode" src="https://github.com/user-attachments/assets/219c53b5-d53f-4360-963e-0985d9257bd7">

Test sample above was done with acceleration 12000 mm/s² and flow rate 27.13 mm³/s


#### OrcaSlicer 2.1.0 and older.

As mentioned earlier, **the print speed is used as a proxy to vary the extrusion flow rate**. Once your PA test is set up, change the gcode preview to “flow” and move the horizontal slider over one of the herringbone patterns and take note of the flow rate for different speeds.
![337939815-e358b960-cf96-41b5-8c7e-addde927933f](https://github.com/user-attachments/assets/21290435-6f2a-4a21-bcf0-28cd6ae1912a)


### Running the tests

#### General tips

It is recommended that the PA step is set to a small value, to allow you to make meaningful distinctions between the different tests – **therefore a PA step value of 0.001 is recommended.  **

**Set the end PA to a value high enough to start showing perimeter separation for the lowest flow (print speed) and acceleration test.** For example, for a Voron 350 using Revo HF, the maximum value was set to 0.05 as that was sufficient to show perimeter separation even at the slowest flow rates and accelerations.  

**If the test is too big to fit on the build plate, increase your starting PA value or the PA step value accordingly until the test can fit.** If the lowest value becomes too high and there is no ideal PA present in the test, focus on increasing the PA step value to reduce the number of herringbones printed (hence the size of the print).

<img width="402" alt="PA calibration parameters" src="https://github.com/user-attachments/assets/b411dc30-5556-4e7c-8c40-5279d3074eae">

#### OrcaSlicer 2.3.0 and newer

PA pattern calibration configuration window have been changed to simplify test setup. Now all is needed is to fill list of accelerations and speeds into relevant fields of the calibration window:

![PA pattern batch mode](./images/pa/pa-pattern-batch.png)

Test patterns generated for each acceleration-speed pair and all parameters are set accordingly. No additional actions needed from user side. Just slice and print all plates generated.

Refer to [Calibration Guide](./Calibration) for more details on batch mode calibration.

#### OrcaSlicer 2.2.0 and older

Setup your PA test as usual from the calibration menu in Orca slicer. Once setup, your PA test should look like the below:

<img width="437" alt="PA calibration test 1" src="https://github.com/user-attachments/assets/1e6159fe-c3c5-4480-95a1-4383f1fae422">
<img width="437" alt="Pa calibration test 2" src="https://github.com/user-attachments/assets/c360bb18-a97a-4f37-b5a3-bb0c67cac2b6">

Now input your identified print speeds and accelerations in the fields above and run the PA tests. 

**IMPORTANT:** Make sure your acceleration values are all the same in all text boxes. Same for the print speed values and Jerk (XY) values. Make sure your Jerk value is set to the external perimeter jerk used in your print profiles.  

#### Test results processing

Now run the tests and note the optimal PA value, the flow, and the acceleration. You should produce a table like this:

<img width="452" alt="calibration table" src="https://github.com/user-attachments/assets/9451e8e4-352f-4cfc-b835-dffa4420d580">

Concatenate the PA value, the flow value, and the acceleration value into the final comma-separated sets to create the values entered in the model as shown above.  

**You’re now done! The PA profile is created and calibrated!**

Remember to paste the values in the adaptive pressure advance measurements text box as shown below, and save your filament profile.

<img width="452" alt="pa profile" src="https://github.com/user-attachments/assets/e6e61d1b-e422-4a6a-88ff-f55e10f79900">


### Tips

#### Model input:

The adaptive PA model built into the slicer is flexible enough to allow for as many or as few increments of flow and acceleration as you want. Ideally, you want at a minimum 3x data points for acceleration and flow in order to create a meaningful model.  

However, if you don’t want to calibrate for flow, just run the acceleration tests and leave flow the same for each test (in which case you’ll input only 3 rows in the model text box). In this case, flow will be ignored when the model is used.  

Similarly for acceleration – in the above example you’ll input only 4 rows in the model text box, in which case acceleration will be ignored when the model is used.  

**However, make sure a triplet of values is always provided – PA value, Flow, Acceleration.**

#### Identifying the right PA:

Higher acceleration and higher flow rate PA tests are easier to identify the optimal PA as the range of “good” values is much narrower. It’s evident where the PA is too large, as gaps start to appear in the corner and where PA is too low, as the corner starts bulging.  

However, the lower the flow rate and accelerations are, the range of good values is much wider. Having examined the PA tests even under a microscope, what is evident, is that if you can’t distinguish a value as being evidently better than another (i.e. sharper corner with no gaps) with the naked eye, then both values are correct. In which case, if you can’t find any meaningful difference, simply use the optimal values from the higher flow rates.

- **Too high PA**  

![Too high PA](https://github.com/user-attachments/assets/ebc4e2d4-373e-42d5-af72-4d5bc81048ca)

- **Too low PA**  

![Too low PA](https://github.com/user-attachments/assets/6a2b6f16-7d1c-46d0-91f3-def5ed560318)

- **Optimal PA**  

![Optimal PA](https://github.com/user-attachments/assets/cd47cf2e-dd32-47b4-bbdd-1563de8849be)
