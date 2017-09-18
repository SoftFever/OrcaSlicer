#include "PerimeterGenerator.hpp"
#include "ClipperUtils.hpp"
#include "ExtrusionEntityCollection.hpp"
#include <cmath>
#include <cassert>

namespace Slic3r {

void
PerimeterGenerator::process()
{
    // other perimeters
    this->_mm3_per_mm               = this->perimeter_flow.mm3_per_mm();
    coord_t perimeter_width         = this->perimeter_flow.scaled_width();
    coord_t perimeter_spacing       = this->perimeter_flow.scaled_spacing();
    
    // external perimeters
    this->_ext_mm3_per_mm           = this->ext_perimeter_flow.mm3_per_mm();
    coord_t ext_perimeter_width     = this->ext_perimeter_flow.scaled_width();
    coord_t ext_perimeter_spacing   = this->ext_perimeter_flow.scaled_spacing();
    coord_t ext_perimeter_spacing2  = this->ext_perimeter_flow.scaled_spacing(this->perimeter_flow);
    
    // overhang perimeters
    this->_mm3_per_mm_overhang      = this->overhang_flow.mm3_per_mm();
    
    // solid infill
    coord_t solid_infill_spacing    = this->solid_infill_flow.scaled_spacing();
    
    // Calculate the minimum required spacing between two adjacent traces.
    // This should be equal to the nominal flow spacing but we experiment
    // with some tolerance in order to avoid triggering medial axis when
    // some squishing might work. Loops are still spaced by the entire
    // flow spacing; this only applies to collapsing parts.
    // For ext_min_spacing we use the ext_perimeter_spacing calculated for two adjacent
    // external loops (which is the correct way) instead of using ext_perimeter_spacing2
    // which is the spacing between external and internal, which is not correct
    // and would make the collapsing (thus the details resolution) dependent on 
    // internal flow which is unrelated.
    coord_t min_spacing         = perimeter_spacing      * (1 - INSET_OVERLAP_TOLERANCE);
    coord_t ext_min_spacing     = ext_perimeter_spacing  * (1 - INSET_OVERLAP_TOLERANCE);
    
    // prepare grown lower layer slices for overhang detection
    if (this->lower_slices != NULL && this->config->overhangs) {
        // We consider overhang any part where the entire nozzle diameter is not supported by the
        // lower layer, so we take lower slices and offset them by half the nozzle diameter used 
        // in the current layer
        double nozzle_diameter = this->print_config->nozzle_diameter.get_at(this->config->perimeter_extruder-1);
        
        this->_lower_slices_p = offset(*this->lower_slices, float(scale_(+nozzle_diameter/2)));
    }
    
    // we need to process each island separately because we might have different
    // extra perimeters for each one
    for (const Surface &surface : this->slices->surfaces) {
        // detect how many perimeters must be generated for this island
        const int loop_number = this->config->perimeters + surface.extra_perimeters -1;  // 0-indexed loops
        
        Polygons gaps;
        
        Polygons last = surface.expolygon.simplify_p(SCALED_RESOLUTION);
        if (loop_number >= 0) {  // no loops = -1
            
            std::vector<PerimeterGeneratorLoops> contours(loop_number+1);    // depth => loops
            std::vector<PerimeterGeneratorLoops> holes(loop_number+1);       // depth => loops
            ThickPolylines thin_walls;
            
            // we loop one time more than needed in order to find gaps after the last perimeter was applied
            for (int i = 0; i <= loop_number+1; ++i) {  // outer loop is 0
                Polygons offsets;
                if (i == 0) {
                    // the minimum thickness of a single loop is:
                    // ext_width/2 + ext_spacing/2 + spacing/2 + width/2
                    if (this->config->thin_walls) {
                        offsets = offset2(
                            last,
                            -(ext_perimeter_width / 2 + ext_min_spacing / 2 - 1),
                            +(ext_min_spacing/2 - 1)
                        );
                    } else {
                        offsets = offset(last, - ext_perimeter_width / 2);
                    }
                    
                    // look for thin walls
                    if (this->config->thin_walls) {
                        Polygons diffpp = diff(
                            last,
                            offset(offsets, ext_perimeter_width / 2),
                            true  // medial axis requires non-overlapping geometry
                        );
                        
                        // the following offset2 ensures almost nothing in @thin_walls is narrower than $min_width
                        // (actually, something larger than that still may exist due to mitering or other causes)
                        coord_t min_width = scale_(this->ext_perimeter_flow.nozzle_diameter / 3);
                        ExPolygons expp = offset2_ex(diffpp, -min_width/2, +min_width/2);
                        
                        // the maximum thickness of our thin wall area is equal to the minimum thickness of a single loop
                        for (ExPolygons::const_iterator ex = expp.begin(); ex != expp.end(); ++ex)
                            ex->medial_axis(ext_perimeter_width + ext_perimeter_spacing2, min_width, &thin_walls);
                        
                        #ifdef DEBUG
                        printf("  " PRINTF_ZU " thin walls detected\n", thin_walls.size());
                        #endif
                        
                        /*
                        if (false) {
                            require "Slic3r/SVG.pm";
                            Slic3r::SVG::output(
                                "medial_axis.svg",
                                no_arrows       => 1,
                                #expolygons      => \@expp,
                                polylines       => \@thin_walls,
                            );
                        }
                        */
                    }
                } else {
                    //FIXME Is this offset correct if the line width of the inner perimeters differs
                    // from the line width of the infill?
                    coord_t distance = (i == 1) ? ext_perimeter_spacing2 : perimeter_spacing;
                    
                    if (this->config->thin_walls) {
                        // This path will ensure, that the perimeters do not overfill, as in 
                        // prusa3d/Slic3r GH #32, but with the cost of rounding the perimeters
                        // excessively, creating gaps, which then need to be filled in by the not very 
                        // reliable gap fill algorithm.
                        // Also the offset2(perimeter, -x, x) may sometimes lead to a perimeter, which is larger than
                        // the original.
                        offsets = offset2(
                            last,
                            -(distance + min_spacing/2 - 1),
                            +(min_spacing/2 - 1)
                        );
                    } else {
                        // If "detect thin walls" is not enabled, this paths will be entered, which 
                        // leads to overflows, as in prusa3d/Slic3r GH #32
                        offsets = offset(
                            last,
                            -distance
                        );
                    }
                    
                    // look for gaps
                    if (this->config->gap_fill_speed.value > 0 && this->config->fill_density.value > 0) {
                        // not using safety offset here would "detect" very narrow gaps
                        // (but still long enough to escape the area threshold) that gap fill
                        // won't be able to fill but we'd still remove from infill area
                        Polygons diff_pp = diff(
                            offset(last, -0.5*distance),
                            offset(offsets, +0.5*distance + 10)  // safety offset
                        );
                        gaps.insert(gaps.end(), diff_pp.begin(), diff_pp.end());
                    }
                }
                
                if (offsets.empty()) break;
                if (i > loop_number) break; // we were only looking for gaps this time
                
                last = offsets;
                for (Polygons::const_iterator polygon = offsets.begin(); polygon != offsets.end(); ++polygon) {
                    PerimeterGeneratorLoop loop(*polygon, i);
                    loop.is_contour = polygon->is_counter_clockwise();
                    if (loop.is_contour) {
                        contours[i].push_back(loop);
                    } else {
                        holes[i].push_back(loop);
                    }
                }
            }
            
            // nest loops: holes first
            for (int d = 0; d <= loop_number; ++d) {
                PerimeterGeneratorLoops &holes_d = holes[d];
                
                // loop through all holes having depth == d
                for (int i = 0; i < (int)holes_d.size(); ++i) {
                    const PerimeterGeneratorLoop &loop = holes_d[i];
                    
                    // find the hole loop that contains this one, if any
                    for (int t = d+1; t <= loop_number; ++t) {
                        for (int j = 0; j < (int)holes[t].size(); ++j) {
                            PerimeterGeneratorLoop &candidate_parent = holes[t][j];
                            if (candidate_parent.polygon.contains(loop.polygon.first_point())) {
                                candidate_parent.children.push_back(loop);
                                holes_d.erase(holes_d.begin() + i);
                                --i;
                                goto NEXT_LOOP;
                            }
                        }
                    }
                    
                    // if no hole contains this hole, find the contour loop that contains it
                    for (int t = loop_number; t >= 0; --t) {
                        for (int j = 0; j < (int)contours[t].size(); ++j) {
                            PerimeterGeneratorLoop &candidate_parent = contours[t][j];
                            if (candidate_parent.polygon.contains(loop.polygon.first_point())) {
                                candidate_parent.children.push_back(loop);
                                holes_d.erase(holes_d.begin() + i);
                                --i;
                                goto NEXT_LOOP;
                            }
                        }
                    }
                    NEXT_LOOP: ;
                }
            }
        
            // nest contour loops
            for (int d = loop_number; d >= 1; --d) {
                PerimeterGeneratorLoops &contours_d = contours[d];
                
                // loop through all contours having depth == d
                for (int i = 0; i < (int)contours_d.size(); ++i) {
                    const PerimeterGeneratorLoop &loop = contours_d[i];
                
                    // find the contour loop that contains it
                    for (int t = d-1; t >= 0; --t) {
                        for (int j = 0; j < contours[t].size(); ++j) {
                            PerimeterGeneratorLoop &candidate_parent = contours[t][j];
                            if (candidate_parent.polygon.contains(loop.polygon.first_point())) {
                                candidate_parent.children.push_back(loop);
                                contours_d.erase(contours_d.begin() + i);
                                --i;
                                goto NEXT_CONTOUR;
                            }
                        }
                    }
                    
                    NEXT_CONTOUR: ;
                }
            }
        
            // at this point, all loops should be in contours[0]
            
            ExtrusionEntityCollection entities = this->_traverse_loops(contours.front(), thin_walls);
            
            // if brim will be printed, reverse the order of perimeters so that
            // we continue inwards after having finished the brim
            // TODO: add test for perimeter order
            if (this->config->external_perimeters_first
                || (this->layer_id == 0 && this->print_config->brim_width.value > 0))
                    entities.reverse();
            
            // append perimeters for this slice as a collection
            if (!entities.empty())
                this->loops->append(entities);
        } // for each loop of an island

        // fill gaps
        if (!gaps.empty()) {
            /*
            SVG svg("gaps.svg");
            svg.draw(union_ex(gaps));
            svg.Close();
            */
            
            // collapse 
            double min = 0.2 * perimeter_width * (1 - INSET_OVERLAP_TOLERANCE);
            double max = 2. * perimeter_spacing;
            ExPolygons gaps_ex = diff_ex(
                offset2(gaps, -min/2, +min/2),
                offset2(gaps, -max/2, +max/2),
                true
            );
            
            ThickPolylines polylines;
            for (ExPolygons::const_iterator ex = gaps_ex.begin(); ex != gaps_ex.end(); ++ex)
                ex->medial_axis(max, min, &polylines);
            
            if (!polylines.empty()) {
                ExtrusionEntityCollection gap_fill = this->_variable_width(polylines, 
                    erGapFill, this->solid_infill_flow);
                
                this->gap_fill->append(gap_fill.entities);
            
                /*  Make sure we don't infill narrow parts that are already gap-filled
                    (we only consider this surface's gaps to reduce the diff() complexity).
                    Growing actual extrusions ensures that gaps not filled by medial axis
                    are not subtracted from fill surfaces (they might be too short gaps
                    that medial axis skips but infill might join with other infill regions
                    and use zigzag).  */
                //FIXME Vojtech: This grows by a rounded extrusion width, not by line spacing,
                // therefore it may cover the area, but no the volume.
                last = diff(last, gap_fill.polygons_covered_by_width(10.f));
            }
        }

        // create one more offset to be used as boundary for fill
        // we offset by half the perimeter spacing (to get to the actual infill boundary)
        // and then we offset back and forth by half the infill spacing to only consider the
        // non-collapsing regions
        coord_t inset = 0;
        if (loop_number == 0) {
            // one loop
            inset += ext_perimeter_spacing / 2;
        } else if (loop_number > 0) {
            // two or more loops
            inset += perimeter_spacing / 2;
        }
        // only apply infill overlap if we actually have one perimeter
        if (inset > 0)
            inset -= this->config->get_abs_value("infill_overlap", inset + solid_infill_spacing / 2);
        // simplify infill contours according to resolution
        Polygons pp;
        for (ExPolygon &ex : union_ex(last))
            ex.simplify_p(SCALED_RESOLUTION, &pp);
        // collapse too narrow infill areas
        coord_t min_perimeter_infill_spacing = solid_infill_spacing * (1 - INSET_OVERLAP_TOLERANCE);
        // append infill areas to fill_surfaces
        this->fill_surfaces->append(
            offset2_ex(
                pp,
                -inset -min_perimeter_infill_spacing/2,
                +min_perimeter_infill_spacing/2),
            stInternal);
    } // for each island
}

ExtrusionEntityCollection
PerimeterGenerator::_traverse_loops(const PerimeterGeneratorLoops &loops,
    ThickPolylines &thin_walls) const
{
    // loops is an arrayref of ::Loop objects
    // turn each one into an ExtrusionLoop object
    ExtrusionEntityCollection coll;
    for (PerimeterGeneratorLoops::const_iterator loop = loops.begin();
        loop != loops.end(); ++loop) {
        bool is_external = loop->is_external();
        
        ExtrusionRole role;
        ExtrusionLoopRole loop_role;
        role = is_external ? erExternalPerimeter : erPerimeter;
        if (loop->is_internal_contour()) {
            // Note that we set loop role to ContourInternalPerimeter
            // also when loop is both internal and external (i.e.
            // there's only one contour loop).
            loop_role = elrContourInternalPerimeter;
        } else {
            loop_role = elrDefault;
        }
        
        // detect overhanging/bridging perimeters
        ExtrusionPaths paths;
        if (this->config->overhangs && this->layer_id > 0
            && !(this->object_config->support_material && this->object_config->support_material_contact_distance.value == 0)) {
            // get non-overhang paths by intersecting this loop with the grown lower slices
            extrusion_paths_append(
                paths,
                intersection_pl(loop->polygon, this->_lower_slices_p),
                role,
                is_external ? this->_ext_mm3_per_mm           : this->_mm3_per_mm,
                is_external ? this->ext_perimeter_flow.width  : this->perimeter_flow.width,
                this->layer_height);
            
            // get overhang paths by checking what parts of this loop fall 
            // outside the grown lower slices (thus where the distance between
            // the loop centerline and original lower slices is >= half nozzle diameter
            extrusion_paths_append(
                paths,
                diff_pl(loop->polygon, this->_lower_slices_p),
                erOverhangPerimeter,
                this->_mm3_per_mm_overhang,
                this->overhang_flow.width,
                this->overhang_flow.height);
            
            // reapply the nearest point search for starting point
            // We allow polyline reversal because Clipper may have randomly
            // reversed polylines during clipping.
            paths = (ExtrusionPaths)ExtrusionEntityCollection(paths).chained_path();
        } else {
            ExtrusionPath path(role);
            path.polyline   = loop->polygon.split_at_first_point();
            path.mm3_per_mm = is_external ? this->_ext_mm3_per_mm           : this->_mm3_per_mm;
            path.width      = is_external ? this->ext_perimeter_flow.width  : this->perimeter_flow.width;
            path.height     = this->layer_height;
            paths.push_back(path);
        }
        
        coll.append(ExtrusionLoop(paths, loop_role));
    }
    
    // append thin walls to the nearest-neighbor search (only for first iteration)
    if (!thin_walls.empty()) {
        ExtrusionEntityCollection tw = this->_variable_width
            (thin_walls, erExternalPerimeter, this->ext_perimeter_flow);
        
        coll.append(tw.entities);
        thin_walls.clear();
    }
    
    // sort entities into a new collection using a nearest-neighbor search,
    // preserving the original indices which are useful for detecting thin walls
    ExtrusionEntityCollection sorted_coll;
    coll.chained_path(&sorted_coll, false, erMixed, &sorted_coll.orig_indices);
    
    // traverse children and build the final collection
    ExtrusionEntityCollection entities;
    for (std::vector<size_t>::const_iterator idx = sorted_coll.orig_indices.begin();
        idx != sorted_coll.orig_indices.end();
        ++idx) {
        
        if (*idx >= loops.size()) {
            // this is a thin wall
            // let's get it from the sorted collection as it might have been reversed
            size_t i = idx - sorted_coll.orig_indices.begin();
            entities.append(*sorted_coll.entities[i]);
        } else {
            const PerimeterGeneratorLoop &loop = loops[*idx];
            ExtrusionLoop eloop = *dynamic_cast<ExtrusionLoop*>(coll.entities[*idx]);
            
            ExtrusionEntityCollection children = this->_traverse_loops(loop.children, thin_walls);
            if (loop.is_contour) {
                eloop.make_counter_clockwise();
                entities.append(children.entities);
                entities.append(eloop);
            } else {
                eloop.make_clockwise();
                entities.append(eloop);
                entities.append(children.entities);
            }
        }
    }
    return entities;
}

ExtrusionEntityCollection
PerimeterGenerator::_variable_width(const ThickPolylines &polylines, ExtrusionRole role, Flow flow) const
{
    // this value determines granularity of adaptive width, as G-code does not allow
    // variable extrusion within a single move; this value shall only affect the amount
    // of segments, and any pruning shall be performed before we apply this tolerance
    const double tolerance = scale_(0.05);
    
    ExtrusionEntityCollection coll;
    for (ThickPolylines::const_iterator p = polylines.begin(); p != polylines.end(); ++p) {
        ExtrusionPaths paths;
        ExtrusionPath path(role);
        ThickLines lines = p->thicklines();
        
        for (int i = 0; i < (int)lines.size(); ++i) {
            const ThickLine& line = lines[i];
            
            const coordf_t line_len = line.length();
            if (line_len < SCALED_EPSILON) continue;
            
            double thickness_delta = fabs(line.a_width - line.b_width);
            if (thickness_delta > tolerance) {
                const unsigned short segments = ceil(thickness_delta / tolerance);
                const coordf_t seg_len = line_len / segments;
                Points pp;
                std::vector<coordf_t> width;
                {
                    pp.push_back(line.a);
                    width.push_back(line.a_width);
                    for (size_t j = 1; j < segments; ++j) {
                        pp.push_back(line.point_at(j*seg_len));
                        
                        coordf_t w = line.a_width + (j*seg_len) * (line.b_width-line.a_width) / line_len;
                        width.push_back(w);
                        width.push_back(w);
                    }
                    pp.push_back(line.b);
                    width.push_back(line.b_width);
                    
                    assert(pp.size() == segments + 1);
                    assert(width.size() == segments*2);
                }
                
                // delete this line and insert new ones
                lines.erase(lines.begin() + i);
                for (size_t j = 0; j < segments; ++j) {
                    ThickLine new_line(pp[j], pp[j+1]);
                    new_line.a_width = width[2*j];
                    new_line.b_width = width[2*j+1];
                    lines.insert(lines.begin() + i + j, new_line);
                }
                
                --i;
                continue;
            }
            
            const double w = fmax(line.a_width, line.b_width);
            
            if (path.polyline.points.empty()) {
                path.polyline.append(line.a);
                path.polyline.append(line.b);
                
                flow.width = unscale(w);
                #ifdef SLIC3R_DEBUG
                printf("  filling %f gap\n", flow.width);
                #endif
                path.mm3_per_mm  = flow.mm3_per_mm();
                path.width       = flow.width;
                path.height      = flow.height;
            } else {
                thickness_delta = fabs(scale_(flow.width) - w);
                if (thickness_delta <= tolerance) {
                    // the width difference between this line and the current flow width is 
                    // within the accepted tolerance
                
                    path.polyline.append(line.b);
                } else {
                    // we need to initialize a new line
                    paths.push_back(path);
                    path = ExtrusionPath(role);
                    --i;
                }
            }
        }
        if (path.polyline.is_valid())
            paths.push_back(path);
        
        // append paths to collection
        if (!paths.empty()) {
            if (paths.front().first_point().coincides_with(paths.back().last_point()))
                coll.append(ExtrusionLoop(paths));
            else
                coll.append(paths);
        }
    }
    
    return coll;
}

bool
PerimeterGeneratorLoop::is_internal_contour() const
{
    if (this->is_contour) {
        // an internal contour is a contour containing no other contours
        for (std::vector<PerimeterGeneratorLoop>::const_iterator loop = this->children.begin();
            loop != this->children.end(); ++loop) {
            if (loop->is_contour) {
                return false;
            }
        }
        return true;
    }
    return false;
}

}
