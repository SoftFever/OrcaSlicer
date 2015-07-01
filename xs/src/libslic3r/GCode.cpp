#include "GCode.hpp"

namespace Slic3r {

AvoidCrossingPerimeters::AvoidCrossingPerimeters()
    : use_external_mp(false), use_external_mp_once(false), disable_once(true),
        _external_mp(NULL), _layer_mp(NULL)
{
}

AvoidCrossingPerimeters::~AvoidCrossingPerimeters()
{
    if (this->_external_mp != NULL)
        delete this->_external_mp;
    
    if (this->_layer_mp != NULL)
        delete this->_layer_mp;
}

void
AvoidCrossingPerimeters::init_external_mp(const ExPolygons &islands)
{
    if (this->_external_mp != NULL)
        delete this->_external_mp;
    
    this->_external_mp = new MotionPlanner(islands);
}

void
AvoidCrossingPerimeters::init_layer_mp(const ExPolygons &islands)
{
    if (this->_layer_mp != NULL)
        delete this->_layer_mp;
    
    this->_layer_mp = new MotionPlanner(islands);
}

Polyline
AvoidCrossingPerimeters::travel_to(Point point, const Pointf &gcodegen_origin,
    const Point &gcodegen_last_pos)
{
    if (this->use_external_mp || this->use_external_mp_once) {
        // get current origin set in gcodegen
        // (the one that will be used to translate the G-code coordinates by)
        Point scaled_origin = Point::new_scale(gcodegen_origin.x, gcodegen_origin.y);
        
        // represent last_pos in absolute G-code coordinates
        Point last_pos = gcodegen_last_pos;
        last_pos.translate(scaled_origin);
        
        // represent point in absolute G-code coordinates
        point.translate(scaled_origin);
        
        // calculate path
        Polyline travel = this->_external_mp->shortest_path(last_pos, point);
        
        // translate the path back into the shifted coordinate system that gcodegen
        // is currently using for writing coordinates
        travel.translate(scaled_origin.negative());
        return travel;
    } else {
        return this->_layer_mp->shortest_path(gcodegen_last_pos, point);
    }
}

#ifdef SLIC3RXS
REGISTER_CLASS(AvoidCrossingPerimeters, "GCode::AvoidCrossingPerimeters");
#endif

Wipe::Wipe()
    : enable(false)
{
}

bool
Wipe::has_path()
{
    return !this->path.points.empty();
}

void
Wipe::reset_path()
{
    this->path = Polyline();
}

#ifdef SLIC3RXS
REGISTER_CLASS(Wipe, "GCode::Wipe");
#endif

}
