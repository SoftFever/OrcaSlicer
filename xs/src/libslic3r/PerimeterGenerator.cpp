#include "PerimeterGenerator.hpp"
#include "ClipperUtils.hpp"
#include "ExtrusionEntityCollection.hpp"

namespace Slic3r {

void
PerimeterGenerator::process()
{
    // other perimeters
    this->_mm3_per_mm           = this->perimeter_flow.mm3_per_mm();
    coord_t pwidth              = this->perimeter_flow.scaled_width();
    coord_t pspacing            = this->perimeter_flow.scaled_spacing();
    
    // external perimeters
    this->_ext_mm3_per_mm       = this->ext_perimeter_flow.mm3_per_mm();
    coord_t ext_pwidth          = this->ext_perimeter_flow.scaled_width();
    coord_t ext_pspacing        = this->ext_perimeter_flow.scaled_spacing();
    coord_t ext_pspacing2       = this->ext_perimeter_flow.scaled_spacing(this->perimeter_flow);
    
    // overhang perimeters
    this->_mm3_per_mm_overhang  = this->overhang_flow.mm3_per_mm();
    
    // solid infill
    coord_t ispacing            = this->solid_infill_flow.scaled_spacing();
    coord_t gap_area_threshold  = pwidth * pwidth;
    
    // Calculate the minimum required spacing between two adjacent traces.
    // This should be equal to the nominal flow spacing but we experiment
    // with some tolerance in order to avoid triggering medial axis when
    // some squishing might work. Loops are still spaced by the entire
    // flow spacing; this only applies to collapsing parts.
    // For ext_min_spacing we use the ext_pspacing calculated for two adjacent
    // external loops (which is the correct way) instead of using ext_pspacing2
    // which is the spacing between external and internal, which is not correct
    // and would make the collapsing (thus the details resolution) dependent on 
    // internal flow which is unrelated.
    coord_t min_spacing         = pspacing      * (1 - INSET_OVERLAP_TOLERANCE);
    coord_t ext_min_spacing     = ext_pspacing  * (1 - INSET_OVERLAP_TOLERANCE);
    
    // prepare grown lower layer slices for overhang detection
    if (this->lower_slices != NULL && this->config->overhangs) {
        // We consider overhang any part where the entire nozzle diameter is not supported by the
        // lower layer, so we take lower slices and offset them by half the nozzle diameter used 
        // in the current layer
        double nozzle_diameter = this->print_config->nozzle_diameter.get_at(this->config->perimeter_extruder-1);
        
        this->_lower_slices_p = offset(*this->lower_slices, scale_(+nozzle_diameter/2));
    }
    
    // we need to process each island separately because we might have different
    // extra perimeters for each one
    for (Surfaces::const_iterator surface = this->slices->surfaces.begin();
        surface != this->slices->surfaces.end(); ++surface) {
        // detect how many perimeters must be generated for this island
        signed short loop_number = this->config->perimeters + surface->extra_perimeters;
        loop_number--;  // 0-indexed loops
        
        Polygons gaps;
        
        Polygons last = surface->expolygon.simplify_p(SCALED_RESOLUTION);
        if (loop_number >= 0) {  // no loops = -1
            
            std::vector<PerimeterGeneratorLoops> contours(loop_number+1);    // depth => loops
            std::vector<PerimeterGeneratorLoops> holes(loop_number+1);       // depth => loops
            Polylines thin_walls;
            
            // we loop one time more than needed in order to find gaps after the last perimeter was applied
            for (signed short i = 0; i <= loop_number+1; ++i) {  // outer loop is 0
                Polygons offsets;
                if (i == 0) {
                    // the minimum thickness of a single loop is:
                    // ext_width/2 + ext_spacing/2 + spacing/2 + width/2
                    if (this->config->thin_walls) {
                        offsets = offset2(
                            last,
                            -(ext_pwidth/2 + ext_min_spacing/2 - 1),
                            +(ext_min_spacing/2 - 1)
                        );
                    } else {
                        offsets = offset(last, -ext_pwidth/2);
                    }
                    
                    // look for thin walls
                    if (this->config->thin_walls) {
                        Polygons diffpp = diff(
                            last,
                            offset(offsets, +ext_pwidth/2),
                            true  // medial axis requires non-overlapping geometry
                        );
                        
                        // the following offset2 ensures almost nothing in @thin_walls is narrower than $min_width
                        // (actually, something larger than that still may exist due to mitering or other causes)
                        coord_t min_width = ext_pwidth / 2;
                        ExPolygons expp = offset2_ex(diffpp, -min_width/2, +min_width/2);
                        
                        // the maximum thickness of our thin wall area is equal to the minimum thickness of a single loop
                        Polylines pp;
                        for (ExPolygons::const_iterator ex = expp.begin(); ex != expp.end(); ++ex)
                            ex->medial_axis(ext_pwidth + ext_pspacing2, min_width, &pp);
                        
                        double threshold = ext_pwidth * 2;
                        for (Polylines::const_iterator p = pp.begin(); p != pp.end(); ++p) {
                            if (p->length() > threshold) {
                                thin_walls.push_back(*p);
                            }
                        }
                        
                        #ifdef DEBUG
                        printf("  %zu thin walls detected\n", thin_walls.size());
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
                    coord_t distance = (i == 1) ? ext_pspacing2 : pspacing;
                    
                    if (this->config->thin_walls) {
                        offsets = offset2(
                            last,
                            -(distance + min_spacing/2 - 1),
                            +(min_spacing/2 - 1)
                        );
                    } else {
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
                        ExPolygons diff_expp = diff_ex(
                            offset(last, -0.5*distance),
                            offset(offsets, +0.5*distance + 10)  // safety offset
                        );
                        for (ExPolygons::const_iterator ex = diff_expp.begin(); ex != diff_expp.end(); ++ex) {
                            if (fabs(ex->area()) >= gap_area_threshold) {
                                Polygons pp = *ex;
                                gaps.insert(gaps.end(), pp.begin(), pp.end());
                            }
                        }
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
            for (signed short d = 0; d <= loop_number; ++d) {
                PerimeterGeneratorLoops &holes_d = holes[d];
                
                // loop through all holes having depth == d
                for (signed short i = 0; i < holes_d.size(); ++i) {
                    const PerimeterGeneratorLoop &loop = holes_d[i];
                    
                    // find the hole loop that contains this one, if any
                    for (signed short t = d+1; t <= loop_number; ++t) {
                        for (signed short j = 0; j < holes[t].size(); ++j) {
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
                    for (signed short t = loop_number; t >= 0; --t) {
                        for (signed short j = 0; j < contours[t].size(); ++j) {
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
            for (signed short d = loop_number; d >= 1; --d) {
                PerimeterGeneratorLoops &contours_d = contours[d];
                
                // loop through all contours having depth == d
                for (signed short i = 0; i < contours_d.size(); ++i) {
                    const PerimeterGeneratorLoop &loop = contours_d[i];
                
                    // find the contour loop that contains it
                    for (signed short t = d-1; t >= 0; --t) {
                        for (signed short j = 0; j < contours[t].size(); ++j) {
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
        }
        
        // fill gaps
        if (!gaps.empty()) {
            /*
            if (false) {
                require "Slic3r/SVG.pm";
                Slic3r::SVG::output(
                    "gaps.svg",
                    expolygons => union_ex(\@gaps),
                );
            }
            */
            
            // where $pwidth < thickness < 2*$pspacing, infill with width = 2*$pwidth
            // where 0.1*$pwidth < thickness < $pwidth, infill with width = 1*$pwidth
            std::vector<PerimeterGeneratorGapSize> gap_sizes;
            gap_sizes.push_back(PerimeterGeneratorGapSize(pwidth, 2*pspacing, 2*pwidth));
            gap_sizes.push_back(PerimeterGeneratorGapSize(0.1*pwidth, pwidth, 1*pwidth));
            
            for (std::vector<PerimeterGeneratorGapSize>::const_iterator gap_size = gap_sizes.begin();
                gap_size != gap_sizes.end(); ++gap_size) {
                ExtrusionEntityCollection gap_fill = this->_fill_gaps(gap_size->min, 
                    gap_size->max, unscale(gap_size->width), gaps);
                this->gap_fill->append(gap_fill.entities);
                
                // Make sure we don't infill narrow parts that are already gap-filled
                // (we only consider this surface's gaps to reduce the diff() complexity).
                // Growing actual extrusions ensures that gaps not filled by medial axis
                // are not subtracted from fill surfaces (they might be too short gaps
                // that medial axis skips but infill might join with other infill regions
                // and use zigzag).
                coord_t dist = gap_size->width/2;
                Polygons filled;
                for (ExtrusionEntitiesPtr::const_iterator it = gap_fill.entities.begin();
                    it != gap_fill.entities.end(); ++it) {
                    Polygons f;
                    offset((*it)->as_polyline(), &f, dist);
                    filled.insert(filled.end(), f.begin(), f.end());
                }
                last = diff(last, filled);
                gaps = diff(gaps, filled);  // prevent more gap fill here
            }
        }
        
        // create one more offset to be used as boundary for fill
        // we offset by half the perimeter spacing (to get to the actual infill boundary)
        // and then we offset back and forth by half the infill spacing to only consider the
        // non-collapsing regions
        coord_t inset = 0;
        if (loop_number == 0) {
            // one loop
            inset += ext_pspacing2/2;
        } else if (loop_number > 0) {
            // two or more loops
            inset += pspacing/2;
        }
        
        // only apply infill overlap if we actually have one perimeter
        if (inset > 0)
            inset -= this->config->get_abs_value("infill_overlap", inset + ispacing/2);
        
        {
            ExPolygons expp = union_ex(last);
            
            // simplify infill contours according to resolution
            Polygons pp;
            for (ExPolygons::const_iterator ex = expp.begin(); ex != expp.end(); ++ex)
                ex->simplify_p(SCALED_RESOLUTION, &pp);
            
            // collapse too narrow infill areas
            coord_t min_perimeter_infill_spacing = ispacing * (1 - INSET_OVERLAP_TOLERANCE);
            expp = offset2_ex(
                pp,
                -inset -min_perimeter_infill_spacing/2,
                +min_perimeter_infill_spacing/2
            );
            
            // append infill areas to fill_surfaces
            for (ExPolygons::const_iterator ex = expp.begin(); ex != expp.end(); ++ex)
                this->fill_surfaces->surfaces.push_back(Surface(stInternal, *ex));  // use a bogus surface type
        }
    }
}

ExtrusionEntityCollection
PerimeterGenerator::_traverse_loops(const PerimeterGeneratorLoops &loops,
    Polylines &thin_walls) const
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
            {
                Polylines polylines;
                intersection((Polygons)loop->polygon, this->_lower_slices_p, &polylines);
                
                for (Polylines::const_iterator polyline = polylines.begin(); polyline != polylines.end(); ++polyline) {
                    ExtrusionPath path(role);
                    path.polyline   = *polyline;
                    path.mm3_per_mm = is_external ? this->_ext_mm3_per_mm           : this->_mm3_per_mm;
                    path.width      = is_external ? this->ext_perimeter_flow.width  : this->perimeter_flow.width;
                    path.height     = this->layer_height;
                    paths.push_back(path);
                }
            }
            
            // get overhang paths by checking what parts of this loop fall 
            // outside the grown lower slices (thus where the distance between
            // the loop centerline and original lower slices is >= half nozzle diameter
            {
                Polylines polylines;
                diff((Polygons)loop->polygon, this->_lower_slices_p, &polylines);
                
                for (Polylines::const_iterator polyline = polylines.begin(); polyline != polylines.end(); ++polyline) {
                    ExtrusionPath path(erOverhangPerimeter);
                    path.polyline   = *polyline;
                    path.mm3_per_mm = this->_mm3_per_mm_overhang;
                    path.width      = this->overhang_flow.width;
                    path.height     = this->overhang_flow.height;
                    paths.push_back(path);
                }
            }
            
            // reapply the nearest point search for starting point
            // We allow polyline reversal because Clipper may have randomly
            // reversed polylines during clipping.
            paths = ExtrusionEntityCollection(paths).chained_path();
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
    for (Polylines::const_iterator polyline = thin_walls.begin(); polyline != thin_walls.end(); ++polyline) {
        ExtrusionPath path(erExternalPerimeter);
        path.polyline   = *polyline;
        path.mm3_per_mm = this->_mm3_per_mm;
        path.width      = this->perimeter_flow.width;
        path.height     = this->layer_height;
        coll.append(path);
    }
    thin_walls.clear();
    
    // sort entities into a new collection using a nearest-neighbor search,
    // preserving the original indices which are useful for detecting thin walls
    ExtrusionEntityCollection sorted_coll;
    coll.chained_path(&sorted_coll, false, &sorted_coll.orig_indices);
    
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
PerimeterGenerator::_fill_gaps(double min, double max, double w,
    const Polygons &gaps) const
{
    ExtrusionEntityCollection coll;
    
    min *= (1 - INSET_OVERLAP_TOLERANCE);
    
    ExPolygons curr = diff_ex(
        offset2(gaps, -min/2, +min/2),
        offset2(gaps, -max/2, +max/2),
        true
    );
    
    Polylines polylines;
    for (ExPolygons::const_iterator ex = curr.begin(); ex != curr.end(); ++ex)
        ex->medial_axis(max, min/2, &polylines);
    if (polylines.empty())
        return coll;
    
    #ifdef SLIC3R_DEBUG
    if (!curr.empty())
        printf("  %zu gaps filled with extrusion width = %f\n", curr.size(), w);
    #endif
    
    //my $flow = $layerm->flow(FLOW_ROLE_SOLID_INFILL, 0, $w);
    Flow flow(
        w, this->layer_height, this->solid_infill_flow.nozzle_diameter
    );
    
    double mm3_per_mm = flow.mm3_per_mm();
    
    for (Polylines::const_iterator p = polylines.begin(); p != polylines.end(); ++p) {
        ExtrusionPath path(erGapFill);
        path.polyline   = *p;
        path.mm3_per_mm = mm3_per_mm;
        path.width      = flow.width;
        path.height     = this->layer_height;
        
        if (p->is_valid() && p->first_point().coincides_with(p->last_point())) {
            // since medial_axis() now returns only Polyline objects, detect loops here
            ExtrusionLoop loop;
            loop.paths.push_back(path);
            coll.append(loop);
        } else {
            coll.append(path);
        }
    }
    
    return coll;
}

bool
PerimeterGeneratorLoop::is_external() const
{
    return this->depth == 0;
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
