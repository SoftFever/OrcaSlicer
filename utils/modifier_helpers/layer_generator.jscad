// title: Layer_generator
// written by: Joseph Lenox
// Used for generating cubes oriented about the center 
// for making simple modifier meshes.

var width = 100;
var layer_height = 0.3;
var z = 30;
function main() {
    
    return cube(size=[width,width,layer_height], center=true).translate([0,0,z]);    
}
function getParameterDefinitions() {
  return [
      { name: 'width', type: 'float', initial: 100, caption: "Width of the cube:" },
      { name: 'layer_height', type: 'float', initial: 0.3, caption: "Layer height used:" },
      { name: 'z', type: 'float', initial: 0, caption: "Z:" }
  ];
}
