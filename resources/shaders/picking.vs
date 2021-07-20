#version 110

uniform mat4 world_matrix;

varying float world_pos_z;

void main()
{
    world_pos_z = (world_matrix * gl_Vertex).z;
    gl_Position = ftransform();
}
