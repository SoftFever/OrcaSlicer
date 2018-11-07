#include "SLAPrint.hpp"

namespace Slic3r {

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
