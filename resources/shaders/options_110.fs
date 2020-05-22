#version 120

uniform vec3 uniform_color;

void main()
{
    gl_FragColor = vec4(uniform_color, 1.0);
}
