#include "SLAPrint.hpp"
#include "GUI.hpp"
#include "GLGizmo.hpp"
#include "Plater.hpp"

#include "GUI_App.hpp"

namespace Slic3r {

const std::string SLAPrint::m_stage_labels[] = {
    "", // IDLE,
    L("Finding best rotation"), // FIND_ROTATION,
    L("Scanning model structure"), // SUPPORT_POINTS,
    L("Generating support tree"), // SUPPORT_TREE,
    L("Generating base pool"), // BASE_POOL,
    L("Slicing model"), // SLICE_MODEL,
    L("Slicing supports"), // SLICE_SUPPORTS,
    L("Exporting layers"), // EXPORT,
    L("Prepared for printing"), // DONE,
    L("SLA print preparation aborted")  // ABORT,
};

void SLAPrint::_start()
{
}

void SLAPrint::_synch() {
    m_gcfg = m_config_reader();
    // TODO: fill PrintObject cache
    m_dirty.store(false);
}

bool SLAPrint::start() {
    if(!m_process) return false;

    m_process->schedule([this](){ _start(); });

    return true;
}

namespace GUI {

void GLGizmoSlaSupports::on_deactivate() {
    std::cout << "gizmo deactivated " << std::endl;
    if(!m_model_object) return;

    SLAPrint& print = wxGetApp().plater()->sla_print();
    print.synch();

    print.start();

//    sla::Controller supportctl;
//    std::cout << "Generating supports:" << std::endl;

//    // TODO: somehow get the global status indicator
//    supportctl.statuscb = [] (unsigned st, const std::string& msg) {
//        std::cout << st << "% "  << msg << std::endl;
//    };

//    TriangleMesh&& m = m_model_object->raw_mesh();
//    m.transform(m_model_object_matrix);
//    auto emesh = sla::to_eigenmesh(m);

//    sla::SupportConfig cfg;
//    sla::PointSet input = sla::support_points(*m_model_object, 0 /*instance*/);

//    sla::SLASupportTree stree(input, emesh, cfg, supportctl);

//    TriangleMesh output;
//    stree.merged_mesh(output);

//    _3DScene::reload_scene(m_parent.get_wxglcanvas(), false);
}
}

}
