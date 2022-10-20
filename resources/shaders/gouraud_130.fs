#version 130

const vec3 ZERO = vec3(0.0, 0.0, 0.0);
//BBS: add grey and orange
//const vec3 GREY = vec3(0.9, 0.9, 0.9);
const vec3 ORANGE = vec3(0.8, 0.4, 0.0);
const float EPSILON = 0.0001;

struct PrintVolumeDetection
{
	// 0 = rectangle, 1 = circle, 2 = custom, 3 = invalid
	int type;
    // type = 0 (rectangle):
    // x = min.x, y = min.y, z = max.x, w = max.y
    // type = 1 (circle):
    // x = center.x, y = center.y, z = radius
	vec4 xy_data;
    // x = min z, y = max z
	vec2 z_data;
};

struct SlopeDetection
{
    bool actived;
	float normal_z;
    mat3 volume_world_normal_matrix;
};

//BBS: add wireframe logic
varying vec3 barycentric_coordinates;
float edgeFactor(float lineWidth) {
    vec3 d = fwidth(barycentric_coordinates);
    vec3 a3 = smoothstep(vec3(0.0), d * lineWidth, barycentric_coordinates);
    return min(min(a3.x, a3.y), a3.z);
}

vec3 wireframe(vec3 fill, vec3 stroke, float lineWidth) {
    return mix(stroke, fill, edgeFactor(lineWidth));
}

vec3 getWireframeColor(vec3 fill) {
    float brightness = 0.2126 * fill.r + 0.7152 * fill.g + 0.0722 * fill.b;
    return (brightness > 0.75) ? vec3(0.11, 0.165, 0.208) : vec3(0.988, 0.988, 0.988);
}

uniform vec4 uniform_color;
uniform SlopeDetection slope;

//BBS: add outline_color
uniform bool is_outline;
uniform bool show_wireframe;

uniform bool offset_depth_buffer;

#ifdef ENABLE_ENVIRONMENT_MAP
    uniform sampler2D environment_tex;
    uniform bool use_environment_tex;
#endif // ENABLE_ENVIRONMENT_MAP

varying vec3 clipping_planes_dots;

// x = diffuse, y = specular;
varying vec2 intensity;

uniform PrintVolumeDetection print_volume;

varying vec4 model_pos;
varying vec4 world_pos;
varying float world_normal_z;
varying vec3 eye_normal;

void main()
{
    if (any(lessThan(clipping_planes_dots, ZERO)))
        discard;
    vec3  color = uniform_color.rgb;
    float alpha = uniform_color.a;

    if (slope.actived && world_normal_z < slope.normal_z - EPSILON) {
        //color = vec3(0.7, 0.7, 1.0);
        color = ORANGE;
        alpha = 1.0;
    }
	
    // if the fragment is outside the print volume -> use darker color
    vec3 pv_check_min = ZERO;
    vec3 pv_check_max = ZERO;
    if (print_volume.type == 0) {
        // rectangle
        pv_check_min = world_pos.xyz - vec3(print_volume.xy_data.x, print_volume.xy_data.y, print_volume.z_data.x);
        pv_check_max = world_pos.xyz - vec3(print_volume.xy_data.z, print_volume.xy_data.w, print_volume.z_data.y);
        color = (any(lessThan(pv_check_min, ZERO)) || any(greaterThan(pv_check_max, ZERO))) ? mix(color, ZERO, 0.3333) : color;
    }
    else if (print_volume.type == 1) {
        // circle
        float delta_radius = print_volume.xy_data.z - distance(world_pos.xy, print_volume.xy_data.xy);
        pv_check_min = vec3(delta_radius, 0.0, world_pos.z - print_volume.z_data.x);
        pv_check_max = vec3(0.0, 0.0, world_pos.z - print_volume.z_data.y);
        color = (any(lessThan(pv_check_min, ZERO)) || any(greaterThan(pv_check_max, ZERO))) ? mix(color, ZERO, 0.3333) : color;
    }

	//BBS: add outline_color
	if (is_outline)
		gl_FragColor = uniform_color;
#ifdef ENABLE_ENVIRONMENT_MAP
  else if (use_environment_tex)
        gl_FragColor = vec4(0.45 * texture2D(environment_tex, normalize(eye_normal).xy * 0.5 + 0.5).xyz + 0.8 * color * intensity.x, alpha);
#endif
	else {
        //gl_FragColor = vec4(vec3(intensity.y) + color * intensity.x, alpha);
		if (show_wireframe) {
			vec3 wireframeColor = show_wireframe ? getWireframeColor(color) : color;
			vec3 triangleColor = wireframe(color, wireframeColor, 1.0);
			gl_FragColor = vec4(vec3(intensity.y) + triangleColor * intensity.x, alpha);
		}
		else {
			gl_FragColor = vec4(vec3(intensity.y) + color * intensity.x, alpha);
		}
	}
    // In the support painting gizmo and the seam painting gizmo are painted triangles rendered over the already
    // rendered object. To resolved z-fighting between previously rendered object and painted triangles, values
    // inside the depth buffer are offset by small epsilon for painted triangles inside those gizmos.
    gl_FragDepth = gl_FragCoord.z - (offset_depth_buffer ? EPSILON : 0.0);
}