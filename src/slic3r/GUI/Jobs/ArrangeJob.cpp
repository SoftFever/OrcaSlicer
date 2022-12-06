#include "ArrangeJob.hpp"

#include "libslic3r/BuildVolume.hpp"
#include "libslic3r/SVG.hpp"
#include "libslic3r/MTUtils.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/ModelArrange.hpp"

#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"

#include "libnest2d/common.hpp"

#define SAVE_ARRANGE_POLY 0

namespace Slic3r { namespace GUI {
    using ArrangePolygon = arrangement::ArrangePolygon;

// Cache the wti info
class WipeTower: public GLCanvas3D::WipeTowerInfo {
public:
    explicit WipeTower(const GLCanvas3D::WipeTowerInfo &wti)
        : GLCanvas3D::WipeTowerInfo(wti)
    {}

    explicit WipeTower(GLCanvas3D::WipeTowerInfo &&wti)
        : GLCanvas3D::WipeTowerInfo(std::move(wti))
    {}

    void apply_arrange_result(const Vec2d& tr, double rotation, int item_id)
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
        //BBS
        ret.name = "WipeTower";
        ret.is_virt_object = true;
        ret.is_wipe_tower = true;
        ++ret.priority;

        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " arrange: wipe tower info:" << m_bb << ", m_pos: " << m_pos.transpose();

        return ret;
    }
};

// BBS: add partplate logic
static WipeTower get_wipe_tower(const Plater &plater, int plate_idx)
{
    return WipeTower{plater.canvas3D()->get_wipe_tower_info(plate_idx)};
}

arrangement::ArrangePolygon get_wipetower_arrange_poly(WipeTower* tower)
{
    ArrangePolygon ap = tower->get_arrange_polygon();
    ap.bed_idx = 0;
    ap.setter = NULL; // do not move wipe tower
    return ap;
}

void ArrangeJob::clear_input()
{
    const Model &model = m_plater->model();

    size_t count = 0, cunprint = 0; // To know how much space to reserve
    for (auto obj : model.objects)
        for (auto mi : obj->instances)
            mi->printable ? count++ : cunprint++;

    params.nonprefered_regions.clear();
    m_selected.clear();
    m_unselected.clear();
    m_unprintable.clear();
    m_locked.clear();
    m_unarranged.clear();
    m_selected.reserve(count + 1 /* for optional wti */);
    m_unselected.reserve(count + 1 /* for optional wti */);
    m_unprintable.reserve(cunprint /* for optional wti */);
    m_locked.reserve(count + 1 /* for optional wti */);
    current_plate_index = 0;
}

ArrangePolygon ArrangeJob::prepare_arrange_polygon(void* model_instance)
{
    ModelInstance* instance = (ModelInstance*)model_instance;
    const Slic3r::DynamicPrintConfig& config = wxGetApp().preset_bundle->full_config();
    return get_instance_arrange_poly(instance, config);
}

void ArrangeJob::prepare_selected() {
    PartPlateList& plate_list = m_plater->get_partplate_list();

    clear_input();

    Model& model = m_plater->model();
    bool selected_is_locked = false;
    //BBS: remove logic for unselected object
    //double stride = bed_stride_x(m_plater);

    std::vector<const Selection::InstanceIdxsList*>
        obj_sel(model.objects.size(), nullptr);

    for (auto& s : m_plater->get_selection().get_content())
        if (s.first < int(obj_sel.size()))
            obj_sel[size_t(s.first)] = &s.second;

    // Go through the objects and check if inside the selection
    for (size_t oidx = 0; oidx < model.objects.size(); ++oidx) {
        const Selection::InstanceIdxsList* instlist = obj_sel[oidx];
        ModelObject* mo = model.objects[oidx];

        std::vector<bool> inst_sel(mo->instances.size(), false);

        if (instlist)
            for (auto inst_id : *instlist)
                inst_sel[size_t(inst_id)] = true;

        for (size_t i = 0; i < inst_sel.size(); ++i) {
            ModelInstance* mi = mo->instances[i];
            ArrangePolygon&& ap = prepare_arrange_polygon(mo->instances[i]);
            //BBS: partplate_list preprocess
            //remove the locked plate's instances, neither in selected, nor in un-selected
            bool locked = plate_list.preprocess_arrange_polygon(oidx, i, ap, inst_sel[i]);
            if (!locked)
                {
                ArrangePolygons& cont = mo->instances[i]->printable ?
                    (inst_sel[i] ? m_selected :
                        m_unselected) :
                    m_unprintable;

                ap.itemid = cont.size();
                cont.emplace_back(std::move(ap));
                }
            else
                {
                //skip this object due to be locked in plate
                ap.itemid = m_locked.size();
                m_locked.emplace_back(std::move(ap));
                if (inst_sel[i])
                    selected_is_locked = true;
                BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": skip locked instance, obj_id %1%, instance_id %2%, name %3%") % oidx % i % mo->name;
                }
            }
        }


    // If the selection was empty arrange everything
    //if (m_selected.empty()) m_selected.swap(m_unselected);
    if (m_selected.empty()) {
        if (!selected_is_locked)
            m_selected.swap(m_unselected);
        else {
            m_plater->get_notification_manager()->push_notification(NotificationType::BBLPlateInfo,
                NotificationManager::NotificationLevel::WarningNotificationLevel, into_u8(_L("All the selected objects are on the locked plate,\nWe can not do auto-arrange on these objects.")));
            }
        }

    prepare_wipe_tower();


    // The strides have to be removed from the fixed items. For the
    // arrangeable (selected) items bed_idx is ignored and the
    // translation is irrelevant.
    //BBS: remove logic for unselected object
    //for (auto &p : m_unselected) p.translation(X) -= p.bed_idx * stride;
}

void ArrangeJob::prepare_all() {
    PartPlateList& plate_list = m_plater->get_partplate_list();

    clear_input();

    Model &model = m_plater->model();
    bool selected_is_locked = false;

    // Go through the objects and check if inside the selection
    for (size_t oidx = 0; oidx < model.objects.size(); ++oidx) {
        ModelObject *mo = model.objects[oidx];

        for (size_t i = 0; i < mo->instances.size(); ++i) {
            ModelInstance * mi = mo->instances[i];
            ArrangePolygon&& ap = prepare_arrange_polygon(mo->instances[i]);
            //BBS: partplate_list preprocess
            //remove the locked plate's instances, neither in selected, nor in un-selected
            bool locked = plate_list.preprocess_arrange_polygon(oidx, i, ap, true);
            if (!locked)
            {
                ArrangePolygons& cont = mo->instances[i]->printable ? m_selected :m_unprintable;

                ap.itemid = cont.size();
                cont.emplace_back(std::move(ap));
            }
            else
            {
                //skip this object due to be locked in plate
                ap.itemid = m_locked.size();
                m_locked.emplace_back(std::move(ap));
                selected_is_locked = true;
                BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": skip locked instance, obj_id %1%, instance_id %2%") % oidx % i;
            }
        }
    }


    // If the selection was empty arrange everything
    //if (m_selected.empty()) m_selected.swap(m_unselected);
    if (m_selected.empty()) {
        if (!selected_is_locked) {
            m_plater->get_notification_manager()->push_notification(NotificationType::BBLPlateInfo,
                NotificationManager::NotificationLevel::WarningNotificationLevel, into_u8(_L("No arrangable objects are selected.")));
        }
        else {
            m_plater->get_notification_manager()->push_notification(NotificationType::BBLPlateInfo,
                NotificationManager::NotificationLevel::WarningNotificationLevel, into_u8(_L("All the selected objects are on the locked plate,\nWe can not do auto-arrange on these objects.")));
        }
    }

    prepare_wipe_tower();

    // add the virtual object into unselect list if has
    plate_list.preprocess_exclude_areas(m_unselected, MAX_NUM_PLATES);
}

// 准备料塔。逻辑如下：
// 1. 如果料塔被禁用，或是逐件打印，则不需要料塔
// 2. 以下两种情况需要料塔：1）某对象是多色对象；2）打开了支撑，且支撑体与接触面使用的是不同材料
// 3. 如果允许不同材料落在相同盘，则以下情况也需要料塔：1）所有选定对象中使用了多种热床温度相同的材料（比如颜色不同的PLA）
void ArrangeJob::prepare_wipe_tower()
{
    bool need_wipe_tower = false;

    // if wipe tower is explicitly disabled, no need to estimate
    DynamicPrintConfig &current_config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    auto                op             = current_config.option("enable_prime_tower");
    if (op && op->getBool() == false || params.is_seq_print) return;

    // estimate if we need wipe tower for all plates:
    // need wipe tower if some object has multiple extruders (has paint-on colors or support material)
    for (const auto &item : m_selected) {
        std::set<int> obj_extruders;
        for (int id : item.extrude_ids) obj_extruders.insert(id);
        if (obj_extruders.size() > 1) {
            need_wipe_tower = true;
            BOOST_LOG_TRIVIAL(info) << "arrange: need wipe tower because object " << item.name << " has multiple extruders (has paint-on colors)";
            break;
        }
    }
     
    // if multile extruders have same bed temp, we need wipe tower
     if (params.allow_multi_materials_on_same_plate) {
        std::map<int, std::set<int>> bedTemp2extruderIds;
        for (const auto &item : m_selected)
            for (auto id : item.extrude_ids) { bedTemp2extruderIds[item.bed_temp].insert(id); }
        for (const auto &be : bedTemp2extruderIds) {
            if (be.second.size() > 1) {
                need_wipe_tower = true;
                BOOST_LOG_TRIVIAL(info) << "arrange: need wipe tower because allow_multi_materials_on_same_plate=true and we have multiple extruders of same type";
                break;
            }
        }
    }
    BOOST_LOG_TRIVIAL(info) << "arrange: need_wipe_tower=" << need_wipe_tower;

    if (need_wipe_tower) {
        // check all plates to see if wipe tower is already there
        ArrangePolygon    wipe_tower_ap;
        std::vector<bool> plates_have_wipe_tower(MAX_NUM_PLATES, false);
        for (int bedid = 0; bedid < MAX_NUM_PLATES; bedid++)
            if (auto wti = get_wipe_tower(*m_plater, bedid)) {
                ArrangePolygon &&ap = get_wipetower_arrange_poly(&wti);
                wipe_tower_ap       = ap;
                ap.bed_idx          = bedid;
                m_unselected.emplace_back(std::move(ap));
                plates_have_wipe_tower[bedid] = true;
            }

        // if wipe tower is not init yet (no wipe tower in any plate before arrangement)
        if (wipe_tower_ap.poly.empty()) {
            auto &print                       = wxGetApp().plater()->get_partplate_list().get_current_fff_print();
            wipe_tower_ap.poly.contour.points = print.first_layer_wipe_tower_corners(false);
            wipe_tower_ap.name                = "WipeTower";
            wipe_tower_ap.is_virt_object      = true;
            wipe_tower_ap.is_wipe_tower       = true;
        }

        for (int bedid = 0; bedid < MAX_NUM_PLATES; bedid++) {
            if (!plates_have_wipe_tower[bedid]) {
                wipe_tower_ap.bed_idx = bedid;
                m_unselected.emplace_back(wipe_tower_ap);
            }
        }
    }
}


//BBS: prepare current part plate for arranging
void ArrangeJob::prepare_partplate() {
    clear_input();

    PartPlateList& plate_list = m_plater->get_partplate_list();
    PartPlate* plate = plate_list.get_curr_plate();
    current_plate_index = plate_list.get_curr_plate_index();
    assert(plate != nullptr);

    if (plate->empty())
    {
        //no instances on this plate
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": no instances in current plate!");

        return;
    }

    if (plate->is_locked()) {
        m_plater->get_notification_manager()->push_notification(NotificationType::BBLPlateInfo,
            NotificationManager::NotificationLevel::WarningNotificationLevel, into_u8(_L("This plate is locked,\nWe can not do auto-arrange on this plate.")));
        return;
    }

    Model& model = m_plater->model();

    // Go through the objects and check if inside the selection
    for (size_t oidx = 0; oidx < model.objects.size(); ++oidx)
    {
        ModelObject* mo = model.objects[oidx];
        for (size_t inst_idx = 0; inst_idx < mo->instances.size(); ++inst_idx)
        {
            bool in_plate = plate->contain_instance(oidx, inst_idx);
            ArrangePolygon&& ap = prepare_arrange_polygon(mo->instances[inst_idx]);

            ArrangePolygons& cont = mo->instances[inst_idx]->printable ?
                (in_plate ? m_selected : m_unselected) :
                m_unprintable;
            bool locked = plate_list.preprocess_arrange_polygon_other_locked(oidx, inst_idx, ap, in_plate);
            if (!locked)
            {
                ap.itemid = cont.size();
                cont.emplace_back(std::move(ap));
            }
            else
            {
                //skip this object due to be not in current plate, treated as locked
                ap.itemid = m_locked.size();
                m_locked.emplace_back(std::move(ap));
                BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": skip locked instance, obj_id %1%, name %2%") % oidx % mo->name;
            }
        }
    }

    // BBS
    if (auto wti = get_wipe_tower(*m_plater, current_plate_index)) {
        ArrangePolygon&& ap = get_wipetower_arrange_poly(&wti);
        m_unselected.emplace_back(std::move(ap));
    }

    // add the virtual object into unselect list if has
    plate_list.preprocess_exclude_areas(m_unselected, current_plate_index + 1);
}

//BBS: add partplate logic
void ArrangeJob::prepare()
{
    m_plater->get_notification_manager()->push_notification(NotificationType::ArrangeOngoing,
        NotificationManager::NotificationLevel::RegularNotificationLevel, _u8L("Arranging..."));
    m_plater->get_notification_manager()->bbl_close_plateinfo_notification();

    {
        const GLCanvas3D::ArrangeSettings &settings = static_cast<const GLCanvas3D *>(m_plater->canvas3D())->get_arrange_settings();
        auto &                             print    = wxGetApp().plater()->get_partplate_list().get_current_fff_print();

        params.clearance_height_to_rod             = print.config().extruder_clearance_height_to_rod.value;
        params.clearance_height_to_lid             = print.config().extruder_clearance_height_to_lid.value;
        params.cleareance_radius                   = print.config().extruder_clearance_radius.value;
        params.printable_height                    = print.config().printable_height.value;
        params.allow_rotations                     = settings.enable_rotation;
        params.allow_multi_materials_on_same_plate = settings.allow_multi_materials_on_same_plate;
        params.avoid_extrusion_cali_region         = settings.avoid_extrusion_cali_region;
        params.is_seq_print                        = settings.is_seq_print;
        params.min_obj_distance                    = scaled(settings.distance);
    }

    //BBS update extruder params and speed table before arranging
    Plater::setExtruderParams(Model::extruderParamsMap);
    Plater::setPrintSpeedTable(Model::printSpeedMap);

    int state = m_plater->get_prepare_state();
    if (state == Job::JobPrepareState::PREPARE_STATE_DEFAULT) {
        only_on_partplate = false;
        prepare_all();
    }
    else if (state == Job::JobPrepareState::PREPARE_STATE_MENU) {
        only_on_partplate = true;   // only arrange items on current plate
        prepare_partplate();
    }


#if SAVE_ARRANGE_POLY
    if (1)
    { // subtract excluded region and get a polygon bed
        auto& print = wxGetApp().plater()->get_partplate_list().get_current_fff_print();
        auto print_config = print.config();
        bed_poly.points = get_bed_shape(*m_plater->config());
        Pointfs excluse_area_points = print_config.bed_exclude_area.values;
        Polygons exclude_polys;
        Polygon exclude_poly;
        for (int i = 0; i < excluse_area_points.size(); i++) {
            auto pt = excluse_area_points[i];
            exclude_poly.points.emplace_back(scale_(pt.x()), scale_(pt.y()));
            if (i % 4 == 3) {  // exclude areas are always rectangle
                exclude_polys.push_back(exclude_poly);
                exclude_poly.points.clear();
            }
        }
        bed_poly = diff({ bed_poly }, exclude_polys)[0];
    }

    BoundingBox bbox = bed_poly.bounding_box();
    Point center = bbox.center();
    auto polys_to_draw = m_selected;
    for (auto it = polys_to_draw.begin(); it != polys_to_draw.end(); it++) {
        it->poly.translate(center);
        bbox.merge(it->poly);
    }
    SVG svg("SVG/arrange_poly.svg", bbox);
    if (svg.is_opened()) {
        svg.draw_outline(bed_poly);
        //svg.draw_grid(bbox, "gray", scale_(0.05));
        std::vector<std::string> color_array = { "red","black","yellow","gree","blue" };
        for (auto it = polys_to_draw.begin(); it != polys_to_draw.end(); it++) {
            std::string color = color_array[(it - polys_to_draw.begin()) % color_array.size()];
            svg.add_comment(it->name);
            svg.draw_text(get_extents(it->poly).min, it->name.c_str(), color.c_str());
            svg.draw_outline(it->poly, color);
        }
    }
#endif

    check_unprintable();
}

void ArrangeJob::check_unprintable()
{
    for (auto it = m_selected.begin(); it != m_selected.end();) {
        if (it->poly.area() < 0.001)
        {
#if SAVE_ARRANGE_POLY
            SVG svg("SVG/arrange_unprintable_"+it->name+".svg", get_extents(it->poly));
            if (svg.is_opened())
                svg.draw_outline(it->poly);
#endif

            m_unprintable.push_back(*it);
            auto msg = (boost::format(
                _utf8("Object %s has zero size and can't be arranged."))
                % _utf8(it->name)).str();
            m_plater->get_notification_manager()->push_notification(NotificationType::BBLPlateInfo,
                                NotificationManager::NotificationLevel::WarningNotificationLevel, msg);
            it = m_selected.erase(it);
        }
        else
            it++;
    }
}

void ArrangeJob::on_exception(const std::exception_ptr &eptr)
{
    try {
        if (eptr)
            std::rethrow_exception(eptr);
    } catch (libnest2d::GeometryException &) {
        show_error(m_plater, _(L("Arrange failed. "
                                 "Found some exceptions when processing object geometries.")));
    } catch (std::exception &) {
        PlaterJob::on_exception(eptr);
    }
}

void ArrangeJob::process()
{
    const GLCanvas3D::ArrangeSettings &settings =
        static_cast<const GLCanvas3D*>(m_plater->canvas3D())->get_arrange_settings();
    auto & partplate_list = m_plater->get_partplate_list();
    auto& print = wxGetApp().plater()->get_partplate_list().get_current_fff_print();

    if (params.is_seq_print)
        params.min_obj_distance = std::max(params.min_obj_distance, scaled(params.cleareance_radius));

    if (params.avoid_extrusion_cali_region && print.full_print_config().opt_bool("scan_first_layer"))
        partplate_list.preprocess_nonprefered_areas(m_unselected, MAX_NUM_PLATES);
        
    double skirt_distance = print.has_skirt() ? print.config().skirt_distance.value : 0;
    double brim_max = 0;
    std::for_each(m_selected.begin(), m_selected.end(), [&](ArrangePolygon ap) {  brim_max = std::max(brim_max, ap.brim_width); });

    // Note: skirt_distance is now defined between outermost brim and skirt, not the object and skirt.
    // So we can't do max but do adding instead.
    params.brim_skirt_distance = skirt_distance + brim_max;
    params.bed_shrink_x = settings.bed_shrink_x + params.brim_skirt_distance;
    params.bed_shrink_y = settings.bed_shrink_y + params.brim_skirt_distance;
    // for sequential print, we need to inflate the bed because cleareance_radius is so large
    if (params.is_seq_print) {
        float shift_dist = params.cleareance_radius / 2 - 5;
        params.bed_shrink_x -= shift_dist;
        params.bed_shrink_y -= shift_dist;
        // dont forget to move the excluded region
        for (auto& region : m_unselected) {
            if (region.is_virt_object)
                region.poly.translate(-scaled(shift_dist), -scaled(shift_dist));
        }
    }

    if (print.full_print_config().opt_bool("enable_support")) {
        params.bed_shrink_x = std::max(5.f, params.bed_shrink_x);
        params.bed_shrink_y = std::max(5.f, params.bed_shrink_y);
        params.min_obj_distance = std::max(scaled(10.0), params.min_obj_distance);
    }

    // do not inflate brim_width. Objects are allowed to have overlapped brim.
    Points      bedpts = get_bed_shape(*m_plater->config());
    BoundingBox bedbb  = Polygon(bedpts).bounding_box();
    std::for_each(m_selected.begin(), m_selected.end(), [&](ArrangePolygon &ap) {
        ap.inflation      = params.min_obj_distance / 2;
        BoundingBox apbb  = ap.poly.contour.bounding_box();
        coord_t     diffx  = bedbb.size().x() - apbb.size().x();
        coord_t     diffy  = bedbb.size().y() - apbb.size().y();
        if (diffx > 0 && diffy > 0) { 
            coord_t min_diff = std::min(diffx, diffy);
            ap.inflation     = std::min(min_diff / 2, ap.inflation);
        }
    });
    // For occulusion regions, inflation should be larger to prevent genrating brim on them.
    // However, extrusion cali regions are exceptional, since we can allow brim overlaps them.
    // 屏蔽区域只需要膨胀brim宽度，防止brim长过去；挤出标定区域不需要膨胀，brim可以长过去。
    // 以前我们认为还需要膨胀clearance_radius/2，这其实是不需要的，因为这些区域并不会真的摆放物体，
    // 其他物体的膨胀轮廓是可以跟它们重叠的。
    double scaled_exclusion_gap = scale_(1);
    std::for_each(m_unselected.begin(), m_unselected.end(), [&](auto &ap) {
        ap.inflation = !ap.is_virt_object ?
                           params.min_obj_distance / 2 :
                           (ap.is_extrusion_cali_object ? 0 : scaled_exclusion_gap);
    });


    partplate_list.preprocess_exclude_areas(params.excluded_regions, 1, scaled_exclusion_gap);

    // shrink bed by moving to center by dist
    auto shrinkFun = [](Points& bedpts, double dist, int direction) {
#define SGN(x) ((x)>=0?1:-1)
        Point center = Polygon(bedpts).bounding_box().center();
        for (auto& pt : bedpts)
            pt[direction] += dist * SGN(center[direction] - pt[direction]);
    };
    shrinkFun(bedpts, scaled(params.bed_shrink_x), 0);
    shrinkFun(bedpts, scaled(params.bed_shrink_y), 1);

    BOOST_LOG_TRIVIAL(debug) << "arrange bed_shrink_x=" << params.bed_shrink_x
        << ", brim_max= "<<brim_max<<", "
        << "; bedpts:" << bedpts[0].transpose() << ", " << bedpts[1].transpose() << ", " << bedpts[2].transpose() << ", " << bedpts[3].transpose();

    params.stopcondition = [this]() { return was_canceled(); };

    params.progressind = [this](unsigned num_finished, std::string str="") {
        update_status(num_finished, _L("Arranging") + " " + str);
    };

    {
        BOOST_LOG_TRIVIAL(debug) << "items selected before arrange: ";
        for (auto selected : m_selected)
            BOOST_LOG_TRIVIAL(debug) << selected.name << ", extruder: " << selected.extrude_ids.back() << ", bed: " << selected.bed_idx
            << ", bed_temp: " << selected.first_bed_temp << ", print_temp: " << selected.print_temp;
        BOOST_LOG_TRIVIAL(debug) << "items unselected before arrange: ";
        for (auto item : m_unselected)
            if (!item.is_virt_object)
            BOOST_LOG_TRIVIAL(debug) << item.name << ", extruder: " << item.extrude_ids.back() << ", bed: " << item.bed_idx << ", trans: " << item.translation.transpose();
    }

    arrangement::arrange(m_selected, m_unselected, bedpts, params);

    // sort by item id
    std::sort(m_selected.begin(), m_selected.end(), [](auto a, auto b) {return a.itemid < b.itemid; });
    {
        BOOST_LOG_TRIVIAL(debug) << "items selected after arrange: ";
        for (auto selected : m_selected)
            BOOST_LOG_TRIVIAL(debug) << selected.name << ", extruder: " << selected.extrude_ids.back() << ", bed: " << selected.bed_idx
                                     << ", bed_temp: " << selected.first_bed_temp << ", print_temp: " << selected.print_temp
                                     << ", trans: " << unscale<double>(selected.translation(X)) << ","<< unscale<double>(selected.translation(Y));
        BOOST_LOG_TRIVIAL(debug) << "items unselected after arrange: ";
        for (auto item : m_unselected)
            if (!item.is_virt_object)
                BOOST_LOG_TRIVIAL(debug) << item.name << ", extruder: " << item.extrude_ids.back() << ", bed: " << item.bed_idx
                << ", trans: " << item.translation.transpose();
    }

    arrangement::arrange(m_unprintable, {}, bedpts, params);

    // put unpackable items to m_unprintable so they goes outside
    bool we_have_unpackable_items = false;
    for (auto item : m_selected) {
        if (item.bed_idx < 0) {
            //BBS: already processed in m_selected
            //m_unprintable.push_back(std::move(item));
            we_have_unpackable_items = true;
        }
    }

    // finalize just here.
    update_status(status_range(),
        was_canceled() ? _(L("Arranging canceled.")) :
        we_have_unpackable_items ? _(L("Arranging is done but there are unpacked items. Reduce spacing and try again.")) : _(L("Arranging done.")));
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

    //BBS: partplate
    PartPlateList& plate_list = m_plater->get_partplate_list();
    //clear all the relations before apply the arrangement results
    if (only_on_partplate) {
        plate_list.clear(false, false, true, current_plate_index);
    }
    else
        plate_list.clear(false, false, true, -1);
    //BBS: adjust the bed_index, create new plates, get the max bed_index
    for (ArrangePolygon& ap : m_selected) {
        //if (ap.bed_idx < 0) continue;  // bed_idx<0 means unarrangable
        //BBS: partplate postprocess
        if (only_on_partplate)
            plate_list.postprocess_bed_index_for_current_plate(ap);
        else
            plate_list.postprocess_bed_index_for_selected(ap);

        beds = std::max(ap.bed_idx, beds);

        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": arrange selected %4%: bed_id %1%, trans {%2%,%3%}") % ap.bed_idx % unscale<double>(ap.translation(X)) % unscale<double>(ap.translation(Y)) % ap.name;
    }

    //BBS: adjust the bed_index, create new plates, get the max bed_index
    for (ArrangePolygon& ap : m_unselected)
    {
        if (ap.is_virt_object)
            continue;

        //BBS: partplate postprocess
        if (!only_on_partplate)
            plate_list.postprocess_bed_index_for_unselected(ap);

        beds = std::max(ap.bed_idx, beds);
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(":arrange unselected %4%: bed_id %1%, trans {%2%,%3%}") % ap.bed_idx % unscale<double>(ap.translation(X)) % unscale<double>(ap.translation(Y)) % ap.name;
    }

    for (ArrangePolygon& ap : m_locked) {
        beds = std::max(ap.bed_idx, beds);

        plate_list.postprocess_arrange_polygon(ap, false);

        ap.apply();
    }

    // Apply the arrange result to all selected objects
    for (ArrangePolygon& ap : m_selected) {
        //BBS: partplate postprocess
        plate_list.postprocess_arrange_polygon(ap, true);

        ap.apply();
    }

    // Apply the arrange result to unselected objects(due to the sukodu-style column changes, the position of unselected may also be modified)
    for (ArrangePolygon& ap : m_unselected)
    {
        if (ap.is_virt_object)
            continue;

        //BBS: partplate postprocess
        plate_list.postprocess_arrange_polygon(ap, false);

        ap.apply();
    }

    // Move the unprintable items to the last virtual bed.
    // Note ap.apply() moves relatively according to bed_idx, so we need to subtract the orignal bed_idx
    for (ArrangePolygon& ap : m_unprintable) {
        ap.bed_idx = beds + 1;
        plate_list.postprocess_arrange_polygon(ap, true);

        ap.apply();
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(":arrange m_unprintable: name: %4%, bed_id %1%, trans {%2%,%3%}") % ap.bed_idx % unscale<double>(ap.translation(X)) % unscale<double>(ap.translation(Y)) % ap.name;
    }

    m_plater->update();
    // BBS
    //wxGetApp().obj_manipul()->set_dirty();

    if (!m_unarranged.empty()) {
        std::set<std::string> names;
        for (ModelInstance *mi : m_unarranged)
            names.insert(mi->get_object()->name);

        m_plater->get_notification_manager()->push_notification(GUI::format(
            _L("Arrangement ignored the following objects which can't fit into a single bed:\n%s"),
            concat_strings(names, "\n")));
    }
    m_plater->get_notification_manager()->close_notification_of_type(NotificationType::ArrangeOngoing);

    //BBS: reload all objects due to arrange
    if (only_on_partplate) {
        plate_list.rebuild_plates_after_arrangement(!only_on_partplate, true, current_plate_index);
    }
    else {
        plate_list.rebuild_plates_after_arrangement(!only_on_partplate, true);
    }

    // BBS: update slice context and gcode result.
    m_plater->update_slicing_context_to_current_partplate();

    wxGetApp().obj_list()->reload_all_plates();

    m_plater->update();

    Job::finalize();
}

std::optional<arrangement::ArrangePolygon>
get_wipe_tower_arrangepoly(const Plater &plater)
{
    // BBS FIXME: use actual plate_idx
    if (auto wti = get_wipe_tower(plater, 0))
        return get_wipetower_arrange_poly(&wti);

    return {};
}

//BBS: add sudoku-style stride
double bed_stride_x(const Plater* plater) {
    double bedwidth = plater->build_volume().bounding_box().size().x();
    return scaled<double>((1. + LOGICAL_BED_GAP) * bedwidth);
}

double bed_stride_y(const Plater* plater) {
    double beddepth = plater->build_volume().bounding_box().size().y();
    return scaled<double>((1. + LOGICAL_BED_GAP) * beddepth);
}


arrangement::ArrangeParams get_arrange_params(Plater *p)
{
    const GLCanvas3D::ArrangeSettings &settings =
        static_cast<const GLCanvas3D*>(p->canvas3D())->get_arrange_settings();

    arrangement::ArrangeParams params;
    params.allow_rotations  = settings.enable_rotation;
    params.min_obj_distance = scaled(settings.distance);
    //BBS: add specific params
    params.is_seq_print = settings.is_seq_print;
    params.bed_shrink_x = settings.bed_shrink_x;
    params.bed_shrink_y = settings.bed_shrink_y;

    return params;
}

}} // namespace Slic3r::GUI
