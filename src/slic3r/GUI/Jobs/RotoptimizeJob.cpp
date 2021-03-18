#include "RotoptimizeJob.hpp"

#include "libslic3r/MTUtils.hpp"
#include "libslic3r/SLA/Rotfinder.hpp"
#include "libslic3r/MinAreaBoundingBox.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/SLAPrint.hpp"

#include "slic3r/GUI/Plater.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "libslic3r/AppConfig.hpp"

namespace Slic3r { namespace GUI {

void RotoptimizeJob::prepare()
{
    std::string accuracy_str =
        wxGetApp().app_config->get("rotoptimize", "accuracy");

    std::string method_str =
        wxGetApp().app_config->get("rotoptimize", "method_id");

    if (!accuracy_str.empty())
        m_accuracy = std::stof(accuracy_str);

    if (!method_str.empty())
        m_method_id = std::stoi(method_str);

    m_accuracy = std::max(0.f, std::min(m_accuracy, 1.f));
    m_method_id = std::max(size_t(0), std::min(get_methods_count() - 1, m_method_id));
}

void RotoptimizeJob::process()
{
    int obj_idx = m_plater->get_selected_object_idx();
    if (obj_idx < 0 || int(m_plater->sla_print().objects().size()) <= obj_idx)
        return;
    
    ModelObject *o = m_plater->model().objects[size_t(obj_idx)];
    const SLAPrintObject *po = m_plater->sla_print().objects()[size_t(obj_idx)];

    if (!o || !po) return;

    Vec2d r = Methods[m_method_id].findfn(*po, m_accuracy, [this](int s) {
        if (s > 0 && s < 100)
            update_status(s, _(L("Searching for optimal orientation")));

        return !was_canceled();
    });

    double mindist = 6.0; // FIXME

    if (!was_canceled()) {
        for(ModelInstance * oi : o->instances) {
            oi->set_rotation({r[X], r[Y], 0.});

            auto    trmatrix = oi->get_transformation().get_matrix();
            Polygon trchull  = o->convex_hull_2d(trmatrix);

            MinAreaBoundigBox rotbb(trchull, MinAreaBoundigBox::pcConvex);
            double            phi = rotbb.angle_to_X();

            // The box should be landscape
            if(rotbb.width() < rotbb.height()) phi += PI / 2;

            Vec3d rt = oi->get_rotation(); rt(Z) += phi;

            oi->set_rotation(rt);
        }

        m_plater->find_new_position(o->instances, scaled(mindist));

        // Correct the z offset of the object which was corrupted be
        // the rotation
        o->ensure_on_bed();
    }

    update_status(100, was_canceled() ? _(L("Orientation search canceled.")) :
                                        _(L("Orientation found.")));
}

void RotoptimizeJob::finalize()
{
    if (!was_canceled())
        m_plater->update();
    
    Job::finalize();
}

}}
