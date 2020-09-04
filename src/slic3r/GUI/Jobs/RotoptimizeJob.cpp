#include "RotoptimizeJob.hpp"

#include "libslic3r/MTUtils.hpp"
#include "libslic3r/SLA/Rotfinder.hpp"
#include "libslic3r/MinAreaBoundingBox.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/SLAPrint.hpp"

#include "slic3r/GUI/Plater.hpp"

namespace Slic3r { namespace GUI {

void RotoptimizeJob::process()
{
    int obj_idx = m_plater->get_selected_object_idx();
    if (obj_idx < 0) { return; }
    
    ModelObject *o = m_plater->model().objects[size_t(obj_idx)];
    const SLAPrintObject *po = m_plater->sla_print().objects()[size_t(obj_idx)];

    if (!o || !po) return;

    TriangleMesh mesh = o->raw_mesh();
    mesh.require_shared_vertices();

//    for (auto inst : o->instances) {
//        Transform3d tr = Transform3d::Identity();
//        tr.rotate(Eigen::AngleAxisd(inst->get_rotation(Z), Vec3d::UnitZ()));
//        tr.rotate(Eigen::AngleAxisd(inst->get_rotation(Y), Vec3d::UnitY()));
//        tr.rotate(Eigen::AngleAxisd(inst->get_rotation(X), Vec3d::UnitX()));

//        double score = sla::get_model_supportedness(*po, tr);

//        std::cout << "Model supportedness before: " << score << std::endl;
//    }

    auto r = sla::find_best_rotation(
        *po,
        1.f,
        [this](unsigned s) {
            if (s < 100)
                update_status(int(s),
                              _(L("Searching for optimal orientation")));
        },
        [this]() { return was_canceled(); });


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
