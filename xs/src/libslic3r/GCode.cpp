#include "GCode.hpp"
#include "ExtrusionEntity.hpp"
#include "EdgeGrid.hpp"
#include <algorithm>
#include <cstdlib>
#include <math.h>

#include "SVG.hpp"

#if 0
// Enable debugging and asserts, even in the release build.
#define DEBUG
#define _DEBUG
#undef NDEBUG
#endif

#include <assert.h>

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
AvoidCrossingPerimeters::travel_to(GCode &gcodegen, Point point)
{
    if (this->use_external_mp || this->use_external_mp_once) {
        // get current origin set in gcodegen
        // (the one that will be used to translate the G-code coordinates by)
        Point scaled_origin = Point::new_scale(gcodegen.origin.x, gcodegen.origin.y);
        
        // represent last_pos in absolute G-code coordinates
        Point last_pos = gcodegen.last_pos();
        last_pos.translate(scaled_origin);
        
        // represent point in absolute G-code coordinates
        point.translate(scaled_origin);
        
        // calculate path
        Polyline travel = this->_external_mp->shortest_path(last_pos, point);
        //exit(0);
        // translate the path back into the shifted coordinate system that gcodegen
        // is currently using for writing coordinates
        travel.translate(scaled_origin.negative());
        return travel;
    } else {
        return this->_layer_mp->shortest_path(gcodegen.last_pos(), point);
    }
}

OozePrevention::OozePrevention()
    : enable(false)
{
}

std::string
OozePrevention::pre_toolchange(GCode &gcodegen)
{
    std::string gcode;
    
    // move to the nearest standby point
    if (!this->standby_points.empty()) {
        // get current position in print coordinates
        Pointf3 writer_pos = gcodegen.writer.get_position();
        Point pos = Point::new_scale(writer_pos.x, writer_pos.y);
        
        // find standby point
        Point standby_point;
        pos.nearest_point(this->standby_points, &standby_point);
        
        /*  We don't call gcodegen.travel_to() because we don't need retraction (it was already
            triggered by the caller) nor avoid_crossing_perimeters and also because the coordinates
            of the destination point must not be transformed by origin nor current extruder offset.  */
        gcode += gcodegen.writer.travel_to_xy(Pointf::new_unscale(standby_point), 
            "move to standby position");
    }
    
    if (gcodegen.config.standby_temperature_delta.value != 0) {
        // we assume that heating is always slower than cooling, so no need to block
        gcode += gcodegen.writer.set_temperature
            (this->_get_temp(gcodegen) + gcodegen.config.standby_temperature_delta.value, false);
    }
    
    return gcode;
}

std::string
OozePrevention::post_toolchange(GCode &gcodegen)
{
    std::string gcode;
    
    if (gcodegen.config.standby_temperature_delta.value != 0) {
        gcode += gcodegen.writer.set_temperature(this->_get_temp(gcodegen), true);
    }
    
    return gcode;
}

int
OozePrevention::_get_temp(GCode &gcodegen)
{
    return (gcodegen.layer != NULL && gcodegen.layer->id() == 0)
        ? gcodegen.config.first_layer_temperature.get_at(gcodegen.writer.extruder()->id)
        : gcodegen.config.temperature.get_at(gcodegen.writer.extruder()->id);
}

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

std::string
Wipe::wipe(GCode &gcodegen, bool toolchange)
{
    std::string gcode;
    
    /*  Reduce feedrate a bit; travel speed is often too high to move on existing material.
        Too fast = ripping of existing material; too slow = short wipe path, thus more blob.  */
    double wipe_speed = gcodegen.writer.config.travel_speed.value * 0.8;
    
    // get the retraction length
    double length = toolchange
        ? gcodegen.writer.extruder()->retract_length_toolchange()
        : gcodegen.writer.extruder()->retract_length();
    
    if (length > 0) {
        /*  Calculate how long we need to travel in order to consume the required
            amount of retraction. In other words, how far do we move in XY at wipe_speed
            for the time needed to consume retract_length at retract_speed?  */
        double wipe_dist = scale_(length / gcodegen.writer.extruder()->retract_speed() * wipe_speed);
    
        /*  Take the stored wipe path and replace first point with the current actual position
            (they might be different, for example, in case of loop clipping).  */
        Polyline wipe_path;
        wipe_path.append(gcodegen.last_pos());
        wipe_path.append(
            this->path.points.begin() + 1,
            this->path.points.end()
        );
        
        wipe_path.clip_end(wipe_path.length() - wipe_dist);
    
        // subdivide the retraction in segments
        double retracted = 0;
        Lines lines = wipe_path.lines();
        for (Lines::const_iterator line = lines.begin(); line != lines.end(); ++line) {
            double segment_length = line->length();
            /*  Reduce retraction length a bit to avoid effective retraction speed to be greater than the configured one
                due to rounding (TODO: test and/or better math for this)  */
            double dE = length * (segment_length / wipe_dist) * 0.95;
            gcode += gcodegen.writer.set_speed(wipe_speed*60, "", gcodegen.enable_cooling_markers ? ";_WIPE" : "");
            gcode += gcodegen.writer.extrude_to_xy(
                gcodegen.point_to_gcode(line->b),
                -dE,
                "wipe and retract"
            );
            retracted += dE;
        }
        gcodegen.writer.extruder()->retracted += retracted;
        
        // prevent wiping again on same path
        this->reset_path();
    }
    
    return gcode;
}

#define EXTRUDER_CONFIG(OPT) this->config.OPT.get_at(this->writer.extruder()->id)

GCode::GCode()
    : placeholder_parser(NULL), enable_loop_clipping(true), 
        enable_cooling_markers(false), enable_extrusion_role_markers(false), enable_analyzer_markers(false),
        layer_count(0),
        layer_index(-1), layer(NULL), first_layer(false), elapsed_time(0.0), volumetric_speed(0),
        _last_pos_defined(false),
        _lower_layer_edge_grid(NULL),
        _last_extrusion_role(erNone)
{
}

GCode::~GCode()
{
    delete _lower_layer_edge_grid;
    _lower_layer_edge_grid = NULL;
}

const Point&
GCode::last_pos() const
{
    return this->_last_pos;
}

void
GCode::set_last_pos(const Point &pos)
{
    this->_last_pos = pos;
    this->_last_pos_defined = true;
}

bool
GCode::last_pos_defined() const
{
    return this->_last_pos_defined;
}

void
GCode::apply_print_config(const PrintConfig &print_config)
{
    this->writer.apply_print_config(print_config);
    this->config.apply(print_config);
}

void
GCode::set_extruders(const std::vector<unsigned int> &extruder_ids)
{
    this->writer.set_extruders(extruder_ids);
    
    // enable wipe path generation if any extruder has wipe enabled
    this->wipe.enable = false;
    for (std::vector<unsigned int>::const_iterator it = extruder_ids.begin();
        it != extruder_ids.end(); ++it) {
        if (this->config.wipe.get_at(*it)) {
            this->wipe.enable = true;
            break;
        }
    }
}

void
GCode::set_origin(const Pointf &pointf)
{    
    // if origin increases (goes towards right), last_pos decreases because it goes towards left
    const Point translate(
        scale_(this->origin.x - pointf.x),
        scale_(this->origin.y - pointf.y)
    );
    this->_last_pos.translate(translate);
    this->wipe.path.translate(translate);
    this->origin = pointf;
}

std::string
GCode::preamble()
{
    std::string gcode = this->writer.preamble();
    
    /*  Perform a *silent* move to z_offset: we need this to initialize the Z
        position of our writer object so that any initial lift taking place
        before the first layer change will raise the extruder from the correct
        initial Z instead of 0.  */
    this->writer.travel_to_z(this->config.z_offset.value);
    
    return gcode;
}

std::string
GCode::change_layer(const Layer &layer)
{
    this->layer = &layer;
    this->layer_index++;
    this->first_layer = (layer.id() == 0);
    delete this->_lower_layer_edge_grid;
    this->_lower_layer_edge_grid = NULL;

    std::string gcode;

    if (enable_analyzer_markers) {
        // Store the binary pointer to the layer object directly into the G-code to be accessed by the GCodeAnalyzer.
        char buf[64];
        sprintf(buf, ";_LAYEROBJ:%p\n", this->layer);
        gcode += buf;
    }
    
    // avoid computing islands and overhangs if they're not needed
    if (this->config.avoid_crossing_perimeters) {
        ExPolygons islands = union_ex(layer.slices, true);
        this->avoid_crossing_perimeters.init_layer_mp(islands);
    }
    
    if (this->layer_count > 0) {
        gcode += this->writer.update_progress(this->layer_index, this->layer_count);
    }
    
    coordf_t z = layer.print_z + this->config.z_offset.value;  // in unscaled coordinates
    if (EXTRUDER_CONFIG(retract_layer_change) && this->writer.will_move_z(z)) {
        gcode += this->retract();
    }
    {
        std::ostringstream comment;
        comment << "move to next layer (" << this->layer_index << ")";
        gcode += this->writer.travel_to_z(z, comment.str());
    }
    
    // forget last wiping path as wiping after raising Z is pointless
    this->wipe.reset_path();
    
    return gcode;
}

static inline const char* ExtrusionRole2String(const ExtrusionRole role)
{
    switch (role) {
    case erNone:                        return "erNone";
    case erPerimeter:                   return "erPerimeter";
    case erExternalPerimeter:           return "erExternalPerimeter";
    case erOverhangPerimeter:           return "erOverhangPerimeter";
    case erInternalInfill:              return "erInternalInfill";
    case erSolidInfill:                 return "erSolidInfill";
    case erTopSolidInfill:              return "erTopSolidInfill";
    case erBridgeInfill:                return "erBridgeInfill";
    case erGapFill:                     return "erGapFill";
    case erSkirt:                       return "erSkirt";
    case erSupportMaterial:             return "erSupportMaterial";
    case erSupportMaterialInterface:    return "erSupportMaterialInterface";
    default:                            return "erInvalid";
    };
}

static inline const char* ExtrusionLoopRole2String(const ExtrusionLoopRole role)
{
    switch (role) {
    case elrDefault:                    return "elrDefault";
    case elrContourInternalPerimeter:   return "elrContourInternalPerimeter";
    case elrSkirt:                      return "elrSkirt";
    default:                            return "elrInvalid";
    }
};

// Return a value in <0, 1> of a cubic B-spline kernel centered around zero.
// The B-spline is re-scaled so it has value 1 at zero.
static inline float bspline_kernel(float x)
{
    x = std::abs(x);
	if (x < 1.f) {
		return 1.f - (3. / 2.) * x * x + (3.f / 4.f) * x * x * x;
	}
	else if (x < 2.f) {
		x -= 1.f;
		float x2 = x * x;
		float x3 = x2 * x;
		return (1.f / 4.f) - (3.f / 4.f) * x + (3.f / 4.f) * x2 - (1.f / 4.f) * x3;
	}
	else
        return 0;
}

static float extrudate_overlap_penalty(float nozzle_r, float weight_zero, float overlap_distance)
{
    // The extrudate is not fully supported by the lower layer. Fit a polynomial penalty curve.
    // Solved by sympy package:
/*
from sympy import *
(x,a,b,c,d,r,z)=symbols('x a b c d r z')
p = a + b*x + c*x*x + d*x*x*x
p2 = p.subs(solve([p.subs(x, -r), p.diff(x).subs(x, -r), p.diff(x,x).subs(x, -r), p.subs(x, 0)-z], [a, b, c, d]))
from sympy.plotting import plot
plot(p2.subs(r,0.2).subs(z,1.), (x, -1, 3), adaptive=False, nb_of_points=400)
*/
    if (overlap_distance < - nozzle_r) {
        // The extrudate is fully supported by the lower layer. This is the ideal case, therefore zero penalty.
        return 0.f;
    } else {
        float x  = overlap_distance / nozzle_r;
        float x2 = x * x;
        float x3 = x2 * x;
        return weight_zero * (1.f + 3.f * x + 3.f * x2 + x3);
    }
}

static Points::iterator project_point_to_polygon_and_insert(Polygon &polygon, const Point &pt, double eps)
{
    assert(polygon.points.size() >= 2);
    if (polygon.points.size() <= 1)
    if (polygon.points.size() == 1)
        return polygon.points.begin();

    Point  pt_min;
    double d_min = std::numeric_limits<double>::max();
    size_t i_min = size_t(-1);

    for (size_t i = 0; i < polygon.points.size(); ++ i) {
        size_t j = i + 1;
        if (j == polygon.points.size())
            j = 0;
        const Point &p1 = polygon.points[i];
        const Point &p2 = polygon.points[j];
        const Slic3r::Point v_seg = p1.vector_to(p2);
        const Slic3r::Point v_pt  = p1.vector_to(pt);
        const int64_t l2_seg = int64_t(v_seg.x) * int64_t(v_seg.x) + int64_t(v_seg.y) * int64_t(v_seg.y);
        int64_t t_pt = int64_t(v_seg.x) * int64_t(v_pt.x) + int64_t(v_seg.y) * int64_t(v_pt.y);
        if (t_pt < 0) {
            // Closest to p1.
            double dabs = sqrt(int64_t(v_pt.x) * int64_t(v_pt.x) + int64_t(v_pt.y) * int64_t(v_pt.y));
            if (dabs < d_min) {
                d_min  = dabs;
                i_min  = i;
                pt_min = p1;
            }
        }
        else if (t_pt > l2_seg) {
            // Closest to p2. Then p2 is the starting point of another segment, which shall be discovered in the next step.
            continue;
        } else {
            // Closest to the segment.
            assert(t_pt >= 0 && t_pt <= l2_seg);
            int64_t d_seg = int64_t(v_seg.y) * int64_t(v_pt.x) - int64_t(v_seg.x) * int64_t(v_pt.y);
            double d = double(d_seg) / sqrt(double(l2_seg));
            double dabs = std::abs(d);
            if (dabs < d_min) {
                d_min  = dabs;
                i_min  = i;
                // Evaluate the foot point.
                pt_min = p1;
                double linv = double(d_seg) / double(l2_seg);
                pt_min.x = pt.x - coord_t(floor(double(v_seg.y) * linv + 0.5));
				pt_min.y = pt.y + coord_t(floor(double(v_seg.x) * linv + 0.5));
				assert(Line(p1, p2).distance_to(pt_min) < scale_(1e-5));
            }
        }
    }

	assert(i_min != size_t(-1));
    if (pt_min.distance_to(polygon.points[i_min]) > eps) {
        // Insert a new point on the segment i_min, i_min+1.
        return polygon.points.insert(polygon.points.begin() + (i_min + 1), pt_min);
    }
    return polygon.points.begin() + i_min;
}

std::vector<float> polygon_parameter_by_length(const Polygon &polygon)
{
    // Parametrize the polygon by its length.
    std::vector<float> lengths(polygon.points.size()+1, 0.);
    for (size_t i = 1; i < polygon.points.size(); ++ i)
        lengths[i] = lengths[i-1] + polygon.points[i].distance_to(polygon.points[i-1]);
    lengths.back() = lengths[lengths.size()-2] + polygon.points.front().distance_to(polygon.points.back());
    return lengths;
}

std::vector<float> polygon_angles_at_vertices(const Polygon &polygon, const std::vector<float> &lengths, float min_arm_length)
{
    assert(polygon.points.size() + 1 == lengths.size());
    if (min_arm_length > 0.25f * lengths.back())
        min_arm_length = 0.25f * lengths.back();

    // Find the initial prev / next point span.
    size_t idx_prev = polygon.points.size();
    size_t idx_curr = 0;
    size_t idx_next = 1;
    while (idx_prev > idx_curr && lengths.back() - lengths[idx_prev] < min_arm_length)
        -- idx_prev;
    while (idx_next < idx_prev && lengths[idx_next] < min_arm_length)
        ++ idx_next;

    std::vector<float> angles(polygon.points.size(), 0.f);
    for (; idx_curr < polygon.points.size(); ++ idx_curr) {
        // Move idx_prev up until the distance between idx_prev and idx_curr is lower than min_arm_length.
        if (idx_prev >= idx_curr) {
            while (idx_prev < polygon.points.size() && lengths.back() - lengths[idx_prev] + lengths[idx_curr] > min_arm_length)
                ++ idx_prev;
            if (idx_prev == polygon.points.size())
                idx_prev = 0;
        }
        while (idx_prev < idx_curr && lengths[idx_curr] - lengths[idx_prev] > min_arm_length)
            ++ idx_prev;
        // Move idx_prev one step back.
        if (idx_prev == 0)
            idx_prev = polygon.points.size() - 1;
        else
            -- idx_prev;
        // Move idx_next up until the distance between idx_curr and idx_next is greater than min_arm_length.
        if (idx_curr <= idx_next) {
            while (idx_next < polygon.points.size() && lengths[idx_next] - lengths[idx_curr] < min_arm_length)
                ++ idx_next;
            if (idx_next == polygon.points.size())
                idx_next = 0;
        }
        while (idx_next < idx_curr && lengths.back() - lengths[idx_curr] + lengths[idx_next] < min_arm_length)
            ++ idx_next;
        // Calculate angle between idx_prev, idx_curr, idx_next.
        const Point &p0 = polygon.points[idx_prev];
        const Point &p1 = polygon.points[idx_curr];
        const Point &p2 = polygon.points[idx_next];
        const Point  v1 = p0.vector_to(p1);
        const Point  v2 = p1.vector_to(p2);
		int64_t dot   = int64_t(v1.x)*int64_t(v2.x) + int64_t(v1.y)*int64_t(v2.y);
		int64_t cross = int64_t(v1.x)*int64_t(v2.y) - int64_t(v1.y)*int64_t(v2.x);
		float angle = float(atan2(double(cross), double(dot)));
        angles[idx_curr] = angle;
    }

    return angles;
}

std::string
GCode::extrude(ExtrusionLoop loop, std::string description, double speed)
{
    // get a copy; don't modify the orientation of the original loop object otherwise
    // next copies (if any) would not detect the correct orientation

    if (this->layer->lower_layer != NULL) {
        if (this->_lower_layer_edge_grid == NULL) {
            // Create the distance field for a layer below.
            const coord_t distance_field_resolution = scale_(1.f);
            this->_lower_layer_edge_grid = new EdgeGrid::Grid();
            this->_lower_layer_edge_grid->create(this->layer->lower_layer->slices, distance_field_resolution);
            this->_lower_layer_edge_grid->calculate_sdf();
            #if 0
            {
                static int iRun = 0;
                BoundingBox bbox = this->_lower_layer_edge_grid->bbox();
                bbox.min.x -= scale_(5.f);
                bbox.min.y -= scale_(5.f);
                bbox.max.x += scale_(5.f);
                bbox.max.y += scale_(5.f);
                EdgeGrid::save_png(*this->_lower_layer_edge_grid, bbox, scale_(0.1f), debug_out_path("GCode_extrude_loop_edge_grid-%d.png", iRun++));
            }
            #endif
        }
    }
  
    // extrude all loops ccw
    bool was_clockwise = loop.make_counter_clockwise();
    
    SeamPosition seam_position = this->config.seam_position;
    if (loop.role == elrSkirt) seam_position = spNearest;
    
    // find the point of the loop that is closest to the current extruder position
    // or randomize if requested
    Point last_pos = this->last_pos();
    if (this->config.spiral_vase) {
        loop.split_at(last_pos, false);
    } else if (seam_position == spNearest || seam_position == spAligned) {
        Polygon        polygon    = loop.polygon();
        const coordf_t nozzle_dmr = EXTRUDER_CONFIG(nozzle_diameter);
        const coord_t  nozzle_r   = scale_(0.5*nozzle_dmr);

        // Retrieve the last start position for this object.
        float last_pos_weight = 1.f;
        if (seam_position == spAligned && this->layer != NULL && this->_seam_position.count(this->layer->object()) > 0) {
            last_pos = this->_seam_position[this->layer->object()];
            last_pos_weight = 5.f;
        }

        // Insert a projection of last_pos into the polygon.
		size_t last_pos_proj_idx;
		{
			Points::iterator it = project_point_to_polygon_and_insert(polygon, last_pos, 0.1 * nozzle_r);
			last_pos_proj_idx = it - polygon.points.begin();
		}
        Point last_pos_proj = polygon.points[last_pos_proj_idx];
        // Parametrize the polygon by its length.
        std::vector<float> lengths = polygon_parameter_by_length(polygon);

        // For each polygon point, store a penalty.
        // First calculate the angles, store them as penalties. The angles are caluculated over a minimum arm length of nozzle_r.
        std::vector<float> penalties = polygon_angles_at_vertices(polygon, lengths, nozzle_r);
        // No penalty for reflex points, slight penalty for convex points, high penalty for flat surfaces.
        const float penaltyConvexVertex = 1.f;
        const float penaltyFlatSurface  = 5.f;
        const float penaltySeam         = 1.3f;
        const float penaltyOverhangHalf = 10.f;
        // Penalty for visible seams.
        for (size_t i = 0; i < polygon.points.size(); ++ i) {
            float ccwAngle = penalties[i];
            if (was_clockwise)
                ccwAngle = - ccwAngle;
            float penalty = 0;
//            if (ccwAngle <- float(PI/3.))
            if (ccwAngle <- float(0.6 * PI))
                // Sharp reflex vertex. We love that, it hides the seam perfectly.
                penalty = 0.f;
//            else if (ccwAngle > float(PI/3.))
            else if (ccwAngle > float(0.6 * PI))
                // Seams on sharp convex vertices are more visible than on reflex vertices.
                penalty = penaltyConvexVertex;
            else if (ccwAngle < 0.f) {
                // Interpolate penalty between maximum and zero.
                penalty = penaltyFlatSurface * bspline_kernel(ccwAngle * (PI * 2. / 3.));
            } else {
                assert(ccwAngle >= 0.f);
                // Interpolate penalty between maximum and the penalty for a convex vertex.
                penalty = penaltyConvexVertex + (penaltyFlatSurface - penaltyConvexVertex) * bspline_kernel(ccwAngle * (PI * 2. / 3.));
            }
            // Give a negative penalty for points close to the last point or the prefered seam location.
            //float dist_to_last_pos_proj = last_pos_proj.distance_to(polygon.points[i]);
            float dist_to_last_pos_proj = (i < last_pos_proj_idx) ? 
                std::min(lengths[last_pos_proj_idx] - lengths[i], lengths.back() - lengths[last_pos_proj_idx] + lengths[i]) : 
                std::min(lengths[i] - lengths[last_pos_proj_idx], lengths.back() - lengths[i] + lengths[last_pos_proj_idx]);
            float dist_max = 0.1f * lengths.back(); // 5.f * nozzle_dmr
            penalty -= last_pos_weight * bspline_kernel(dist_to_last_pos_proj / dist_max);
            penalties[i] = std::max(0.f, penalty);
        }

        // Penalty for overhangs.
        if (this->_lower_layer_edge_grid) {
            // Use the edge grid distance field structure over the lower layer to calculate overhangs.
            coord_t nozzle_r = scale_(0.5*nozzle_dmr);
            coord_t search_r = scale_(0.8*nozzle_dmr);
            for (size_t i = 0; i < polygon.points.size(); ++ i) {
                const Point &p = polygon.points[i];
                coordf_t dist;
                // Signed distance is positive outside the object, negative inside the object.
                // The point is considered at an overhang, if it is more than nozzle radius
                // outside of the lower layer contour.
                bool found = this->_lower_layer_edge_grid->signed_distance(p, search_r, dist);
                // If the approximate Signed Distance Field was initialized over this->_lower_layer_edge_grid,
                // then the signed distnace shall always be known.
                assert(found);
                penalties[i] += extrudate_overlap_penalty(nozzle_r, penaltyOverhangHalf, dist);
            }
        }

        // Find a point with a minimum penalty.
        size_t idx_min = std::min_element(penalties.begin(), penalties.end()) - penalties.begin();

        // Export the contour into a SVG file.
        #if 0
        {
            static int iRun = 0;
            SVG svg(debug_out_path("GCode_extrude_loop-%d.svg", iRun ++));
			if (this->layer->lower_layer != NULL)
				svg.draw(this->layer->lower_layer->slices.expolygons);
            for (size_t i = 0; i < loop.paths.size(); ++ i)
                svg.draw(loop.paths[i].as_polyline(), "red");
            Polylines polylines;
            for (size_t i = 0; i < loop.paths.size(); ++ i)
                polylines.push_back(loop.paths[i].as_polyline());
            Slic3r::Polygons polygons;
            coordf_t nozzle_dmr = EXTRUDER_CONFIG(nozzle_diameter);
            coord_t delta = scale_(0.5*nozzle_dmr);
            Slic3r::offset(polylines, &polygons, delta);
//            for (size_t i = 0; i < polygons.size(); ++ i) svg.draw((Polyline)polygons[i], "blue");
			svg.draw(last_pos, "green", 3);
			svg.draw(polygon.points[idx_min], "yellow", 3);
			svg.Close();
        }
        #endif

        // Split the loop at the point with a minium penalty.
        if (!loop.split_at_vertex(polygon.points[idx_min]))
            // The point is not in the original loop. Insert it.
            loop.split_at(polygon.points[idx_min], true);

    } else if (seam_position == spRandom) {
        if (loop.role == elrContourInternalPerimeter) {
            // This loop does not contain any other loop. Set a random position.
            // The other loops will get a seam close to the random point chosen
            // on the inner most contour.
            //FIXME This works correctly for inner contours first only.
            //FIXME Better parametrize the loop by its length.
            Polygon polygon = loop.polygon();
            Point centroid = polygon.centroid();
            last_pos = Point(polygon.bounding_box().max.x, centroid.y);
            last_pos.rotate(fmod((float)rand()/16.0, 2.0*PI), centroid);
        }
        loop.split_at(last_pos, true);
    }
    
    // clip the path to avoid the extruder to get exactly on the first point of the loop;
    // if polyline was shorter than the clipping distance we'd get a null polyline, so
    // we discard it in that case
    double clip_length = this->enable_loop_clipping
        ? scale_(EXTRUDER_CONFIG(nozzle_diameter)) * LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER
        : 0;
    
    // get paths
    ExtrusionPaths paths;
    loop.clip_end(clip_length, &paths);
    if (paths.empty()) return "";
    
    // apply the small perimeter speed
    if (paths.front().is_perimeter() && loop.length() <= SMALL_PERIMETER_LENGTH) {
        if (speed == -1) speed = this->config.get_abs_value("small_perimeter_speed");
    }
    
    // extrude along the path
    std::string gcode;
    for (ExtrusionPaths::const_iterator path = paths.begin(); path != paths.end(); ++path)
//    description += ExtrusionLoopRole2String(loop.role);
//    description += ExtrusionRole2String(path->role);
        gcode += this->_extrude(*path, description, speed);
    
    // reset acceleration
    gcode += this->writer.set_acceleration(this->config.default_acceleration.value);
    
    if (this->wipe.enable)
        this->wipe.path = paths.front().polyline;  // TODO: don't limit wipe to last path
    
    // make a little move inwards before leaving loop
    if (paths.back().role == erExternalPerimeter && this->layer != NULL && this->config.perimeters > 1) {
        // detect angle between last and first segment
        // the side depends on the original winding order of the polygon (left for contours, right for holes)
        Point a = paths.front().polyline.points[1];  // second point
        Point b = *(paths.back().polyline.points.end()-3);       // second to last point
        if (was_clockwise) {
            // swap points
            Point c = a; a = b; b = c;
        }
        
        double angle = paths.front().first_point().ccw_angle(a, b) / 3;
        
        // turn left if contour, turn right if hole
        if (was_clockwise) angle *= -1;
        
        // create the destination point along the first segment and rotate it
        // we make sure we don't exceed the segment length because we don't know
        // the rotation of the second segment so we might cross the object boundary
        Line first_segment(
            paths.front().polyline.points[0],
            paths.front().polyline.points[1]
        );
        double distance = std::min(
            scale_(EXTRUDER_CONFIG(nozzle_diameter)),
            first_segment.length()
        );
        Point point = first_segment.point_at(distance);
        point.rotate(angle, first_segment.a);
        
        // generate the travel move
        gcode += this->writer.travel_to_xy(this->point_to_gcode(point), "move inwards before travel");
    }
    
    return gcode;
}

std::string
GCode::extrude(ExtrusionMultiPath multipath, std::string description, double speed)
{
    // extrude along the path
    std::string gcode;
    for (ExtrusionPaths::const_iterator path = multipath.paths.begin(); path != multipath.paths.end(); ++path)
//    description += ExtrusionLoopRole2String(loop.role);
//    description += ExtrusionRole2String(path->role);
        gcode += this->_extrude(*path, description, speed);
    
    // reset acceleration
    gcode += this->writer.set_acceleration(this->config.default_acceleration.value);
  
//FIXME perform wipe on multi paths?  
//    if (this->wipe.enable)
//        this->wipe.path = paths.front().polyline;  // TODO: don't limit wipe to last path
    
    return gcode;
}

std::string
GCode::extrude(const ExtrusionEntity &entity, std::string description, double speed)
{
    if (const ExtrusionPath* path = dynamic_cast<const ExtrusionPath*>(&entity)) {
        return this->extrude(*path, description, speed);
    } else if (const ExtrusionMultiPath* multipath = dynamic_cast<const ExtrusionMultiPath*>(&entity)) {
        return this->extrude(*multipath, description, speed);
    } else if (const ExtrusionLoop* loop = dynamic_cast<const ExtrusionLoop*>(&entity)) {
        return this->extrude(*loop, description, speed);
    } else {
        CONFESS("Invalid argument supplied to extrude()");
        return "";
    }
}

std::string
GCode::extrude(const ExtrusionPath &path, std::string description, double speed)
{
//    description += ExtrusionRole2String(path.role);
    std::string gcode = this->_extrude(path, description, speed);
    
    // reset acceleration
    gcode += this->writer.set_acceleration(this->config.default_acceleration.value);
    
    return gcode;
}

std::string
GCode::_extrude(ExtrusionPath path, std::string description, double speed)
{
    path.simplify(SCALED_RESOLUTION);
    
    std::string gcode;
    
    // go to first point of extrusion path
    if (!this->_last_pos_defined || !this->_last_pos.coincides_with(path.first_point())) {
        gcode += this->travel_to(
            path.first_point(),
            path.role,
            "move to first " + description + " point"
        );
    }
    
    // compensate retraction
    gcode += this->unretract();
    
    // adjust acceleration
    {
        double acceleration;
        if (this->config.first_layer_acceleration.value > 0 && this->first_layer) {
            acceleration = this->config.first_layer_acceleration.value;
        } else if (this->config.perimeter_acceleration.value > 0 && path.is_perimeter()) {
            acceleration = this->config.perimeter_acceleration.value;
        } else if (this->config.bridge_acceleration.value > 0 && path.is_bridge()) {
            acceleration = this->config.bridge_acceleration.value;
        } else if (this->config.infill_acceleration.value > 0 && path.is_infill()) {
            acceleration = this->config.infill_acceleration.value;
        } else {
            acceleration = this->config.default_acceleration.value;
        }
        gcode += this->writer.set_acceleration(acceleration);
    }
    
    // calculate extrusion length per distance unit
    double e_per_mm = this->writer.extruder()->e_per_mm3 * path.mm3_per_mm;
    if (this->writer.extrusion_axis().empty()) e_per_mm = 0;
    
    // set speed
    if (speed == -1) {
        if (path.role == erPerimeter) {
            speed = this->config.get_abs_value("perimeter_speed");
        } else if (path.role == erExternalPerimeter) {
            speed = this->config.get_abs_value("external_perimeter_speed");
        } else if (path.role == erOverhangPerimeter || path.role == erBridgeInfill) {
            speed = this->config.get_abs_value("bridge_speed");
        } else if (path.role == erInternalInfill) {
            speed = this->config.get_abs_value("infill_speed");
        } else if (path.role == erSolidInfill) {
            speed = this->config.get_abs_value("solid_infill_speed");
        } else if (path.role == erTopSolidInfill) {
            speed = this->config.get_abs_value("top_solid_infill_speed");
        } else if (path.role == erGapFill) {
            speed = this->config.get_abs_value("gap_fill_speed");
        } else {
            CONFESS("Invalid speed");
        }
    }
    if (this->first_layer) {
        speed = this->config.get_abs_value("first_layer_speed", speed);
    }
    if (this->volumetric_speed != 0 && speed == 0) {
        speed = this->volumetric_speed / path.mm3_per_mm;
    }
    if (this->config.max_volumetric_speed.value > 0) {
        // cap speed with max_volumetric_speed anyway (even if user is not using autospeed)
        speed = std::min(
            speed,
            this->config.max_volumetric_speed.value / path.mm3_per_mm
        );
    }
    if (EXTRUDER_CONFIG(filament_max_volumetric_speed) > 0) {
        // cap speed with max_volumetric_speed anyway (even if user is not using autospeed)
        speed = std::min(
            speed,
            EXTRUDER_CONFIG(filament_max_volumetric_speed) / path.mm3_per_mm
        );
    }
    double F = speed * 60;  // convert mm/sec to mm/min
    
    // extrude arc or line
    if (this->enable_extrusion_role_markers || this->enable_analyzer_markers) {
        if (path.role != this->_last_extrusion_role) {
            this->_last_extrusion_role = path.role;
            char buf[32];
            sprintf(buf, ";_EXTRUSION_ROLE:%d\n", int(path.role));
            gcode += buf;
        }
    }
    if (path.is_bridge() && this->enable_cooling_markers)
        gcode += ";_BRIDGE_FAN_START\n";
    gcode += this->writer.set_speed(F, "", this->enable_cooling_markers ? ";_EXTRUDE_SET_SPEED" : "");
    double path_length = 0;
    {
        std::string comment = this->config.gcode_comments ? description : "";
        Lines lines = path.polyline.lines();
        for (Lines::const_iterator line = lines.begin(); line != lines.end(); ++line) {
            const double line_length = line->length() * SCALING_FACTOR;
            path_length += line_length;
            
            gcode += this->writer.extrude_to_xy(
                this->point_to_gcode(line->b),
                e_per_mm * line_length,
                comment
            );
        }
    }
    if (this->wipe.enable) {
        this->wipe.path = path.polyline;
        this->wipe.path.reverse();
    }
    if (path.is_bridge() && this->enable_cooling_markers)
        gcode += ";_BRIDGE_FAN_END\n";
    
    this->set_last_pos(path.last_point());
    
    if (this->config.cooling)
        this->elapsed_time += path_length / F * 60;
    
    return gcode;
}

// This method accepts &point in print coordinates.
std::string
GCode::travel_to(const Point &point, ExtrusionRole role, std::string comment)
{    
    /*  Define the travel move as a line between current position and the taget point.
        This is expressed in print coordinates, so it will need to be translated by
        this->origin in order to get G-code coordinates.  */
    Polyline travel;
    travel.append(this->last_pos());
    travel.append(point);
    
    // check whether a straight travel move would need retraction
    bool needs_retraction = this->needs_retraction(travel, role);
    
    // if a retraction would be needed, try to use avoid_crossing_perimeters to plan a
    // multi-hop travel path inside the configuration space
    if (needs_retraction
        && this->config.avoid_crossing_perimeters
        && !this->avoid_crossing_perimeters.disable_once) {
        travel = this->avoid_crossing_perimeters.travel_to(*this, point);
        
        // check again whether the new travel path still needs a retraction
        needs_retraction = this->needs_retraction(travel, role);
        //if (needs_retraction && this->layer_index > 1) exit(0);
    }
    
    // Re-allow avoid_crossing_perimeters for the next travel moves
    this->avoid_crossing_perimeters.disable_once = false;
    this->avoid_crossing_perimeters.use_external_mp_once = false;
    
    // generate G-code for the travel move
    std::string gcode;
    if (needs_retraction) gcode += this->retract();
    
    // use G1 because we rely on paths being straight (G0 may make round paths)
    Lines lines = travel.lines();
    double path_length = 0;
    for (Lines::const_iterator line = lines.begin(); line != lines.end(); ++line) {
	    const double line_length = line->length() * SCALING_FACTOR;
	    path_length += line_length;

	    gcode += this->writer.travel_to_xy(this->point_to_gcode(line->b), comment);
    }

    if (this->config.cooling)
        this->elapsed_time += path_length / this->config.get_abs_value("travel_speed");

    return gcode;
}

bool
GCode::needs_retraction(const Polyline &travel, ExtrusionRole role)
{
    if (travel.length() < scale_(EXTRUDER_CONFIG(retract_before_travel))) {
        // skip retraction if the move is shorter than the configured threshold
        return false;
    }
    
    if (role == erSupportMaterial) {
        const SupportLayer* support_layer = dynamic_cast<const SupportLayer*>(this->layer);
        if (support_layer != NULL && support_layer->support_islands.contains(travel)) {
            // skip retraction if this is a travel move inside a support material island
            return false;
        }
    }
    
    if (this->config.only_retract_when_crossing_perimeters && this->layer != NULL) {
        if (this->config.fill_density.value > 0
            && this->layer->any_internal_region_slice_contains(travel)) {
            /*  skip retraction if travel is contained in an internal slice *and*
                internal infill is enabled (so that stringing is entirely not visible)  */
            return false;
        } else if (this->layer->any_bottom_region_slice_contains(travel)
            && this->layer->upper_layer != NULL
            && this->layer->upper_layer->slices.contains(travel)
            && (this->config.bottom_solid_layers.value >= 2 || this->config.fill_density.value > 0)) {
            /*  skip retraction if travel is contained in an *infilled* bottom slice
                but only if it's also covered by an *infilled* upper layer's slice
                so that it's not visible from above (a bottom surface might not have an
                upper slice in case of a thin membrane)  */
            return false;
        }
    }
    
    // retract if only_retract_when_crossing_perimeters is disabled or doesn't apply
    return true;
}

std::string
GCode::retract(bool toolchange)
{
    std::string gcode;
    
    if (this->writer.extruder() == NULL)
        return gcode;
    
    // wipe (if it's enabled for this extruder and we have a stored wipe path)
    if (EXTRUDER_CONFIG(wipe) && this->wipe.has_path()) {
        gcode += this->wipe.wipe(*this, toolchange);
    }
    
    /*  The parent class will decide whether we need to perform an actual retraction
        (the extruder might be already retracted fully or partially). We call these 
        methods even if we performed wipe, since this will ensure the entire retraction
        length is honored in case wipe path was too short.  */
    gcode += toolchange ? this->writer.retract_for_toolchange() : this->writer.retract();
    
    gcode += this->writer.reset_e();
    if (this->writer.extruder()->retract_length() > 0 || this->config.use_firmware_retraction)
        gcode += this->writer.lift();
    
    return gcode;
}

std::string
GCode::unretract()
{
    std::string gcode;
    gcode += this->writer.unlift();
    gcode += this->writer.unretract();
    return gcode;
}

std::string
GCode::set_extruder(unsigned int extruder_id)
{
    this->placeholder_parser->set("current_extruder", extruder_id);
    if (!this->writer.need_toolchange(extruder_id))
        return "";
    
    // if we are running a single-extruder setup, just set the extruder and return nothing
    if (!this->writer.multiple_extruders) {
        return this->writer.toolchange(extruder_id);
    }
    
    // prepend retraction on the current extruder
    std::string gcode = this->retract(true);
    
    // append custom toolchange G-code
    if (this->writer.extruder() != NULL && !this->config.toolchange_gcode.value.empty()) {
        PlaceholderParser pp = *this->placeholder_parser;
        pp.set("previous_extruder", this->writer.extruder()->id);
        pp.set("next_extruder",     extruder_id);
        gcode += pp.process(this->config.toolchange_gcode.value) + '\n';
    }
    
    // if ooze prevention is enabled, park current extruder in the nearest
    // standby point and set it to the standby temperature
    if (this->ooze_prevention.enable && this->writer.extruder() != NULL)
        gcode += this->ooze_prevention.pre_toolchange(*this);
    
    // append the toolchange command
    gcode += this->writer.toolchange(extruder_id);
    
    // set the new extruder to the operating temperature
    if (this->ooze_prevention.enable)
        gcode += this->ooze_prevention.post_toolchange(*this);
    
    return gcode;
}

// convert a model-space scaled point into G-code coordinates
Pointf
GCode::point_to_gcode(const Point &point)
{
    Pointf extruder_offset = EXTRUDER_CONFIG(extruder_offset);
    return Pointf(
        unscale(point.x) + this->origin.x - extruder_offset.x,
        unscale(point.y) + this->origin.y - extruder_offset.y
    );
}

}
