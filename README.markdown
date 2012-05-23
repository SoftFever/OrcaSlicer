_Q: Oh cool, a new RepRap slicer?_

A: Yes.

# Slic3r

## What's it?

Slic3r is an STL-to-GCODE translator for RepRap 3D printers, aiming to
be a modern and fast alternative to Skeinforge.

See the [project homepage](http://slic3r.org/) at slic3r.org
for more information.

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

Sure! Send patches and/or drop me a line at aar@cpan.org. You can also 
find me in #reprap and in #slic3r on FreeNode with the nickname _Sound_.

## What's Slic3r license?

Slic3r is licensed under the _GNU Affero General Public License, version 3_.
The author is Alessandro Ranellucci.

## How can I invoke slic3r.pl using the command line?

    Usage: slic3r.pl [ OPTIONS ] file.stl
    
        --help              Output this usage screen and exit
        --save <file>       Save configuration to the specified file
        --load <file>       Load configuration from the specified file. It can be used 
                            more than once to load options from multiple files.
        -o, --output <file> File to output gcode to (by default, the file will be saved
                            into the same directory as the input file using the 
                            --output-filename-format to generate the filename)
        -j, --threads <num> Number of threads to use (1+, default: 4) 
      
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
        --gcode-flavor      The type of G-code to generate (reprap/teacup/makerbot/mach3/no-extrusion,
                            default: reprap)
        --use-relative-e-distances Enable this to get relative E values
        --gcode-arcs        Use G2/G3 commands for native arcs (experimental, not supported
                            by all firmwares)
        --g0                Use G0 commands for retraction (experimental, not supported by all
                            firmwares)
        --gcode-comments    Make G-code verbose by adding comments (default: no)
        
      Filament options:
        --filament-diameter Diameter in mm of your raw filament (default: 3)
        --extrusion-multiplier
                            Change this to alter the amount of plastic extruded. There should be
                            very little need to change this value, which is only useful to 
                            compensate for filament packing (default: 1)
        --temperature       Extrusion temperature in degree Celsius, set 0 to disable (default: 200)
        --first-layer-temperature Extrusion temperature for the first layer, in degree Celsius,
                            set 0 to disable (default: same as --temperature)
        --bed-temperature   Heated bed temperature in degree Celsius, set 0 to disable (default: 200)
        --first-layer-bed-temperature Heated bed temperature for the first layer, in degree Celsius,
                            set 0 to disable (default: same as --bed-temperature)
        
      Speed options:
        --travel-speed      Speed of non-print moves in mm/s (default: 130)
        --perimeter-speed   Speed of print moves for perimeters in mm/s (default: 30)
        --small-perimeter-speed
                            Speed of print moves for small perimeters in mm/s (default: 30)
        --infill-speed      Speed of print moves in mm/s (default: 60)
        --solid-infill-speed Speed of print moves for solid surfaces in mm/s (default: 60)
        --bridge-speed      Speed of bridge print moves in mm/s (default: 60)
        --bottom-layer-speed-ratio
                            Factor to increase/decrease speeds on bottom 
                            layer by (default: 0.3)
        
      Accuracy options:
        --layer-height      Layer height in mm (default: 0.4)
        --first-layer-height-ratio
                            Multiplication factor for the height to slice and print the first
                            layer with (> 0, default: 1)
        --infill-every-layers
                            Infill every N layers (default: 1)
      
      Print options:
        --perimeters        Number of perimeters/horizontal skins (range: 0+, default: 3)
        --solid-layers      Number of solid layers to do for top/bottom surfaces
                            (range: 1+, default: 3)
        --fill-density      Infill density (range: 0-1, default: 0.4)
        --fill-angle        Infill angle in degrees (range: 0-90, default: 45)
        --fill-pattern      Pattern to use to fill non-solid layers (default: rectilinear)
        --solid-fill-pattern Pattern to use to fill solid layers (default: rectilinear)
        --start-gcode       Load initial gcode from the supplied file. This will overwrite
                            the default command (home all axes [G28]).
        --end-gcode         Load final gcode from the supplied file. This will overwrite 
                            the default commands (turn off temperature [M104 S0],
                            home X axis [G28 X], disable motors [M84]).
        --layer-gcode       Load layer-change G-code from the supplied file (default: nothing).
        --support-material  Generate support material for overhangs
        --randomize-start   Randomize starting point across layers (default: yes)
      
       Retraction options:
        --retract-length    Length of retraction in mm when pausing extrusion 
                            (default: 1)
        --retract-speed     Speed for retraction in mm/s (default: 30)
        --retract-restart-extra
                            Additional amount of filament in mm to push after
                            compensating retraction (default: 0)
        --retract-before-travel
                            Only retract before travel moves of this length in mm (default: 2)
        --retract-lift      Lift Z by the given distance in mm when retracting (default: 0)
       
       Cooling options:
        --cooling           Enable fan and cooling control
        --min-fan-speed     Minimum fan speed (default: 35%)
        --max-fan-speed     Maximum fan speed (default: 100%)
        --bridge-fan-speed  Fan speed to use when bridging (default: 100%)
        --fan-below-layer-time Enable fan if layer print time is below this approximate number 
                            of seconds (default: 60)
        --slowdown-below-layer-time Slow down if layer print time is below this approximate number
                            of seconds (default: 15)
        --min-print-speed   Minimum print speed speed (mm/s, default: 10)
        --disable-fan-first-layers Disable fan for the first N layers (default: 1)
        --fan-always-on     Keep fan always on at min fan speed, even for layers that don't need
                            cooling
       
       Skirt options:
        --skirts            Number of skirts to draw (0+, default: 1)
        --skirt-distance    Distance in mm between innermost skirt and object 
                            (default: 6)
        --skirt-height      Height of skirts to draw (expressed in layers, 0+, default: 1)
       
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
        --extrusion-width-ratio
                            Calculate the extrusion width as the layer height multiplied by
                            this value (> 0, default: calculated automatically)
        --bridge-flow-ratio Multiplier for extrusion when bridging (> 0, default: 1)
        


If you want to change a preset file, just do

    slic3r.pl --load config.ini --layer-height 0.25 --save config.ini

If you want to slice a file overriding an option contained in your preset file:

    slic3r.pl --load config.ini --layer-height 0.25 file.stl

## How can I integrate Slic3r with Pronterface?

Put this into *slicecommand*:

    slic3r.pl $s --load config.ini --output $o

And this into *sliceoptscommand*:

    slic3r.pl --load config.ini --ignore-nonexistent-config

Replace `slic3r.pl` with the full path to the slic3r executable and `config.ini`
with the full path of your config file (put it in your home directory or where
you like).
On Mac, the executable has a path like this:

    /Applications/Slic3r.app/Contents/MacOS/slic3r

## How can I specify a custom filename format for output G-code files?

You can specify a filename format by using any of the config options. 
Just enclose them in square brackets, and Slic3r will replace them upon
exporting.
The additional `[input_filename]` and `[input_filename_base]` options will
be replaced by the input file name (in the second case, the .stl extension 
is stripped).

The default format is `[input_filename_base].gcode`, meaning that if you slice
a *foo.stl* file, the output will be saved to *foo.gcode*.

See below for more complex examples:

    [input_filename_base]_h[layer_height]_p[perimeters]_s[solid_layers].gcode
    [input_filename]_center[print_center]_[layer_height]layers.gcode

