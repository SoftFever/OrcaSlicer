#version 110

varying vec3 eye_position;
varying vec3 eye_normal;

void main()
{
    eye_position = (gl_ModelViewMatrix * gl_Vertex).xyz;
    eye_normal = gl_NormalMatrix * vec3(0.0, 0.0, 1.0);
    gl_Position = ftransform();
}
