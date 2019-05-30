#include "GUI_ObjectLayers.hpp"
#include "GUI_ObjectList.hpp"

#include "OptionsGroup.hpp"
#include "PresetBundle.hpp"
#include "libslic3r/Model.hpp"

#include <boost/algorithm/string.hpp>

#include "I18N.hpp"

#include <wx/wupdlock.h>

namespace Slic3r
{
namespace GUI
{

typedef std::map<t_layer_height_range, coordf_t> t_layer_height_ranges;

#define field_width 8

ObjectLayers::ObjectLayers(wxWindow* parent) :
    OG_Settings(parent, true)
{
    m_og->label_width = 1;
//     m_og->set_grid_vgap(5);
    
    // Legend for object layers
    Line line = Line{ "", "" };

    ConfigOptionDef def;
    def.label = "";
    def.gui_type = "legend";
    def.type = coString;
    def.width = field_width;

    for (const std::string axis : { "Min Z", "Max Z", "Layer height" }) {
        def.set_default_value(new ConfigOptionString{ axis });
        std::string label = boost::algorithm::replace_all_copy(axis, " ", "_");
        boost::algorithm::to_lower(label);
        line.append_option(Option(def, label + "_legend"));

        m_legends.push_back(label + "_legend");
    }

    m_og->append_line(line);

    m_bmp_delete    = ScalableBitmap(parent, "cross");
    m_bmp_add       = ScalableBitmap(parent, "add_copies");
}

static Line create_new_layer(const t_layer_height_ranges::value_type& layer)
{
    Line line = Line{ "", "" };
    ConfigOptionDef def;
    def.label = "";
    def.gui_type = "";
    def.type = coFloat;
    def.width = field_width;

    std::string label = (boost::format("min_z_%.2f") % layer.first.first).str();
    def.set_default_value(new ConfigOptionFloat(layer.first.first));
    line.append_option(Option(def, label));

    label = (boost::format("max_z_%.2f") % layer.first.second).str();
    def.set_default_value(new ConfigOptionFloat(layer.first.second));
    line.append_option(Option(def, label));

    label = (boost::format("layer_height_%.2f_%.2f") % layer.first.first % layer.first.second).str();
    def.set_default_value(new ConfigOptionFloat(layer.second));
    line.append_option(Option(def, label));

    return line;
}

void ObjectLayers::create_layers_list()
{
    auto create_btns = [this](wxWindow* parent) {
        auto sizer = new wxBoxSizer(wxHORIZONTAL);

        auto del_btn = new ScalableButton(parent, wxID_ANY, m_bmp_delete);
        del_btn->SetToolTip(_(L("Remove layer")));

        sizer->Add(del_btn, 0, wxRIGHT, em_unit(parent));

        del_btn->Bind(wxEVT_BUTTON, [this](wxEvent &event) {
            del_layer();
//             wxTheApp->CallAfter([this]() {
//                 wxWindowUpdateLocker noUpdates(m_parent);
//                 update_layers_list(); 
//                 m_parent->Layout();
//             });
        });

        auto add_btn = new ScalableButton(parent, wxID_ANY, m_bmp_add);
        add_btn->SetToolTip(_(L("Add layer")));

        sizer->Add(add_btn, 0, wxRIGHT, em_unit(parent));

        add_btn->Bind(wxEVT_BUTTON, [this](wxEvent &event) {
            add_layer();
//         wxTheApp->CallAfter([this]() {
//             wxWindowUpdateLocker noUpdates(m_parent);
//             update_layers_list(); 
//             m_parent->Layout();
//         });
        });

        return sizer;
    };

    for (const auto layer : m_object->layer_height_ranges)
    {
        Line line = create_new_layer(layer);
        line.append_widget(create_btns);
        m_og->append_line(line);
    }
}

void ObjectLayers::create_layer()
{
    for (const auto layer : m_object->layer_height_ranges)
    {
        m_og->append_line(create_new_layer(layer));
        break;
    }
}

void ObjectLayers::update_layers_list()
{
    ObjectList* objects_ctrl   = wxGetApp().obj_list();
    if (objects_ctrl->multiple_selection()) return;

    const auto item = objects_ctrl->GetSelection();
    if (!item) return;

    const int obj_idx = objects_ctrl->get_selected_obj_idx();
    if (obj_idx < 0) return;

    const ItemType type = objects_ctrl->GetModel()->GetItemType(item);
    if (!(type & (itLayerRoot | itLayer))) return;

    m_object = objects_ctrl->object(obj_idx);
    if (!m_object || m_object->layer_height_ranges.empty()) return;

    // Delete all controls from options group except of the legends

    auto grid_sizer = m_og->get_grid_sizer();
    const int cols = grid_sizer->GetEffectiveColsCount();
    const int rows = grid_sizer->GetEffectiveRowsCount();
    for (int idx = cols*rows-1; idx >= cols; idx--) {
        wxSizerItem* t = grid_sizer->GetItem(idx);
        if (t->IsSizer())
            t->GetSizer()->Clear(true);
            grid_sizer->Remove(idx);
    }

    m_og->clear_fields_except_of(m_legends);


    // Add new control according to the selected item  

    if (type & itLayerRoot)
        create_layers_list();
    else
        create_layer();
    
    m_parent->Layout();
}

void ObjectLayers::UpdateAndShow(const bool show)
{
    if (show)
        update_layers_list();

    OG_Settings::UpdateAndShow(show);
}

void ObjectLayers::msw_rescale()
{
    m_bmp_delete.msw_rescale();
    m_bmp_add.msw_rescale();
}

} //namespace GUI
} //namespace Slic3r 