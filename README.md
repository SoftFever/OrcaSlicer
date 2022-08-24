
# Bambu Studio - SoftFever
A modified version of Bambu Studio.   
It has following changes:

- ## Support third-party printers:
    - Voron 2.4
    - Voron Trident
    - Voron 0.1
    - Prusa MK3S
- ## Export to to .gcode file.  
    ![image](./SoftFever_doc/export_gcode.png)  

- ## Send gcode file to printer  
    1. Setup connection  
        <img src="./SoftFever_doc/print_host.png" width="1024">
    2. Send to print  
        <img src="./SoftFever_doc/send_to_print.png" width="1024">
- ## Support single wall mode on first layer  
    ![image](./SoftFever_doc/single_wall_first_layer.png)  
- ## Support Chamber temperature. This setting can be use in machine start G-Gcode  
    ![image](./SoftFever_doc/chamber_temperature.png)  
    <img src="./SoftFever_doc/chamber_temperature_custom_gcode.png" width="686">
- ## Thumbview is supported

Demo video: https://youtu.be/vSNE9iGj2II  
 
# NOTE: 
## - If you have troubles to run the build, you might need to install following runtimes:  
- [MicrosoftEdgeWebView2RuntimeInstallerX64](https://github.com/SoftFever/BambuStudio-SoftFever/releases/download/v1.0.10-sf2/MicrosoftEdgeWebView2RuntimeInstallerX64.exe)  
- [vcredist2019_x64](https://github.com/SoftFever/BambuStudio-SoftFever/releases/download/v1.0.10-sf2/vcredist2019_x64.exe)  

## - BambuStudio use G2/G3 commands by default. You need to turn on ARC support in your printer's firmware use with this slicer.
- For Voron and any Klipper based printers:  
You can enable gcode_arcs(G2/G3) support by adding following section into you printer.cfg file:  
```
[gcode_arcs]
resolution: 0.1

[gcode_macro M205]
gcode:
    {% set x_jerk = params.X|default(5)|float %}
    {% set y_jerk = params.Y|default(5)|float %}
    SET_VELOCITY_LIMIT SQUARE_CORNER_VELOCITY={x_jerk}

```

It's also recommended to add followinging dummy macros to make Klipper happy
```
# Make BambuStudio happy
[gcode_macro G17]
gcode:

[gcode_macro M981]
gcode:

[gcode_macro M1003]
gcode:
```

- For Prusa MK3S:  
ARC movement are supported by default.


# Gallery
![image](./SoftFever_doc/printers1.png)  
![image](./SoftFever_doc/printers2.png)  
![image](./SoftFever_doc/printers3.png)  
![image](./SoftFever_doc/printers4.png)  
![image](./SoftFever_doc/voron.png)  



Image credits: 
 1. Voron 2.4 and Trident: vorondesign.com
 3. Voron 0.1: myself
 4. Prusa MK3S: Prusa3d  


=========================SPLIT=========================     

![image](https://user-images.githubusercontent.com/106916061/179006347-497d24c0-9bd6-45b7-8c49-d5cc8ecfe5d7.png)
# BambuStudio
Bambu Studio is a cutting-edge, feature-rich slicing software.  
It contains project-based workflows, systematically optimized slicing algorithms, and an easy-to-use graphic interface, bringing users an incredibly smooth printing experience.

Prebuilt Windows, macOS 64-bit releases are available through the [github releases page](https://github.com/bambulab/BambuStudio/releases/).  
Linux version currently is not supported.

Bambu Studio is based on [PrusaSlicer](https://github.com/prusa3d/PrusaSlicer) by Prusa Research, which is from [Slic3r](https://github.com/Slic3r/Slic3r) by Alessandro Ranellucci and the RepRap community.

See the [wiki](https://github.com/bambulab/BambuStudio/wiki) and the [documentation directory](https://github.com/bambulab/BambuStudio/tree/master/doc) for more informations.

# What are Bambu Studio's main features?
Key features are:
- Basic slicing features & GCode viewer
- Multiple plates management
- Remote control & monitoring
- Auto-arrange objects
- Auto-orient objects
- Hybrid/Tree/Normal support types, Customized support
- multi-material printing and rich painting tools
- multi-platform (Win/Mac/Linux) support
- Global/Object/Part level slicing parameters

Other major features are:
- Advanced cooling logic controlling fan speed and dynamic print speed
- Auto brim according to mechanical analysis
- Support arc path(G2/G3)
- Support STEP format
- Assembly & explosion view
- Flushing transition-filament into infill/object during filament change

# How to compile
Following platforms are currently supported to compile:
- Windows 64-bit, [Compile Guide](https://github.com/bambulab/BambuStudio/wiki/Windows-Compile-Guide)
- Mac 64-bit, [Compile Guide](https://github.com/bambulab/BambuStudio/wiki/Mac-Compile-Guide)

# Report issue
You can add an issue to the [github tracker](https://github.com/bambulab/BambuStudio/issues) if **it isn't already present.**

# License
Bambu Studio is licensed under the GNU Affero General Public License, version 3. Bambu Studio is based on PrusaSlicer by PrusaResearch.

PrusaSlicer is licensed under the GNU Affero General Public License, version 3. PrusaSlicer is owned by Prusa Research. PrusaSlicer is originally based on Slic3r by Alessandro Ranellucci.

Slic3r is licensed under the GNU Affero General Public License, version 3. Slic3r was created by Alessandro Ranellucci with the help of many other contributors.

The GNU Affero General Public License, version 3 ensures that if you use any part of this software in any way (even behind a web server), your software must be released under the same license.

The bambu networking plugin is based on non-free libraries. It is optional to the Bambu Studio and provides extended functionalities for users.

