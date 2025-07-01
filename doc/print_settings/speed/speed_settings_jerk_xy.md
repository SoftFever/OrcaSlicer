# Jerk XY

Jerk in 3D printing is usually set on the printer's firmware settings.  
This setting will try to override the jerk when [normal printing jerk](#normal-printing) or [Junction Deviation](#junction-deviation) value is different than 0.
Orca will limit the jerk to not exceed the jerk set in the Printer's Motion Ability settings.

- [Default](#default)
  - [Outer wall](#outer-wall)
  - [Inner wall](#inner-wall)
  - [Infill](#infill)
  - [Top surface](#top-surface)
  - [Initial layer](#initial-layer)
  - [Travel](#travel)
- [Junction Deviation](#junction-deviation)
- [Useful links](#useful-links)

## Default

Default Jerk value.

### Outer wall

Jerk for outer wall printing. This is usually set to a lower value than normal printing to ensure better quality.

### Inner wall

Jerk for inner wall printing. This is usually set to a higher but still reasonable value than outer wall printing to improve speed.

### Infill

Jerk for infill printing. This is usually set to a value higher than inner wall printing to improve speed.

### Top surface

Jerk for top surface printing. This is usually set to a lower value than infill to ensure better quality.

### Initial layer

Jerk for initial layer printing. This is usually set to a lower value than top surface to improve adhesion.

### Travel

Jerk for travel printing. This is usually set to a higher value than infill to reduce travel time.

## Junction Deviation

Alternative to Jerk, Junction Deviation is the default method for controlling cornering speed in MarlinFW (Marlin2) printers.  
Higher values result in more aggressive cornering speeds, while lower values produce smoother, more controlled cornering.

This value will **only be overwritten** if it is lower than the Junction Deviation value set in Printer settings > Motion ability. If it is higher, the value configured in Motion ability will be used.

To Calculate your Junction Deviation value, please refer to the [Junction Deviation Calibration guide](cornering-calib#junction-deviation).

```math
JD = 0,4 \cdot \frac{\text{Jerk}^2}{\text{Accel.}}
```

## Useful links

- [Klipper Kinematics](https://www.klipper3d.org/Kinematics.html?h=accelerat#acceleration)
- [Marlin Junction Deviation](https://marlinfw.org/docs/configuration/configuration.html#junction-deviation-)
- [JD Explained and Visualized, by Paul Wanamaker](https://reprap.org/forum/read.php?1,739819)
- [Computing JD for Marlin Firmware](https://blog.kyneticcnc.com/2018/10/computing-junction-deviation-for-marlin.html)
- [Improving GRBL: Cornering Algorithm](https://onehossshay.wordpress.com/2011/09/24/improving_grbl_cornering_algorithm/)
