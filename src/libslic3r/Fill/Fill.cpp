#include <assert.h>
#include <stdio.h>
#include <memory>

#include "../ClipperUtils.hpp"
#include "../Geometry.hpp"
#include "../Layer.hpp"
#include "../Print.hpp"
#include "../PrintConfig.hpp"
#include "../Surface.hpp"

#include "FillBase.hpp"

namespace Slic3r {

struct SurfaceFillParams
{
	SurfaceFillParams() : flow(0.f, 0.f, 0.f, false) { memset(this, 0, sizeof(*this)); }
	// Zero based extruder ID.
	unsigned int 	extruder;
	// Infill pattern, adjusted for the density etc.
	InfillPattern  	pattern;

    // FillBase
    // in unscaled coordinates
    coordf_t    	spacing;
    // infill / perimeter overlap, in unscaled coordinates
    coordf_t    	overlap;
    // Angle as provided by the region config, in radians.
    float       	angle;
    // Non-negative for a bridge.
    float 			bridge_angle;

    // FillParams
    float       	density;
    // Don't connect the fill lines around the inner perimeter.
    bool        	dont_connect;
    // Don't adjust spacing to fill the space evenly.
    bool        	dont_adjust;

    // width, height of extrusion, nozzle diameter, is bridge
    // For the output, for fill generator.
	Flow 			flow;

	// For the output
	ExtrusionRole	extrusion_role;

	// Various print settings?

	// Index of this entry in a linear vector.
	size_t 			idx;


	bool operator<(const SurfaceFillParams &rhs) const {
#define RETURN_COMPARE_NON_EQUAL(KEY) if (this->KEY < rhs.KEY) return true; if (this->KEY > rhs.KEY) return false;
#define RETURN_COMPARE_NON_EQUAL_TYPED(TYPE, KEY) if (TYPE(this->KEY) < TYPE(rhs.KEY)) return true; if (TYPE(this->KEY) > TYPE(rhs.KEY)) return false;

		// Sort first by decreasing bridging angle, so that the bridges are processed with priority when trimming one layer by the other.
		if (this->bridge_angle > rhs.bridge_angle) return true; 
		if (this->bridge_angle < rhs.bridge_angle) return false;

		RETURN_COMPARE_NON_EQUAL(extruder);
		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, pattern);
		RETURN_COMPARE_NON_EQUAL(spacing);
		RETURN_COMPARE_NON_EQUAL(overlap);
		RETURN_COMPARE_NON_EQUAL(angle);
		RETURN_COMPARE_NON_EQUAL(density);
		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, dont_connect);
		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, dont_adjust);
		RETURN_COMPARE_NON_EQUAL(flow.width);
		RETURN_COMPARE_NON_EQUAL(flow.height);
		RETURN_COMPARE_NON_EQUAL(flow.nozzle_diameter);
		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, flow.bridge);
		RETURN_COMPARE_NON_EQUAL_TYPED(unsigned, extrusion_role);
		return false;
	}

	bool operator==(const SurfaceFillParams &rhs) const {
		return  this->extruder 			== rhs.extruder 		&&
				this->pattern 			== rhs.pattern 			&&
				this->pattern 			== rhs.pattern 			&&
				this->spacing 			== rhs.spacing 			&&
				this->overlap 			== rhs.overlap 			&&
				this->angle   			== rhs.angle   			&&
				this->density   		== rhs.density   		&&
				this->dont_connect  	== rhs.dont_connect 	&&
				this->dont_adjust   	== rhs.dont_adjust 		&&
				this->flow 				== rhs.flow 			&&
				this->extrusion_role	== rhs.extrusion_role;
	}
};

struct SurfaceFill {
	SurfaceFill(const SurfaceFillParams& params) : region_id(size_t(-1)), surface(stCount, ExPolygon()), params(params) {}

	size_t 				region_id;
	Surface 			surface;
	ExPolygons       	expolygons;
	SurfaceFillParams	params;
};

std::vector<SurfaceFill> group_fills(const Layer &layer)
{
	std::vector<SurfaceFill> surface_fills;

	// Fill in a map of a region & surface to SurfaceFillParams.
	std::set<SurfaceFillParams> 						set_surface_params;
	std::vector<std::vector<const SurfaceFillParams*>> 	region_to_surface_params(layer.regions().size(), std::vector<const SurfaceFillParams*>());
    SurfaceFillParams									params;
    bool 												has_internal_voids = false;
	for (size_t region_id = 0; region_id < layer.regions().size(); ++ region_id) {
		const LayerRegion  &layerm = *layer.regions()[region_id];
		region_to_surface_params[region_id].assign(layerm.fill_surfaces.size(), nullptr);
	    for (const Surface &surface : layerm.fill_surfaces.surfaces)
	        if (surface.surface_type == stInternalVoid)
	        	has_internal_voids = true;
	        else {
		        FlowRole extrusion_role = (surface.surface_type == stTop) ? frTopSolidInfill : (surface.is_solid() ? frSolidInfill : frInfill);
		        bool     is_bridge 	    = layerm.layer()->id() > 0 && surface.is_bridge();
		        params.extruder 	 = layerm.region()->extruder(extrusion_role);
		        params.pattern 		 = layerm.region()->config().fill_pattern.value;
		        params.density       = float(layerm.region()->config().fill_density);

		        if (surface.is_solid()) {
		            params.density = 100.f;
		            params.pattern = (surface.is_external() && ! is_bridge) ? 
						(surface.is_top() ? layerm.region()->config().top_fill_pattern.value : layerm.region()->config().bottom_fill_pattern.value) :
		                ipRectilinear;
		        } else if (params.density <= 0)
		            continue;

		        params.extrusion_role =
		            is_bridge ?
		                erBridgeInfill :
		                (surface.is_solid() ?
		                    ((surface.surface_type == stTop) ? erTopSolidInfill : erSolidInfill) :
		                    erInternalInfill);
		        params.bridge_angle = float(surface.bridge_angle);
		        params.angle 		= float(Geometry::deg2rad(layerm.region()->config().fill_angle.value));
		        
		        // calculate the actual flow we'll be using for this infill
		        params.flow = layerm.region()->flow(
		            extrusion_role,
		            (surface.thickness == -1) ? layerm.layer()->height : surface.thickness, // extrusion height
		            is_bridge || Fill::use_bridge_flow(params.pattern), 					// bridge flow?
		            layerm.layer()->id() == 0,          									// first layer?
		            -1,                                 									// auto width
		            *layerm.layer()->object()
		        );
		        
		        // Calculate flow spacing for infill pattern generation.
		        if (! surface.is_solid() && ! is_bridge) {
		            // it's internal infill, so we can calculate a generic flow spacing 
		            // for all layers, for avoiding the ugly effect of
		            // misaligned infill on first layer because of different extrusion width and
		            // layer height
		            params.spacing = layerm.region()->flow(
			                frInfill,
			                layerm.layer()->object()->config().layer_height.value,  // TODO: handle infill_every_layers?
			                false,  // no bridge
			                false,  // no first layer
			                -1,     // auto width
			                *layer.object()
			            ).spacing();
		        } else
		            params.spacing = params.flow.spacing();

		        auto it_params = set_surface_params.find(params);
		        if (it_params == set_surface_params.end())
		        	it_params = set_surface_params.insert(it_params, params);
		        region_to_surface_params[region_id][&surface - &layerm.fill_surfaces.surfaces.front()] = &(*it_params);
		    }
	}

	surface_fills.reserve(set_surface_params.size());
	for (const SurfaceFillParams &params : set_surface_params) {
		const_cast<SurfaceFillParams&>(params).idx = surface_fills.size();
		surface_fills.emplace_back(params);
	}

	for (size_t region_id = 0; region_id < layer.regions().size(); ++ region_id) {
		const LayerRegion &layerm = *layer.regions()[region_id];
	    for (const Surface &surface : layerm.fill_surfaces.surfaces)
	        if (surface.surface_type != stInternalVoid) {
	        	const SurfaceFillParams *params = region_to_surface_params[region_id][&surface - &layerm.fill_surfaces.surfaces.front()];
				if (params != nullptr) {
	        		SurfaceFill &fill = surface_fills[params->idx];
	        		if (fill.region_id = size_t(-1)) {
	        			fill.region_id = region_id;
	        			fill.surface = surface;
	        			fill.expolygons.emplace_back(std::move(fill.surface.expolygon));
	        		} else
	        			fill.expolygons.emplace_back(surface.expolygon);
				}
	        }
	}

	{
		Polygons all_polygons;
		for (SurfaceFill &fill : surface_fills)
			if (! fill.expolygons.empty() && (fill.expolygons.size() > 1 || ! all_polygons.empty())) {
				Polygons polys = to_polygons(std::move(fill.expolygons));
	            // Make a union of polygons, use a safety offset, subtract the preceding polygons.
			    // Bridges are processed first (see SurfaceFill::operator<())
	            fill.expolygons = all_polygons.empty() ? union_ex(polys, true) : diff_ex(polys, all_polygons, true);
				append(all_polygons, std::move(polys));
	        }
	}

    // we need to detect any narrow surfaces that might collapse
    // when adding spacing below
    // such narrow surfaces are often generated in sloping walls
    // by bridge_over_infill() and combine_infill() as a result of the
    // subtraction of the combinable area from the layer infill area,
    // which leaves small areas near the perimeters
    // we are going to grow such regions by overlapping them with the void (if any)
    // TODO: detect and investigate whether there could be narrow regions without
    // any void neighbors
    if (has_internal_voids) {
    	// Internal voids are generated only if "infill_only_where_needed" or "infill_every_layers" are active.
        coord_t  distance_between_surfaces = 0;
        Polygons surfaces_polygons;
        Polygons voids;
		int      region_internal_infill = -1;
		int		 region_solid_infill = -1;
		int		 region_some_infill = -1;
    	for (SurfaceFill &surface_fill : surface_fills)
			if (! surface_fill.expolygons.empty()) {
    			distance_between_surfaces = std::max(distance_between_surfaces, surface_fill.params.flow.scaled_spacing());
				append((surface_fill.surface.surface_type == stInternalVoid) ? voids : surfaces_polygons, to_polygons(surface_fill.expolygons));
				if (surface_fill.surface.surface_type == stInternalSolid)
					region_internal_infill = (int)surface_fill.region_id;
				if (surface_fill.surface.is_solid())
					region_solid_infill = (int)surface_fill.region_id;
				if (surface_fill.surface.surface_type != stInternalVoid)
					region_some_infill = (int)surface_fill.region_id;
			}
    	if (! voids.empty() && ! surfaces_polygons.empty()) {
    		// First clip voids by the printing polygons, as the voids were ignored by the loop above during mutual clipping.
    		voids = diff(voids, surfaces_polygons);
	        // Corners of infill regions, which would not be filled with an extrusion path with a radius of distance_between_surfaces/2
	        Polygons collapsed = diff(
	            surfaces_polygons,
	            offset2(surfaces_polygons, (float)-distance_between_surfaces/2, (float)+distance_between_surfaces/2),
	            true);
	        //FIXME why the voids are added to collapsed here? First it is expensive, second the result may lead to some unwanted regions being
	        // added if two offsetted void regions merge.
	        // polygons_append(voids, collapsed);
	        ExPolygons extensions = intersection_ex(offset(collapsed, (float)distance_between_surfaces), voids, true);
	        // Now find an internal infill SurfaceFill to add these extrusions to.
	        SurfaceFill *internal_solid_fill = nullptr;
			unsigned int region_id = 0;
			if (region_internal_infill != -1)
				region_id = region_internal_infill;
			else if (region_solid_infill != -1)
				region_id = region_solid_infill;
			else if (region_some_infill != -1)
				region_id = region_some_infill;
			const LayerRegion& layerm = *layer.regions()[region_id];
	        for (SurfaceFill &surface_fill : surface_fills)
	        	if (surface_fill.surface.surface_type == stInternalSolid && std::abs(layerm.layer()->height - surface_fill.params.flow.height) < EPSILON) {
	        		internal_solid_fill = &surface_fill;
	        		break;
	        	}
	        if (internal_solid_fill == nullptr) {
	        	// Produce another solid fill.
		        params.extruder 	 = layerm.region()->extruder(frSolidInfill);
	            params.pattern 		 = ipRectilinear;
	            params.density 		 = 100.f;
		        params.extrusion_role = erInternalInfill;
		        params.angle 		= float(Geometry::deg2rad(layerm.region()->config().fill_angle.value));
		        // calculate the actual flow we'll be using for this infill
		        params.flow = layerm.region()->flow(
		            frSolidInfill,
		            layerm.layer()->height, 		// extrusion height
		            false, 							// bridge flow?
		            layerm.layer()->id() == 0,      // first layer?
		            -1,                             // auto width
		            *layer.object()
		        );
		        params.spacing = params.flow.spacing();	        
				surface_fills.emplace_back(params);
				surface_fills.back().surface.surface_type = stInternalSolid;
				surface_fills.back().surface.thickness = layer.height;
				surface_fills.back().expolygons = std::move(extensions);
	        } else {
	        	append(extensions, std::move(internal_solid_fill->expolygons));
	        	internal_solid_fill->expolygons = union_ex(extensions);
	        }
		}
    }

	return surface_fills;
}

// friend to Layer
void Layer::make_fills()
{
	for (LayerRegion *layerm : m_regions)
		layerm->fills.clear();

	std::vector<SurfaceFill>  surface_fills = group_fills(*this);
	const Slic3r::BoundingBox bbox = this->object()->bounding_box();

    for (SurfaceFill &surface_fill : surface_fills) {
        // Create the filler object.
        std::unique_ptr<Fill> f = std::unique_ptr<Fill>(Fill::new_from_type(surface_fill.params.pattern));
        f->set_bounding_box(bbox);
        f->layer_id = this->id();
        f->z 		= this->print_z;
        f->angle 	= surface_fill.params.angle;
        f->spacing  = surface_fill.params.spacing;

        // calculate flow spacing for infill pattern generation
        bool using_internal_flow = ! surface_fill.surface.is_solid() && ! surface_fill.params.flow.bridge;
        double link_max_length = 0.;
        if (! surface_fill.params.flow.bridge) {
#if 0
            link_max_length = layerm.region()->config().get_abs_value(surface.is_external() ? "external_fill_link_max_length" : "fill_link_max_length", flow.spacing());
//            printf("flow spacing: %f,  is_external: %d, link_max_length: %lf\n", flow.spacing(), int(surface.is_external()), link_max_length);
#else
            if (surface_fill.params.density > 80.) // 80%
                link_max_length = 3. * f->spacing;
#endif
        }

        // Maximum length of the perimeter segment linking two infill lines.
        f->link_max_length = (coord_t)scale_(link_max_length);
        // Used by the concentric infill pattern to clip the loops to create extrusion paths.
        f->loop_clipping = coord_t(scale_(surface_fill.params.flow.nozzle_diameter) * LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER);

        // apply half spacing using this flow's own spacing and generate infill
        FillParams params;
        params.density 		= float(0.01 * surface_fill.params.density);
        params.dont_adjust 	= surface_fill.params.dont_adjust; // false

        for (ExPolygon &expoly : surface_fill.expolygons) {
        	surface_fill.surface.expolygon = std::move(expoly);
	        Polylines polylines = f->fill_surface(&surface_fill.surface, params);
	        if (! polylines.empty()) {
		        // calculate actual flow from spacing (which might have been adjusted by the infill
		        // pattern generator)
		        double flow_mm3_per_mm = surface_fill.params.flow.mm3_per_mm();
		        double flow_width      = surface_fill.params.flow.width;
		        if (using_internal_flow) {
		            // if we used the internal flow we're not doing a solid infill
		            // so we can safely ignore the slight variation that might have
		            // been applied to f->spacing
		        } else {
		            Flow new_flow = Flow::new_from_spacing(float(f->spacing), surface_fill.params.flow.nozzle_diameter, surface_fill.params.flow.height, surface_fill.params.flow.bridge);
		        	flow_mm3_per_mm = new_flow.mm3_per_mm();
		        	flow_width      = new_flow.width;
		        }
		        // Save into layer.
		        auto *eec = new ExtrusionEntityCollection();
		        m_regions[surface_fill.region_id]->fills.entities.push_back(eec);
		        // Only concentric fills are not sorted.
		        eec->no_sort = f->no_sort();
		        extrusion_entities_append_paths(
		            eec->entities, std::move(polylines),
		            surface_fill.params.extrusion_role,
		            flow_mm3_per_mm, float(flow_width), surface_fill.params.flow.height);
		    }
		}
    }

    // add thin fill regions
    // Unpacks the collection, creates multiple collections per path.
    // The path type could be ExtrusionPath, ExtrusionLoop or ExtrusionEntityCollection.
    // Why the paths are unpacked?
	for (LayerRegion *layerm : m_regions)
	    for (const ExtrusionEntity *thin_fill : layerm->thin_fills.entities) {
	        ExtrusionEntityCollection &collection = *(new ExtrusionEntityCollection());
	        layerm->fills.entities.push_back(&collection);
	        collection.entities.push_back(thin_fill->clone());
	    }

#ifndef NDEBUG
	for (LayerRegion *layerm : m_regions)
	    for (size_t i = 0; i < layerm->fills.entities.size(); ++ i)
    	    assert(dynamic_cast<ExtrusionEntityCollection*>(layerm->fills.entities[i]) != nullptr);
#endif
}

} // namespace Slic3r
