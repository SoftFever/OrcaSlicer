#ifndef slic3r_EmbossJob_hpp_
#define slic3r_EmbossJob_hpp_

#include <atomic>
#include <memory>
#include <string>
#include <functional>
#include <libslic3r/Emboss.hpp>
#include <libslic3r/EmbossShape.hpp> // ExPolygonsWithIds
#include "libslic3r/Point.hpp" // Transform3d
#include "libslic3r/ObjectID.hpp"

#include "slic3r/GUI/Camera.hpp"
//#include "slic3r/GUI/TextLines.hpp"
#include "slic3r/Utils/RaycastManager.hpp"
#include "JobNew.hpp"

namespace Slic3r {
class TriangleMesh;
class ModelVolume;
class ModelObject;
enum class ModelVolumeType : int;
class BuildVolume;
namespace GUI {
//class Plater;
class GLCanvas3D;
class Worker;
class Selection;
namespace Emboss {
class DataBase
{
public:
    DataBase(const std::string &volume_name, std::shared_ptr<std::atomic<bool>> cancel) : volume_name(volume_name), cancel(std::move(cancel)) {}
    DataBase(const std::string &volume_name, std::shared_ptr<std::atomic<bool>> cancel, EmbossShape &&shape)
        : volume_name(volume_name), cancel(std::move(cancel)), shape(std::move(shape))
    {}
    DataBase(DataBase &&) = default;
    virtual ~DataBase()   = default;

    /// <summary>
    /// Create shape
    /// e.g. Text extract glyphs from font
    /// Not 'const' function because it could modify shape
    /// </summary>
    virtual EmbossShape &create_shape() { return shape; };

    /// <summary>
    /// Write data how to reconstruct shape to volume
    /// </summary>
    /// <param name="volume">Data object for store emboss params</param>
    virtual void write(ModelVolume &volume) const;

    // Define projection move
    // True (raised) .. move outside from surface (MODEL_PART)
    // False (engraved).. move into object (NEGATIVE_VOLUME)
    bool is_outside = true;

    // Define per letter projection on one text line
    // [optional] It is not used when empty
    Slic3r::Emboss::TextLines text_lines = {};
    // [optional] Define distance for surface
    // It is used only for flat surface (not cutted)
    // Position of Zero(not set value) differ for MODEL_PART and NEGATIVE_VOLUME
    std::optional<float> from_surface;
    // new volume name
    std::string volume_name;
    // flag that job is canceled
    // for time after process.
    std::shared_ptr<std::atomic<bool>> cancel;
    // shape to emboss
    EmbossShape shape;
};

struct DataCreateVolumeUtil : public DataBase // modfiy bu bbs //struct DataCreateVolume : public DataBase
{
    // define embossed volume type
    ModelVolumeType volume_type;

    // parent ModelObject index where to create volume
    ObjectID object_id;

    // new created volume transformation
    Transform3d trmat;
};
using DataBasePtr = std::unique_ptr<DataBase>;
/// <summary>
/// Hold neccessary data to update embossed text object in job
/// </summary>
struct DataUpdate
{
    // Hold data about shape
    DataBasePtr base;

    // unique identifier of volume to change
    ObjectID volume_id;

    // Used for prevent flooding Undo/Redo stack on slider.
    bool make_snapshot;
};

    struct CreateVolumeParams
{
    GLCanvas3D &canvas;
    // Direction of ray into scene
    const Camera &camera;
    // To put new object on the build volume
    const BuildVolume &build_volume;
    // used to emplace job for execution
    Worker &worker;
    // Contain AABB trees from scene
    typedef std::function<void()> register_mesh_pick;
    register_mesh_pick on_register_mesh_pick{nullptr};
    RaycastManager &raycaster;
    RaycastManager::AllowVolumes& raycast_condition;
    // New created volume type
    ModelVolumeType volume_type;
    // Define which gizmo open on the success
    unsigned char gizmo_type; // GLGizmosManager::EType
    // Volume define object to add new volume
    const GLVolume *gl_volume;
    // Contain AABB trees from scene
    // RaycastManager &raycaster;
    // Wanted additionl move in Z(emboss) direction of new created volume
    std::optional<float> distance = {};
    // Wanted additionl rotation around Z of new created volume
    std::optional<float> angle = {};
};
struct DataCreateObject
{
    // Hold data about shape
    DataBasePtr base;
    // define position on screen where to create object
    Vec2d screen_coor;
    // projection property
    const Camera &camera;
    // shape of bed in case of create volume on bed
    std::vector<Vec2d> bed_shape;
    // Define which gizmo open on the success
    unsigned char gizmo_type;
    // additionl rotation around Z axe, given by style settings
    std::optional<float> angle = {};
};

 /// <summary>
/// Hold neccessary data to create ModelVolume in job
/// Volume is created on the surface of existing volume in object.
/// NOTE: EmbossDataBase::font_file doesn't have to be valid !!!
/// </summary>
struct DataCreateVolume
{
    // Hold data about shape
    DataBasePtr base;

    // define embossed volume type
    ModelVolumeType volume_type;

    // parent ModelObject index where to create volume
    ObjectID object_id;

    // new created volume transformation
    std::optional<Transform3d> trmat;

    // Define which gizmo open on the success
    unsigned char gizmo_type;
};

struct SurfaceVolumeData
{
    // Transformation of volume inside of object
    Transform3d transform;

    struct ModelSource
    {
        // source volumes
        std::shared_ptr<const TriangleMesh> mesh;
        // Transformation of volume inside of object
        Transform3d tr;
    };
    using ModelSources = std::vector<ModelSource>;
    ModelSources sources;
};


    /// <summary>
/// Hold neccessary data to update embossed text object in job
/// </summary>
struct UpdateSurfaceVolumeData : public DataUpdate, public SurfaceVolumeData
{};

static bool was_canceled(const JobNew::Ctl &ctl, const DataBase &base);
static bool exception_process(std::exception_ptr &eptr);
static bool finalize(bool canceled, std::exception_ptr &eptr, const DataBase &input);
/// <summary>
/// Update text volume to use surface from object
/// </summary>
class UpdateSurfaceVolumeJob : public JobNew
{
    UpdateSurfaceVolumeData m_input;
    TriangleMesh            m_result;

public:
    // move params to private variable
    explicit UpdateSurfaceVolumeJob(UpdateSurfaceVolumeData &&input);
    void process(Ctl &ctl) override;
    void finalize(bool canceled, std::exception_ptr &eptr) override;
    static bool is_use_surfae_error;
};

/// <summary>
/// Update text shape in existing text volume
/// Predict that there is only one runnig(not canceled) instance of it
/// </summary>
class UpdateJob : public JobNew
{
    DataUpdate   m_input;
    TriangleMesh m_result;

public:
    // move params to private variable
    explicit UpdateJob(DataUpdate &&input);

    /// <summary>
    /// Create new embossed volume by m_input data and store to m_result
    /// </summary>
    /// <param name="ctl">Control containing cancel flag</param>
    void process(Ctl &ctl) override;

    /// <summary>
    /// Update volume - change object_id
    /// </summary>
    /// <param name="canceled">Was process canceled.
    /// NOTE: Be carefull it doesn't care about
    /// time between finished process and started finalize part.</param>
    /// <param name="">unused</param>
    void finalize(bool canceled, std::exception_ptr &eptr) override;

    /// <summary>
    /// Update text volume
    /// </summary>
    /// <param name="volume">Volume to be updated</param>
    /// <param name="mesh">New Triangle mesh for volume</param>
    /// <param name="base">Data to write into volume</param>
    static void update_volume(ModelVolume *volume, TriangleMesh &&mesh, const DataBase &base);
};

/// <summary>
/// Create new TextObject on the platter
/// Should not be stopped
/// </summary>
class CreateObjectJob : public JobNew
{
    DataCreateObject m_input;
    TriangleMesh     m_result;
    Transform3d      m_transformation;

public:
    explicit CreateObjectJob(DataCreateObject &&input);
    void process(Ctl &ctl) override;
    void finalize(bool canceled, std::exception_ptr &eptr) override;
};

struct Texture
{
    unsigned id{0};
    unsigned width{0};
    unsigned height{0};
};

/// <summary>
/// Hold neccessary data to create(cut) volume from surface object in job
/// </summary>
struct CreateSurfaceVolumeData : public SurfaceVolumeData
{
    // Hold data about shape
    DataBasePtr base;
    // define embossed volume type
    ModelVolumeType volume_type;
    // parent ModelObject index where to create volume
    ObjectID object_id;
    // Define which gizmo open on the success
    unsigned char gizmo_type;
};
class CreateSurfaceVolumeJob : public JobNew
{
    CreateSurfaceVolumeData m_input;
    TriangleMesh            m_result;

public:
    explicit CreateSurfaceVolumeJob(CreateSurfaceVolumeData &&input);
    void process(Ctl &ctl) override;
    void finalize(bool canceled, std::exception_ptr &eptr) override;
};

class CreateVolumeJob : public JobNew
{
    DataCreateVolume m_input;
    TriangleMesh     m_result;

public:
    explicit CreateVolumeJob(DataCreateVolume &&input);
    void process(Ctl &ctl) override;
    void finalize(bool canceled, std::exception_ptr &eptr) override;
};
static bool check(unsigned char gizmo_type);
static bool check(const DataBase &input, bool check_fontfile, bool use_surface = false);
static bool check(const CreateVolumeParams &input);
static bool check(const DataCreateObject &input);
static bool check(const DataUpdate &input, bool is_main_thread = false, bool use_surface = false);
static bool check(const CreateSurfaceVolumeData &input, bool is_main_thread = false);
static bool check(const DataCreateVolume &input, bool is_main_thread = false);
static bool check(const UpdateSurfaceVolumeData &input, bool is_main_thread = false);
bool        start_create_object_job(const CreateVolumeParams &input, DataBasePtr emboss_data, const Vec2d &coor);
bool        start_create_volume_without_position(CreateVolumeParams &input, DataBasePtr data);
bool        start_create_volume_job( Worker &worker, const ModelObject &object, const std::optional<Transform3d> &volume_tr, DataBasePtr data, ModelVolumeType volume_type, unsigned char gizmo_type);
bool start_create_volume_on_surface_job(CreateVolumeParams &input, DataBasePtr data, const Vec2d &mouse_pos);
bool start_create_volume(CreateVolumeParams &input, DataBasePtr data, const Vec2d &mouse_pos);
static ExPolygons           create_shape(DataBase &input);
static TriangleMesh         create_mesh_per_glyph(DataBase &input);
static TriangleMesh         try_create_mesh(DataBase &input);
static TriangleMesh         create_mesh(DataBase &input);
static indexed_triangle_set cut_surface_to_its(const ExPolygons &shapes, const Transform3d &tr, const SurfaceVolumeData::ModelSources &sources, DataBase &input);
static TriangleMesh         cut_per_glyph_surface(DataBase &input1, const SurfaceVolumeData &input2);
static TriangleMesh         cut_surface(DataBase &input1, const SurfaceVolumeData &input2);
static void                 _update_volume(TriangleMesh &&mesh, const DataUpdate &data, const Transform3d *tr = nullptr);
static void                 create_volume(
                    TriangleMesh &&mesh, const ObjectID &object_id, const ModelVolumeType type, const std::optional<Transform3d> &trmat, const DataBase &data, unsigned char gizmo_type);
/// Update text volume
/// </summary>
/// <param name="volume">Volume to be updated</param>
/// <param name="mesh">New Triangle mesh for volume</param>
/// <param name="base">Data to write into volume</param>
bool start_update_volume(DataUpdate &&data, const ModelVolume &volume, const Selection &selection, RaycastManager &raycaster);

/// <summary>
/// Copied triangles from object to be able create mesh for cut surface from
/// </summary>
/// <param name="volume">Define embossed volume</param>
/// <returns>Source data for cut surface from</returns>
SurfaceVolumeData::ModelSources create_volume_sources(const ModelVolume &volume);
/// <summary>
/// Copied triangles from object to be able create mesh for cut surface from
/// </summary>
/// <param name="volumes">Source object volumes for cut surface from</param>
/// <param name="text_volume_id">Source volume id</param>
/// <returns>Source data for cut surface from</returns>
SurfaceVolumeData::ModelSources create_sources(const ModelVolumePtrs &volumes, std::optional<size_t> text_volume_id = {});
}
} // namespace Slic3r::GUI
} // namespace Slic3r

#endif // slic3r_EmbossJob_hpp_
