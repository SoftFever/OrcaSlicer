#version 140

uniform vec4 uniform_color;
uniform float emission_factor;

// x = tainted, y = specular;
in vec2 intensity;

out vec4 out_color;

void main()
{
    out_color = uniform_color;
}
