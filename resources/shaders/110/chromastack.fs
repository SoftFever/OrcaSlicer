#version 110

// What a ChromaStack print looks like, with the stack composited per fragment.
//
// The colour is not a vertex attribute, not a texture on the surface and not a table: the fragment
// walks the filament stack itself, the same Beer-Lambert absorption the solver used, from its own
// height. A table indexed by height can only hold the answer for one geometry - a column seen
// straight down - and the two places that is not true are exactly where a print is looked at:
//
//  - at a grazing angle the light leaving the surface crosses each layer along the view ray rather
//    than straight up, so it passes through more filament and the colour deepens;
//  - a wall is not a column seen through at all. It is the cut side of the layers at that height,
//    backed by more of the same filament, so it shows that band's own colour - which is why the side
//    of a ChromaStack print is a set of stripes and its top is a picture.
//
// Per band rather than per layer, which is exact rather than a shortcut: a run of n layers of one
// filament passes exp(-n d) whether it is composited in one step or n, so a stack of sixteen bands
// costs sixteen iterations however many hundreds of layers it has.

#define INTENSITY_CORRECTION 0.6
const vec3 LIGHT_TOP_DIR = vec3(-0.4574957, 0.4574957, 0.7624929);
#define LIGHT_TOP_DIFFUSE    (0.8 * INTENSITY_CORRECTION)
#define LIGHT_TOP_SPECULAR   (0.125 * INTENSITY_CORRECTION)
#define LIGHT_TOP_SHININESS  20.0
const vec3 LIGHT_FRONT_DIR = vec3(0.6985074, 0.1397015, 0.6985074);
#define LIGHT_FRONT_DIFFUSE  (0.3 * INTENSITY_CORRECTION)
#define INTENSITY_AMBIENT    0.3

#define CHROMA_MAX_BANDS 16

// The stack, bottom-up. Band 0 carries the solid base folded into it, since the base prints in its
// filament and the model treats it as opaque anyway.
uniform int   band_count;
uniform vec3  band_color[CHROMA_MAX_BANDS];
// The optical depth of ONE layer of that band: h / (CHROMA_TD_OPAQUE_FRACTION * TD / 3), so the layer
// passes exp(-density) of what is behind it. Precomputed because TD, the layer height and the
// constants that relate them belong to the model, not to a shader.
uniform float band_density[CHROMA_MAX_BANDS];
uniform float band_layers[CHROMA_MAX_BANDS];
uniform float total_layers;

uniform float base_z;        // world Z of the bottom of the object
uniform float layer_height;  // mm
// World +Z in eye space: the direction the stack is built along, which is what the view ray's path
// through it is measured against.
uniform vec3  stack_axis;
// How translucent the stack reads overall, 0 to 1, from the filament on top. Drives the wrap and the
// rim; an opaque stack should not glow.
uniform float translucency;
uniform float alpha;

varying vec3  clipping_planes_dots;
varying vec3  eye_normal;
varying vec3  eye_position;
varying vec3  world_normal;
varying float world_z;

void main()
{
    if (any(lessThan(clipping_planes_dots, vec3(0.0))))
        discard;

    float z_rel  = world_z - base_z;
    float layers = max(z_rel / layer_height, 0.0);
    // How much of a layer one pixel covers. Past half a layer the height is taken as continuous, so a
    // surface too far away to resolve its layers averages them instead of flickering between them.
    float px      = fwidth(layers);
    float stepped = clamp(ceil(layers - 1.0e-4), 1.0, total_layers);
    float here    = mix(stepped, clamp(layers, 0.0, total_layers), clamp(px - 0.5, 0.0, 1.0));

    vec3 N = normalize(eye_normal);
    vec3 V = normalize(-eye_position);

    // The view ray's path through one layer, in layers. Straight down it is 1; at 60 degrees off the
    // stacking axis it is 2. Clamped because a ray along the layers passes through no stack at all,
    // and that case is the wall below rather than an infinite path.
    float facing = clamp(abs(dot(normalize(stack_axis), V)), 0.0, 1.0);
    float path   = 1.0 / max(facing, 0.25);

    // Walk the bands from the bottom, stopping at the layer this fragment sits at. `own` is the
    // filament of the band that layer belongs to, which is what the side of the print shows.
    vec3  composite = band_color[0];
    vec3  own       = band_color[0];
    float consumed  = 0.0;
    for (int i = 0; i < CHROMA_MAX_BANDS; ++i) {
        if (i >= band_count)
            break;
        float n = clamp(here - consumed, 0.0, band_layers[i]);
        if (n <= 0.0)
            break;
        float t   = exp(-band_density[i] * path * n);
        composite = (1.0 - t) * band_color[i] + t * composite;
        own       = band_color[i];
        consumed += band_layers[i];
    }

    // A terrace is a column seen through; a wall is the layers seen edge on. Everything between is
    // between.
    float up     = clamp(abs(world_normal.z), 0.0, 1.0);
    vec3  albedo = mix(own, composite, smoothstep(0.35, 0.8, up));

    // Layer lines, on the walls only: a terrace is one layer's top surface and has none to show.
    float wall   = 1.0 - up;
    float phase  = fract(layers);
    float groove = smoothstep(0.0, 0.16, phase) * smoothstep(1.0, 0.84, phase);
    groove       = mix(groove, 0.65, clamp(px * 1.5, 0.0, 1.0));
    albedo      *= mix(1.0, 0.84 + 0.16 * groove, wall * 0.9);

    // Diffuse, with a wrapped terminator: light entering translucent filament comes back out a little
    // way round the curve, so the shaded side never goes as dark as an opaque surface's would.
    float wrap    = 0.25 + 0.45 * translucency;
    float diffuse = max((dot(N, LIGHT_TOP_DIR) + wrap) / (1.0 + wrap), 0.0) * LIGHT_TOP_DIFFUSE;
    diffuse      += max((dot(N, LIGHT_FRONT_DIR) + wrap) / (1.0 + wrap), 0.0) * LIGHT_FRONT_DIFFUSE;

    // Specular, strongest along a layer line, which is what makes a printed wall glint in stripes.
    float spec = LIGHT_TOP_SPECULAR * pow(max(dot(V, reflect(-LIGHT_TOP_DIR, N)), 0.0), LIGHT_TOP_SHININESS);
    spec      *= mix(1.0, 0.35 + 0.65 * groove, wall * 0.9);

    // Scattering out of the edges: a translucent part lights up where it is seen at a grazing angle.
    float rim = pow(1.0 - clamp(dot(N, V), 0.0, 1.0), 4.0);

    vec3 color = albedo * (INTENSITY_AMBIENT + diffuse) + vec3(spec) + albedo * rim * translucency * 0.55;
    gl_FragColor = vec4(color, alpha);
}
