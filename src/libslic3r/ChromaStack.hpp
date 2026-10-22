#ifndef slic3r_ChromaStack_hpp_
#define slic3r_ChromaStack_hpp_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <cereal/cereal.hpp>
#include <cereal/types/vector.hpp>

#include "Color.hpp"
#include "TriangleMesh.hpp"

namespace Slic3r {

// ChromaStack-style colour printing: a picture is reproduced on a single face by stacking thin layers
// of a few filaments, swapping filament at fixed layer boundaries, and varying the *height* of each
// pixel's column. Where the column is low, the layers above it are missing and the filament below
// shows; where it is high, the topmost filament covers everything beneath it. Because filament is
// translucent, a column that stops part-way through a run shows a blend of that run's filament and
// what is under it, which is what gives far more colours than there are filaments loaded.
//
// So there are only two things to compute, and they are both here:
//  - the *palette*: the colour the stack shows at every possible column height (chroma_palette());
//  - the *height map*: per pixel, the column height whose palette colour is closest to the picture
//    (chroma_solve()).
//
// Everything else is machinery that already exists. The height map is handed to a texture
// displacement layer as its height texture, so the existing projection/preview/bake pipeline turns
// it into real geometry (see TextureDisplacement.hpp), and the filament swaps are handed to the
// object's height ranges (ModelObject::layer_config_ranges), which is the slicer's own way of
// saying "print this Z band with that filament".

// The depth of filament that blocks essentially all the light behind it, as a fraction of the
// filament's transmission distance (TD).
//
// TD's scale is empirical, not physical: its values run from about 1
// for an opaque filament to 10 and above for a nearly clear one, and the community's calibration
// prints (the "seashell" step test) read a TD off the step where the colour beneath stops showing.
// That step is about a tenth of the TD number, which is the only thing this constant encodes - a
// filament of TD 2 stops hiding what is under it at roughly 0.2 mm, i.e. after two or three layers,
// while one of TD 8 needs most of a millimetre. The model below turns that into a per-layer
// transmittance; see chroma_palette().
static constexpr float CHROMA_TD_OPAQUE_FRACTION = 0.1f;

// What a fresh stack gets above the base. Twenty-four layers is about two millimetres at a fine layer
// height: enough steps for a continuous tonal ramp, and still a print that finishes in an evening.
static constexpr int CHROMA_DEFAULT_RELIEF_LAYERS = 24;

// The most picture layers the solve can express. A column's height above the base is stored as one
// byte per pixel in the 8-bit grayscale height map the displacement reads, so beyond this two
// different heights would have to share a grey level and the relief's steps would stop landing on
// layer boundaries - which is the one thing the quantization exists to guarantee.
//
// It bounds the bands, not the stack: the base is whatever the model already is under the painted
// face, often hundreds of layers on a tall part, and nothing about it is quantized into the height
// map. As a limit on the picture it is not one in practice - 255 layers is 20 mm of relief at
// 0.08 mm, far past where a picture gains anything.
static constexpr int CHROMA_MAX_RELIEF_LAYERS = 255;

// How far from level the painted surface may tilt before the picture stops being printable there.
//
// A swap happens at one print Z across the whole plate, so what a point shows depends on how high it
// is, not on how far the relief stands off the surface it sits on. On a tilted face a relief of d mm
// along the normal only raises the print by d*cos(tilt), so the relief has to be deepened by
// 1/cos(tilt) to put its steps at the heights the colours were solved for - which runs away as the
// surface approaches vertical, and at 60 degrees has already doubled. Past here the correction is
// clamped and those texels print the wrong colour, which chroma_problems() reports.
static constexpr float CHROMA_MAX_TILT_DEG = 60.f;

// The most filament changes one stack may hold. Every band is a swap the printer has to make, so this is
// a limit on patience rather than on the model; the palette itself is bounded by the layer count.
static constexpr size_t CHROMA_MAX_BANDS = 16;

// One filament as the colour model sees it: what colour it is, and how far light gets into it.
struct ChromaFilament
{
    // 0-based index into the project's filaments, which is what a height range's `extruder` option
    // (1-based) is written from. -1 for a filament that is not loaded, which the GUI refuses to
    // solve with rather than silently printing in the wrong colour.
    int      filament = -1;
    ColorRGB color;
    // The filament preset's own name, so the panel can say which filament this is. Six similar greys
    // are six identical swatches and six identical numbers otherwise. Stored rather than looked up on
    // demand because a stack outlives the project's filament list - a preset can be swapped out from
    // under a saved stack, and then the name is the only record of what it was solved against.
    std::string name;
    // Transmission distance, mm, as the filament preset reports it
    // (`filament_transmission_distance`). Clamped to a sane range when the palette is built: zero
    // would make a layer infinitely opaque and a negative value is meaningless.
    float    td_mm = 4.f;

    template<class Archive> void serialize(Archive &ar)
    {
        float r = color.r(), g = color.g(), b = color.b();
        ar(filament, r, g, b, td_mm, name);
        color = ColorRGB(r, g, b);
    }
};

// A run of consecutive layers printed in one filament. The stack is a list of these, bottom-up, so
// a band boundary is exactly where the print pauses and the filament is changed.
struct ChromaBand
{
    ChromaFilament filament;
    int              layers = 1;

    template<class Archive> void serialize(Archive &ar) { ar(filament, layers); }
};

// The whole recipe: which filaments, in which order, how thick a layer is, and how much of the
// bottom is solid backing rather than picture.
// How big a plate a picture gets when it makes its own, measured on its longer side. A hundred
// millimetres is a postcard: large enough that the layers are a picture rather than a swatch, small
// enough to fit any printer that exists and to print in an evening.
static constexpr float CHROMA_PLATE_DEFAULT_MM = 100.f;

// A plate made for a picture that has no model of its own.
//
// Most of what ChromaStack is good at - a photograph, a poster, a piece of line art - has no model
// behind it and does not want one. The slab it has to be printed on is a function of the picture and
// a few numbers, so the slab is made here rather than asked for: the picture's own proportions, a
// solid backing to print the colours against, and a frame around it.
//
// The frame is a sweep, not a set of shapes. Its outline says what the plate's edge is, its profile
// says what the band between that edge and the picture does in cross-section, and every combination
// of the two is a frame - which is what lets "thicker", "rounded" and "stepped" be numbers on a card
// instead of a gallery of models to choose between.
struct ChromaFrame
{
    // The plate's outline. The picture is always a rectangle inside it; this is what is around it.
    enum class Shape : int
    {
        Rectangle = 0, // the picture's own rectangle, grown by the frame's width
        Rounded   = 1, // the same, with the corners turned by `corner_mm`
        Ellipse   = 2, // an oval through the corners of that rectangle
        Outline   = 3, // `outline` below: whatever shape was handed in, usually from an SVG
    };

    // What the band between the outline and the picture does on its way in. Each is a polyline in
    // (how far in, how high), swept around the outline - see chroma_frame_profile().
    enum class Profile : int
    {
        Flat   = 0, // level with the picture's base: a border of one colour and nothing more
        Bevel  = 1, // a straight ramp from the outer edge up to the lip
        Step   = 2, // one square step, the shape of a mount board
        Round  = 3, // a quarter round, the shape of a moulding
        Ogee   = 4, // round out then round in, the shape a picture frame usually is
    };

    Shape   shape     = Shape::Rectangle;
    Profile profile   = Profile::Flat;
    // How wide the band is, measured in from the plate's edge. 0 means no frame at all: the picture
    // runs to the edge of the plate.
    float   width_mm  = 6.f;
    // How far the frame stands above the picture's base. 0 with any profile is the same as Flat.
    float   rise_mm   = 2.f;
    // The corner radius for Rounded, and ignored by every other shape.
    float   corner_mm = 4.f;
    // The outline for Shape::Outline: a closed loop in millimetres, centred on the picture. Held as
    // the loop rather than as a path to the SVG it was read from, so a plate can be rebuilt from a
    // saved project whether or not that file is still on the machine.
    std::vector<Vec2f> outline;

    bool operator==(const ChromaFrame &o) const
    {
        return shape == o.shape && profile == o.profile && width_mm == o.width_mm && rise_mm == o.rise_mm &&
               corner_mm == o.corner_mm && outline == o.outline;
    }
    bool operator!=(const ChromaFrame &o) const { return !(*this == o); }

    template<class Archive> void serialize(Archive &ar)
    {
        ar(shape, profile, width_mm, rise_mm, corner_mm, outline);
    }
};

// The plate a picture made for itself: the picture's own area, what is under it, and what is around
// it. Everything else about the print - which filaments, how many layers, what colour each height
// shows - is the stack this hangs off.
struct ChromaPlate
{
    // Whether this picture made its own plate. False for a picture painted onto a model, which is
    // the other half of what ChromaStack does and is not touched by any of this.
    bool  generated = false;

    // The picture's area on the plate. The longer side is what the user sets; the other follows the
    // picture's proportions, because a photograph squeezed to fit a square is not the picture.
    float width_mm  = 100.f;
    float height_mm = 100.f;
    // The solid backing under the picture. Written in millimetres rather than layers because it is a
    // dimension of the plate; the stack's own base_layers is measured from it.
    float base_mm   = 1.f;
    // How finely the picture's face is divided, in millimetres. The relief is carried by vertices, so
    // a face of two triangles has no picture on it however good the height map is. The bake subdivides
    // further where the picture needs it; this is only enough to start from.
    float cell_mm   = 1.f;

    ChromaFrame frame;

    bool operator==(const ChromaPlate &o) const
    {
        return generated == o.generated && width_mm == o.width_mm && height_mm == o.height_mm &&
               base_mm == o.base_mm && cell_mm == o.cell_mm && frame == o.frame;
    }
    bool operator!=(const ChromaPlate &o) const { return !(*this == o); }

    template<class Archive> void serialize(Archive &ar)
    {
        ar(generated, width_mm, height_mm, base_mm, cell_mm, frame);
    }
};

struct ChromaStack
{
    std::vector<ChromaBand> bands; // bottom-up; bands.front() is the one the base is printed in

    // The print's layer height, mm. Taken from the print profile rather than chosen here - a stack
    // solved against 0.08 mm layers prints as the wrong picture at 0.2 mm, so the GUI re-solves when
    // the profile's layer height changes.
    float layer_height_mm = 0.08f;

    // Layers of solid backing under the picture: everything the model already is below the painted
    // face. Not part of any band - the bands are the picture - but printed in the bottom band's
    // filament, since that is what is loaded when the print starts.
    //
    // Keeping it out of the bands is what lets it be what it really is: a measurement of the model,
    // often a hundred layers or more, while a band is a handful. Folding the two together put a
    // three-digit number on a slider meant for single digits and made "how thick is the backing" and
    // "how many layers does the first filament get" the same control, which they are not.
    //
    // The colour model treats the base as opaque. That is what makes a column's colour depend only on
    // the layers above it, and why the panel warns about a base thin enough to let the plate show
    // through.
    int  base_layers = 4;

    // Whether `base_layers` follows the model or was typed in. It follows by default, and it should:
    // the painted face is wherever the geometry puts it, and a base that disagrees with it shifts
    // every colour in the picture - the stack ends up anchored at the bottom of the part instead of
    // under the face, so the picture's whole range is squeezed into the first few millimetres and
    // everything above it clamps to the top band.
    //
    // It cannot be measured once and left: a layer is created, and its picture chosen, before there
    // is any paint to measure, and the face moves again whenever the part is scaled or the layer
    // height changes. So the panel re-measures it every frame while this is set.
    bool base_auto = true;

    // Spread the quantization error into neighbouring pixels (Floyd-Steinberg, serpentine) instead
    // of rounding each pixel to its nearest palette colour on its own. A palette of twenty or thirty
    // colours has to cover a photograph's whole range, so plain rounding leaves visible bands across
    // a sky or a cheek; diffusing the error trades them for fine stipple, which at a tenth of a
    // millimetre per pixel reads as the intermediate colour. Off for flat-coloured artwork, where
    // stipple is the artifact rather than the cure.
    bool dither = false;

    // A fingerprint of the object height ranges this stack last wrote, or 0 for none. The swaps are
    // written whenever the stack changes, which is only safe while they are still the ones we wrote:
    // the ranges are the object's, a user may have made their own, and silently replacing those would
    // be the panel taking something it was not given. Ranges that do not match this are left alone and
    // the card says so.
    uint64_t applied_ranges = 0;

    // Total printed layers, base included: what the tallest column is.
    int  total_layers() const;
    // Layers the picture is built from, i.e. the bands' own total above the base.
    int  relief_layers() const;
    // The relief's height in mm, which is the `depth_mm` of the texture displacement layer the
    // height map is handed to.
    float depth_mm() const;
    bool  empty() const { return bands.empty(); }

    // The plate this picture made for itself, if it made one. A picture painted onto a model leaves
    // it at its defaults and nothing reads it.
    ChromaPlate plate;

    template<class Archive> void serialize(Archive &ar)
    {
        ar(bands, layer_height_mm, base_layers, dither, base_auto, applied_ranges, plate);
    }
};

// The optical depth of one layer of a filament: how much of what is behind it that layer takes out,
// as the exponent of Beer-Lambert absorption. A layer passes exp(-density) of the light from under it,
// and a run of n layers passes exp(-n * density), which is what makes the whole model per-band rather
// than per-layer arithmetic.
//
// The one place the relation between a TD, a layer height and an absorption lives, so the palette and
// the preview's shader cannot drift apart: both ask here.
float chroma_layer_density(float td_mm, float layer_height_mm);

// The colour the stack shows at every column height: entry k is a column of exactly k printed
// layers, so the vector has total_layers() + 1 entries.
//
// The model is Beer-Lambert absorption, layer by layer. A layer of thickness h in a filament of
// transmission distance TD passes
//
//     T = exp(-h / (CHROMA_TD_OPAQUE_FRACTION * TD / 3))
//
// of the light coming from under it and shows its own colour with the rest, so
//
//     C_k = (1 - T_k) * c_k + T_k * C_(k-1)
//
// walking up from an opaque base, C_0 = c_1. The divisor is what makes "opaque at
// CHROMA_TD_OPAQUE_FRACTION * TD" true: three e-folding lengths pass 5% of the light, which is
// where the colour beneath stops being visible to the eye.
//
// Beer-Lambert is not just the physically honest choice here, it is the only one that stays
// consistent when applied per layer: transmittance multiplies, so a run of n layers of one filament
// passes exp(-n h / d) whether it is composited in one step or n, and the palette does not depend on
// how the stack happens to be cut into bands.
//
// Compositing is done on linear light and the entries are converted back to sRGB, because the
// quantity being attenuated is light, not an sRGB code.
std::vector<ColorRGB> chroma_palette(const ChromaStack &stack);

// What chroma_solve() produced.
struct ChromaSolve
{
    int width  = 0;
    int height = 0;

    // Per pixel, row-major and top-to-bottom like DecodedHeightTexture: the column's printed layer
    // count, between base_layers and total_layers(). Sixteen bits because it counts from the build
    // plate, and the base alone can be several hundred layers on a tall part.
    std::vector<uint16_t> levels;
    // The same thing as the 8-bit grayscale height map the displacement samples: 0 at base_layers,
    // 255 at total_layers(). This is what gets encoded as a PNG into the layer's `image_data`, which
    // is why the solve has to be the thing that owns the quantization - the relief's steps have to
    // land on layer boundaries, and a grey level is the only channel the displacement reads.
    std::vector<uint8_t> grey;
    // Per pixel RGB: the palette colour the solve chose, i.e. what the print is predicted to look
    // like. Shown in the panel next to the source picture, since the 3D view can only show the
    // relief - the colours do not exist until the print swaps filament.
    std::vector<uint8_t> preview;

    // How far the prediction landed from the picture, as a mean and worst-case CIE76 dE over all
    // pixels. The panel reports it because it is the only honest answer to "will this stack
    // reproduce my image": under about 5 is a good match, over about 15 means the filaments cannot
    // reach the picture's colours and no amount of layers will fix it.
    double mean_delta_e = 0.;
    double max_delta_e  = 0.;

    bool empty() const { return width <= 0 || height <= 0 || levels.empty(); }
};

// The painted surface itself, sampled on the solve's own grid: how far from level it is at each
// texel of the picture. Built by the GUI, which is the only place that knows where the picture lands
// on the mesh.
struct ChromaSurface
{
    int width  = 0;
    int height = 0;
    // cos(tilt) of the surface under this texel, i.e. the component of its normal along the print
    // axis. 0 where no painted triangle covers the texel, which is also how "nothing here" is told
    // from "level here".
    std::vector<float> facing;

    bool empty() const { return width <= 0 || height <= 0 || facing.empty(); }
};

// Deepens the solved relief where the surface is tilted, so its steps still land on the print heights
// the colours were solved for, and returns the depth in millimetres the layer must carry for the
// rewritten grey map to mean the same thing.
//
// The grey map is a linear encoding of "how far to displace along the normal", so the correction is a
// per-texel factor on it: 1/cos(tilt), clamped at CHROMA_MAX_TILT_DEG. The deepest texel sets the
// layer's depth and keeps the full 8 bits in use.
//
// A level patch leaves the map and the depth exactly as they were, so nothing about a flat picture
// changes by passing through here.
float chroma_tilt_relief(ChromaSolve &solve, const ChromaStack &stack, const ChromaSurface &surface);

// Solves `rgb` (width * height * 3 bytes, row-major, top-to-bottom) against the stack's palette.
// An empty stack, a zero-sized image or a stack with no relief layers gives an empty result.
ChromaSolve chroma_solve(const uint8_t *rgb, int width, int height, const ChromaStack &stack);

// How many layers of this filament it takes to hide what is under it, at this layer height. The
// single most useful number about a stack and the one nothing else makes visible: it is what decides
// whether a band is a gradient or a step.
//
// A filament hides what is behind it at about CHROMA_TD_OPAQUE_FRACTION of its TD (see
// chroma_palette()), so this is that depth in layers. Below two the band has no intermediate colours
// at all - the picture can only use the filament or not use it - which is what a coarse layer height
// does to an otherwise good stack: TD 4 at 0.2 mm covers in two layers, the same filament at 0.08 mm
// in five.
float chroma_layers_to_opaque(float td_mm, float layer_height_mm);

// How many of the picture's layers actually change the colour.
//
// A band eventually stops saying anything: once it hides what is under it, another layer of it shows
// the colour the one below already showed. Those layers still cost print time and still take up
// relief height, and nothing about the stack makes them visible - the band looks like ten layers of
// gradient and is, say, five of gradient and five of flat.
//
// Measured against the palette rather than against TD, because how long a band keeps paying depends
// on what is under it as much as on the filament: a light band over a dark one is finished in four or
// five layers, while a dark one over a light one goes on moving the colour perceptibly well past the
// depth at which it is nominally opaque.
int chroma_distinct_levels(const ChromaStack &stack);

// Shortens every band to its last layer that still changes the colour. Bands already shorter than
// that are left alone: they are still inside their fade, where every layer counts.
void chroma_trim_bands(ChromaStack &stack);

// The printed layer a point `z_mm` above the bottom of the object falls in: 1-based, clamped to the
// stack, 0 for a stack that prints nothing. Pair it with chroma_palette() to get the colour shown
// there.
//
// This, rather than the height map, is what decides the colour a surface shows. A filament swap
// happens at one print Z across the whole plate, so every surface crossing that height changes colour
// whatever the picture says - the terraces of the relief, the walls between them, and the sides of the
// model alike. Colouring a preview this way is therefore not an approximation of the print; it is what
// the print does, and it keeps working after the relief has been baked into the mesh and the height
// map is no longer attached to anything on screen.
int chroma_level_at(const ChromaStack &stack, double z_mm);

// The solve's height map as an 8-bit grayscale PNG, ready to be stored in a
// TextureDisplacementLayer's `image_data`.
std::vector<unsigned char> chroma_encode_height_png(const ChromaSolve &solve);

// One filament swap as an object height range: the Z band, in the object's own coordinates (0 at its
// bottom), that prints in `filament` (0-based).
struct ChromaRange
{
    double z_min = 0.;
    double z_max = 0.;
    int    filament = -1;
};

// The stack's bands as height ranges, in object coordinates, starting at `z_bottom` (which is 0 for
// a stack whose base starts at the object's first layer). Bands printed in the same filament as the
// band below are merged, so a stack that repeats a filament does not produce a pointless swap.
std::vector<ChromaRange> chroma_ranges(const ChromaStack &stack, double z_bottom = 0.);

// A picture's colours, coarsely bucketed and weighted by how much of it they cover. Scoring a
// candidate stack against this rather than against the picture is what makes trying hundreds of them
// affordable: a photograph is millions of pixels and a few hundred distinguishable colours.
struct ChromaHistogram
{
    std::vector<std::pair<ColorRGB, float>> bins; // colour and its share of the picture, summing to 1
    bool empty() const { return bins.empty(); }
};
ChromaHistogram chroma_histogram(const uint8_t *rgb, int width, int height, int max_bins = 192);

// Picks `count` of `available`, in printing order, for a picture.
//
// The order matters as much as the choice - a stack is filaments seen through each other, so the same
// four in a different order reach different colours - which is why this searches orderings rather than
// just picking the four nearest the picture's own colours. It grows the stack one filament at a time,
// trying every remaining filament at every position and keeping whichever placement loses the least to
// the picture, which is a few dozen candidate stacks instead of the tens of thousands an exhaustive
// search over subsets and permutations would be.
//
// Bands come out equal, since what each one needs depends on the stack it ends up in; chroma_trim_bands()
// is what shortens them afterwards. An empty result means there was nothing to choose from.
ChromaStack chroma_auto_stack(const ChromaHistogram &picture, const std::vector<ChromaFilament> &available,
                              int count, float layer_height_mm, int base_layers, int relief_layers);

// How far a stack lands from a picture, as the mean colour difference over `picture`'s bins - the same
// measure the solve reports, computed without solving. This is what chroma_auto_stack() minimises.
double chroma_stack_error(const ChromaHistogram &picture, const ChromaStack &stack);

// One colour worth owning for a picture, and what the user already has that is like it.
struct ChromaSuggestion
{
    ColorRGB color;            // the colour to go looking for
    float    td_mm = 4.f;      // how translucent it wants to be, for the band it would print
    int      layers = 1;       // how many layers that band would get
    int      nearest = -1;     // the loaded filament closest to it, or -1 when none is loaded
    float    nearest_de = 0.f; // how far that one is, as a colour difference
};

// The `count` colours a picture would most like to be printed in, in printing order, whether or not
// anyone owns them.
//
// chroma_auto_stack() answers "which of my filaments", which is the question with a cheap answer and
// a ceiling: a picture whose colours are not in the drawer cannot be printed well out of it however
// the drawer is arranged. This answers the other one - what to go and buy - by letting the colours
// themselves move: a weighted k-means over the picture's colours for a starting set, then a hill
// climb that walks each band's colour wherever the stack's own error falls fastest.
//
// Each suggestion carries the transmission distance its band wants, since a colour is only half of
// what to look for: the same red at TD 1 and at TD 6 are different filaments for this purpose.
std::vector<ChromaSuggestion> chroma_suggest_colors(const ChromaHistogram &picture, int count,
                                                    const std::vector<ChromaFilament> &loaded,
                                                    float layer_height_mm, int relief_layers);

// A step wedge for measuring a filament's transmission distance by printing it.
//
// TD cannot be read off a spool and no filament preset ships one, so every stack starts from a guess
// and every prediction rests on it. The measurement is a staircase of the filament under test printed
// over a contrasting base: the step at which the base stops showing through is the depth at which that
// filament covers, and CHROMA_TD_OPAQUE_FRACTION turns that depth into a TD.
struct ChromaWedge
{
    int   steps           = 16;    // how many thicknesses to offer
    float layer_height_mm = 0.08f; // the height they step by, and the height it must be printed at
    int   base_layers     = 6;     // the contrasting base under them, thick enough to be opaque itself
    float step_depth_mm   = 7.f;   // how deep each step is, i.e. how much of it there is to look at
    float width_mm        = 36.f;  // across the staircase
    // Every fifth step is widened by this much, so the step a user is looking at can be counted
    // without counting every step up to it.
    float tick_mm         = 5.f;

    float base_height_mm() const { return float(base_layers) * layer_height_mm; }
};

// The frame's cross-section, swept around the outline to make the band between the plate's edge and
// the picture.
//
// A polyline in (how far in from the edge, how high above z = 0), outer edge first, ending where the
// picture starts: the last sample is always (width_mm, base_mm), because that is where the frame
// meets the picture's own surface. Two samples at the same inset are a vertical face, which is what
// makes a step a step.
//
// Everything a frame can be is this polyline, which is why the profiles are an enum of shapes rather
// than a library of models: a thicker frame is a longer sweep of the same curve, and an SVG outline
// is the same curve swept around a different loop.
std::vector<Vec2f> chroma_frame_profile(const ChromaFrame &frame, float base_mm);

// The plate's outline, in millimetres, centred on the picture: the loop the frame's profile is swept
// around. `grow_mm` is how far outside the picture's own rectangle it runs, which is the frame's
// width for the plate's edge and less than that for a ring further into the sweep.
//
// Shape::Outline returns the frame's own loop, scaled about its centre so that it sits `grow_mm`
// outside the picture - an SVG is a shape, not a size, and the picture is what has the size.
std::vector<Vec2f> chroma_frame_outline(const ChromaPlate &plate, float grow_mm);

// The plate as a mesh, sitting on z = 0 and centred on the origin.
//
// The picture's own face comes first in `indices`, which is what lets the caller paint it without
// knowing anything about how the rest of the plate is built: the first `picture_triangles` triangles
// are the surface the picture is printed on, and everything after them is the frame and the backing.
struct ChromaPlateMesh
{
    indexed_triangle_set its;
    size_t               picture_triangles = 0;

    bool empty() const { return its.indices.empty(); }
};

ChromaPlateMesh chroma_plate_mesh(const ChromaPlate &plate);

// The wedge as a mesh, sitting on z = 0. The base is one slab; step i (1-based) stands i layers proud
// of it, so the filament under test is i layers thick there.
indexed_triangle_set chroma_wedge_mesh(const ChromaWedge &wedge);

// The transmission distance a wedge has measured: `step` is the first step at which the base below no
// longer shows through. 0 or less means it was not readable, and gives 0.
float chroma_td_from_step(int step, float layer_height_mm);

// A starting point for a stack: the given filaments, lightest at the bottom, each with an equal
// share of `relief_layers` on top of `base_layers`.
//
// Light-to-dark is the convention filament-painting guides teach, and it is not arbitrary: a dark
// filament over a light one reads as a midtone at one or two layers and as the dark colour at four,
// so putting the dark filaments last is what buys the long tonal ramp. The reverse order gives a few
// washed-out steps and then nothing.
ChromaStack chroma_default_stack(const std::vector<ChromaFilament> &filaments, float layer_height_mm,
                                     int base_layers, int relief_layers);

} // namespace Slic3r

#endif // slic3r_ChromaStack_hpp_
