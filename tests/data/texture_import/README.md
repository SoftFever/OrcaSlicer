`two_color_jpeg.glb` is a generated, closed tetrahedron with four faces, UVs, and
an embedded 8×8 red/blue JPEG base-color texture. Its coordinates span 20 mm.

The compact model exercises JPEG decoding, automatic color clustering, geometry
smoothing, and the filament-matching dialog without mesh repair or units prompts.

`two_color_sphere/test_two_color_sphere.obj` and its matching MTL are the original
red/blue material-color sphere supplied by RF47 in
[the PR discussion](https://github.com/OrcaSlicer/OrcaSlicer/pull/16607#issuecomment-6244475095).
They contain solid `Kd` colors and no texture images.

`test_two_color_sphere_textured.obj` is a derived version of that sphere. Its MTL
uses white `Kd` values and references `textures/two_color.jpg` by a relative path;
the two face groups sample different halves of that image through their UVs.
The JPEG is extracted unchanged from `two_color_jpeg.glb`. This case must retain
both colors from the external texture, rather than fall back to MTL colors.
