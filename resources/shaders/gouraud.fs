#version 110

const vec3 ZERO = vec3(0.0, 0.0, 0.0);
const vec3 GREEN = vec3(0.0, 0.7, 0.0);
const vec3 YELLOW = vec3(0.5, 0.7, 0.0);
const vec3 RED = vec3(0.7, 0.0, 0.0);
const float EPSILON = 0.0001;

struct SlopeDetection
{
    bool actived;
	// x = yellow, y = red
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
    float gradient_range = slope.z_range.x - slope.z_range.y;
    return (world_normal_z > slope.z_range.x - EPSILON) ? GREEN : ((gradient_range == 0.0) ? RED : mix(RED, YELLOW, clamp((world_normal_z - slope.z_range.y) / gradient_range, 0.0, 1.0)));
}

void main()
{
    if (any(lessThan(clipping_planes_dots, ZERO)))
        discard;
	vec3 color = slope.actived ? slope_color() : uniform_color.rgb;
    // if the fragment is outside the print volume -> use darker color
	color = (any(lessThan(delta_box_min, ZERO)) || any(greaterThan(delta_box_max, ZERO))) ? mix(color, ZERO, 0.3333) : color;
    gl_FragColor = vec4(vec3(intensity.y, intensity.y, intensity.y) + color * intensity.x, uniform_color.a);
}
