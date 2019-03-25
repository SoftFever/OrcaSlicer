#version 110

const vec3 ZERO = vec3(0.0, 0.0, 0.0);

// x = tainted, y = specular;
varying vec2 intensity;

varying vec3 delta_box_min;
varying vec3 delta_box_max;

varying vec3 world_pos;

uniform vec4 uniform_color;

// x = min z, y = max z;
uniform vec2 z_range;

// clipping plane (general orientation):
uniform vec4 clipping_plane;

void main()
{
    if ((world_pos.z < z_range.x) || (z_range.y < world_pos.z))
        discard;

    if (world_pos.x*clipping_plane.x + world_pos.y*clipping_plane.y + world_pos.z*clipping_plane.z + clipping_plane.w < 0.f )
        discard;

    // if the fragment is outside the print volume -> use darker color
    vec3 color = (any(lessThan(delta_box_min, ZERO)) || any(greaterThan(delta_box_max, ZERO))) ? mix(uniform_color.rgb, ZERO, 0.3333) : uniform_color.rgb;
    gl_FragColor = vec4(vec3(intensity.y, intensity.y, intensity.y) + color * intensity.x, uniform_color.a);
}
