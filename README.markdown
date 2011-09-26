_Q: Yet another RepRap slicer?_

A: Yes.

# Slic3r

## What's it?

Slic3r is (er, will be) an STL-to-GCODE translator for RepRap 3D printers, 
like Enrique's Skeinforge or RevK's E3D.

## Why another one? Why Perl?

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
* ability to scale and rotate input object;
* use different speed for bottom layer.

Roadmap includes the following goals:

* output some statistics;
* allow the user to customize initial and final GCODE commands;
* support material for internal perimeters;
* ability to infill in the direction of bridges;
* multiply input object;
* cool;
* nice packaging for cross-platform deployment.

## Is it usable already?

Yes. I need to write a script to install dependencies.

## Can I help?

Sure! Send patches and/or drop me a line at aar@cpan.org. You can also 
find me in #RepRap on FreeNode with the nickname _Sound_.

## What's Slic3r license?

Slic3r is dual-licensed under the _Perl Artistic License_ and the _AGPLv3_.
The author is Alessandro Ranellucci (that's me).
