_Q: Oh cool, a new RepRap slicer?_

A: Yes.

# Slic3r

## What's it?

Slic3r is a G-code generator for 3D printers. It's compatible with RepRaps,
Makerbots, Ultimakers and many more machines.

See the [project homepage](http://slic3r.org/) at slic3r.org and the
[documentation](https://github.com/alexrj/Slic3r/wiki/Documentation) on the Slic3r wiki for more information.

## What language is it written in?

Proudly Perl, with some parts in C++.
If you're wondering why Perl, see http://xkcd.com/224/

## What's its current status?

Slic3r current key features are:

* multi-platform (Linux/Mac/Win) and packaged as standalone-app with no dependencies required;
* easy configuration/calibration;
* read binary and ASCII STL files as well as OBJ and AMF;
* powerful command line interface;
* easy GUI with plating and manipulation facilities;
* multithreaded;
* multiple infill patterns, with customizable density and angle;
* retraction;
* skirt;
* infill every N layers (like the "Skin" plugin for Skeinforge);
* detect optimal infill direction for bridges;
* save configuration profiles;
* center print around bed center point;
* multiple solid layers near horizontal external surfaces;
* ability to scale, rotate and duplicate input objects;
* customizable initial and final G-code;
* support material;
* cooling and fan control;
* use different speed for bottom layer, perimeters, small perimeters, bridges, solid infill;
* ability to print complete objects before moving onto next one.

Experimental features include:

* generation of G2/G3 commands for native arcs;
* G0 commands for fast retraction.

Roadmap includes the following goals:

* output some statistics;
* support material for internal perimeters;
* more GUI work;
* more fill patterns;
* a more complete roadmap is needed too ;-)

## How to install?

It's very easy. See the [project homepage](http://slic3r.org/)
for instructions and links to the precompiled packages that you can just
download and run, with no dependencies required.

## Can I help?

Sure! Drop me a line at aar@cpan.org. You can also 
find me in #reprap and in #slic3r on FreeNode with the nickname _Sound_.
Before sending patches and pull requests contact me to discuss your proposed
changes: this way we'll ensure nobody wastes their time and no conflicts arise
in development.

## What's Slic3r license?

Slic3r is licensed under the _GNU Affero General Public License, version 3_.
The author is Alessandro Ranellucci.

The [Silk icon set](http://www.famfamfam.com/lab/icons/silk/) used in Slic3r is
licensed under the _Creative Commons Attribution 3.0 License_.
The author of the Silk icon set is Mark James.

## How can I invoke slic3r.pl using the command line?

    Usage: slic3r.pl [ OPTIONS ] file.stl
    
        --help              Output this usage screen and exit
        --version           Output the version of Slic3r and exit
        --save <file>       Save configuration to the specified file
        --load <file>       Load configuration from the specified file. It can be used 
                            more than once to load options from multiple files.
        -o, --output <file> File to output gcode to (by default, the file will be saved
                            into the same directory as the input file using the 
                            --output-filename-format to generate the filename)
    
      Output options:
        --output-filename-format
                            Output file name format; all config options enclosed in brackets
                            will be replaced by their values, as well as [input_filename_base]
                            and [input_filename] (default: [input_filename_base].gcode)
        --post-process      Generated G-code will be processed with the supplied script;
                            call this more than once to process through multiple scripts.
        --export-svg        Export a SVG file containing slices instead of G-code.
        -m, --merge         If multiple files are supplied, they will be composed into a single 
                            print rather than processed individually.
      
      Printer options:
        --nozzle-diameter   Diameter of nozzle in mm (default: 0.5)
        --print-center      Coordinates in mm of the point to center the print around 
                            (default: 100,100)
        --z-offset          Additional height in mm to add to vertical coordinates
                            (+/-, default: 0)
        --gcode-flavor      The type of G-code to generate (reprap/teacup/makerbot/sailfish/mach3/no-extrusion,
                            default: reprap)
        --use-relative-e-distances Enable this to get relative E values
        --gcode-arcs        Use G2/G3 commands for native arcs (experimental, not supported
                            by all firmwares)
        --g0                Use G0 commands for retraction (experimental, not supported by all
                            firmwares)
        --gcode-comments    Make G-code verbose by adding comments (default: no)
        --vibration-limit   Limit the frequency of moves on X and Y axes (Hz, set zero to disable;
                            default: 0)
        
      Filament options:
        --filament-diameter Diameter in mm of your raw filament (default: 3)
        --extrusion-multiplier
                            Change this to alter the amount of plastic extruded. There should be
                            very little need to change this value, which is only useful to 
                            compensate for filament packing (default: 1)
        --temperature       Extrusion temperature in degree Celsius, set 0 to disable (default: 200)
        --first-layer-temperature Extrusion temperature for the first layer, in degree Celsius,
                            set 0 to disable (default: same as --temperature)
        --bed-temperature   Heated bed temperature in degree Celsius, set 0 to disable (default: 0)
        --first-layer-bed-temperature Heated bed temperature for the first layer, in degree Celsius,
                            set 0 to disable (default: same as --bed-temperature)
        
      Speed options:
        --travel-speed      Speed of non-print moves in mm/s (default: 130)
        --perimeter-speed   Speed of print moves for perimeters in mm/s (default: 30)
        --small-perimeter-speed
                            Speed of print moves for small perimeters in mm/s or % over perimeter speed
                            (default: 30)
        --external-perimeter-speed
                            Speed of print moves for the external perimeter in mm/s or % over perimeter speed
                            (default: 100%)
        --infill-speed      Speed of print moves in mm/s (default: 60)
        --solid-infill-speed Speed of print moves for solid surfaces in mm/s or % over infill speed
                            (default: 60)
        --top-solid-infill-speed Speed of print moves for top surfaces in mm/s or % over solid infill speed
                            (default: 50)
        --support-material-speed
                            Speed of support material print moves in mm/s (default: 60)
        --bridge-speed      Speed of bridge print moves in mm/s (default: 60)
        --gap-fill-speed    Speed of gap fill print moves in mm/s (default: 20)
        --first-layer-speed Speed of print moves for bottom layer, expressed either as an absolute
                            value or as a percentage over normal speeds (default: 30%)
        
      Acceleration options:
        --perimeter-acceleration
                            Overrides firmware's default acceleration for perimeters. (mm/s^2, set zero
                            to disable; default: 0)
        --infill-acceleration
                            Overrides firmware's default acceleration for infill. (mm/s^2, set zero
                            to disable; default: 0)
        --default-acceleration
                            Acceleration will be reset to this value after the specific settings above
                            have been applied. (mm/s^2, set zero to disable; default: 130)
        
      Accuracy options:
        --layer-height      Layer height in mm (default: 0.4)
        --first-layer-height Layer height for first layer (mm or %, default: 100%)
        --infill-every-layers
                            Infill every N layers (default: 1)
        --solid-infill-every-layers
                            Force a solid layer every N layers (default: 0)
    
      Print options:
        --perimeters        Number of perimeters/horizontal skins (range: 0+, default: 3)
        --top-solid-layers  Number of solid layers to do for top surfaces (range: 0+, default: 3)
        --bottom-solid-layers  Number of solid layers to do for bottom surfaces (range: 0+, default: 3)
        --solid-layers      Shortcut for setting the two options above at once
        --fill-density      Infill density (range: 0-1, default: 0.4)
        --fill-angle        Infill angle in degrees (range: 0-90, default: 45)
        --fill-pattern      Pattern to use to fill non-solid layers (default: rectilinear)
        --solid-fill-pattern Pattern to use to fill solid layers (default: rectilinear)
        --start-gcode       Load initial G-code from the supplied file. This will overwrite
                            the default command (home all axes [G28]).
        --end-gcode         Load final G-code from the supplied file. This will overwrite 
                            the default commands (turn off temperature [M104 S0],
                            home X axis [G28 X], disable motors [M84]).
        --layer-gcode       Load layer-change G-code from the supplied file (default: nothing).
        --toolchange-gcode  Load tool-change G-code from the supplied file (default: nothing).
        --extra-perimeters  Add more perimeters when needed (default: yes)
        --randomize-start   Randomize starting point across layers (default: yes)
        --avoid-crossing-perimeters Optimize travel moves so that no perimeters are crossed (default: no)
        --only-retract-when-crossing-perimeters
                            Disable retraction when travelling between infill paths inside the same island.
                            (default: no)
        --solid-infill-below-area
                            Force solid infill when a region has a smaller area than this threshold
                            (mm^2, default: 70)
      
       Support material options:
        --support-material  Generate support material for overhangs
        --support-material-threshold
                            Overhang threshold angle (range: 0-90, set 0 for automatic detection,
                            default: 0)
        --support-material-pattern
                            Pattern to use for support material (default: rectilinear)
        --support-material-spacing
                            Spacing between pattern lines (mm, default: 2.5)
        --support-material-angle
                            Support material angle in degrees (range: 0-90, default: 0)
      
       Retraction options:
        --retract-length    Length of retraction in mm when pausing extrusion (default: 1)
        --retract-speed     Speed for retraction in mm/s (default: 30)
        --retract-restart-extra
                            Additional amount of filament in mm to push after
                            compensating retraction (default: 0)
        --retract-before-travel
                            Only retract before travel moves of this length in mm (default: 2)
        --retract-lift      Lift Z by the given distance in mm when retracting (default: 0)
        
       Retraction options for multi-extruder setups:
        --retract-length-toolchange
                            Length of retraction in mm when disabling tool (default: 1)
        --retract-restart-extra-toolchnage
                            Additional amount of filament in mm to push after
                            switching tool (default: 0)
       
       Cooling options:
        --cooling           Enable fan and cooling control
        --min-fan-speed     Minimum fan speed (default: 35%)
        --max-fan-speed     Maximum fan speed (default: 100%)
        --bridge-fan-speed  Fan speed to use when bridging (default: 100%)
        --fan-below-layer-time Enable fan if layer print time is below this approximate number 
                            of seconds (default: 60)
        --slowdown-below-layer-time Slow down if layer print time is below this approximate number
                            of seconds (default: 15)
        --min-print-speed   Minimum print speed (mm/s, default: 10)
        --disable-fan-first-layers Disable fan for the first N layers (default: 1)
        --fan-always-on     Keep fan always on at min fan speed, even for layers that don't need
                            cooling
       
       Skirt options:
        --skirts            Number of skirts to draw (0+, default: 1)
        --skirt-distance    Distance in mm between innermost skirt and object 
                            (default: 6)
        --skirt-height      Height of skirts to draw (expressed in layers, 0+, default: 1)
        --min-skirt-length  Generate no less than the number of loops required to consume this length
                            of filament on the first layer, for each extruder (mm, 0+, default: 0)
        --brim-width        Width of the brim that will get added to each object to help adhesion
                            (mm, default: 0)
       
       Transform options:
        --scale             Factor for scaling input object (default: 1)
        --rotate            Rotation angle in degrees (0-360, default: 0)
        --duplicate         Number of items with auto-arrange (1+, default: 1)
        --bed-size          Bed size, only used for auto-arrange (mm, default: 200,200)
        --duplicate-grid    Number of items with grid arrangement (default: 1,1)
        --duplicate-distance Distance in mm between copies (default: 6)
       
       Sequential printing options:
        --complete-objects  When printing multiple objects and/or copies, complete each one before
                            starting the next one; watch out for extruder collisions (default: no)
        --extruder-clearance-radius Radius in mm above which extruder won't collide with anything
                            (default: 20)
        --extruder-clearance-height Maximum vertical extruder depth; i.e. vertical distance from 
                            extruder tip and carriage bottom (default: 20)
       
       Miscellaneous options:
        --notes             Notes to be added as comments to the output file
      
       Flow options (advanced):
        --extrusion-width   Set extrusion width manually; it accepts either an absolute value in mm
                            (like 0.65) or a percentage over layer height (like 200%)
        --first-layer-extrusion-width
                            Set a different extrusion width for first layer
        --perimeter-extrusion-width
                            Set a different extrusion width for perimeters
        --infill-extrusion-width
                            Set a different extrusion width for infill
        --support-material-extrusion-width
                            Set a different extrusion width for support material
        --bridge-flow-ratio Multiplier for extrusion when bridging (> 0, default: 1)
  
       Multiple extruder options:
        --extruder-offset   Offset of each extruder, if firmware doesn't handle the displacement
                            (can be specified multiple times, default: 0x0)
        --perimeter-extruder
                            Extruder to use for perimeters (1+, default: 1)
        --infill-extruder   Extruder to use for infill (1+, default: 1)
        --support-material-extruder
                            Extruder to use for support material (1+, default: 1)

If you want to change a preset file, just do

    slic3r.pl --load config.ini --layer-height 0.25 --save config.ini

If you want to slice a file overriding an option contained in your preset file:

    slic3r.pl --load config.ini --layer-height 0.25 file.stl
