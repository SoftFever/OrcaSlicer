#ifndef ORIENTJOB_HPP
#define ORIENTJOB_HPP

#include "PlaterJob.hpp"
#include "libslic3r/Orient.hpp"

namespace Slic3r {

class ModelObject;

namespace GUI {

class OrientJob : public PlaterJob
{
    using OrientMesh = orientation::OrientMesh;
    using OrientMeshs = orientation::OrientMeshs;

    OrientMeshs m_selected, m_unselected, m_unprintable;

    // clear m_selected and m_unselected, reserve space for next usage
    void clear_input();

    //BBS: add only one plate mode
    void prepare_selection(std::vector<bool> obj_sel, bool only_one_plate);
    
    // Prepare the selected and unselected items separately. If nothing is
    // selected, behaves as if everything would be selected.
    void prepare_selected();

    //BBS:prepare the items from current selected partplate
    void prepare_partplate();

protected:
    void prepare() override;
    void on_exception(const std::exception_ptr &) override;
    
public:
    OrientJob(std::shared_ptr<ProgressIndicator> pri, Plater *plater)
        : PlaterJob{std::move(pri), plater}
    {}    
    
    void process() override;
    
    void finalize() override;
#if 0
    static
    orientation::OrientMesh get_orient_mesh(ModelObject* obj, const Plater* plater)
    {
        using OrientMesh = orientation::OrientMesh;
        OrientMesh om;
        om.name = obj->name;
        om.mesh = obj->mesh(); // don't know the difference to obj->raw_mesh(). Both seem OK
        om.setter = [obj, plater](const OrientMesh& p) {
            obj->rotate(p.angle, p.axis);
            obj->ensure_on_bed();
        };
        return om;
    }
#endif
    static orientation::OrientMesh get_orient_mesh(ModelInstance* instance);
};


}} // namespace Slic3r::GUI

#endif // ORIENTJOB_HPP
