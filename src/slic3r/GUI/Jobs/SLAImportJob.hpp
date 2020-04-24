#ifndef SLAIMPORTJOB_HPP
#define SLAIMPORTJOB_HPP

#include "Job.hpp"

namespace Slic3r { namespace GUI {

class Plater;

class SLAImportJob : public Job {    
    class priv;
    
    std::unique_ptr<priv> p;
    
public:
    SLAImportJob(std::shared_ptr<ProgressIndicator> pri, Plater *plater);
    ~SLAImportJob();

    void process() override;
    
    void reset();
    
protected:
    void prepare() override;
    
    void finalize() override;
};

}}     // namespace Slic3r::GUI

#endif // SLAIMPORTJOB_HPP
