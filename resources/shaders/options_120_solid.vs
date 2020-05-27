#version 120

uniform float zoom;
// x = min, y = max
uniform vec2 point_sizes;

varying vec3 eye_center;

void main()
{
    gl_PointSize = clamp(zoom, point_sizes.x, point_sizes.y);
    eye_center = (gl_ModelViewMatrix * gl_Vertex).xyz;
    gl_Position = ftransform();    
}
