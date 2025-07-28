# Overhangs

## Detect overhang wall

Detect the overhang percentage relative to line width and use different speed to print. For 100% overhang, bridge speed is used.

## Make overhang printable

Modify the geometry to print overhangs without support material.

### Maximum angle

Maximum angle of overhangs to allow after making more steep overhangs printable.  
90° will not change the model at all and allow any overhang, while 0 will replace all overhangs with conical material.

### Hole area

Maximum area of a hole in the base of the model before it's filled by conical material.  
A value of 0 will fill all the holes in the model base.

## Extra perimeters on overhangs

Create additional perimeter paths over steep overhangs and areas where bridges cannot be anchored.

## Reverse on even

Extrude perimeters that have a part over an overhang in the reverse direction on even layers. This alternating pattern can drastically improve steep overhangs.  
This setting can also help reduce part warping due to the reduction of stresses in the part walls.

### Reverse internal only

Apply the reverse perimeters logic only on internal perimeters.  
This setting greatly reduces part stresses as they are now distributed in alternating directions. This should reduce part warping while also maintaining external wall quality. This feature can be very useful for warp prone material, like ABS/ASA, and also for elastic filaments, like TPU and Silk PLA. It can also help reduce warping on floating regions over supports.  
For this setting to be the most effective, it is recommended to set the Reverse Threshold to 0 so that all internal walls print in alternating directions on even layers irrespective of their overhang degree.

### Reverse threshold

Number of mm the overhang need to be for the reversal to be considered useful. Can be a % of the perimeter width.  
Value 0 enables reversal on every even layers regardless.  
When [Detect overhang wall](#detect-overhang-wall) is not enabled, this option is ignored and reversal happens on every even layers regardless.
