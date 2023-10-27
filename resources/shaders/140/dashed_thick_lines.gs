#version 150

// see as reference: https://github.com/mhalber/Lines/blob/master/geometry_shader_lines.h
//                   https://stackoverflow.com/questions/52928678/dashed-line-in-opengl3

layout(lines) in;
layout(triangle_strip, max_vertices = 4) out;

const vec2 aa_radius = vec2(0.5);

uniform vec2 viewport_size;
uniform float width;

in float coord_s[];

out float line_width;
out float line_length;
out vec3 uvs;

void main()
{
	float u_width  = viewport_size[0];
	float u_height = viewport_size[1];

	vec2 ndc_0 = gl_in[0].gl_Position.xy / gl_in[0].gl_Position.w;
	vec2 ndc_1 = gl_in[1].gl_Position.xy / gl_in[1].gl_Position.w;

	vec2 viewport_line_vector = 0.5 * (ndc_1 - ndc_0) * viewport_size;
	vec2 dir = normalize(viewport_line_vector);
	vec2 normal_dir = vec2(-dir.y, dir.x);

	line_width  = max(0.0, width) + 2.0 * aa_radius[0];
	line_length = length(viewport_line_vector) + 2.0 * aa_radius[1];

	vec2 normal    = vec2(line_width / u_width, line_width / u_height) * normal_dir;
    vec2 extension = vec2(aa_radius[1] / u_width, aa_radius[1] / u_height) * dir;

	float half_line_width  = 0.5 * line_width;
	float half_line_length = 0.5 * line_length;

	uvs = vec3(-half_line_width, half_line_length, coord_s[0]);
	gl_Position = vec4((ndc_0 + normal - extension) * gl_in[0].gl_Position.w, gl_in[0].gl_Position.zw);
	EmitVertex();

	uvs = vec3(-half_line_width, -half_line_length, coord_s[0]);
	gl_Position = vec4((ndc_0 - normal - extension) * gl_in[0].gl_Position.w, gl_in[0].gl_Position.zw);
	EmitVertex();

	uvs = vec3(half_line_width, half_line_length, coord_s[1]);
	gl_Position = vec4((ndc_1 + normal + extension) * gl_in[1].gl_Position.w, gl_in[1].gl_Position.zw);
	EmitVertex();

	uvs = vec3(half_line_width, -half_line_length, coord_s[1]);
	gl_Position = vec4((ndc_1 - normal + extension) * gl_in[1].gl_Position.w, gl_in[1].gl_Position.zw);
	EmitVertex();

	EndPrimitive();
}
