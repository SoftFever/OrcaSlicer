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
Of course, Perl's not that fast as C and usage of modules like Moose make
everything quite memory-hungry, but I'm happy with it. I want to build a "rapid
prototyping" architecture for a slicer.

Also, http://xkcd.com/224/

## What's its current status?

Slic3r is able to:

* read binary and ASCII STL files;
* generate multiple perimeters (skins);
* generate rectilinear fill (100% solid for external surfaces or with customizable less density for inner surfaces);
* use relative or absolute extrusion commands;
* center print around bed center point;
* use different speed for bottom layer;
* output relevant GCODE.

Roadmap includes the following goals:

* output some statistics;
* allow the user to customize initial and final GCODE commands;
* retraction;
* option for filling multiple solid layers near external surfaces;
* support material for internal perimeters;
* ability to infill in the direction of bridges;
* skirt;
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
