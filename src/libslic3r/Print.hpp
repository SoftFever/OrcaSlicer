#ifndef slic3r_Print_hpp_
#define slic3r_Print_hpp_

#include "libslic3r.h"
#include <atomic>
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
    PrintState() { for (size_t i = 0; i < COUNT; ++ i) m_state[i].store(INVALID, std::memory_order_relaxed); }

    enum State {
        INVALID,
        STARTED,
        DONE,
    };
    
    // With full memory barrier.
    bool is_done(StepType step) const { return m_state[step] == DONE; }

    // Set the step as started. Block on mutex while the Print / PrintObject / PrintRegion objects are being
    // modified by the UI thread.
    // This is necessary to block until the Print::apply_config() updates its state, which may
    // influence the processing step being entered.
    void set_started(StepType step, tbb::mutex &mtx) {
        mtx.lock();
        m_state[step].store(STARTED, std::memory_order_relaxed);
        mtx.unlock();
    }

    // Set the step as done. Block on mutex while the Print / PrintObject / PrintRegion objects are being
    // modified by the UI thread.
    void set_done(StepType step, tbb::mutex &mtx) { 
        mtx.lock();
        m_state[step].store(DONE, std::memory_order_relaxed);
        mtx.unlock();
    }

    // Make the step invalid.
    // The provided mutex should be locked at this point, guarding access to m_state.
    // In case the step has already been entered or finished, cancel the background
    // processing by calling the cancel callback.
    template<typename CancelationCallback>
    bool invalidate(StepType step, tbb::mutex &mtx, CancelationCallback cancel) {
        bool invalidated = m_state[step].load(std::memory_order_relaxed) != INVALID;
        if (invalidated) {
#if 0
            if (mtx.state != mtx.HELD) {
                printf("Not held!\n");
            }
#endif
            // Raise the mutex, so that the following cancel() callback could cancel
            // the background processing.
            mtx.unlock();
            cancel();
			m_state[step] = INVALID;
            mtx.lock();
        }
        return invalidated;
    }

	template<typename CancelationCallback, typename StepTypeIterator>
	bool invalidate_multiple(StepTypeIterator step_begin, StepTypeIterator step_end, tbb::mutex &mtx, CancelationCallback cancel) {
		bool invalidated = false;
		for (StepTypeIterator it = step_begin; ! invalidated && it != step_end; ++ it)
			invalidated = m_state[*it].load(std::memory_order_relaxed) != INVALID;
		if (invalidated) {
#if 0
			if (mtx.state != mtx.HELD) {
				printf("Not held!\n");
			}
#endif
			// Raise the mutex, so that the following cancel() callback could cancel
			// the background processing.
			mtx.unlock();
			cancel();
			for (StepTypeIterator it = step_begin; it != step_end; ++ it)
				m_state[*it] = INVALID;
			mtx.lock();
		}
		return invalidated;
	}

    // Make all steps invalid.
    // The provided mutex should be locked at this point, guarding access to m_state.
    // In case any step has already been entered or finished, cancel the background
    // processing by calling the cancel callback.
    template<typename CancelationCallback>
    bool invalidate_all(tbb::mutex &mtx, CancelationCallback cancel) {
        bool invalidated = false;
        for (size_t i = 0; i < COUNT; ++ i)
            if (m_state[i].load(std::memory_order_relaxed) != INVALID) {
                invalidated = true;
                break;
            }
        if (invalidated) {
            mtx.unlock();
            cancel();
            for (size_t i = 0; i < COUNT; ++ i)
                m_state[i].store(INVALID, std::memory_order_relaxed);
            mtx.lock();
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

// Methods NOT modifying the PrintRegion's state:
public:
    const Print*                print() const { return m_print; }
    const PrintRegionConfig&    config() const { return m_config; }
    Flow                        flow(FlowRole role, double layer_height, bool bridge, bool first_layer, double width, const PrintObject &object) const;
    // Average diameter of nozzles participating on extruding this region.
    coordf_t                    nozzle_dmr_avg(const PrintConfig &print_config) const;
    // Average diameter of nozzles participating on extruding this region.
    coordf_t                    bridging_height_avg(const PrintConfig &print_config) const;

// Methods modifying the PrintRegion's state:
public:
    Print*                      print() { return m_print; }
    void                        set_config(const PrintRegionConfig &config) { m_config = config; }
    void                        set_config(PrintRegionConfig &&config) { m_config = std::move(config); }
    void                        config_apply_only(const ConfigBase &other, const t_config_option_keys &keys, bool ignore_nonexistent = false) 
                                        { this->m_config.apply_only(other, keys, ignore_nonexistent); }

protected:
    size_t             m_refcnt;

private:
    Print             *m_print;
    PrintRegionConfig  m_config;
    
    PrintRegion(Print* print) : m_refcnt(0), m_print(print) {}
    PrintRegion(Print* print, const PrintRegionConfig &config) : m_refcnt(0), m_print(print), m_config(config) {}
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
    t_layer_height_ranges   layer_height_ranges;

    // Profile of increasing z to a layer height, to be linearly interpolated when calculating the layers.
    // The pairs of <z, layer_height> are packed into a 1D array to simplify handling by the Perl XS.
    // layer_height_profile must not be set by the background thread.
    std::vector<coordf_t>   layer_height_profile;
    // There is a layer_height_profile at both PrintObject and ModelObject. The layer_height_profile at the ModelObject
    // is used for interactive editing and for loading / storing into a project file (AMF file as of today).
    // This flag indicates that the layer_height_profile at the UI has been updated, therefore the backend needs to get it.
    // This flag is necessary as we cannot safely clear the layer_height_profile if the background calculation is running.
    bool                    layer_height_profile_valid;
    
    // this is set to true when LayerRegion->slices is split in top/internal/bottom
    // so that next call to make_perimeters() performs a union() before computing loops
    bool                    typed_slices;

    Vec3crd                 size;           // XYZ in scaled coordinates

    Print*                  print()                 { return m_print; }
    const Print*            print() const           { return m_print; }
    ModelObject*            model_object()          { return m_model_object; }
    const ModelObject*      model_object() const    { return m_model_object; }
    const PrintObjectConfig& config() const         { return m_config; }    
    void                    config_apply(const ConfigBase &other, bool ignore_nonexistent = false) { this->m_config.apply(other, ignore_nonexistent); }
    void                    config_apply_only(const ConfigBase &other, const t_config_option_keys &keys, bool ignore_nonexistent = false) { this->m_config.apply_only(other, keys, ignore_nonexistent); }
    const LayerPtrs&        layers() const          { return m_layers; }
    const SupportLayerPtrs& support_layers() const  { return m_support_layers; }

    const Transform3d&      trafo() const           { return m_trafo; }
    void                    set_trafo(const Transform3d& trafo) { m_trafo = trafo; }

    const Points&           copies() const { return m_copies; }
    bool                    add_copy(const Vec2d &point);
    bool                    delete_last_copy();
    bool                    delete_all_copies() { return this->set_copies(Points()); }
    bool                    set_copies(const Points &points);
    bool                    reload_model_instances();

    // since the object is aligned to origin, bounding box coincides with size
    BoundingBox bounding_box() const { return BoundingBox(Point(0,0), to_2d(this->size)); }

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
    size_t layer_count() const { return m_layers.size(); }
    void clear_layers();
    Layer* get_layer(int idx) { return m_layers[idx]; }
    const Layer* get_layer(int idx) const { return m_layers[idx]; }

    // print_z: top of the layer; slice_z: center of the layer.
    Layer* add_layer(int id, coordf_t height, coordf_t print_z, coordf_t slice_z);

    size_t support_layer_count() const { return m_support_layers.size(); }
    void clear_support_layers();
    SupportLayer* get_support_layer(int idx) { return m_support_layers[idx]; }
    SupportLayer* add_support_layer(int id, coordf_t height, coordf_t print_z);
    SupportLayerPtrs::const_iterator insert_support_layer(SupportLayerPtrs::const_iterator pos, int id, coordf_t height, coordf_t print_z, coordf_t slice_z);
    void delete_support_layer(int idx);
    
    // methods for handling state
    bool invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys);
    bool invalidate_step(PrintObjectStep step);
	template<typename StepTypeIterator>
	bool invalidate_steps(StepTypeIterator step_begin, StepTypeIterator step_end) { return m_state.invalidate_multiple(step_begin, step_end, this->cancel_mutex(), this->cancel_callback()); }
	bool invalidate_steps(std::initializer_list<PrintObjectStep> il) { return m_state.invalidate_multiple(il.begin(), il.end(), this->cancel_mutex(), this->cancel_callback()); }
	bool invalidate_all_steps() { return m_state.invalidate_all(this->cancel_mutex(), this->cancel_callback()); }
    bool is_step_done(PrintObjectStep step) const { return m_state.is_done(step); }

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

    // Called when slicing to SVG (see Print.pm sub export_svg), and used by perimeters.t
    void slice();

    // Helpers to slice support enforcer / blocker meshes by the support generator.
    std::vector<ExPolygons>     slice_support_enforcers() const;
    std::vector<ExPolygons>     slice_support_blockers() const;

private:
    void make_perimeters();
    void prepare_infill();
    void infill();
    void generate_support_material();

    void _slice();
    std::string _fix_slicing_errors();
    void _simplify_slices(double distance);
    void _make_perimeters();
    bool has_support_material() const;
    void detect_surfaces_type();
    void process_external_surfaces();
    void discover_vertical_shells();
    void bridge_over_infill();
    void clip_fill_surfaces();
    void discover_horizontal_shells();
    void combine_infill();
    void _generate_support_material();

    bool is_printable() const { return ! m_copies.empty(); }
	// Implemented in cpp due to cyclic dependencies between Print and PrintObject.
	tbb::mutex&            cancel_mutex();
    std::function<void()>  cancel_callback();

    Print                                  *m_print;
    ModelObject                            *m_model_object;
    PrintObjectConfig                       m_config;
    // Translation in Z + Rotation + Scaling / Mirroring.
    Transform3d                             m_trafo = Transform3d::Identity();
    // Slic3r::Point objects in scaled G-code coordinates
    Points                                  m_copies;
    // scaled coordinates to add to copies (to compensate for the alignment
    // operated when creating the object but still preserving a coherent API
    // for external callers)
    Point                                   m_copies_shift;

    LayerPtrs                               m_layers;
    SupportLayerPtrs                        m_support_layers;

    PrintState<PrintObjectStep, posCount>   m_state;

    // TODO: call model_object->get_bounding_box() instead of accepting
        // parameter
    PrintObject(Print* print, ModelObject* model_object, const BoundingBoxf3 &modobj_bbox);
    ~PrintObject() {}

    void set_started(PrintObjectStep step);
    void set_done(PrintObjectStep step);
    std::vector<ExPolygons> _slice_region(size_t region_id, const std::vector<float> &z, bool modifier);
    std::vector<ExPolygons> _slice_volumes(const std::vector<float> &z, const std::vector<const ModelVolume*> &volumes) const;
};

struct WipeTowerData
{
    // Following section will be consumed by the GCodeGenerator.
    // Tool ordering of a non-sequential print has to be known to calculate the wipe tower.
    // Cache it here, so it does not need to be recalculated during the G-code generation.
    ToolOrdering                                          tool_ordering;
    // Cache of tool changes per print layer.
    std::unique_ptr<WipeTower::ToolChangeResult>          priming;
    std::vector<std::vector<WipeTower::ToolChangeResult>> tool_changes;
    std::unique_ptr<WipeTower::ToolChangeResult>          final_purge;
    std::vector<float>                                    used_filament;
    int                                                   number_of_toolchanges;

    // Depth of the wipe tower to pass to GLCanvas3D for exact bounding box:
    float                                                 depth;

    void clear() {
        tool_ordering.clear();
        priming.reset(nullptr);
        tool_changes.clear();
        final_purge.reset(nullptr);
        used_filament.clear();
        number_of_toolchanges = -1;
        depth = 0.f;
    }
};

struct PrintStatistics
{
    PrintStatistics() { clear(); }
    std::string                     estimated_normal_print_time;
    std::string                     estimated_silent_print_time;
    double                          total_used_filament;
    double                          total_extruded_volume;
    double                          total_cost;
    double                          total_weight;
    double                          total_wipe_tower_cost;
    double                          total_wipe_tower_filament;
    std::map<size_t, float>         filament_stats;

    void clear() {
        estimated_normal_print_time.clear();
        estimated_silent_print_time.clear();
        total_used_filament    = 0.;
        total_extruded_volume  = 0.;
        total_cost             = 0.;
        total_weight           = 0.;
        total_wipe_tower_cost  = 0.;
        total_wipe_tower_filament = 0.;
        filament_stats.clear();
    }
};

typedef std::vector<PrintObject*> PrintObjectPtrs;
typedef std::vector<PrintRegion*> PrintRegionPtrs;

// The complete print tray with possibly multiple objects.
class Print
{
public:
    Print() { restart(); }
    ~Print() { clear_objects(); }

    // Methods, which change the state of Print / PrintObject / PrintRegion.
    // The following methods are synchronized with process() and export_gcode(),
    // so that process() and export_gcode() may be called from a background thread.
    // In case the following methods need to modify data processed by process() or export_gcode(),
    // a cancellation callback is executed to stop the background processing before the operation.
    void                clear_objects();
    void                delete_object(size_t idx);
    void                reload_object(size_t idx);
    bool                reload_model_instances();
    void                add_model_object(ModelObject* model_object, int idx = -1);
    bool                apply_config(DynamicPrintConfig config);
    enum ApplyStatus {
        // No change after the Print::apply() call.
        APPLY_STATUS_UNCHANGED,
        // Some of the Print / PrintObject / PrintObjectInstance data was changed,
        // but no result was invalidated (only data influencing not yet calculated results were changed).
        APPLY_STATUS_CHANGED,
        // Some data was changed, which in turn invalidated already calculated steps.
        APPLY_STATUS_INVALIDATED,
    };
    ApplyStatus         apply(const Model &model, const DynamicPrintConfig &config);

    void                process();
    void                export_gcode(const std::string &path_template, GCodePreviewData *preview_data);
    // SLA export, temporary.
    void                export_png(const std::string &dirpath);

    // methods for handling state
    bool                is_step_done(PrintStep step) const { return m_state.is_done(step); }
    bool                is_step_done(PrintObjectStep step) const;

    bool                has_infinite_skirt() const;
    bool                has_skirt() const;
    PrintObjectPtrs     get_printable_objects() const;
    float               get_wipe_tower_depth() const { return m_wipe_tower_data.depth; }

    // Returns an empty string if valid, otherwise returns an error message.
    std::string         validate() const;
    BoundingBox         bounding_box() const;
    BoundingBox         total_bounding_box() const;
    double              skirt_first_layer_height() const;
    Flow                brim_flow() const;
    Flow                skirt_flow() const;
    
    std::vector<unsigned int> object_extruders() const;
    std::vector<unsigned int> support_material_extruders() const;
    std::vector<unsigned int> extruders() const;
    double              max_allowed_layer_height() const;
    bool                has_support_material() const;
    // Make sure the background processing has no access to this model_object during this call!
    void                auto_assign_extruders(ModelObject* model_object) const;

    const PrintConfig&          config() const { return m_config; }
    const PrintObjectConfig&    default_object_config() const { return m_default_object_config; }
    const PrintRegionConfig&    default_region_config() const { return m_default_region_config; }
    const PrintObjectPtrs&      objects() const { return m_objects; }
    PrintObject*                get_object(size_t idx) { return m_objects[idx]; }
    const PrintObject*          get_object(size_t idx) const { return m_objects[idx]; }
    const PrintRegionPtrs&      regions() const { return m_regions; }
    const PlaceholderParser&    placeholder_parser() const { return m_placeholder_parser; }
    PlaceholderParser&          placeholder_parser() { return m_placeholder_parser; }
    // How many of PrintObject::copies() over all print objects are there?
    // If zero, then the print is empty and the print shall not be executed.
    unsigned int                num_object_instances() const;

    // Returns extruder this eec should be printed with, according to PrintRegion config:
    static int get_extruder(const ExtrusionEntityCollection& fill, const PrintRegion &region);

    const ExtrusionEntityCollection& skirt() const { return m_skirt; }
    const ExtrusionEntityCollection& brim() const { return m_brim; }

    const PrintStatistics&      print_statistics() const { return m_print_statistics; }

    // Wipe tower support.
    bool                        has_wipe_tower() const;
    const WipeTowerData&        wipe_tower_data() const { return m_wipe_tower_data; }

    std::string                 output_filename() const;
    std::string                 output_filepath(const std::string &path) const;

    typedef std::function<void(int, const std::string&)>  status_callback_type;
    // Default status console print out in the form of percent => message.
    void                set_status_default() { m_status_callback = nullptr; }
    // No status output or callback whatsoever, useful mostly for automatic tests.
    void                set_status_silent() { m_status_callback = [](int, const std::string&){}; }
    // Register a custom status callback.
    void                set_status_callback(status_callback_type cb) { m_status_callback = cb; }
    // Calls a registered callback to update the status, or print out the default message.
    void                set_status(int percent, const std::string &message) { 
        if (m_status_callback) m_status_callback(percent, message);
        else printf("%d => %s\n", percent, message.c_str());
    }

    typedef std::function<void()>  cancel_callback_type;
    // Various methods will call this callback to stop the background processing (the Print::process() call)
    // in case a successive change of the Print / PrintObject / PrintRegion instances changed
    // the state of the finished or running calculations.
    void                set_cancel_callback(cancel_callback_type cancel_callback) { m_cancel_callback = cancel_callback; }
    // Has the calculation been canceled?
	enum CancelStatus {
		// No cancelation, background processing should run.
		NOT_CANCELED = 0,
		// Canceled by user from the user interface (user pressed the "Cancel" button or user closed the application).
		CANCELED_BY_USER = 1,
		// Canceled internally from Print::apply() through the Print/PrintObject::invalidate_step() or ::invalidate_all_steps().
		CANCELED_INTERNAL = 2
	};
    CancelStatus        cancel_status() const { return m_cancel_status; }
    // Has the calculation been canceled?
	bool                canceled() const { return m_cancel_status != NOT_CANCELED; }
    // Cancel the running computation. Stop execution of all the background threads.
	void                cancel() { m_cancel_status = CANCELED_BY_USER; }
	void                cancel_internal() { m_cancel_status = CANCELED_INTERNAL; }
    // Cancel the running computation. Stop execution of all the background threads.
	void                restart() { m_cancel_status = NOT_CANCELED; }

    // Accessed by SupportMaterial
    const PrintRegion*  get_region(size_t idx) const  { return m_regions[idx]; }

protected:
	void                set_started(PrintStep step) { m_state.set_started(step, m_mutex); throw_if_canceled(); }
	void                set_done(PrintStep step) { m_state.set_done(step, m_mutex); throw_if_canceled(); }
    bool                invalidate_step(PrintStep step);
	template<typename StepTypeIterator>
	bool				invalidate_steps(StepTypeIterator step_begin, StepTypeIterator step_end) { return m_state.invalidate_multiple(step_begin, step_end, m_mutex, m_cancel_callback); }
    bool                invalidate_steps(std::initializer_list<PrintStep> il) { return m_state.invalidate_multiple(il.begin(), il.end(), m_mutex, m_cancel_callback); }
    bool                invalidate_all_steps() { return m_state.invalidate_all(m_mutex, m_cancel_callback); }

    // methods for handling regions
    PrintRegion*        get_region(size_t idx)        { return m_regions[idx]; }
    PrintRegion*        add_region();
    PrintRegion*        add_region(const PrintRegionConfig &config);

private:
    // Update "scale", "input_filename", "input_filename_base" placeholders from the current m_objects.
    void                update_object_placeholders();

    bool                invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys);

    // If the background processing stop was requested, throw CanceledException.
    // To be called by the worker thread and its sub-threads (mostly launched on the TBB thread pool) regularly.
    void                throw_if_canceled() const { if (m_cancel_status) throw CanceledException(); }

    void                _make_skirt();
    void                _make_brim();
    void                _make_wipe_tower();
    void                _simplify_slices(double distance);

    // Declared here to have access to Model / ModelObject / ModelInstance
    static void         model_volume_list_update_supports(ModelObject &model_object_dst, const ModelObject &model_object_src);

    PrintState<PrintStep, psCount>          m_state;
    // Mutex used for synchronization of the worker thread with the UI thread:
    // The mutex will be used to guard the worker thread against entering a stage
    // while the data influencing the stage is modified.
    mutable tbb::mutex                      m_mutex;

    tbb::atomic<CancelStatus>               m_cancel_status;
    // Callback to be evoked regularly to update state of the UI thread.
    status_callback_type                    m_status_callback;

    // Callback to be evoked to stop the background processing before a state is updated.
    cancel_callback_type                    m_cancel_callback = [](){};

    Model                                   m_model;
    PrintConfig                             m_config;
    PrintObjectConfig                       m_default_object_config;
    PrintRegionConfig                       m_default_region_config;
    PrintObjectPtrs                         m_objects;
    PrintRegionPtrs                         m_regions;
    PlaceholderParser                       m_placeholder_parser;

    // Ordered collections of extrusion paths to build skirt loops and brim.
    ExtrusionEntityCollection               m_skirt;
    ExtrusionEntityCollection               m_brim;

    // Following section will be consumed by the GCodeGenerator.
    WipeTowerData                           m_wipe_tower_data;

    // Estimated print time, filament consumed.
    PrintStatistics                         m_print_statistics;

    // To allow GCode to set the Print's GCodeExport step status.
    friend class GCode;
    // Allow PrintObject to access m_mutex and m_cancel_callback.
    friend class PrintObject;
};

}

#endif
