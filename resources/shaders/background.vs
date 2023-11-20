#version 110

varying vec2 tex_coord;

void main()
{
    gl_Position = gl_Vertex;
	tex_coord = gl_MultiTexCoord0.xy;
}
