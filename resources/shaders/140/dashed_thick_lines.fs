#version 150

// see as reference: https://github.com/mhalber/Lines/blob/master/geometry_shader_lines.h
//                   https://stackoverflow.com/questions/52928678/dashed-line-in-opengl3

const vec2 aa_radius = vec2(0.5);

uniform float dash_size;
uniform float gap_size;
uniform vec4 uniform_color;

in float line_width;
in float line_length;
in vec3 uvs;

out vec4 out_color;

void main()
{
    if (gap_size > 0.0 && fract(uvs.z / (dash_size + gap_size)) > dash_size / (dash_size + gap_size))
        discard;
		
	// We render a quad that is fattened by r, giving total width of the line to be w+r. We want smoothing to happen
	// around w, so that the edge is properly smoothed out. As such, in the smoothstep function we have:
	// Far edge   : 1.0                                          = (w+r) / (w+r)
	// Close edge : 1.0 - (2r / (w+r)) = (w+r)/(w+r) - 2r/(w+r)) = (w-r) / (w+r)
	// This way the smoothing is centered around 'w'.

	out_color = uniform_color;
	float au = 1.0 - smoothstep(1.0 - (2.0 * aa_radius[0] / line_width),  1.0, abs(uvs.x / line_width));
	float av = 1.0 - smoothstep(1.0 - (2.0 * aa_radius[1] / line_length), 1.0, abs(uvs.y / line_length));
	out_color.a *= min(av, au);
}
