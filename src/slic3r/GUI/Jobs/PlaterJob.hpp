#ifndef PLATERJOB_HPP
#define PLATERJOB_HPP

#include "Job.hpp"

namespace Slic3r { namespace GUI {

class Plater;
class NotificationManager;

class PlaterJob : public Job {
protected:
    Plater *m_plater;

    void on_exception(const std::exception_ptr &) override;

public:

    PlaterJob(std::shared_ptr<NotificationManager> nm, Plater *plater):
        Job{nm}, m_plater{plater} {}
};

}} // namespace Slic3r::GUI

#endif // PLATERJOB_HPP
