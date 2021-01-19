#ifndef ROTOPTIMIZEJOB_HPP
#define ROTOPTIMIZEJOB_HPP

#include "PlaterJob.hpp"

namespace Slic3r { namespace GUI {

class RotoptimizeJob : public PlaterJob
{
public:
    RotoptimizeJob(std::shared_ptr<ProgressIndicator> pri, Plater *plater)
        : PlaterJob{std::move(pri), plater}
    {}
    
    void process() override;
    void finalize() override;
};

}} // namespace Slic3r::GUI

#endif // ROTOPTIMIZEJOB_HPP
