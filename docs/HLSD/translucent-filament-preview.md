# Translucent filaments in Preview

## Purpose

A filament color can carry an opacity (`#RRGGBBAA`, see [color-picker.md](color-picker.md)). Prepare
draws the objects of such a filament in its transparent pass. Preview does the same for their
toolpaths, so the inside of a translucent part and whatever lies behind it show through.

## Where the opacity applies

- Only the views that color toolpaths by filament: Filament, Summary and Tool. The other views
  color by feature or by a value, and stay opaque.
- Only extrusions. Travels, wipes and option markers stay opaque, and so do the layers that hold a
  pause or a custom G-code, which these views draw grey.
- A filament with less than 10% opacity is drawn as white at 30%, as `adjust_color_for_rendering()`
  does in Prepare. Otherwise a fully clear filament would not be drawn at all.
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

A translucent segment takes its diffuse light in its own color, scaled by its opacity, and a
highlight computed per fragment, untinted and not scaled, so a clear filament keeps its gloss
instead of fading into the background. There is no Fresnel term: an extrusion seen end-on would
turn into a bright opaque stripe.

The translucent segments are not sorted by depth. The opaque pass draws the last printed segments
first when looking down, to cut overdraw. The translucent pass draws them the other way round, so the
farthest layers blend first. Surfaces of one color blend alike in any order, so a single translucent
color stays in this direct pass.

## More than one translucent color

Between two translucent colors the blending order shows, so on desktop OpenGL the translucent
segments then blend order independently, with weighted blended order independent transparency
(McGuire and Bavoil, 2013), in `ViewerImpl::render_translucent()`.

1. The depth of the scene drawn so far is copied into a texture. The fragment shader discards a
   translucent fragment behind it, as there is no depth buffer to test against.
2. Each translucent fragment adds its premultiplied color, times a weight, to an `RGBA16F` target,
   and `-log(1 - alpha)` to an `R16F` target, with `GL_ONE, GL_ONE`. The weight is the opacity
   times `max((1 - z)^3, 0.01)`, z being the depth across the extrusions' bounding box, so nearer
   surfaces count more. Its ceiling keeps thousands of stacked surfaces within half float range,
   and its floor keeps far ones out of the subnormals.
3. When the canvas is multisampled, the targets are too, then resolved, so that translucent lines
   keep their antialiasing.
4. A full screen pass draws the weighted mean color over the scene, with the coverage
   `1 - exp(-sum)`.

Swapping the drawing order changes the result by at most 2 levels out of 255, against up to 246 in
the direct pass. The targets take about 50 bytes per pixel with 4x multisampling, which is why a
single translucent color keeps the direct pass. OpenGL ES, whose float targets are optional, always
takes the direct pass, as does a multisampled framebuffer object, whose depth cannot be copied into
a texture.

## Cost and side effects

Without depth writes, every translucent surface on screen is shaded, so the cost of the second
pass grows with the number of translucent surfaces stacked on screen. Order independent blending
writes 10 bytes of float data per sample instead of 4, and roughly doubles that pass again.
In realistic view, a translucent segment casts into a share of the shadow map texels as large as
its opacity, in an ordered 4x4 pattern that the 5x5 filter of the receivers reads as a lighter
shadow. The pattern shifts with the segment, so layers stacked along the light add up: a 1 mm
translucent box at 35% casts a shadow about 60% as dark as an opaque one, a 10 mm box nearly a full
one.
The SSAO pass reads depth, so a translucent surface takes the occlusion of what lies behind it.
