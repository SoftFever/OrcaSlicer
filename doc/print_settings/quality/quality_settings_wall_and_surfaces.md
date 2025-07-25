# Wall and surfaces

- [Walls printing order](#walls-printing-order)
  - [Inner/Outer](#innerouter)
  - [Inner/Outer/Inner](#innerouterinner)
  - [Outer/Inner](#outerinner)
  - [Print infill first](#print-infill-first)
- [Wall loop direction](#wall-loop-direction)
- [Surface flow ratio](#surface-flow-ratio)
- [Only one wall](#only-one-wall)
  - [Threshold](#threshold)
- [Avoid crossing walls](#avoid-crossing-walls)
  - [Max detour length](#max-detour-length)
- [Small area flow compensation](#small-area-flow-compensation)
  - [Flow Compensation Model](#flow-compensation-model)

## Walls printing order

Print sequence of the internal (inner) and external (outer) walls.  

### Inner/Outer

Use Inner/Outer for best overhangs. This is because the overhanging walls can adhere to a neighbouring perimeter while printing. However, this option results in slightly reduced surface quality as the external perimeter is deformed by being squashed to the internal perimeter.  

### Inner/Outer/Inner

Use Inner/Outer/Inner for the best external surface finish and dimensional accuracy as the external wall is printed undisturbed from an internal perimeter. However, overhang performance will reduce as there is no internal perimeter to print the external wall against. This option requires a minimum of 3 walls to be effective as it prints the internal walls from the 3rd perimeter onwards first, then the external perimeter and, finally, the first internal perimeter. This option is recommended against the Outer/Inner option in most cases.  

### Outer/Inner

Use Outer/Inner for the same external wall quality and dimensional accuracy benefits of Inner/Outer/Inner option. However, the z seams will appear less consistent as the first extrusion of a new layer starts on a visible surface.

### Print infill first

Order of wall/infill. When the tickbox is unchecked the walls are printed first, which works best in most cases.  
Printing infill first may help with extreme overhangs as the walls have the neighbouring infill to adhere to. However, the infill will slightly push out the printed walls where it is attached to them, resulting in a worse external surface finish. It can also cause the infill to shine through the external surfaces of the part.

## Wall loop direction

The direction which the wall loops are extruded when looking down from the top.  
By default all walls are extruded in counter-clockwise, unless Reverse on even is enabled. Set this to any option other than Auto will force the wall direction regardless of the Reverse on even.

> [!NOTE]
> This option will be disabled if spiral vase mode is enabled.

## Surface flow ratio

This factor affects the amount of material for top solid infill. You can decrease it slightly to have smooth surface finish.  
The actual top surface flow used is calculated by multiplying this value with the filament flow ratio, and if set, the object's flow ratio.

## Only one wall

Use only one wall on flat surfaces, to give more space to the top infill pattern.

### Threshold

If a top surface has to be printed and it's partially covered by another layer, it won't be considered at a top layer where its width is below this value. This can be useful to not let the 'one perimeter on top' trigger on surface that should be covered only by perimeters. This value can be a mm or a % of the perimeter extrusion width.  
Warning: If enabled, artifacts can be created if you have some thin features on the next layer, like letters. Set this setting to 0 to remove these artifacts.

## Avoid crossing walls

Maximum detour distance for avoiding crossing wall. Don't detour if the detour distance is larger than this value. Detour length could be specified either as an absolute value or as percentage (for example 50%) of a direct travel path. Zero to disable.

### Max detour length

Maximum detour distance for avoiding crossing wall. Don't detour if the detour distance is larger than this value. Detour length could be specified either as an absolute value or as percentage (for example 50%) of a direct travel path. Zero to disable.

## Small area flow compensation

Enable flow compensation for small infill areas.

### Flow Compensation Model

used to adjust the flow for small infill areas. The model is expressed as a comma separated pair of values for extrusion length and flow correction factors, one per line, in the following format:

```c++
0,0;
0.2,0.4444;
0.4,0.6145;
0.6,0.7059;
0.8,0.7619;
1.5,0.8571;
2,0.8889;
3,0.9231;
5,0.9520;
10,1;
```
