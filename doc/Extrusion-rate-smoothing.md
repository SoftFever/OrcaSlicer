<h1>Extrusion rate smoothing (known as pressure equalizer in Prusa Slicer)</h1>

Extrusion rate smoothing (ERS) aims to limit the rate of extrusion volume change to be below a user set threshold (the ERS value). It aims to assist the printer firmware internal motion planners, pressure advance and pressure advance smooth time in achieving the desired nozzle flow by reducing the stresses put on the extrusion system, especially when printing at high speeds and high accelerations.

![Screenshot 2023-09-18 at 22 44 26](https://github.com/SoftFever/OrcaSlicer/assets/59056762/281b9c78-9f5c-428e-86b9-509de099a3e7)

<h2>Theory:</h2>

Enabling this feature creates a small extrusion rate "ramp" by slowing down and ramping up print speeds prior to and after the features causing a sudden change in extrusion flow rate needs, such as overhangs and overhang perimeters. 
This happens by breaking down the line segments into smaller "chunks" proportional to the ERS segment length and reducing the print speed of that segment, so that the requested extrusion volumetric flow rate change is at or below the ERS threshold.

In summary, it takes the "edge" off rapid extrusion changes caused by acceleration/deceleration. It reduces wall artefacts that show when the print speeds change suddenly, because the extruder cannot perfectly adhere to the requested by the firmware flow rates, especially when the extrusion rate is changing rapidly. 

The below artefact is mitigated through the use of ERS.
![ERS Disabled](https://github.com/SoftFever/OrcaSlicer/assets/59056762/31fdbf91-2067-4286-8bc1-4f7de4a628b6)

The bulging visible above is due to the extruder not being able to respond fast enough against the required speed change when printing with high accelerations and high speeds and requested to slow down for an overhang. 
In the above scenario, the printer (Bambu Lab X1 Carbon) was requested to slow down from a 200mm/sec print speed to 40mm/sec at an acceleration of 5k/sec2. The extruder could not keep up with the pressure change, resulting in a slight bump ahead at the point of speed change.

This parameter interacts with the below printer kinematic settings and physical limits:
1. The limits of the extruder system - how fast can it change pressure in the nozzle
2. The configured pressure advance values - that also affect pressure changes in the nozzle
3. The acceleration profile of the printer - higher accelerations mean higher pressure changes
4. The pressure advance smooth time (klipper) - higher smooth time means higher deviation from ideal extrusion, hence more opportunity for this feature to be useful.

<h3>Acceleration vs. Extrusion rate smoothing</h3>
A printer's motion system does not exactly follow the speed changes seen in the gcode preview screen of Orca slicer. When a speed change is requested, the look ahead planner of the firmware calculates the slow down needed in advance of that slower point and commences slowing down alead of time. The rate of slowdown is limited by the move's acceleration values. At 2k acceleration, slowing down from 200mm/sec to 40mm/sec would take approximately 9.6mm. This is derived from the following equation:

![image](https://github.com/igiannakas/OrcaSlicer/assets/59056762/4ba0356b-49ab-428c-ab10-f2c88bcc1bcb)

![image](https://github.com/igiannakas/OrcaSlicer/assets/59056762/3958deb5-fbc3-4d07-8903-4575033717fd)

The time taken to declerate to this new speed would be 0.08 seconds, derived from the following equation:

![image](https://github.com/igiannakas/OrcaSlicer/assets/59056762/ea9f19b4-defe-4656-9ecc-a6576c87d8e0)

A printer printing at 200mm/sec with a 0.42 line width and 0.16 layer height would be extruding plastic at approx 12.16mm3/sec as can also seen from the below visual.

![image](https://github.com/igiannakas/OrcaSlicer/assets/59056762/83242b26-7174-4da1-b815-d9fcec767bcd)

When the printer is extruding at 40mm/sec with the same line width and layer height as above, the flow rate is 2.43mm3/sec.

So what we are asking the extruder to do is slow down from 12.16mm3/sec flow to 2.43mm3/sec flow in 0.08 seconds or an extrusion change rate of 121mm3/sec2. 

**This value is proportional to the acceleration of the printer. At 4k this value doubles, at 1k it is half and is independant of the speed of movement or starting and ending speeds.**

**This value is also proportional to the line width - double the line width will result in double the extrusion rate change and vice versa.**

So, continuing with the worked example, a 2k acceleration produces an extrusion rate change ramp of 121mm3/sec2. **Therefore, setting a value higher than this would not bring any benefit to the print quality as the motion system would slow down less aggressively based on its acceleration limits.**

<h3>Pressure advance vs extrusion rate smoothing</h3>

Then we need to consider pressure advance and smooth time. 

**Pressure Advance** adjusts the extruder's speed to account for the pressure changes inside the hotend's melt zone. When the print head moves and extrudes filament, there's a delay between the movement of the extruder gear and the plastic actually being extruded due to the compressibility of the molten plastic in the hotend. This delay can cause too much plastic to be extruded when the print head starts moving or not enough plastic when the print head stops, leading to issues like blobbing or under-extrusion. This 

**Pressure Advance Smooth time** helps to mitigate potential negative effects on print quality due to the rapid changes in extruder flow rate, which are controlled by the Pressure Advance algorithm. This parameter essentially adds a smoothing effect to the adjustments made by Pressure Advance, aiming to prevent sharp or sudden changes in the extrusion rate.

When Pressure Advance adjusts the extruder speed to compensate for the pressure build-up or reduction in the hotend, it can lead to abrupt changes in the flow rate. These abrupt changes can potentially cause issues like:

1. Extruder motor skipping,
2. Increased wear on the extruder gear and filament,
3. Visible artifacts on the print surface due to non-uniform extrusion.

The smooth time setting introduces a controlled delay over which the Pressure Advance adjustments are spread out. This results in a more gradual application or reduction of extrusion pressure, leading to smoother transitions in filament flow.

**1. Increasing Smooth Time:** Leads to more gradual changes in extrusion pressure. While this can reduce artifacts and stress on the extruder system, setting it too high may diminish the effectiveness of Pressure Advance, as the compensation becomes too delayed to counteract the pressure dynamics accurately.
**2. Decreasing Smooth Time:** Makes the Pressure Advance adjustments more immediate, which can improve the responsiveness of pressure compensation but may also reintroduce abrupt changes in flow rate, potentially leading to the issues mentioned above.

In essence, pressure advance smooth time creates an intentional deviation from the ideal extruder rotation and, as a consequence, extrusion amount, to allow the printer's extruder to perform within its mechanical limits. Typically this value is set to 0.04sec, which means that when Pressure Advance makes adjustments to the extruder's flow rate to compensate for changes in pressure within the hotend, these adjustments are spread out over a period of 0.04 seconds. 

In the worked example above, **we need to set an Extrusion Rate smoothing value enough to decrease the error introduced by pressure advance smooth time to the produced output flow.** The lower the extrusion rate smoothing value, the lower the changes in flow hence the lower the deviation from the ideal extrusion caused by the smooth time algorithm.

<h2>Finding the ideal Extrusion Rate smoothing value:</h2>

Firstly, this value needs to be lower than the extrusion rate changes resulting from the acceleration profile of the printer. As, generaly, the greatest impact is in external wall finish, use your external perimeter acceleration as a point of reference. Below are some approximate ERS values for a given acceleration and 0.42 line width
1. 30mm3/sec for 0.5k acceleration
2. 60.5mm3/sec for 1k acceleration
3. 121mm3/sec2 for 2k acceleration
4. 242mm3/sec2 for 4k acceleration

For 0.45 line width the below are approximate ERS values:
1. 32mm3/sec for 0.5k acceleration
2. 65mm3/sec for 1k acceleration
3. 129mm3/sec2 for 2k acceleration
4. 260mm3/sec2 for 4k acceleration

So your tuning starting point needs to be an ERS value that is less than this. A good point experiment with test prints would be a value of 60-80% of the above maximum values. This will give some meaningfull assistance to pressure advance, reducing the deviation introduced by pressure advance smooth time. The greater the smooth time, the greater the quality benefit will be.

Therefore, for a 0.42 line width, the below are a recommended set of starting ERS values
1. 18-25mm3/sec for 0.5k acceleration
2. 35-50mm3/sec for 1k acceleration
3. 70-100mm3/sec2 for 2k acceleration
4. 145-200mm3/sec2 for 4k acceleration

Perform a test print with the above and adjust to your liking!

<h2>A note for bowden printers using marlin without pressure advance. </h2>
If your printer is not equipped with pressure advance and especially if you are using a bowden setup, you dont have the benefit of pressure advance adjusting your flow dynamically based on print speed and accelerations. In this special case, ERS will be doing all the heavy lifting that pressure advance would typically perform. In this scenario a low value of 8-10mm3/sec is usually recomended, irrespective of your acceleration settings, to smooth out pressure changes in the extrusion system as much as possible without impacting print speed too much. 


