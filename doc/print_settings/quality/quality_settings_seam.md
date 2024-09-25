# Seam Section

Unless printed in spiral vase mode, every layer needs to begin somewhere and end somewhere. That start and end of the extrusion is what results in what visually looks like a seam on the perimeters. This section contains options to control the visual appearance of a seam.

- **Seam Position**: Controls the placement of the seam. 
  1. **Aligned**: Will attempt to align the seam to a hidden internal facet of the model.
  2. **Nearest**: Will place the seam at the nearest starting point compared to where the nozzle stopped printing in the previous layer.
  3. **Back**: Will align the seam in a (mostly) straight line at the rear of the model.
  4. **Random**: Will randomize the placement of the seam between layers.
  
  Typically, aligned or back work the best, especially in combination with seam painting. However, as seams create weak points and slight surface "bulges" or "divots," random seam placement may be optimal for parts that need higher strength as that weak point is spread to different locations between layers (e.g., a pin meant to fit through a hole).

- **Staggered Inner Seams**: As the seam location forms a weak point in the print (it's a discontinuity in the extrusion process after all!), staggering the seam on the internal perimeters can help reduce stress points. This setting moves the start of the internal wall's seam around across layers as well as away from the external perimeter seam. This way, the internal and external seams don't all align at the same point and between them across layers, distributing those weak points further away from the seam location, hence making the part stronger. It can also help improve the water tightness of your model.

- **Seam Gap**: Controls the gap in mm or as a percentage of the nozzle size between the two ends of a loop starting and ending with a seam. A larger gap will reduce the bulging seen at the seam. A smaller gap reduces the visual appearance of a seam. For a well-tuned printer with pressure advance, a value of 0-15% is typically optimal.

- **Scarf Seam**: Read more here: [Better Seams - An Orca Slicer Guide](https://www.printables.com/model/783313-better-seams-an-orca-slicer-guide-to-using-scarf-s).

- **Role-Based Wipe Speed**: Controls the speed of a wipe motion, i.e., how fast the nozzle will move over a printed area to "clean" it before traveling to another area of the model. It is recommended to turn this option on, to ensure the nozzle performs the wipe motion with the same speed that the feature was printed with.

- **Wipe Speed**: If role-based wipe speed is disabled, set this field to the absolute wipe speed or as a percentage over the travel speed.

- **Wipe on Loops**: When finishing printing a "loop" (i.e., an extrusion that starts and ends at the same point), move the nozzle slightly inwards towards the part. That move aims to reduce seam unevenness by tucking in the end of the seam to the part. It also slightly cleans the nozzle before traveling to the next area of the model, reducing stringing.

- **Wipe Before External Perimeters**: To minimize the visibility of potential over-extrusion at the start of an external perimeter, the de-retraction move is performed slightly on the inside of the model and, hence, the start of the external perimeter. That way, any potential over-extrusion is hidden from the outside surface.

  This is useful when printing with Outer/Inner or Inner/Outer/Inner wall print order, as in these modes, it is more likely an external perimeter is printed immediately after a de-retraction move, which would cause slight extrusion variance at the start of a seam.

## Tips:
With seams being inevitable when 3D printing using FFF, there are two distinct approaches on how to deal with them:

1. **Try and hide the seam as much as possible**: This can be done by enabling scarf seam, which works very well, especially with simple models with limited overhang regions.
2. **Try and make the seam as "clean" and "distinct" as possible**: This can be done by tuning the seam gap and enabling role-based wipe speed, wipe on loops, and wipe before the external loop.

## Troubleshooting Seam Performance:
The section below will focus on troubleshooting traditional seams. For scarf seam troubleshooting, refer to the guide linked above.

There are several factors that influence how clean the seam of your model is, with the biggest one being extrusion control after a travel move. As a seam defines the start and end of an extrusion, it is critical that:

1. **The same amount of material is extruded at the same point across layers** to ensure a consistent visual appearance at the start of a seam.
2. **The printer consistently stops extruding at the same point** across layers.

However, due to mechanical and material tolerances, as well as the very nature of 3D printing with FFF, that is not always possible. Hopefully with some tuning you'll be able to achieve prints like this!

![IMG_4059](https://github.com/user-attachments/assets/e60c3d24-9b21-4484-bcbe-614237a2fe09)


### Troubleshooting the Start of a Seam:
Imagine the scenario where the toolhead finishes printing a layer line on one side of the bed, retracts, travels the whole distance of the bed to de-retract, and starts printing another part. Compare this to the scenario where the toolhead finishes printing an internal perimeter and only travels a few mm to start printing an external perimeter, without even retracting or de-retracting.

The first scenario has much more opportunity for the filament to ooze outside the nozzle, resulting in a small blob forming at the start of the seam or, conversely, if too much material has leaked, a gap/under extrusion at the start of the seam.

The key to a consistent start of a seam is to reduce the opportunity for ooze as much as possible. The good news is that this is mostly tunable by:

1. **Ensure your pressure advance is calibrated correctly**. A too low pressure advance will result in the nozzle experiencing excess pressure at the end of the previous extrusion, which increases the chance of oozing when traveling.
2. **Make sure your travel speed is as fast as possible within your printer's limits**, and the travel acceleration is as high as practically possible, again within the printer's limits. This reduces the travel time between features, reducing oozing.
3. **Enable wipe before external perimeters** – this setting performs the de-retraction move inside the model, hence reducing the visual appearance of the "blob" if it does appear at the seam.
4. **Increase your travel distance threshold to be such that small travel moves do not trigger a retraction and de-retraction operation**, reducing extrusion variances caused by the extruder tolerances. 2-4mm is a good starting point as, if your PA is tuned correctly and your travel speed and acceleration are high, it is unlikely that the nozzle will ooze in the milliseconds it will take to travel to the new location.
5. **Enable retract on layer change**, to ensure the start of your layer is always performed under the same conditions – a de-pressurized nozzle with retracted filament.

In addition, some toolhead systems are inherently better at seams compared to others. For example, high-flow nozzles with larger melt zones usually have poorer extrusion control as more of the material is in a molten state inside the nozzle. They tend to string more, ooze easier, and hence have poorer seam performance. Conversely, smaller melt zone nozzles have more of the filament solid in their heat zone, leading to more accurate extrusion control and better seam performance.

So this is a trade-off between print speed and print quality. From experimental data, volcano-type nozzles tend to perform the worst at seams, followed by CHT-type nozzles, and finally regular flow nozzles.

In addition, larger nozzle diameters allow for more opportunity for material to leak compared to smaller diameter nozzles. A 0.2/0.25 mm nozzle will have significantly better seam performance than a 0.4, and that will have much better performance than a 0.6mm nozzle and so forth.

### Troubleshooting the End of a Seam:
The end of a seam is much easier to get right, as the extrusion system is already at a pressure equilibrium while printing. It just needs to stop extruding at the right time and consistently.

**If you are getting bulges at the seam**, the extruder is not stopping at the right time. The first thing to tune would be **pressure advance** – too low of a PA will result in the nozzle still being pressurized when finishing the print move, hence leaving a wider line at the end as it stops printing.

And the opposite is true too – **too high PA will result in under extrusion at the end of a print move**, shown as a larger-than-needed gap at the seam. Thankfully, tuning PA is straightforward, so run the calibration tests and pick the optimal value for your material, print speed, and acceleration.

Furthermore, the printer mechanics have tolerances – the print head may be requested to stop at point XY but practically it cannot stop precisely at that point due to the limits of micro-stepping, belt tension, and toolhead rigidity. Here is where tuning the seam gap comes into effect. **A slightly larger seam gap will allow for more variance to be tolerated at the end of a print move before showing as a seam bulge**. Experiment with this value after you are certain your PA is tuned correctly and your travel speeds and retractions are set appropriately.

Finally, the techniques of **wiping can help improve the visual continuity and consistency of a seam** (please note, these settings do not make the seam less visible, but rather make them more consistent!). Wiping on loops with a consistent speed helps tuck in the end of the seam, hiding the effects of retraction from view.

### The Role of Wall Ordering in Seam Appearance:
The order of wall printing plays a significant role in the appearance of a seam. **Starting to print the external perimeter first after a long travel move will always result in more visible artifacts compared to printing the internal perimeters first and traveling just a few mm to print the external perimeter.**

For optimal seam performance, printing with **inner-outer-inner wall order is typically best, followed by inner-outer**. It reduces the amount of traveling performed prior to printing the external perimeter and ensures the nozzle is having as consistent pressure as possible, compared to printing outer-inner.
