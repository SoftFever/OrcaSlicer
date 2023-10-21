#version 110

varying vec2 tex_coords;

void main()
{
    gl_Position = ftransform();
	tex_coords = gl_MultiTexCoord0.xy;
}
