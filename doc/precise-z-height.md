# Precise Z Height Adjustment

This feature ensures the accurate Z height of the model after slicing, even if the model height is not a multiple of the layer height.

For example, slicing a 20mm x 20mm x 20.1mm cube with a layer height of 0.2mm would typically result in a final height of 20.2mm due to the layer height increments.

By enabling this parameter, the layer height of the last five layers is adjusted so that the final sliced height matches the actual object height, resulting in an accurate 20.1mm (as shown in the picture).

- **Precise Z Height Off**

  ![PreciseZOff](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/PreciseZ/PreciseZOff.png?raw=true)

- **Precise Z Height On**

  ![PreciseZOn](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/PreciseZ/PreciseZOn.png?raw=true)
