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
            ++ap.priority; // need to be included in the result
            m_selected.emplace_back(ap);
        }

    if (m_selected.empty()) return;

    m_bedpts = get_bed_shape(*m_plater->config());

    auto &objects = m_plater->model().objects;
    for (size_t idx = 0; idx < objects.size(); ++idx)
        if (int(idx) != m_object_idx)
            for (ModelInstance *mi : objects[idx]->instances) {
                m_unselected.emplace_back(get_arrange_poly(PtrWrapper{mi}, m_plater));
                m_unselected.back().bed_idx = 0;
            }

    if (auto wt = get_wipe_tower_arrangepoly(*m_plater))
        m_unselected.emplace_back(std::move(*wt));

    double sc = scaled<double>(1.) * scaled(1.);

    ExPolygon poly = m_selected.front().poly;
    double poly_area = poly.area() / sc;
    double unsel_area = std::accumulate(m_unselected.begin(),
                                        m_unselected.end(), 0.,
                                        [](double s, const auto &ap) {
                                            return s + ap.poly.area();
                                        }) / sc;

    double fixed_area = unsel_area + m_selected.size() * poly_area;

    // This is the maximum range, the real number will always be close but less.
    double bed_area = Polygon{m_bedpts}.area() / sc;

    m_status_range = (bed_area - fixed_area) / poly_area;

    ModelInstance *mi = model_object->instances[0];
    for (int i = 0; i < m_status_range; ++i) {
        ArrangePolygon ap;
        ap.poly = m_selected.front().poly;
        ap.bed_idx = arrangement::UNARRANGED;
        ap.setter = [this, mi](const ArrangePolygon &p) {
            ModelObject *mo = m_plater->model().objects[m_object_idx];
            ModelInstance *inst = mo->add_instance(*mi);
            inst->apply_arrange_result(p.translation.cast<double>(), p.rotation);
        };
        m_selected.emplace_back(ap);
    }
}

void FillBedJob::process()
{
    if (m_object_idx == -1 || m_selected.empty()) return;

    GLCanvas3D::ArrangeSettings settings =
        m_plater->canvas3D()->get_arrange_settings();

    arrangement::ArrangeParams params;
    params.allow_rotations  = settings.enable_rotation;
    params.min_obj_distance = m_plater->config()->opt_bool("complete_objects") ?
                                  scaled(settings.distance_seq_print) :
                                  scaled(settings.distance);

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
