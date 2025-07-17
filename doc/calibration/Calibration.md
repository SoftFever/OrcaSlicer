# Calibration Guide

This guide offers a structured and comprehensive overview of the calibration process for Orca Slicer.

It covers key aspects such as flow rate, pressure advance, temperature towers, retraction tests, and advanced calibration techniques. Each section includes step-by-step instructions and visuals to help you better understand and carry out each calibration effectively.

To access the calibration features, you can find them in the **Calibration** section of the Orca Slicer interface.

![Calibration Button](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/calibration.png?raw=true)

> [!IMPORTANT]
> After completing the calibration process, remember to create a new project in order to exit the calibration mode.

The recommended order for calibration is as follows:

1. **[Temperature](temp-calib):** Start by calibrating the temperature of the nozzle and the bed. This is crucial as it affects the viscosity of the filament, which in turn influences how well it flows through the nozzle and adheres to the print bed.

   <img src="https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/Temp-calib/temp-tower.jpg?raw=true" alt="temp-tower" height="200">

2. **[Flow](flow-rate-calib):** Calibrate the flow rate to ensure that the correct amount of filament is being extruded. This is important for achieving accurate dimensions and good layer adhesion.

   <img src="https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/Flow-Rate/flowrate-6.jpg?raw=true" alt="flowrate-6" height="200">

1. **[Pressure Advance](pressure-advance-calib):** Calibrate the pressure advance settings to improve print quality and reduce artifacts caused by pressure fluctuations in the nozzle.

   - **[Adaptive Pressure Advance](adaptive-pressure-advance-calib):** This is an advanced calibration technique that can be used to further optimize the pressure advance settings for different print speeds and geometries.

      <img src="https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/pa/pa-tower.jpg?raw=true" alt="pa-tower" height="200">

2. **[Retraction](retraction-calib):** Calibrate the retraction settings to minimize stringing and improve print quality. Doing this after Flow and Pressure Advance calibration is recommended, as it ensures that the printer is already set up for optimal extrusion.

   <img src="https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/retraction/retraction_test_print.jpg?raw=true" alt="Retraction" height="200">

3. **[Tolerance](tolerance-calib):** Calibrate the tolerances of your printer to ensure that it can accurately reproduce the dimensions of the model being printed. This is important for achieving a good fit between parts and for ensuring that the final print meets the desired specifications.

   <img src="https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/Tolerance/OrcaToleranceTes_m6.jpg?raw=true" alt="Tolerance" height="200">

4. **[Max Volumetric Speed](volumetric-speed-calib):** Calibrate the maximum volumetric speed of the filament. This is important for ensuring that the printer can handle the flow rate of the filament without causing issues such as under-extrusion or over-extrusion.

   <img src="https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/MVF/mvf_measurement_point.jpg?raw=true" alt="Max_Volumetric_Speed" height="200">

5. **[Cornering](cornering-calib):** Calibrate the Jerk/Junction Deviation settings to improve print quality and reduce artifacts caused by sharp corners and changes in direction.

     <img src="https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/JunctionDeviation/jd_second_print_measure.jpg?raw=true" alt="Cornering" height="200">

6. **[Input Shaping](input-shaping-calib):** This is an advanced calibration technique that can be used to reduce ringing and improve print quality by compensating for mechanical vibrations in the printer.

   <img src="https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/InputShaping/IS_damp_marlin_print_measure.jpg?raw=true" alt="Input_Shaping" height="200">

7. **[VFA](vfa-calib):** A VFA speed test is available to find resonance speeds.

   <img src="https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/vfa/vfa_test_print.jpg?raw=true" alt="vfa_test_print" height="200">

---

_Credits:_

- _The Flowrate test and retraction test is inspired by [SuperSlicer](https://github.com/supermerill/SuperSlicer)._
- _The PA Line method is inspired by [K-factor Calibration Pattern](https://marlinfw.org/tools/lin_advance/k-factor.html)._
- _The PA Tower method is inspired by [Klipper](https://www.klipper3d.org/Pressure_Advance.html)._
- _The temp tower model is remixed from [Smart compact temperature calibration tower](https://www.thingiverse.com/thing:2729076)._
- _The max flowrate test was inspired by Stefan (CNC Kitchen), and the model used in the test is a remix of his [Extrusion Test Structure](https://www.printables.com/model/342075-extrusion-test-structure)._
- _ZV Input Shaping is inspired by [Marlin Input Shaping](https://marlinfw.org/docs/features/input_shaping.html) and [Ringing Tower 3D STL](https://marlinfw.org/assets/stl/ringing_tower.stl)._
