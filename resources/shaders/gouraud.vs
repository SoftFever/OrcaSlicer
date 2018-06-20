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
//#define LIGHT_FRONT_SPECULAR (0.0 * INTENSITY_CORRECTION)
//#define LIGHT_FRONT_SHININESS 5.0

#define INTENSITY_AMBIENT    0.3

const vec3 ZERO = vec3(0.0, 0.0, 0.0);

struct PrintBoxDetection
{
    vec3 min;
    vec3 max;
    // xyz contains the offset, if w == 1.0 detection needs to be performed
    vec4 volume_origin;
};

uniform PrintBoxDetection print_box;

// x = tainted, y = specular;
varying vec2 intensity;

varying vec3 delta_box_min;
varying vec3 delta_box_max;

void main()
{
    // First transform the normal into camera space and normalize the result.
    vec3 normal = normalize(gl_NormalMatrix * gl_Normal);
    
    // Compute the cos of the angle between the normal and lights direction. The light is directional so the direction is constant for every vertex.
    // Since these two are normalized the cosine is the dot product. We also need to clamp the result to the [0,1] range.
    float NdotL = max(dot(normal, LIGHT_TOP_DIR), 0.0);

    intensity.x = INTENSITY_AMBIENT + NdotL * LIGHT_TOP_DIFFUSE;
    intensity.y = 0.0;

    if (NdotL > 0.0)
        intensity.y += LIGHT_TOP_SPECULAR * pow(max(dot(normal, reflect(-LIGHT_TOP_DIR, normal)), 0.0), LIGHT_TOP_SHININESS);

    // Perform the same lighting calculation for the 2nd light source (no specular applied).
    NdotL = max(dot(normal, LIGHT_FRONT_DIR), 0.0);
    intensity.x += NdotL * LIGHT_FRONT_DIFFUSE;

    // compute deltas for out of print volume detection (world coordinates)
    if (print_box.volume_origin.w == 1.0)
    {
        vec3 v = gl_Vertex.xyz + print_box.volume_origin.xyz;
        delta_box_min = v - print_box.min;
        delta_box_max = v - print_box.max;
    }
    else
    {
        delta_box_min = ZERO;
        delta_box_max = ZERO;
    }    

    gl_Position = ftransform();
} 
