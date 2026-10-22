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
- The legend swatches carry the same opacity.

## Data

`libvgcode::convert_filament_colors()` decodes the filament color strings into a
`libvgcode::PaletteWithAlpha`. libvgcode keeps one RGBA8 color per vertex, in a `GL_RGBA8` texture
buffer, or in RGBA8 2D textures on OpenGL ES. The color depends on the view type, while the alpha
comes from the filament alone. The viewer also notes whether any vertex is translucent.

## Rendering

Both passes draw the same list of enabled segments. Each segment instance reads the alpha of its
first vertex, and the vertex shader moves the segment outside the clip volume when it does not
belong to the current pass. Without a translucent vertex, the second pass is skipped and the
first draws exactly what it drew before.

1. The opaque segments, with depth writes.
2. The option markers.
3. The translucent segments, blended with `GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`, depth tested
   but without depth writes. Opaque toolpaths and objects hide the translucent ones behind them,
   and a translucent segment hides nothing. Drawing the markers first keeps those inside a
   translucent part behind its surface, instead of on top of it.

The translucent segments are not sorted. The opaque pass draws the last printed segments first
when looking down, to cut overdraw. The translucent pass draws them the other way round, so the
farthest layers blend first. Overlapping surfaces of one filament share a color, so the order
inside a layer barely shows; between two translucent filaments, it is approximate.

The second pass runs the vertex shader over every enabled segment again, and without depth writes
it shades every translucent surface on screen, so drawing the toolpaths takes about twice as long.
That is paid only while a translucent filament is shown.
The shadow casters draw depth only, so translucent toolpaths cast shadows as opaque ones do.
The SSAO pass reads depth, so a translucent surface takes the occlusion of what lies behind it.
