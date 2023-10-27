#version 140

const float EPSILON = 0.0001;

uniform vec4 uniform_color;

void main()
{
    gl_FragColor = uniform_color;
    // Values inside depth buffer for fragments of the contour of a selected area are offset
    // by small epsilon to solve z-fighting between painted triangles and contour lines.
    gl_FragDepth = gl_FragCoord.z - EPSILON;
}
