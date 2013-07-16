#include "ExtrusionEntity.hpp"

namespace Slic3r {

void
ExtrusionPath::reverse()
{
    this->polyline.reverse();
}

ExtrusionPath*
ExtrusionLoop::split_at_index(int index)
{
    Polyline* poly = this->polygon.split_at_index(index);
    
    ExtrusionPath* path = new ExtrusionPath();
    path->polyline      = *poly;
    path->role          = this->role;
    path->height        = this->height;
    path->flow_spacing  = this->flow_spacing;
    
    delete poly;
    return path;
}

ExtrusionPath*
ExtrusionLoop::split_at_first_point()
{
    return this->split_at_index(0);
}

}
