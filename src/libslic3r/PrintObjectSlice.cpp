#include "ElephantFootCompensation.hpp"
#include "I18N.hpp"
#include "Layer.hpp"
#include "MultiMaterialSegmentation.hpp"
#include "Print.hpp"
#include "ClipperUtils.hpp"

#include <boost/log/trivial.hpp>

#include <tbb/parallel_for.h>

//! macro used to mark string used at localization, return same string
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

LayerPtrs new_layers(
    PrintObject                 *print_object,
    // Object layers (pairs of bottom/top Z coordinate), without the raft.
    const std::vector<coordf_t> &object_layers)
{
    LayerPtrs out;
    out.reserve(object_layers.size());
    auto     id   = int(print_object->slicing_parameters().raft_layers());
    coordf_t zmin = print_object->slicing_parameters().object_print_z_min;
    Layer   *prev = nullptr;
    for (size_t i_layer = 0; i_layer < object_layers.size(); i_layer += 2) {
        coordf_t lo = object_layers[i_layer];
        coordf_t hi = object_layers[i_layer + 1];
        coordf_t slice_z = 0.5 * (lo + hi);
        Layer *layer = new Layer(id ++, print_object, hi - lo, hi + zmin, slice_z);
        out.emplace_back(layer);
        if (prev != nullptr) {
            prev->upper_layer = layer;
            layer->lower_layer = prev;
        }
        prev = layer;
    }
    return out;
}

template<typename LayerContainer>
static inline std::vector<float> zs_from_layers(const LayerContainer &layers)
{
    std::vector<float> zs;
    zs.reserve(layers.size());
    for (const Layer *l : layers)
        zs.emplace_back((float)l->slice_z);
    return zs;
}

//FIXME The admesh repair function may break the face connectivity, rather refresh it here as the slicing code relies on it.
// This function will go away once we get rid of admesh from ModelVolume.
static indexed_triangle_set get_mesh_its_fix_mesh_connectivity(TriangleMesh mesh)
{
    assert(mesh.repaired && mesh.has_shared_vertices());
    if (mesh.stl.stats.number_of_facets > 0) {
        assert(mesh.repaired && mesh.has_shared_vertices());
        auto nr_degenerated = mesh.stl.stats.degenerate_facets;
        stl_check_facets_exact(&mesh.stl);
        if (nr_degenerated != mesh.stl.stats.degenerate_facets)
            // stl_check_facets_exact() removed some newly degenerated faces. Some faces could become degenerate after some mesh transformation.
            stl_generate_shared_vertices(&mesh.stl, mesh.its);
    } else
        mesh.its.clear();
    return std::move(mesh.its);
}

// Slice single triangle mesh.
static std::vector<ExPolygons> slice_volume(
    const ModelVolume             &volume,
    const std::vector<float>      &zs, 
    const MeshSlicingParamsEx     &params,
    const std::function<void()>   &throw_on_cancel_callback)
{
    std::vector<ExPolygons> layers;
    if (! zs.empty()) {
        indexed_triangle_set its = get_mesh_its_fix_mesh_connectivity(volume.mesh());
        if (its.indices.size() > 0) {
            MeshSlicingParamsEx params2 { params };
            params2.trafo = params2.trafo * volume.get_matrix();
            if (params2.trafo.rotation().determinant() < 0.)
                its_flip_triangles(its);
            layers = slice_mesh_ex(its, zs, params2, throw_on_cancel_callback);
            throw_on_cancel_callback();
        }
    }
    return layers;
}

// Slice single triangle mesh.
// Filter the zs not inside the ranges. The ranges are closed at the bottom and open at the top, they are sorted lexicographically and non overlapping.
static std::vector<ExPolygons> slice_volume(
    const ModelVolume                           &volume,
    const std::vector<float>                    &z,
    const std::vector<t_layer_height_range>     &ranges,
    const MeshSlicingParamsEx                   &params,
    const std::function<void()>                 &throw_on_cancel_callback)
{
    std::vector<ExPolygons> out;
    if (! z.empty() && ! ranges.empty()) {
        if (ranges.size() == 1 && z.front() >= ranges.front().first && z.back() < ranges.front().second) {
            // All layers fit into a single range.
            out = slice_volume(volume, z, params, throw_on_cancel_callback);
        } else {
            std::vector<float>                     z_filtered;
            std::vector<std::pair<size_t, size_t>> n_filtered;
            z_filtered.reserve(z.size());
            n_filtered.reserve(2 * ranges.size());
            size_t i = 0;
            for (const t_layer_height_range &range : ranges) {
                for (; i < z.size() && z[i] < range.first; ++ i) ;
                size_t first = i;
                for (; i < z.size() && z[i] < range.second; ++ i)
                    z_filtered.emplace_back(z[i]);
                if (i > first)
                    n_filtered.emplace_back(std::make_pair(first, i));
            }
            if (! n_filtered.empty()) {
                std::vector<ExPolygons> layers = slice_volume(volume, z_filtered, params, throw_on_cancel_callback);
                out.assign(z.size(), ExPolygons());
                i = 0;
                for (const std::pair<size_t, size_t> &span : n_filtered)
                    for (size_t j = span.first; j < span.second; ++ j)
                        out[j] = std::move(layers[i ++]);
            }
        }
    }
    return out;
}

struct VolumeSlices
{
    ObjectID                volume_id;
    std::vector<ExPolygons> slices;
};

static inline bool model_volume_needs_slicing(const ModelVolume &mv)
{
    ModelVolumeType type = mv.type();
    return type == ModelVolumeType::MODEL_PART || type == ModelVolumeType::NEGATIVE_VOLUME || type == ModelVolumeType::PARAMETER_MODIFIER;
}

// Slice printable volumes, negative volumes and modifier volumes, sorted by ModelVolume::id().
// Apply closing radius.
// Apply positive XY compensation to ModelVolumeType::MODEL_PART and ModelVolumeType::PARAMETER_MODIFIER, not to ModelVolumeType::NEGATIVE_VOLUME.
// Apply contour simplification.
static std::vector<VolumeSlices> slice_volumes_inner(
    const PrintConfig                                        &print_config,
    const PrintObjectConfig                                  &print_object_config,
    const Transform3d                                        &object_trafo,
    ModelVolumePtrs                                           model_volumes,
    const std::vector<PrintObjectRegions::LayerRangeRegions> &layer_ranges,
    const std::vector<float>                                 &zs,
    const std::function<void()>                              &throw_on_cancel_callback)
{
    model_volumes_sort_by_id(model_volumes);

    std::vector<VolumeSlices> out;
    out.reserve(model_volumes.size());

    std::vector<t_layer_height_range> slicing_ranges;
    if (layer_ranges.size() > 1)
        slicing_ranges.reserve(layer_ranges.size());

    MeshSlicingParamsEx params_base;
    params_base.closing_radius = print_object_config.slice_closing_radius.value;
    params_base.extra_offset   = 0;
    params_base.trafo          = object_trafo;
    params_base.resolution     = print_config.resolution.value;

    switch (print_object_config.slicing_mode.value) {
    case SlicingMode::Regular:    params_base.mode = MeshSlicingParams::SlicingMode::Regular; break;
    case SlicingMode::EvenOdd:    params_base.mode = MeshSlicingParams::SlicingMode::EvenOdd; break;
    case SlicingMode::CloseHoles: params_base.mode = MeshSlicingParams::SlicingMode::Positive; break;
    }

    params_base.mode_below     = params_base.mode;

    const auto extra_offset = std::max(0.f, float(print_object_config.xy_size_compensation.value));

    for (const ModelVolume *model_volume : model_volumes)
        if (model_volume_needs_slicing(*model_volume)) {
            MeshSlicingParamsEx params { params_base };
            if (! model_volume->is_negative_volume())
                params.extra_offset = extra_offset;
            if (layer_ranges.size() == 1) {
                if (const PrintObjectRegions::LayerRangeRegions &layer_range = layer_ranges.front(); layer_range.has_volume(model_volume->id())) {
                    if (model_volume->is_model_part() && print_config.spiral_vase) {
                        auto it = std::find_if(layer_range.volume_regions.begin(), layer_range.volume_regions.end(), 
                            [model_volume](const auto &slice){ return model_volume == slice.model_volume; });
                        params.mode = MeshSlicingParams::SlicingMode::PositiveLargestContour;
                        // Slice the bottom layers with SlicingMode::Regular.
                        // This needs to be in sync with LayerRegion::make_perimeters() spiral_vase!
                        const PrintRegionConfig &region_config = it->region->config();
                        params.slicing_mode_normal_below_layer = size_t(region_config.bottom_solid_layers.value);
                        for (; params.slicing_mode_normal_below_layer < zs.size() && zs[params.slicing_mode_normal_below_layer] < region_config.bottom_solid_min_thickness - EPSILON;
                            ++ params.slicing_mode_normal_below_layer);
                    }
                    out.push_back({
                        model_volume->id(), 
                        slice_volume(*model_volume, zs, params, throw_on_cancel_callback)
                    });
                }
            } else {
                assert(! print_config.spiral_vase);
                slicing_ranges.clear();
                for (const PrintObjectRegions::LayerRangeRegions &layer_range : layer_ranges)
                    if (layer_range.has_volume(model_volume->id()))
                        slicing_ranges.emplace_back(layer_range.layer_height_range);
                if (! slicing_ranges.empty())
                    out.push_back({ 
                        model_volume->id(), 
                        slice_volume(*model_volume, zs, slicing_ranges, params, throw_on_cancel_callback)
                    });
            }
            if (! out.empty() && out.back().slices.empty())
                out.pop_back();
        }

    return out;
}

static inline VolumeSlices& volume_slices_find_by_id(std::vector<VolumeSlices> &volume_slices, const ObjectID id)
{
    auto it = lower_bound_by_predicate(volume_slices.begin(), volume_slices.end(), [id](const VolumeSlices &vs) { return vs.volume_id < id; });
    assert(it != volume_slices.end() && it->volume_id == id);
    return *it;
}

static inline bool overlap_in_xy(const PrintObjectRegions::BoundingBox &l, const PrintObjectRegions::BoundingBox &r)
{
    return ! (l.max().x() < r.min().x() || l.min().x() > r.max().x() ||
              l.max().y() < r.min().y() || l.min().y() > r.max().y());
}

static std::vector<PrintObjectRegions::LayerRangeRegions>::const_iterator layer_range_first(const std::vector<PrintObjectRegions::LayerRangeRegions> &layer_ranges, double z)
{
    auto  it = lower_bound_by_predicate(layer_ranges.begin(), layer_ranges.end(),
        [z](const PrintObjectRegions::LayerRangeRegions &lr) { return lr.layer_height_range.second < z; });
    assert(it != layer_ranges.end() && it->layer_height_range.first <= z && z <= it->layer_height_range.second);
    if (z == it->layer_height_range.second)
        if (auto it_next = it; ++ it_next != layer_ranges.end() && it_next->layer_height_range.first == z)
            it = it_next;
    assert(it != layer_ranges.end() && it->layer_height_range.first <= z && z <= it->layer_height_range.second);
    return it;
}

static std::vector<PrintObjectRegions::LayerRangeRegions>::const_iterator layer_range_next(
    const std::vector<PrintObjectRegions::LayerRangeRegions>            &layer_ranges, 
    std::vector<PrintObjectRegions::LayerRangeRegions>::const_iterator   it,
    double                                                               z)
{
    for (; it->layer_height_range.second <= z; ++ it)
        assert(it != layer_ranges.end());
    assert(it != layer_ranges.end() && it->layer_height_range.first <= z && z < it->layer_height_range.second);
    return it;
}

static std::vector<std::vector<ExPolygons>> slices_to_regions(
    ModelVolumePtrs                                           model_volumes,
    const PrintObjectRegions                                 &print_object_regions,
    const std::vector<float>                                 &zs,
    std::vector<VolumeSlices>                               &&volume_slices,
    // If clipping is disabled, then ExPolygons produced by different volumes will never be merged, thus they will be allowed to overlap.
    // It is up to the model designer to handle these overlaps.
    const bool                                                clip_multipart_objects,
    const std::function<void()>                              &throw_on_cancel_callback)
{
    model_volumes_sort_by_id(model_volumes);

    std::vector<std::vector<ExPolygons>> slices_by_region(print_object_regions.all_regions.size(), std::vector<ExPolygons>(zs.size(), ExPolygons()));

    // First shuffle slices into regions if there is no overlap with another region possible, collect zs of the complex cases.
    std::vector<std::pair<size_t, float>> zs_complex;
    {
        size_t z_idx = 0;
        for (const PrintObjectRegions::LayerRangeRegions &layer_range : print_object_regions.layer_ranges) {
            for (; z_idx < zs.size() && zs[z_idx] < layer_range.layer_height_range.first; ++ z_idx) ;
            if (layer_range.volume_regions.empty()) {
            } else if (layer_range.volume_regions.size() == 1) {
                const ModelVolume *model_volume = layer_range.volume_regions.front().model_volume;
                assert(model_volume != nullptr);
                if (model_volume->is_model_part()) {
                    VolumeSlices &slices_src = volume_slices_find_by_id(volume_slices, model_volume->id());
                    auto         &slices_dst = slices_by_region[layer_range.volume_regions.front().region->print_object_region_id()];
                    for (; z_idx < zs.size() && zs[z_idx] < layer_range.layer_height_range.second; ++ z_idx)
                        slices_dst[z_idx] = std::move(slices_src.slices[z_idx]);
                }
            } else {
                zs_complex.reserve(zs.size());
                for (; z_idx < zs.size() && zs[z_idx] < layer_range.layer_height_range.second; ++ z_idx) {
                    float z                          = zs[z_idx];
                    int   idx_first_printable_region = -1;
                    bool  complex                    = false;
                    for (int idx_region = 0; idx_region < int(layer_range.volume_regions.size()); ++ idx_region) {
                        const PrintObjectRegions::VolumeRegion &region = layer_range.volume_regions[idx_region];
                        if (region.bbox->min().z() <= z && region.bbox->max().z() >= z) {
                            if (idx_first_printable_region == -1 && region.model_volume->is_model_part())
                                idx_first_printable_region = idx_region;
                            else if (idx_first_printable_region != -1) {
                                // Test for overlap with some other region.
                                for (int idx_region2 = idx_first_printable_region; idx_region2 < idx_region; ++ idx_region2) {
                                    const PrintObjectRegions::VolumeRegion &region2 = layer_range.volume_regions[idx_region2];
                                    if (region2.bbox->min().z() <= z && region2.bbox->max().z() >= z && overlap_in_xy(*region.bbox, *region2.bbox)) {
                                        complex = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    if (complex)
                        zs_complex.push_back({ z_idx, z });
                    else if (idx_first_printable_region >= 0) {
                        const PrintObjectRegions::VolumeRegion &region = layer_range.volume_regions[idx_first_printable_region];
                        slices_by_region[region.region->print_object_region_id()][z_idx] = std::move(volume_slices_find_by_id(volume_slices, region.model_volume->id()).slices[z_idx]);
                    }
                }
            }
            throw_on_cancel_callback();
        }
    }

    // Second perform region clipping and assignment in parallel.
    if (! zs_complex.empty()) {
        std::vector<std::vector<VolumeSlices*>> layer_ranges_regions_to_slices(print_object_regions.layer_ranges.size(), std::vector<VolumeSlices*>());
        for (const PrintObjectRegions::LayerRangeRegions &layer_range : print_object_regions.layer_ranges) {
            std::vector<VolumeSlices*> &layer_range_regions_to_slices = layer_ranges_regions_to_slices[&layer_range - print_object_regions.layer_ranges.data()];
            layer_range_regions_to_slices.reserve(layer_range.volume_regions.size());
            for (const PrintObjectRegions::VolumeRegion &region : layer_range.volume_regions)
                layer_range_regions_to_slices.push_back(&volume_slices_find_by_id(volume_slices, region.model_volume->id()));
        }
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, zs_complex.size()),
            [&slices_by_region, &print_object_regions, &zs_complex, &layer_ranges_regions_to_slices, clip_multipart_objects, &throw_on_cancel_callback]
                (const tbb::blocked_range<size_t> &range) {
                float z              = zs_complex[range.begin()].second;
                auto  it_layer_range = layer_range_first(print_object_regions.layer_ranges, z);
                // Per volume_regions slices at this Z height.
                struct RegionSlice { 
                    ExPolygons  expolygons;
                    // Identifier of this region in PrintObjectRegions::all_regions
                    int         region_id;
                    ObjectID    volume_id;
                    bool operator<(const RegionSlice &rhs) const {
                        bool this_empty = this->region_id < 0 || this->expolygons.empty();
                        bool rhs_empty  = rhs.region_id < 0 || rhs.expolygons.empty();
                        // Sort the empty items to the end of the list.
                        // Sort by region_id & volume_id lexicographically.
                        return ! this_empty && (rhs_empty || (this->region_id < rhs.region_id || (this->region_id == rhs.region_id && volume_id < volume_id)));
                    }
                };
                std::vector<RegionSlice> temp_slices;
                for (size_t zs_complex_idx = range.begin(); zs_complex_idx < range.end(); ++ zs_complex_idx) {
                    auto [z_idx, z] = zs_complex[zs_complex_idx];
                    it_layer_range = layer_range_next(print_object_regions.layer_ranges, it_layer_range, z);
                    const PrintObjectRegions::LayerRangeRegions &layer_range = *it_layer_range;
                    {
                        std::vector<VolumeSlices*> &layer_range_regions_to_slices = layer_ranges_regions_to_slices[it_layer_range - print_object_regions.layer_ranges.begin()];
                        // Per volume_regions slices at thiz Z height.
                        temp_slices.clear();
                        temp_slices.reserve(layer_range.volume_regions.size());
                        for (VolumeSlices* &slices : layer_range_regions_to_slices) {
                            const PrintObjectRegions::VolumeRegion &volume_region = layer_range.volume_regions[&slices - layer_range_regions_to_slices.data()];
                            temp_slices.push_back({ std::move(slices->slices[z_idx]), volume_region.region ? volume_region.region->print_object_region_id() : -1, volume_region.model_volume->id() });
                        }
                    }
                    for (int idx_region = 0; idx_region < int(layer_range.volume_regions.size()); ++ idx_region)
                        if (! temp_slices[idx_region].expolygons.empty()) {
                            const PrintObjectRegions::VolumeRegion &region = layer_range.volume_regions[idx_region];
                            if (region.model_volume->is_modifier()) {
                                assert(region.parent > -1);
                                bool next_region_same_modifier = idx_region + 1 < int(temp_slices.size()) && layer_range.volume_regions[idx_region + 1].model_volume == region.model_volume;
                                RegionSlice &parent_slice = temp_slices[region.parent];
                                RegionSlice &this_slice   = temp_slices[idx_region];
                                ExPolygons   source       = std::move(this_slice.expolygons);
                                if (parent_slice.expolygons.empty()) {
                                    this_slice  .expolygons.clear();
                                } else {
                                    this_slice  .expolygons = intersection_ex(parent_slice.expolygons, source);
                                    parent_slice.expolygons = diff_ex        (parent_slice.expolygons, source);
                                }
                                if (next_region_same_modifier)
                                    // To be used in the following iteration.
                                    temp_slices[idx_region + 1].expolygons = std::move(source);
                            } else if ((region.model_volume->is_model_part() && clip_multipart_objects) || region.model_volume->is_negative_volume()) {
                                // Clip every non-zero region preceding it.
                                for (int idx_region2 = 0; idx_region2 < idx_region; ++ idx_region2)
                                    if (! temp_slices[idx_region2].expolygons.empty()) {
                                        if (const PrintObjectRegions::VolumeRegion &region2 = layer_range.volume_regions[idx_region2];
                                            ! region2.model_volume->is_negative_volume() && overlap_in_xy(*region.bbox, *region2.bbox))
                                            temp_slices[idx_region2].expolygons = diff_ex(temp_slices[idx_region2].expolygons, temp_slices[idx_region].expolygons);
                                    }
                            }
                        }
                    // Sort by region_id, push empty slices to the end.
                    std::sort(temp_slices.begin(), temp_slices.end());
                    // Remove the empty slices.
                    temp_slices.erase(std::find_if(temp_slices.begin(), temp_slices.end(), [](const auto &slice) { return slice.region_id == -1 || slice.expolygons.empty(); }), temp_slices.end());
                    // Merge slices and store them to the output.
                    for (int i = 0; i < int(temp_slices.size());) {
                        // Find a range of temp_slices with the same region_id.
                        int j = i;
                        bool merged = false;
                        ExPolygons &expolygons = temp_slices[i].expolygons;
                        for (++ j; 
                             j < int(temp_slices.size()) && 
                                temp_slices[i].region_id == temp_slices[j].region_id && 
                                (clip_multipart_objects || temp_slices[i].volume_id == temp_slices[j].volume_id); 
                             ++ j)
                            if (ExPolygons &expolygons2 = temp_slices[j].expolygons; ! expolygons2.empty()) {
                                if (expolygons.empty()) {
                                    expolygons = std::move(expolygons2);
                                } else {
                                    append(expolygons, std::move(expolygons2));
                                    merged = true;
                                }
                            }
                        if (merged)
                            expolygons = offset2_ex(expolygons, float(scale_(EPSILON)), -float(scale_(EPSILON)));
                        slices_by_region[temp_slices[i].region_id][z_idx] = std::move(expolygons);
                        i = j;
                    }
                    throw_on_cancel_callback();
                }
            });
    }

    return slices_by_region;
}

std::string fix_slicing_errors(LayerPtrs &layers, const std::function<void()> &throw_if_canceled)
{
    // Collect layers with slicing errors.
    // These layers will be fixed in parallel.
    std::vector<size_t> buggy_layers;
    buggy_layers.reserve(layers.size());
    for (size_t idx_layer = 0; idx_layer < layers.size(); ++ idx_layer)
        if (layers[idx_layer]->slicing_errors)
            buggy_layers.push_back(idx_layer);

    BOOST_LOG_TRIVIAL(debug) << "Slicing objects - fixing slicing errors in parallel - begin";
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, buggy_layers.size()),
        [&layers, &throw_if_canceled, &buggy_layers](const tbb::blocked_range<size_t>& range) {
            for (size_t buggy_layer_idx = range.begin(); buggy_layer_idx < range.end(); ++ buggy_layer_idx) {
                throw_if_canceled();
                size_t idx_layer = buggy_layers[buggy_layer_idx];
                Layer *layer     = layers[idx_layer];
                assert(layer->slicing_errors);
                // Try to repair the layer surfaces by merging all contours and all holes from neighbor layers.
                // BOOST_LOG_TRIVIAL(trace) << "Attempting to repair layer" << idx_layer;
                for (size_t region_id = 0; region_id < layer->region_count(); ++ region_id) {
                    LayerRegion *layerm = layer->get_region(region_id);
                    // Find the first valid layer below / above the current layer.
                    const Surfaces *upper_surfaces = nullptr;
                    const Surfaces *lower_surfaces = nullptr;
                    for (size_t j = idx_layer + 1; j < layers.size(); ++ j)
                        if (! layers[j]->slicing_errors) {
                            upper_surfaces = &layers[j]->regions()[region_id]->slices.surfaces;
                            break;
                        }
                    for (int j = int(idx_layer) - 1; j >= 0; -- j)
                        if (! layers[j]->slicing_errors) {
                            lower_surfaces = &layers[j]->regions()[region_id]->slices.surfaces;
                            break;
                        }
                    // Collect outer contours and holes from the valid layers above & below.
                    Polygons outer;
                    outer.reserve(
                        ((upper_surfaces == nullptr) ? 0 : upper_surfaces->size()) + 
                        ((lower_surfaces == nullptr) ? 0 : lower_surfaces->size()));
                    size_t num_holes = 0;
                    if (upper_surfaces)
                        for (const auto &surface : *upper_surfaces) {
                            outer.push_back(surface.expolygon.contour);
                            num_holes += surface.expolygon.holes.size();
                        }
                    if (lower_surfaces)
                        for (const auto &surface : *lower_surfaces) {
                            outer.push_back(surface.expolygon.contour);
                            num_holes += surface.expolygon.holes.size();
                        }
                    Polygons holes;
                    holes.reserve(num_holes);
                    if (upper_surfaces)
                        for (const auto &surface : *upper_surfaces)
                            polygons_append(holes, surface.expolygon.holes);
                    if (lower_surfaces)
                        for (const auto &surface : *lower_surfaces)
                            polygons_append(holes, surface.expolygon.holes);
                    layerm->slices.set(diff_ex(union_(outer), holes), stInternal);
                }
                // Update layer slices after repairing the single regions.
                layer->make_slices();
            }
        });
    throw_if_canceled();
    BOOST_LOG_TRIVIAL(debug) << "Slicing objects - fixing slicing errors in parallel - end";

    // remove empty layers from bottom
    while (! layers.empty() && (layers.front()->lslices.empty() || layers.front()->empty())) {
        delete layers.front();
        layers.erase(layers.begin());
        layers.front()->lower_layer = nullptr;
        for (size_t i = 0; i < layers.size(); ++ i)
            layers[i]->set_id(layers[i]->id() - 1);
    }

    return buggy_layers.empty() ? "" :
        "The model has overlapping or self-intersecting facets. I tried to repair it, "
        "however you might want to check the results or repair the input file and retry.\n";
}

// Called by make_perimeters()
// 1) Decides Z positions of the layers,
// 2) Initializes layers and their regions
// 3) Slices the object meshes
// 4) Slices the modifier meshes and reclassifies the slices of the object meshes by the slices of the modifier meshes
// 5) Applies size compensation (offsets the slices in XY plane)
// 6) Replaces bad slices by the slices reconstructed from the upper/lower layer
// Resulting expolygons of layer regions are marked as Internal.
void PrintObject::slice()
{
    if (! this->set_started(posSlice))
        return;
    m_print->set_status(10, L("Processing triangulated mesh"));
    std::vector<coordf_t> layer_height_profile;
    this->update_layer_height_profile(*this->model_object(), m_slicing_params, layer_height_profile);
    m_print->throw_if_canceled();
    m_typed_slices = false;
    this->clear_layers();
    m_layers = new_layers(this, generate_object_layers(m_slicing_params, layer_height_profile));
    this->slice_volumes();
    m_print->throw_if_canceled();
    // Fix the model.
    //FIXME is this the right place to do? It is done repeateadly at the UI and now here at the backend.
    std::string warning = fix_slicing_errors(m_layers, [this](){ m_print->throw_if_canceled(); });
    m_print->throw_if_canceled();
    if (! warning.empty())
        BOOST_LOG_TRIVIAL(info) << warning;
    // Update bounding boxes, back up raw slices of complex models.
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, m_layers.size()),
        [this](const tbb::blocked_range<size_t>& range) {
            for (size_t layer_idx = range.begin(); layer_idx < range.end(); ++ layer_idx) {
                m_print->throw_if_canceled();
                Layer &layer = *m_layers[layer_idx];
                layer.lslices_bboxes.clear();
                layer.lslices_bboxes.reserve(layer.lslices.size());
                for (const ExPolygon &expoly : layer.lslices)
                	layer.lslices_bboxes.emplace_back(get_extents(expoly));
                layer.backup_untyped_slices();
            }
        });
    if (m_layers.empty())
        throw Slic3r::SlicingError("No layers were detected. You might want to repair your STL file(s) or check their size or thickness and retry.\n");    
    this->set_done(posSlice);
}

template<typename ThrowOnCancel>
static inline void apply_mm_segmentation(PrintObject &print_object, ThrowOnCancel throw_on_cancel)
{
    // Returns MMU segmentation based on painting in MMU segmentation gizmo
    std::vector<std::vector<std::pair<ExPolygon, size_t>>> segmentation = multi_material_segmentation_by_painting(print_object, throw_on_cancel);
    assert(segmentation.size() == print_object.layer_count());
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, segmentation.size(), std::max(segmentation.size() / 128, size_t(1))),
        [&print_object, &segmentation, throw_on_cancel](const tbb::blocked_range<size_t> &range) {
            const auto  &layer_ranges   = print_object.shared_regions()->layer_ranges;
            double       z              = print_object.get_layer(range.begin())->slice_z;
            auto         it_layer_range = layer_range_first(layer_ranges, z);
            const size_t num_extruders = print_object.print()->config().nozzle_diameter.size();
            struct ByExtruder {
                ExPolygons  expolygons;
                BoundingBox bbox;
            };
            std::vector<ByExtruder> by_extruder;
            struct ByRegion {
                ExPolygons  expolygons;
                bool        needs_merge { false };
            };
            std::vector<ByRegion> by_region;
            for (size_t layer_id = range.begin(); layer_id < range.end(); ++ layer_id) {
                throw_on_cancel();
                Layer *layer = print_object.get_layer(layer_id);
                it_layer_range = layer_range_next(layer_ranges, it_layer_range, layer->slice_z);
                const PrintObjectRegions::LayerRangeRegions &layer_range = *it_layer_range;
                // Gather per extruder expolygons.
                by_extruder.assign(num_extruders, ByExtruder());
                by_region.assign(layer->region_count(), ByRegion());
                bool layer_split = false;
                for (size_t extruder_id = 0; extruder_id < num_extruders; ++ extruder_id) {
                    ByExtruder &region = by_extruder[extruder_id];
                    for (const std::pair<ExPolygon, size_t> &colored_polygon : segmentation[layer_id])
                        if (colored_polygon.second == extruder_id)
                            region.expolygons.emplace_back(std::move(colored_polygon.first));
                    if (! region.expolygons.empty()) {
                        region.bbox = get_extents(region.expolygons);
                        layer_split = true;
                    }
                }
                if (! layer_split)
                    continue;
                // Split LayerRegions by by_extruder regions.
                auto it_painted_region = layer_range.painted_regions.begin();
                for (int region_id = 0; region_id < int(layer->region_count()); ++ region_id)
                    if (LayerRegion &layerm = *layer->get_region(region_id); ! layerm.slices.surfaces.empty()) {
                        const BoundingBox bbox = get_extents(layerm.slices.surfaces);
                        assert(it_painted_region < layer_range.painted_regions.end());
                        for (; layer_range.volume_regions[it_painted_region->parent].region->print_object_region_id() < region_id; ++ it_painted_region)
                            assert(it_painted_region < layer_range.painted_regions.end());
                        assert(&layerm.region() == it_painted_region->region && layerm.region().print_object_region_id() == region_id);
                        // 1-based extruder ID
                        bool   self_trimmed = false;
                        int    self_extruder_id = -1;
                        for (int extruder_id = 1; extruder_id <= int(by_extruder.size()); ++ extruder_id)
                            if (ByExtruder &segmented = by_extruder[extruder_id - 1]; segmented.bbox.defined && bbox.overlap(segmented.bbox)) {
                                // Find the target region.
                                for (; int(it_painted_region->extruder_id) < extruder_id; ++ it_painted_region)
                                    assert(it_painted_region < layer_range.painted_regions.end());
                                assert(layer_range.volume_regions[it_painted_region->parent].region == &layerm.region() && int(it_painted_region->extruder_id) == extruder_id);
                                //FIXME Don't trim by self, it is not reliable.
                                if (&layerm.region() == it_painted_region->region) {
                                    self_extruder_id = extruder_id;
                                    continue;
                                }
                                // Steal from this region.
                                int         target_region_id = it_painted_region->region->print_object_region_id();
                                ExPolygons  stolen           = intersection_ex(layerm.slices.surfaces, segmented.expolygons);
                                if (! stolen.empty()) {
                                    ByRegion &dst = by_region[target_region_id];
                                    if (dst.expolygons.empty()) {
                                        dst.expolygons = std::move(stolen);
                                    } else {
                                        append(dst.expolygons, std::move(stolen));
                                        dst.needs_merge = true;
                                    }
                                }
#if 0
                                if (&layerm.region() == it_painted_region->region)
                                    // Slices of this LayerRegion were trimmed by a MMU region of the same PrintRegion.
                                    self_trimmed = true;
#endif
                            }
                        if (! self_trimmed) {
                            // Trim slices of this LayerRegion with all the MMU regions.
                            Polygons mine = to_polygons(std::move(layerm.slices.surfaces));
                            for (auto &segmented : by_extruder)
                                if (&segmented - by_extruder.data() + 1 != self_extruder_id && segmented.bbox.defined && bbox.overlap(segmented.bbox)) {
                                    mine = diff(mine, segmented.expolygons);
                                    if (mine.empty())
                                        break;
                                }
                            if (! mine.empty()) {
                                ByRegion &dst = by_region[layerm.region().print_object_region_id()];
                                if (dst.expolygons.empty()) {
                                    dst.expolygons = union_ex(mine);
                                } else {
                                    append(dst.expolygons, union_ex(mine));
                                    dst.needs_merge = true;
                                }
                            }
                        }
                    }
                // Re-create Surfaces of LayerRegions.
                for (size_t region_id = 0; region_id < layer->region_count(); ++ region_id) {
                    ByRegion &src = by_region[region_id];
                    if (src.needs_merge)
                        // Multiple regions were merged into one.
                        src.expolygons = offset2_ex(src.expolygons, float(scale_(EPSILON)), - float(scale_(EPSILON)));
                    layer->get_region(region_id)->slices.set(std::move(src.expolygons), stInternal);
                }
            }
        });
}

// 1) Decides Z positions of the layers,
// 2) Initializes layers and their regions
// 3) Slices the object meshes
// 4) Slices the modifier meshes and reclassifies the slices of the object meshes by the slices of the modifier meshes
// 5) Applies size compensation (offsets the slices in XY plane)
// 6) Replaces bad slices by the slices reconstructed from the upper/lower layer
// Resulting expolygons of layer regions are marked as Internal.
//
// this should be idempotent
void PrintObject::slice_volumes()
{
    BOOST_LOG_TRIVIAL(info) << "Slicing volumes..." << log_memory_info();
    const Print *print                      = this->print();
    const auto   throw_on_cancel_callback   = std::function<void()>([print](){ print->throw_if_canceled(); });

    // Clear old LayerRegions, allocate for new PrintRegions.
    for (Layer* layer : m_layers) {
        layer->m_regions.clear();
        layer->m_regions.reserve(m_shared_regions->all_regions.size());
        for (const std::unique_ptr<PrintRegion> &pr : m_shared_regions->all_regions)
            layer->m_regions.emplace_back(new LayerRegion(layer, pr.get()));
    }

    std::vector<float>                   slice_zs      = zs_from_layers(m_layers);
    Transform3d                          trafo         = this->trafo();
    trafo.pretranslate(Vec3d(- unscale<float>(m_center_offset.x()), - unscale<float>(m_center_offset.y()), 0));
    std::vector<std::vector<ExPolygons>> region_slices = slices_to_regions(this->model_object()->volumes, *m_shared_regions, slice_zs,
        slice_volumes_inner(
            print->config(), this->config(), trafo,
            this->model_object()->volumes, m_shared_regions->layer_ranges, slice_zs, throw_on_cancel_callback),
        m_config.clip_multipart_objects,
        throw_on_cancel_callback);

    for (size_t region_id = 0; region_id < region_slices.size(); ++ region_id) {
        std::vector<ExPolygons> &by_layer = region_slices[region_id];
        for (size_t layer_id = 0; layer_id < by_layer.size(); ++ layer_id)
            m_layers[layer_id]->regions()[region_id]->slices.append(std::move(by_layer[layer_id]), stInternal);
    }
    region_slices.clear();
    
    BOOST_LOG_TRIVIAL(debug) << "Slicing volumes - removing top empty layers";
    while (! m_layers.empty()) {
        const Layer *layer = m_layers.back();
        if (! layer->empty())
            break;
        delete layer;
        m_layers.pop_back();
    }
    if (! m_layers.empty())
        m_layers.back()->upper_layer = nullptr;
    m_print->throw_if_canceled();

    // Is any ModelVolume MMU painted?
    if (const auto& volumes = this->model_object()->volumes;
        std::find_if(volumes.begin(), volumes.end(), [](const ModelVolume* v) { return !v->mmu_segmentation_facets.empty(); }) != volumes.end()) {
        BOOST_LOG_TRIVIAL(debug) << "Slicing volumes - MMU segmentation";
        apply_mm_segmentation(*this, [print]() { print->throw_if_canceled(); });
    }


    BOOST_LOG_TRIVIAL(debug) << "Slicing volumes - make_slices in parallel - begin";
    {
        // Compensation value, scaled. Only applying the negative scaling here, as the positive scaling has already been applied during slicing.
        const auto  xy_compensation_scaled              = scaled<float>(std::min(m_config.xy_size_compensation.value, 0.));
        const float elephant_foot_compensation_scaled 	= (m_config.raft_layers == 0) ? 
        	// Only enable Elephant foot compensation if printing directly on the print bed.
            float(scale_(m_config.elefant_foot_compensation.value)) :
        	0.f;
        // Uncompensated slices for the first layer in case the Elephant foot compensation is applied.
	    ExPolygons  lslices_1st_layer;
	    tbb::parallel_for(
	        tbb::blocked_range<size_t>(0, m_layers.size()),
			[this, xy_compensation_scaled, elephant_foot_compensation_scaled, &lslices_1st_layer](const tbb::blocked_range<size_t>& range) {
	            for (size_t layer_id = range.begin(); layer_id < range.end(); ++ layer_id) {
	                m_print->throw_if_canceled();
	                Layer *layer = m_layers[layer_id];
	                // Apply size compensation and perform clipping of multi-part objects.
	                float elfoot = (layer_id == 0) ? elephant_foot_compensation_scaled : 0.f;
	                if (layer->m_regions.size() == 1) {
	                    // Optimized version for a single region layer.
	                    // Single region, growing or shrinking.
	                    LayerRegion *layerm = layer->m_regions.front();
	                    if (elfoot > 0) {
		                    // Apply the elephant foot compensation and store the 1st layer slices without the Elephant foot compensation applied.
		                    lslices_1st_layer = to_expolygons(std::move(layerm->slices.surfaces));
		                    float delta = xy_compensation_scaled;
	                        if (delta > elfoot) {
	                            delta -= elfoot;
	                            elfoot = 0.f;
	                        } else if (delta > 0)
	                            elfoot -= delta;
							layerm->slices.set(
								union_ex(
									Slic3r::elephant_foot_compensation(
										(delta == 0.f) ? lslices_1st_layer : offset_ex(lslices_1st_layer, delta), 
	                            		layerm->flow(frExternalPerimeter), unscale<double>(elfoot))),
								stInternal);
							if (xy_compensation_scaled < 0.f)
								lslices_1st_layer = offset_ex(std::move(lslices_1st_layer), xy_compensation_scaled);
	                    } else if (xy_compensation_scaled < 0.f) {
	                        // Apply the XY compensation.
	                        layerm->slices.set(
                                offset_ex(to_expolygons(std::move(layerm->slices.surfaces)), xy_compensation_scaled),
	                            stInternal);
	                    }
	                } else {
	                    if (xy_compensation_scaled < 0.f || elfoot > 0.f) {
	                        // Apply the negative XY compensation.
	                        Polygons trimming;
	                        static const float eps = float(scale_(m_config.slice_closing_radius.value) * 1.5);
	                        if (elfoot > 0.f) {
	                        	lslices_1st_layer = offset_ex(layer->merged(eps), std::min(xy_compensation_scaled, 0.f) - eps);
								trimming = to_polygons(Slic3r::elephant_foot_compensation(lslices_1st_layer,
									layer->m_regions.front()->flow(frExternalPerimeter), unscale<double>(elfoot)));
	                        } else
		                        trimming = offset(layer->merged(float(SCALED_EPSILON)), xy_compensation_scaled - float(SCALED_EPSILON));
	                        for (size_t region_id = 0; region_id < layer->m_regions.size(); ++ region_id)
	                            layer->m_regions[region_id]->trim_surfaces(trimming);
	                    }
	                }
	                // Merge all regions' slices to get islands, chain them by a shortest path.
	                layer->make_slices();
	            }
	        });
	    if (elephant_foot_compensation_scaled > 0.f && ! m_layers.empty()) {
	    	// The Elephant foot has been compensated, therefore the 1st layer's lslices are shrank with the Elephant foot compensation value.
	    	// Store the uncompensated value there.
	    	assert(m_layers.front()->id() == 0);
			m_layers.front()->lslices = std::move(lslices_1st_layer);
		}
	}

    m_print->throw_if_canceled();
    BOOST_LOG_TRIVIAL(debug) << "Slicing volumes - make_slices in parallel - end";
}

std::vector<ExPolygons> PrintObject::slice_support_volumes(const ModelVolumeType model_volume_type) const
{
    auto it_volume     = this->model_object()->volumes.begin();
    auto it_volume_end = this->model_object()->volumes.end();
    for (; it_volume != it_volume_end && (*it_volume)->type() != model_volume_type; ++ it_volume) ;
    std::vector<ExPolygons> slices;
    if (it_volume != it_volume_end) {
        // Found at least a single support volume of model_volume_type.
        std::vector<float> zs = zs_from_layers(this->layers());
        std::vector<char>  merge_layers;
        bool               merge = false;
        const Print       *print = this->print();
        auto               throw_on_cancel_callback = std::function<void()>([print](){ print->throw_if_canceled(); });
        MeshSlicingParamsEx params;
        params.trafo = this->trafo();
        params.trafo.pretranslate(Vec3d(-unscale<float>(m_center_offset.x()), -unscale<float>(m_center_offset.y()), 0));
        for (; it_volume != it_volume_end; ++ it_volume)
            if ((*it_volume)->type() == model_volume_type) {
                std::vector<ExPolygons> slices2 = slice_volume(*(*it_volume), zs, params, throw_on_cancel_callback);
                if (slices.empty())
                    slices = std::move(slices2);
                else if (! slices2.empty()) {
                    if (merge_layers.empty())
                        merge_layers.assign(zs.size(), false);
                    for (size_t i = 0; i < zs.size(); ++ i) {
                        if (slices[i].empty())
                            slices[i] = std::move(slices2[i]);
                        else if (! slices2[i].empty()) {
                            append(slices[i], std::move(slices2[i]));
                            merge_layers[i] = true;
                            merge = true;
                        }
                    }
                }
            }
        if (merge) {
            std::vector<ExPolygons*> to_merge;
            to_merge.reserve(zs.size());
            for (size_t i = 0; i < zs.size(); ++ i)
                if (merge_layers[i])
                    to_merge.emplace_back(&slices[i]);
            tbb::parallel_for(
                tbb::blocked_range<size_t>(0, to_merge.size()),
                [&to_merge](const tbb::blocked_range<size_t> &range) {
                    for (size_t i = range.begin(); i < range.end(); ++ i)
                        *to_merge[i] = union_ex(*to_merge[i]);
            });
        }
    }
    return slices;
}

} // namespace Slic3r
