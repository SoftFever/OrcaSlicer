# Translucent filaments

## Purpose

A filament color can carry an opacity (`#RRGGBBAA`, see [color-picker.md](color-picker.md)). Prepare
draws the parts of such a filament see-through, and Preview their toolpaths, so the inside of a
translucent part and whatever lies behind it show through. Both tabs aim at the same look: the
opacity of the plastic the view ray crosses, matte without realistic view and glossy with it.

## Where the opacity applies

- Only the views that color toolpaths by filament: Filament, Summary and Tool. The other views
  color by feature or by a value, and stay opaque.
- Only extrusions. Travels, wipes and option markers stay opaque, and so do the layers that hold a
  pause or a custom G-code, which these views draw grey.
- A filament with less than 10% opacity is drawn as white at 30%, as `adjust_color_for_rendering()`
  does in Prepare. Otherwise a fully clear filament would not be drawn at all.
- The "Translucent filaments" preference (`preview_translucent_filaments`, on by default) turns all
  of it off: every filament is then drawn opaque, the legend swatches included. It applies to the
  loaded preview at once, through `Viewer::set_translucent_filaments()`.
- The legend swatches carry the same opacity, drawn over the white and grey checkerboard of the
  sidebar's clear filament swatch so that they read on the dark legend.
- The opacity is that of 0.4 mm of the filament. Each translucent segment raises it to the length
  of the view ray through the centre of its diamond section, `1 - (1 - a)^(path / 0.4)`, so thin
  layers stacked under the camera add up as one thick layer would, and a wall seen side on keeps
  about the filament's opacity whatever the layer height.

## Data

`libvgcode::convert_filament_colors()` decodes the filament color strings into a
`libvgcode::PaletteWithAlpha`. libvgcode keeps one RGBA8 color per vertex, in a `GL_RGBA8` texture
buffer, or in RGBA8 2D textures on OpenGL ES. The color depends on the view type, while the alpha
comes from the filament alone. The viewer also notes whether any vertex is translucent.

## Rendering

A segment takes the alpha of its first vertex. While any vertex is translucent,
`update_enabled_entities()` sorts the enabled segments by opacity, the opaque ones first, keeping
the print order within each group; on OpenGL ES, each texture of the list is sorted on its own.
Each pass draws its own group, from an offset into the list. Without a translucent vertex, the list
is not sorted and the second pass is skipped, so the first draws exactly what it drew before.

1. The opaque segments, with depth writes.
2. The option markers.
3. The translucent segments, in premultiplied color blended with `GL_ONE, GL_ONE_MINUS_SRC_ALPHA`,
   depth tested but without depth writes. Opaque toolpaths and objects hide the translucent ones
   behind them, and a translucent segment hides nothing. Drawing the markers first keeps those
   inside a translucent part behind its surface, instead of on top of it.

`Viewer::render()` draws the first two; `Viewer::render_translucent()` draws the third, which
`GLCanvas3D::_render_gcode_translucent()` calls after the SSAO pass. Drawn that late, the pass turns the
depth test on itself, as the passes before it may leave it off. The occlusion that pass reads
from depth would otherwise shade a translucent surface as the surface behind it, as it would the
centre of mass marker. FXAA still comes after, and so do the sequential view markers drawn by
`GCodeViewer::render_scene()`, which hide the translucent toolpaths behind them.

A translucent segment takes its diffuse light in its own color, scaled by its opacity. In
realistic view, whatever its options, every segment, opaque or translucent, takes instead of the
tinted highlight of the default shading an untinted one computed per fragment, not scaled by the
opacity, so that the filaments look alike and a clear one keeps its gloss instead of fading into
the background. `GLCanvas3D::_render_gcode()` switches it with `Viewer::set_gloss()`. Without
realistic view a translucent filament is matte and an opaque one keeps the default shading. The
highlight takes the normal of the rounded extrusion a segment stands for, an ellipse of its height
and width around its centre line, rather than that of its facets: it then runs smoothly across a
joint, where the facets of two segments would catch it apart, and it fades out towards the tip of a
path end, whose normal runs along the extrusion. There
is no Fresnel term: an extrusion seen end-on would turn into a bright opaque stripe.

Each segment reaches half a line width past its ends, to fill the outside of a turn or round off a
path end, so two segments of a path overlap at their joint. Depth hides that between opaque ones,
but blending would draw the joint twice, and the highlight of realistic view shows the caps sticking
out of an opaque one. A translucent segment, and in realistic view any segment, is therefore cut in
the fragment shader at the bisector plane of each joint of its path, keeping 0.02 mm past it: the
faces each segment turns to the camera do not quite meet without their caps. A vertex where two
segments of a path meet carries 0.5 on top of its layer in the w of its position, which the shaders
otherwise read as an int. That includes the joints across the vertex repeated in place that starts
a new path wherever the flow, acceleration or jerk changes (`starts_path()` in
`LibVGCodeWrapper.cpp`), which real G-code has at most of its joints: the shader then takes the
neighbour one vertex further, and the caps the angle of the joint, which the vertex data leaves at 0
there.

The translucent segments are not sorted by depth. The opaque pass draws the last printed segments
first when looking down, to cut overdraw. The translucent pass draws them the other way round, so the
farthest layers blend first. Surfaces of one color blend alike in any order, so a single translucent
color stays in this direct pass.

## More than one translucent color

Between two translucent colors the blending order shows, so on desktop OpenGL the translucent
segments then blend order independently, in `ViewerImpl::render_translucent_segments()`. The nearest
translucent surface of each pixel is drawn over a weighted blended order independent transparency
(McGuire and Bavoil, 2013) of the others. The mean alone would not do: the walls of a painted object
lie a fraction of a millimetre apart, so its depth weights barely differ, and the more opaque and more
numerous surfaces of one filament would hide the other filament's outer wall in front of them.

1. The depth of the scene drawn so far is copied into a texture. The fragment shader discards a
   translucent fragment behind it, as there is no depth buffer to test against.
2. The translucent segments are drawn once into an `R32F` target cleared to 1, keeping with `GL_MIN`
   the `gl_FragCoord.z` of the nearest. The same program draws the next pass, so the nearest
   fragment finds its own depth there exactly.
3. They are drawn again. The nearest fragment of a pixel goes, premultiplied, to an `RGBA16F` target
   of its own. Every other one adds its premultiplied color, times a weight, to an `RGBA16F` target,
   and `-log(1 - alpha)` to an `R16F` target, with `GL_ONE, GL_ONE`. The weight is the opacity times
   `max((1 - z)^3, 0.01)`, z being the depth across the extrusions' bounding box, so nearer surfaces
   count more. Its ceiling keeps thousands of stacked surfaces within half float range, and its floor
   keeps far ones out of the subnormals. A translucent segment's opacity stays below 1, else the
   shader would take it for an opaque one and add it unweighted.
4. When the canvas is multisampled, the color targets are too, then resolved, so that translucent
   lines keep their antialiasing.
5. A full screen pass draws the nearest surface over the weighted mean color of the others, with
   their coverage `1 - exp(-sum)`, over the scene.

Swapping the drawing order changes the result by at most 2 levels out of 255, against up to 246 in
the direct pass. The targets take about 26 bytes per pixel, and 18 more per sample with
multisampling, which is why a single translucent color keeps the direct pass. OpenGL ES, whose float
targets are optional, always takes the direct pass, as does a multisampled framebuffer object, whose
depth cannot be copied into a texture.

## Cost and side effects

Without depth writes, every translucent surface on screen is shaded, so the cost of the second
pass grows with the number of translucent surfaces stacked on screen. Order independent blending
draws the translucent segments twice and writes 18 bytes of float data per sample instead of 4, about
three times the cost of the direct pass.

In realistic view, a translucent segment casts into a share of the shadow map texels as large as
its opacity, in an ordered 4x4 pattern that the 5x5 filter of the receivers reads as a lighter
shadow. The pattern shifts with the segment, so layers stacked along the light add up: a 1 mm
translucent box at 35% casts a shadow about 60% as dark as an opaque one, a 10 mm box nearly a full
one.

## Prepare

Prepare draws a part as a closed mesh, which would show a single surface at the filament's opacity,
far fainter and darker than the print of that part in Preview. The object shaders therefore shade a
visible model part whose filament color is translucent as the view ray crossing two shells of the
part, the front and the back one, of two 0.45 mm walls each, with the opacity rule of Preview:
`1 - (1 - a)^(1.8 / 0.4 / cos)`, cos being that of the angle between the view and the normal,
floored at 0.25. A filament at 35% is then about 86% opaque face on, and nearly opaque at the
silhouette, where the ray grazes the walls. See `translucent_filament_color()` in `gouraud.fs` and
`phong.fs`.

Such a part is drawn in premultiplied color, blended with `GL_ONE, GL_ONE_MINUS_SRC_ALPHA`, so that
in realistic view its highlight is not dimmed by its opacity; without realistic view it takes no
highlight. `GLVolumeCollection::render()` switches this per volume, and only with a shader that has
the `translucent_filament` uniform, the GLSL 1.40 ones: with the GLSL 1.10 and ES shaders, such a
part blends as a plain transparent volume. Modifiers, negative volumes and opaque parts are drawn as
they were.

The wireframe overlay draws translucent filament parts too, with blending off, so their lines stay
solid.
