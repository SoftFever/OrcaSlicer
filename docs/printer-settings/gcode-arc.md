# Gcode Arc

This is the docs for how to enable G3/G17 in printer firmware.

### Klipper 

Add config below to `printer.cfg`

```
# Enable arcs support
[gcode_arcs]
resolution: 0.1
```

### RRF (RepRap Firmware)

Support in RRF 1.18 and later.
