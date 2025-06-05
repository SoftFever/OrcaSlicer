# Multiple bed types

You can enable it in printer settings.


Once enabled, you can select the bed type in the drop-down menu, corresponding bed temperature will be set automatically.
You can set the bed temperature for each bed type in the filament settings as demonstrated in the following image.
![multi_bed](./images/bed-types.gif)


Orca also support `curr_bed_type` variable in custom G-code.
For example, the following sample G-codes can detect the selected bed type and adjust the G-code offset accordingly for Klipper:
```
{if curr_bed_type=="Textured PEI Plate"}
  SET_GCODE_OFFSET Z=-0.05
{else}
  SET_GCODE_OFFSET Z=0.0
{endif}
```

available bed types are:
```
"Cool Plate"
"Engineering Plate"
"High Temp Plate"
"Textured PEI Plate"
```