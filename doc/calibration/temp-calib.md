# Temp Calibration

In FDM 3D printing, the temperature is a critical factor that affects the quality of the print.
There is no other calibration that can have such a big impact on the print quality as temperature calibration.

## Nozzle Temp tower

Nozzle temperature is one of the most important settings to calibrate for a successful print. The temperature of the nozzle affects the viscosity of the filament, which in turn affects how well it flows through the nozzle and adheres to the print bed. If the temperature is too low, the filament may not flow properly, leading to under-extrusion, poor layer adhesion and stringing. If the temperature is too high, the filament may degrade, over-extrude and produce stringing.

![temp-tower_test](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/Temp-calib/temp-tower_test.gif?raw=true)

![temp-tower_test_menu](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/Temp-calib/temp-tower_test_menu.png?raw=true)

Temp tower is a straightforward test. The temp tower is a vertical tower with multiple blocks, each printed at a different temperature. Once the print is complete, we can examine each block of the tower and determine the optimal temperature for the filament. The optimal temperature is the one that produces the highest quality print with the least amount of issues, such as stringing, layer adhesion, warping (overhang), and bridging.

![temp-tower](https://github.com/SoftFever/OrcaSlicer/blob/main/doc/images/Temp-calib/temp-tower.jpg?raw=true)

## Bed temperature

Bed temperature is another important setting to calibrate for a successful print. The bed temperature affects the adhesion of the filament to the print bed, which in turn affects the overall quality of the print. If the bed temperature is too low, the filament may not adhere properly to the print bed, leading to warping and poor layer adhesion. If the bed temperature is too high, the filament may become too soft and lose its shape, leading to over-extrusion and poor layer adhesion.

This setting doesn't have a specific test, but it is recommended to start with the recommended bed temperature for the filament and adjust it based on the filament manufacturer's recommendations.

## Chamber temperature

Chamber temperature can affect the print quality, especially for high-temperature filaments. A heated chamber can help to maintain a consistent temperature throughout the print, reducing the risk of warping and improving layer adhesion. However, it is important to monitor the chamber temperature to ensure that it does not exceed the recommended temperature for the filament being used.

See: [Chamber temperature printer settings](Chamber-temperature)

> [!NOTE]
> Low temperature Filaments like PLA can clog the nozzle if the chamber temperature is too high.
