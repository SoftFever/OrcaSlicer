#version 110

const vec3 ZERO = vec3(0.0, 0.0, 0.0);
const vec3 GREEN = vec3(0.0, 0.65, 0.0);
const vec3 YELLOW = vec3(0.5, 0.65, 0.0);
const vec3 RED = vec3(0.65, 0.0, 0.0);
const float EPSILON = 0.0001;

struct SlopeDetection
{
    bool active;
	// x = yellow z, y = red z
	vec2 z_range;
    mat3 volume_world_normal_matrix;
};

uniform vec4 uniform_color;
uniform SlopeDetection slope;

varying vec3 clipping_planes_dots;

// x = tainted, y = specular;
varying vec2 intensity;

varying vec3 delta_box_min;
varying vec3 delta_box_max;

varying float world_normal_z;

vec3 slope_color()
{
    return (world_normal_z > slope.z_range.x - EPSILON) ? GREEN : mix(RED, YELLOW, (world_normal_z - slope.z_range.y) / (slope.z_range.x - slope.z_range.y));
}

void main()
{
    if (any(lessThan(clipping_planes_dots, ZERO)))
        discard;
	vec3 color = slope.active ? slope_color() : uniform_color.rgb;
    // if the fragment is outside the print volume -> use darker color
	color = (any(lessThan(delta_box_min, ZERO)) || any(greaterThan(delta_box_max, ZERO))) ? mix(color, ZERO, 0.3333) : color;
    gl_FragColor = vec4(vec3(intensity.y, intensity.y, intensity.y) + color * intensity.x, uniform_color.a);
}
