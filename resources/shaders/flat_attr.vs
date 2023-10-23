#version 110

attribute vec3 v_position;

uniform mat4 projection_view_model_matrix;

void main()
{
    gl_Position = projection_view_model_matrix * vec4(v_position, 1.0);
}
