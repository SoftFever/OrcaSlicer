# ChromaStack - High Level Design

## Purpose and scope

ChromaStack prints a colour picture on a single face of a model: a few filaments are printed one on
top of another, the printer swaps filament at fixed layer boundaries, and the *height* of every
pixel decides how many of those layers reach it. Filament is translucent, so a pixel that stops
part-way through a run shows a blend of that run's filament and whatever is under it, which is
where the colours come from - far more of them than there are filaments loaded.

This is that model inside OrcaSlicer, as a mode of a texture displacement layer. The picture and
the filament stack are the input; the output is a grayscale height map and a set of filament swaps.
Nothing about it is a new pipeline: the height map is handed to the layer the user painted, so the
existing projection, preview and bake turn it into real geometry, and the swaps are handed to the
object's height ranges, which is the slicer's own way of printing a Z band in another filament.

It does not import other filament-painting tools' project files, and it does not choose the filaments or
their order - that is the user's artistic decision, and the panel exists to make its result
visible before the print.

## The colour model

`libslic3r/ChromaStack.hpp` holds the whole of it, free of any GUI dependency, which is what lets the
solve run against plain image bytes and be unit tested.

A **stack** (`ChromaStack`) is a list of **bands** (`ChromaBand`), bottom-up: a filament and
how many layers it gets. Alongside them it carries the print's layer height, which the colours
depend on directly, and the **base**: how many layers of solid backing the picture sits on.

The base is not a band. It is a measurement of the model - everything below the painted face, often
a hundred layers or more on a tall part - while a band is a handful of layers the user chooses. It
prints in the bottom band's filament, because that is what is loaded when the print starts, so it
extends that band's height range down to the first layer instead of being a swap of its own. Only
the bands are quantized into the height map, which is why the 255-layer limit
(`CHROMA_MAX_RELIEF_LAYERS`) bounds the picture and not the stack.

Each filament carries its **transmission distance** (TD), the usual measure for how deep
light gets into it: about 1 for an opaque filament, 4 to 7 for a usefully translucent one, 8 and
above for a nearly clear one. TD lives in the filament preset, as
`filament_transmission_distance` - it is a property of the material, so it belongs with the
material and ships with a profile rather than being retyped per project. It has no effect on
slicing.

`chroma_palette()` turns a stack into the colour it shows at every possible column height, which
is all the solver ever needs. The model is Beer-Lambert absorption, layer by layer: a layer of
thickness `h` in a filament of transmission distance `TD` passes

    T = exp(-h / (CHROMA_TD_OPAQUE_FRACTION * TD / 3))

of the light from under it and shows its own colour with the rest, so walking up from an opaque
base,

    C_k = (1 - T_k) * c_k + T_k * C_(k-1),  C_0 = c_1

The constants say what TD means in physical terms: a filament stops showing what is behind it at
about a tenth of its TD number (`CHROMA_TD_OPAQUE_FRACTION`), and three e-folding lengths of
absorption pass 5% of the light, which is where the eye stops seeing through. Those are calibrated
against how the community's TD step prints behave, not derived from first principles - TD is an
empirical scale.

How fast that happens is the single most useful fact about a stack, and `chroma_layers_to_opaque()`
is it: a filament hides what is under it at about a tenth of its TD, so how many *layers* that takes
depends entirely on how thick they are. TD 4 covers in two layers at 0.2 mm and in five at 0.08 mm.
Below about two layers a band has no intermediate colours at all - the picture can only use the
filament or not use it - which is why an otherwise sound stack prints as flat steps at a coarse
layer height. The panel reports the number under the palette ramp, and warns when it falls below
two, because nothing else makes it visible: the filaments look fine, the bands look long enough, and
the result looks like the colour model is wrong when it is the layer height.

How many of a picture's layers actually earn their place is a second question, and
`chroma_distinct_levels()` answers it: a band eventually hides what is under it, and another layer of
it then shows the colour the one below already showed. It is measured against the palette rather than
against TD, because how long a band keeps paying depends on what is under it as much as on the
filament - a light band over a dark one is finished in four or five layers, while a dark one over a
light one goes on moving the colour perceptibly well past the depth at which it is nominally opaque,
since the last few percent of light coming through still shifts a near-black colour. `Trim` shortens
each band to its last layer that still changes anything.

Beer-Lambert is also the only model that stays self-consistent applied per layer: transmittance
multiplies, so a run of n layers of one filament passes `exp(-n h / d)` whether it is composited in
one step or n, and the palette therefore does not depend on how the stack happens to be cut into
bands. A unit test asserts exactly that. Compositing is done on linear light, because the quantity
being attenuated is light rather than an sRGB code.

The base is assumed opaque. That is what makes a column's colour depend only on the layers above it,
and it is why the panel warns when the base is thin.

## The solve

`chroma_solve()` maps each pixel of the picture to the column height whose palette colour is
closest to it, perceptually: both sides go to CIELAB and the match is a CIE76 distance. A 1-D
palette of a few dozen entries makes the extra accuracy of CIEDE2000 hard to see and the cost of it
easy to feel - several hundred million comparisons for a large picture - so the cheaper metric is
the deliberate choice here, unlike the texture colour quantizer, which matches against a
two-dimensional palette of filament mixes and uses a precomputed CIEDE2000 cube.

Optional Floyd-Steinberg error diffusion, serpentine, trades the bands a photograph shows across a
sky or a cheek for fine stipple. Off by default, since stipple is the artifact rather than the cure
for flat-coloured artwork.

Three things come back: the per-pixel layer count, the same thing as an 8-bit grayscale height map
(0 at the base, 255 at the top of the stack), and the palette colours chosen, as an RGB image. The
quantization belongs to the solve rather than to the displacement because the relief's steps have
to land on layer boundaries, and a grey level is the only channel the displacement reads.

The solve also reports the mean and worst colour difference it had to accept. That is the only
honest answer to "will this stack reproduce my picture": under about 5 is a good match, over about
15 means the filaments cannot reach the picture's colours and no number of layers will help.

## How it attaches to a texture displacement layer

A ChromaStack layer is an ordinary `TextureDisplacementLayer` whose height map is computed rather than
imported:

- `chroma_source` keeps the picture the user chose, so the solve can be redone whenever the stack,
  the layer height or the picture changes;
- `chroma` is the stack;
- `image_data` - the same field every other layer uses - holds whatever the last solve produced.

So everything downstream needs no changes at all and knows nothing about colour stacks. It also
means the solve writes `depth_mm` (the relief is exactly as tall as the layers above the base) and
pins `invert`, `midlevel` and `smoothing`: blurring a solved height map moves its steps off the
layer boundaries they were solved for.

Colour by filament swap and the layer's own `color_enabled` (one filament per triangle, via
`mmu_segmentation_facets`) are mutually exclusive, and the panel enforces it. They are two
different ways of colouring the same surface, and painting the relief per triangle would hide the
very stack the heights were solved against.

## The filament swaps

`chroma_ranges()` turns the bands into Z bands in the object's own coordinates, merging any two
adjacent bands that share a filament, since nothing is swapped between them. The panel writes them
onto the object as `layer_config_ranges`, each range carrying `extruder` - which
`apply_to_print_region_config()` spreads over every feature filament the range does not otherwise
override, i.e. the whole band prints in that filament - and `layer_height`, both because the solve
is only valid at the height it was run at and because `layer_height_profile_from_ranges()` reads
that option without checking it is there.

Height ranges rather than colour-change custom G-code: they are object-relative, so they survive
the object being moved, they are saved in the project file, and they appear in the object list where
the user can see and edit them. The cost is that an object can only have one set of them, so
applying a stack replaces whatever ranges the object had, with a confirmation when it had any.

`ObjectList::rebuild_layer_ranges()` mirrors them in the object tree without touching the
selection - changing the selection while a gizmo is open tears the gizmo down under the user.

## Geometry the model assumes

A filament swap happens at one Z across the whole plate, and the relief grows along the painted
surface's normal. The two agree only where that surface is horizontal, so:

- the painted patch's height above the bottom of the object is what the base layer count has to
  match, and the panel measures it from the patch and offers the number rather than leaving the user
  to work it out;
- a patch that is not flat in Z gets a warning, because its colours will only be right where it is
  level. The relief itself is still computed, since a gently domed plaque is a legitimate thing to
  print and only the user can judge how much error is acceptable.

The model itself is the user's: the stack needs a slab of the base thickness with a face to paint,
and nothing here creates or thickens geometry.

## Seeing the colours

The colours are blends of translucent filaments. No single filament makes them, so they cannot be
expressed as the per-triangle filament assignment the 3D scene paints with, and there is nothing for
the ordinary view to draw. They exist in two places: the sliced preview, which shows the filament
bands as they print, and an overlay the gizmo draws over whatever surface is on screen.

That overlay is a shader, `resources/shaders/*/chromastack.*`, and it reads the colour from each
fragment's own **print Z** rather than from the height map or from anything in the mesh. A filament
swap happens at one Z across the whole plate, so the layer a point sits in is what decides its
colour whatever the picture says. Two things follow. A band edge lands exactly on the layer boundary
however coarse the triangles are - a per-triangle colouring puts it wherever the mesh happens to
have an edge. And it keeps working after a bake, when the relief is real geometry, the paint has
been cleared and the height map is attached to nothing on screen; the overlay is built from the live
preview's displaced mesh before a bake and from the volume's own mesh after one.

The stack's colours reach the shader as a texture of one texel per printed layer, because a base can
be several hundred layers and no uniform array holds that. The mesh carries nothing about the stack,
so editing the stack costs a palette upload rather than a mesh rebuild.

On top of the colour the shader tries to look like printed plastic, because a flat shaded colour
reads as a rendering and the whole point of the overlay is judging a print:

- the terminator is **wrapped** and the grazing angles **glow**, both scaled by the transmission
  distance of the filament on top - filament translucent enough to be worth stacking scatters light
  rather than reflecting it, and an opaque stack correctly gets neither;
- the walls carry **layer lines**, a groove at every layer boundary that also modulates the
  specular, so a wall glints in stripes as a printed one does. Terraces get none: a terrace is one
  layer's top surface and has no lines to show.

Both effects are driven by the fragment's height, so they are free of any texture or UV, and both
fade out through `fwidth` once a pixel covers more than about half a layer - lines finer than a
pixel are noise, and a colour read from one layer out of the dozen under a pixel flickers as the
camera moves. The palette lookup slides off the texel centres over the same range so the linear
filter averages the bands a pixel covers instead of picking one of them.

The overlay is drawn pulled forward and without depth writes, the same pass the painted-colour
overlay uses, since it is the same surface as whatever is underneath it.

## What the panel shows

The kind of a layer is the layer's own, chosen when it is added. The chip in the card's header is
also the control that changes it: clicking it offers the two kinds, and converting keeps everything -
a picture's stack survives a trip through being a relief layer, and a relief layer's texture becomes
the picture it is solved from, a height map being a picture with no colour in it. Converting away
from a picture clears the swaps it wrote, and only those.
There is no panel-wide mode, so one object can hold a picture on its face and a knurl on its rim, and
a card never shows a control that cannot do anything on it. A picture cannot be reordered (anything
printed over it buries the colours) and an object may hold only one, refused when the layer is added
rather than when its swaps would overwrite the first one's.

The card carries the four things a user decides and nothing else: which picture, which filaments in
what order and how deep, whether the result is good enough, and - as a single row of `2 3 4 6` -
how many colours to spend. Everything else is either derived or rare, and either shows as a state
chip or sits behind More settings.

### The solve and the swaps are not buttons

A height map that has to be asked for can be out of date, and a set of swaps that has to be applied
can be missing - and both of those states then need a warning to explain themselves. `chroma_settle()`
removes the states rather than the warnings: every edit marks the stack, and once it has held still
for a moment the layer re-solves and writes its swaps, the way the preview already rebuilds itself.

The swaps are the object's height ranges, not the layer's, so writing them automatically needs a
rule about whose they are. The stack remembers a fingerprint of what it last wrote
(`ChromaStack::applied_ranges`); the automatic path writes only when the ranges are empty or still
match that fingerprint, and otherwise leaves them exactly as they are and says so with a chip, with
a button that asks before replacing them. The panel never takes over something it was not given.

### Saving one

The .3mf stores a texture displacement layer as JSON with its image beside it, and a ChromaStack
layer has two images rather than one: the picture it was solved from and the height map that came
out. Both go in, and the stack itself - the bands, their filaments and transmission distances, the
base, the dither, and the fingerprint of the swaps it wrote - goes in the JSON.

The height map alone would not do. A project that kept only that reopens as a relief layer whose
texture happens to be a staircase: it still prints, but it can never be solved again, and nothing on
screen says why. A stack that comes back with no bands in it is treated as a relief layer on purpose,
since that is what it is.

### Measuring a filament

Transmission distance cannot be read off a spool and no filament preset ships one, so without a
measurement every stack starts from a default of 4.0 and every colour the panel predicts rests on a
guess. `chroma_wedge_mesh()` is the measurement: a staircase of the filament under test, one layer
taller at each step, printed over a base of a filament it has to hide. The step at which the base
stops showing through is the depth at which it covers, and `chroma_td_from_step()` turns that depth
into a TD - the same relationship `chroma_layers_to_opaque()` runs the other way.

`Plater::calib_transmission_distance()` builds the plate: the wedge, the print forced to the layer
height its steps are counted in (first layer included, or every step would be a different thickness
from the one it is read as), and two height ranges putting the base in one filament and the staircase
in the other. It is the only calibration that generates its model rather than loading a shipped one,
because the steps are layers and the layer height is the user's.

It is reached from the Calibration menu, which picks the darkest other loaded filament as the base
and confirms before replacing the plate, and from a ruler beside each band's TD in the card, which
measures that band's filament and offers to put the result straight into the stack.

### Choosing the filaments

`chroma_auto_stack()` answers the question the panel could previously only report on: which of the
loaded filaments reach this picture, and in what order. The order matters as much as the choice,
since a stack is filaments seen through each other, so it grows the stack one filament at a time and
tries every remaining filament at every position, keeping whichever placement loses least to the
picture. That is a few dozen candidates rather than the tens of thousands an exhaustive search over
subsets and orderings would be.

`chroma_suggest_colors()` answers the other half of that question: not which of the loaded filaments
to use, but which colours the picture would most like to be printed in, owned or not. Picking from a
drawer has a ceiling - a picture whose colours are not in it cannot be printed well out of it however
it is arranged - so this lets the colours themselves move: a weighted k-means over the picture for a
starting set, then a hill climb that walks each band's colour wherever the stack's own error falls
fastest. Each suggestion carries the transmission distance its band wants and the nearest thing the
user already has, since a colour is only half of what to go looking for.

Candidates are scored against `chroma_histogram()` - the picture's colours bucketed four bits per
channel and weighted by how much of the picture they cover - rather than against the picture, which
is what makes trying dozens of stacks affordable. A photograph is millions of pixels and a few
hundred distinguishable colours.

### The states

The card contains no messages. Two words of label, `Colors` and `Base`, name controls; everything
else the panel has to say is said by the thing it is about, and the sentence that explains it is in
that thing's tooltip.

- **How good the match is** is the colour of the prediction's own frame: amber when the filaments
  cannot reach the picture, green when they do, the separator grey when it is merely close, and the
  accent colour while the stack is settling. It is a property of that picture, so it is drawn on it.
- **Everything else wrong** is one mark in the card's header, and only when something is. Its tooltip
  is `chroma_problems()`, the whole list in sentences; clicking it does the one thing that can be
  done from there, which is replacing height ranges that are in the way.
- **Whether the base follows the model** is a padlock beside the field, closed while it does.

The reasoning is that a warning printed on a panel is read once and then occupies that space for
ever, and a panel that explains itself in paragraphs cannot be scanned at all. A mark that appears
only when it has something to say costs one glyph when there is nothing wrong, and carries as much
prose as the problem needs when there is.

Base, dither and the picture's placement are behind More settings. The base is a measurement, not a
setting: it follows the painted face unless the padlock is opened. The printed-colour overlay is a
view, so its switch is in the View row with the other views rather than inside a layer's card.
