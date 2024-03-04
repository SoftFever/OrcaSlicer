**Extrusion rate smoothing (known as pressure equalizer in Prusa Slicer)**

Extrusion rate smoothing (ERS) aims to limit the rate of extrusion volume change to be below a user set threshold (the ERS value). It aims to assist the printer firmware internal motion planners, pressure advance and pressure advance smooth time in achieving the desired nozzle flow by reducing the stresses put on the extrusion system, especially when printing at high speeds and high accelerations.

**Theory:**

Enabling this feature creates a small extrusion rate "ramp" by slowing down and ramping up print speeds prior to and after the features causing a sudden change in extrusion flow rate needs. This happens by breaking down the line segments into smaller "chunks" proportional to the ERS segment length and reducing the print speed of that segment, so that the requested extrusion volumetric flow rate change is at or below the ERS threshold.

In summart, it takes the "edge" off pressure advance. It reduces wall artefacts that show when the print speeds change suddenly, because the extruder cannot perfectly adhere to the requested by the firmware flow rates, especially when the extrusion rate is changing rapidly. 

The below artefact is mitigated through the use of ERS
https://private-user-images.githubusercontent.com/59056762/270168527-31fdbf91-2067-4286-8bc1-4f7de4a628b6.jpeg?jwt=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJnaXRodWIuY29tIiwiYXVkIjoicmF3LmdpdGh1YnVzZXJjb250ZW50LmNvbSIsImtleSI6ImtleTUiLCJleHAiOjE3MDk1NjgyNjQsIm5iZiI6MTcwOTU2Nzk2NCwicGF0aCI6Ii81OTA1Njc2Mi8yNzAxNjg1MjctMzFmZGJmOTEtMjA2Ny00Mjg2LThiYzEtNGY3ZGU0YTYyOGI2LmpwZWc_WC1BbXotQWxnb3JpdGhtPUFXUzQtSE1BQy1TSEEyNTYmWC1BbXotQ3JlZGVudGlhbD1BS0lBVkNPRFlMU0E1M1BRSzRaQSUyRjIwMjQwMzA0JTJGdXMtZWFzdC0xJTJGczMlMkZhd3M0X3JlcXVlc3QmWC1BbXotRGF0ZT0yMDI0MDMwNFQxNTU5MjRaJlgtQW16LUV4cGlyZXM9MzAwJlgtQW16LVNpZ25hdHVyZT0wZTczZGJiZDE1NDFmNjI5NWFkYTBjMzg4YWFkZDllOTA5YWE2MDFmOGU3YzRlN2ZkZTgyMTFiNjU4YjNlNzM3JlgtQW16LVNpZ25lZEhlYWRlcnM9aG9zdCZhY3Rvcl9pZD0wJmtleV9pZD0wJnJlcG9faWQ9MCJ9.fRxgHI5twW6mIKU92raWjtOll1a-vgUTP0EJttsArXo


to cover for the deficiency in its internal jerk implementation which is worse in my view compared to the Klipper SCV implementation and aggressive PA smoothing. Hence the high value recommendation for BBL stock profiles. Experimentally it worked well - taking the edge off the artefacts.



When a printer transitions from high speed, high flow printing to a slower printed feature, the extruder is required to reduce its rotation and consequence, nozzle flow by potentially an am

Yeap the tool tip could do with an update. I've written up my thoughts below (sorry for the post length!!!) but may be wrong as this is a pretty complicated topic, especially how it interacts with internal motion planners, PA and PA smooth time in the klipper/BBL firmwares. 


That is expected as a higher speed transition results in a higher flow change, meaning you need a higher ERS value to maintain the same smoothing over the same distance.

However this may not be the right answer as we are trying to fight:
1. The limits of the extruder system - how fast can it change pressure in the nozzle
2. The configured pressure advance values - that also affect pressure changes in the nozzle
3. The acceleration profile of the printer - higher accelerations mean higher pressure changes
4. The pressure advance smooth time - higher smooth time means higher deviation from ideal extrusion, hence more opportunity for this feature to be useful.

When I ported it, I targeted BBL printers mostly which, because of the fast external perimeter speed and high accelerations and less than ideal PA smoothing implementation, needed high ERS values to take the "edge" off PA artefacting. Basically providing a small "ramp" to slow down and ramp up speeds to cover for the deficiency in its internal jerk implementation which is worse in my view compared to the Klipper SCV implementation and aggressive PA smoothing. Hence the high value recommendation for BBL stock profiles. Experimentally it worked well - taking the edge off the artefacts.

**However, this feature won't make a difference if the extrusion rate smoothing slope results in speed changes that are less than what the internal motion planner of the firmware plans during acceleration/deceleration moves.** For example, when transitioning from a high speed area to a low speed area there is no "sudden" stop as you're bound by the print acceleration, so what you see on the speed view in Orca isn't exactly what is happening when printing the model. For a BBL printer printing at 10k+ speeds the ERS value can be afforded to be higher as the deceleration-> acceleration slope will result in more sudden extrusion rate changes compared to a print with more moderate accelerations.

**Then the matter of Pressure Advance smooth time comes in.** This is the amount of time allowed to the extruder to smooth out a newly requested extrusion rate value in Klipper. (https://klipper.discourse.group/t/pressure-advance-smooth-time-on-direct-extruders-with-short-filament-path/1971/6) **This results in deviations from the ideal nozzle pressure, as the extruder cannot move instantaneously, hence why its extrusion rate change is smoothed over time. This deviation is what we are trying to mitigate here in areas of sudden speed change.**

Ideally, I think that you'd want extrusion rate smoothing to be resulting in **extrusion rate changes that are smaller than the PA smooth time value** for it to have any meaningful effect (hence speed changes that are larger than what the planner would produce). I think that if the ERS flow changes are over the PA smooth time threshold the look ahead planner will reduce extrusion rate faster than the ERS smoothing moves hence making it redundant.

So in summary **it may make more sense to go conservative in this value** to allow it to have an effect on the print. For a fast accelerating printer with PA smoothing that is close to 0, as is the BBL printers (which result in artefacts when slowing down quickly), it may make sense to keep the value high, like 200/300 or so.

**For a Klipper printer, especially if you have PA smooth time of 0.04** which is the default and are printing with more conservative accelerations (like 2-6k), maybe it makes sense to keep the value at a more moderate level, like 100 or so or even lower.

For a slower printer with no PA and limited accelerations, a much lower value makes more sense - like 10-15 or so.

This should be solvable with math, however it makes my head spin just thinking of it :) You have a **speed ramp** due to the acceleration profile, that results in an **extrusion rate ramp** based on the PA value which **is smoothed out** over PA smooth time. **Ideally you want the ERS value to be below the computed flow changes from the above.**

So where does this leave us? Lower values are more likely to produce a meaningful result, but going too low will slow down the print more than needed to take the edge off these artefacts...

**Personally I use it on occasion on the BBL X1C where models show the need for it** (sharp speed changes on external walls) but in these cases I may just print the perimeter slower anyway to get better quality as loosing arcs is a bigger issue quality wise on that printer (steppers exhibit more VFA without arc moves). **When I do use it I have settled on a value of 200 with 3 as a segment (to avoid overloading the MCU).**

**On the V2.4 I dont use it most of the time as I haven't seen the same level of artefacts as on the X1C** on external perimeters but then I am using danger klipper with extrusion rate sync to IS and a low PA smooth time value of 0.01s and a nozzle that doesnt take much to pressurise (not a UHF style nozzle that needs to push plenty of material to ramp up pressure).  **When I do use it on the 2.4 I have settled on a value of around 100 with 1 segment.**

**With the above in mind, a new tool tip could be:** 

This parameter smooths out sudden extrusion rate changes that happen when the printer transitions from printing a high flow (high speed/larger width) extrusion to a lower flow (lower speed/smaller width) extrusion and vice versa.

It defines the maximum rate by which the extruded volumetric flow in mm3/sec can change over time. Higher values mean higher extrusion rate changes are allowed, resulting in faster speed transitions. 

A value of 0 disables the feature.

For a high speed, high flow direct drive printer (like the Bambu lab or Voron), especially with a well tuned PA and PA smooth time settings, this value is usually not needed. However, it can provide some marginal benefit in certain cases where feature speeds vary greatly, such as in overhangs or where the extruder and/or hot end cannot keep up with the requested flow changes as a result of pressure advance.

In these cases, if printing with high accelerations (10k+) and high speeds (200mm/sec+) a value of around 200-300mm3/sec2 is recommended as it allows for just enough smoothing to assist pressure advance achieve a smoother flow transition.

In cases where more moderate accelerations are used (2-5k) and moderate speeds (100mm/sec) a value of around 90-120mm3/sec2 is recommended.

Finally for printers not utilising pressure advance a low value of around 10-15mm3/s2 is a good starting point. These printers will benefit the most from this feature due to the lack of pressure advance in the firmware.

This feature is known as Pressure Equalizer in Prusa slicer.

Note: this parameter disables arc fitting.


