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

// This will be the driver to create background threads. Possibly
// implemented using a BackgroundSlicingProcess or something derived from that
// The methods should be thread safe, obviously...
class BackgroundProcess {
    std::function<void()> m_synchfn;
public:

    virtual ~BackgroundProcess() {}

    /// schedule a task on the background
    virtual void schedule(std::function<void()> fn) = 0;

    /// Report status change, used inside the worker thread
    virtual void status(unsigned st, const std::string& msg) = 0;

    /// Check whether the calculation was canceled from the UI. Called by the
    /// worker thread
    virtual bool is_canceled() = 0;

    /// Determine the state of the background process. If something is running
    /// returns true. If no job is running, returns false.
    virtual bool is_running() = 0;

    /// Trigger the synchronization of frontend/backend data
    virtual void input_changed() {
        m_synchfn();  // will just call the provided synch function itself.
    }

    /// Setting up a callback that transfers the input parameters to the worker
    /// thread. Appropriate synchronization has to be implemented here. A simple
    /// condition variable and mutex pair should do the job.
    void on_input_changed(std::function<void()> synchfn) {
        m_synchfn = synchfn;
    }
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
 * TODO: This class uses the BackgroundProcess interface to create workers and
 * manage input change events. An appropriate implementation can be derived
 * from BackgroundSlicingProcess which is now working only with the FDM Print.
 */
class SLAPrint /* : public Print */ {
public:

    enum CallType {
        BLOCKING, NON_BLOCKING
    };

    // Global SLA printing configuration
    struct GlobalConfig {
        double width_mm;
        double height_mm;
        unsigned long width_px;
        unsigned long height_px;
        // ...
    };

private:

    // Caching instance transformations and slices
    struct PrintObjectInstance {
        Transform3f tr;
        SlicedSupports slice_cache;
    };

    using InstanceMap = std::unordered_map<ModelInstance*, PrintObjectInstance>;

    // Every ModelObject will have its PrintObject here. It will contain the
    // support tree geometry, the cached index-triangle structure (emesh) and
    // the map of the instance cache
    struct PrintObject {
        sla::EigenMesh3D emesh;
        std::unique_ptr<sla::SLASupportTree> support_tree_ptr;
        InstanceMap instances;
    };

    // Map definition for the print objects
    using ObjectMap = std::unordered_map<ModelObject*, PrintObject>;

    // Input data channels: ***************************************************

    const Model *m_model = nullptr; // The model itself

    // something to read out the config profiles and return the values we need.
    std::function<GlobalConfig()> m_config_reader;

    // ************************************************************************

    GlobalConfig m_gcfg;        // The global SLA print config instance
    ObjectMap m_data;           // The model data cache (PrintObject's)
    std::shared_ptr<BackgroundProcess> m_process; // The scheduler

    // For now it will just stop the whole process and invalidate everything
    std::atomic<bool> m_dirty;

    enum Stages {
        IDLE,
        FIND_ROTATION,
        SUPPORT_POINTS,
        SUPPORT_TREE,
        BASE_POOL,
        SLICE_MODEL,
        SLICE_SUPPORTS,
        EXPORT,
        DONE,
        ABORT,
        NUM_STAGES
    };

    static const std::string m_stage_labels[NUM_STAGES];

    void _start();
    void _synch();

public:

    void set_scheduler(std::shared_ptr<BackgroundProcess> scheduler) {
        if(scheduler && !scheduler->is_running()) {
            m_process = scheduler;
            m_process->on_input_changed([this] {
                _synch();
            });
        }
    }

    SLAPrint(const Model * model,
             std::function<SLAPrint::GlobalConfig(void)> cfgreader =
                    [](){ return SLAPrint::GlobalConfig(); },
             std::shared_ptr<BackgroundProcess> scheduler = {}):
        m_model(model), m_config_reader(cfgreader)
    {
        set_scheduler(scheduler);
    }

    void synch() {
        if(m_process) m_process->input_changed();
    }

    // This will start the calculation using the
    bool start();

    // Get the full support structure (including the base pool)
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
