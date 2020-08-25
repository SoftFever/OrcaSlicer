#version 110

varying vec3 eye_position;
varying vec3 eye_normal;

vec3 world_normal()
{
    // the world normal is always parallel to the world XY plane
    // the x component is stored into gl_Vertex.w
    float x = gl_Vertex.w;
    float y = sqrt(1.0 - x * x);
    return vec3(x, y, 0.0);
}

void main()
{
    vec4 world_position = vec4(gl_Vertex.xyz, 1.0);
    gl_Position = gl_ModelViewProjectionMatrix * world_position;
    eye_position = (gl_ModelViewMatrix * world_position).xyz;
    eye_normal = gl_NormalMatrix * world_normal();    
}
