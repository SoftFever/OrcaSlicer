// version 120 is needed for gl_PointCoord
#version 120

uniform vec3 uniform_color;
uniform float percent_outline_radius;
uniform float percent_center_radius;

vec4 hardcoded_color(float sq_radius, vec3 color)
{
    if ((sq_radius < 0.005625) || (sq_radius > 0.180625))
        return vec4(0.5 * color, 1.0);
    else
        return vec4(color, 1.0);
}

vec4 customizable_color(float sq_radius, vec3 color)
{
    float in_radius = 0.5 * percent_center_radius;
    float out_radius = 0.5 * (1.0 - percent_outline_radius);
    if ((sq_radius < in_radius * in_radius) || (sq_radius > out_radius * out_radius))
        return vec4(0.5 * color, 1.0);
    else
        return vec4(color, 1.0);
}

void main()
{
    vec2 pos = gl_PointCoord - vec2(0.5);
    float sq_radius = dot(pos, pos);
    if (sq_radius > 0.25)
        discard;

    gl_FragColor = customizable_color(sq_radius, uniform_color);
//    gl_FragColor = hardcoded_color(sq_radius, uniform_color);
}
