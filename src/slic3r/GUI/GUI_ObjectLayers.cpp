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

ObjectLayers::ObjectLayers(wxWindow* parent) :
    OG_Settings(parent, true)
{
    m_og->label_width = 0;
    m_og->set_grid_vgap(5);  
    
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
    }

    m_og->append_line(line);

    m_bmp_delete    = ScalableBitmap(parent, "cross");
    m_bmp_add       = ScalableBitmap(parent, "add_copies");
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

    ModelObject* object = objects_ctrl->object(obj_idx);
    if (!object || object->layer_height_ranges.empty()) return;

    auto grid_sizer = m_og->get_grid_sizer();

    const int cols = grid_sizer->GetCols();
    const int rows = grid_sizer->GetRows();
    for (int idx = cols*rows-1; idx >= cols; idx--) {
        grid_sizer->Remove(idx);
    }

    ConfigOptionDef def;
    def.label = "";
    def.gui_type = "";
    def.type = coFloat;
    def.width = field_width;

    if (type & itLayerRoot)
    {
        auto create_btns = [this](wxWindow* parent) {
            auto sizer = new wxBoxSizer(wxHORIZONTAL);
            auto del_btn = new ScalableButton(parent, wxID_ANY, m_bmp_delete);
            del_btn->SetToolTip(_(L("Remove layer")));

            sizer->Add(del_btn, 0, wxRIGHT, em_unit(parent));

            del_btn->Bind(wxEVT_BUTTON, [this](wxEvent &event) {
                del_layer();
//                 wxTheApp->CallAfter([this]() {
//                     wxWindowUpdateLocker noUpdates(m_parent);
//                     update_layers_list(); 
//                     m_parent->Layout();
//                 });
            });

            auto add_btn = new ScalableButton(parent, wxID_ANY, m_bmp_add);
            add_btn->SetToolTip(_(L("Add layer")));

            sizer->Add(add_btn, 0, wxRIGHT, em_unit(parent));

            add_btn->Bind(wxEVT_BUTTON, [this](wxEvent &event) {
                add_layer();
//                 wxTheApp->CallAfter([this]() {
//                     wxWindowUpdateLocker noUpdates(m_parent);
//                     update_layers_list(); 
//                     m_parent->Layout();
//                 });
            });

            return sizer;
        };

        Line line{"",""};
        for (const auto layer : object->layer_height_ranges)
        {
            std::string label = (boost::format("min_z_%.2f") % layer.first.first).str();
            def.set_default_value(new ConfigOptionFloat(layer.first.first));
            line.append_option(Option(def, label));
           
            label = (boost::format("max_z_%.2f") % layer.first.second).str();
            def.set_default_value(new ConfigOptionFloat(layer.first.second));
            line.append_option(Option(def, label));
           
            label = (boost::format("layer_height_%.2f_%.2f") % layer.first.first % layer.first.second).str();
            def.set_default_value(new ConfigOptionFloat(layer.second));
            line.append_option(Option(def, label));

            line.append_widget(create_btns);
        }

        m_og->append_line(line);
    }
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