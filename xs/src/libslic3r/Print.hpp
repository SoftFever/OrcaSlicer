#ifndef slic3r_Print_hpp_
#define slic3r_Print_hpp_

#include "libslic3r.h"
#include <set>
#include <vector>
#include <string>
#include "BoundingBox.hpp"
#include "Flow.hpp"
#include "PrintConfig.hpp"
#include "Point.hpp"
#include "Layer.hpp"
#include "Model.hpp"
#include "PlaceholderParser.hpp"
#include "Slicing.hpp"
#include "GCode/ToolOrdering.hpp"
#include "GCode/WipeTower.hpp"

#include "tbb/atomic.h"

namespace Slic3r {

class Print;
class PrintObject;
class ModelObject;

// Print step IDs for keeping track of the print state.
enum PrintStep {
    psSkirt, psBrim, psWipeTower, psCount,
};
enum PrintObjectStep {
    posSlice, posPerimeters, posPrepareInfill,
    posInfill, posSupportMaterial, posCount,
};

// To be instantiated over PrintStep or PrintObjectStep enums.
template <class StepType, size_t COUNT>
class PrintState
{
public:
    PrintState() { memset(state, 0, sizeof(state)); }

    enum State {
        INVALID,
        STARTED,
        DONE,
    };
    State state[COUNT];
    
    bool is_started(StepType step) const { return this->state[step] == STARTED; }
    bool is_done(StepType step) const { return this->state[step] == DONE; }
    void set_started(StepType step) { this->state[step] = STARTED; }
    void set_done(StepType step) { this->state[step] = DONE; }
    bool invalidate(StepType step) {
        bool invalidated = this->state[step] != INVALID;
        this->state[step] = INVALID;
        return invalidated;
    }
    bool invalidate_all() {
        bool invalidated = false;
        for (size_t i = 0; i < COUNT; ++ i)
            if (this->state[i] != INVALID) {
                invalidated = true;
                break;
            }
        memset(state, 0, sizeof(state));
        return invalidated;
    }
};

// A PrintRegion object represents a group of volumes to print
// sharing the same config (including the same assigned extruder(s))
class PrintRegion
{
    friend class Print;

public:
    PrintRegionConfig config;

    Print* print() { return this->_print; }
    Flow flow(FlowRole role, double layer_height, bool bridge, bool first_layer, double width, const PrintObject &object) const;
    coordf_t nozzle_dmr_avg(const PrintConfig &print_config) const;

private:
    Print* _print;
    
    PrintRegion(Print* print) : _print(print) {}
    ~PrintRegion() {}
};


typedef std::vector<Layer*> LayerPtrs;
typedef std::vector<SupportLayer*> SupportLayerPtrs;
class BoundingBoxf3;        // TODO: for temporary constructor parameter

class PrintObject
{
    friend class Print;

public:
    // vector of (vectors of volume ids), indexed by region_id
    std::vector<std::vector<int>> region_volumes;
    PrintObjectConfig config;
    t_layer_height_ranges layer_height_ranges;

    // Profile of increasing z to a layer height, to be linearly interpolated when calculating the layers.
    // The pairs of <z, layer_height> are packed into a 1D array to simplify handling by the Perl XS.
    // layer_height_profile must not be set by the background thread.
    std::vector<coordf_t> layer_height_profile;
    // There is a layer_height_profile at both PrintObject and ModelObject. The layer_height_profile at the ModelObject
    // is used for interactive editing and for loading / storing into a project file (AMF file as of today).
    // This flag indicates that the layer_height_profile at the UI has been updated, therefore the backend needs to get it.
    // This flag is necessary as we cannot safely clear the layer_height_profile if the background calculation is running.
    bool                  layer_height_profile_valid;
    
    // this is set to true when LayerRegion->slices is split in top/internal/bottom
    // so that next call to make_perimeters() performs a union() before computing loops
    bool typed_slices;

    Point3 size;           // XYZ in scaled coordinates

    // scaled coordinates to add to copies (to compensate for the alignment
    // operated when creating the object but still preserving a coherent API
    // for external callers)
    Point _copies_shift;

    // Slic3r::Point objects in scaled G-code coordinates in our coordinates
    Points _shifted_copies;

    LayerPtrs                               layers;
    SupportLayerPtrs                        support_layers;
    PrintState<PrintObjectStep, posCount>   state;

    Print*              print()                 { return this->_print; }
    const Print*        print() const           { return this->_print; }
    ModelObject*        model_object()          { return this->_model_object; }
    const ModelObject*  model_object() const    { return this->_model_object; }

    const Points& copies() const { return this->_copies; }
    bool add_copy(const Pointf &point);
    bool delete_last_copy();
    bool delete_all_copies() { return this->set_copies(Points()); }
    bool set_copies(const Points &points);
    bool reload_model_instances();
    // since the object is aligned to origin, bounding box coincides with size
    BoundingBox bounding_box() const { return BoundingBox(Point(0,0), this->size); }

    // adds region_id, too, if necessary
    void add_region_volume(unsigned int region_id, int volume_id) {
        if (region_id >= region_volumes.size())
            region_volumes.resize(region_id + 1);
        region_volumes[region_id].push_back(volume_id);
    }
    // This is the *total* layer count (including support layers)
    // this value is not supposed to be compared with Layer::id
    // since they have different semantics.
    size_t total_layer_count() const { return this->layer_count() + this->support_layer_count(); }
    size_t layer_count() const { return this->layers.size(); }
    void clear_layers();
    Layer* get_layer(int idx) { return this->layers.at(idx); }
    const Layer* get_layer(int idx) const { return this->layers.at(idx); }

    // print_z: top of the layer; slice_z: center of the layer.
    Layer* add_layer(int id, coordf_t height, coordf_t print_z, coordf_t slice_z);

    size_t support_layer_count() const { return this->support_layers.size(); }
    void clear_support_layers();
    SupportLayer* get_support_layer(int idx) { return this->support_layers.at(idx); }
    SupportLayer* add_support_layer(int id, coordf_t height, coordf_t print_z);
    void delete_support_layer(int idx);
    
    // methods for handling state
    bool invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys);
    bool invalidate_step(PrintObjectStep step);
    bool invalidate_all_steps() { return this->state.invalidate_all(); }

    // To be used over the layer_height_profile of both the PrintObject and ModelObject
    // to initialize the height profile with the height ranges.
    bool update_layer_height_profile(std::vector<coordf_t> &layer_height_profile) const;

    // Process layer_height_ranges, the raft layers and first layer thickness into layer_height_profile.
    // The layer_height_profile may be later modified interactively by the user to refine layers at sloping surfaces.
    bool update_layer_height_profile();

    void reset_layer_height_profile();

    void adjust_layer_height_profile(coordf_t z, coordf_t layer_thickness_delta, coordf_t band_width, int action);

    // Collect the slicing parameters, to be used by variable layer thickness algorithm,
    // by the interactive layer height editor and by the printing process itself.
    // The slicing parameters are dependent on various configuration values
    // (layer height, first layer height, raft settings, print nozzle diameter etc).
    SlicingParameters slicing_parameters() const;

    void _slice();
    std::string _fix_slicing_errors();
    void _simplify_slices(double distance);
    void _prepare_infill();
    bool has_support_material() const;
    void detect_surfaces_type();
    void process_external_surfaces();
    void discover_vertical_shells();
    void bridge_over_infill();
    void _make_perimeters();
    void _infill();
    void clip_fill_surfaces();
    void discover_horizontal_shells();
    void combine_infill();
    void _generate_support_material();

private:
    Print* _print;
    ModelObject* _model_object;
    Points _copies;      // Slic3r::Point objects in scaled G-code coordinates

    // TODO: call model_object->get_bounding_box() instead of accepting
        // parameter
    PrintObject(Print* print, ModelObject* model_object, const BoundingBoxf3 &modobj_bbox);
    ~PrintObject() {}

    std::vector<ExPolygons> _slice_region(size_t region_id, const std::vector<float> &z, bool modifier);
};

typedef std::vector<PrintObject*> PrintObjectPtrs;
typedef std::vector<PrintRegion*> PrintRegionPtrs;

// The complete print tray with possibly multiple objects.
class Print
{
public:
    PrintConfig config;
    PrintObjectConfig default_object_config;
    PrintRegionConfig default_region_config;
    PrintObjectPtrs objects;
    PrintRegionPtrs regions;
    PlaceholderParser placeholder_parser;
    // TODO: status_cb
    std::string                     estimated_print_time;
    double                          total_used_filament, total_extruded_volume, total_cost, total_weight;
    std::map<size_t, float>         filament_stats;
    PrintState<PrintStep, psCount>  state;

    // ordered collections of extrusion paths to build skirt loops and brim
    ExtrusionEntityCollection skirt, brim;

    Print() : total_used_filament(0), total_extruded_volume(0) { restart(); }
    ~Print() { clear_objects(); }
    
    // methods for handling objects
    void clear_objects();
    PrintObject* get_object(size_t idx) { return objects.at(idx); }
    const PrintObject* get_object(size_t idx) const { return objects.at(idx); }

    void delete_object(size_t idx);
    void reload_object(size_t idx);
    bool reload_model_instances();

    // methods for handling regions
    PrintRegion* get_region(size_t idx) { return regions.at(idx); }
    const PrintRegion* get_region(size_t idx) const  { return regions.at(idx); }
    PrintRegion* add_region();
    
    // methods for handling state
    bool invalidate_step(PrintStep step);
    bool invalidate_all_steps() { return this->state.invalidate_all(); }
    bool step_done(PrintObjectStep step) const;
    
    void add_model_object(ModelObject* model_object, int idx = -1);
    bool apply_config(DynamicPrintConfig config);
    bool has_infinite_skirt() const;
    bool has_skirt() const;
    // Returns an empty string if valid, otherwise returns an error message.
    std::string validate() const;
    BoundingBox bounding_box() const;
    BoundingBox total_bounding_box() const;
    double skirt_first_layer_height() const;
    Flow brim_flow() const;
    Flow skirt_flow() const;
    
    std::vector<unsigned int> object_extruders() const;
    std::vector<unsigned int> support_material_extruders() const;
    std::vector<unsigned int> extruders() const;
    void _simplify_slices(double distance);
    double max_allowed_layer_height() const;
    bool has_support_material() const;
    void auto_assign_extruders(ModelObject* model_object) const;

    void _make_skirt();
    void _make_brim();

    // Wipe tower support.
    bool has_wipe_tower() const;
    void _clear_wipe_tower();
    void _make_wipe_tower();
    // Tool ordering of a non-sequential print has to be known to calculate the wipe tower.
    // Cache it here, so it does not need to be recalculated during the G-code generation.
    ToolOrdering m_tool_ordering;
    // Cache of tool changes per print layer.
    std::unique_ptr<WipeTower::ToolChangeResult>          m_wipe_tower_priming;
    std::vector<std::vector<WipeTower::ToolChangeResult>> m_wipe_tower_tool_changes;
    std::unique_ptr<WipeTower::ToolChangeResult>          m_wipe_tower_final_purge;

    std::string output_filename();
    std::string output_filepath(const std::string &path);

    // Calls a registered callback to update the status.
    void set_status(int percent, const std::string &message);
    // Cancel the running computation. Stop execution of all the background threads.
    void cancel() { m_canceled = true; }
    // Cancel the running computation. Stop execution of all the background threads.
    void restart() { m_canceled = false; }
    // Has the calculation been canceled?
    bool canceled() { return m_canceled; }
    
private:
    bool invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys);
    PrintRegionConfig _region_config_from_model_volume(const ModelVolume &volume);

    // Has the calculation been canceled?
    tbb::atomic<bool>   m_canceled;
};

#define FOREACH_BASE(type, container, iterator) for (type::const_iterator iterator = (container).begin(); iterator != (container).end(); ++iterator)
#define FOREACH_REGION(print, region)       FOREACH_BASE(PrintRegionPtrs, (print)->regions, region)
#define FOREACH_OBJECT(print, object)       FOREACH_BASE(PrintObjectPtrs, (print)->objects, object)
#define FOREACH_LAYER(object, layer)        FOREACH_BASE(LayerPtrs, (object)->layers, layer)
#define FOREACH_LAYERREGION(layer, layerm)  FOREACH_BASE(LayerRegionPtrs, (layer)->regions, layerm)

}

#endif
