#include "SLAPrint.hpp"
#include "SLA/SLASupportTree.hpp"

#include "I18N.hpp"

//! macro used to mark string used at localization,
//! return same string
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

class SLAPrintObject::SupportData {
public:
    sla::EigenMesh3D emesh;             // index-triangle representation
    sla::PointSet support_points;       // all the support points (manual/auto)
    std::unique_ptr<sla::SLASupportTree> support_tree_ptr; // the supports
    SlicedSupports slice_cache;         // sliced supports
};

namespace {

const std::array<unsigned, slaposCount>     OBJ_STEP_LEVELS =
{
    20,
    30,
    50,
    70,
    80,
    100
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
    50,     // slapsRasterize
    100,    // slapsValidate
};

const std::array<std::string, slapsCount> PRINT_STEP_LABELS =
{
    L("Rasterizing layers"),         // slapsRasterize
    L("Validating"),                 // slapsValidate
};

}

void SLAPrint::clear()
{
	tbb::mutex::scoped_lock lock(this->cancel_mutex());
    // The following call should stop background processing if it is running.
    this->invalidate_all_steps();

    for (SLAPrintObject *object : m_objects) delete object;
	m_objects.clear();
}

SLAPrint::ApplyStatus SLAPrint::apply(const Model &model,
                                      const DynamicPrintConfig &config)
{
	if (m_objects.empty())
		return APPLY_STATUS_UNCHANGED;

    // Grab the lock for the Print / PrintObject milestones.
	tbb::mutex::scoped_lock lock(this->cancel_mutex());

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
       		//TODO
            m_objects.emplace_back(new SLAPrintObject(this, model_object));
        }
	}

	return APPLY_STATUS_INVALIDATED;
}

void SLAPrint::process()
{
    using namespace sla;

    std::cout << "SLA Processing triggered" << std::endl;

    // Assumption: at this point the print objects should be populated only with
    // the model objects we have to process and the instances are also filtered

    auto slice_model = [](const SLAPrintObject&) {


    };

    auto support_points = [](const SLAPrintObject&) {
        // for(SLAPrintObject *po : pobjects) {
            // TODO: calculate automatic support points
            // po->m_supportdata->slice_cache contains the slices at this point
        //}
    };

    auto support_tree = [this](const SLAPrintObject& po) {
        auto& emesh = po.m_supportdata->emesh;
        auto& pts = po.m_supportdata->support_points; // nowhere filled yet
        auto& supportd = *po.m_supportdata;
        try {
            SupportConfig scfg;  //  TODO fill or replace with po.m_config

            sla::Controller ctl;
            ctl.statuscb = [this](unsigned st, const std::string& msg) {
                unsigned stinit = OBJ_STEP_LEVELS[slaposSupportTree];
                double d = (OBJ_STEP_LEVELS[slaposBasePool] - stinit) / 100.0;
                set_status(unsigned(stinit + st*d), msg);
            };
            ctl.stopcondition = [this](){ return canceled(); };

            supportd.support_tree_ptr.reset(
                        new SLASupportTree(pts, emesh, scfg, ctl));

        } catch(sla::SLASupportsStoppedException&) {
            // no need to rethrow
            // throw_if_canceled();
        }
    };

    auto base_pool = [](const SLAPrintObject&) {

    };

    auto slice_supports = [](const SLAPrintObject&) {

    };

    auto rasterize = []() {

    };

    using slaposFn = std::function<void(const SLAPrintObject&)>;

    std::array<SLAPrintObjectStep, slaposCount> objectsteps = {
        slaposObjectSlice,
        slaposSupportIslands,
        slaposSupportPoints,
        slaposSupportTree,
        slaposBasePool,
        slaposSliceSupports
    };

    std::array<slaposFn, slaposCount> fullprogram =
    {
        slice_model,
        [](const SLAPrintObject&){}, // slaposSupportIslands now empty
        support_points,
        support_tree,
        base_pool,
        slice_supports
    };

    for(SLAPrintObject * po : m_objects) {
        for(size_t s = 0; s < fullprogram.size(); ++s) {
            auto currentstep = objectsteps[s];

            // Cancellation checking. Each step will check for cancellation
            // on its own and return earlier gracefully. Just after it returns
            // execution gets to this point and throws the canceled signal.
            throw_if_canceled();

            if(po->m_stepmask[s]) {
                set_status(OBJ_STEP_LEVELS[currentstep],
                           OBJ_STEP_LABELS[currentstep]);

                po->set_started(currentstep);
                fullprogram[s](*po);
                po->set_done(currentstep);
            }
        }
    }
}

SLAPrintObject::SLAPrintObject(SLAPrint *print, ModelObject *model_object):
    Inherited(print),
    m_model_object(model_object),
    m_supportdata(new SupportData()),
    m_stepmask(slaposCount, true)
{
    m_supportdata->emesh = sla::to_eigenmesh(*m_model_object);
}

SLAPrintObject::~SLAPrintObject() {}

} // namespace Slic3r
