#include "SLAPrint.hpp"
#include "SLA/SLASupportTree.hpp"
#include "SLA/SLABasePool.hpp"

#include <tbb/parallel_for.h>
//#include <tbb/spin_mutex.h>//#include "tbb/mutex.h"

#include "I18N.hpp"

//! macro used to mark string used at localization,
//! return same string
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

using SlicedModel = SlicedSupports;
using SupportTreePtr = std::unique_ptr<sla::SLASupportTree>;

class SLAPrintObject::SupportData {
public:
    sla::EigenMesh3D emesh;              // index-triangle representation
    sla::PointSet    support_points;     // all the support points (manual/auto)
    SupportTreePtr   support_tree_ptr;   // the supports
    SlicedSupports   support_slices;     // sliced supports
};

namespace {

const std::array<unsigned, slaposCount>     OBJ_STEP_LEVELS =
{
    0,
    20,
    30,
    50,
    70,
    90
};

const std::array<std::string, slaposCount> OBJ_STEP_LABELS =
{
    L("Slicing model"),                 // slaposObjectSlice,
    L("Generating islands"),            // slaposSupportIslands,
    L("Scanning model structure"),      // slaposSupportPoints,
    L("Generating support tree"),       // slaposSupportTree,
    L("Generating base pool"),          // slaposBasePool,
    L("Slicing supports")               // slaposSliceSupports,
};

const std::array<unsigned, slapsCount> PRINT_STEP_LEVELS =
{
    // This is after processing all the Print objects, so we start from 50%
    50,     // slapsRasterize
    90,     // slapsValidate
};

const std::array<std::string, slapsCount> PRINT_STEP_LABELS =
{
    L("Rasterizing layers"),         // slapsRasterize
    L("Validating"),                 // slapsValidate
};

}

void SLAPrint::clear()
{
	tbb::mutex::scoped_lock lock(this->state_mutex());
    // The following call should stop background processing if it is running.
    this->invalidate_all_steps();

    for (SLAPrintObject *object : m_objects) delete object;
	m_objects.clear();
}

SLAPrint::ApplyStatus SLAPrint::apply(const Model &model,
                                      const DynamicPrintConfig &config_in)
{
//	if (m_objects.empty())
//		return APPLY_STATUS_UNCHANGED;

    // Grab the lock for the Print / PrintObject milestones.
	tbb::mutex::scoped_lock lock(this->state_mutex());
	if (m_objects.empty() && model.objects.empty() && m_model.objects.empty())
        return APPLY_STATUS_UNCHANGED;

    // Temporary: just to have to correct layer height for the rasterization
    DynamicPrintConfig config(config_in);
    config.normalize();
    //auto lh = config.opt<ConfigOptionFloat>("layer_height");

	// Temporary quick fix, just invalidate everything.
    {
        for (SLAPrintObject *print_object : m_objects) {
			print_object->invalidate_all_steps();
            delete print_object;
		}
		m_objects.clear();
		this->invalidate_all_steps();

        // Copy the model by value (deep copy),
        // keep the Model / ModelObject / ModelInstance / ModelVolume IDs.
        m_model.assign_copy(model);
        // Generate new SLAPrintObjects.
        for (ModelObject *model_object : m_model.objects) {
            auto po = new SLAPrintObject(this, model_object);

            // po->m_config.layer_height.set(lh);
            po->m_config.apply(config, true);

            m_objects.emplace_back(po);
            for (ModelInstance *oinst : model_object->instances) {
                Point tr = Point::new_scale(oinst->get_offset()(X),
                                            oinst->get_offset()(Y));
                auto rotZ = float(oinst->get_rotation()(Z));
				po->m_instances.emplace_back(oinst->id(), tr, rotZ);
            }
        }
    }

	return APPLY_STATUS_INVALIDATED;
}

void SLAPrint::process()
{
    using namespace sla;

    // Assumption: at this point the print objects should be populated only with
    // the model objects we have to process and the instances are also filtered

    // shortcut to initial layer height
    auto ilh = float(m_material_config.initial_layer_height.getFloat());

    // Slicing the model object. This method is oversimplified and needs to
    // be compared with the fff slicing algorithm for verification
    auto slice_model = [this, ilh](SLAPrintObject& po) {
        auto lh = float(po.m_config.layer_height.getFloat());

        TriangleMesh mesh = po.transformed_mesh();
        TriangleMeshSlicer slicer(&mesh);
        auto bb3d = mesh.bounding_box();

        auto H = bb3d.max(Z) - bb3d.min(Z);
        auto gnd = float(bb3d.min(Z));
        std::vector<float> heights = {gnd};
        for(float h = gnd + ilh; h < gnd + H; h += lh) heights.emplace_back(h);

        auto& layers = po.m_model_slices;
        slicer.slice(heights, &layers, [this](){
            throw_if_canceled();
        });
    };

    auto support_points = [](SLAPrintObject& po) {
        ModelObject& mo = *po.m_model_object;
        if(!mo.sla_support_points.empty()) {
            po.m_supportdata.reset(new SLAPrintObject::SupportData());
            po.m_supportdata->emesh = sla::to_eigenmesh(po.transformed_mesh());

            po.m_supportdata->support_points =
                    sla::to_point_set(po.transformed_support_points());
        }

        // for(SLAPrintObject *po : pobjects) {
            // TODO: calculate automatic support points
            // po->m_supportdata->slice_cache contains the slices at this point
        //}
    };

    // In this step we create the supports
    auto support_tree = [this](SLAPrintObject& po) {
        if(!po.m_supportdata) return;

        auto& emesh = po.m_supportdata->emesh;
        auto& pts = po.m_supportdata->support_points; // nowhere filled yet
        try {
            sla::SupportConfig scfg;
            SLAPrintObjectConfig& c = po.m_config;

            scfg.head_front_radius_mm = c.support_head_front_radius.getFloat();
            scfg.head_back_radius_mm = c.support_head_back_radius.getFloat();
            scfg.head_penetration_mm = c.support_head_penetration.getFloat();
            scfg.head_width_mm = c.support_head_width.getFloat();
            scfg.object_elevation_mm = c.support_object_elevation.getFloat();
            scfg.tilt = c.support_critical_angle.getFloat() * PI / 180.0 ;
            scfg.max_bridge_length_mm = c.support_max_bridge_length.getFloat();
            scfg.pillar_radius_mm = c.support_pillar_radius.getFloat();

            sla::Controller ctl;
            ctl.statuscb = [this](unsigned st, const std::string& msg) {
                unsigned stinit = OBJ_STEP_LEVELS[slaposSupportTree];
                double d = (OBJ_STEP_LEVELS[slaposBasePool] - stinit) / 100.0;
                set_status(unsigned(stinit + st*d), msg);
            };
            ctl.stopcondition = [this](){ return canceled(); };
            ctl.cancelfn = [this]() { throw_if_canceled(); };

             po.m_supportdata->support_tree_ptr.reset(
                        new SLASupportTree(pts, emesh, scfg, ctl));

        } catch(sla::SLASupportsStoppedException&) {
            // no need to rethrow
            // throw_if_canceled();
        }
    };

    // This step generates the sla base pad
    auto base_pool = [](SLAPrintObject& po) {
        // this step can only go after the support tree has been created
        // and before the supports had been sliced. (or the slicing has to be
        // repeated)

        if(po.is_step_done(slaposSupportTree) &&
           po.m_supportdata &&
           po.m_supportdata->support_tree_ptr)
        {
            double wt = po.m_config.pad_wall_thickness.getFloat();
            double h =  po.m_config.pad_wall_height.getFloat();
            double md = po.m_config.pad_max_merge_distance.getFloat();
            double er = po.m_config.pad_edge_radius.getFloat();
            double lh = po.m_config.layer_height.getFloat();
            double elevation = po.m_config.support_object_elevation.getFloat();

            std::cout << "Pad height " << h << std::endl;

            sla::ExPolygons bp;
            if(elevation < h/2) sla::base_plate(po.transformed_mesh(), bp,
                                                float(h/2), float(lh));

            po.m_supportdata->support_tree_ptr->add_pad(bp, wt, h, md, er);
        }
    };

    // Slicing the support geometries similarly to the model slicing procedure.
    // If the pad had been added previously (see step "base_pool" than it will
    // be part of the slices)
    auto slice_supports = [ilh](SLAPrintObject& po) {
        auto& sd = po.m_supportdata;
        if(sd && sd->support_tree_ptr) {
            auto lh = float(po.m_config.layer_height.getFloat());
            sd->support_slices = sd->support_tree_ptr->slice(lh, ilh);
        }
    };

    // Rasterizing the model objects, and their supports
    auto rasterize = [this, ilh]() {
        using Layer = sla::ExPolygons;
        using LayerCopies = std::vector<SLAPrintObject::Instance>;
        struct LayerRef {
            std::reference_wrapper<const Layer> lref;
            std::reference_wrapper<const LayerCopies> copies;
            LayerRef(const Layer& lyr, const LayerCopies& cp) :
                lref(std::cref(lyr)), copies(std::cref(cp)) {}
        };

        using LayerRefs = std::vector<LayerRef>;

        // layers according to quantized height levels
        std::map<long long, LayerRefs> levels;

        // For all print objects, go through its initial layers and place them
        // into the layers hash
        for(SLAPrintObject *o : m_objects) {

            double gndlvl = o->transformed_mesh().bounding_box().min(Z);

            double lh = o->m_config.layer_height.getFloat();
            SlicedModel & oslices = o->m_model_slices;
            for(int i = 0; i < oslices.size(); ++i) {
                int a = i == 0 ? 0 : 1;
                int b = i == 0 ? 0 : i - 1;

                double h = gndlvl + ilh * a + b * lh;
                long long lyridx = static_cast<long long>(scale_(h));
                auto& lyrs = levels[lyridx]; // this initializes a new record
                lyrs.emplace_back(oslices[i], o->m_instances);
            }

            if(o->m_supportdata) { // deal with the support slices if present
                auto& sslices = o->m_supportdata->support_slices;
                double el = o->m_config.support_object_elevation.getFloat();
                //TODO: remove next line:
                el = SupportConfig().object_elevation_mm;

                for(int i = 0; i < sslices.size(); ++i) {
                    int a = i == 0 ? 0 : 1;
                    int b = i == 0 ? 0 : i - 1;

                    double h = gndlvl - el + ilh * a + b * lh;

                    long long lyridx = static_cast<long long>(scale_(h));
                    auto& lyrs = levels[lyridx];
                    lyrs.emplace_back(sslices[i], o->m_instances);
                }
            }
        }

        if(canceled()) return;

        // collect all the keys
        std::vector<long long> keys; keys.reserve(levels.size());
        for(auto& e : levels) keys.emplace_back(e.first);

        { // create a raster printer for the current print parameters
            // I don't know any better
            auto& ocfg = m_objects.front()->m_config;
            auto& matcfg = m_material_config;
            auto& printcfg = m_printer_config;

            double w = printcfg.display_width.getFloat();
            double h = printcfg.display_height.getFloat();
            unsigned pw = printcfg.display_pixels_x.getInt();
            unsigned ph = printcfg.display_pixels_y.getInt();
            double lh = ocfg.layer_height.getFloat();
            double exp_t = matcfg.exposure_time.getFloat();
            double iexp_t = matcfg.initial_exposure_time.getFloat();

            m_printer.reset(new SLAPrinter(w, h, pw, ph, lh, exp_t, iexp_t));
        }

        // Allocate space for all the layers
        SLAPrinter& printer = *m_printer;
        auto lvlcnt = unsigned(levels.size());
        printer.layers(lvlcnt);

        // TODO exclusive progress indication for this step would be good
        // as it is the longest of all. It would require synchronization
        // in the parallel processing.

        // procedure to process one height level. This will run in parallel
        auto lvlfn = [this, &keys, &levels, &printer](unsigned level_id) {
            if(canceled()) return;

            LayerRefs& lrange = levels[keys[level_id]];

            // Switch to the appropriate layer in the printer
            printer.begin_layer(level_id);

            for(auto& lyrref : lrange) { // for all layers in the current level
                if(canceled()) break;
                const Layer& sl = lyrref.lref;   // get the layer reference
                const LayerCopies& copies = lyrref.copies;

                // Draw all the polygons in the slice to the actual layer.
                for(auto& cp : copies) {
                    for(ExPolygon slice : sl) {
                        slice.translate(cp.shift(X), cp.shift(Y));
                        slice.rotate(cp.rotation);
                        printer.draw_polygon(slice, level_id);
                    }
                }
            }

            // Finish the layer for later saving it.
            printer.finish_layer(level_id);
        };

        // last minute escape
        if(canceled()) return;

        // Sequential version (for testing)
        // for(unsigned l = 0; l < lvlcnt; ++l) process_level(l);

        // Print all the layers in parallel
        tbb::parallel_for<unsigned, decltype(lvlfn)>(0, lvlcnt, lvlfn);
    };

    using slaposFn = std::function<void(SLAPrintObject&)>;
    using slapsFn  = std::function<void(void)>;

    std::array<SLAPrintObjectStep, slaposCount> objectsteps = {
        slaposObjectSlice,
        slaposSupportIslands,
        slaposSupportPoints,
        slaposSupportTree,
        slaposBasePool,
        slaposSliceSupports
    };

    std::array<slaposFn, slaposCount> pobj_program =
    {
        slice_model,
        [](SLAPrintObject&){}, // slaposSupportIslands now empty
        support_points,
        support_tree,
        base_pool,
        slice_supports
    };

    std::array<slapsFn, slapsCount> print_program =
    {
        rasterize,
        [](){}  // validate
    };

    const unsigned min_objstatus = 0;
    const unsigned max_objstatus = PRINT_STEP_LEVELS[slapsRasterize];
    const size_t objcount = m_objects.size();
    const double ostepd = (max_objstatus - min_objstatus) / (objcount * 100.0);

    for(SLAPrintObject * po : m_objects) {
        for(size_t s = 0; s < pobj_program.size(); ++s) {
            auto currentstep = objectsteps[s];

            // Cancellation checking. Each step will check for cancellation
            // on its own and return earlier gracefully. Just after it returns
            // execution gets to this point and throws the canceled signal.
            throw_if_canceled();

            if(po->m_stepmask[s] && !po->is_step_done(currentstep)) {
                unsigned st = OBJ_STEP_LEVELS[currentstep];
                st = unsigned(min_objstatus + st * ostepd);
                set_status(st, OBJ_STEP_LABELS[currentstep]);

                po->set_started(currentstep);
                pobj_program[s](*po);
                po->set_done(currentstep);
            }
        }
    }

    std::array<SLAPrintStep, slapsCount> printsteps = {
        slapsRasterize, slapsValidate
    };

    // this would disable the rasterization step
//    m_stepmask[slapsRasterize] = false;

    for(size_t s = 0; s < print_program.size(); ++s) {
        auto currentstep = printsteps[s];

        throw_if_canceled();

        if(m_stepmask[s] && !is_step_done(currentstep)) {
            set_status(PRINT_STEP_LEVELS[currentstep],
                       PRINT_STEP_LABELS[currentstep]);

            set_started(currentstep);
            print_program[s]();
            set_done(currentstep);
        }
    }

    // If everything vent well
    set_status(100, L("Slicing done"));
}

SLAPrintObject::SLAPrintObject(SLAPrint *print, ModelObject *model_object):
    Inherited(print, model_object),
    m_stepmask(slaposCount, true)
{
}

SLAPrintObject::~SLAPrintObject() {}

double SLAPrintObject::get_elevation() const {
    return m_supportdata && m_supportdata->support_tree_ptr?
                m_supportdata->support_tree_ptr->get_elevation() :
                0;
}

const std::vector<ExPolygons> &SLAPrintObject::get_support_slices() const
{
    // I don't want to return a copy but the points may not exist, so ...
    static const std::vector<ExPolygons> dummy_empty;

    if(!m_supportdata) return dummy_empty;
    return m_supportdata->support_slices;
}

const std::vector<ExPolygons> &SLAPrintObject::get_model_slices() const
{
    return m_model_slices;
}

bool SLAPrintObject::has_mesh(SLAPrintObjectStep step) const
{
    switch (step) {
    case slaposSupportTree:
//        return m_supportdata && m_supportdata->support_tree_ptr && ! m_supportdata->support_tree_ptr->get().merged_mesh().empty();
		return ! this->support_mesh().empty();
    case slaposBasePool:
//		return m_supportdata && m_supportdata->support_tree_ptr && ! m_supportdata->support_tree_ptr->get_pad().empty();
		return ! this->pad_mesh().empty();
	default:
        return false;
    }
}

TriangleMesh SLAPrintObject::get_mesh(SLAPrintObjectStep step) const
{
	switch (step) {
	case slaposSupportTree:
		return this->support_mesh();
	case slaposBasePool:
		return this->pad_mesh();
	default:
		return TriangleMesh();
	}
}

TriangleMesh SLAPrintObject::support_mesh() const
{
    TriangleMesh trm;

    if(m_supportdata && m_supportdata->support_tree_ptr)
        m_supportdata->support_tree_ptr->merged_mesh(trm);

    // TODO: is this necessary?
    trm.repair();

    return trm;
}

TriangleMesh SLAPrintObject::pad_mesh() const
{
    if(!m_supportdata || !m_supportdata->support_tree_ptr) return {};

    return m_supportdata->support_tree_ptr->get_pad();
}

const TriangleMesh &SLAPrintObject::transformed_mesh() const {
    // we need to transform the raw mesh...
    // currently all the instances share the same x and y rotation and scaling
    // so we have to extract those from e.g. the first instance and apply to the
    // raw mesh. This is also true for the support points.
    // BUT: when the support structure is spawned for each instance than it has
    // to omit the X, Y rotation and scaling as those have been already applied
    // or apply an inverse transformation on the support structure after it
    // has been created.

    if(m_trmesh_valid) return m_transformed_rmesh;
    m_transformed_rmesh = m_model_object->raw_mesh();
    m_transformed_rmesh.transform(m_trafo);
    m_trmesh_valid = true;
    return m_transformed_rmesh;
}

std::vector<Vec3d> SLAPrintObject::transformed_support_points() const
{
    assert(m_model_object != nullptr);
    auto& spts = m_model_object->sla_support_points;

    // this could be cached as well
    std::vector<Vec3d> ret; ret.reserve(spts.size());

    for(auto& sp : spts) ret.emplace_back( trafo() * Vec3d(sp.cast<double>()));

    return ret;
}

} // namespace Slic3r
