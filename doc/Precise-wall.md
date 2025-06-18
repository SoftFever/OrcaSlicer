# Precise Wall

The 'Precise Wall' is a distinctive feature introduced by OrcaSlicer, aimed at improving the dimensional accuracy of prints and minimizing layer inconsistencies by slightly increasing the spacing between the outer wall and the inner wall.

## Technical explanation

Below is a technical explanation of how this feature works.

First, it's important to understand some basic concepts like flow, extrusion width, and space. Slic3r has an excellent document that covers these topics in detail. You can refer to this [article](https://manual.slic3r.org/advanced/flow-math).

Now, let's dive into the specifics. Slic3r and its forks, such as PrusaSlicer, SuperSlicer, and OrcaSlicer, assume that the extrusion path has an oval shape, which accounts for the overlaps. For example, if we set the wall width to 0.4mm and the layer height to 0.2mm, the combined thickness of two walls laid side by side is 0.714mm instead of 0.8mm due to the overlapping.

- **Precise Wall Off**

  ![PreciseWallOff](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/PreciseWall/PreciseWallOff.svg?raw=true)

- **Precise Wall On**

  ![PreciseWallOn](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/PreciseWall/PreciseWallOn.svg?raw=true)

This approach enhances the strength of 3D-printed parts. However, it does have some side effects. For instance, when the inner-outer wall order is used, the outer wall can be pushed outside, leading to potential size inaccuracy and more layer inconsistency.

It's important to keep in mind that this approach to handling flow is specific to Slic3r and its forks. Other slicing software, such as Cura, assumes that the extrusion path is rectangular and, therefore, does not include overlapping. Two 0.4 mm walls will result in a 0.8 mm shell thickness in Cura.

OrcaSlicer adheres to Slic3r's approach to handling flow. To address the downsides mentioned earlier, OrcaSlicer introduced the 'Precise Wall' feature. When this feature is enabled in OrcaSlicer, the overlap between the outer wall and its adjacent inner wall is set to zero. This ensures that the overall strength of the printed part is unaffected, while the size accuracy and layer consistency are improved.
