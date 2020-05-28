// version 120 is needed for gl_PointCoord
#version 120

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

uniform vec3 uniform_color;
uniform float percent_outline_radius;
uniform float percent_center_radius;

// x = width, y = height
uniform ivec2 viewport_sizes;
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//uniform vec2 z_range;
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
uniform mat4 inv_proj_matrix;

varying vec3 eye_center;

float radius = 0.5;
// x = tainted, y = specular;
vec2 intensity;

vec3 eye_position_from_fragment()
{
    // Convert screen coordinates to normalized device coordinates (NDC)
    vec4 ndc = vec4(
        (gl_FragCoord.x / viewport_sizes.x - 0.5) * 2.0,
        (gl_FragCoord.y / viewport_sizes.y - 0.5) * 2.0,
        (gl_FragCoord.z - 0.5) * 2.0,
        1.0);

    // Convert NDC throuch inverse clip coordinates to view coordinates
    vec4 clip = inv_proj_matrix * ndc;
    return (clip / clip.w).xyz;
}

vec3 eye_position_on_sphere(vec3 eye_fragment_position)
{
    vec3 eye_dir = normalize(eye_fragment_position);
    float a = dot(eye_dir, eye_dir);
    float b = 2.0 * dot(-eye_center, eye_dir);
    float c = dot(eye_center, eye_center) - radius * radius;
    float discriminant = b * b - 4 * a * c;
    float t = -(b + sqrt(discriminant)) / (2.0 * a);
    return t * eye_dir;
}

vec4 on_sphere_color(vec3 eye_on_sphere_position)
{
    vec3 eye_normal = normalize(eye_on_sphere_position - eye_center);

    // Compute the cos of the angle between the normal and lights direction. The light is directional so the direction is constant for every vertex.
    // Since these two are normalized the cosine is the dot product. We also need to clamp the result to the [0,1] range.
    float NdotL = max(dot(eye_normal, LIGHT_TOP_DIR), 0.0);

    intensity.x = INTENSITY_AMBIENT + NdotL * LIGHT_TOP_DIFFUSE;
    intensity.y = LIGHT_TOP_SPECULAR * pow(max(dot(-normalize(eye_on_sphere_position), reflect(-LIGHT_TOP_DIR, eye_normal)), 0.0), LIGHT_TOP_SHININESS);

    // Perform the same lighting calculation for the 2nd light source (no specular applied).
    NdotL = max(dot(eye_normal, LIGHT_FRONT_DIR), 0.0);
    intensity.x += NdotL * LIGHT_FRONT_DIFFUSE;
    
    return vec4(vec3(intensity.y, intensity.y, intensity.y) + uniform_color.rgb * intensity.x, 1.0);
}

float fragment_depth(vec3 eye_pos)
{
    // see: https://stackoverflow.com/questions/10264949/glsl-gl-fragcoord-z-calculation-and-setting-gl-fragdepth
    vec4 clip_pos = gl_ProjectionMatrix * vec4(eye_pos, 1.0);
    float ndc_depth = clip_pos.z / clip_pos.w;

    return (((gl_DepthRange.far - gl_DepthRange.near) * ndc_depth) + gl_DepthRange.near + gl_DepthRange.far) / 2.0;
}

void main()
{
    vec2 pos = gl_PointCoord - vec2(0.5, 0.5);
    float sq_radius = dot(pos, pos);
    if (sq_radius > 0.25)
        discard;
     
    vec3 eye_on_sphere_position = eye_position_on_sphere(eye_position_from_fragment());

    gl_FragDepth = fragment_depth(eye_on_sphere_position);
//    gl_FragDepth = eye_on_sphere_position.z;
//    gl_FragDepth = (eye_on_sphere_position.z - z_range.x) / (z_range.y - z_range.x);
    gl_FragColor = on_sphere_color(eye_on_sphere_position);
}
