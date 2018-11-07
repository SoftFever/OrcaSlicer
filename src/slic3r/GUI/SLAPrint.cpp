#include "SLAPrint.hpp"
#include "GUI.hpp"

namespace Slic3r {

const std::string SLAPrint::m_stage_labels[] = {
    "", // IDLE,
    L("Finding best rotation"), // FIND_ROTATION,
    L("Scanning model structure"), // SUPPORT_POINTS,
    L("Generating support tree"), // SUPPORT_TREE,
    L("Generating base pool"), // BASE_POOL,
    L("Slicing model"), // SLICE_MODEL,
    L("Slicing supports"), // SLICE_SUPPORTS,
    L("Exporting layers"), // EXPORT,
    L("Prepared for printing"), // DONE,
    L("SLA print preparation aborted")  // ABORT,
};

void SLAPrint::synch() {
    m_gcfg = m_config_reader();
    // TODO: check model objects and instances
}

bool SLAPrint::start(std::shared_ptr<BackgroundProcess> scheduler) {
    if(!m_process || !m_process->is_running()) set_scheduler(scheduler);
    if(!m_process) return false;

    m_process->schedule([this, scheduler](){

    });

    return true;
}

}
