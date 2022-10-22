#version 130

const vec3 ZERO = vec3(0.0, 0.0, 0.0);

uniform mat4 volume_world_matrix;
// Clipping plane, x = min z, y = max z. Used by the FFF and SLA previews to clip with a top / bottom plane.
uniform vec2 z_range;
// Clipping plane - general orientation. Used by the SLA gizmo.
uniform vec4 clipping_plane;

varying vec3 clipping_planes_dots;
varying vec4 model_pos;

varying vec3 barycentric_coordinates;

struct SlopeDetection
{
    bool actived;
	float normal_z;
    mat3 volume_world_normal_matrix;
};
uniform SlopeDetection slope;
void main()
{
    model_pos = gl_Vertex;
    // Point in homogenous coordinates.
    vec4 world_pos = volume_world_matrix * gl_Vertex;

    gl_Position = ftransform();
    // Fill in the scalars for fragment shader clipping. Fragments with any of these components lower than zero are discarded.
    clipping_planes_dots = vec3(dot(world_pos, clipping_plane), world_pos.z - z_range.x, z_range.y - world_pos.z);
	
    //compute the Barycentric Coordinates
    int vertexMod3 = gl_VertexID % 3;
    barycentric_coordinates = vec3(float(vertexMod3 == 0), float(vertexMod3 == 1), float(vertexMod3 == 2));
}
