#ifndef slic3r_SLAPrint_hpp_
#define slic3r_SLAPrint_hpp_

#include "PrintBase.hpp"
#include "Point.hpp"
//#include "SLA/SLASupportTree.hpp"

namespace Slic3r {

enum SLAPrintStep {
//	slapsSliceModel,
//	slapsSliceSupports,
	slapsRasterize,
	slapsCount
};

enum SLAPrintObjectStep {
	slaposSupportPoints,
	slaposSupportTree,
	slaposBasePool,
	slaposCount
};

class SLAPrint;

class SLAPrintObject : public PrintObjectBaseWithState<Print, SLAPrintObjectStep, slaposCount>
{
private: // Prevents erroneous use by other classes.
    typedef PrintObjectBaseWithState<Print, SLAPrintObjectStep, slaposCount> Inherited;

public:

private:
//    sla::EigenMesh3D emesh;
    std::vector<Vec2f> instances;
//    Transform3f tr;
//    std::unique_ptr<sla::SLASupportTree> support_tree_ptr;
//    SlicedSupports slice_cache;

	friend SLAPrint;
};

/**
 * @brief This class is the high level FSM for the SLA printing process.
 *
 * It should support the background processing framework and contain the
 * metadata for the support geometries and their slicing. It should also
 * dispatch the SLA printing configuration values to the appropriate calculation
 * steps.
 *
 * TODO (decide): The last important feature is the support for visualization
 * which (at least for now) will be implemented as a method(s) returning the
 * triangle meshes or receiving the rendering canvas and drawing on that
 * directly.
 *
 * TODO: This class uses the BackgroundProcess interface to create workers and
 * manage input change events. An appropriate implementation can be derived
 * from BackgroundSlicingProcess which is now working only with the FDM Print.
 */
class SLAPrint : public PrintBaseWithState<SLAPrintStep, slapsCount>
{
private: // Prevents erroneous use by other classes.
    typedef PrintBaseWithState<SLAPrintStep, slapsCount> Inherited;

public:
    SLAPrint() {}
	virtual ~SLAPrint() { this->clear(); }

	PrinterTechnology	technology() const noexcept { return ptSLA; }

    void                clear() override;
    bool                empty() const override { return false; }
    ApplyStatus         apply(const Model &model, const DynamicPrintConfig &config) override;
    void                process() override;

private:
    Model                           m_model;
    SLAPrinterConfig                m_printer_config;
    SLAMaterialConfig               m_material_config;

	std::vector<SLAPrintObject*>	m_objects;

	friend SLAPrintObject;
};

} // namespace Slic3r

#endif /* slic3r_SLAPrint_hpp_ */
