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

uniform bool compute_triangle_normals_in_fs;

void main()
{
    if (any(lessThan(clipping_planes_dots, ZERO)))
        discard;
    vec3  color = uniform_color.rgb;
    float alpha = uniform_color.a;

    vec2  intensity_fs      = intensity;
    vec3  eye_normal_fs     = eye_normal;
    float world_normal_z_fs = world_normal_z;
    if (compute_triangle_normals_in_fs) {
        vec3 triangle_normal = normalize(cross(dFdx(model_pos.xyz), dFdy(model_pos.xyz)));

        // First transform the normal into camera space and normalize the result.
        eye_normal_fs = normalize(gl_NormalMatrix * triangle_normal);

        // Compute the cos of the angle between the normal and lights direction. The light is directional so the direction is constant for every vertex.
        // Since these two are normalized the cosine is the dot product. We also need to clamp the result to the [0,1] range.
        float NdotL = max(dot(eye_normal_fs, LIGHT_TOP_DIR), 0.0);

        intensity_fs = vec2(0.0, 0.0);
        intensity_fs.x = INTENSITY_AMBIENT + NdotL * LIGHT_TOP_DIFFUSE;
        vec3 position = (gl_ModelViewMatrix * model_pos).xyz;
        intensity_fs.y = LIGHT_TOP_SPECULAR * pow(max(dot(-normalize(position), reflect(-LIGHT_TOP_DIR, eye_normal_fs)), 0.0), LIGHT_TOP_SHININESS);

        // Perform the same lighting calculation for the 2nd light source (no specular applied).
        NdotL = max(dot(eye_normal_fs, LIGHT_FRONT_DIR), 0.0);
        intensity_fs.x += NdotL * LIGHT_FRONT_DIFFUSE;

        // z component of normal vector in world coordinate used for slope shading
        world_normal_z_fs = slope.actived ? (normalize(slope.volume_world_normal_matrix * triangle_normal)).z : 0.0;
    }

    if (slope.actived && world_normal_z_fs < slope.normal_z - EPSILON) {
        color = vec3(0.7, 0.7, 1.0);
        alpha = 1.0;
    }
    // if the fragment is outside the print volume -> use darker color
	color = (any(lessThan(delta_box_min, ZERO)) || any(greaterThan(delta_box_max, ZERO))) ? mix(color, ZERO, 0.3333) : color;
#ifdef ENABLE_ENVIRONMENT_MAP
    if (use_environment_tex)
        gl_FragColor = vec4(0.45 * texture2D(environment_tex, normalize(eye_normal_fs).xy * 0.5 + 0.5).xyz + 0.8 * color * intensity_fs.x, alpha);
    else
#endif
        gl_FragColor = vec4(vec3(intensity_fs.y) + color * intensity_fs.x, alpha);
}
