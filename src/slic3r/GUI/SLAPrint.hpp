#include <libslic3r.h>
#include "Point.hpp"
#include "SLA/SLASupportTree.hpp"

#include <string>
#include <memory>
#include <unordered_map>
#include <atomic>

namespace Slic3r {

class Model;
class ModelObject;
class ModelInstance;
class GLCanvas3D;
class DynamicPrintConfig;

// Ok, this will be the driver to create background threads. Possibly
// implemented using a BackgroundSlicingProcess or something derived from that
// The methods should be thread safe, obviously...
class BackgroundProcess {
public:

    virtual ~BackgroundProcess() {}

    virtual void schedule(std::function<void()> fn) = 0;

    virtual void status(unsigned st, const std::string& msg) = 0;

    virtual bool is_canceled() = 0;

    virtual void on_input_changed(std::function<void()> synchfn) = 0;

    virtual bool is_running() = 0;
};

/**
 * @brief This class is the high level FSM for the SLA printing process.
 *
 * It should support the background processing framework and contain the
 * metadata for the support geometries and their slicing. It should also
 * dispatch the SLA printing configuration values to the appropriate calculation
 * steps.
 *
 * TODO (decide): The last important feature is the support for visualization
 * which (at least for now) will be implemented as a method(s) returning the
 * triangle meshes or receiving the rendering canvas and drawing on that
 * directly.
 *
 * TODO: This class has to be implement the Print interface (not defined yet)
 * to be usable as the input to the BackgroundSlicingProcess.
 */
class SLAPrint /* : public Print */ {
public:

    enum CallType {
        BLOCKING, NON_BLOCKING
    };

    struct GlobalConfig {
        double width_mm;
        double height_mm;
        unsigned long width_px;
        unsigned long height_px;
        // ...
    };

private:

    struct PrintObjectInstance {
        Transform3f tr;
        std::unique_ptr<sla::SLASupportTree> support_tree_ptr;
        SlicedSupports slice_cache;
    };

    using InstanceMap = std::unordered_map<ModelInstance*, PrintObjectInstance>;

    struct PrintObject {
        sla::EigenMesh3D emesh;
        InstanceMap instances;
    };

    using ObjectMap = std::unordered_map<ModelObject*, PrintObject>;

    // Input data channels: ***************************************************

    const Model *m_model = nullptr; // The model itself

    // something to read out the config profiles and return the values we need.
    std::function<GlobalConfig()> m_config_reader;
    // ************************************************************************

    GlobalConfig m_gcfg;
    ObjectMap m_data;
    std::shared_ptr<BackgroundProcess> m_process;

    // For now it will just stop the whole process and invalidate everything
    void synch();
    std::atomic<bool> m_dirty;

    void set_scheduler(std::shared_ptr<BackgroundProcess> scheduler) {
        if(scheduler && !scheduler->is_running()) {
            m_process = scheduler;
            m_process->on_input_changed([this] {
                /*synch(); */
                m_dirty.store(true);
            });
        }
    }

public:

    SLAPrint(const Model * model,
                    std::function<SLAPrint::GlobalConfig(void)> cfgreader = [](){ return SLAPrint::GlobalConfig(); },
                    std::shared_ptr<BackgroundProcess> scheduler = {}):
        m_model(model), m_config_reader(cfgreader)
    {
        synch();
        m_dirty.store(false);
        set_scheduler(scheduler);
    }

    // This will start the calculation using the
    bool start(std::shared_ptr<BackgroundProcess> scheduler);

    // Get the full support structure (including the supports)
    // This should block until the supports are not ready?
    bool support_mesh(TriangleMesh& output, CallType calltype = BLOCKING);

    // Exporting to the final zip file, or possibly other format.
    // filepath is reserved to be a zip filename or directory or anything
    // that a particular format requires.
    // (I know, we will use zipped PNG, but everything changes here so quickly)
    bool export_layers(const std::string& filepath,
                       CallType calltype = BLOCKING);
};

}
