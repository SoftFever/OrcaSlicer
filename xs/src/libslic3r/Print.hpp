#ifndef slic3r_Print_hpp_
#define slic3r_Print_hpp_

#include "libslic3r.h"
#include <set>
#include <vector>
#include <string>
#include <functional>
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
// tbb/mutex.h includes Windows, which in turn defines min/max macros. Convince Windows.h to not define these min/max macros.
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include "tbb/mutex.h"

namespace Slic3r {

class Print;
class PrintObject;
class ModelObject;
class GCode;
class GCodePreviewData;

// Print step IDs for keeping track of the print state.
enum PrintStep {
    psSkirt, psBrim, psWipeTower, psGCodeExport, psCount,
};
enum PrintObjectStep {
    posSlice, posPerimeters, posPrepareInfill,
    posInfill, posSupportMaterial, posCount,
};

class CanceledException : public std::exception {
public:
   const char* what() const throw() { return "Background processing has been canceled"; }
};

// To be instantiated over PrintStep or PrintObjectStep enums.
template <class StepType, size_t COUNT>
class PrintState
{
public:
    PrintState() { for (size_t i = 0; i < COUNT; ++ i) m_state[i] = INVALID; }

    enum State {
        INVALID,
        STARTED,
        DONE,
    };
    
    bool is_done(StepType step) const { return m_state[step] == DONE; }
    // set_started() will lock the provided mutex before setting the state.
    // This is necessary to block until the Print::apply_config() updates its state, which may
    // influence the processing step being entered.
    void set_started(StepType step, tbb::mutex &mtx) { mtx.lock(); m_state[step] = STARTED; mtx.unlock(); }
    void set_done(StepType step) { m_state[step] = DONE; }
    bool invalidate(StepType step) {
        bool invalidated = m_state[step] != INVALID;
        m_state[step] = INVALID;
        return invalidated;
    }
    bool invalidate_all() {
        bool invalidated = false;
        for (size_t i = 0; i < COUNT; ++ i)
            if (m_state[i] != INVALID) {
                invalidated = true;
                m_state[i] = INVALID;
                break;
            }
        return invalidated;
    }

private:
    std::atomic<State>          m_state[COUNT];
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
    bool invalidate_all_steps() { return m_state.invalidate_all(); }
    bool is_step_done(PrintObjectStep step) const { return m_state.is_done(step); }

    // To be used over the layer_height_profile of both the PrintObject and ModelObject
    // to initialize the height profile with the height ranges.
    bool update_layer_height_profile(std::vector<coordf_t> &layer_height_profile) const;

    // Process layer_height_ranges, the raft layers and first layer thickness into layer_height_profile.
    // The layer_height_profile may be later modified interactively by the user to refine layers at sloping surfaces.
    bool update_layer_height_profile();

    void reset_layer_height_profile();

    // Collect the slicing parameters, to be used by variable layer thickness algorithm,
    // by the interactive layer height editor and by the printing process itself.
    // The slicing parameters are dependent on various configuration values
    // (layer height, first layer height, raft settings, print nozzle diameter etc).
    SlicingParameters slicing_parameters() const;

    // Called when slicing to SVG (see Print.pm sub export_svg), and used by perimeters.t
    void slice();

private:
    void make_perimeters();
    void prepare_infill();
    void infill();
    void generate_support_material();

    void _slice();
    std::string _fix_slicing_errors();
    void _simplify_slices(double distance);
    bool has_support_material() const;
    void detect_surfaces_type();
    void process_external_surfaces();
    void discover_vertical_shells();
    void bridge_over_infill();
    void clip_fill_surfaces();
    void discover_horizontal_shells();
    void combine_infill();
    void _generate_support_material();

    Print* _print;
    ModelObject* _model_object;
    Points _copies;      // Slic3r::Point objects in scaled G-code coordinates

    PrintState<PrintObjectStep, posCount>   m_state;
    // Mutex used for synchronization of the worker thread with the UI thread:
    // The mutex will be used to guard the worker thread against entering a stage
    // while the data influencing the stage is modified.
    tbb::mutex                              m_mutex;

    // TODO: call model_object->get_bounding_box() instead of accepting
        // parameter
    PrintObject(Print* print, ModelObject* model_object, const BoundingBoxf3 &modobj_bbox);
    ~PrintObject() {}

    void set_started(PrintObjectStep step) { m_state.set_started(step, m_mutex); }
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
    std::string                     estimated_print_time;
    double                          total_used_filament, total_extruded_volume, total_cost, total_weight;
    std::map<size_t, float>         filament_stats;

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
    bool invalidate_all_steps() { return m_state.invalidate_all(); }
    bool is_step_done(PrintStep step) const { return m_state.is_done(step); }
    bool is_step_done(PrintObjectStep step) const;

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

    void process();
    void export_gcode(const std::string &path_template, GCodePreviewData *preview_data);

    // Wipe tower support.
    bool has_wipe_tower() const;
    // Tool ordering of a non-sequential print has to be known to calculate the wipe tower.
    // Cache it here, so it does not need to be recalculated during the G-code generation.
    ToolOrdering m_tool_ordering;
    // Cache of tool changes per print layer.
    std::unique_ptr<WipeTower::ToolChangeResult>          m_wipe_tower_priming;
    std::vector<std::vector<WipeTower::ToolChangeResult>> m_wipe_tower_tool_changes;
    std::unique_ptr<WipeTower::ToolChangeResult>          m_wipe_tower_final_purge;

    std::string output_filename();
    std::string output_filepath(const std::string &path);

    typedef std::function<void(int, const std::string&)>  status_callback_type;
    // Default status console print out in the form of percent => message.
    void set_status_default() { m_status_callback = nullptr; }
    // No status output or callback whatsoever, useful mostly for automatic tests.
    void set_status_silent() { m_status_callback = [](int, const std::string&){}; }
    // Register a custom status callback.
    void set_status_callback(status_callback_type cb) { m_status_callback = cb; }
    // Calls a registered callback to update the status, or print out the default message.
    void set_status(int percent, const std::string &message) { 
        if (m_status_callback) m_status_callback(percent, message);
        else printf("%d => %s\n", percent, message.c_str());
    }
    // Cancel the running computation. Stop execution of all the background threads.
    void cancel() { m_canceled = true; }
    // Cancel the running computation. Stop execution of all the background threads.
    void restart() { m_canceled = false; }
    // Has the calculation been canceled?
    bool canceled() { return m_canceled; }
    void throw_if_canceled() { if (m_canceled) throw CanceledException(); }

protected:
    void set_started(PrintStep step) { m_state.set_started(step, m_mutex); }
    void set_done(PrintStep step) { m_state.set_done(step); }

private:
    bool invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys);
    PrintRegionConfig _region_config_from_model_volume(const ModelVolume &volume);

    void _make_skirt();
    void _make_brim();
    void _clear_wipe_tower();
    void _make_wipe_tower();

    PrintState<PrintStep, psCount>          m_state;
    // Mutex used for synchronization of the worker thread with the UI thread:
    // The mutex will be used to guard the worker thread against entering a stage
    // while the data influencing the stage is modified.
    tbb::mutex                              m_mutex;
    // Has the calculation been canceled?
    tbb::atomic<bool>                       m_canceled;
    // Callback to be evoked regularly to update state of the UI thread.
    status_callback_type                    m_status_callback;

    // To allow GCode to set the Print's GCodeExport step status.
    friend class GCode;
};

#define FOREACH_BASE(type, container, iterator) for (type::const_iterator iterator = (container).begin(); iterator != (container).end(); ++iterator)
#define FOREACH_REGION(print, region)       FOREACH_BASE(PrintRegionPtrs, (print)->regions, region)
#define FOREACH_OBJECT(print, object)       FOREACH_BASE(PrintObjectPtrs, (print)->objects, object)
#define FOREACH_LAYER(object, layer)        FOREACH_BASE(LayerPtrs, (object)->layers, layer)
#define FOREACH_LAYERREGION(layer, layerm)  FOREACH_BASE(LayerRegionPtrs, (layer)->regions, layerm)

}

#endif
