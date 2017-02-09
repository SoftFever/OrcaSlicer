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

namespace Slic3r {

class Print;
class PrintObject;
class ModelObject;

// Print step IDs for keeping track of the print state.
enum PrintStep {
    psSkirt, psBrim,
};
enum PrintObjectStep {
    posSlice, posPerimeters, posPrepareInfill,
    posInfill, posSupportMaterial,
};

// To be instantiated over PrintStep or PrintObjectStep enums.
template <class StepType>
class PrintState
{
public:
    std::set<StepType> started, done;
    
    bool is_started(StepType step) const { return this->started.find(step) != this->started.end(); }
    bool is_done(StepType step) const { return this->done.find(step) != this->done.end(); }
    void set_started(StepType step) { this->started.insert(step); }
    void set_done(StepType step) { this->done.insert(step); }
    bool invalidate(StepType step) {
        bool invalidated = this->started.erase(step) > 0;
        this->done.erase(step);
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

    Print* print();
    Flow flow(FlowRole role, double layer_height, bool bridge, bool first_layer, double width, const PrintObject &object) const;

    private:
    Print* _print;
    
    PrintRegion(Print* print);
    ~PrintRegion();
};


typedef std::vector<Layer*> LayerPtrs;
typedef std::vector<SupportLayer*> SupportLayerPtrs;
class BoundingBoxf3;        // TODO: for temporary constructor parameter

class PrintObject
{
    friend class Print;

public:
    // map of (vectors of volume ids), indexed by region_id
    /* (we use map instead of vector so that we don't have to worry about
       resizing it and the [] operator adds new items automagically) */
    std::map< size_t,std::vector<int> > region_volumes;
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

    LayerPtrs layers;
    SupportLayerPtrs support_layers;
    PrintState<PrintObjectStep> state;
    
    Print*              print()                 { return this->_print; }
    const Print*        print() const           { return this->_print; }
    ModelObject*        model_object()          { return this->_model_object; }
    const ModelObject*  model_object() const    { return this->_model_object; }

    Points copies() const { return this->_copies; }
    bool add_copy(const Pointf &point);
    bool delete_last_copy();
    bool delete_all_copies();
    bool set_copies(const Points &points);
    bool reload_model_instances();
    BoundingBox bounding_box() const {
        // since the object is aligned to origin, bounding box coincides with size
        return BoundingBox(Point(0,0), this->size);
    }
    
    // adds region_id, too, if necessary
    void add_region_volume(int region_id, int volume_id);

    size_t total_layer_count() const;
    size_t layer_count() const;
    void clear_layers();
    Layer* get_layer(int idx) { return this->layers.at(idx); }
    const Layer* get_layer(int idx) const { return this->layers.at(idx); }

    // print_z: top of the layer; slice_z: center of the layer.
    Layer* add_layer(int id, coordf_t height, coordf_t print_z, coordf_t slice_z);
    void delete_layer(int idx);

    size_t support_layer_count() const;
    void clear_support_layers();
    SupportLayer* get_support_layer(int idx);
    SupportLayer* add_support_layer(int id, coordf_t height, coordf_t print_z);
    void delete_support_layer(int idx);
    
    // methods for handling state
    bool invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys);
    bool invalidate_step(PrintObjectStep step);
    bool invalidate_all_steps();

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

    void _slice();
    bool has_support_material() const;
    void detect_surfaces_type();
    void process_external_surfaces();
    void discover_vertical_shells();
    void bridge_over_infill();
    void _make_perimeters();
    void _infill();
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
    double total_used_filament, total_extruded_volume, total_cost, total_weight;
    std::map<size_t,float> filament_stats;
    PrintState<PrintStep> state;

    // ordered collections of extrusion paths to build skirt loops and brim
    ExtrusionEntityCollection skirt, brim;

    Print();
    ~Print();
    
    // methods for handling objects
    void clear_objects();
    PrintObject* get_object(size_t idx);
    void delete_object(size_t idx);
    void reload_object(size_t idx);
    bool reload_model_instances();

    // methods for handling regions
    PrintRegion* get_region(size_t idx) { return regions.at(idx); }
    const PrintRegion* get_region(size_t idx) const  { return regions.at(idx); }
    PrintRegion* add_region();
    
    // methods for handling state
    bool invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys);
    bool invalidate_step(PrintStep step);
    bool invalidate_all_steps();
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
    
    std::set<size_t> object_extruders() const;
    std::set<size_t> support_material_extruders() const;
    std::set<size_t> extruders() const;
    void _simplify_slices(double distance);
    double max_allowed_layer_height() const;
    bool has_support_material() const;
    void auto_assign_extruders(ModelObject* model_object) const;
    
    private:
    void clear_regions();
    void delete_region(size_t idx);
    PrintRegionConfig _region_config_from_model_volume(const ModelVolume &volume);
};

#define FOREACH_BASE(type, container, iterator) for (type::const_iterator iterator = (container).begin(); iterator != (container).end(); ++iterator)
#define FOREACH_REGION(print, region)       FOREACH_BASE(PrintRegionPtrs, (print)->regions, region)
#define FOREACH_OBJECT(print, object)       FOREACH_BASE(PrintObjectPtrs, (print)->objects, object)
#define FOREACH_LAYER(object, layer)        FOREACH_BASE(LayerPtrs, (object)->layers, layer)
#define FOREACH_LAYERREGION(layer, layerm)  FOREACH_BASE(LayerRegionPtrs, (layer)->regions, layerm)

}

#endif
