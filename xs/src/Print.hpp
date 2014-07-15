#ifndef slic3r_Print_hpp_
#define slic3r_Print_hpp_

#include <myinit.h>
#include <set>
#include <vector>
#include "PrintConfig.hpp"
#include "Point.hpp"
#include "Layer.hpp"
#include "PlaceholderParser.hpp"


namespace Slic3r {

class Print;
class ModelObject;


enum PrintStep {
    psInitExtruders, psSkirt, psBrim,
};
enum PrintObjectStep {
    posSlice, posPerimeters, posPrepareInfill,
    posInfill, posSupportMaterial,
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
    // vector of (vectors of volume ids), indexed by region_id
    std::vector<std::vector<int> > region_volumes;
    Points copies;      // Slic3r::Point objects in scaled G-code coordinates
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

    // adds region_id, too, if necessary
    void add_region_volume(int region_id, int volume_id);

    size_t layer_count();
    void clear_layers();
    Layer* get_layer(int idx);
    Layer* add_layer(int id, coordf_t height, coordf_t print_z, coordf_t slice_z);
    void delete_layer(int idx);

    size_t support_layer_count();
    void clear_support_layers();
    SupportLayer* get_support_layer(int idx);
    SupportLayer* add_support_layer(int id, coordf_t height, coordf_t print_z, coordf_t slice_z);
    void delete_support_layer(int idx);
    
    // methods for handling state
    bool invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys);
    bool invalidate_step(PrintObjectStep step);
    bool invalidate_all_steps();
    
    private:
    Print* _print;
    ModelObject* _model_object;

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
    PrintObject* add_object(ModelObject *model_object, const BoundingBoxf3 &modobj_bbox);
    PrintObject* set_new_object(size_t idx, ModelObject *model_object, const BoundingBoxf3 &modobj_bbox);
    void delete_object(size_t idx);

    // methods for handling regions
    PrintRegion* get_region(size_t idx);
    PrintRegion* add_region();
    
    // methods for handling state
    bool invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys);
    bool invalidate_step(PrintStep step);
    bool invalidate_all_steps();

    private:
    void clear_regions();
    void delete_region(size_t idx);
};

}

#endif
