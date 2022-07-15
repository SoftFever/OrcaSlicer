#version 110

const vec3 ORANGE = vec3(0.8, 0.4, 0.0);
uniform vec4 uniform_color;

void main()
{
    gl_FragColor = uniform_color;
	//gl_FragColor = vec4(ORANGE, 1.0);
}