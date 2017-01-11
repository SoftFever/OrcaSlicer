#include "ClipperUtils.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "PerimeterGenerator.hpp"
#include "Layer.hpp"
#include "Print.hpp"
#include "SupportMaterial.hpp"
#include "Fill/FillBase.hpp"
#include "EdgeGrid.hpp"

#include <cmath>
#include <memory>
#include <boost/log/trivial.hpp>
#include <unordered_set>

// #define SLIC3R_DEBUG

// Make assert active if SLIC3R_DEBUG
#ifdef SLIC3R_DEBUG
    #undef NDEBUG
    #include "SVG.hpp"
#endif

#include <cassert>

namespace Slic3r {

// Increment used to reach MARGIN in steps to avoid trespassing thin objects
#define NUM_MARGIN_STEPS 3

// Dimensions of a tree-like structure to save material
#define PILLAR_SIZE (2.5)
#define PILLAR_SPACING 10

//#define SUPPORT_SURFACES_OFFSET_PARAMETERS ClipperLib::jtMiter, 3.
//#define SUPPORT_SURFACES_OFFSET_PARAMETERS ClipperLib::jtMiter, 1.5
#define SUPPORT_SURFACES_OFFSET_PARAMETERS ClipperLib::jtSquare, 0.

#ifdef SLIC3R_DEBUG
const char* support_surface_type_to_color_name(const PrintObjectSupportMaterial::SupporLayerType surface_type)
{
    switch (surface_type) {
        case PrintObjectSupportMaterial::sltTopContact:     return "rgb(255,0,0)"; // "red";
        case PrintObjectSupportMaterial::sltTopInterface:   return "rgb(0,255,0)"; // "green";
        case PrintObjectSupportMaterial::sltBase:           return "rgb(0,0,255)"; // "blue";
        case PrintObjectSupportMaterial::sltBottomInterface:return "rgb(255,255,128)"; // yellow 
        case PrintObjectSupportMaterial::sltBottomContact:  return "rgb(255,0,255)"; // magenta
        case PrintObjectSupportMaterial::sltRaftInterface:  return "rgb(0,255,255)";
        case PrintObjectSupportMaterial::sltRaftBase:       return "rgb(128,128,128)";
        case PrintObjectSupportMaterial::sltUnknown:        return "rgb(128,0,0)"; // maroon
        default:                                            return "rgb(64,64,64)";
    };
}

Point export_support_surface_type_legend_to_svg_box_size()
{
    return Point(scale_(1.+10.*8.), scale_(3.)); 
}

void export_support_surface_type_legend_to_svg(SVG &svg, const Point &pos)
{
    // 1st row
    coord_t pos_x0 = pos.x + scale_(1.);
    coord_t pos_x = pos_x0;
    coord_t pos_y = pos.y + scale_(1.5);
    coord_t step_x = scale_(10.);
    svg.draw_legend(Point(pos_x, pos_y), "top contact"    , support_surface_type_to_color_name(PrintObjectSupportMaterial::sltTopContact));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "top iface"      , support_surface_type_to_color_name(PrintObjectSupportMaterial::sltTopInterface));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "base"           , support_surface_type_to_color_name(PrintObjectSupportMaterial::sltBase));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "bottom iface"   , support_surface_type_to_color_name(PrintObjectSupportMaterial::sltBottomInterface));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "bottom contact" , support_surface_type_to_color_name(PrintObjectSupportMaterial::sltBottomContact));
    // 2nd row
    pos_x = pos_x0;
    pos_y = pos.y+scale_(2.8);
    svg.draw_legend(Point(pos_x, pos_y), "raft interface" , support_surface_type_to_color_name(PrintObjectSupportMaterial::sltRaftInterface));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "raft base"      , support_surface_type_to_color_name(PrintObjectSupportMaterial::sltRaftBase));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "unknown"        , support_surface_type_to_color_name(PrintObjectSupportMaterial::sltUnknown));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "intermediate"   , support_surface_type_to_color_name(PrintObjectSupportMaterial::sltIntermediate));
}

void export_print_z_polygons_to_svg(const char *path, PrintObjectSupportMaterial::MyLayer ** const layers, size_t n_layers)
{
    BoundingBox bbox;
    for (int i = 0; i < n_layers; ++ i)
        bbox.merge(get_extents(layers[i]->polygons));
    Point legend_size = export_support_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min.x, bbox.max.y);
    bbox.merge(Point(std::max(bbox.min.x + legend_size.x, bbox.max.x), bbox.max.y + legend_size.y));
    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (int i = 0; i < n_layers; ++ i)
        svg.draw(union_ex(layers[i]->polygons), support_surface_type_to_color_name(layers[i]->layer_type), transparency);
    for (int i = 0; i < n_layers; ++ i)
        svg.draw(to_lines(layers[i]->polygons), support_surface_type_to_color_name(layers[i]->layer_type));
    export_support_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}
#endif /* SLIC3R_DEBUG */

PrintObjectSupportMaterial::PrintObjectSupportMaterial(const PrintObject *object, const SlicingParameters &slicing_params) :
    m_object                (object),
    m_print_config          (&object->print()->config),
    m_object_config         (&object->config),
    m_slicing_params        (slicing_params),

    m_first_layer_flow (Flow::new_from_config_width(
        frSupportMaterial,
        // The width parameter accepted by new_from_config_width is of type ConfigOptionFloatOrPercent, the Flow class takes care of the percent to value substitution.
        (object->print()->config.first_layer_extrusion_width.value > 0) ? object->print()->config.first_layer_extrusion_width : object->config.support_material_extrusion_width,
        float(object->print()->config.nozzle_diameter.get_at(object->config.support_material_extruder-1)),
        float(slicing_params.first_print_layer_height),
        false)),
    m_support_material_flow (Flow::new_from_config_width(
        frSupportMaterial, 
        // The width parameter accepted by new_from_config_width is of type ConfigOptionFloatOrPercent, the Flow class takes care of the percent to value substitution.
        (object->config.support_material_extrusion_width.value > 0) ? object->config.support_material_extrusion_width : object->config.extrusion_width,
        float(object->print()->config.nozzle_diameter.get_at(object->config.support_material_extruder-1)),
        float(slicing_params.layer_height),
        false)), 
    m_support_material_interface_flow(Flow::new_from_config_width(
        frSupportMaterialInterface,
        // The width parameter accepted by new_from_config_width is of type ConfigOptionFloatOrPercent, the Flow class takes care of the percent to value substitution.
        (object->config.support_material_extrusion_width > 0) ? object->config.support_material_extrusion_width : object->config.extrusion_width,
        float(object->print()->config.nozzle_diameter.get_at(object->config.support_material_interface_extruder-1)),
        float(slicing_params.layer_height),
        false)),
 
    // 50 mirons layer
    m_support_layer_height_min  (0.05),
    m_support_layer_height_max  (0.)
{
    if (m_object_config->support_material_interface_layers.value == 0) {
        // No interface layers allowed, print everything with the base support pattern.
        m_support_material_interface_flow = m_support_material_flow;
    }

    // Evaluate the XY gap between the object outer perimeters and the support structures.
    coordf_t external_perimeter_width = 0.;
    for (std::map<size_t,std::vector<int>>::const_iterator it_region = object->region_volumes.begin(); it_region != object->region_volumes.end(); ++ it_region) {
        const PrintRegionConfig &config = object->print()->get_region(it_region->first)->config;
        coordf_t width = config.external_perimeter_extrusion_width.get_abs_value(slicing_params.layer_height);
        if (width <= 0.)
            width = m_print_config->nozzle_diameter.get_at(config.perimeter_extruder-1);
        external_perimeter_width = std::max(external_perimeter_width, width);
    }
    m_gap_xy = m_object_config->support_material_xy_spacing.get_abs_value(external_perimeter_width);
}

// Using the std::deque as an allocator.
inline PrintObjectSupportMaterial::MyLayer& layer_allocate(
    std::deque<PrintObjectSupportMaterial::MyLayer> &layer_storage, 
    PrintObjectSupportMaterial::SupporLayerType      layer_type)
{ 
    layer_storage.push_back(PrintObjectSupportMaterial::MyLayer());
    layer_storage.back().layer_type = layer_type;
    return layer_storage.back();
}

inline void layers_append(PrintObjectSupportMaterial::MyLayersPtr &dst, const PrintObjectSupportMaterial::MyLayersPtr &src)
{
    dst.insert(dst.end(), src.begin(), src.end());
}

// Compare layers lexicographically.
struct MyLayersPtrCompare
{
    bool operator()(const PrintObjectSupportMaterial::MyLayer* layer1, const PrintObjectSupportMaterial::MyLayer* layer2) const {
        return *layer1 < *layer2;
    }
};

void PrintObjectSupportMaterial::generate(PrintObject &object)
{
    BOOST_LOG_TRIVIAL(info) << "Support generator - Start";

    coordf_t max_object_layer_height = 0.;
    for (size_t i = 0; i < object.layer_count(); ++ i)
        max_object_layer_height = std::max(max_object_layer_height, object.layers[i]->height);

    if (m_support_layer_height_max == 0)
        m_support_layer_height_max = std::max(max_object_layer_height, 0.75 * m_support_material_flow.nozzle_diameter);
//  m_support_interface_layer_height_max = std::max(max_object_layer_height, 0.75 * m_support_material_interface_flow.nozzle_diameter);

    // Layer instances will be allocated by std::deque and they will be kept until the end of this function call.
    // The layers will be referenced by various LayersPtr (of type std::vector<Layer*>)
    MyLayerStorage layer_storage;

    BOOST_LOG_TRIVIAL(info) << "Support generator - Creating top contacts";

    // Determine the top contact surfaces of the support, defined as:
    // contact = overhangs - clearance + margin
    // This method is responsible for identifying what contact surfaces
    // should the support material expose to the object in order to guarantee
    // that it will be effective, regardless of how it's built below.
    // If raft is to be generated, the 1st top_contact layer will contain the 1st object layer silhouette without holes.
    MyLayersPtr top_contacts = this->top_contact_layers(object, layer_storage);
    if (top_contacts.empty())
        // Nothing is supported, no supports are generated.
        return;

#ifdef SLIC3R_DEBUG
    static int iRun = 0;
    iRun ++;
    for (MyLayersPtr::const_iterator it = top_contacts.begin(); it != top_contacts.end(); ++ it)
        Slic3r::SVG::export_expolygons(
            debug_out_path("support-top-contacts-%d-%lf.svg", iRun, (*it)->print_z), 
            union_ex((*it)->polygons, false));
#endif /* SLIC3R_DEBUG */

    BOOST_LOG_TRIVIAL(info) << "Support generator - Creating bottom contacts";

    // Determine the bottom contact surfaces of the supports over the top surfaces of the object.
    // Depending on whether the support is soluble or not, the contact layer thickness is decided.
    // layer_support_areas contains the per object layer support areas. These per object layer support areas
    // may get merged and trimmed by this->generate_base_layers() if the support layers are not synchronized with object layers.
    std::vector<Polygons> layer_support_areas;
    MyLayersPtr bottom_contacts = this->bottom_contact_layers_and_layer_support_areas(
        object, top_contacts, layer_storage,
        layer_support_areas);

#ifdef SLIC3R_DEBUG
    for (size_t layer_id = 0; layer_id < object.layers.size(); ++ layer_id)
        Slic3r::SVG::export_expolygons(
            debug_out_path("support-areas-%d-%lf.svg", iRun, object.layers[layer_id]->print_z), 
            union_ex(layer_support_areas[layer_id], false));
#endif /* SLIC3R_DEBUG */

    BOOST_LOG_TRIVIAL(info) << "Support generator - Trimming top contacts by bottom contacts";

    // Because the top and bottom contacts are thick slabs, they may overlap causing over extrusion 
    // and unwanted strong bonds to the object.
    // Rather trim the top contacts by their overlapping bottom contacts to leave a gap instead of over extruding
    // top contacts over the bottom contacts.
    this->trim_top_contacts_by_bottom_contacts(object, bottom_contacts, top_contacts);

    BOOST_LOG_TRIVIAL(info) << "Support generator - Creating intermediate layers - indices";

    // Allocate empty layers between the top / bottom support contact layers
    // as placeholders for the base and intermediate support layers.
    // The layers may or may not be synchronized with the object layers, depending on the configuration.
    // For example, a single nozzle multi material printing will need to generate a waste tower, which in turn
    // wastes less material, if there are as little tool changes as possible.
    MyLayersPtr intermediate_layers = this->raft_and_intermediate_support_layers(
        object, bottom_contacts, top_contacts, layer_storage, max_object_layer_height);

    this->trim_support_layers_by_object(object, top_contacts, m_support_layer_height_min, 0., m_gap_xy);

    BOOST_LOG_TRIVIAL(info) << "Support generator - Creating base layers";

    // Fill in intermediate layers between the top / bottom support contact layers, trimm them by the object.
    this->generate_base_layers(object, bottom_contacts, top_contacts, intermediate_layers, layer_support_areas);

#ifdef SLIC3R_DEBUG
    for (MyLayersPtr::const_iterator it = intermediate_layers.begin(); it != intermediate_layers.end(); ++ it)
        Slic3r::SVG::export_expolygons(
            debug_out_path("support-base-layers-%d-%lf.svg", iRun, (*it)->print_z), 
            union_ex((*it)->polygons, false));
#endif /* SLIC3R_DEBUG */

    BOOST_LOG_TRIVIAL(info) << "Support generator - Creating raft";

    // If raft is to be generated, the 1st top_contact layer will contain the 1st object layer silhouette with holes filled.
    // There is also a 1st intermediate layer containing bases of support columns.
    // Inflate the bases of the support columns and create the raft base under the object.
    MyLayersPtr raft_layers = this->generate_raft_base(object, top_contacts, intermediate_layers, layer_storage);

/*
    // If we wanted to apply some special logic to the first support layers lying on
    // object's top surfaces this is the place to detect them
    LayersSet shape;
    if (m_objectconfig->support_material_pattern.value == smpPillars)
        shape = this->generate_pillars_shape(contact, support_z);
*/

    BOOST_LOG_TRIVIAL(info) << "Support generator - Creating interfaces";

    // Propagate top / bottom contact layers to generate interface layers.
    MyLayersPtr interface_layers = this->generate_interface_layers(
        object, bottom_contacts, top_contacts, intermediate_layers, layer_storage);

#ifdef SLIC3R_DEBUG
    for (MyLayersPtr::const_iterator it = interface_layers.begin(); it != interface_layers.end(); ++ it)
        Slic3r::SVG::export_expolygons(
            debug_out_path("support-interface-layers-%d-%lf.svg", iRun, (*it)->print_z), 
            union_ex((*it)->polygons, false));
#endif /* SLIC3R_DEBUG */

/*
    // Clip with the pillars.
    if (! shape.empty()) {
        this->clip_with_shape(interface, shape);
        this->clip_with_shape(base, shape);
    }
*/

    BOOST_LOG_TRIVIAL(info) << "Support generator - Creating layers";

//    raft_layers.clear();
//    bottom_contacts.clear();
//    top_contacts.clear();
//    intermediate_layers.clear();
//    interface_layers.clear();

    // Install support layers into the object.
    // A support layer installed on a PrintObject has a unique print_z.
    MyLayersPtr layers_sorted;
    layers_sorted.reserve(raft_layers.size() + bottom_contacts.size() + top_contacts.size() + intermediate_layers.size() + interface_layers.size());
    layers_append(layers_sorted, raft_layers);
    layers_append(layers_sorted, bottom_contacts);
    layers_append(layers_sorted, top_contacts);
    layers_append(layers_sorted, intermediate_layers);
    layers_append(layers_sorted, interface_layers);
    // Sort the layers lexicographically by a raising print_z and a decreasing height.
    std::sort(layers_sorted.begin(), layers_sorted.end(), MyLayersPtrCompare());
    int layer_id = 0;
    assert(object.support_layers.empty());
    for (int i = 0; i < int(layers_sorted.size());) {
        // Find the last layer with roughly the same print_z, find the minimum layer height of all.
		// Due to the floating point inaccuracies, the print_z may not be the same even if in theory they should.
        int j = i + 1;
		coordf_t zmax = layers_sorted[i]->print_z + EPSILON;
		for (; j < layers_sorted.size() && layers_sorted[j]->print_z <= zmax; ++j) ;
		// Assign an average print_z to the set of layers with nearly equal print_z.
		coordf_t zavg = 0.5 * (layers_sorted[i]->print_z + layers_sorted[j - 1]->print_z);
		coordf_t height_min = layers_sorted[i]->height;
		for (int u = i; u < j; ++u) {
			MyLayer &layer = *layers_sorted[u];
			layer.print_z = zavg;
			height_min = std::min(height_min, layer.height);
		}
		object.add_support_layer(layer_id, height_min, zavg);
        if (layer_id > 0) {
            // Inter-link the support layers into a linked list.
            SupportLayer *sl1 = object.support_layers[object.support_layer_count()-2];
            SupportLayer *sl2 = object.support_layers.back();
            sl1->upper_layer = sl2;
            sl2->lower_layer = sl1;
        }
#ifdef SLIC3R_DEBUG
        export_print_z_polygons_to_svg(debug_out_path("support-%d-%lf.svg", iRun, zavg).c_str(), layers_sorted.data() + i, j - i);
#endif
        i = j;
        ++ layer_id;
    }
    
    BOOST_LOG_TRIVIAL(info) << "Support generator - Generating tool paths";

    // Generate the actual toolpaths and save them into each layer.
    this->generate_toolpaths(object, raft_layers, bottom_contacts, top_contacts, intermediate_layers, interface_layers);

    BOOST_LOG_TRIVIAL(info) << "Support generator - End";
}

// Collect all polygons of all regions in a layer with a given surface type.
Polygons collect_region_slices_by_type(const Layer &layer, SurfaceType surface_type)
{
    // 1) Count the new polygons first.
    size_t n_polygons_new = 0;
    for (LayerRegionPtrs::const_iterator it_region = layer.regions.begin(); it_region != layer.regions.end(); ++ it_region) {
        const LayerRegion       &region = *(*it_region);
        const SurfaceCollection &slices = region.slices;
        for (Surfaces::const_iterator it = slices.surfaces.begin(); it != slices.surfaces.end(); ++ it) {
            const Surface &surface = *it;
            if (surface.surface_type == surface_type)
                n_polygons_new += surface.expolygon.holes.size() + 1;
        }
    }

    // 2) Collect the new polygons.
    Polygons out;
    out.reserve(n_polygons_new);
    for (LayerRegionPtrs::const_iterator it_region = layer.regions.begin(); it_region != layer.regions.end(); ++ it_region) {
        const LayerRegion       &region = *(*it_region);
        const SurfaceCollection &slices = region.slices;
        for (Surfaces::const_iterator it = slices.surfaces.begin(); it != slices.surfaces.end(); ++ it) {
            const Surface &surface = *it;
            if (surface.surface_type == surface_type)
                polygons_append(out, surface.expolygon);
        }
    }

    return out;
}

// Collect outer contours of all slices of this layer.
// This is useful for calculating the support base with holes filled.
Polygons collect_slices_outer(const Layer &layer)
{
    Polygons out;
    out.reserve(out.size() + layer.slices.expolygons.size());
    for (ExPolygons::const_iterator it = layer.slices.expolygons.begin(); it != layer.slices.expolygons.end(); ++ it)
        out.push_back(it->contour);
    return out;
}

// Generate top contact layers supporting overhangs.
// For a soluble interface material synchronize the layer heights with the object, otherwise leave the layer height undefined.
// If supports over bed surface only are requested, don't generate contact layers over an object.
PrintObjectSupportMaterial::MyLayersPtr PrintObjectSupportMaterial::top_contact_layers(
    const PrintObject &object, MyLayerStorage &layer_storage) const
{
#ifdef SLIC3R_DEBUG
    static int iRun = 0;
    ++ iRun; 
#endif /* SLIC3R_DEBUG */

    // Output layers, sorted by top Z.
    MyLayersPtr contact_out;

    // If user specified a custom angle threshold, convert it to radians.
    // Zero means automatic overhang detection.
    double threshold_rad = (m_object_config->support_material_threshold.value > 0) ? 
        M_PI * double(m_object_config->support_material_threshold.value + 1) / 180. : // +1 makes the threshold inclusive
        0.;
    
    // Build support on a build plate only? If so, then collect top surfaces into buildplate_only_top_surfaces
    // and subtract buildplate_only_top_surfaces from the contact surfaces, so
    // there is no contact surface supported by a top surface.
    bool buildplate_only = this->build_plate_only();
    Polygons buildplate_only_top_surfaces;

    // Determine top contact areas.
    // If generating raft only (no support), only calculate top contact areas for the 0th layer.
    size_t num_layers = this->has_support() ? object.layer_count() : 1;
    // If having a raft, start with 0th layer, otherwise with 1st layer.
    // Note that layer_id < layer->id when raft_layers > 0 as the layer->id incorporates the raft layers.
    // So layer_id == 0 means first object layer and layer->id == 0 means first print layer if there are no explicit raft layers.
    for (size_t layer_id = this->has_raft() ? 0 : 1; layer_id < num_layers; ++ layer_id) 
    {
        const Layer &layer = *object.layers[layer_id];

        // Detect overhangs and contact areas needed to support them.
        // Collect overhangs and contacts of all regions of this layer supported by the layer immediately below.
        Polygons overhang_polygons;
        Polygons contact_polygons;
        Polygons slices_margin_cached;
        float    slices_margin_cached_offset = -1.;
        if (layer_id == 0) {
            // This is the first object layer, so the object is being printed on a raft and
            // we're here just to get the object footprint for the raft.
            // We only consider contours and discard holes to get a more continuous raft.
            overhang_polygons = collect_slices_outer(layer);
            // Extend by SUPPORT_MATERIAL_MARGIN, which is 1.5mm
            contact_polygons = offset(overhang_polygons, scale_(SUPPORT_MATERIAL_MARGIN));
        } else {
            // Generate overhang / contact_polygons for non-raft layers.
            const Layer &lower_layer = *object.layers[layer_id-1];
            if (buildplate_only) {
                // Merge the new slices with the preceding slices.
                // Apply the safety offset to the newly added polygons, so they will connect
                // with the polygons collected before,
                // but don't apply the safety offset during the union operation as it would
                // inflate the polygons over and over.
                polygons_append(buildplate_only_top_surfaces, offset(lower_layer.slices.expolygons, scale_(0.01)));
                buildplate_only_top_surfaces = union_(buildplate_only_top_surfaces, false); // don't apply the safety offset.
            }

            for (LayerRegionPtrs::const_iterator it_layerm = layer.regions.begin(); it_layerm != layer.regions.end(); ++ it_layerm) {
                const LayerRegion &layerm = *(*it_layerm);
                // Extrusion width accounts for the roundings of the extrudates.
                // It is the maximum widh of the extrudate.
                float fw = float(layerm.flow(frExternalPerimeter).scaled_width());
                float lower_layer_offset = 
                    (layer_id < m_object_config->support_material_enforce_layers.value) ? 
                        // Enforce a full possible support, ignore the overhang angle.
                        0.f :
                    (threshold_rad > 0. ? 
                        // Overhang defined by an angle.
                        float(scale_(lower_layer.height / tan(threshold_rad))) :
                        // Overhang defined by half the extrusion width.
                        0.5f * fw);
                // Overhang polygons for this layer and region.
                Polygons diff_polygons;
                Polygons layerm_polygons = to_polygons(layerm.slices);
                Polygons lower_layer_polygons = to_polygons(lower_layer.slices.expolygons);
                if (lower_layer_offset == 0.f) {
                    // Support everything.
                    diff_polygons = diff(layerm_polygons, lower_layer_polygons);
                } else {
                    // Get the regions needing a suport, collapse very tiny spots.
                    //FIXME cache the lower layer offset if this layer has multiple regions.
                    diff_polygons = offset2(
                        diff(layerm_polygons,
                             offset(lower_layer_polygons, lower_layer_offset, SUPPORT_SURFACES_OFFSET_PARAMETERS)), 
                        -0.1f*fw, +0.1f*fw);
                    if (diff_polygons.empty())
                        continue;
                    // Offset the support regions back to a full overhang, restrict them to the full overhang.
                    diff_polygons = diff(
                        intersection(offset(diff_polygons, lower_layer_offset, SUPPORT_SURFACES_OFFSET_PARAMETERS), layerm_polygons), 
                        lower_layer_polygons);
                }
                if (diff_polygons.empty())
                    continue;

                #ifdef SLIC3R_DEBUG
                {
                    ::Slic3r::SVG svg(debug_out_path("support-top-contacts-raw-run%d-layer%d-region%d.svg", iRun, layer_id, it_layerm - layer.regions.begin()), get_extents(diff_polygons));
                    Slic3r::ExPolygons expolys = union_ex(diff_polygons, false);
                    svg.draw(expolys);
                }
                #endif /* SLIC3R_DEBUG */

                if (m_object_config->dont_support_bridges) {
                    // compute the area of bridging perimeters
                    // Note: this is duplicate code from GCode.pm, we need to refactor
                    if (true) {
                        Polygons bridged_perimeters;
                        {
                            Flow bridge_flow = layerm.flow(frPerimeter, true);
                            coordf_t nozzle_diameter = m_print_config->nozzle_diameter.get_at(layerm.region()->config.perimeter_extruder-1);
                            Polygons lower_grown_slices = offset(lower_layer_polygons, 0.5f*float(scale_(nozzle_diameter)), SUPPORT_SURFACES_OFFSET_PARAMETERS);
                            
                            // Collect perimeters of this layer.
                            // TODO: split_at_first_point() could split a bridge mid-way
                            Polylines overhang_perimeters;
                            for (ExtrusionEntitiesPtr::const_iterator it_island = layerm.perimeters.entities.begin(); it_island != layerm.perimeters.entities.end(); ++ it_island) {
                                const ExtrusionEntityCollection *island = dynamic_cast<ExtrusionEntityCollection*>(*it_island);
                                assert(island != NULL);
                                for (size_t i = 0; i < island->entities.size(); ++ i) {
                                    ExtrusionEntity *entity = island->entities[i];
                                    ExtrusionLoop *loop = dynamic_cast<Slic3r::ExtrusionLoop*>(entity);
                                    overhang_perimeters.push_back(loop ? 
                                        loop->as_polyline() :
                                        dynamic_cast<const Slic3r::ExtrusionPath*>(entity)->polyline);
                                }
                            }
                            
                            // workaround for Clipper bug, see Slic3r::Polygon::clip_as_polyline()
                            for (Polylines::iterator it = overhang_perimeters.begin(); it != overhang_perimeters.end(); ++ it)
                                it->points[0].x += 1;
                            // Trim the perimeters of this layer by the lower layer to get the unsupported pieces of perimeters.
                            overhang_perimeters = diff_pl(overhang_perimeters, lower_grown_slices);
                            
                            // only consider straight overhangs
                            // only consider overhangs having endpoints inside layer's slices
                            // convert bridging polylines into polygons by inflating them with their thickness
                            // since we're dealing with bridges, we can't assume width is larger than spacing,
                            // so we take the largest value and also apply safety offset to be ensure no gaps
                            // are left in between
                            float w = float(std::max(bridge_flow.scaled_width(), bridge_flow.scaled_spacing()));
                            for (Polylines::iterator it = overhang_perimeters.begin(); it != overhang_perimeters.end(); ++ it) {
                                if (it->is_straight()) {
                                    // This is a bridge 
                                    it->extend_start(fw);
                                    it->extend_end(fw);
                                    if (layer.slices.contains(it->first_point()) && layer.slices.contains(it->last_point()))
                                        // Offset a polyline into a polygon.
                                        polygons_append(bridged_perimeters, offset(*it, 0.5f * w + 10.f));
                                }
                            }
                            bridged_perimeters = union_(bridged_perimeters);
                        }
                        // remove the entire bridges and only support the unsupported edges
                        Polygons bridges;
                        for (Surfaces::const_iterator it = layerm.fill_surfaces.surfaces.begin(); it != layerm.fill_surfaces.surfaces.end(); ++ it)
                            if (it->surface_type == stBottomBridge && it->bridge_angle != -1)
                                polygons_append(bridges, it->expolygon);
                        diff_polygons = diff(diff_polygons, bridges, true);
                        polygons_append(bridges, bridged_perimeters);
                        polygons_append(diff_polygons, 
                            intersection(
                                // Offset unsupported edges into polygons.
                                offset(layerm.unsupported_bridge_edges.polylines, scale_(SUPPORT_MATERIAL_MARGIN), SUPPORT_SURFACES_OFFSET_PARAMETERS),
                                bridges));
                    } else {
                        // just remove bridged areas
                        diff_polygons = diff(diff_polygons, layerm.bridged, true);
                    }
                } // if (m_objconfig->dont_support_bridges)

                if (buildplate_only) {
                    // Don't support overhangs above the top surfaces.
                    // This step is done before the contact surface is calculated by growing the overhang region.
                    diff_polygons = diff(diff_polygons, buildplate_only_top_surfaces);
                }

                if (diff_polygons.empty())
                    continue;

                #ifdef SLIC3R_DEBUG
                Slic3r::SVG::export_expolygons(
                    debug_out_path("support-top-contacts-filtered-run%d-layer%d-region%d.svg", iRun, layer_id, it_layerm - layer.regions.begin()),
                    union_ex(diff_polygons, false));
                #endif /* SLIC3R_DEBUG */

                if (this->has_contact_loops())
                    polygons_append(overhang_polygons, diff_polygons);

                // Let's define the required contact area by using a max gap of half the upper 
                // extrusion width and extending the area according to the configured margin.
                // We increment the area in steps because we don't want our support to overflow
                // on the other side of the object (if it's very thin).
                {
                    //FIMXE 1) Make the offset configurable, 2) Make the Z span configurable.
                    float slices_margin_offset = float(0.5*fw);
                    if (slices_margin_cached_offset != slices_margin_offset) {
                        slices_margin_cached_offset = slices_margin_offset;
                        slices_margin_cached = offset(lower_layer.slices.expolygons, slices_margin_offset, SUPPORT_SURFACES_OFFSET_PARAMETERS);
                        if (buildplate_only) {
                            // Trim the inflated contact surfaces by the top surfaces as well.
                            polygons_append(slices_margin_cached, buildplate_only_top_surfaces);
                            slices_margin_cached = union_(slices_margin_cached);
                        }
                    }
                    // Offset the contact polygons outside.
                    for (size_t i = 0; i < NUM_MARGIN_STEPS; ++ i) {
                        diff_polygons = diff(
                            offset(
                                diff_polygons,
                                SUPPORT_MATERIAL_MARGIN / NUM_MARGIN_STEPS,
                                ClipperLib::jtRound,
                                // round mitter limit
                                scale_(0.05)),
                            slices_margin_cached);
                    }
                }
                polygons_append(contact_polygons, diff_polygons);
            } // for each layer.region
        } // end of Generate overhang/contact_polygons for non-raft layers.
        
        // now apply the contact areas to the layer were they need to be made
        if (! contact_polygons.empty()) {
            // get the average nozzle diameter used on this layer
            MyLayer     &new_layer   = layer_allocate(layer_storage, sltTopContact);
            const Layer *layer_below = (layer_id > 0) ? object.layers[layer_id - 1] : NULL;
            new_layer.idx_object_layer_above = layer_id;
            if (m_slicing_params.soluble_interface) {
                // Align the contact surface height with a layer immediately below the supported layer.
                new_layer.height = layer_below ? 
                    // Interface layer will be synchronized with the object.
                    object.layers[layer_id-1]->height : 
                    // Don't know the thickness of the raft layer yet.
                    0.;
                new_layer.print_z = layer.print_z - layer.height;
                new_layer.bottom_z = new_layer.print_z - new_layer.height;
            } else {
                // Contact layer will be printed with a normal flow, but
                // it will support layers printed with a bridging flow.
                //FIXME Probably printing with the bridge flow? How about the unsupported perimeters? Are they printed with the bridging flow?
                // In the future we may switch to a normal extrusion flow for the supported bridges.
                // Get the average nozzle diameter used on this layer.
                coordf_t nozzle_dmr = 0.;
                size_t   n_nozzle_dmrs = 0;
                for (LayerRegionPtrs::const_iterator it_region_ptr = layer.regions.begin(); it_region_ptr != layer.regions.end(); ++ it_region_ptr) {
                    const PrintRegion &region = *(*it_region_ptr)->region();
                    nozzle_dmr += m_print_config->nozzle_diameter.get_at(region.config.perimeter_extruder.value - 1);
                    nozzle_dmr += m_print_config->nozzle_diameter.get_at(region.config.infill_extruder.value - 1);
                    nozzle_dmr += m_print_config->nozzle_diameter.get_at(region.config.solid_infill_extruder.value - 1);
                    n_nozzle_dmrs += 3;
                }
                nozzle_dmr /= coordf_t(n_nozzle_dmrs);
                new_layer.print_z  = layer.print_z - nozzle_dmr - m_object_config->support_material_contact_distance;
                new_layer.bottom_z = new_layer.print_z;
                new_layer.height   = 0.;
                if (this->synchronize_layers()) {
                    // Align bottom of this layer with a top of the closest object layer
                    // while not trespassing into the 1st layer and keeping the support layer thickness bounded.
                    int layer_id_below = int(layer_id) - 1;
                    for (; layer_id_below >= 0; -- layer_id_below) {
                        layer_below = object.layers[layer_id_below];
                        if (layer_below->print_z <= new_layer.print_z - m_support_layer_height_min) {
                            // This is a feasible support layer height.
                            new_layer.bottom_z = layer_below->print_z;
                            new_layer.height = new_layer.print_z - new_layer.bottom_z;
                            assert(new_layer.height <= m_support_layer_height_max);
                            break;
                        }                        
                    }
                    if (layer_id_below == -1) {
                        // Could not align with any of the top surfaces of object layers.
                        if (this->has_raft()) {
                            // If having a raft, all the other layers will be aligned one with the other.
                        } else {
                            // Give up, ignore this layer.
                            continue;
                        }
                    }
                } else {
                    // Don't know the height of the top contact layer yet. The top contact layer is printed with a normal flow and 
                    // its height will be set adaptively later on.
                }
            }

            // Ignore this contact area if it's too low.
            // Don't want to print a layer below the first layer height as it may not stick well.
            //FIXME there may be a need for a single layer support, then one may decide to print it either as a bottom contact or a top contact
            // and it may actually make sense to do it with a thinner layer than the first layer height.
            if (new_layer.print_z < this->first_layer_height() + m_support_layer_height_min)
                continue;

#if 1
            {
                // Create an EdgeGrid, initialize it with projection, initialize signed distance field.
                Slic3r::EdgeGrid::Grid grid;
                coordf_t support_spacing = m_object_config->support_material_spacing.value + m_support_material_flow.spacing();
                coord_t grid_resolution = scale_(support_spacing); // scale_(1.5f);
                BoundingBox bbox = get_extents(contact_polygons);
                bbox.offset(20);
                bbox.align_to_grid(grid_resolution);
                grid.set_bbox(bbox);
                grid.create(contact_polygons, grid_resolution);
                grid.calculate_sdf();
                // Extract a bounding contour from the grid, trim by the object.
                contact_polygons = diff(
                    grid.contours_simplified(m_support_material_flow.scaled_spacing()/2 + 5),
                    slices_margin_cached,
                    true);
            }
#endif

            new_layer.polygons = std::move(contact_polygons);
            // Store the overhang polygons as the aux_polygons.
            // The overhang polygons are used in the path generator for planning of the contact loops.
            // if (this->has_contact_loops())
            new_layer.aux_polygons = new Polygons(std::move(overhang_polygons));
            contact_out.push_back(&new_layer);

            if (0) { 
                // Slic3r::SVG::output("out\\contact_" . $contact_z . ".svg",
                //     green_expolygons => union_ex($buildplate_only_top_surfaces),
                //     blue_expolygons  => union_ex(\@contact),
                //     red_expolygons   => union_ex(\@overhang),
                // );
            }
        }
    }

    return contact_out;
}

// Generate bottom contact layers supporting the top contact layers.
// For a soluble interface material synchronize the layer heights with the object, 
// otherwise set the layer height to a bridging flow of a support interface nozzle.
PrintObjectSupportMaterial::MyLayersPtr PrintObjectSupportMaterial::bottom_contact_layers_and_layer_support_areas(
    const PrintObject &object, const MyLayersPtr &top_contacts, MyLayerStorage &layer_storage,
    std::vector<Polygons> &layer_support_areas) const
{
#ifdef SLIC3R_DEBUG
    static int iRun = 0;
    ++ iRun; 
#endif /* SLIC3R_DEBUG */

    // Allocate empty surface areas, one per object layer.
    layer_support_areas.assign(object.total_layer_count(), Polygons());

    // find object top surfaces
    // we'll use them to clip our support and detect where does it stick
    MyLayersPtr bottom_contacts;

    if (! top_contacts.empty()) 
    {
        // There is some support to be built, if there are non-empty top surfaces detected.
        // Sum of unsupported contact areas above the current layer.print_z.
        Polygons  projection;
        // Last top contact layer visited when collecting the projection of contact areas.
        int       contact_idx = int(top_contacts.size()) - 1;
        for (int layer_id = int(object.total_layer_count()) - 2; layer_id >= 0; -- layer_id) {
            BOOST_LOG_TRIVIAL(trace) << "Support generator - bottom_contact_layers - layer " << layer_id;
            const Layer &layer = *object.get_layer(layer_id);
            // Top surfaces of this layer, to be used to stop the surface volume from growing down.
            Polygons top;
            if (! m_object_config->support_material_buildplate_only)
                top = collect_region_slices_by_type(layer, stTop);
            // Collect projections of all contact areas above or at the same level as this top surface.
            for (; contact_idx >= 0 && top_contacts[contact_idx]->print_z >= layer.print_z; -- contact_idx) {
                Polygons polygons_new;
                // Contact surfaces are expanded away from the object, trimmed by the object.
                // Use a slight positive offset to overlap the touching regions.
                polygons_append(polygons_new, offset(top_contacts[contact_idx]->polygons, SCALED_EPSILON));
                // These are the overhang surfaces. They are touching the object and they are not expanded away from the object.
                // Use a slight positive offset to overlap the touching regions.
                polygons_append(polygons_new, offset(*top_contacts[contact_idx]->aux_polygons, SCALED_EPSILON));
                polygons_append(projection, union_(polygons_new));
            }
            if (projection.empty())
                continue;
            projection = union_(projection);
    #ifdef SLIC3R_DEBUG
            {
                BoundingBox bbox = get_extents(projection);
                bbox.merge(get_extents(top));
                ::Slic3r::SVG svg(debug_out_path("support-bottom-layers-raw-%d-%lf.svg", iRun, layer.print_z), bbox);
                svg.draw(union_ex(top, false), "blue", 0.5f);
                svg.draw(union_ex(projection, true), "red", 0.5f);
                svg.draw_outline(union_ex(projection, true), "red", "blue", scale_(0.1f));
                svg.draw(layer.slices.expolygons, "green", 0.5f);
            }
    #endif /* SLIC3R_DEBUG */

            // Now find whether any projection of the contact surfaces above layer.print_z not yet supported by any 
            // top surfaces above layer.print_z falls onto this top surface. 
            // Touching are the contact surfaces supported exclusively by this top surfaces.
            // Don't use a safety offset as it has been applied during insertion of polygons.
            if (! top.empty()) {
                Polygons touching = intersection(top, projection, false);
                if (! touching.empty()) {
                    // Allocate a new bottom contact layer.
                    MyLayer &layer_new = layer_allocate(layer_storage, sltBottomContact);
                    bottom_contacts.push_back(&layer_new);
                    // Grow top surfaces so that interface and support generation are generated
                    // with some spacing from object - it looks we don't need the actual
                    // top shapes so this can be done here
                    layer_new.height  = m_slicing_params.soluble_interface ? 
                        // Align the interface layer with the object's layer height.
                        object.get_layer(layer_id + 1)->height :
                        // Place a bridge flow interface layer over the top surface.
                        m_support_material_interface_flow.nozzle_diameter;
                    layer_new.print_z = layer.print_z + layer_new.height + 
                        (m_slicing_params.soluble_interface ? 0. : m_object_config->support_material_contact_distance.value);
                    layer_new.bottom_z = layer.print_z;
                    layer_new.idx_object_layer_below = layer_id;
                    layer_new.bridging = ! m_slicing_params.soluble_interface;
                    //FIXME how much to inflate the top surface?
                    layer_new.polygons = offset(touching, float(m_support_material_flow.scaled_width()), SUPPORT_SURFACES_OFFSET_PARAMETERS);
        #ifdef SLIC3R_DEBUG
                    Slic3r::SVG::export_expolygons(
                        debug_out_path("support-bottom-contacts-%d-%lf.svg", iRun, layer_new.print_z), 
                        union_ex(layer_new.polygons, false));
        #endif /* SLIC3R_DEBUG */
                }
            } // ! top.empty()

            remove_sticks(projection);
            remove_degenerate(projection);

            // Create an EdgeGrid, initialize it with projection, initialize signed distance field.
            Slic3r::EdgeGrid::Grid grid;
            coordf_t support_spacing = m_object_config->support_material_spacing.value + m_support_material_flow.spacing();
            coord_t grid_resolution = scale_(support_spacing); // scale_(1.5f);
            BoundingBox bbox = get_extents(projection);
            bbox.offset(20);
            bbox.align_to_grid(grid_resolution);
			grid.set_bbox(bbox);
			grid.create(projection, grid_resolution);
            grid.calculate_sdf();

            // Extract a bounding contour from the grid.
            Polygons projection_simplified = grid.contours_simplified(-5);
#ifdef SLIC3R_DEBUG
            {
                BoundingBox bbox = get_extents(projection);
                bbox.merge(get_extents(projection_simplified));
                ::Slic3r::SVG svg(debug_out_path("support-bottom-contacts-simplified-%d-%d.svg", iRun, layer_id), bbox);
                svg.draw(union_ex(projection, false), "blue", 0.5);
                svg.draw(union_ex(projection_simplified, false), "red", 0.5);
    #ifdef SLIC3R_GUI
                bbox.min.x -= scale_(5.f);
                bbox.min.y -= scale_(5.f);
                bbox.max.x += scale_(5.f);
                bbox.max.y += scale_(5.f);
                EdgeGrid::save_png(grid, bbox, scale_(0.1f), debug_out_path("support-bottom-contacts-df-%d-%d.png", iRun, layer_id).c_str());
    #endif /* SLIC3R_GUI */
            }
#endif /* SLIC3R_DEBUG */
            // Cache the slice of a support volume. The support volume is expanded by 1/2 of support material flow spacing
            // to allow a placement of suppot zig-zag snake along the grid lines.
            layer_support_areas[layer_id] = diff(
                grid.contours_simplified(m_support_material_flow.scaled_spacing()/2 + 5), 
                to_polygons(layer.slices.expolygons),
                true);

            // Remove the areas that touched from the projection that will continue on next, lower, top surfaces.
            // projection = diff(projection, touching);
            projection = diff(projection_simplified, to_polygons(layer.slices.expolygons), true);
//            layer_support_areas[layer_id] = projection;
        }
        std::reverse(bottom_contacts.begin(), bottom_contacts.end());
    } // ! top_contacts.empty()

    trim_support_layers_by_object(object, bottom_contacts, m_support_layer_height_min, 0., m_gap_xy);

    return bottom_contacts;
}

// Trim the top_contacts layers with the bottom_contacts layers if they overlap, so there would not be enough vertical space for both of them.
void PrintObjectSupportMaterial::trim_top_contacts_by_bottom_contacts(
    const PrintObject &object, const MyLayersPtr &bottom_contacts, MyLayersPtr &top_contacts) const
{
    size_t idx_top_first = 0;
    // For all bottom contact layers:
    for (size_t idx_bottom = 0; idx_bottom < bottom_contacts.size() && idx_top_first < top_contacts.size(); ++ idx_bottom) {
        const MyLayer &layer_bottom = *bottom_contacts[idx_bottom];
        // Find the first top layer overlapping with layer_bottom.
        while (idx_top_first < top_contacts.size() && top_contacts[idx_top_first]->print_z <= layer_bottom.print_z - layer_bottom.height)
            ++ idx_top_first;
        // For all top contact layers overlapping with the thick bottom contact layer:
        for (size_t idx_top = idx_top_first; idx_top < top_contacts.size(); ++ idx_top) {
            MyLayer &layer_top = *top_contacts[idx_top];
            coordf_t interface_z = (layer_top.print_z == layer_top.bottom_z) ? 
                // Layer height has not been decided yet.
                (layer_top.bottom_z - m_support_layer_height_min) :
                // Layer height has already been assigned.
                (layer_top.bottom_z + EPSILON);
            if (interface_z < layer_bottom.print_z) {
                // Layers overlap. Trim layer_top with layer_bottom.
                layer_top.polygons = diff(layer_top.polygons, layer_bottom.polygons);
            } else
                break;
        }
    }
}

// A helper for sorting the top / bottom contact layers by their contact with the touching support layer:
// Top contact surfaces (those supporting overhangs) are sorted by their bottom print Z,
// bottom contact surfaces (those supported by top object surfaces) are sorted by their top print Z.
struct LayerExtreme
{
    LayerExtreme(PrintObjectSupportMaterial::MyLayer *alayer, bool ais_top) : layer(alayer), is_top(ais_top) {}
    PrintObjectSupportMaterial::MyLayer  *layer;
    // top or bottom extreme
    bool         is_top;

    coordf_t    z() const { return is_top ? layer->print_z : layer->print_z - layer->height; }

    bool operator<(const LayerExtreme &other) const { return z() < other.z(); }
};

PrintObjectSupportMaterial::MyLayersPtr PrintObjectSupportMaterial::raft_and_intermediate_support_layers(
    const PrintObject   &object,
    const MyLayersPtr   &bottom_contacts,
    const MyLayersPtr   &top_contacts,
    MyLayerStorage      &layer_storage,
    const coordf_t       max_object_layer_height) const
{
    MyLayersPtr intermediate_layers;

    // Collect and sort the extremes (bottoms of the top contacts and tops of the bottom contacts).
    std::vector<LayerExtreme> extremes;
    extremes.reserve(top_contacts.size() + bottom_contacts.size());
    for (size_t i = 0; i < top_contacts.size(); ++ i)
        // Bottoms of the top contact layers. In case of non-soluble supports,
        // the top contact layer thickness is not known yet.
        extremes.push_back(LayerExtreme(top_contacts[i], false));
    for (size_t i = 0; i < bottom_contacts.size(); ++ i)
        // Tops of the bottom contact layers.
        extremes.push_back(LayerExtreme(bottom_contacts[i], true));
    if (extremes.empty())
        return intermediate_layers;
    std::sort(extremes.begin(), extremes.end());

	assert(extremes.front().z() > m_slicing_params.raft_interface_top_z && extremes.front().z() >= m_slicing_params.first_print_layer_height);

//    bool synchronize = m_slicing_params.soluble_interface || this->synchronize_layers();
    bool synchronize = this->synchronize_layers();

    // Generate intermediate layers.
    // The first intermediate layer is the same as the 1st layer if there is no raft,
    // or the bottom of the first intermediate layer is aligned with the bottom of the raft contact layer.
    // Intermediate layers are always printed with a normal etrusion flow (non-bridging).
    size_t idx_layer_object = 0;
    for (size_t idx_extreme = 0; idx_extreme < extremes.size(); ++ idx_extreme) {
        LayerExtreme *extr1  = (idx_extreme == 0) ? NULL : &extremes[idx_extreme-1];
        coordf_t      extr1z = (extr1 == NULL) ? m_slicing_params.raft_interface_top_z : extr1->z();
        LayerExtreme &extr2  = extremes[idx_extreme];
        coordf_t      extr2z = extr2.z();
        coordf_t      dist   = extr2z - extr1z;
        assert(dist >= 0.);
		if (dist == 0.)
			continue;
        // Insert intermediate layers.
        size_t        n_layers_extra = size_t(ceil(dist / m_support_layer_height_max));
        assert(n_layers_extra > 0);
        coordf_t      step   = dist / coordf_t(n_layers_extra);
        if (! synchronize && extr2.layer->layer_type == sltTopContact) {
            // This is a top interface layer, which does not have a height assigned yet. Do it now.
            assert(extr2.layer->height == 0.);
            extr2.layer->height = step;
            extr2.layer->bottom_z = extr2z = extr2.layer->print_z - step;
            -- n_layers_extra;
            if (extr2.layer->bottom_z < this->first_layer_height()) {
                // Split the span into two layers: the top layer up to the first layer height, 
                // and the new intermediate layer below.
                // 1) Adjust the bottom of this top layer.
                assert(n_layers_extra == 0);
                extr2.layer->bottom_z = extr2z = this->first_layer_height();
                extr2.layer->height = extr2.layer->print_z - extr2.layer->bottom_z;
                // 2) Insert a new intermediate layer.
                MyLayer &layer_new = layer_allocate(layer_storage, sltIntermediate);
                layer_new.bottom_z   = extr1z;
                layer_new.print_z    = this->first_layer_height();
                layer_new.height     = layer_new.print_z - layer_new.bottom_z;
                intermediate_layers.push_back(&layer_new);
                continue;
            }
        } else if (extr1z + step > this->first_layer_height()) {
            MyLayer &layer_new = layer_allocate(layer_storage, sltIntermediate);
            layer_new.bottom_z   = extr1z;
            layer_new.print_z    = extr1z = this->first_layer_height();
            layer_new.height     = layer_new.print_z - layer_new.bottom_z;
            intermediate_layers.push_back(&layer_new);
            dist = extr2z - extr1z;
            assert(dist >= 0.);
            n_layers_extra = size_t(ceil(dist / m_support_layer_height_max));
            step = dist / coordf_t(n_layers_extra);
        }
        coordf_t extr2z_large_steps = extr2z;
        if (synchronize) {
            // Synchronize support layers with the object layers.
            if (object.layers.front()->print_z - extr1z > m_support_layer_height_max) {
                // Generate the initial couple of layers before reaching the 1st object layer print_z level.
                extr2z_large_steps = object.layers.front()->print_z;
                dist = extr2z_large_steps - extr1z;
                assert(dist >= 0.);
                n_layers_extra = size_t(ceil(dist / m_support_layer_height_max));
                step = dist / coordf_t(n_layers_extra);
            }
        }
        // Take the largest allowed step in the Z axis until extr2z_large_steps is reached.
        for (size_t i = 0; i < n_layers_extra; ++ i) {
            MyLayer &layer_new = layer_allocate(layer_storage, sltIntermediate);
			if (i + 1 == n_layers_extra) {
				// Last intermediate layer added. Align the last entered layer with extr2z_large_steps exactly.
				layer_new.bottom_z = (i == 0) ? extr1z : intermediate_layers.back()->print_z;
				layer_new.print_z = extr2z_large_steps;
				layer_new.height = layer_new.print_z - layer_new.bottom_z;
			}
			else {
				// Intermediate layer, not the last added.
				layer_new.height = step;
				layer_new.bottom_z = extr1z + i * step;
				layer_new.print_z = layer_new.bottom_z + step;
			}
            intermediate_layers.push_back(&layer_new);
        }
        if (synchronize) {
            // Emit support layers synchronized with object layers.
            extr1z = extr2z_large_steps;
            while (extr1z < extr2z) {
                //while (idx_layer_object < object.layers.size() && object.layers[idx_layer_object].print_z < extr1z)
                // idx_layer_object
            }
        }
    }

#ifdef _DEBUG
	for (size_t i = 0; i < top_contacts.size(); ++i)
		assert(top_contacts[i]->height > 0.);
#endif /* _DEBUG */

    return intermediate_layers;
}

// At this stage there shall be intermediate_layers allocated between bottom_contacts and top_contacts, but they have no polygons assigned.
// Also the bottom/top_contacts shall have a layer thickness assigned already.
void PrintObjectSupportMaterial::generate_base_layers(
    const PrintObject   &object,
    const MyLayersPtr   &bottom_contacts,
    const MyLayersPtr   &top_contacts,
    MyLayersPtr         &intermediate_layers,
    std::vector<Polygons> &layer_support_areas) const
{
#ifdef SLIC3R_DEBUG
    static int iRun = 0;
#endif /* SLIC3R_DEBUG */

    if (top_contacts.empty())
        // No top contacts -> no intermediate layers will be produced.
        return;

    // coordf_t fillet_radius_scaled = scale_(m_object_config->support_material_spacing);
    int idx_top_contact_above = int(top_contacts.size()) - 1;
    int idx_bottom_contact_overlapping = int(bottom_contacts.size()) - 1;
    int idx_object_layer_above = int(object.total_layer_count()) - 1;
    for (int idx_intermediate = int(intermediate_layers.size()) - 1; idx_intermediate >= 0; -- idx_intermediate)
    {
        BOOST_LOG_TRIVIAL(trace) << "Support generator - generate_base_layers - creating layer " << 
            idx_intermediate << " of " << intermediate_layers.size();
        MyLayer &layer_intermediate = *intermediate_layers[idx_intermediate];

        // Find a top_contact layer touching the layer_intermediate from above, if any, and collect its polygons into polygons_new.
        while (idx_top_contact_above >= 0 && top_contacts[idx_top_contact_above]->bottom_z > layer_intermediate.print_z + EPSILON)
            -- idx_top_contact_above;

        // New polygons for layer_intermediate.
        Polygons polygons_new;

        // Use the precomputed layer_support_areas.
        while (idx_object_layer_above > 0 && object.get_layer(idx_object_layer_above - 1)->print_z > layer_intermediate.print_z - EPSILON)
            -- idx_object_layer_above;
        polygons_new = layer_support_areas[idx_object_layer_above];

        // Polygons to trim polygons_new.
        Polygons polygons_trimming; 

        // Find the first top_contact layer intersecting with this layer.
        int idx_top_contact_overlapping = idx_top_contact_above;
        while (idx_top_contact_overlapping >= 0 && 
               top_contacts[idx_top_contact_overlapping]->bottom_z > layer_intermediate.print_z - EPSILON)
            -- idx_top_contact_overlapping;
        // Collect all the top_contact layer intersecting with this layer.
        for (; idx_top_contact_overlapping >= 0; -- idx_top_contact_overlapping) {
			MyLayer &layer_top_overlapping = *top_contacts[idx_top_contact_overlapping];
            if (layer_top_overlapping.print_z < layer_intermediate.bottom_z + EPSILON)
                break;
            polygons_append(polygons_trimming, layer_top_overlapping.polygons);
        }

        // Find the first bottom_contact layer intersecting with this layer.
        while (idx_bottom_contact_overlapping >= 0 && 
            bottom_contacts[idx_bottom_contact_overlapping]->bottom_z > layer_intermediate.print_z - EPSILON)
            -- idx_bottom_contact_overlapping;
        // Collect all the top_contact layer intersecting with this layer.
        for (int i = idx_bottom_contact_overlapping; i >= 0; -- i) {
			MyLayer &layer_bottom_overlapping = *bottom_contacts[i];
            if (layer_bottom_overlapping.print_z < layer_intermediate.print_z - layer_intermediate.height + EPSILON)
                break; 
            polygons_append(polygons_trimming, layer_bottom_overlapping.polygons);
        }

#ifdef SLIC3R_DEBUG
        {
            BoundingBox bbox = get_extents(polygons_new);
            bbox.merge(get_extents(polygons_trimming));
            ::Slic3r::SVG svg(debug_out_path("support-intermediate-layers-raw-%d-%lf.svg", iRun, layer_intermediate.print_z), bbox);
            svg.draw(union_ex(polygons_new, false), "blue", 0.5f);
            svg.draw(union_ex(polygons_trimming, true), "red", 0.5f);
        }
#endif /* SLIC3R_DEBUG */

        // Trim the polygons, store them.
        if (polygons_trimming.empty())
            layer_intermediate.polygons = std::move(polygons_new);
        else
			layer_intermediate.polygons = diff(
                polygons_new,
                polygons_trimming,
                true); // safety offset to merge the touching source polygons
        layer_intermediate.layer_type = sltBase;

/*
        if (0) {
            // Fillet the base polygons and trim them again with the top, interface and contact layers.
            $base->{$i} = diff(
                offset2(
                    $base->{$i}, 
                    $fillet_radius_scaled, 
                    -$fillet_radius_scaled,
                    # Use a geometric offsetting for filleting.
                    JT_ROUND,
                    0.2*$fillet_radius_scaled),
                $trim_polygons,
                false); // don't apply the safety offset.
        }
*/
    }
 
#ifdef SLIC3R_DEBUG
	for (MyLayersPtr::const_iterator it = intermediate_layers.begin(); it != intermediate_layers.end(); ++it)
        ::Slic3r::SVG::export_expolygons(
            debug_out_path("support-intermediate-layers-untrimmed-%d-%lf.svg", iRun, (*it)->print_z),
            union_ex((*it)->polygons, false));
    ++ iRun;
#endif /* SLIC3R_DEBUG */

    trim_support_layers_by_object(object, intermediate_layers, m_support_layer_height_min, m_support_layer_height_min, m_gap_xy);
}

void PrintObjectSupportMaterial::trim_support_layers_by_object(
    const PrintObject   &object,
    MyLayersPtr         &support_layers,
    const coordf_t       gap_extra_above,
    const coordf_t       gap_extra_below,
    const coordf_t       gap_xy) const
{
    //FIXME This could be trivially parallelized.
    const coord_t  gap_xy_scaled = scale_(gap_xy);
    size_t idx_object_layer_overlapping = 0;
    // For all intermediate support layers:
    for (MyLayersPtr::iterator it_layer = support_layers.begin(); it_layer != support_layers.end(); ++ it_layer) {
        BOOST_LOG_TRIVIAL(trace) << "Support generator - trim_support_layers_by_object - trimmming layer " << 
            (it_layer - support_layers.begin()) << " of " << support_layers.size();

        MyLayer &support_layer = *(*it_layer);
        if (support_layer.polygons.empty())
            // Empty support layer, nothing to trim.
            continue;
        // Find the overlapping object layers including the extra above / below gap.
        while (idx_object_layer_overlapping < object.layer_count() && 
            object.get_layer(idx_object_layer_overlapping)->print_z < support_layer.print_z - support_layer.height - gap_extra_below + EPSILON)
            ++ idx_object_layer_overlapping;
        // Collect all the object layers intersecting with this layer.
        Polygons polygons_trimming;
        for (int i = idx_object_layer_overlapping; i < object.layer_count(); ++ i) {
            const Layer &object_layer = *object.get_layer(i);
            if (object_layer.print_z - object_layer.height > support_layer.print_z + gap_extra_above - EPSILON)
                break;
            polygons_append(polygons_trimming, (Polygons)object_layer.slices);
        }

        // $layer->slices contains the full shape of layer, thus including
        // perimeter's width. $support contains the full shape of support
        // material, thus including the width of its foremost extrusion.
        // We leave a gap equal to a full extrusion width.
        support_layer.polygons = diff(
            support_layer.polygons,
            offset(polygons_trimming, gap_xy_scaled, SUPPORT_SURFACES_OFFSET_PARAMETERS));
    }
}

PrintObjectSupportMaterial::MyLayersPtr PrintObjectSupportMaterial::generate_raft_base(
    const PrintObject   &object,
    const MyLayersPtr   &top_contacts,
    MyLayersPtr         &intermediate_layers,
    MyLayerStorage      &layer_storage) const
{
    // Areas covered by the raft, supporting the raft interface and the support columns.
    Polygons raft_polygons;
    // How much to inflate the support columns to be stable. This also applies to the 1st layer, if no raft layers are to be printed.
    const float inflate_factor = scale_(3.);
    MyLayer *contacts      = top_contacts.empty() ? nullptr : top_contacts.front();
    MyLayer *columns_base  = intermediate_layers.empty() ? nullptr : intermediate_layers.front();
	if (contacts != nullptr && contacts->print_z > m_slicing_params.raft_contact_top_z + EPSILON)
		// This is not the raft contact layer.
		contacts = nullptr;

	// Output vector.
    MyLayersPtr raft_layers;

    // Expand the 1st intermediate layer, which contains the bases of the support columns.
	Polygons base;
	if (columns_base != nullptr) {
		base = offset(columns_base->polygons, inflate_factor);
		// Modify the 1st intermediate layer with the expanded support columns.
		columns_base->polygons = diff(
			base,
			offset(m_object->layers.front()->slices.expolygons, scale_(m_gap_xy), SUPPORT_SURFACES_OFFSET_PARAMETERS));
		if (contacts != nullptr)
			columns_base->polygons = diff(columns_base->polygons, contacts->polygons);
	}
    if (m_slicing_params.has_raft() && contacts != nullptr) {
        // Merge the untrimmed columns base with the expanded raft interface, to be used for the support base and interface.
        base = union_(base, offset(contacts->polygons, inflate_factor, SUPPORT_SURFACES_OFFSET_PARAMETERS));
    }
	if (m_slicing_params.has_raft() && m_slicing_params.raft_layers() > 1 && ! base.empty()) {
        // Do not add the raft contact layer, only add the raft layers below the contact layer.
        // Insert the 1st layer.
        {
            MyLayer &new_layer = layer_allocate(layer_storage, (m_slicing_params.base_raft_layers > 0) ? sltRaftBase : sltRaftInterface);
            raft_layers.push_back(&new_layer);
            new_layer.print_z = m_slicing_params.first_print_layer_height;
            new_layer.height  = m_slicing_params.first_print_layer_height;
            new_layer.bottom_z = 0.;
			new_layer.polygons = base;
        }
        // Insert the base layers.
        for (size_t i = 1; i < m_slicing_params.base_raft_layers; ++ i) {
			coordf_t print_z = raft_layers.back()->print_z;
            MyLayer &new_layer  = layer_allocate(layer_storage, sltRaftBase);
            raft_layers.push_back(&new_layer);
			new_layer.print_z  = print_z + m_slicing_params.base_raft_layer_height;
            new_layer.height   = m_slicing_params.base_raft_layer_height;
			new_layer.bottom_z = print_z;
			new_layer.polygons = base;
		}
        // Insert the interface layers.
        for (size_t i = 1; i < m_slicing_params.interface_raft_layers; ++ i) {
			coordf_t print_z = raft_layers.back()->print_z;
			MyLayer &new_layer = layer_allocate(layer_storage, sltRaftInterface);
            raft_layers.push_back(&new_layer);
			new_layer.print_z = print_z + m_slicing_params.interface_raft_layer_height;
            new_layer.height  = m_slicing_params.interface_raft_layer_height;
			new_layer.bottom_z = print_z;
			new_layer.polygons = base;
		}
    }

    return raft_layers;
}

// Convert some of the intermediate layers into top/bottom interface layers.
PrintObjectSupportMaterial::MyLayersPtr PrintObjectSupportMaterial::generate_interface_layers(
    const PrintObject   &object,
    const MyLayersPtr   &bottom_contacts,
    const MyLayersPtr   &top_contacts,
    MyLayersPtr         &intermediate_layers,
    MyLayerStorage      &layer_storage) const
{
    // Old comment:
    // Compute interface area on this layer as diff of upper contact area
    // (or upper interface area) and layer slices.
    // This diff is responsible of the contact between support material and
    // the top surfaces of the object. We should probably offset the top 
    // surfaces vertically before performing the diff, but this needs 
    // investigation.

//    my $area_threshold = $self->interface_flow->scaled_spacing ** 2;

    MyLayersPtr interface_layers;
    // Contact layer is considered an interface layer, therefore run the following block only if support_material_interface_layers > 1.
    if (! intermediate_layers.empty() && m_object_config->support_material_interface_layers.value > 1) {
        // Index of the first top contact layer intersecting the current intermediate layer.
        size_t idx_top_contact_first = 0;
        // Index of the first bottom contact layer intersecting the current intermediate layer.
        size_t idx_bottom_contact_first = 0;
        // For all intermediate layers, collect top contact surfaces, which are not further than support_material_interface_layers.
        //FIXME this could be parallelized.
        for (size_t idx_intermediate_layer = 0; idx_intermediate_layer < intermediate_layers.size(); ++ idx_intermediate_layer) {
            MyLayer &intermediate_layer = *intermediate_layers[idx_intermediate_layer];
            // Top / bottom Z coordinate of a slab, over which we are collecting the top / bottom contact surfaces.
            coordf_t top_z    = intermediate_layers[std::min<int>(intermediate_layers.size()-1, idx_intermediate_layer + m_object_config->support_material_interface_layers - 1)]->print_z;
            coordf_t bottom_z = intermediate_layers[std::max<int>(0, int(idx_intermediate_layer) - int(m_object_config->support_material_interface_layers) + 1)]->bottom_z;
            // Move idx_top_contact_first up until above the current print_z.
            while (idx_top_contact_first < top_contacts.size() && top_contacts[idx_top_contact_first]->print_z < intermediate_layer.print_z)
                ++ idx_top_contact_first;
            // Collect the top contact areas above this intermediate layer, below top_z.
            Polygons polygons_top_contact_projected;
            for (size_t idx_top_contact = idx_top_contact_first; idx_top_contact < top_contacts.size(); ++ idx_top_contact) {
                const MyLayer &top_contact_layer = *top_contacts[idx_top_contact];
                if (top_contact_layer.bottom_z - EPSILON > top_z)
                    break;
                polygons_append(polygons_top_contact_projected, top_contact_layer.polygons);
            }
            // Move idx_bottom_contact_first up until touching bottom_z.
            while (idx_bottom_contact_first < bottom_contacts.size() && bottom_contacts[idx_bottom_contact_first]->print_z + EPSILON < bottom_z)
                ++ idx_bottom_contact_first;
            // Collect the top contact areas above this intermediate layer, below top_z.
            Polygons polygons_bottom_contact_projected;
            for (size_t idx_bottom_contact = idx_bottom_contact_first; idx_bottom_contact < bottom_contacts.size(); ++ idx_bottom_contact) {
                const MyLayer &bottom_contact_layer = *bottom_contacts[idx_bottom_contact];
                if (bottom_contact_layer.print_z - EPSILON > intermediate_layer.bottom_z)
                    break;
                polygons_append(polygons_bottom_contact_projected, bottom_contact_layer.polygons);
            }

            if (polygons_top_contact_projected.empty() && polygons_bottom_contact_projected.empty())
                continue;

            // Insert a new layer into top_interface_layers.
            MyLayer &layer_new = layer_allocate(layer_storage, 
                polygons_top_contact_projected.empty() ? sltBottomInterface : sltTopInterface);
            layer_new.print_z    = intermediate_layer.print_z;
            layer_new.bottom_z   = intermediate_layer.bottom_z;
            layer_new.height     = intermediate_layer.height;
            layer_new.bridging   = intermediate_layer.bridging;
            interface_layers.push_back(&layer_new);

            polygons_append(polygons_top_contact_projected, polygons_bottom_contact_projected);
            polygons_top_contact_projected = union_(polygons_top_contact_projected, true);
            layer_new.polygons = intersection(intermediate_layer.polygons, polygons_top_contact_projected);
            //FIXME filter layer_new.polygons islands by a minimum area?
//                $interface_area = [ grep abs($_->area) >= $area_threshold, @$interface_area ];
            intermediate_layer.polygons = diff(intermediate_layer.polygons, polygons_top_contact_projected, false);
        }
    }
    
    return interface_layers;
}

static inline void fill_expolygons_generate_paths(
    ExtrusionEntitiesPtr    &dst, 
    const ExPolygons        &expolygons,
    Fill                    *filler,
    float                    density,
    ExtrusionRole            role, 
    const Flow              &flow)
{
    FillParams fill_params;
    fill_params.density = density;
    fill_params.complete = true;
    fill_params.dont_adjust = true;
    for (ExPolygons::const_iterator it_expolygon = expolygons.begin(); it_expolygon != expolygons.end(); ++ it_expolygon) {
        Surface surface(stInternal, *it_expolygon);
        extrusion_entities_append_paths(
            dst,
            filler->fill_surface(&surface, fill_params),
            role, 
            flow.mm3_per_mm(), flow.width, flow.height);
    }
}

static inline void fill_expolygons_generate_paths(
    ExtrusionEntitiesPtr    &dst,
    ExPolygons             &&expolygons,
    Fill                    *filler,
    float                    density,
    ExtrusionRole            role,
    const Flow              &flow)
{
    FillParams fill_params;
    fill_params.density = density;
    fill_params.complete = true;
    fill_params.dont_adjust = true;
    for (ExPolygons::iterator it_expolygon = expolygons.begin(); it_expolygon != expolygons.end(); ++ it_expolygon) {
        Surface surface(stInternal, std::move(*it_expolygon));
        extrusion_entities_append_paths(
            dst,
            filler->fill_surface(&surface, fill_params),
            role,
            flow.mm3_per_mm(), flow.width, flow.height);
    }
}

// Support layers, partially processed.
struct MyLayerExtruded
{
    MyLayerExtruded() : layer(nullptr) {}

    bool empty() const {
        return layer == nullptr || layer->polygons.empty();
    }

    bool could_merge(const MyLayerExtruded &other) const {
        return ! this->empty() && ! other.empty() &&
            this->layer->height == other.layer->height &&
            this->layer->bridging == other.layer->bridging;
    }

    // Merge regions, perform boolean union over the merged polygons.
    void merge(MyLayerExtruded &&other) {
        assert(could_merge(other));
        Slic3r::polygons_append(layer->polygons, std::move(other.layer->polygons));
        layer->polygons = union_(layer->polygons, true);
        other.layer->polygons.clear();
    }

    void polygons_append(Polygons &dst) const {
        if (layer != NULL && ! layer->polygons.empty())
            Slic3r::polygons_append(dst, layer->polygons);
    }

    // The source layer. It carries the height and extrusion type (bridging / non bridging, extrusion height).
    PrintObjectSupportMaterial::MyLayer *layer;
    // Collect extrusions. They will be exported sorted by the bottom height.
    ExtrusionEntitiesPtr                 extrusions;
};

typedef std::vector<MyLayerExtruded*> MyLayerExtrudedPtrs;

struct LoopInterfaceProcessor
{
    LoopInterfaceProcessor(coordf_t circle_r) :
        n_contact_loops(0),
        circle_radius(circle_r),
        circle_distance(circle_r * 3.)
    {
        // Shape of the top contact area.
        circle.points.reserve(6);
        for (size_t i = 0; i < 6; ++ i) {
            double angle = double(i) * M_PI / 3.;
            circle.points.push_back(Point(circle_radius * cos(angle), circle_radius * sin(angle)));
        }
    }

    // Generate loop contacts at the top_contact_layer,
    // trim the top_contact_layer->polygons with the areas covered by the loops.
    void generate(MyLayerExtruded &top_contact_layer, const Flow &interface_flow_src);

    int         n_contact_loops;
    coordf_t    circle_radius;
    coordf_t    circle_distance;
    Polygon     circle;
};

void LoopInterfaceProcessor::generate(MyLayerExtruded &top_contact_layer, const Flow &interface_flow_src)
{
    if (n_contact_loops == 0 || top_contact_layer.empty())
        return;

    Flow flow = interface_flow_src;
    flow.height = float(top_contact_layer.layer->height);

    Polygons overhang_polygons;
    // if (top_contact_layer.layer->aux_polygons != nullptr)
        overhang_polygons = std::move(*top_contact_layer.layer->aux_polygons);

    // Generate the outermost loop.
    // Find centerline of the external loop (or any other kind of extrusions should the loop be skipped)
    Polygons top_contact_polygons = offset(top_contact_layer.layer->polygons, - 0.5f * flow.scaled_width());
    
    Polygons loops0;
    {
        // find centerline of the external loop of the contours
        // only consider the loops facing the overhang
        Polygons external_loops;
        // Positions of the loop centers.
        Polygons circles;
        Polygons overhang_with_margin = offset(overhang_polygons, 0.5f * flow.scaled_width());
        for (Polygons::const_iterator it_contact = top_contact_polygons.begin(); it_contact != top_contact_polygons.end(); ++ it_contact)
            if (! intersection_pl(it_contact->split_at_first_point(), overhang_with_margin).empty()) {
                external_loops.push_back(*it_contact);
                Points positions_new = it_contact->equally_spaced_points(circle_distance);
                for (Points::const_iterator it_center = positions_new.begin(); it_center != positions_new.end(); ++ it_center) {
                    circles.push_back(circle);
                    Polygon &circle_new = circles.back();
                    for (size_t i = 0; i < circle_new.points.size(); ++ i)
                        circle_new.points[i].translate(*it_center);
                }
            }
        // Apply a pattern to the loop.
        loops0 = diff(external_loops, circles);
    }

    Polylines loop_lines;
    {
        // make more loops
        Polygons loop_polygons = loops0;
        for (size_t i = 1; i < n_contact_loops; ++ i)
            polygons_append(loop_polygons, 
                offset2(
                    loops0, 
                    - int(i) * flow.scaled_spacing() - 0.5f * flow.scaled_spacing(), 
                    0.5f * flow.scaled_spacing()));
        // clip such loops to the side oriented towards the object
        loop_lines.reserve(loop_polygons.size());
        for (Polygons::const_iterator it = loop_polygons.begin(); it != loop_polygons.end(); ++ it)
            loop_lines.push_back(it->split_at_first_point());
        loop_lines = intersection_pl(loop_lines, offset(overhang_polygons, scale_(SUPPORT_MATERIAL_MARGIN)));
    }
    
    // add the contact infill area to the interface area
    // note that growing loops by $circle_radius ensures no tiny
    // extrusions are left inside the circles; however it creates
    // a very large gap between loops and contact_infill_polygons, so maybe another
    // solution should be found to achieve both goals
    top_contact_layer.layer->polygons = diff(top_contact_layer.layer->polygons, offset(loop_lines, float(circle_radius * 1.1)));

    // Transform loops into ExtrusionPath objects.
    extrusion_entities_append_paths(
        top_contact_layer.extrusions,
        STDMOVE(loop_lines),
        erSupportMaterialInterface, flow.mm3_per_mm(), flow.width, flow.height);
}

void PrintObjectSupportMaterial::generate_toolpaths(
    const PrintObject   &object,
    const MyLayersPtr   &raft_layers,
    const MyLayersPtr   &bottom_contacts,
    const MyLayersPtr   &top_contacts,
    const MyLayersPtr   &intermediate_layers,
    const MyLayersPtr   &interface_layers) const
{
//    Slic3r::debugf "Generating patterns\n";
    // loop_interface_processor with a given circle radius.
    LoopInterfaceProcessor loop_interface_processor(1.5 * m_support_material_interface_flow.scaled_width());

    // Prepare fillers.
    SupportMaterialPattern  support_pattern = m_object_config->support_material_pattern;
    bool                    with_sheath     = m_object_config->support_material_with_sheath;
    InfillPattern           infill_pattern;
    std::vector<double>     angles;
    angles.push_back(m_object_config->support_material_angle);
    switch (support_pattern) {
    case smpRectilinearGrid:
        angles.push_back(angles[0] + 90.);
        // fall through
    case smpRectilinear:
        infill_pattern = ipRectilinear;
        break;
    case smpHoneycomb:
    case smpPillars:
        infill_pattern = ipHoneycomb;
        break;
    }
    std::unique_ptr<Fill> filler_interface = std::unique_ptr<Fill>(Fill::new_from_type(ipRectilinear));
    std::unique_ptr<Fill> filler_support   = std::unique_ptr<Fill>(Fill::new_from_type(infill_pattern));
    {
//        BoundingBox bbox_object = object.bounding_box();
        BoundingBox bbox_object(Point(-scale_(1.), -scale_(1.0)), Point(scale_(1.), scale_(1.)));
        filler_interface->set_bounding_box(bbox_object);
        filler_support->set_bounding_box(bbox_object);
    }

    coordf_t interface_angle    = m_object_config->support_material_angle + 90.;
    coordf_t interface_spacing  = m_object_config->support_material_interface_spacing.value + m_support_material_interface_flow.spacing();
    coordf_t interface_density  = std::min(1., m_support_material_interface_flow.spacing() / interface_spacing);
    coordf_t support_spacing    = m_object_config->support_material_spacing.value + m_support_material_flow.spacing();
    coordf_t support_density    = std::min(1., m_support_material_flow.spacing() / support_spacing);
    if (m_object_config->support_material_interface_layers.value == 0) {
        // No interface layers allowed, print everything with the base support pattern.
        interface_spacing = support_spacing;
        interface_density = support_density;
    }

    //FIXME Parallelize the support generator:
    /*
    Slic3r::parallelize(
        threads => $self->print_config->threads,
        items => [ 0 .. n_$object.support_layers} ],
        thread_cb => sub {
            my $q = shift;
            while (defined (my $layer_id = $q->dequeue)) {
                $process_layer->($layer_id);
            }
        },
        no_threads_cb => sub {
            $process_layer->($_) for 0 .. n_{$object.support_layers};
        },
    );
    */
    // Insert the raft base layers.
    size_t support_layer_id = 0;
    for (; support_layer_id < size_t(std::max(0, int(m_slicing_params.raft_layers()) - 1)); ++ support_layer_id) {
        assert(support_layer_id < raft_layers.size());
        SupportLayer &support_layer = *object.support_layers[support_layer_id];
        assert(support_layer.support_fills.entities.empty());
        assert(support_layer.support_interface_fills.entities.empty());
        assert(support_layer.support_islands.expolygons.empty());
        MyLayer      &raft_layer    = *raft_layers[support_layer_id];
        //FIXME When paralellizing, each thread shall have its own copy of the fillers.
        Fill *filler = filler_support.get();
        filler->angle = 0.;
        // We don't use $base_flow->spacing because we need a constant spacing
        // value that guarantees that all layers are correctly aligned.
        Flow flow(m_support_material_flow.width, raft_layer.height, m_support_material_flow.nozzle_diameter, raft_layer.bridging);
        filler->spacing = m_support_material_flow.spacing();
        float density = support_density;
        // find centerline of the external loop/extrusions
        ExPolygons to_infill = (support_layer_id == 0 || ! with_sheath) ?
            // union_ex(base_polygons, true) :
            offset2_ex(raft_layer.polygons, SCALED_EPSILON, - SCALED_EPSILON) :
            offset2_ex(raft_layer.polygons, SCALED_EPSILON, - SCALED_EPSILON - 0.5*flow.scaled_width());
        if (support_layer_id == 0) {
            // Base flange.
            filler = filler_interface.get();
            filler->angle = m_object_config->support_material_angle + 90.;
            density = 0.5f;
            flow = m_first_layer_flow;
            // use the proper spacing for first layer as we don't need to align
            // its pattern to the other layers
            //FIXME When paralellizing, each thread shall have its own copy of the fillers.
            filler->spacing = flow.spacing();
        } else if (with_sheath) {
            // Draw a perimeter all around the support infill. This makes the support stable, but difficult to remove.
            // TODO: use brim ordering algorithm
            Polygons to_infill_polygons = to_polygons(to_infill);
            // TODO: use offset2_ex()
            to_infill = offset_ex(to_infill, - flow.scaled_spacing());
            extrusion_entities_append_paths(
                support_layer.support_fills.entities, 
                to_polylines(STDMOVE(to_infill_polygons)),
                erSupportMaterial, flow.mm3_per_mm(), flow.width, flow.height);
        }
        fill_expolygons_generate_paths(
            // Destination
            support_layer.support_fills.entities, 
            // Regions to fill
            STDMOVE(to_infill), 
            // Filler and its parameters
            filler, density,
            // Extrusion parameters
            erSupportMaterial, flow);

    }

    // Indices of the 1st layer in their respective container at the support layer height.
    size_t idx_layer_bottom_contact   = 0;
    size_t idx_layer_top_contact      = 0;
    size_t idx_layer_intermediate     = 0;
    size_t idx_layer_inteface         = 0;
    for (; support_layer_id < object.support_layers.size(); ++ support_layer_id) 
    {
        SupportLayer &support_layer = *object.support_layers[support_layer_id];

        // Find polygons with the same print_z.
        MyLayerExtruded bottom_contact_layer;
        MyLayerExtruded top_contact_layer;
        MyLayerExtruded base_layer;
        MyLayerExtruded interface_layer;
        MyLayerExtrudedPtrs mylayers;

        // Increment the layer indices to find a layer at support_layer.print_z.
        for (; idx_layer_bottom_contact < bottom_contacts    .size() && bottom_contacts    [idx_layer_bottom_contact]->print_z < support_layer.print_z - EPSILON; ++ idx_layer_bottom_contact) ;
        for (; idx_layer_top_contact    < top_contacts       .size() && top_contacts       [idx_layer_top_contact   ]->print_z < support_layer.print_z - EPSILON; ++ idx_layer_top_contact   ) ;
        for (; idx_layer_intermediate   < intermediate_layers.size() && intermediate_layers[idx_layer_intermediate  ]->print_z < support_layer.print_z - EPSILON; ++ idx_layer_intermediate  ) ;
        for (; idx_layer_inteface       < interface_layers   .size() && interface_layers   [idx_layer_inteface      ]->print_z < support_layer.print_z - EPSILON; ++ idx_layer_inteface      ) ;
        // Copy polygons from the layers.
        mylayers.reserve(4);
        if (idx_layer_bottom_contact < bottom_contacts.size() && bottom_contacts[idx_layer_bottom_contact]->print_z < support_layer.print_z + EPSILON) {
            bottom_contact_layer.layer = bottom_contacts[idx_layer_bottom_contact];
            mylayers.push_back(&bottom_contact_layer);
        }
        if (idx_layer_top_contact < top_contacts.size() && top_contacts[idx_layer_top_contact]->print_z < support_layer.print_z + EPSILON) {
            top_contact_layer.layer = top_contacts[idx_layer_top_contact];
            mylayers.push_back(&top_contact_layer);
        }
        if (idx_layer_inteface < interface_layers.size() && interface_layers[idx_layer_inteface]->print_z < support_layer.print_z + EPSILON) {
            interface_layer.layer = interface_layers[idx_layer_inteface];
            mylayers.push_back(&interface_layer);
        }
        if (idx_layer_intermediate < intermediate_layers.size() && intermediate_layers[idx_layer_intermediate]->print_z < support_layer.print_z + EPSILON) {
            base_layer.layer = intermediate_layers[idx_layer_intermediate];
            mylayers.push_back(&base_layer);
        }
        // Sort the layers with the same print_z coordinate by their heights, thickest first.
        std::sort(mylayers.begin(), mylayers.end(), [](const MyLayerExtruded *p1, const MyLayerExtruded *p2) { return p1->layer->height > p2->layer->height; });

        /* {
            require "Slic3r/SVG.pm";
            Slic3r::SVG::output("out\\layer_" . $z . ".svg",
                blue_expolygons     => union_ex($base),
                red_expolygons      => union_ex($contact),
                green_expolygons    => union_ex($interface),
            );
        } */
        
        if (m_object_config->support_material_interface_layers == 0) {
            // If no interface layers were requested, we treat the contact layer exactly as a generic base layer.
			if (base_layer.could_merge(top_contact_layer)) 
				base_layer.merge(std::move(top_contact_layer));
			else if (base_layer.empty() && !top_contact_layer.empty() && !top_contact_layer.layer->bridging)
				std::swap(base_layer, top_contact_layer);
            if (base_layer.could_merge(bottom_contact_layer))
                base_layer.merge(std::move(bottom_contact_layer));
            else if (base_layer.empty() && !bottom_contact_layer.empty() && !bottom_contact_layer.layer->bridging)
                std::swap(base_layer, bottom_contact_layer);
        } else {
            loop_interface_processor.generate(top_contact_layer, m_support_material_interface_flow);
            // If no loops are allowed, we treat the contact layer exactly as a generic interface layer.
            if (interface_layer.could_merge(top_contact_layer))
                interface_layer.merge(std::move(top_contact_layer));
        } 

        if (! interface_layer.empty() && ! base_layer.empty()) {
            // turn base support into interface when it's contained in our holes
            // (this way we get wider interface anchoring)
            //FIXME one wants to fill in the inner most holes of the interfaces, not all the holes.
            Polygons islands = top_level_islands(interface_layer.layer->polygons);
            polygons_append(interface_layer.layer->polygons, intersection(base_layer.layer->polygons, islands));
            base_layer.layer->polygons = diff(base_layer.layer->polygons, islands);
        }

		// interface and contact infill
		if (! top_contact_layer.empty()) {
			//FIXME When paralellizing, each thread shall have its own copy of the fillers.
			Flow interface_flow(
				top_contact_layer.layer->bridging ? top_contact_layer.layer->height : m_support_material_interface_flow.width,
				top_contact_layer.layer->height,
				m_support_material_interface_flow.nozzle_diameter,
				top_contact_layer.layer->bridging);
			filler_interface->angle = interface_angle;
			filler_interface->spacing = m_support_material_interface_flow.spacing();
			fill_expolygons_generate_paths(
				// Destination
				support_layer.support_fills.entities,
				// Regions to fill
				union_ex(top_contact_layer.layer->polygons, true),
				// Filler and its parameters
				filler_interface.get(), interface_density,
				// Extrusion parameters
				erSupportMaterialInterface, interface_flow);
		}

        // interface and contact infill
        if (! interface_layer.empty()) {
            //FIXME When paralellizing, each thread shall have its own copy of the fillers.
            Flow interface_flow(
                interface_layer.layer->bridging ? interface_layer.layer->height : m_support_material_interface_flow.width, 
                interface_layer.layer->height,
                m_support_material_interface_flow.nozzle_diameter,
                interface_layer.layer->bridging);
            filler_interface->angle   = interface_angle;
            filler_interface->spacing = m_support_material_interface_flow.spacing();
            fill_expolygons_generate_paths(
                // Destination
                support_layer.support_fills.entities, 
                // Regions to fill
                union_ex(interface_layer.layer->polygons, true), 
                // Filler and its parameters
                filler_interface.get(), interface_density,
                // Extrusion parameters
                erSupportMaterialInterface, interface_flow);
        }

        // support or flange
        if (! base_layer.empty()) {
            //FIXME When paralellizing, each thread shall have its own copy of the fillers.
            Fill *filler = filler_support.get();
            filler->angle = angles[support_layer_id % angles.size()];
            // We don't use $base_flow->spacing because we need a constant spacing
            // value that guarantees that all layers are correctly aligned.
            Flow flow(m_support_material_flow.width, base_layer.layer->height, m_support_material_flow.nozzle_diameter, base_layer.layer->bridging);
            filler->spacing = m_support_material_flow.spacing();
            float density = support_density;
            // find centerline of the external loop/extrusions
            ExPolygons to_infill = (support_layer_id == 0 || ! with_sheath) ?
                // union_ex(base_polygons, true) :
                offset2_ex(base_layer.layer->polygons, SCALED_EPSILON, - SCALED_EPSILON) :
                offset2_ex(base_layer.layer->polygons, SCALED_EPSILON, - SCALED_EPSILON - 0.5*flow.scaled_width());
            /* {
                require "Slic3r/SVG.pm";
                Slic3r::SVG::output("out\\to_infill_base" . $z . ".svg",
                    red_expolygons      => union_ex($contact),
                    green_expolygons    => union_ex($interface),
                    blue_expolygons     => $to_infill,
                );
            } */
            if (base_layer.layer->bottom_z < EPSILON) {
                // Base flange.
                filler = filler_interface.get();
                filler->angle = m_object_config->support_material_angle + 90.;
                density = 0.5f;
                flow = m_first_layer_flow;
                // use the proper spacing for first layer as we don't need to align
                // its pattern to the other layers
                //FIXME When paralellizing, each thread shall have its own copy of the fillers.
                filler->spacing = flow.spacing();
            } else if (with_sheath) {
                // Draw a perimeter all around the support infill. This makes the support stable, but difficult to remove.
                // TODO: use brim ordering algorithm
                Polygons to_infill_polygons = to_polygons(to_infill);
                // TODO: use offset2_ex()
                to_infill = offset_ex(to_infill, - flow.scaled_spacing());
                extrusion_entities_append_paths(
                    support_layer.support_fills.entities, 
                    to_polylines(STDMOVE(to_infill_polygons)),
                    erSupportMaterial, flow.mm3_per_mm(), flow.width, flow.height);
            }
            fill_expolygons_generate_paths(
                // Destination
                support_layer.support_fills.entities, 
                // Regions to fill
                STDMOVE(to_infill), 
                // Filler and its parameters
                filler, density,
                // Extrusion parameters
                erSupportMaterial, flow);
        }

        // support or flange
        if (! bottom_contact_layer.empty()) {
            //FIXME When paralellizing, each thread shall have its own copy of the fillers.
            Flow interface_flow(
                bottom_contact_layer.layer->bridging ? bottom_contact_layer.layer->height : m_support_material_interface_flow.width, 
                bottom_contact_layer.layer->height,
                m_support_material_interface_flow.nozzle_diameter,
                bottom_contact_layer.layer->bridging);
            filler_interface->angle = (m_object_config->support_material_interface_layers.value == 0) ?
                    // If zero interface layers are configured, use the same angle as for the base layers.
                    angles[support_layer_id % angles.size()] :
                    // Use interface angle for the interface layers.
                    interface_angle;
            filler_interface->spacing = m_support_material_interface_flow.spacing();
            fill_expolygons_generate_paths(
                // Destination
                support_layer.support_fills.entities, 
                // Regions to fill
                union_ex(bottom_contact_layer.layer->polygons, true), 
                // Filler and its parameters
                filler_interface.get(), interface_density,
                // Extrusion parameters
                erSupportMaterial, interface_flow);
        }

        // Collect the support areas with this print_z into islands, as there is no need
        // for retraction over these islands.
        Polygons polys;
        // Collect the extrusions, sorted by the bottom extrusion height.
        for (MyLayerExtrudedPtrs::iterator it = mylayers.begin(); it != mylayers.end(); ++ it) {
            (*it)->polygons_append(polys);
            std::move(std::begin((*it)->extrusions), std::end((*it)->extrusions), 
                std::back_inserter(support_layer.support_fills.entities));
        }
        if (! polys.empty())
            expolygons_append(support_layer.support_islands.expolygons, union_ex(polys));
        /* {
            require "Slic3r/SVG.pm";
            Slic3r::SVG::output("islands_" . $z . ".svg",
                red_expolygons      => union_ex($contact),
                green_expolygons    => union_ex($interface),
                green_polylines     => [ map $_->unpack->polyline, @{$layer->support_contact_fills} ],
                polylines           => [ map $_->unpack->polyline, @{$layer->support_fills} ],
            );
        } */
    } // for each support_layer_id
}

/*
void PrintObjectSupportMaterial::clip_by_pillars(
    const PrintObject   &object,
    LayersPtr           &bottom_contacts,
    LayersPtr           &top_contacts,
    LayersPtr           &intermediate_contacts);

{
    // this prevents supplying an empty point set to BoundingBox constructor
    if (top_contacts.empty())
        return;

    coord_t pillar_size    = scale_(PILLAR_SIZE);
    coord_t pillar_spacing = scale_(PILLAR_SPACING);
    
    // A regular grid of pillars, filling the 2D bounding box.
    Polygons grid;
    {
        // Rectangle with a side of 2.5x2.5mm.
        Polygon pillar;
        pillar.points.push_back(Point(0, 0));
        pillar.points.push_back(Point(pillar_size, 0));
        pillar.points.push_back(Point(pillar_size, pillar_size));
        pillar.points.push_back(Point(0, pillar_size));
        
        // 2D bounding box of the projection of all contact polygons.
        BoundingBox bbox;
        for (LayersPtr::const_iterator it = top_contacts.begin(); it != top_contacts.end(); ++ it)
            bbox.merge(get_extents((*it)->polygons));
        grid.reserve(size_t(ceil(bb.size().x / pillar_spacing)) * size_t(ceil(bb.size().y / pillar_spacing)));
        for (coord_t x = bb.min.x; x <= bb.max.x - pillar_size; x += pillar_spacing) {
            for (coord_t y = bb.min.y; y <= bb.max.y - pillar_size; y += pillar_spacing) {
                grid.push_back(pillar);
                for (size_t i = 0; i < pillar.points.size(); ++ i)
                    grid.back().points[i].translate(Point(x, y));
            }
        }
    }
    
    // add pillars to every layer
    for my $i (0..n_support_z) {
        $shape->[$i] = [ @$grid ];
    }
    
    // build capitals
    for my $i (0..n_support_z) {
        my $z = $support_z->[$i];
        
        my $capitals = intersection(
            $grid,
            $contact->{$z} // [],
        );
        
        // work on one pillar at time (if any) to prevent the capitals from being merged
        // but store the contact area supported by the capital because we need to make 
        // sure nothing is left
        my $contact_supported_by_capitals = [];
        foreach my $capital (@$capitals) {
            // enlarge capital tops
            $capital = offset([$capital], +($pillar_spacing - $pillar_size)/2);
            push @$contact_supported_by_capitals, @$capital;
            
            for (my $j = $i-1; $j >= 0; $j--) {
                my $jz = $support_z->[$j];
                $capital = offset($capital, -$self->interface_flow->scaled_width/2);
                last if !@$capitals;
                push @{ $shape->[$j] }, @$capital;
            }
        }
        
        // Capitals will not generally cover the whole contact area because there will be
        // remainders. For now we handle this situation by projecting such unsupported
        // areas to the ground, just like we would do with a normal support.
        my $contact_not_supported_by_capitals = diff(
            $contact->{$z} // [],
            $contact_supported_by_capitals,
        );
        if (@$contact_not_supported_by_capitals) {
            for (my $j = $i-1; $j >= 0; $j--) {
                push @{ $shape->[$j] }, @$contact_not_supported_by_capitals;
            }
        }
    }
}

sub clip_with_shape {
    my ($self, $support, $shape) = @_;
    
    foreach my $i (keys %$support) {
        // don't clip bottom layer with shape so that we 
        // can generate a continuous base flange
        // also don't clip raft layers
        next if $i == 0;
        next if $i < $self->object_config->raft_layers;
        $support->{$i} = intersection(
            $support->{$i},
            $shape->[$i],
        );
    }
}
*/

} // namespace Slic3r
