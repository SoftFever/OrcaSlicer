// version 120 is needed for gl_PointCoord
#version 120

uniform vec3 uniform_color;
uniform float percent_outline_radius;
uniform float percent_center_radius;

vec4 hardcoded_color(float radius, vec3 color)
{
    return ((radius < 0.15) || (radius > 0.85)) ? vec4(0.5 * color, 1.0) : vec4(color, 1.0);
}

vec4 customizable_color(float radius, vec3 color)
{
    return ((radius < percent_center_radius) || (radius > 1.0 - percent_outline_radius)) ?
            vec4(0.5 * color, 1.0) : vec4(color, 1.0);
}

void main()
{
    vec2 pos = (gl_PointCoord - 0.5) * 2.0;
    float radius = length(pos);
    if (radius > 1.0)
        discard;

    gl_FragColor = customizable_color(radius, uniform_color);
}
