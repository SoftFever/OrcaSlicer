# Line Width

These settings control how wide the extruded lines are.

- **Default**: The default line width in mm or as a percentage of the nozzle size.
  
- **First Layer**: The line width of the first layer. Typically, this is wider than the rest of the print, to promote better bed adhesion. See tips below for why.
  
- **Outer Wall**: The line width in mm or as a percentage of the nozzle size used when printing the model’s external wall perimeters.
  
- **Inner Wall**: The line width in mm or as a percentage of the nozzle size used when printing the model’s internal wall perimeters.
  
- **Top Surface**: The line width in mm or as a percentage of the nozzle size used when printing the model’s top surface.
  
- **Sparse Infill**: The line width in mm or as a percentage of the nozzle size used when printing the model’s sparse infill.
  
- **Internal Solid Infill**: The line width in mm or as a percentage of the nozzle size used when printing the model’s internal solid infill.
  
- **Support**: The line width in mm or as a percentage of the nozzle size used when printing the model’s support structures.


## Tips:
1. **Typically, the line width will be anything from 100% up to 150% of the nozzle width**. Due to the way the slicer’s flow math works, a 100% line width will attempt to extrude slightly “smaller” than the nozzle size and when squished onto the layer below will match the nozzle orifice. You can read more on the flow math here: [Flow Math](https://manual.slic3r.org/advanced/flow-math).

2. **For most cases, the minimum acceptable recommended line width is 105% of the nozzle diameter**, typically reserved for the outer walls, where greater precision is required. A wider line is less precise than a thinner line.

3. **Wider lines provide better adhesion to the layer below**, as the material is squished more with the previous layer. For parts that need to be strong, setting this value to 120-150% of the nozzle diameter is recommended and has been experimentally proven to significantly increase part strength.

4. **Wider lines improve step over and overhang appearance**, i.e., the overlap of the currently printed line to the surface below. So, if you are printing models with overhangs, setting a larger external perimeter line width will improve the overhang’s appearance to an extent.

5. **For top surfaces, typically a value of ~100%-105% of the nozzle width is recommended** as it provides the most precision, compared to a wider line.

6. **For external walls, you need to strike a balance between precision and step over and, consequently, overhang appearance.** Typically these values are set to ~105% of nozzle diameter for models with limited overhangs up to ~120% for models with more significant overhangs.

7. **For internal walls, you typically want to maximize part strength**, so a good starting point is approximately 120% of the nozzle width, which gives a good balance between print speed, accuracy, and material use. However, depending on the model, larger or smaller line widths may make sense in order to reduce gap fill and/or line width variations if you are using Arachne.

8. **Don’t feel constrained to have wider internal wall lines compared to external ones**. While this is the default for most profiles, for models where significant overhangs are present, printing wider external walls compared to the internal ones may yield better overhang quality without increasing material use!

9. **For sparse infill, the line width also affects how dense, visually, the sparse infill will be.** The sparse infill aims to extrude a set amount of material based on the percentage infill selected. When increasing the line width, the space between the sparse infill extrusions is larger in order to roughly maintain the same material usage. Typically for sparse infill, a value of 120% of nozzle diameter is a good starting point.

10. **For supports, using 100% or less line width will make the supports weaker** by reducing their layer adhesion, making them easier to remove.

11. **If your printer is limited mechanically, try to maintain the material flow as consistent as possible between critical features of your model**, to ease the load on the extruder having to adapt its flow between them. This is especially useful for printers that do not use pressure advance/linear advance and if your extruder is not as capable mechanically. You can do that by adjusting the line widths and speeds to reduce the variation between critical features (e.g., external and internal wall flow). For example, print them at the same speed and the same line width, or print the external perimeter slightly wider and slightly slower than the internal perimeter. Material flow can be visualized in the sliced model – flow drop down.
