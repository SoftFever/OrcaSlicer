# Support Advanced

- [Z distance](#z-distance)
- [Support wall loops](#support-wall-loops)
- [Base Pattern](#base-pattern)
  - [Base pattern spacing](#base-pattern-spacing)
- [Pattern angle](#pattern-angle)
- [Interface layers](#interface-layers)
- [Interface pattern](#interface-pattern)
- [Interface spacing](#interface-spacing)
- [Normal support expansion](#normal-support-expansion)
- [Support/object XY distance](#supportobject-xy-distance)
- [Support/object first layer gap](#supportobject-first-layer-gap)
- [Don't support bridges](#dont-support-bridges)
- [Independent support layer height](#independent-support-layer-height)

## Z distance

The Z gap between support interface and object.

## Support wall loops

This setting specifies the count of support walls in the range of [0,2]. 0 means auto.

## Base Pattern

Line pattern for the base of the support.

### Base pattern spacing

Spacing between support lines.

## Pattern angle

Use this setting to rotate the support pattern on the horizontal plane.

## Interface layers

The number of interface layers.

## Interface pattern

The pattern used for the support interface.

## Interface spacing

Spacing of interface lines. Zero means solid interface.

## Normal support expansion

Expand (+) or shrink (-) the horizontal span of normal support.

## Support/object XY distance

XY separation between an object and its support.

## Support/object first layer gap

XY separation between an object and its support at the first layer.

## Don't support bridges

Don't support the whole bridge area which make support very large. Bridges can usually be printed directly without support if not very long.

## Independent support layer height

Support layer uses layer height independent with object layer. This is to support customizing z-gap and save print time. This option will be invalid when the prime tower is enabled.
