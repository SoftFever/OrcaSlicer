- [Flow rate](#Flow-rate)
- [Pressure Advance](#Pressure-Advance)
  1. [Line method](#Line-method)
  2. [Tower method](#Tower-method)
- [Temp tower](#Temp-tower)
- [Retraction test](#Retraction-test)
- [Orca Tolerance Test](#Orca-Tolerance-Test)
- [Advanced calibration]
  1. [Max Volumetric speed]
  2. [VFA]  

**NOTE**: After completing the calibration process, remember to create a new project in order to exit the calibration mode.
# Flow rate
 ##### *NOTE: For Bambulab X1/X1C users, make sure you do not select the 'Flow calibration' option.*  
 ![uncheck](https://user-images.githubusercontent.com/103989404/221345187-3c317a46-4d85-4221-99b9-adb5c7f48026.jpeg)  
----------------------------------------
![flowrate](https://user-images.githubusercontent.com/103989404/210137579-3fd141ad-f2da-4542-a1fd-fc4b4d673908.gif)
Calibrating the flow rate involves a two-step process.  
Steps
1. Select the printer, filament, and process you would like to use for the test.
2. Select `Pass 1` in the `Calibration` menu
3. A new project consisting of nine blocks will be created, each with a different flow rate modifier. Slice and print the project.
4. Examine the blocks and determine which one has the smoothest top surface.
![flowrate-pass1_resize](https://user-images.githubusercontent.com/103989404/210138585-98821729-b19e-4452-a08d-697f147d36f0.jpg)
![0-5](https://user-images.githubusercontent.com/103989404/210138714-63daae9c-6778-453a-afa9-9a976d61bfd5.jpg)

5. Update the flow ratio in the filament settings using the following equation: `FlowRatio_old*(100 + modifier)/100`. If your previous flow ratio was `0.98` and you selected the block with a flow rate modifier of `+5`, the new value should be calculated as follows: `0.98x(100+5)/100 = 1.029`. ** Remember** to save the filament profile.
6. Perform the `Pass 2` calibration. This process is similar to `Pass 1`, but a new project with ten blocks will be generated. The flow rate modifiers for this project will range from `-9 to 0`.
7. Repeat steps 4 and 5. In this case, if your previous flow ratio was 1.029 and you selected the block with a flow rate modifier of -6, the new value should be calculated as follows: `1.029x(100-6)/100 = 0.96726`. ** Remember ** to save the filament profile.  
![pass2](https://user-images.githubusercontent.com/103989404/210139072-f2fa91a6-4e3b-4d2a-81f2-c50155e1ff6d.jpg)
![-6](https://user-images.githubusercontent.com/103989404/210139131-ee224146-b242-4c1c-ac96-35ef0ca591f1.jpg)
![image](https://user-images.githubusercontent.com/103989404/210139721-919be130-fbba-4e3a-aa58-8a563e8c7792.png)

# Pressure Advance
I will present two approaches for calibrating the pressure advance value. Both methods have their own advantages and disadvantages. It is important to note that each method has two versions: one for a direct drive extruder and one for a Bowden extruder. Make sure to select the appropriate version for your test.  
 ##### *NOTE: For Bambulab X1/X1C users, make sure you do not select the 'Flow calibration' option.*  
 ![uncheck](https://user-images.githubusercontent.com/103989404/221345187-3c317a46-4d85-4221-99b9-adb5c7f48026.jpeg)  
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

### Tower method
The tower method may take a bit more time to complete, but it does not rely on the quality of the first layer. 
The PA value for this test will be increased by 0.002 for every 1 mm increase in height.  (**NOTE** 0.02 for Bowden)  
Steps:
 1. Select the printer, filament, and process you would like to use for the test.
 2. Examine each corner of the print and mark the height that yields the best overall result.
 3. I selected a height of 8 mm for this case, so the pressure advance value should be calculated as `0.002x8 = 0.016`.
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

***
*Credits:*  
- *The Flowrate test and retraction test is inspired by [SuperSlicer](https://github.com/supermerill/SuperSlicer)*  
- *The PA Line method is inspired by [K-factor Calibration Pattern](https://marlinfw.org/tools/lin_advance/k-factor.html)*     
- *The PA Tower method is inspired by [Klipper](https://www.klipper3d.org/Pressure_Advance.html)*
- *The temp tower model is remixed from [Smart compact temperature calibration tower](https://www.thingiverse.com/thing:2729076)
- *The max flowrate test was inspired by Stefan(CNC Kitchen), and the model used in the test is a remix of his [Extrusion Test Structure](https://www.printables.com/model/342075-extrusion-test-structure).
- *chapgpt* ;)
