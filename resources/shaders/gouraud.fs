#version 110

#define INTENSITY_CORRECTION 0.6

// normalized values for (-0.6/1.31, 0.6/1.31, 1./1.31)
const vec3 LIGHT_TOP_DIR = vec3(-0.4574957, 0.4574957, 0.7624929);
#define LIGHT_TOP_DIFFUSE    (0.8 * INTENSITY_CORRECTION)
#define LIGHT_TOP_SPECULAR   (0.125 * INTENSITY_CORRECTION)
#define LIGHT_TOP_SHININESS  20.0

// normalized values for (1./1.43, 0.2/1.43, 1./1.43)
const vec3 LIGHT_FRONT_DIR = vec3(0.6985074, 0.1397015, 0.6985074);
#define LIGHT_FRONT_DIFFUSE  (0.3 * INTENSITY_CORRECTION)

#define INTENSITY_AMBIENT    0.3

const vec3 ZERO = vec3(0.0, 0.0, 0.0);
const vec3 GREEN = vec3(0.0, 0.7, 0.0);
const vec3 YELLOW = vec3(0.5, 0.7, 0.0);
const vec3 RED = vec3(0.7, 0.0, 0.0);
const vec3 WHITE = vec3(1.0, 1.0, 1.0);
const float EPSILON = 0.0001;
const float BANDS_WIDTH = 10.0;

struct SlopeDetection
{
    bool actived;
	float normal_z;
    mat3 volume_world_normal_matrix;
};

uniform vec4 uniform_color;
uniform SlopeDetection slope;

uniform bool offset_depth_buffer;

#ifdef ENABLE_ENVIRONMENT_MAP
    uniform sampler2D environment_tex;
    uniform bool use_environment_tex;
#endif // ENABLE_ENVIRONMENT_MAP

varying vec3 clipping_planes_dots;

// x = diffuse, y = specular;
varying vec2 intensity;

varying vec3 delta_box_min;
varying vec3 delta_box_max;

varying vec4 model_pos;
varying float world_pos_z;
varying float world_normal_z;
varying vec3 eye_normal;

void main()
{
    if (any(lessThan(clipping_planes_dots, ZERO)))
        discard;
    vec3  color = uniform_color.rgb;
    float alpha = uniform_color.a;

    if (slope.actived && world_normal_z < slope.normal_z - EPSILON) {
        color = vec3(0.7, 0.7, 1.0);
        alpha = 1.0;
    }
    // if the fragment is outside the print volume -> use darker color
	color = (any(lessThan(delta_box_min, ZERO)) || any(greaterThan(delta_box_max, ZERO))) ? mix(color, ZERO, 0.3333) : color;
#ifdef ENABLE_ENVIRONMENT_MAP
    if (use_environment_tex)
        gl_FragColor = vec4(0.45 * texture2D(environment_tex, normalize(eye_normal).xy * 0.5 + 0.5).xyz + 0.8 * color * intensity.x, alpha);
    else
#endif
        gl_FragColor = vec4(vec3(intensity.y) + color * intensity.x, alpha);

    // In the support painting gizmo and the seam painting gizmo are painted triangles rendered over the already
    // rendered object. To resolved z-fighting between previously rendered object and painted triangles, values
    // inside the depth buffer are offset by small epsilon for painted triangles inside those gizmos.
    gl_FragDepth = gl_FragCoord.z - (offset_depth_buffer ? EPSILON : 0.0);
}
