#version 120

uniform bool use_fixed_screen_size;
uniform float zoom;
uniform float point_size;
uniform float near_plane_height;

float fixed_screen_size()
{
    return point_size;
}

float fixed_world_size()
{
    return (gl_Position.w == 1.0) ? zoom * near_plane_height * point_size : near_plane_height * point_size / gl_Position.w;
}

void main()
{
    gl_Position = ftransform();
    gl_PointSize = use_fixed_screen_size ? fixed_screen_size() : fixed_world_size();
}
