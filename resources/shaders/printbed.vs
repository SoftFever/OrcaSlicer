#version 110

attribute vec3 v_position;
attribute vec2 v_tex_coords;

varying vec2 tex_coords;

void main()
{
    gl_Position = gl_ModelViewProjectionMatrix * vec4(v_position.x, v_position.y, v_position.z, 1.0);
	// the following line leads to crash on some Intel graphics card
    //gl_Position = gl_ModelViewProjectionMatrix * vec4(v_position, 1.0);
    tex_coords = v_tex_coords;
}
