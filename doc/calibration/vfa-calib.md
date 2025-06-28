# VFA

Vertical Fine Artifacts (VFA) are small artifacts that can occur on the surface of a 3D print, particularly in areas where there are sharp corners or changes in direction. These artifacts can be caused by a variety of factors, including mechanical vibrations, resonance, and other factors that can affect the quality of the print.

Because of the nature of these artifacts the methods to reduce them can be mechanical such as changing motors, belts and pulleys or with advanced calibrations such as [Jerk/Junction Deviation](cornering-calib) corrections or [Input Shaping](input-shaping-calib).

## VFA Test

OrcaSlicer's VFA test is used to identify the print speed that minimizes ringing artifacts. It prints a tower with walls at key angles while gradually increasing the print speed. The goal is to find the speed at which VFA artifacts are least visible, revealing the optimal range for clean surfaces.

![vfa_test_menu](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/vfa/vfa_test_menu.png?raw=true)

![vfa_test_print](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/vfa/vfa_test_print.jpg?raw=true)