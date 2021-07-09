#include "ArrangeJob.hpp"

#include "libslic3r/MTUtils.hpp"
#include "libslic3r/Model.hpp"

#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectManipulation.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/format.hpp"

#include "libnest2d/common.hpp"

namespace Slic3r { namespace GUI {

// Cache the wti info
class WipeTower: public GLCanvas3D::WipeTowerInfo {
    using ArrangePolygon = arrangement::ArrangePolygon;
public:
    explicit WipeTower(const GLCanvas3D::WipeTowerInfo &wti)
        : GLCanvas3D::WipeTowerInfo(wti)
    {}
    
    explicit WipeTower(GLCanvas3D::WipeTowerInfo &&wti)
        : GLCanvas3D::WipeTowerInfo(std::move(wti))
    {}

    void apply_arrange_result(const Vec2d& tr, double rotation)
    {
        m_pos = unscaled(tr); m_rotation = rotation;
        apply_wipe_tower();
    }
    
    ArrangePolygon get_arrange_polygon() const
    {
        Polygon ap({
            {scaled(m_bb.min)},
            {scaled(m_bb.max.x()), scaled(m_bb.min.y())},
            {scaled(m_bb.max)},
            {scaled(m_bb.min.x()), scaled(m_bb.max.y())}
            });
        
        ArrangePolygon ret;
        ret.poly.contour = std::move(ap);
        ret.translation  = scaled(m_pos);
        ret.rotation     = m_rotation;
        ++ret.priority;

        return ret;
    }
};

static WipeTower get_wipe_tower(const Plater &plater)
{
    return WipeTower{plater.canvas3D()->get_wipe_tower_info()};
}

void ArrangeJob::clear_input()
{
    const Model &model = m_plater->model();
    
    size_t count = 0, cunprint = 0; // To know how much space to reserve
    for (auto obj : model.objects)
        for (auto mi : obj->instances)
            mi->printable ? count++ : cunprint++;
    
    m_selected.clear();
    m_unselected.clear();
    m_unprintable.clear();
    m_unarranged.clear();
    m_selected.reserve(count + 1 /* for optional wti */);
    m_unselected.reserve(count + 1 /* for optional wti */);
    m_unprintable.reserve(cunprint /* for optional wti */);
}

void ArrangeJob::prepare_all() {
    clear_input();
    
    for (ModelObject *obj: m_plater->model().objects)
        for (ModelInstance *mi : obj->instances) {
            ArrangePolygons & cont = mi->printable ? m_selected : m_unprintable;
            cont.emplace_back(get_arrange_poly_(mi));
        }

    if (auto wti = get_wipe_tower_arrangepoly(*m_plater))
        m_selected.emplace_back(std::move(*wti));
}

void ArrangeJob::prepare_selected() {
    clear_input();
    
    Model &model = m_plater->model();
    double stride = bed_stride(m_plater);
    
    std::vector<const Selection::InstanceIdxsList *>
            obj_sel(model.objects.size(), nullptr);
    
    for (auto &s : m_plater->get_selection().get_content())
        if (s.first < int(obj_sel.size()))
            obj_sel[size_t(s.first)] = &s.second;
    
    // Go through the objects and check if inside the selection
    for (size_t oidx = 0; oidx < model.objects.size(); ++oidx) {
        const Selection::InstanceIdxsList * instlist = obj_sel[oidx];
        ModelObject *mo = model.objects[oidx];
        
        std::vector<bool> inst_sel(mo->instances.size(), false);
        
        if (instlist)
            for (auto inst_id : *instlist)
                inst_sel[size_t(inst_id)] = true;
        
        for (size_t i = 0; i < inst_sel.size(); ++i) {
            ModelInstance * mi = mo->instances[i];
            ArrangePolygon &&ap = get_arrange_poly_(mi);

            ArrangePolygons &cont = mo->instances[i]->printable ?
                        (inst_sel[i] ? m_selected :
                                       m_unselected) :
                        m_unprintable;
            
            cont.emplace_back(std::move(ap));
        }
    }
    
    if (auto wti = get_wipe_tower(*m_plater)) {
        ArrangePolygon &&ap = get_arrange_poly(wti, m_plater);

        auto &cont = m_plater->get_selection().is_wipe_tower() ? m_selected :
                                                                 m_unselected;
        cont.emplace_back(std::move(ap));
    }
    
    // If the selection was empty arrange everything
    if (m_selected.empty()) m_selected.swap(m_unselected);
    
    // The strides have to be removed from the fixed items. For the
    // arrangeable (selected) items bed_idx is ignored and the
    // translation is irrelevant.
    for (auto &p : m_unselected) p.translation(X) -= p.bed_idx * stride;
}

arrangement::ArrangePolygon ArrangeJob::get_arrange_poly_(ModelInstance *mi)
{
    arrangement::ArrangePolygon ap = get_arrange_poly(mi, m_plater);

    auto setter = ap.setter;
    ap.setter = [this, setter, mi](const arrangement::ArrangePolygon &set_ap) {
        setter(set_ap);
        if (!set_ap.is_arranged())
            m_unarranged.emplace_back(mi);
    };

    return ap;
}

void ArrangeJob::prepare()
{
    wxGetKeyState(WXK_SHIFT) ? prepare_selected() : prepare_all();
}

void ArrangeJob::on_exception(const std::exception_ptr &eptr)
{
    try {
        if (eptr)
            std::rethrow_exception(eptr);
    } catch (libnest2d::GeometryException &) {
        show_error(m_plater, _(L("Could not arrange model objects! "
                                 "Some geometries may be invalid.")));
    } catch (std::exception &) {
        PlaterJob::on_exception(eptr);
    }
}

void ArrangeJob::process()
{
    static const auto arrangestr = _(L("Arranging"));

    arrangement::ArrangeParams params = get_arrange_params(m_plater);

    auto count = unsigned(m_selected.size() + m_unprintable.size());
    Points bedpts = get_bed_shape(*m_plater->config());
    
    params.stopcondition = [this]() { return was_canceled(); };
    
    params.progressind = [this, count](unsigned st) {
        st += m_unprintable.size();
        if (st > 0) update_status(int(count - st), arrangestr);
    };

    arrangement::arrange(m_selected, m_unselected, bedpts, params);

    params.progressind = [this, count](unsigned st) {
        if (st > 0) update_status(int(count - st), arrangestr);
    };

    arrangement::arrange(m_unprintable, {}, bedpts, params);

    // finalize just here.
    update_status(int(count),
                  was_canceled() ? _(L("Arranging canceled."))
                                   : _(L("Arranging done.")));
}

static std::string concat_strings(const std::set<std::string> &strings,
                                  const std::string &delim = "\n")
{
    return std::accumulate(
        strings.begin(), strings.end(), std::string(""),
        [delim](const std::string &s, const std::string &name) {
            return s + name + delim;
        });
}

void ArrangeJob::finalize() {
    // Ignore the arrange result if aborted.
    if (was_canceled()) return;
    
    // Unprintable items go to the last virtual bed
    int beds = 0;
    
    // Apply the arrange result to all selected objects
    for (ArrangePolygon &ap : m_selected) {
        beds = std::max(ap.bed_idx, beds);
        ap.apply();
    }
    
    // Get the virtual beds from the unselected items
    for (ArrangePolygon &ap : m_unselected)
        beds = std::max(ap.bed_idx, beds);
    
    // Move the unprintable items to the last virtual bed.
    for (ArrangePolygon &ap : m_unprintable) {
        ap.bed_idx += beds + 1;
        ap.apply();
    }

    m_plater->update();
    wxGetApp().obj_manipul()->set_dirty();

    if (!m_unarranged.empty()) {
        std::set<std::string> names;
        for (ModelInstance *mi : m_unarranged)
            names.insert(mi->get_object()->name);

        m_plater->get_notification_manager()->push_notification(GUI::format(
            _L("Arrangement ignored the following objects which can't fit into a single bed:\n%s"),
            concat_strings(names, "\n")));
    }

    Job::finalize();
}

std::optional<arrangement::ArrangePolygon>
get_wipe_tower_arrangepoly(const Plater &plater)
{
    if (auto wti = get_wipe_tower(plater))
        return get_arrange_poly(wti, &plater);

    return {};
}

double bed_stride(const Plater *plater) {
    double bedwidth = plater->bed_shape_bb().size().x();
    return scaled<double>((1. + LOGICAL_BED_GAP) * bedwidth);
}

template<>
arrangement::ArrangePolygon get_arrange_poly(ModelInstance *inst,
                                             const Plater * plater)
{
    return get_arrange_poly(PtrWrapper{inst}, plater);
}

arrangement::ArrangeParams get_arrange_params(Plater *p)
{
    const GLCanvas3D::ArrangeSettings &settings =
        static_cast<const GLCanvas3D*>(p->canvas3D())->get_arrange_settings();

    arrangement::ArrangeParams params;
    params.allow_rotations  = settings.enable_rotation;
    params.min_obj_distance = scaled(settings.distance);

    return params;
}

}} // namespace Slic3r::GUI
