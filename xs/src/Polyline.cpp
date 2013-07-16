#include "Polyline.hpp"

namespace Slic3r {

Lines
Polyline::lines()
{
    Lines lines;
    for (int i = 0; i < this->points.size()-1; i++) {
        lines.push_back(Line(this->points[i], this->points[i+1]));
    }
    return lines;
}

}
