# Tooltip illustrations

The texture-displacement panel's tooltips can carry a picture under their text: a still image, or a
short animation that loops while the tooltip is open. Several settings in that panel describe a shape
rather than a number - what Rotation turns, what Tile repeats, what Invert turns inside out - and a
sentence about a shape is read twice and understood once.

## Where a tooltip is drawn

`GLGizmoTextureDisplacement::panel_tooltip()` replaces `ImGuiWrapper::tooltip()` throughout the panel.
It differs in two ways.

It places the tooltip beside the panel instead of under the mouse. The panel is pinned to the right
edge of the canvas, so a tooltip following the mouse covers the control it is explaining, and one
carrying a picture covers most of the panel with it. The tooltip goes to the left of
`m_tooltip_anchor_min`/`_max` - the panel's own screen rectangle, or the texture picker's while that
popup is up and the tooltip comes from inside it - and to the right only when there is no room on the
left. This is the placement the picker popup itself already uses.

ImGui normally keeps a tooltip glued to the mouse and ignores `SetNextWindowPos()`. Orca's imgui
branch honours a position set for a tooltip (see the `ImGuiWindowFlags_Tooltip` branch of `Begin()` in
`deps_src/imgui/imgui.cpp`) and still clamps the result inside the canvas, which is what keeps a tall
tooltip near the bottom edge on screen.

It also gives the tooltip a fixed width rather than one derived from its text, because a picture has
to be laid out against something, and a column of tooltips that are all one width is steadier to read.

## Where the pictures live

`resources/images/texture_displacement/`, as PNG, JPG, BMP or GIF. A tooltip names them by file:

```cpp
hover_tip(_u8L("Repeats the texture across the painted area. ..."),
          { { "tile_off.png", _L("Off") }, { "tile_on.png", _L("On") } });
```

Several in one tooltip are laid out side by side at equal widths, which is how a setting is shown as a
comparison - the same model at two values of it. The caption under each is translated; the file name
is not.

A file that is missing or will not decode is skipped, and a tooltip whose pictures are all missing is
exactly the text tooltip it was before. So the art for a tooltip can land after the tooltip does, and
a half-finished set never breaks the panel.

## Decoding and playback

`slic3r/GUI/AnimatedTexture.hpp` decodes a file once and uploads one GL texture per frame. Stills go
through `wxImage`; GIFs go through `wxGIFDecoder`, whose raw palette indices are composited here frame
by frame, replaying the disposal method each frame asks for. That replay is what makes an animation
look continuous: a GIF frame is a sub-rectangle drawn over what came before it, so an animation that
only re-sends the pixels that changed draws as a flicker of disconnected patches without it.

A frame delay of 0 or 1 hundredths of a second is read as 100 ms, the convention browsers settle on,
so an animation authored against that plays at the speed it was authored at.

The animation restarts when its tooltip opens, rather than running off a free-running clock: these are
short loops of one setting going from one value to another, and joining one halfway through shows the
end of a change the user never saw the start of. The canvas only redraws on demand, so each frame asks
for the next through `GLCanvas3D::schedule_extra_frame()`, timed to when that frame is actually due.

## Making one

Captures of the real tool are the first choice for anything involving the model, the mesh or colour.
Settings whose meaning is a shape can be drawn from the same arithmetic instead:
`scripts/gen_texture_displacement_tips.py` shades a height field as a relief catching the light, and
produces `tile_off.png`, `tile_on.png` and `rotation.gif` that way.

Either way:

- Square, around 256 px. The tooltip is about 20 em wide, so that is roughly native size on a 100%
  display and downscales cleanly above it.
- Neutral grey. These say what a shape does; colour in one reads as a third thing the setting does,
  and has to work against both themes.
- Animations loop seamlessly and stay short - a dozen frames at 100 ms, under about 250 KB. They sit
  in `resources/`, which ships with every copy of the application.
- No text in the picture. Captions are translated; pixels are not.
