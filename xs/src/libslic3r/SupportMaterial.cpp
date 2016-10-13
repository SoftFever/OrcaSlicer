#include "PerimeterGenerator.hpp"
#include "ClipperUtils.hpp"
#include "ExtrusionEntityCollection.hpp"
#include <cmath>
#include <cassert>

namespace Slic3r {

// increment used to reach MARGIN in steps to avoid trespassing thin objects
#define MARGIN_STEPS 3
// generate a tree-like structure to save material
#define PILLAR_SIZE (2.5)
#define PILLAR_SPACING 10

inline Layer& layer_allocate(std::deque<Layer> &layer_storage)
{ 
    m_layer_storage.push_back(Layer());
    return m_layer.back();
}

void PrintSupportMaterial::generate(PrintObject *object)
{
    coordf_t max_object_layer_height = 0.;
    for (size_t i = 0; i < object->layer_count(); ++ i)
        max_object_layer_height = std::max(max_object_layer_height, object->get_layer(i)->height);

    if (m_support_layer_height_max == 0)
        m_support_layer_height_max = std::max(max_object_layer_height, 0.75 * m_flow.nozzle_diameter);
    if (m_support_interface_layer_height_max == 0)
        m_support_interface_layer_height_max = std::max(max_object_layer_height, 0.75 * m_interface_flow.nozzle_diameter);

    // Layer instances will be allocated by std::deque and they will be kept until the end of this function call.
    // The layers will be referenced by various LayersPtr (std::vector<Layer*>)
    std::deque<Layer> layer_storage;

    // Determine the top surfaces of the support, defined as:
    // contact = overhangs - clearance + margin
    // This method is responsible for identifying what contact surfaces
    // should the support material expose to the object in order to guarantee
    // that it will be effective, regardless of how it's built below.
    LayersSet top_contacts = this->top_contact_layers(object, layer_storage);
    if (top_contacts.empty())
        return;

    // Determine the top surfaces of the object. We need these to determine 
    // the layer heights of support material and to clip support to the object
    // silhouette.
    LayersSet bottom_contacts = this->bottom_contact_layers(object, top_contacts, layer_storage);

    this->trim_top_contacts_by_bottom_contacts(object, bottom_contacts, top_contacts);

    // We now know the upper and lower boundaries for our support material object
    // (@$contact_z and @$top_z), so we can generate intermediate layers.
    LayersSet support_layers = this->support_layers(object, bottom_contacts, top_contacts, layer_storage, max_object_layer_height);

    // If we wanted to apply some special logic to the first support layers lying on
    // object's top surfaces this is the place to detect them
    LayersSet shape;
    if (this->object_config->support_material_pattern.value == smpPillars)
        shape = this->generate_pillars_shape(contact, support_z);

    // Propagate contact layers downwards to generate interface layers
    LayersSet interface = this->generate_top_interface_layers(support_z, contact, top);
    this->clip_with_object(interface, support_z, object);
    if (! shape.empty())
        this->clip_with_shape(interface, shape);

    // Propagate contact layers and interface layers downwards to generate
    // the main support layers.
    LayersSet base = this->generate_base_layers(object, bottom_contacts, top_contacts, intermediate_layers, layer_storage);
    if (! shape.empty())
        this->clip_with_shape(base, shape);

    // Install support layers into object.
    for (size_t i = 0; i < support_z.size(); ++ i) {
        object->add_support_layer(
            i, // id
            (i == 0) ? support_z[i] : (support_z[i] - support_z[i-1]), // height
            support_z[i] // print_z
        );
        if (i > 0) {
            SupportLayer *sl1 = object->get_support_layer[object->support_layer_count()-2];
            SupportLayer *sl2 = object->get_support_layer[object->support_layer_count()-1];
            sl1->set_upper_layer(sl2);
            sl2->set_lower_layer(sl1);
        }
    }
    
    // Generate the actual toolpaths and save them into each layer.
    this->generate_toolpaths(object, overhang, contact, interface, base);
}

void collect_region_slices_by_type(const Layer &layer, SurfaceType surface_type, Polygons &out)
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
    out.reserve(out.size() + n_polygons_new);
    for (LayerRegionPtrs::const_iterator it_region = layer.regions.begin(); it_region != layer.regions.end(); ++ it_region) {
        const LayerRegion       &region = *(*it_region);
        const SurfaceCollection &slices = region.slices;
        for (Surfaces::const_iterator it = slices.surfaces.begin(); it != slices.surfaces.end(); ++ it) {
            const Surface &surface = *it;
            if (surface.surface_type == surface_type) {
                out.push_back(surface.expolygon.contour);
                out.insert(out.end(), surface.expolygon.holes.begin(), surface.expolygon.holes.end());
            }
        }
    }
}

Polygons collect_region_slices_by_type(const Layer &layer, SurfaceType surface_type)
{
    Polygons out;
    collect_regon_slices_by_type(layer, surface_type, out);
    return out;
}

void collect_region_slices_all(const Layer &layer, Polygons &out)
{
    // 1) Count the new polygons first.
    size_t n_polygons_new = 0;
    for (LayerRegionPtrs::const_iterator it_region = layer.regions.begin(); it_region != layer.regions.end(); ++ it_region) {
        const LayerRegion       &region = *(*it_region);
        const SurfaceCollection &slices = region.slices;
        for (Surfaces::const_iterator it = slices.surfaces.begin(); it != slices.surfaces.end(); ++ it)
            n_polygons_new += it->expolygon.holes.size() + 1;
    }

    // 2) Collect the new polygons.
    out.reserve(out.size() + n_polygons_new);
    for (LayerRegionPtrs::const_iterator it_region = layer.regions.begin(); it_region != layer.regions.end(); ++ it_region) {
        const LayerRegion       &region = *(*it_region);
        const SurfaceCollection &slices = region.slices;
        for (Surfaces::const_iterator it = slices.surfaces.begin(); it != slices.surfaces.end(); ++ it) {
            const Surface &surface = *it;
            out.push_back(surface.expolygon.contour);
            out.insert(out.end(), surface.expolygon.holes.begin(), surface.expolygon.holes.end());
        }
    }
}

Polygons collect_region_slices_all(const Layer &layer)
{
    Polygons out;
    collect_region_slices_all(layer, out);
    return out;
}

// Collect outer contours of all expolygons in all layer region slices.
void collect_region_slices_outer(const Layer &layer, Polygons &out)
{
    // 1) Count the new polygons first.
    size_t n_polygons_new = 0;
    for (LayerRegionPtrs::const_iterator it_region = layer.regions.begin(); it_region != layer.regions.end(); ++ it_region) {
        const LayerRegion &region = *(*it_region);
        n_polygons_new += region.slices.surfaces.size();
    }

    // 2) Collect the new polygons.
    out.reserve(out.size() + n_polygons_new);
    for (LayerRegionPtrs::const_iterator it_region = layer.regions.begin(); it_region != layer.regions.end(); ++ it_region) {
        const LayerRegion &region = *(*it_region);
        for (Surfaces::const_iterator it = region.slices.surfaces.begin(); it != region.slices.surfaces.end(); ++ it)
            out.push_back(it->expolygon.contour);
    }
}

// Collect outer contours of all expolygons in all layer region slices.
Polygons collect_region_slices_outer(const Layer &layer)
{
    Polygons out;
    collect_region_slices_outer(layer, out);
    return out;
}

void collect_layer_slices_all(const Layer &layer, Polygons &out)
{
    // 1) Count the new polygons first.
    size_t n_polygons_new = 0;
    const SurfaceCollection &slices = region.slices;
    for (Surfaces::const_iterator it = slices.surfaces.begin(); it != slices.surfaces.end(); ++ it)
        n_polygons_new += it->expolygon.holes.size() + 1;

    // 2) Collect the new polygons.
    for (Surfaces::const_iterator it = slices.surfaces.begin(); it != slices.surfaces.end(); ++ it) {
        const Surface &surface = *it;
        out.push_back(surface.expolygon.contour);
        out.insert(out.end(), surface.expolygon.holes.begin(), surface.expolygon.holes.end());
    }
}

Polygons collect_layer_slices_all(const Layer &layer)
{
    Polygons out;
    collect_layer_slices_all(layer, out);
    return out;
}

PrintSupportMaterial::LayersPtr PrintSupportMaterial::top_contact_layers(const PrintObject &object, std::deque<Layer> &layer_storage) const
{
    // Output layers, sorte by top Z.
    LayersPtr contact_out;

    // if user specified a custom angle threshold, convert it to radians
    double threshold_rad = 0.;
    if (this->object_config->support_material_threshold > 0) {
        threshold_rad = M_PI * double(this->object_config->support_material_threshold + 1) / 180.; // +1 makes the threshold inclusive
        // Slic3r::debugf "Threshold angle = %d°\n", rad2deg($threshold_rad);
    }
    
    // Build support on a build plate only? If so, then collect top surfaces into $buildplate_only_top_surfaces
    // and subtract $buildplate_only_top_surfaces from the contact surfaces, so
    // there is no contact surface supported by a top surface.
    bool buildplate_only = this->object_config->support_material && this->object_config->support_material_buildplate_only;
    Polygons buildplate_only_top_surfaces;

/*
    Layer *first_object_layer = object.get_layer(this->object_config->raft_layers);
    if (first_object_layer == NULL)
        // Nothing to print, nothing to support.
        return contact_out;

    // If printing over raft, make sure the bottom of the contact surfaces are above the raft.
    coordf_t raft_top = (this->object_config->raft_layers > 0) ? (first_object_layer->print_z - first_object_layer->height) : 0.;
*/

    // determine contact areas
    for (size_t layer_id = 0; layer_id < object->layer_count(); ++ layer_id) {
        // Note that layer_id < layer->id when raft_layers > 0 as the layer->id incorporates the raft layers.
        // So layer_id == 0 means first object layer and layer->id == 0 means first print layer if there are no explicit raft layers.
        if (m_object_config->raft_layers == 0) {
            if (layer_id == 0)
                // No raft, 1st object layer cannot be supported by a support contact layer.
                continue;
        } else if (! this->object_config->support_material) {
            // If we are only going to generate raft. Just check the 'overhangs' of the first object layer.
            if (layer_id > 0)
                break;
        }

        const Layer &layer = *object->get_layer(layer_id);

        if (buildplate_only) {
            // Collect the top surfaces up to this layer and merge them.
            Polygons projection_new = collect_region_slices_by_type(layer, stTop);
            if (! projection_new.empty()) {
                // Merge the new top surfaces with the preceding top surfaces.
                // Apply the safety offset to the newly added polygons, so they will connect
                // with the polygons collected before,
                // but don't apply the safety offset during the union operation as it would
                // inflate the polygons over and over.
                projection_new = offset(projection_new, scale_(0.01));
                buildplate_only_top_surfaces.insert(buildplate_only_top_surfaces.end(), projection_new.begin(), projection_new.end());
                buildplate_only_top_surfaces = union_(buildplate_only_top_surfaces, false);
            }
        }

        // detect overhangs and contact areas needed to support them
        Polygons overhang_polygons;
        Polygons contact_polygons;
        if (layer_id == 0) {
            // this is the first object layer, so we're here just to get the object
            // footprint for the raft
            // we only consider contours and discard holes to get a more continuous raft
            overhang_polygons = collect_region_slices_outer(layer);
            // Extend by SUPPORT_MATERIAL_MARGIN, which is 1.5mm
            contact_polygons = offset_(overhang_polygons, scale_(SUPPORT_MATERIAL_MARGIN));
        } else {
            const Layer &lower_layer = *object->get_layer(layer_id-1);
            for (LayerRegionPtrs::const_iterator it_layerm = layer.regions.begin(); it_layerm != layer.regions.end(); ++ it_layerm) {
                const LayerRegion &layerm = *(*it_layerm);
                // Extrusion width accounts for the roundings of the extrudates.
                // It is the maximum widh of the extrudate.
                coord_t fw = layerm.flow(FLOW_ROLE_EXTERNAL_PERIMETER).scaled_width;
                Polygons diff_polygons;
            
                // If a threshold angle was specified, use a different logic for detecting overhangs.
                if (threshold_rad > 0.
                    || layer_id < this->object_config->support_material_enforce_layers
                    || (this->object_config->raft_layers > 0 && layer_id == 0)) {
                    coordf_t d = (threshold_rad > 0.)
                        ? scale_(lower_layer.height * cos(threshold_rad) / sin(threshold_rad))
                        : 0.;
                
                    // Shrinking the supported layer by layer_height/atan(threshold_rad).
                    diff_polygons = diff(
                        offset_((Polygons)layerm.slices, -d),
                        (Polygons)lower_layer->slices);
                
                    // only enforce spacing from the object ($fw/2) if the threshold angle
                    // is not too high: in that case, $d will be very small (as we need to catch
                    // very short overhangs), and such contact area would be eaten by the
                    // enforced spacing, resulting in high threshold angles to be almost ignored
                    if (d > 0.5*fw)
                        diff_polygons = diff(
                            offset(diff_polygons, d - 0.5*fw),
                            (Polygons)lower_layer.slices);
                } else {
                    // Automatic overhang detection.
                    diff_polygons = diff(
                        (Polygons)layerm.slices,
                        offset((Polygons)lower_layer.slices, 0.5*fw));
                    
                    // collapse very tiny spots
                    diff_polygons = offset2(diff_polygons, -0.1*fw, +0.1*fw);

                    // $diff now contains the ring or stripe comprised between the boundary of 
                    // lower slices and the centerline of the last perimeter in this overhanging layer.
                    // Void $diff means that there's no upper perimeter whose centerline is
                    // outside the lower slice boundary, thus no overhang
                }
                
                if (this->object_config->dont_support_bridges) {
                    // compute the area of bridging perimeters
                    // Note: this is duplicate code from GCode.pm, we need to refactor
                    
                    Polygons bridged_perimeters;
                    {
                        Flow bridge_flow = layerm.flow(FLOW_ROLE_PERIMETER, true);
                        
                        coordf_t nozzle_diameter = this->print_config->get_at('nozzle_diameter', layerm.region->config->perimeter_extruder-1);
                        Polygons lower_grown_slices = offset((Polygons)lower_layer.slices, +scale_(0.5*nozzle_diameter));
                        
                        // TODO: split_at_first_point() could split a bridge mid-way
                        Polylines overhang_perimeters;
                        for (size_t i = 0; i < layerm.perimeters.entities.size(); ++ i) {
                            ExtrusionEntity *entity = layerm.perimeters.entities[i];
                            ExtrusionLoop *loop = dynamic_cast<const Slic3r::ExtrusionLoop*>(entity);
                            overhang_perimeters.push_back(loop ? 
                                loop->polygon->split_at_first_point() :
                                dynamic_cast<const Slic3r::ExtrusionPath*>(entity)->path);
                        }
                        
                        // workaround for Clipper bug, see Slic3r::Polygon::clip_as_polyline()
                        for (Polylines::iterator it = overhang_perimeters.begin(); it != overhang_perimeters.end(); ++ it)
                            (*it)[0].x += 1;
                        overhang_perimeters = diff(overhang_perimeters, lower_grown_slices);
                        
                        // only consider straight overhangs
                        // only consider overhangs having endpoints inside layer's slices
                        // convert bridging polylines into polygons by inflating them with their thickness
                        // since we're dealing with bridges, we can't assume width is larger than spacing,
                        // so we take the largest value and also apply safety offset to be ensure no gaps
                        // are left in between
                        coordf_t w = std::max(bridge_flow.scaled_width, bridge_flow.scaled_spacing);
                        for (Polylines::iterator it = overhang_perimeters.begin(); it != overhang_perimeters.end(); ++ it) {
                            if (it->is_straight()) {
                                it->extend_start($fw);
                                it->extend_end($fw);
                                if (layer.slices.contains(it->find_point()) && layer.slices.contains_point(it->last_point()))
                                    // Offset a polyline into a polygon.
                                    bridged_perimeters.push_back(offset(*it, 0.5 * w + 10.));
                            }
                        }
                        bridged_perimeters = union(bridged_perimeters);
                    }
                    
                    if (1) {
                        // remove the entire bridges and only support the unsupported edges
                        Polygons bridges;
                        for (Surfaces::const_iterator it = layerm.fill_surfaces.surfaces.begin(); it != layerm.fill_surfaces.surfaces.end(); ++ it) {
                            if (it->surface_type == stBottomBridge && it->bridge_angle != -1) {
                                bridges.push_back(it->expolygon.contour);
                                bridges.insert(bridges.end(), it->expolygon.holes.begin(), it->expolygon.holes.end());
                            }
                            
                        bridged_perimeters.insert(bridged_perimeters.end(), bridges.begin(), bridges.end());
                        diff_polygons = diff(diff_polygons, bridged_perimeters, true);
                        
                        Polygons bridge_anchors = intersection(offset(polylines, scale_(SUPPORT_MATERIAL_MARGIN)), bridges);
                        diff_polygons.insert(diff_polygons.end(), bridge_anchors.begin(), bridge_anchors.end());
                    } else {
                        // just remove bridged areas
                        diff_polygons = diff(diff_polygons, layerm.bridged, true);
                    }
                } // if (this->object_config->dont_support_bridges)

                if (buildplate_only) {
                    // Don't support overhangs above the top surfaces.
                    // This step is done before the contact surface is calcuated by growing the overhang region.
                    diff_polygons = diff(diff_polygons, buildplate_only_top_surfaces);
                }

                if (diff_polygons.empty())
                    continue;
                // NOTE: this is not the full overhang as it misses the outermost half of the perimeter width!
                //FIXME Vojtech: Is the preceding comment true?
                overhang.polygons.insert(overhang.polygons.end(), diff_polygons.begin(), diff_polygons.end());

                // Let's define the required contact area by using a max gap of half the upper 
                // extrusion width and extending the area according to the configured margin.
                // We increment the area in steps because we don't want our support to overflow
                // on the other side of the object (if it's very thin).
                {
                    Polygons slices_margin = offset((Polygons)lower_layer.slices, 0.5*fw);
                    if (buildplate_only) {
                        // Trim the inflated contact surfaces by the top surfaces as well.
                        slices_margin.insert(slices_margin.end(), buildplate_only_top_surfaces);
                        slices_margin = union_(slices_margin);
                    }
                    for (size_t i = 0; i <= MARGIN_STEPS; ++ i) {
                        diff_polygons = diff(
                            offset(
                                diff_polygons, 
                                (i == 0) ? (0.5 * fw) : (SUPPORT_MATERIAL_MARGIN / MARGIN_STEPS),
                                CLIPPER_OFFSET_SCALE,
                                ClipperLib::jtRound,
                                scale_(0.05) * CLIPPER_OFFSET_SCALE),
                            slices_margin);
                    }
                }
                contact.polygons.insert(contact.polygons.end(), diff_polygons.begin(), diff_polygons.end());
            }
        }
        
        // now apply the contact areas to the layer were they need to be made
        if (! contact.polygons.empty()) {
            // get the average nozzle diameter used on this layer
            Layer &new_layer = layer_allocate(layer_storage);
            new_layer.layer_type = sltTopContact;
            new_layer.idx_object_layer_above = layer_id;
            if (m_soluble_interface) {
                // Align the contact surface height with a layer immediately below the supported layer.
                new_layer.print_z = layer.print_z - layer.heigh;
                new_layer.height = (layer_id > 0) ? 
                    // Interface layer will be synchronized with the object.
                    object.get_layer(layer_id - 1)->height : 
                    // Don't know the thickness of the raft layer yet.
                    0.;
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
                    const PrintRegion &region = (*it_region_ptr)->region;
                    nozzle_dmr += m_print_config->get_at('nozzle_diameter', region.config->perimeter_extruder-1);
                    nozzle_dmr += m_print_config->get_at('nozzle_diameter', region.config->infill_extruder-1);
                    nozzle_dmr += m_print_config->get_at('nozzle_diameter', region.config->solid_infill_extruder-1);
                    n_nozzle_dmrs += 3;
                }
                nozzle_dmr /= n_nozzle_dmrs;
                new_layer.print_z = layer.print_z - nozzle_dmr - $self->object_config->support_material_contact_distance;
                // Don't know the height of the top contact layer yet. The top contact layer is printed with a normal flow and 
                // its height will be set adaptively later on.
                new_layer.height = 0.;
                new_layer.bottom_z = new_layer.print_z;
            }

            // Ignore this contact area if it's too low.
            // Don't want to print a layer below the first layer height as it may not stick well.
            //FIXME there may be a need for a single layer support, then one may decide to print it either as a bottom contact or a top contact
            // and it may actually make sense to do it with a thinner layer than the first layer height.
            if (new_layer.print_z < this->object_config->get_value('first_layer_height') - EPSILON)
                continue;
            
            new_layer.polygons.swap(contact_polygons);
            new_layer.aux_polygons = new Polygons();
            new_layer.aux_polygons->swap(overhang_polygons);
            contact_out.push_back(new_layer);

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

/*
static inline LayersPtr sort_layers(LayersSet &layers)
{
    LayersPtr sorted;
    for (LayersSet::const_iterator it = layers.begin(); it != layers.end(); ++ it)
        sorted.push_back(const_cast<Layer*>(&(*it)));
    std::sort(sorted.begin(), sorted.end());
    return sorted;
}
*/

void PrintSupportMaterial::bottom_contact_layers(const PrintObject &object, LayersDeque &top_contacts) const
{
    // find object top surfaces
    // we'll use them to clip our support and detect where does it stick
    LayersPtr bottom_contact_layers;
    if (! self->object_config->support_material_buildplate_only && ! layers.empty())
    {
        // Sum of unsupported contact areas above the current $layer->print_z.
        Polygons  projection;
        // Last top contact layer visited when collecting the projection of contact areas.
        int       contact_idx = int(top_contacts.size()) - 1;
        for (int layer_id = int(object.total_layer_count()) - 2; layer_id >= 0; -- layer_id) {
            const Layer &layer = *object.get_layer(layer_id);
            Polygons top = collect_region_slices_by_type(layer, stTop);
            if (top.empty())
                continue;
            // compute projection of the contact areas above this top layer
            // first add all the 'new' contact areas to the current projection
            // ('new' means all the areas that are lower than the last top layer
            // we considered)
            // use <= instead of just < because otherwise we'd ignore any contact regions
            // having the same Z of top layers
            while (contact_idx >= 0 && top_contacts[contact_idx]->print_z > layer.print_z) {
                projection.insert(projection.back(), top_contacts[contact_idx]->polygons.begin(), top_contacts[contact_idx]->polygons.end());
            // Now find whether any projection of the contact surfaces above $layer->print_z not yet supported by any top surfaces above $layer->z falls onto this top surface. 
            // $touching are the contact surfaces supported exclusively by this @top surfaaces.
            Polygons touching = intersection(projection, top);
            if (touching.empty())
                continue;
            // Allocate a new bottom contact layer.
            Layer &layer_new = layer_allocate(top_contacts);
            bottom_contact_layers.push_back(&layer_new);
            layer_new.layer_type = sltBottomContact;
            // grow top surfaces so that interface and support generation are generated
            // with some spacing from object - it looks we don't need the actual
            // top shapes so this can be done here
            support_layer.height  = m_soluble_interface ? 
                // Align the interface layer with the object's layer height.
                object->get_layer(layer_id + 1)->height :
                // Place a bridge flow interface layer over the top surface.
                m_interface_flow->nozzle_diameter;
            layer_new.print_z = layer.print_z + support_layer.height + m_object_config->support_material_contact_distance;
            new_layer.bottom_z = layer.print_z;
            layer_new.idx_object_layer_below = layer_id;
            layer_new.bridging = true;
            Polygons poly_new = offset(touching, m_flow.scaled_width);
            layer_new.polygons.swap(poly_new);
            // Remove the areas that touched from the projection that will continue on next, lower, top surfaces.
            projection = diff(projection, touching);
        }
    }
    return bottom_contact_layers;
}

// Trim the top_contacts layers with the bottom_contacts layers if they overlap, so there would not be enough vertical space for both of them.
void PrintSupportMaterial::trim_top_contacts_by_bottom_contacts(const PrintObject &object, const LayersPtr &bottom_contacts, const LayersPtr &top_contacts)
{
    size_t idx_top_first = 0;
    coordf_t min_layer_height = 0.05;
    // For all bottom contact layers:
    for (size_t idx_bottom = 0; idx_bottom < bottom_contacts.size() && idx_top_first < top_contacts.size(); ++ idx_bottom) {
        const Layer &layer_bottom = *bottom_contacts[idx_bottom];
        // Find the first top layer overlapping with layer_bottom.
        while (idx_top_first < top_contacts.size() && top_contacts[idx_top]->print_z <= layer_bottom.bottom_z)
            ++ idx_top_first;
        // For all top contact layers overlapping with the thick bottom contact layer:
        for (size_t idx_top = idx_top_first; idx_top < top_contacts.size(); ++ idx_top) {
            Layer &layer_top = *top_contacts[idx_top];
            coordf_t interface_z = m_soluble_interface ? 
                (layer_top.bottom_z + EPSILON) :
                (layer_top.bottom - min_layer_height);
            if (interface_z < layer_bottom.print_z) {
                // Layers overlap. Trim layer_top with layer_bottom.
                layer_top.polygons = diff(layer_top.polygons, layer_bottom.polygons);
            } else
                break;
        }
    }
}

PrintSupportMaterial::LayersPtr PrintSupportMaterial::raft_and_intermediate_support_layers(
    const PrintObject   &object,
    const LayersPtr     &bottom_contacts,
    const LayersPtr     &top_contacts,
    std::deque<Layer>   &layer_storage,
    const coordf_t       max_object_layer_height);
{
    // determine layer height for any non-contact layer
    // we use max() to prevent many ultra-thin layers to be inserted in case
    // layer_height > nozzle_diameter * 0.75
    
    //my $nozzle_diameter = $self->print_config->get_at('nozzle_diameter', $self->object_config->support_material_extruder-1);
    //my $support_material_height = max($max_object_layer_height, $nozzle_diameter * 0.75);
    //my $contact_distance = $self->contact_distance($support_material_height, $nozzle_diameter);

    // Collect all known layers here:
    // bottom interface layers, newly created intermediate layers, top interface layers, raft layers.
    LayersPtr support_layers;
    
    coordf_t first_layer_height = m_object_config->get_value('first_layer_height');

    if (m_object_config->raft_layers == 0) {
        // No raft.
    } else if (m_object_config->raft_layers == 1) {
        // Only the raft interface layer.
    } else {
        assert(m_object_config->raft_layers > 1);
        // Generate the 
    }
    while (z.front()->print_z < first_layer_height - EPSILON)
        z.erase(z.begin());
    if (z.empty())
        return z;

    if (z.front()->print_z > first_layer_height + EPSILON) {
        Layer support_layer;
        support_layer.layer_type = stlFirstLayer;
        support_layer.print_z    = first_layer_height;
        support_layer.height     = first_layer_height;
        LayersSet::const_iterator it = other.insert(support_layer);
        z.insert(z.begin(), support_layer);
    }

    // Add raft layers by dividing the space between first layer and first contact layer evenly.
    if (m_object_config->raft_layers > 1 && z.size() > 1) {
        // z[1] is last raft layer (contact layer for the first layer object)
        coordf_t height = (z[1].print_z - z[0].print_z) / (m_object_config->raft_layers - 1);
        // Since we already have two raft layers ($z[0] and $z[1]) we need to insert raft_layers-2 more.
        z.insert(z.begin() + 1, m_object_config - 2, NULL);
        for (size_t i = 1; i < m_object_config->raft_layers; ++ i) {
            Layer support_layer;
            support_layer.layer_type = stlRaft;
            support_layer.print_z    = z.front()->print_z + height * i;
            support_layer.height     = height;
            LayersSet::const_iterator it = other.insert(support_layer);
            z.insert(z.begin(), support_layer);
        }
    }

    // create other layers (skip raft layers as they're already done and use thicker layers)
    for (int i = int(z.size()); i >= m_object_config->raft_layers; -- i) {
        coordf_t target_height = support_material_height;
        if (i > 0 && $top{ $z[$i-1] }) {
            // Bridge flow?
            //FIXME We want to enforce not only the bridge flow height, but also the interface gap!
            // This will introduce an additional layer if the gap is set to an extreme value!
            $target_height = $nozzle_diameter;
        }
        
        // enforce first layer height
        //FIXME better to split the layers regularly, than to bite a constant height one at a time, 
        // and then be left with a very thin layer at the end.
        if ((i == 0 && z[i] > target_height + first_layer_height)
            || (z[i] - z[i-1] > target_height + EPSILON)) {
            splice @z, $i, 0, ($z[$i] - $target_height);
            $i++;
        }
    }

    // Collect and sort the extremes (bottoms of the top contacts and tops of the bottom contacts).
    std::vector<LayerExtreme> extremes;
    extremes.reserve(top_contacts.size() + bottom_contacts.size());
    for (size_t i = 0; i < top_contacts.size(); ++ i)
        extremes.push_back(LayerExtreme(top_contacts[i], false));
    for (size_t i = 0; i < bottom_contacts.size(); ++ i)
        extremes.push_back(LayerExtreme(bottom_contacts[i], true));
    std::sort(extremes.begin(), extremes.end());

    // Generate intermediate layers.
    LayersPtr intermediate_layers;
    coordf_t max_support_layer_height;
    for (size_t idx_extreme = 0; idx_extreme + 1 < extremes.size(); ++ idx_extreme) {
        LayerExtreme &extr1 = extremes[idx_extreme];
        LayerExtreme &extr2 = extremes[idx_extreme+1];
        coordf_t dist = extr2.z() - extr1.z();
        assert(dist > 0.);
        // Insert intermediate layers.
        size_t n_layers_extra = size_t(ceil(dist / max_support_layer_height));
        coordf_t step = dist / coordf_t(n_layers_extra);
        if (! m_soluble_interface && extr2.layer->layer_type == stlTop) {
            // This is a top interface layer, which does not have a height assigned yet. Do it now.
            extr2.layer->height = step;
            extr2.layer->bottom_z = extr2.layer->top_z - step;
            -- n_layers_extra;
        }
        for (size_t i = 0; i < n_layers_extra; ++ i) {
            Layer &layer_new = layer_allocate(layer_storage);
            layer_new.layer_type = sltIntermediate;
            layer_new.height     = step;
            layer_new.bottom_z   = extr1.z() + i * step;
            layer_new.top_z      = layer_new.bottom_z + step;
            intermediate_layers.push_back(&layer_new);
        }
    }

    return support_layers;
}

inline void polygons_append(Polygons &dst, const Polygons &src)
{
    dst.insert(dst.end(), src.begin(), src.end());
}

// At this stage there shall be intermediate_layers allocated between bottom_contacts and top_contacts, but they have no polygons assigned.
// Also the bottom/top_contacts shall have a thickness assigned.
void PrintSupportMaterial::generate_base_layers(
    const PrintObject   &object,
    const LayersPtr     &bottom_contacts,
    const LayersPtr     &top_contacts,
    LayersPtr           &intermediate_layers)
{
    if (top_contacts.empty())
        // No top contacts -> no intermediate layers will be produced.
        return;

    // coordf_t fillet_radius_scaled = scale_(m_object_config->support_material_spacing);

    int idx_top_contact_above = int(top_contacts.size()) - 1;
    int idx_top_contact_overlapping = int(top_contacts.size()) - 1;
    int idx_bottom_contact_overlapping = int(bottom_contacts.size()) - 1;
    for (int idx_intermediate = int(intermediate_layers.size()) - 1; idx_intermediate >= 0; -- idx_intermediate)
    {
        Layer &layer_intermediate = intermediate_layers[idx_intermediate];

        // New polygons for layer_intermediate.
        Polygons polygons_new;

        // Find a top_contact layer touching the layer_intermediate from above, if any, and collect its polygons into polygons_new.
        while (idx_top_contact_above >= 0 && top_contacts[idx_top_contact_above].bottom_z > layer_intermediate.print_z + EPSILON)
            -- idx_top_contact_above;
        if (idx_top_contact_above >= 0 && top_contacts[idx_top_contact_above].top_z > layer_intermediate.print_z)
            polygons_append(polygons_new, top_contacts[idx_top_contact_above].polygons);

        // Add polygons from the intermediate layer above.
        if (idx_intermediate + 1 < int(intermediate_layers.size()))
            polygons_append(polygons_new, intermediate_layers[idx_intermediate+1].polygons);

        // Polygons to trim polygons_new.
        Polygons polygons_trimming;

        // Find the first top_contact layer intersecting with this layer.
        while (idx_top_contact_overlapping >= 0 && top_contacts[idx_top_contact_overlapping].bottom_z > layer_intermediate.print_z + overlap_extra_above - EPSILON)
            -- idx_top_contact_overlapping;
        // Collect all the top_contact layer intersecting with this layer.
        for (int i = idx_top_contact_overlapping; i >= 0; -- i) {
            Layer &layer_top_overlapping = top_contacts[idx_top_contact_overlapping];
            if (layer_top_overlapping.print_z < layer_intermediate.bottom_z - overlap_extra_below)
                break;
            polygons_append(polygons_trimming, layer_top_overlapping.polygons);
        }

        // Find the first bottom_contact layer intersecting with this layer.
        while (idx_bottom_contact_overlapping >= 0 && bottom_contacts[idx_bottom_contact_overlapping].bottom_z > layer_intermediate.print_z + overlap_extra_above - EPSILON)
            -- idx_bottom_contact_overlapping;
        // Collect all the top_contact layer intersecting with this layer.
        for (int i = idx_bottom_contact_overlapping; i >= 0; -- i) {
            Layer &layer_bottom_overlapping = bottom_contacts[idx_bottom_contact_overlapping];
            if (layer_bottom_overlapping.print_z < layer_intermediate.print_z - layer_intermediate.height - overlap_extra_below)
                break;
            polygons_append(polygons_trimming, layer_bottom_overlapping.polygons);
        }

        // Trim the polygons, store them.
        if (polygons_trimming.empty())
            layer_intermediate.polygons.swap(polygons_new);
        else
            layer_intermediate.polygons = diff(
                polygons_new,
                polygons_trimming,
                true); // safety offset to merge the touching source polygons

/*
        if (0) {
            // Fillet the base polygons and trim them again with the top, interface and contact layers.
            $base->{$i} = diff(
                offset2(
                    $base->{$i}, 
                    $fillet_radius_scaled, 
                    -$fillet_radius_scaled,
                    # Use a geometric offsetting for filleting.
                    CLIPPER_OFFSET_SCALE,
                    JT_ROUND,
                    0.2*$fillet_radius_scaled*CLIPPER_OFFSET_SCALE),
                $trim_polygons,
                false); // don't apply the safety offset.
        }
*/
    }

    //FIXME This could be parallelized.
    const coordf_t gap_extra_above = 0.1f;
    const coordf_t gap_extra_below = 0.1f;
    const coordf_t gap_xy_scaled = m_flow.scaled_width;
    size_t idx_object_layer_overlapping = 0;
    // For all intermediate layers:
    for (LayersPtr::iterator it_layer = intermediate_layers.begin(); it_layer != intermediate_layers.end(); ++ it_layer) {
        Layer &layer_intermediate = *(*it_layer);
        if (layer_intermediate.polygons.empty())
            continue;
        // Find the overlapping object layers including the extra above / below gap.
        while (idx_object_layer_overlapping < object.layer_count() && 
            object.get_layer(idx_object_layer_overlapping)->print_z < layer_intermediate.print_z - layer_intermediate.height - gap_extra_below + EPSILON)
            ++ idx_object_layer_overlapping;
        // Collect all the object layers intersecting with this layer.
        Polygons polygons_trimming;
        for (int i = idx_object_layer_overlapping; i < object.layer_count(); ++ i) {
            Layer &object_layer = *object.get_layer(i);
            if (object_layer.print_z > layer_intermediate.print_z + gap_extra_above - EPSILON)
                break;
            polygons_append(polygons_trimming, (Polygons)object_layer.slices);
        }

        // $layer->slices contains the full shape of layer, thus including
        // perimeter's width. $support contains the full shape of support
        // material, thus including the width of its foremost extrusion.
        // We leave a gap equal to a full extrusion width.
        layer_intermediate.polygons = diff(
            layer_intermediate.polygons,
            offset(polygons_trimming, gap_xy_scaled),
        );
    }
}

// Convert some of the intermediate layers into top/bottom interface layers.
LayersPtr PrintSupportMaterial::generate_interface_layers(
    const PrintObject   &object,
    const LayersPtr     &bottom_contacts,
    const LayersPtr     &top_contacts,
    LayersPtr           &intermediate_layers,
    std::deque<Layer>   &layer_storage)
{
    // Old comment:
    // Compute interface area on this layer as diff of upper contact area
    // (or upper interface area) and layer slices.
    // This diff is responsible of the contact between support material and
    // the top surfaces of the object. We should probably offset the top 
    // surfaces vertically before performing the diff, but this needs 
    // investigation.

//    my $area_threshold = $self->interface_flow->scaled_spacing ** 2;

    LayersPtr interface_layers;
    // Contact layer is considered an interface layer, therefore run the following block only if support_material_interface_layers > 1.
    if (! intermediate_layers.empty() && m_object_config->support_material_interface_layers > 1) {
        // Index of the first top contact layer intersecting the current intermediate layer.
        size_t idx_top_contact_first = 0;
        // Index of the first bottom contact layer intersecting the current intermediate layer.
        size_t idx_bottom_contact_first = 0;
        // For all intermediate layers, collect top contact surfaces, which are not further than support_material_interface_layers.
        //FIXME this could be parallelized.
        for (size_t idx_intermediate_layer = 0; idx_intermediate_layer < intermediate_layers.size(); ++ idx_intermediate_layer) {
            Layer &intermediate_layer = intermediate_layers[idx_intermediate_layer];
            // Top / bottom Z coordinate of a slab, over which we are collecting the top / bottom contact surfaces.
            coordf_t top_z    = intermediate_layers[std::min<int>(intermediate_layers.size()-1, idx_intermediate_layer + m_object_config->support_material_interface_layers - 1)]->print_z;
            coordf_t bottom_z = intermediate_layers[std::max<int>(0, int(idx_intermediate_layer) - int(m_object_config->support_material_interface_layers) + 1)]->bottom_z;
            // Move idx_top_contact_first up until above the current print_z.
            while (idx_top_contact_first < top_contacts.size() && top_contacts[idx_top_contact_first]->print_z < intermediate_layer->print_z)
                ++ idx_top_contact_first;
            // Collect the top contact areas above this intermediate layer, below top_z.
            Polygons polygons_top_contact_projected;
            for (size_t idx_top_contact = idx_top_contact_first; idx_top_contact < top_contacts.size(); ++ idx_top_contact) {
                const Layer &top_contact_layer = top_contacts[idx_top_contact];
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
                const Layer &bottom_contact_layer = bottom_contacts[idx_bottom_contact];
                if (bottom_contact_layer.top_z - EPSILON > intermediate_layer.bottom_z)
                    break;
                polygons_append(polygons_bottom_contact_projected, bottom_contact_layer.polygons);
            }

            if (polygons_top_contact_projected.empty() && polygons_bottom_contact_projected.empty())
                continue;

            // Insert a new layer into top_interface_layers.
            Layer &layer_new = layer_allocate(layer_storage);
            layer_new.layer_type = polygons_top_contact_projected.empty() ? stlBottomInterface : sltTopInterface;
            layer_new.print_z    = intermediate_layer.print_z;
            layer_new.bottom_z   = intermediate_layer.bottom_z;
            layer_new.height     = intermediate_layer.height;
            layer_new.bridging   = intermediate_layer.bridging;
            top_interface_layers.push_back(layer_new);

            polygons_append(polygons_top_contact_projected, polygons_bottom_contact_projected);
            polygons_top_contact_projected = union_(polygons_top_contact_projected, true);
            layer_new.polygons = intersection(intermediate_layers.polygons, polygons_top_contact_projected);
            //FIXME filter layer_new.polygons islands by a minimum area?
//                $interface_area = [ grep abs($_->area) >= $area_threshold, @$interface_area ];
            intermediate_layers.polygons = diff(intermediate_layers.polygons, polygons_top_contact_projected, false)
        }
    }
    
    return interface_layers;
}

void PrintSupportMaterial::generate_toolpaths(
    const PrintObject   &object,
    const MyLayersPtr   &bottom_contacts,
    const MyLayersPtr   &top_contacts,
    const MyLayersPtr   &intermediate_layers,
    const MyLayersPtr   &interface_layers)
{
    // Shape of the top contact area.
    int         n_contact_loops = 1;
    coordf_t    circle_radius   = 1.5 * m_interface_flow.scaled_width;
    coordf_t    circle_distance = 3. * circle_radius;
    Polygon     circle;
    circle.reserve(6);
    for (size_t i = 0; i < 6; ++ i) {
        double angle = double(i) * m_PI / 3.;
        circle.push_back(Point(circle_radius * cos(angle), circle_radius * sin(angle)));
    }
    
//    Slic3r::debugf "Generating patterns\n";

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
    std::auto_ptr<Fill> filler_interface = std::auto_ptr<Fill>(Fill::new_from_type(ipRectilinear));
    std::auto_ptr<Fill> filler_support   = std::auto_ptr<Fill>(Fill::new_from_type(infill_pattern));
    {
        BoundingBox bbox_object = object.bounding_box();
        fill_interface->set_bounding_box(bbox_object);
        fill_interface->set_bounding_box(fill_support);
    }

    coordf_t interface_angle    = m_object_config->support_material_angle + 90.;
    coordf_t interface_spacing  = m_object_config->support_material_interface_spacing + m_interface_flow.spacing;
    coordf_t interface_density  = (interface_spacing == 0.) ? 1. : (m_interface_flow.spacing / m_interface_spacing);
    coordf_t support_spacing    = m_object_config.object_config->support_material_spacing + m_flow.spacing;
    coordf_t support_density    = (support_spacing == 0.) ? 1. : (m_flow.spacing / support_spacing);
    
    //FIXME Parallelize the support generator:
    /*
    Slic3r::parallelize(
        threads => $self->print_config->threads,
        items => [ 0 .. n_$object->support_layers} ],
        thread_cb => sub {
            my $q = shift;
            while (defined (my $layer_id = $q->dequeue)) {
                $process_layer->($layer_id);
            }
        },
        no_threads_cb => sub {
            $process_layer->($_) for 0 .. n_{$object->support_layers};
        },
    );
    */
    // Indices of the 1st layer in their respective container at the support layer height.
    size_t idx_layer_bottom_contact   = 0;
    size_t idx_layer_top_contact      = 0;
    size_t idx_layer_intermediate     = 0;
    size_t idx_layer_inteface         = 0;
    for (size_t support_layer_id = 0; support_layer_id < object.support_layers.size(); ++ support_layer_id) 
    {
        SupportLayer &support_layer = *object.support_layers[support_layer_id];

        // Find polygons with the same print_z.
        Polygons bottom_contact_polygons;
        Polygons interface_polygons;
        Polygons base_polygons;
        // Increment the layer indices to find a layer at support_layer.print_z.
        for (; idx_layer_bottom_contact < bottom_contacts    .size() && bottom_contacts    [idx_layer_bottom_contact].print_z < support_layer.print_z - EPSILON; ++ idx_layer_bottom_contact) ;
        for (; idx_layer_top_contact    < top_contacts       .size() && top_contacts       [idx_layer_top_contact   ].print_z < support_layer.print_z - EPSILON; ++ idx_layer_top_contact   ) ;
        for (; idx_layer_intermediate   < intermediate_layers.size() && intermediate_layers[idx_layer_intermediate  ].print_z < support_layer.print_z - EPSILON; ++ idx_layer_intermediate  ) ;
        for (; idx_layer_inteface       < interface_layers   .size() && interface_layers   [idx_layer_inteface      ].print_z < support_layer.print_z - EPSILON; ++ interface_layers        ) ;
        // Copy polygons from the layers.
        if (idx_layer_bottom_contact < bottom_contacts.size() && bottom_contacts[idx_layer_bottom_contact].print_z < support_layer.print_z + EPSILON)
            bottom_contact_polygons = bottom_contacts[idx_layer_bottom_contact].polygons;
        if (idx_layer_inteface < interface_layers.size() && interface_layers[idx_layer_inteface].print_z < support_layer.print_z + EPSILON)
            interface_polygons = interface_layers[idx_layer_inteface].polygons;
        if (idx_layer_intermediate < intermediate_layers.size() && intermediate_layers[idx_layer_intermediate].print_z < support_layer.print_z + EPSILON)
            base_polygons = intermediate_layers[idx_layer_intermediate].polygons;
        
        // We redefine flows locally by applying this layer's height.
        Flow flow           = m_flow;
        Flow interface_flow = m_interface_flow;
        flow.set_height(layer.height);
        interface_flow.set_height(layer.height);
        
        /*
        if (1) {
            require "Slic3r/SVG.pm";
            Slic3r::SVG::output("out\\layer_" . $z . ".svg",
                blue_expolygons     => union_ex($base),
                red_expolygons      => union_ex($contact),
                green_expolygons    => union_ex($interface),
            );
        }
        */
        
        // Store inslands, over which the retract will be disabled.
        {
            Polygons polys(bottom_contact_polygons);
            polygons_append(polys, interface_polygons);
            polygons_append(polys, base_polygons);
            polygons_append(polys, top_contact_polygons);
            ExPolygons islands = union_ex(polys);
            support_layer.support_islands.expolygons.insert(support_layer.support_islands.expolygons.end(), islands.begin(), islands.end());
        }
        
        Polygons contact_infill_polygons;
        if (idx_layer_top_contact < top_contacts.size() && top_contacts[idx_layer_top_contact].print_z < support_layer.print_z + EPSILON)
        {
            // Having a top interface layer.
            Polygons top_contact_polygons = top_contacts[idx_layer_top_contact].polygons;
            if (m_object_config->support_material_interface_layers == 0)
                // If no interface layers were requested, we treat the contact layer exactly as a generic base layer.
                polygons_append(base_polygons, top_contact_polygons);
            else if (n_contact_loops == 0)
                // If no loops are allowed, we treat the contact layer exactly as a generic interface layer.
                polygons_append(interface_polygons, top_contact_polygons);
            else if (! top_contact_polygons.empty())
            {
                // Create loop paths and 
                Polygons overhang_polygons = (top_contacts[idx_layer_top_contact]->aux_polygons == NULL) ? 
                    Polygons() :
                    *top_contacts[idx_layer_top_contact]->aux_polygons;

                // Generate the outermost loop.
                // Find centerline of the external loop (or any other kind of extrusions should the loop be skipped)
                top_contact_polygons = offset(top_contact_polygons, - 0.5 * interface_flow.scaled_width);
                
                Polygons loops0;
                {
                    // find centerline of the external loop of the contours
                    // only consider the loops facing the overhang
                    Polygons external_loops;
                    // Positions of the loop centers.
                    Polygons circles;
                    {
                        Polygons overhang_with_margin = offset(overhang_polygons, 0.5 * interface_flow.scaled_width);
                        for (Polygons::const_iterator it_contact = top_contact_polygons.begin(); it_contact != top_contact_polygons.end(); ++ it_contact)
                            if (! intersection_pl(it_contact->split_at_first_point, overhang_with_margin).empty()) {
                                external_loops.push_back(*it_contact);
                                Points positions_new = it_contact->equally_spaced_points(circle_distance);
                                for (Points::const_iterator it_center = positions_new.begin(); it_center != positions_new.end(); ++ it_center) {
                                    circles.push_back(circle);
                                    Polygon &circle_new = circles.back();
                                    for (size_t i = 0; i < circle_new.size(); ++ i)
                                        circle_new[i].translate(*it_center);
                                }
                            }
                    }
                    // Apply a pattern to the loop.
                    @loops0 = diff(external_loops, circles);
                }

                // make more loops
                Polygons loops = loops0;
                for (size_t i = 1; i < n_contact_loops)
                    polygons_append(loops, offset2(loops0, - i * interface_flow.scaled_spacing - 0.5 * interface_flow.scaled_spacing, 0.5 * interface_flow.scaled_spacing));

                // clip such loops to the side oriented towards the object
                {
                    Polylines loop_lines;
                    loop_lines.reserve(loops.size());
                    for (Polygons::const_iterator it = loops.begin(); it != loops.end(); ++ it)
                        loop_lines.push_back(it->split_at_first_point);
                    loops = intersection_pl(loop_lines, offset(overhang_polygons, scale_(SUPPORT_MATERIAL_MARGIN)));
                }
                
                // add the contact infill area to the interface area
                // note that growing loops by $circle_radius ensures no tiny
                // extrusions are left inside the circles; however it creates
                // a very large gap between loops and contact_infill_polygons, so maybe another
                // solution should be found to achieve both goals
                contact_infill_polygons = diff(top_contact_polygons, offset(loops, circle_radius * 1.1, true));

                // Transform loops into ExtrusionPath objects.
                for (Polylines::const_iterator it_polyline = loops.begin(); it_polyline != loops.end(); ++ it_polyline) {
                    ExtrusionPath *extrusion_path = new ExtrusionPath(erSupportMaterialInterface);
                    support_layer.support_interface_fills.entities.push_back(extrusion_path);
                    extrusion_path->polyline    = it_polyline->split_at_first_point;
                    extrusion_path->mm3_per_mm  = interface_flow.mm3_per_mm;
                    extrusion_path->width       = interface_flow.width;
                    extrusion_path->height      = support_layer.height;
                }
            }
        }

        // interface and contact infill
        if (! interface_polygons.empty() || ! contact_infill_polygons.empty()) {
            //FIXME When paralellizing, each thread shall have its own copy of the fillers.
            filler_interface->set_angle(interface_angle);
            filler_interface->set_spacing(interface_flow.spacing);
            
            // find centerline of the external loop
            interface_polygons = offset2(interface_polygons, SCALED_EPSILON, - SCALED_EPSILON - 0.5 * interface_flow.scaled_width);
            // join regions by offsetting them to ensure they're merged
            polygons_append(interface_polygons, contact_infill_polygons);
            interface_polygons = offset(interface_polygons, SCALED_EPSILON);
            
            // turn base support into interface when it's contained in our holes
            // (this way we get wider interface anchoring)
            {
                Polygons interface_polygons_new;
                interface_polygons_new.reserve(interface_polygons.size());
                for (Polygons::iterator it_polygon = interface_polygons.begin(); it_polygon != interface_polygons.end(); ++ it_polygon) {
                    if (it_polygon->is_clockwise) {
                        Polygons hole;
                        hole.push_back(*it_polygon);
                        hole.back().make_counter_clockwise();
                        if (diff(hole, base_polygons, true).empty())
                            continue;
                    }
                    interface_polygons_new.push_back(Polygon());
                    interface_polygons_new.back().swap(*it_polygon);
                }
                interface_polygons.swap(interface_polygons_new);
            }
            base_polygons = diff(base_polygons, interface_polygons);

            ExPolygons to_fill = union_ex(interface_polygons);
            for (ExPolygons::const_iterator it_expolygon = to_fill.begin(); it_expolygon != to_fill.end(); ++ it_expolygon) {
                FillParams fill_params;
                fill_params.density = interface_density;
                fill_params.complete = true;
                Polylines polylines = filler_interface->fill_surface(Surface(stInternal, *it_expolygon), fill_params);
                for (Polylines::const_iterator it_polyline = polylines.begin(); it_polyline != polylines.end(); ++ it_polyline) {
                    ExtrusionPath *extrusion_path = new ExtrusionPath(erSupportMaterialInterface);
                    support_layer.support_interface_fills.entities.push_back(extrusion_path);
                    extrusion_path->polyline    = it_polyline->split_at_first_point;
                    extrusion_path->mm3_per_mm  = interface_flow.mm3_per_mm;
                    extrusion_path->width       = interface_flow.width;
                    extrusion_path->height      = support_layer.height;
                }
            }
        }
        
        // support or flange
        if (! base_polygons.empty()) {
            //FIXME When paralellizing, each thread shall have its own copy of the fillers.
            Fill *filler = filler_support.get();
            filler->set_angle(angles[support_layer_id % angles.size()]);
            // We don't use $base_flow->spacing because we need a constant spacing
            // value that guarantees that all layers are correctly aligned.
            filler->set_spacing(flow.spacing);
            
            coordf_t density     = support_density;
            Flow     base_flow   = flow;
            
            // find centerline of the external loop/extrusions
            ExPolygons to_infill = offset2_ex(base_polygons, SCALED_EPSILON, - SCALED_EPSILON - 0.5*flow.scaled_width);

            /*
            if (1) {
                require "Slic3r/SVG.pm";
                Slic3r::SVG::output("out\\to_infill_base" . $z . ".svg",
                    red_expolygons      => union_ex($contact),
                    green_expolygons    => union_ex($interface),
                    blue_expolygons     => $to_infill,
                );
            }
            */
            
            if (support_layer_id == 0) {
                // Base flange.
                filler = filler_interface.get();
                filler->set_angle(m_object_config->support_material_angle + 90.);
                density   = 0.5;
                base_flow = m_first_layer_flow;
                // use the proper spacing for first layer as we don't need to align
                // its pattern to the other layers
                //FIXME When paralellizing, each thread shall have its own copy of the fillers.
                filler->set_spacing(base_flow.spacing);
            } else if (with_sheath) {
                // Draw a perimeter all around the support infill. This makes the support stable, but difficult to remove.
                // TODO: use brim ordering algorithm
                Polygons to_infill_polygons = (Polygons)to_infill;
                for (Polygons::const_iterator it_polyline = to_infill_polygons.begin(); it_polyline != to_infill_polygons.end(); ++ it_polyline) {
                    ExtrusionPath *extrusion_path = new ExtrusionPath(erSupportMaterial);
                    support_layer.support_fills.entities.push_back(extrusion_path);
                    extrusion_path->polyline    = it_polyline->split_at_first_point;
                    extrusion_path->mm3_per_mm  = flow.mm3_per_mm;
                    extrusion_path->width       = flow.width;
                    extrusion_path->height      = support_layer.height;
                }
                // TODO: use offset2_ex()
                to_infill = offset_ex(to_infill_polygons, - flow.scaled_spacing);
            }
            
            for (ExPolygons::const_iterator it_expolygon = to_infill.begin(); it_expolygon != to_infill.end(); ++ it_expolygon) {
                FillParams fill_params;
                fill_params.density = density;
                fill_params.complete = true;
                Polylines polylines = filler->fill_surface(Surface(stInternal, *it_expolygon), fill_params);
                for (Polylines::const_iterator it_polyline = polylines.begin(); it_polyline != polylines.end(); ++ it_polyline) {
                    ExtrusionPath *extrusion_path = new ExtrusionPath(erSupportMaterial);
                    support_layer.support_fills.entities.push_back(extrusion_path);
                    extrusion_path->polyline    = it_polyline->split_at_first_point;
                    extrusion_path->mm3_per_mm  = base_flow.mm3_per_mm;
                    extrusion_path->width       = base_flow.width;
                    extrusion_path->height      = support_layer.height;
            }
        }

        // support or flange
        if (! bottom_contact_polygons.empty()) {
            //FIXME When paralellizing, each thread shall have its own copy of the fillers.
            Fill *filler = filler_support.get();
            filler->set_angle(angles[support_layer_id % angles.size()]);
            // We don't use $base_flow->spacing because we need a constant spacing
            // value that guarantees that all layers are correctly aligned.
            filler->set_spacing(flow.spacing);
            
            coordf_t density     = support_density;
            Flow     base_flow   = flow;
            
            // find centerline of the external loop/extrusions
            ExPolygons to_infill = offset2_ex(base_polygons, SCALED_EPSILON, - SCALED_EPSILON - 0.5*flow.scaled_width);

            /*
            if (1) {
                require "Slic3r/SVG.pm";
                Slic3r::SVG::output("out\\to_infill_base" . $z . ".svg",
                    red_expolygons      => union_ex($contact),
                    green_expolygons    => union_ex($interface),
                    blue_expolygons     => $to_infill,
                );
            }
            */
            
            if (support_layer_id == 0) {
                // Base flange.
                filler = filler_interface.get();
                filler->set_angle(m_object_config->support_material_angle + 90.);
                density   = 0.5;
                base_flow = m_first_layer_flow;
                // use the proper spacing for first layer as we don't need to align
                // its pattern to the other layers
                //FIXME When paralellizing, each thread shall have its own copy of the fillers.
                filler->set_spacing(base_flow.spacing);
            } else if (with_sheath) {
                // Draw a perimeter all around the support infill. This makes the support stable, but difficult to remove.
                // TODO: use brim ordering algorithm
                Polygons to_infill_polygons = (Polygons)to_infill;
                for (Polygons::const_iterator it_polyline = to_infill_polygons.begin(); it_polyline != to_infill_polygons.end(); ++ it_polyline) {
                    ExtrusionPath *extrusion_path = new ExtrusionPath(erSupportMaterial);
                    support_layer.support_fills.entities.push_back(extrusion_path);
                    extrusion_path->polyline    = it_polyline->split_at_first_point;
                    extrusion_path->mm3_per_mm  = flow.mm3_per_mm;
                    extrusion_path->width       = flow.width;
                    extrusion_path->height      = support_layer.height;
                }
                // TODO: use offset2_ex()
                to_infill = offset_ex(to_infill_polygons, - flow.scaled_spacing);
            }
            
            for (ExPolygons::const_iterator it_expolygon = to_infill.begin(); it_expolygon != to_infill.end(); ++ it_expolygon) {
                FillParams fill_params;
                fill_params.density = density;
                fill_params.complete = true;
                Polylines polylines = filler->fill_surface(Surface(stInternal, *it_expolygon), fill_params);
                for (Polylines::const_iterator it_polyline = polylines.begin(); it_polyline != polylines.end(); ++ it_polyline) {
                    ExtrusionPath *extrusion_path = new ExtrusionPath(erSupportMaterial);
                    support_layer.support_fills.entities.push_back(extrusion_path);
                    extrusion_path->polyline    = it_polyline->split_at_first_point;
                    extrusion_path->mm3_per_mm  = base_flow.mm3_per_mm;
                    extrusion_path->width       = base_flow.width;
                    extrusion_path->height      = support_layer.height;
            }
        }

        /*
        if (0) {
            require "Slic3r/SVG.pm";
            Slic3r::SVG::output("islands_" . $z . ".svg",
                red_expolygons      => union_ex($contact),
                green_expolygons    => union_ex($interface),
                green_polylines     => [ map $_->unpack->polyline, @{$layer->support_contact_fills} ],
                polylines           => [ map $_->unpack->polyline, @{$layer->support_fills} ],
            );
        }
        */
    };
}

/*
void PrintSupportMaterial::clip_by_pillars(
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
