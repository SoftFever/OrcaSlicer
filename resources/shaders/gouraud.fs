#version 110

const vec3 ZERO = vec3(0.0, 0.0, 0.0);
const vec3 GREEN = vec3(0.0, 0.7, 0.0);
const vec3 YELLOW = vec3(0.5, 0.7, 0.0);
const vec3 RED = vec3(0.7, 0.0, 0.0);
const float EPSILON = 0.0001;

struct SlopeDetection
{
    bool actived;
	float normal_z;
    mat3 volume_world_normal_matrix;
};

uniform vec4 uniform_color;
uniform SlopeDetection slope;

#if ENABLE_ENVIRONMENT_MAP
    uniform sampler2D environment_tex;
    uniform bool use_environment_tex;
#endif // ENABLE_ENVIRONMENT_MAP

varying vec3 clipping_planes_dots;

// x = tainted, y = specular;
varying vec2 intensity;

varying vec3 delta_box_min;
varying vec3 delta_box_max;

varying float world_normal_z;
varying vec3 eye_normal;

vec3 slope_color()
{
    return (world_normal_z > slope.normal_z - EPSILON) ? GREEN : RED;
}

void main()
{
    if (any(lessThan(clipping_planes_dots, ZERO)))
        discard;
	vec3 color = slope.actived ? slope_color() : uniform_color.rgb;
    // if the fragment is outside the print volume -> use darker color
	color = (any(lessThan(delta_box_min, ZERO)) || any(greaterThan(delta_box_max, ZERO))) ? mix(color, ZERO, 0.3333) : color;
#if ENABLE_ENVIRONMENT_MAP
    if (use_environment_tex)
        gl_FragColor = vec4(0.45 * texture2D(environment_tex, normalize(eye_normal).xy * 0.5 + 0.5).xyz + 0.8 * color * intensity.x, uniform_color.a);
    else
#endif
        gl_FragColor = vec4(vec3(intensity.y) + color * intensity.x, uniform_color.a);
}
