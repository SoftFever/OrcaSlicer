#version 120

uniform float zoom;
// x = min, y = max
uniform vec2 point_sizes;

void main()
{
    gl_PointSize = clamp(zoom, point_sizes.x, point_sizes.y);
    gl_Position = ftransform();    
}
