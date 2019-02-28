#version 110

attribute vec4 v_position;
attribute vec2 v_tex_coords;

varying vec2 tex_coords;

void main()
{
    gl_Position = gl_ProjectionMatrix * gl_ModelViewMatrix * v_position;
    tex_coords = v_tex_coords;
}
