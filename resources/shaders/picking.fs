#version 110

uniform vec4 uniform_color;
uniform bool viewed_from_top;

varying float world_pos_z;

void main()
{
	if (viewed_from_top && world_pos_z < 0.0)
		discard;
	
    gl_FragColor = uniform_color;
}
