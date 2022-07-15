#version 110

varying vec3 eye_normal;

void main()
{
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    eye_normal = gl_NormalMatrix * gl_Normal;
}
