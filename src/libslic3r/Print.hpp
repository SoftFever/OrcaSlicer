#ifndef slic3r_Print_hpp_
#define slic3r_Print_hpp_

#include "PrintBase.hpp"

#include "BoundingBox.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "Flow.hpp"
#include "Point.hpp"
#include "Slicing.hpp"
#include "TriangleMeshSlicer.hpp"
#include "GCode/ToolOrdering.hpp"
#include "GCode/WipeTower.hpp"
#include "GCode/ThumbnailData.hpp"
#include "GCode/GCodeProcessor.hpp"
#include "MultiMaterialSegmentation.hpp"

#include "libslic3r.h"

#include <Eigen/Geometry>

#include <functional>
#include <set>

namespace Slic3r {

class GCode;
class Layer;
class ModelObject;
class Print;
class PrintObject;
class SupportLayer;

namespace FillAdaptive {
    struct Octree;
    struct OctreeDeleter;
    using OctreePtr = std::unique_ptr<Octree, OctreeDeleter>;
};

// Print step IDs for keeping track of the print state.
// The Print steps are applied in this order.
enum PrintStep {
    psWipeTower,
    // Ordering of the tools on PrintObjects for a multi-material print.
    // psToolOrdering is a synonym to psWipeTower, as the Wipe Tower calculates and modifies the ToolOrdering,
    // while if printing without the Wipe Tower, the ToolOrdering is calculated as well.
    psToolOrdering = psWipeTower,
    psSkirt, 
    psBrim,
    // Last step before G-code export, after this step is finished, the initial extrusion path preview
    // should be refreshed.
    psSlicingFinished = psBrim,
    psGCodeExport,
    psCount,
};

enum PrintObjectStep {
    posSlice, posPerimeters, posPrepareInfill,
    posInfill, posIroning, posSupportMaterial, posCount,
};

// A PrintRegion object represents a group of volumes to print
// sharing the same config (including the same assigned extruder(s))
class PrintRegion
{
public:
    PrintRegion() = default;
    PrintRegion(const PrintRegionConfig &config);
    PrintRegion(const PrintRegionConfig &config, const size_t config_hash, int print_object_region_id = -1) : m_config(config), m_config_hash(config_hash), m_print_object_region_id(print_object_region_id) {}
    PrintRegion(PrintRegionConfig &&config);
    PrintRegion(PrintRegionConfig &&config, const size_t config_hash, int print_object_region_id = -1) : m_config(std::move(config)), m_config_hash(config_hash), m_print_object_region_id(print_object_region_id) {}
    ~PrintRegion() = default;

// Methods NOT modifying the PrintRegion's state:
public:
    const PrintRegionConfig&    config() const throw() { return m_config; }
    size_t                      config_hash() const throw() { return m_config_hash; }
    // Identifier of this PrintRegion in the list of Print::m_print_regions.
    int                         print_region_id() const throw() { return m_print_region_id; }
    int                         print_object_region_id() const throw() { return m_print_object_region_id; }
	// 1-based extruder identifier for this region and role.
	unsigned int 				extruder(FlowRole role) const;
    Flow                        flow(const PrintObject &object, FlowRole role, double layer_height, bool first_layer = false) const;
    // Average diameter of nozzles participating on extruding this region.
    coordf_t                    nozzle_dmr_avg(const PrintConfig &print_config) const;
    // Average diameter of nozzles participating on extruding this region.
    coordf_t                    bridging_height_avg(const PrintConfig &print_config) const;

    // Collect 0-based extruder indices used to print this region's object.
	void                        collect_object_printing_extruders(const Print &print, std::vector<unsigned int> &object_extruders) const;
	static void                 collect_object_printing_extruders(const PrintConfig &print_config, const PrintRegionConfig &region_config, const bool has_brim, std::vector<unsigned int> &object_extruders);

// Methods modifying the PrintRegion's state:
public:
    void                        set_config(const PrintRegionConfig &config) { m_config = config; m_config_hash = m_config.hash(); }
    void                        set_config(PrintRegionConfig &&config) { m_config = std::move(config); m_config_hash = m_config.hash(); }
    void                        config_apply_only(const ConfigBase &other, const t_config_option_keys &keys, bool ignore_nonexistent = false) 
                                        { m_config.apply_only(other, keys, ignore_nonexistent); m_config_hash = m_config.hash(); }
private:
    friend Print;
    friend void print_region_ref_inc(PrintRegion&);
    friend void print_region_ref_reset(PrintRegion&);
    friend int  print_region_ref_cnt(const PrintRegion&);

    PrintRegionConfig  m_config;
    size_t             m_config_hash;
    int                m_print_region_id { -1 };
    int                m_print_object_region_id { -1 };
    int                m_ref_cnt { 0 };
};

inline bool operator==(const PrintRegion &lhs, const PrintRegion &rhs) { return lhs.config_hash() == rhs.config_hash() && lhs.config() == rhs.config(); }
inline bool operator!=(const PrintRegion &lhs, const PrintRegion &rhs) { return ! (lhs == rhs); }

template<typename T>
class ConstVectorOfPtrsAdaptor {
public:
    // Returning a non-const pointer to const pointers to T.
    T * const *             begin() const { return m_data->data(); }
    T * const *             end()   const { return m_data->data() + m_data->size(); }
    const T*                front() const { return m_data->front(); }
    const T*                back()  const { return m_data->front(); }
    size_t                  size()  const { return m_data->size(); }
    bool                    empty() const { return m_data->empty(); }
    const T*                operator[](size_t i) const { return (*m_data)[i]; }
    const T*                at(size_t i) const { return m_data->at(i); }
    std::vector<const T*>   vector() const { return std::vector<const T*>(this->begin(), this->end()); }
protected:
    ConstVectorOfPtrsAdaptor(const std::vector<T*> *data) : m_data(data) {}
private:
    const std::vector<T*> *m_data;
};

typedef std::vector<Layer*>       LayerPtrs;
typedef std::vector<const Layer*> ConstLayerPtrs;
class ConstLayerPtrsAdaptor : public ConstVectorOfPtrsAdaptor<Layer> {
    friend PrintObject;
    ConstLayerPtrsAdaptor(const LayerPtrs *data) : ConstVectorOfPtrsAdaptor<Layer>(data) {}
};

typedef std::vector<SupportLayer*>        SupportLayerPtrs;
typedef std::vector<const SupportLayer*>  ConstSupportLayerPtrs;
class ConstSupportLayerPtrsAdaptor : public ConstVectorOfPtrsAdaptor<SupportLayer> {
    friend PrintObject;
    ConstSupportLayerPtrsAdaptor(const SupportLayerPtrs *data) : ConstVectorOfPtrsAdaptor<SupportLayer>(data) {}
};

class BoundingBoxf3;        // TODO: for temporary constructor parameter

// Single instance of a PrintObject.
// As multiple PrintObjects may be generated for a single ModelObject (their instances differ in rotation around Z),
// ModelObject's instancess will be distributed among these multiple PrintObjects.
struct PrintInstance
{
    // Parent PrintObject
    PrintObject 		*print_object;
    // Source ModelInstance of a ModelObject, for which this print_object was created.
	const ModelInstance *model_instance;
	// Shift of this instance's center into the world coordinates.
	Point 				 shift;
};

typedef std::vector<PrintInstance> PrintInstances;

class PrintObjectRegions
{
public:
    // Bounding box of a ModelVolume transformed into the working space of a PrintObject, possibly
    // clipped by a layer range modifier.
    // Only Eigen types of Nx16 size are vectorized. This bounding box will not be vectorized.
    static_assert(sizeof(Eigen::AlignedBox<float, 3>) == 24, "Eigen::AlignedBox<float, 3> is not being vectorized, thus it does not need to be aligned");
    using BoundingBox = Eigen::AlignedBox<float, 3>;
    struct VolumeExtents {
        ObjectID             volume_id;
        BoundingBox          bbox;
    };

    struct VolumeRegion
    {
        // ID of the associated ModelVolume.
        const ModelVolume   *model_volume { nullptr };
        // Index of a parent VolumeRegion.
        int                  parent { -1 };
        // Pointer to PrintObjectRegions::all_regions, null for a negative volume.
        PrintRegion         *region { nullptr };
        // Pointer to VolumeExtents::bbox.
        const BoundingBox   *bbox { nullptr };
        // To speed up merging of same regions.
        const VolumeRegion  *prev_same_region { nullptr };
    };

    struct PaintedRegion
    {
        // 1-based extruder identifier.
        unsigned int     extruder_id;
        // Index of a parent VolumeRegion.
        int              parent { -1 };
        // Pointer to PrintObjectRegions::all_regions.
        PrintRegion     *region { nullptr };
    };

    // One slice over the PrintObject (possibly the whole PrintObject) and a list of ModelVolumes and their bounding boxes
    // possibly clipped by the layer_height_range.
    struct LayerRangeRegions
    {
        t_layer_height_range        layer_height_range;
        // Config of the layer range, null if there is just a single range with no config override.
        // Config is owned by the associated ModelObject.
        const DynamicPrintConfig*   config { nullptr };
        // Volumes sorted by ModelVolume::id().
        std::vector<VolumeExtents>  volumes;

        // Sorted in the order of their source ModelVolumes, thus reflecting the order of region clipping, modifier overrides etc.
        std::vector<VolumeRegion>   volume_regions;
        std::vector<PaintedRegion>  painted_regions;

        bool has_volume(const ObjectID id) const {
            auto it = lower_bound_by_predicate(this->volumes.begin(), this->volumes.end(), [id](const VolumeExtents &l) { return l.volume_id < id; });
            return it != this->volumes.end() && it->volume_id == id;
        }
    };

    std::vector<std::unique_ptr<PrintRegion>>   all_regions;
    std::vector<LayerRangeRegions>              layer_ranges;
    // Transformation of this ModelObject into one of the associated PrintObjects (all PrintObjects derived from a single modelObject differ by a Z rotation only).
    // This transformation is used to calculate VolumeExtents.
    Transform3d                                 trafo_bboxes;
    std::vector<ObjectID>                       cached_volume_ids;

    void ref_cnt_inc() { ++ m_ref_cnt; }
    void ref_cnt_dec() { if (-- m_ref_cnt == 0) delete this; }
    void clear() {
        all_regions.clear();
        layer_ranges.clear();
        cached_volume_ids.clear();
    }

private:
    friend class PrintObject;
    // Number of PrintObjects generated from the same ModelObject and sharing the regions.
    // ref_cnt could only be modified by the main thread, thus it does not need to be atomic.
    size_t                                      m_ref_cnt{ 0 };
};

class PrintObject : public PrintObjectBaseWithState<Print, PrintObjectStep, posCount>
{
private: // Prevents erroneous use by other classes.
    typedef PrintObjectBaseWithState<Print, PrintObjectStep, posCount> Inherited;

public:
    // Size of an object: XYZ in scaled coordinates. The size might not be quite snug in XY plane.
    const Vec3crd&               size() const			{ return m_size; }
    const PrintObjectConfig&     config() const         { return m_config; }    
    ConstLayerPtrsAdaptor        layers() const         { return ConstLayerPtrsAdaptor(&m_layers); }
    ConstSupportLayerPtrsAdaptor support_layers() const { return ConstSupportLayerPtrsAdaptor(&m_support_layers); }
    const Transform3d&           trafo() const          { return m_trafo; }
    const PrintInstances&        instances() const      { return m_instances; }

    // Whoever will get a non-const pointer to PrintObject will be able to modify its layers.
    LayerPtrs&                   layers()               { return m_layers; }
    SupportLayerPtrs&            support_layers()       { return m_support_layers; }

    // Bounding box is used to align the object infill patterns, and to calculate attractor for the rear seam.
    // The bounding box may not be quite snug.
    BoundingBox                  bounding_box() const   { return BoundingBox(Point(- m_size.x() / 2, - m_size.y() / 2), Point(m_size.x() / 2, m_size.y() / 2)); }
    // Height is used for slicing, for sorting the objects by height for sequential printing and for checking vertical clearence in sequential print mode.
    // The height is snug.
    coord_t 				     height() const         { return m_size.z(); }
    // Centering offset of the sliced mesh from the scaled and rotated mesh of the model.
    const Point& 			     center_offset() const  { return m_center_offset; }

    bool                         has_brim() const       { return this->config().brim_type != btNoBrim && this->config().brim_width.value > 0.; }

    // This is the *total* layer count (including support layers)
    // this value is not supposed to be compared with Layer::id
    // since they have different semantics.
    size_t 			total_layer_count() const { return this->layer_count() + this->support_layer_count(); }
    size_t 			layer_count() const { return m_layers.size(); }
    void 			clear_layers();
    const Layer* 	get_layer(int idx) const { return m_layers[idx]; }
    Layer* 			get_layer(int idx) 		 { return m_layers[idx]; }
    // Get a layer exactly at print_z.
    const Layer*	get_layer_at_printz(coordf_t print_z) const;
    Layer*			get_layer_at_printz(coordf_t print_z);
    // Get a layer approximately at print_z.
    const Layer*	get_layer_at_printz(coordf_t print_z, coordf_t epsilon) const;
    Layer*			get_layer_at_printz(coordf_t print_z, coordf_t epsilon);
    // Get the first layer approximately bellow print_z.
    const Layer*	get_first_layer_bellow_printz(coordf_t print_z, coordf_t epsilon) const;

    // print_z: top of the layer; slice_z: center of the layer.
    Layer*          add_layer(int id, coordf_t height, coordf_t print_z, coordf_t slice_z);

    size_t          support_layer_count() const { return m_support_layers.size(); }
    void            clear_support_layers();
    SupportLayer*   get_support_layer(int idx) { return m_support_layers[idx]; }
    SupportLayer*   add_support_layer(int id, coordf_t height, coordf_t print_z);
    SupportLayerPtrs::iterator insert_support_layer(SupportLayerPtrs::iterator pos, size_t id, coordf_t height, coordf_t print_z, coordf_t slice_z);
    void            delete_support_layer(int idx);
    
    // Initialize the layer_height_profile from the model_object's layer_height_profile, from model_object's layer height table, or from slicing parameters.
    // Returns true, if the layer_height_profile was changed.
    static bool     update_layer_height_profile(const ModelObject &model_object, const SlicingParameters &slicing_parameters, std::vector<coordf_t> &layer_height_profile);

    // Collect the slicing parameters, to be used by variable layer thickness algorithm,
    // by the interactive layer height editor and by the printing process itself.
    // The slicing parameters are dependent on various configuration values
    // (layer height, first layer height, raft settings, print nozzle diameter etc).
    const SlicingParameters&    slicing_parameters() const { return m_slicing_params; }
    static SlicingParameters    slicing_parameters(const DynamicPrintConfig &full_config, const ModelObject &model_object, float object_max_z);

    size_t                      num_printing_regions() const throw() { return m_shared_regions->all_regions.size(); }
    const PrintRegion&          printing_region(size_t idx) const throw() { return *m_shared_regions->all_regions[idx].get(); }
    //FIXME returing all possible regions before slicing, thus some of the regions may not be slicing at the end.
    std::vector<std::reference_wrapper<const PrintRegion>> all_regions() const;
    const PrintObjectRegions*   shared_regions() const throw() { return m_shared_regions; }

    bool                        has_support()           const { return m_config.support_material || m_config.support_material_enforce_layers > 0; }
    bool                        has_raft()              const { return m_config.raft_layers > 0; }
    bool                        has_support_material()  const { return this->has_support() || this->has_raft(); }

    // returns 0-based indices of extruders used to print the object (without brim, support and other helper extrusions)
    std::vector<unsigned int>   object_extruders() const;

    // Called by make_perimeters()
    void slice();

    // Helpers to slice support enforcer / blocker meshes by the support generator.
    std::vector<ExPolygons>     slice_support_volumes(const ModelVolumeType model_volume_type) const;
    std::vector<ExPolygons>     slice_support_blockers() const { return this->slice_support_volumes(ModelVolumeType::SUPPORT_BLOCKER); }
    std::vector<ExPolygons>     slice_support_enforcers() const { return this->slice_support_volumes(ModelVolumeType::SUPPORT_ENFORCER); }

    // Helpers to project custom facets on slices
    void project_and_append_custom_facets(bool seam, EnforcerBlockerType type, std::vector<ExPolygons>& expolys) const;

private:
    // to be called from Print only.
    friend class Print;

	PrintObject(Print* print, ModelObject* model_object, const Transform3d& trafo, PrintInstances&& instances);
	~PrintObject() { if (m_shared_regions && -- m_shared_regions->m_ref_cnt == 0) delete m_shared_regions; }
 
    void                    config_apply(const ConfigBase &other, bool ignore_nonexistent = false) { m_config.apply(other, ignore_nonexistent); }
    void                    config_apply_only(const ConfigBase &other, const t_config_option_keys &keys, bool ignore_nonexistent = false) { m_config.apply_only(other, keys, ignore_nonexistent); }
    PrintBase::ApplyStatus  set_instances(PrintInstances &&instances);
    // Invalidates the step, and its depending steps in PrintObject and Print.
    bool                    invalidate_step(PrintObjectStep step);
    // Invalidates all PrintObject and Print steps.
    bool                    invalidate_all_steps();
    // Invalidate steps based on a set of parameters changed.
    // It may be called for both the PrintObjectConfig and PrintRegionConfig.
    bool                    invalidate_state_by_config_options(
        const ConfigOptionResolver &old_config, const ConfigOptionResolver &new_config, const std::vector<t_config_option_key> &opt_keys);
    // If ! m_slicing_params.valid, recalculate.
    void                    update_slicing_parameters();

    static PrintObjectConfig object_config_from_model_object(const PrintObjectConfig &default_object_config, const ModelObject &object, size_t num_extruders);

private:
    void make_perimeters();
    void prepare_infill();
    void infill();
    void ironing();
    void generate_support_material();

    void slice_volumes();
    // Has any support (not counting the raft).
    void detect_surfaces_type();
    void process_external_surfaces();
    void discover_vertical_shells();
    void bridge_over_infill();
    void clip_fill_surfaces();
    void discover_horizontal_shells();
    void combine_infill();
    void _generate_support_material();
    std::pair<FillAdaptive::OctreePtr, FillAdaptive::OctreePtr> prepare_adaptive_infill_data();

    // XYZ in scaled coordinates
    Vec3crd									m_size;
    PrintObjectConfig                       m_config;
    // Translation in Z + Rotation + Scaling / Mirroring.
    Transform3d                             m_trafo = Transform3d::Identity();
    // Slic3r::Point objects in scaled G-code coordinates
    std::vector<PrintInstance>              m_instances;
    // The mesh is being centered before thrown to Clipper, so that the Clipper's fixed coordinates require less bits.
    // This is the adjustment of the  the Object's coordinate system towards PrintObject's coordinate system.
    Point                                   m_center_offset;

    // Object split into layer ranges and regions with their associated configurations.
    // Shared among PrintObjects created for the same ModelObject.
    PrintObjectRegions                     *m_shared_regions { nullptr };

    SlicingParameters                       m_slicing_params;
    LayerPtrs                               m_layers;
    SupportLayerPtrs                        m_support_layers;

    // this is set to true when LayerRegion->slices is split in top/internal/bottom
    // so that next call to make_perimeters() performs a union() before computing loops
    bool                    				m_typed_slices = false;
};

struct WipeTowerData
{
    // Following section will be consumed by the GCodeGenerator.
    // Tool ordering of a non-sequential print has to be known to calculate the wipe tower.
    // Cache it here, so it does not need to be recalculated during the G-code generation.
    ToolOrdering                                         &tool_ordering;
    // Cache of tool changes per print layer.
    std::unique_ptr<std::vector<WipeTower::ToolChangeResult>> priming;
    std::vector<std::vector<WipeTower::ToolChangeResult>> tool_changes;
    std::unique_ptr<WipeTower::ToolChangeResult>          final_purge;
    std::vector<float>                                    used_filament;
    int                                                   number_of_toolchanges;

    // Depth of the wipe tower to pass to GLCanvas3D for exact bounding box:
    float                                                 depth;
    float                                                 brim_width;

    void clear() {
        priming.reset(nullptr);
        tool_changes.clear();
        final_purge.reset(nullptr);
        used_filament.clear();
        number_of_toolchanges = -1;
        depth = 0.f;
        brim_width = 0.f;
    }

private:
	// Only allow the WipeTowerData to be instantiated internally by Print, 
	// as this WipeTowerData shares reference to Print::m_tool_ordering.
	friend class Print;
	WipeTowerData(ToolOrdering &tool_ordering) : tool_ordering(tool_ordering) { clear(); }
	WipeTowerData(const WipeTowerData & /* rhs */) = delete;
	WipeTowerData &operator=(const WipeTowerData & /* rhs */) = delete;
};

struct PrintStatistics
{
    PrintStatistics() { clear(); }
    std::string                     estimated_normal_print_time;
    std::string                     estimated_silent_print_time;
    double                          total_used_filament;
    double                          total_extruded_volume;
    double                          total_cost;
    int                             total_toolchanges;
    double                          total_weight;
    double                          total_wipe_tower_cost;
    double                          total_wipe_tower_filament;
    std::map<size_t, double>        filament_stats;

    // Config with the filled in print statistics.
    DynamicConfig           config() const;
    // Config with the statistics keys populated with placeholder strings.
    static DynamicConfig    placeholders();
    // Replace the print statistics placeholders in the path.
    std::string             finalize_output_path(const std::string &path_in) const;

    void clear() {
        total_used_filament    = 0.;
        total_extruded_volume  = 0.;
        total_cost             = 0.;
        total_toolchanges      = 0;
        total_weight           = 0.;
        total_wipe_tower_cost  = 0.;
        total_wipe_tower_filament = 0.;
        filament_stats.clear();
    }
};

typedef std::vector<PrintObject*>       PrintObjectPtrs;
typedef std::vector<const PrintObject*> ConstPrintObjectPtrs;
class ConstPrintObjectPtrsAdaptor : public ConstVectorOfPtrsAdaptor<PrintObject> {
    friend Print;
    ConstPrintObjectPtrsAdaptor(const PrintObjectPtrs *data) : ConstVectorOfPtrsAdaptor<PrintObject>(data) {}
};

typedef std::vector<PrintRegion*>       PrintRegionPtrs;
/*
typedef std::vector<const PrintRegion*> ConstPrintRegionPtrs;
class ConstPrintRegionPtrsAdaptor : public ConstVectorOfPtrsAdaptor<PrintRegion> {
    friend Print;
    ConstPrintRegionPtrsAdaptor(const PrintRegionPtrs *data) : ConstVectorOfPtrsAdaptor<PrintRegion>(data) {}
};
*/

// The complete print tray with possibly multiple objects.
class Print : public PrintBaseWithState<PrintStep, psCount>
{
private: // Prevents erroneous use by other classes.
    typedef PrintBaseWithState<PrintStep, psCount> Inherited;
    // Bool indicates if supports of PrintObject are top-level contour.
    typedef std::pair<PrintObject *, bool>         PrintObjectInfo;

public:
    Print() = default;
	virtual ~Print() { this->clear(); }

	PrinterTechnology	technology() const noexcept override { return ptFFF; }

    // Methods, which change the state of Print / PrintObject / PrintRegion.
    // The following methods are synchronized with process() and export_gcode(),
    // so that process() and export_gcode() may be called from a background thread.
    // In case the following methods need to modify data processed by process() or export_gcode(),
    // a cancellation callback is executed to stop the background processing before the operation.
    void                clear() override;
    bool                empty() const override { return m_objects.empty(); }
    // List of existing PrintObject IDs, to remove notifications for non-existent IDs.
    std::vector<ObjectID> print_object_ids() const override;

    ApplyStatus         apply(const Model &model, DynamicPrintConfig config) override;

    void                process() override;
    // Exports G-code into a file name based on the path_template, returns the file path of the generated G-code file.
    // If preview_data is not null, the preview_data is filled in for the G-code visualization (not used by the command line Slic3r).
    std::string         export_gcode(const std::string& path_template, GCodeProcessor::Result* result, ThumbnailsGeneratorCallback thumbnail_cb = nullptr);

    // methods for handling state
    bool                is_step_done(PrintStep step) const { return Inherited::is_step_done(step); }
    // Returns true if an object step is done on all objects and there's at least one object.    
    bool                is_step_done(PrintObjectStep step) const;
    // Returns true if the last step was finished with success.
    bool                finished() const override { return this->is_step_done(psGCodeExport); }

    bool                has_infinite_skirt() const;
    bool                has_skirt() const;
    bool                has_brim() const;

    // Returns an empty string if valid, otherwise returns an error message.
    std::string         validate(std::string* warning = nullptr) const override;
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
    ConstPrintObjectPtrsAdaptor objects() const { return ConstPrintObjectPtrsAdaptor(&m_objects); }
    PrintObject*                get_object(size_t idx) { return const_cast<PrintObject*>(m_objects[idx]); }
    const PrintObject*          get_object(size_t idx) const { return m_objects[idx]; }
    // PrintObject by its ObjectID, to be used to uniquely bind slicing warnings to their source PrintObjects
    // in the notification center.
    const PrintObject*          get_object(ObjectID object_id) const { 
        auto it = std::find_if(m_objects.begin(), m_objects.end(), 
            [object_id](const PrintObject *obj) { return obj->id() == object_id; });
        return (it == m_objects.end()) ? nullptr : *it;
    }
    // How many of PrintObject::copies() over all print objects are there?
    // If zero, then the print is empty and the print shall not be executed.
    unsigned int                num_object_instances() const;

    // For Perl bindings. 
    PrintObjectPtrs&            objects_mutable() { return m_objects; }
    PrintRegionPtrs&            print_regions_mutable() { return m_print_regions; }

    const ExtrusionEntityCollection& skirt() const { return m_skirt; }
    const ExtrusionEntityCollection& brim() const { return m_brim; }
    // Convex hull of the 1st layer extrusions, for bed leveling and placing the initial purge line.
    // It encompasses the object extrusions, support extrusions, skirt, brim, wipe tower.
    // It does NOT encompass user extrusions generated by custom G-code,
    // therefore it does NOT encompass the initial purge line.
    // It does NOT encompass MMU/MMU2 starting (wipe) areas.
    const Polygon&                   first_layer_convex_hull() const { return m_first_layer_convex_hull; }

    const PrintStatistics&      print_statistics() const { return m_print_statistics; }
    PrintStatistics&            print_statistics() { return m_print_statistics; }

    // Wipe tower support.
    bool                        has_wipe_tower() const;
    const WipeTowerData&        wipe_tower_data(size_t extruders_cnt = 0) const;
    const ToolOrdering& 		tool_ordering() const { return m_tool_ordering; }

	std::string                 output_filename(const std::string &filename_base = std::string()) const override;

    size_t                      num_print_regions() const throw() { return m_print_regions.size(); }
    const PrintRegion&          get_print_region(size_t idx) const  { return *m_print_regions[idx]; }
    const ToolOrdering&         get_tool_ordering() const { return m_wipe_tower_data.tool_ordering; }

#if ENABLE_SEQUENTIAL_LIMITS
    static bool sequential_print_horizontal_clearance_valid(const Print& print, Polygons* polygons = nullptr);
#endif // ENABLE_SEQUENTIAL_LIMITS

protected:
    // Invalidates the step, and its depending steps in Print.
    bool                invalidate_step(PrintStep step);

private:
    bool                invalidate_state_by_config_options(const ConfigOptionResolver &new_config, const std::vector<t_config_option_key> &opt_keys);

    void                _make_skirt();
    void                _make_wipe_tower();
    void                finalize_first_layer_convex_hull();

    // Islands of objects and their supports extruded at the 1st layer.
    Polygons            first_layer_islands() const;
    // Return 4 wipe tower corners in the world coordinates (shifted and rotated), including the wipe tower brim.
    std::vector<Point>  first_layer_wipe_tower_corners() const;

    PrintConfig                             m_config;
    PrintObjectConfig                       m_default_object_config;
    PrintRegionConfig                       m_default_region_config;
    PrintObjectPtrs                         m_objects;
    PrintRegionPtrs                         m_print_regions;

    // Ordered collections of extrusion paths to build skirt loops and brim.
    ExtrusionEntityCollection               m_skirt;
    ExtrusionEntityCollection               m_brim;
    // Convex hull of the 1st layer extrusions.
    // It encompasses the object extrusions, support extrusions, skirt, brim, wipe tower.
    // It does NOT encompass user extrusions generated by custom G-code,
    // therefore it does NOT encompass the initial purge line.
    // It does NOT encompass MMU/MMU2 starting (wipe) areas.
    Polygon                                 m_first_layer_convex_hull;
    Points                                  m_skirt_convex_hull;

    // Following section will be consumed by the GCodeGenerator.
    ToolOrdering 							m_tool_ordering;
    WipeTowerData                           m_wipe_tower_data {m_tool_ordering};

    // Estimated print time, filament consumed.
    PrintStatistics                         m_print_statistics;

    // To allow GCode to set the Print's GCodeExport step status.
    friend class GCode;
    // Allow PrintObject to access m_mutex and m_cancel_callback.
    friend class PrintObject;
};

} /* slic3r_Print_hpp_ */

#endif
