// version 120 is needed for gl_PointCoord
#version 120

uniform vec4 uniform_color;
uniform float percent_outline_radius;
uniform float percent_center_radius;

vec4 calc_color(float radius, vec4 color)
{
    return ((radius < percent_center_radius) || (radius > 1.0 - percent_outline_radius)) ?
            vec4(0.5 * color.rgb, color.a) : color;
}

void main()
{
    vec2 pos = (gl_PointCoord - 0.5) * 2.0;
    float radius = length(pos);
    if (radius > 1.0)
        discard;

    gl_FragColor = calc_color(radius, uniform_color);
}
