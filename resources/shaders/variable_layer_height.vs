#version 110

#define INTENSITY_CORRECTION 0.6

const vec3 LIGHT_TOP_DIR = vec3(-0.4574957, 0.4574957, 0.7624929);
#define LIGHT_TOP_DIFFUSE    (0.8 * INTENSITY_CORRECTION)
#define LIGHT_TOP_SPECULAR   (0.125 * INTENSITY_CORRECTION)
#define LIGHT_TOP_SHININESS  20.0

const vec3 LIGHT_FRONT_DIR = vec3(0.6985074, 0.1397015, 0.6985074);
#define LIGHT_FRONT_DIFFUSE  (0.3 * INTENSITY_CORRECTION)
//#define LIGHT_FRONT_SPECULAR (0.0 * INTENSITY_CORRECTION)
//#define LIGHT_FRONT_SHININESS 5.0

#define INTENSITY_AMBIENT    0.3

// x = tainted, y = specular;
varying vec2 intensity;

varying float object_z;

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

    // Perform the same lighting calculation for the 2nd light source (no specular)
    NdotL = max(dot(normal, LIGHT_FRONT_DIR), 0.0);
    
    intensity.x += NdotL * LIGHT_FRONT_DIFFUSE;
    
    // Scaled to widths of the Z texture.
    object_z = gl_Vertex.z;

    gl_Position = ftransform();
}
