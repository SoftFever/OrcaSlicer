#ifndef slic3r_Print_hpp_
#define slic3r_Print_hpp_

#include <myinit.h>
#include <set>

namespace Slic3r {

enum PrintStep {
    psInitExtruders, psSlice, psPerimeters, prPrepareInfill,
    psInfill, psSupportMaterial, psSkirt, psBrim,
};

class PrintState
{
    private:
    std::set<PrintStep> _started;
    std::set<PrintStep> _done;
    
    public:
    bool started(PrintStep step) const;
    bool done(PrintStep step) const;
    void set_started(PrintStep step);
    void set_done(PrintStep step);
    void invalidate(PrintStep step);
    void invalidate_all();
};

}

#endif
