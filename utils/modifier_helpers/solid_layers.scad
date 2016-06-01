// Used to generate a modifier mesh to do something every few layers. 
// Load into OpenSCAD, tweak the variables below, export as STL and load as 
// a modifier mesh. Then change settings for the modifier mesh.

// Written by Joseph Lenox; in public domain.

layer_height = 0.3; // set to layer height in slic3r for "best" results.
number_of_solid_layers = 2;
N = 4; // N > number_of_solid_layers or else the whole thing will be solid
model_height = 300.0;
model_width = 300.0; // these two should be at least as big as the model 
model_depth = 300.0; // but bigger isn't a problem
initial_offset=0; // don't generate below this

position_on_bed=[0,0,0]; // in case you need to move it around

// don't touch below unless you know what you are doing.
simple_layers = round(model_height/layer_height);
translate(position_on_bed)
  for (i = [initial_offset:N:simple_layers]) {
    translate([0,0,i*layer_height])
      translate([0,0,(layer_height*number_of_solid_layers)/2])
      cube([model_width,model_depth,layer_height*number_of_solid_layers], center=true);
  }
