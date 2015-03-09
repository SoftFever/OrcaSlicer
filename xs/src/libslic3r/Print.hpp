#ifndef slic3r_Print_hpp_
#define slic3r_Print_hpp_

#include <myinit.h>
#include <set>
#include <vector>
#include <stdexcept>
#include "BoundingBox.hpp"
#include "Flow.hpp"
#include "PrintConfig.hpp"
#include "Point.hpp"
#include "Layer.hpp"
#include "Model.hpp"
#include "PlaceholderParser.hpp"


namespace Slic3r {

class Print;
class PrintObject;
class ModelObject;


enum PrintStep {
    psSkirt, psBrim,
};
enum PrintObjectStep {
    posSlice, posPerimeters, posPrepareInfill,
    posInfill, posSupportMaterial,
};

class PrintValidationException : public std::runtime_error {
    public:
    PrintValidationException(const std::string &error) : std::runtime_error(error) {};
};

template <class StepType>
class PrintState
{
    public:
    std::set<StepType> started, done;
    
    bool is_started(StepType step) const;
    bool is_done(StepType step) const;
    void set_started(StepType step);
    void set_done(StepType step);
    bool invalidate(StepType step);
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
    // TODO: Fill* fill_maker        => (is => 'lazy');
    PrintState<PrintObjectStep> state;
    
    Print* print();
    ModelObject* model_object();
    
    Points copies() const;
    bool add_copy(const Pointf &point);
    bool delete_last_copy();
    bool delete_all_copies();
    bool set_copies(const Points &points);
    bool reload_model_instances();
    BoundingBox bounding_box() const;
    
    // adds region_id, too, if necessary
    void add_region_volume(int region_id, int volume_id);

    size_t total_layer_count() const;
    size_t layer_count() const;
    void clear_layers();
    Layer* get_layer(int idx);
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
    
    bool has_support_material() const;
    void bridge_over_infill();
    
    private:
    Print* _print;
    ModelObject* _model_object;
    Points _copies;      // Slic3r::Point objects in scaled G-code coordinates

    // TODO: call model_object->get_bounding_box() instead of accepting
        // parameter
    PrintObject(Print* print, ModelObject* model_object, const BoundingBoxf3 &modobj_bbox);
    ~PrintObject();
};

typedef std::vector<PrintObject*> PrintObjectPtrs;
typedef std::vector<PrintRegion*> PrintRegionPtrs;

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
    double total_used_filament, total_extruded_volume;
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
    PrintRegion* get_region(size_t idx);
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
    void validate() const;
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
