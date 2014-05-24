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
    psInitExtruders, psSlice, psPerimeters, prPrepareInfill,
    psInfill, psSupportMaterial, psSkirt, psBrim,
};

class PrintState
{
    private:
    std::set<PrintStep> _started;
    std::set<PrintStep> _done;
    
    public:
    bool started(PrintStep step) const;
    bool done(PrintStep step) const;
    void set_started(PrintStep step);
    void set_done(PrintStep step);
    void invalidate(PrintStep step);
    void invalidate_all();
};

// TODO: make stuff private

// A PrintRegion object represents a group of volumes to print
// sharing the same config (including the same assigned extruder(s))
class PrintRegion
{
    friend class Print;

    public:
    PrintRegionConfig config;

    Print* print();
    PrintConfig &print_config();

    private:
    Print* _print;

    PrintRegion(Print* print);
    virtual ~PrintRegion();
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
    PrintState _state;


    Print* print();
    ModelObject* model_object();

    // adds region_id, too, if necessary
    void add_region_volume(int region_id, int volume_id);

    size_t layer_count();
    void clear_layers();
    Layer* get_layer(int idx);
    Layer* add_layer(int id, coordf_t height, coordf_t print_z,
        coordf_t slice_z);
    void delete_layer(int idx);

    size_t support_layer_count();
    void clear_support_layers();
    SupportLayer* get_support_layer(int idx);
    SupportLayer* add_support_layer(int id, coordf_t height, coordf_t print_z,
        coordf_t slice_z);
    void delete_support_layer(int idx);

    private:
    Print* _print;
    ModelObject* _model_object;

    // TODO: call model_object->get_bounding_box() instead of accepting
        // parameter
    PrintObject(Print* print, ModelObject* model_object,
        const BoundingBoxf3 &modobj_bbox);
    virtual ~PrintObject();
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
    double total_used_filament;
    double total_extruded_volume;
    PrintState _state;

    // ordered collection of extrusion paths to build skirt loops
    ExtrusionEntityCollection skirt;

    // ordered collection of extrusion paths to build a brim
    ExtrusionEntityCollection brim;

    Print();
    virtual ~Print();

    void clear_objects();
    PrintObject* get_object(int idx);
    PrintObject* add_object(ModelObject *model_object,
        const BoundingBoxf3 &modobj_bbox);
    PrintObject* set_new_object(size_t idx, ModelObject *model_object,
        const BoundingBoxf3 &modobj_bbox);
    void delete_object(int idx);

    PrintRegion* get_region(int idx);
    PrintRegion* add_region();

    private:
    void clear_regions();
    void delete_region(int idx);
};

}

#endif
