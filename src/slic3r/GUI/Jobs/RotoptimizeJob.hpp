#ifndef ROTOPTIMIZEJOB_HPP
#define ROTOPTIMIZEJOB_HPP

#include "Job.hpp"

namespace Slic3r { namespace GUI {

class Plater;

class RotoptimizeJob : public Job
{
    Plater *m_plater;
public:
    RotoptimizeJob(std::shared_ptr<ProgressIndicator> pri, Plater *plater)
        : Job{std::move(pri)}, m_plater{plater}
    {}
    
    void process() override;
    void finalize() override;
};

}} // namespace Slic3r::GUI

#endif // ROTOPTIMIZEJOB_HPP
