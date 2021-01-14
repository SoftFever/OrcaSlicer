#include "FillBedJob.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/ClipperUtils.hpp"

#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"

#include <numeric>

namespace Slic3r {
namespace GUI {

void FillBedJob::prepare()
{
    m_selected.clear();
    m_unselected.clear();
    m_bedpts.clear();

    m_object_idx = m_plater->get_selected_object_idx();
    if (m_object_idx == -1)
        return;

    ModelObject *model_object = m_plater->model().objects[m_object_idx];
    if (model_object->instances.empty()) return;

    m_selected.reserve(model_object->instances.size());
    for (ModelInstance *inst : model_object->instances)
        if (inst->printable) {
            ArrangePolygon ap = get_arrange_poly(PtrWrapper{inst}, m_plater);
            // Existing objects need to be included in the result. Only
            // the needed amount of object will be added, no more.
            ++ap.priority;
            m_selected.emplace_back(ap);
        }

    if (m_selected.empty()) return;

    m_bedpts = get_bed_shape(*m_plater->config());

    auto &objects = m_plater->model().objects;
    BoundingBox bedbb = get_extents(m_bedpts);

    for (size_t idx = 0; idx < objects.size(); ++idx)
        if (int(idx) != m_object_idx)
            for (ModelInstance *mi : objects[idx]->instances) {
                ArrangePolygon ap = get_arrange_poly(PtrWrapper{mi}, m_plater);
                auto ap_bb = ap.transformed_poly().contour.bounding_box();

                if (ap.bed_idx == 0 && !bedbb.contains(ap_bb))
                    ap.bed_idx = arrangement::UNARRANGED;

                m_unselected.emplace_back(ap);
            }

    if (auto wt = get_wipe_tower_arrangepoly(*m_plater))
        m_unselected.emplace_back(std::move(*wt));

    double sc = scaled<double>(1.) * scaled(1.);

    ExPolygon poly = m_selected.front().poly;
    double poly_area = poly.area() / sc;
    double unsel_area = std::accumulate(m_unselected.begin(),
                                        m_unselected.end(), 0.,
                                        [](double s, const auto &ap) {
                                            return s + (ap.bed_idx == 0) * ap.poly.area();
                                        }) / sc;

    double fixed_area = unsel_area + m_selected.size() * poly_area;
    double bed_area   = Polygon{m_bedpts}.area() / sc;

    // This is the maximum number of items, the real number will always be close but less.
    int needed_items = (bed_area - fixed_area) / poly_area;

    int sel_id = m_plater->get_selection().get_instance_idx();
    // if the selection is not a single instance, choose the first as template
    sel_id = std::max(sel_id, 0);
    ModelInstance *mi = model_object->instances[sel_id];
    ArrangePolygon template_ap = get_arrange_poly(PtrWrapper{mi}, m_plater);

    for (int i = 0; i < needed_items; ++i) {
        ArrangePolygon ap = template_ap;
        ap.poly = m_selected.front().poly;
        ap.bed_idx = arrangement::UNARRANGED;
        ap.setter = [this, mi](const ArrangePolygon &p) {
            ModelObject *mo = m_plater->model().objects[m_object_idx];
            ModelInstance *inst = mo->add_instance(*mi);
            inst->apply_arrange_result(p.translation.cast<double>(), p.rotation);
        };
        m_selected.emplace_back(ap);
    }

    m_status_range = m_selected.size();

    // The strides have to be removed from the fixed items. For the
    // arrangeable (selected) items bed_idx is ignored and the
    // translation is irrelevant.
    double stride = bed_stride(m_plater);
    for (auto &p : m_unselected)
        if (p.bed_idx > 0)
            p.translation(X) -= p.bed_idx * stride;
}

void FillBedJob::process()
{
    if (m_object_idx == -1 || m_selected.empty()) return;

    const GLCanvas3D::ArrangeSettings &settings =
        static_cast<const GLCanvas3D*>(m_plater->canvas3D())->get_arrange_settings();

    arrangement::ArrangeParams params;
    params.allow_rotations  = settings.enable_rotation;
    params.min_obj_distance = scaled(settings.distance);

    bool do_stop = false;
    params.stopcondition = [this, &do_stop]() {
        return was_canceled() || do_stop;
    };

    params.progressind = [this](unsigned st) {
        if (st > 0)
            update_status(int(m_status_range - st), _(L("Filling bed")));
    };

    params.on_packed = [&do_stop] (const ArrangePolygon &ap) {
        do_stop = ap.bed_idx > 0 && ap.priority == 0;
    };

    arrangement::arrange(m_selected, m_unselected, m_bedpts, params);

    // finalize just here.
    update_status(m_status_range, was_canceled() ?
                                      _(L("Bed filling canceled.")) :
                                      _(L("Bed filling done.")));
}

void FillBedJob::finalize()
{
    // Ignore the arrange result if aborted.
    if (was_canceled()) return;

    if (m_object_idx == -1) return;

    ModelObject *model_object = m_plater->model().objects[m_object_idx];
    if (model_object->instances.empty()) return;

    size_t inst_cnt = model_object->instances.size();

    for (ArrangePolygon &ap : m_selected) {
        if (ap.bed_idx != arrangement::UNARRANGED && (ap.priority != 0 || ap.bed_idx == 0))
            ap.apply();
    }

    model_object->ensure_on_bed();

    m_plater->update();

    int added_cnt = std::accumulate(m_selected.begin(), m_selected.end(), 0,
                                     [](int s, auto &ap) {
                                         return s + int(ap.priority == 0 && ap.bed_idx == 0);
                                     });

    // FIXME: somebody explain why this is needed for increase_object_instances
    if (inst_cnt == 1) added_cnt++;

    if (added_cnt > 0)
        m_plater->sidebar()
            .obj_list()->increase_object_instances(m_object_idx, size_t(added_cnt));
}

}} // namespace Slic3r::GUI
