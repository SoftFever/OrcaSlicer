#ifndef slic3r_GCode_SmallAreaInfillFlowCompensator_hpp_
#define slic3r_GCode_SmallAreaInfillFlowCompensator_hpp_

#include "../libslic3r.h"
#include "../PrintConfig.hpp"
#include "../ExtrusionEntity.hpp"
#include "spline/spline.h"
#include <memory>

namespace Slic3r {

// Currently on Linux/macOS, this class spits out large amounts of subobject linkage
// warnings because of the flowModel field. tk::spline is in an anonymous namespace which
// causes this issue. Until the issue can be solved, this is a temporary solution.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsubobject-linkage"

class SmallAreaInfillFlowCompensator
{
public:
    SmallAreaInfillFlowCompensator() = delete;
    explicit SmallAreaInfillFlowCompensator(const Slic3r::GCodeConfig& config);
    ~SmallAreaInfillFlowCompensator() = default;

    double modify_flow(const double line_length, const double dE, const ExtrusionRole role);

private:
    // Model points
    std::vector<double> eLengths;
    std::vector<double> flowComps;

    // TODO: Cubic Spline
    std::unique_ptr<tk::spline> flowModel;

    double flow_comp_model(const double line_length);

    double max_modified_length() { return eLengths.back(); }
};

#pragma GCC diagnostic pop

} // namespace Slic3r

#endif /* slic3r_GCode_SmallAreaInfillFlowCompensator_hpp_ */
