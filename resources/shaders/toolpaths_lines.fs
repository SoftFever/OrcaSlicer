#version 110

// normalized values for (-0.6/1.31, 0.6/1.31, 1./1.31)
const vec3 LIGHT_TOP_DIR = vec3(-0.4574957, 0.4574957, 0.7624929);
const vec3 LIGHT_FRONT_DIR = vec3(0.0, 0.0, 1.0);

// x = ambient, y = top diffuse, z = front diffuse, w = global
uniform vec4 light_intensity;
uniform vec4 uniform_color;

varying vec3 eye_normal;

void main()
{
    vec3 normal = normalize(eye_normal);

    // Compute the cos of the angle between the normal and lights direction. The light is directional so the direction is constant for every vertex.
    // Since these two are normalized the cosine is the dot product. Take the abs value to light the lines no matter in which direction the normal points.
    float NdotL = abs(dot(normal, LIGHT_TOP_DIR));

    float intensity = light_intensity.x + NdotL * light_intensity.y;

    // Perform the same lighting calculation for the 2nd light source.
    NdotL = abs(dot(normal, LIGHT_FRONT_DIR));
    intensity += NdotL * light_intensity.z;    

    gl_FragColor = vec4(uniform_color.rgb * light_intensity.w * intensity, uniform_color.a);
}
