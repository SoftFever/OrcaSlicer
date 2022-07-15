#ifndef slic3r_CurvaAnalyzer_hpp_
#define slic3r_CurvaAnalyzer_hpp_

#include "ExtrusionEntityCollection.hpp"

namespace Slic3r {

enum class ECurveAnalyseMode : unsigned char
{
    RelativeMode,
    AbsoluteMode,
    Count
};

//BBS: CurvaAnalyzer, ansolutely new file
class CurveAnalyzer {
public:
    // This function is used to calculate curvature for paths.
    // Paths must be generated from a closed polygon.
    // Data in paths may be modify, and paths will be spilited and regenerated
    // arrording to different curve degree.
    void calculate_curvatures(ExtrusionPaths& paths, ECurveAnalyseMode mode = ECurveAnalyseMode::RelativeMode);
};

}

#endif
