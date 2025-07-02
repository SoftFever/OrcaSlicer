# Skirt

Skirts are additional perimeters printed at the base of the model to prime the nozzle.

## Loops

Number of loops for the skirt. Zero means disabling skirt.

## Type

- Combined - single skirt for all objects.
- Per object - individual object skirt.

## Minimum extrusion Length

Minimum filament extrusion length in mm when printing the skirt. Zero means this feature is disabled.  
Using a non-zero value is useful if the printer is set up to print without a prime line.  
Final number of loops is not taking into account while arranging or validating objects distance. Increase loop number in such case.

## Distance

Distance from skirt to brim or object.

## Start point

Angle from the object center to skirt start point. Zero is the most right position, counter clockwise is positive angle.

## Speed

Speed of skirt, in mm/s. Zero means use default layer extrusion speed.

## Height

How many layers of skirt. Usually only one layer.

## Shield

A draft shield is useful to protect an ABS or ASA print from warping and detaching from print bed due to wind draft. It is usually needed only with open frame printers, i.e. without an enclosure.  
"Enabled = skirt is as tall as the highest printed object. Otherwise 'Skirt height' is used.  

> [!NOTE]
> With the draft shield active, the skirt will be printed at skirt distance from the object. Therefore, if brims are active it may intersect with them. To avoid this, increase the skirt distance value.

## Single loop after first layer

Limits the draft shield loops to one wall after the first layer. This is useful, on occasion, to conserve filament but may cause the draft shield to warp / crack.
