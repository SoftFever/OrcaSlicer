_Q: Oh cool, a new RepRap slicer?_

A: Yes.

# Slic3r

## What's it?

Slic3r is an STL-to-GCODE translator for RepRap 3D printers, 
like Enrique's Skeinforge or RevK's E3D.

## Why a new one? Why Perl?

The purpose is to build something more maintainable and flexible than both
Skeinforge and E3D. The code makes extensive use of object-oriented 
programming to achieve some level of abstraction instead of working with
raw geometry and low-level data structures.
This should help to maintain code, fix bugs and implement new and better
algorithms in the future.
I also aim at implementing better support for hollow objects, as Skeinforge
isn't smart enough to generate internal support structures for horizontal
facets.

Also, http://xkcd.com/224/

## What's its current status?

Slic3r current features are:

* read binary and ASCII STL files;
* generate multiple perimeters (skins);
* generate rectilinear fill;
* set 0% - 100% infill density;
* set infill angle;
* retraction;
* skirt (with rounded corners);
* use relative or absolute extrusion commands;
* center print around bed center point;
* multiple solid layers near horizontal external surfaces;
* ability to scale, rotate and multiply input object;
* use different speed for bottom layer.

Roadmap includes the following goals:

* output some statistics;
* allow the user to customize initial and final GCODE commands;
* support material for internal perimeters;
* ability to infill in the direction of bridges;
* cool;
* other fill patterns;
* nice packaging for cross-platform deployment.

## Is it usable already?

Yes, although its extrusion math needs to be tested.
I need to write a script to install dependencies and to package
dependency-free executables for main platforms.

## Can I help?

Sure! Send patches and/or drop me a line at aar@cpan.org. You can also 
find me in #reprap on FreeNode with the nickname _Sound_.

## What's Slic3r license?

Slic3r is dual-licensed under the _Perl Artistic License_ and the _AGPLv3_.
The author is Alessandro Ranellucci (me).

## How can I invoke slic3r.pl?

    Usage: slic3r.pl [ OPTIONS ] file.stl
    
        --help              Output this usage screen and exit
        
      Printer options:
        --nozzle-diameter   Diameter of nozzle in mm (default: 0.45)
        --print-center      Coordinates of the point to center the print around 
                            (default: 100,100)
        --use-relative-e-distances
                            Use relative distances for extrusion in GCODE output
        --z-offset          Additional height in mm to add to vertical coordinates
                            (+/-, default: 0)
        
      Filament options:
        --filament-diameter Diameter of your raw filament (default: 3)
        --filament-packing-density
                            Ratio of the extruded volume over volume pushed 
                            into the extruder (default: 0.85)
        
      Speed options:
        --print-feed-rate   Speed of print moves in mm/sec (default: 60)
        --travel-feed-rate  Speed of non-print moves in mm/sec (default: 130)
        --bottom-layer-speed-ratio
                            Factor to increase/decrease speeds on bottom 
                            layer by (default: 0.6)
        
      Accuracy options:
        --layer-height      Layer height in mm (default: 0.4)
      
      Print options:
        --perimeters        Number of perimeters/horizontal skins (range: 1+, 
                            default: 3)
        --solid-layers      Number of solid layers to do for top/bottom surfaces
                            (range: 1+, default: 3)
        --fill-density      Infill density (range: 0-1, default: 0.4)
        --fill-angle        Infill angle in degrees (range: 0-90, default: 0)
        --temperature       Extrusion temperature (default: 195)
      
      Retraction options:
        --retract-length    Length of retraction in mm when pausing extrusion 
                            (default: 2)
        --retract-speed     Speed for retraction in mm/sec (default: 40)
        --retract-restart-extra
                            Additional amount of filament in mm to push after
                            compensating retraction (default: 0)
       Skirt options:
        --skirts            Number of skirts to draw (default: 1)
        --skirt-distance    Distance in mm between innermost skirt and object 
                            (default: 6)
        -o, --output        File to output gcode to (default: <inputfile>.gcode)
       
       Transform options:
        --scale             Factor for scaling input object (default: 1)
        --rotate            Rotation angle in degrees (0-360, default: 0)
        --multiply-x        Number of items along X axis (1+, default: 1)
        --multiply-y        Number of items along Y axis (1+, default: 1)
        --multiply-distance Distance in mm between copies (default: 6)
