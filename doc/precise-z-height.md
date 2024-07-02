This feature ensures the accurate Z height of the model after slicing, even if the model height is not a multiple of the layer height.

For example, slicing a 20mm x 20mm x 20.1mm cube with a layer height of 0.2mm would typically result in a final height of 20.2mm due to the layer height increments. 

By enabling this parameter, the layer height of the last five layers is adjusted so that the final sliced height matches the actual object height, resulting in an accurate 20.1mm (as shown in the picture).
![image](https://github.com/SoftFever/OrcaSlicer/assets/103989404/e2d4efab-a8f4-4df6-baa6-42f526ac83ec)
