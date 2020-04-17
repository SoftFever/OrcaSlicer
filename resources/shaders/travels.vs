#version 110

varying vec3 eye_position;
varying vec3 eye_normal;
//// world z component of the normal used to darken the lower side of the toolpaths
//varying float world_normal_z;

void main()
{
    eye_position = (gl_ModelViewMatrix * gl_Vertex).xyz;
    eye_normal = gl_NormalMatrix * vec3(0.0, 0.0, 1.0);
//    eye_normal = gl_NormalMatrix * gl_Normal;
//	world_normal_z = gl_Normal.z;
    gl_Position = ftransform();
}
