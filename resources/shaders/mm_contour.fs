#version 110

const float EPSILON = 0.0001;

void main()
{
    gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
    // Values inside depth buffer for fragments of the contour of a selected area are offset
    // by small epsilon to solve z-fighting between painted triangles and contour lines.
    gl_FragDepth = gl_FragCoord.z - EPSILON;
}
