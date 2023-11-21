#version 110

uniform vec4 uniform_color;
uniform float emission_factor;

// x = tainted, y = specular;
varying vec2 intensity;
//varying float drop;
varying vec4 world_pos;

void main()
{
    if (world_pos.z < 0.0)
        discard;
    gl_FragColor = vec4(vec3(intensity.y) + uniform_color.rgb * (intensity.x + emission_factor), uniform_color.a);
}
