#version 120

uniform float zoom;
uniform float point_size;
uniform float near_plane_height;

varying vec3 eye_center;

void main()
{
    eye_center = (gl_ModelViewMatrix * gl_Vertex).xyz;
    gl_Position = ftransform();
    gl_PointSize = (gl_Position.w == 1.0) ? zoom * near_plane_height * point_size : near_plane_height * point_size / gl_Position.w;
}
