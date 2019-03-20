#ifndef slic3r_SLAPrint_hpp_
#define slic3r_SLAPrint_hpp_

#include <mutex>
#include "PrintBase.hpp"
#include "PrintExport.hpp"
#include "Point.hpp"
#include "MTUtils.hpp"

namespace Slic3r {

enum SLAPrintStep : unsigned int {
	slapsRasterize,
	slapsValidate,
	slapsCount
};

enum SLAPrintObjectStep : unsigned int {
	slaposObjectSlice,
	slaposSupportPoints,
	slaposSupportTree,
	slaposBasePool,
	slaposSliceSupports,
    slaposIndexSlices,
	slaposCount
};

class SLAPrint;
class GLCanvas;

using _SLAPrintObjectBase =
    PrintObjectBaseWithState<SLAPrint, SLAPrintObjectStep, slaposCount>;

// Layers according to quantized height levels. This will be consumed by
// the printer (rasterizer) in the SLAPrint class.
using LevelID = long long;

template<class It> struct Range {
    It from, to;
    It begin() const { return from; }
    It end() const { return to; }
    using Type = It;

    Range() = default;
    explicit Range(It &&b, It &&e):
        from(std::forward<It>(b)), to(std::forward<It>(e)) {}
};

enum SliceOrigin { soSlice, soModel };

using SliceStore = std::vector<ExPolygons>;
using SliceIterator = SliceStore::const_iterator;
using SliceRange = Range<SliceIterator>;

class SLAPrintObject : public _SLAPrintObjectBase
{
private: // Prevents erroneous use by other classes.
    using Inherited = _SLAPrintObjectBase;

public:

    // I refuse to grantee copying (Tamas)
    SLAPrintObject(const SLAPrintObject&) = delete;
    SLAPrintObject& operator=(const SLAPrintObject&) = delete;

    const SLAPrintObjectConfig& config() const { return m_config; }
    const Transform3d&          trafo()  const { return m_trafo; }

    struct Instance {
    	Instance(ModelID instance_id, const Point &shift, float rotation) : instance_id(instance_id), shift(shift), rotation(rotation) {}
		bool operator==(const Instance &rhs) const { return this->instance_id == rhs.instance_id && this->shift == rhs.shift && this->rotation == rhs.rotation; }
    	// ID of the corresponding ModelInstance.
		ModelID instance_id;
		// Slic3r::Point objects in scaled G-code coordinates
    	Point 	shift;
    	// Rotation along the Z axis, in radians.
    	float 	rotation;
	};
    const std::vector<Instance>& instances() const { return m_instances; }

    bool                    has_mesh(SLAPrintObjectStep step) const;
    TriangleMesh            get_mesh(SLAPrintObjectStep step) const;

    // Get a support mesh centered around origin in XY, and with zero rotation around Z applied.
    // Support mesh is only valid if this->is_step_done(slaposSupportTree) is true.
    const TriangleMesh&     support_mesh() const;
    // Get a pad mesh centered around origin in XY, and with zero rotation around Z applied.
    // Support mesh is only valid if this->is_step_done(slaposBasePool) is true.
    const TriangleMesh&     pad_mesh() const;

    // This will return the transformed mesh which is cached
    const TriangleMesh&     transformed_mesh() const;

    std::vector<sla::SupportPoint>      transformed_support_points() const;

    // Get the needed Z elevation for the model geometry if supports should be
    // displayed. This Z offset should also be applied to the support
    // geometries. Note that this is not the same as the value stored in config
    // as the pad height also needs to be considered.
    double get_elevation() const;

    // This method returns the needed elevation according to the processing
    // status. If the supports are not ready, it is zero, if they are and the
    // pad is not, then without the pad, otherwise the full value is returned.
    double get_current_elevation() const;

    // This method returns the support points of this SLAPrintObject.
    const std::vector<sla::SupportPoint>& get_support_points() const;

private:

    // An index record referencing the slices
    // (get_model_slices(), get_support_slices()) where the keys are the height
    // levels of the model in scaled-clipper coordinates. The levels correspond
    // to the z coordinate of the object coordinate system.
    class SliceRecord {
    public:
        using Key = LevelID;

    private:
        using Idx = size_t;
        static const Idx NONE = Idx(-1); // this will be the max limit of size_t

        LevelID m_print_z = 0;  // Top of the layer
        float m_slice_z = 0.f;    // Exact level of the slice
        float m_height = 0.f;     // Height of the sliced layer
        Idx m_model_slices_idx = NONE;
        Idx m_support_slices_idx = NONE;

    public:

        SliceRecord(Key key, float slicez, float height):
            m_print_z(key), m_slice_z(slicez), m_height(height) {}

        inline static bool cmpfn(const SliceRecord& sr1, const SliceRecord& sr2)
        {
            return sr1.key() < sr2.key();
        }

        inline Key key() const { return m_print_z; }
        inline float slice_level() const { return m_slice_z; }
        inline float layer_height() const { return m_height; }

        SliceIterator get_slices(const SLAPrintObject& po,
                                 SliceOrigin so) const;

        void set_model_slice_idx(Idx id) { m_model_slices_idx = id; }
        void set_support_slice_idx(Idx id) { m_support_slices_idx = id; }
    };


    // Retrieve the slice index which is readable only after slaposIndexSlices
    // is done.
    const std::vector<SliceRecord>& get_slice_index() const;

    std::vector<float> get_slice_levels(float from_eq) const;

public:

    SliceIterator get_slices(SliceOrigin so, LevelID k) const;

    SliceRange get_slices(
            SliceOrigin so,
            float from_level,
            float to_level = std::numeric_limits<float>::infinity()) const;

    // These two methods should be callable on the client side (e.g. UI thread)
    // when the appropriate steps slaposObjectSlice and slaposSliceSupports
    // are ready. All the print objects are processed before slapsRasterize so
    // it is safe to call them during and/or after slapsRasterize.
    const std::vector<ExPolygons>& get_model_slices() const;
    const std::vector<ExPolygons>& get_support_slices() const;

protected:
    // to be called from SLAPrint only.
    friend class SLAPrint;

	SLAPrintObject(SLAPrint* print, ModelObject* model_object);
    ~SLAPrintObject();

    void                    config_apply(const ConfigBase &other, bool ignore_nonexistent = false) { this->m_config.apply(other, ignore_nonexistent); }
    void                    config_apply_only(const ConfigBase &other, const t_config_option_keys &keys, bool ignore_nonexistent = false) 
    	{ this->m_config.apply_only(other, keys, ignore_nonexistent); }

    void                    set_trafo(const Transform3d& trafo) {
        m_transformed_rmesh.invalidate([this, &trafo](){ m_trafo = trafo; });
    }

    void                    set_instances(const std::vector<Instance> &instances) { m_instances = instances; }
    // Invalidates the step, and its depending steps in SLAPrintObject and SLAPrint.
    bool                    invalidate_step(SLAPrintObjectStep step);
    bool                    invalidate_all_steps();
    // Invalidate steps based on a set of parameters changed.
    bool                    invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys);

    // Which steps have to be performed. Implicitly: all
    // to be accessible from SLAPrint
    std::vector<bool>                       m_stepmask;

private:
    // Object specific configuration, pulled from the configuration layer.
    SLAPrintObjectConfig                    m_config;
    // Translation in Z + Rotation by Y and Z + Scaling / Mirroring.
    Transform3d                             m_trafo = Transform3d::Identity();
    std::vector<Instance> 					m_instances;

    // Individual 2d slice polygons from lower z to higher z levels
    std::vector<ExPolygons>                 m_model_slices;

    // Exact (float) height levels mapped to the slices. Each record contains
    // the index to the model and the support slice vectors.
    std::vector<SliceRecord>                m_slice_index;

    // The height levels corrected and scaled up in integer values. This will
    // be used at rasterization.
    std::vector<LevelID>                    m_level_ids;
    std::vector<float>                      m_height_levels;

    // Caching the transformed (m_trafo) raw mesh of the object
    mutable CachedObject<TriangleMesh>      m_transformed_rmesh;

    class SupportData;
    std::unique_ptr<SupportData> m_supportdata;
};

using PrintObjects = std::vector<SLAPrintObject*>;

class TriangleMesh;

struct SLAPrintStatistics
{
    SLAPrintStatistics() { clear(); }
    std::string                     estimated_print_time;
    double                          objects_used_material;
    double                          support_used_material;
    size_t                          slow_layers_count;
    size_t                          fast_layers_count;
    double                          total_cost;
    double                          total_weight;

    // Config with the filled in print statistics.
    DynamicConfig           config() const;
    // Config with the statistics keys populated with placeholder strings.
    static DynamicConfig    placeholders();
    // Replace the print statistics placeholders in the path.
    std::string             finalize_output_path(const std::string &path_in) const;

    void clear() {
        estimated_print_time.clear();
        objects_used_material = 0.;
        support_used_material = 0.;
        slow_layers_count = 0;
        fast_layers_count = 0;
        total_cost = 0.;
        total_weight = 0.;
    }
};

/**
 * @brief This class is the high level FSM for the SLA printing process.
 *
 * It should support the background processing framework and contain the
 * metadata for the support geometries and their slicing. It should also
 * dispatch the SLA printing configuration values to the appropriate calculation
 * steps.
 */
class SLAPrint : public PrintBaseWithState<SLAPrintStep, slapsCount>
{
private: // Prevents erroneous use by other classes.
    typedef PrintBaseWithState<SLAPrintStep, slapsCount> Inherited;

public:
    SLAPrint(): m_stepmask(slapsCount, true) {}

    virtual ~SLAPrint() override { this->clear(); }

    PrinterTechnology	technology() const noexcept override { return ptSLA; }

    void                clear() override;
    bool                empty() const override { return m_objects.empty(); }
    ApplyStatus         apply(const Model &model, const DynamicPrintConfig &config) override;
    void                set_task(const TaskParams &params) override;
    void                process() override;
    void                finalize() override;
    // Returns true if an object step is done on all objects and there's at least one object.    
    bool                is_step_done(SLAPrintObjectStep step) const;
    // Returns true if the last step was finished with success.
	bool                finished() const override { return this->is_step_done(slaposIndexSlices) && this->Inherited::is_step_done(slapsRasterize); }

    template<class Fmt> void export_raster(const std::string& fname) {
        if(m_printer) m_printer->save<Fmt>(fname);
    }
    const PrintObjects& objects() const { return m_objects; }

	std::string         output_filename() const override;

    const SLAPrintStatistics&      print_statistics() const { return m_print_statistics; }

    std::string validate() const override;

private:
    using SLAPrinter = FilePrinter<FilePrinterFormat::SLA_PNGZIP>;
    using SLAPrinterPtr = std::unique_ptr<SLAPrinter>;

    // Invalidate steps based on a set of parameters changed.
    bool invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys);

    std::vector<float> calculate_heights(const BoundingBoxf3& bb,
                                         float elevation,
                                         float initial_layer_height,
                                         float layer_height) const;

    void fill_statistics();

    SLAPrintConfig                  m_print_config;
    SLAPrinterConfig                m_printer_config;
    SLAMaterialConfig               m_material_config;
    SLAPrintObjectConfig            m_default_object_config;

    PrintObjects                    m_objects;
    std::vector<bool>               m_stepmask;

    // Definition of the print input map. It consists of the slices indexed
    // with scaled (clipper) Z coordinates. Also contains the instance
    // transformations in scaled and filtered version. This is enough for the
    // rasterizer to be able to draw every layer in the right position
    using Layer = ExPolygons;
    using LayerCopies = std::vector<SLAPrintObject::Instance>;
    struct LayerRef {
        std::reference_wrapper<const Layer> lref;
        std::reference_wrapper<const LayerCopies> copies;
        LayerRef(const Layer& lyr, const LayerCopies& cp) :
            lref(std::cref(lyr)), copies(std::cref(cp)) {}
    };

    // One level may contain multiple slices from multiple objects and their
    // supports
    using LayerRefs = std::vector<LayerRef>;
    std::map<LevelID, LayerRefs>            m_printer_input;

    // The printer itself
    SLAPrinterPtr                           m_printer;

    // Estimated print time, material consumed.
    SLAPrintStatistics                      m_print_statistics;

	friend SLAPrintObject;
};

} // namespace Slic3r

#endif /* slic3r_SLAPrint_hpp_ */
