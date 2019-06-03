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

#define field_width 8

ObjectLayers::ObjectLayers(wxWindow* parent) :
    OG_Settings(parent, true)
{
    m_og->label_width = 1;
    m_og->set_grid_vgap(5);

    m_og->m_on_change = std::bind(&ObjectLayers::on_change, this, std::placeholders::_1, std::placeholders::_2);
    
    // Legend for object layers
    Line line = Line{ "", "" };

    ConfigOptionDef def;
    def.label = "";
    def.gui_type = "legend";
    def.type = coString;
    def.width = field_width;

    for (const std::string col : { "Min Z", "Max Z", "Layer height" }) {
        def.set_default_value(new ConfigOptionString{ col });
        std::string label = boost::algorithm::replace_all_copy(col, " ", "_");
        boost::algorithm::to_lower(label);
        line.append_option(Option(def, label + "_legend"));

        m_legends.push_back(label + "_legend");
    }

    m_og->append_line(line);

    m_bmp_delete    = ScalableBitmap(parent, "remove_copies"/*"cross"*/);
    m_bmp_add       = ScalableBitmap(parent, "add_copies");
}

static Line create_new_layer(const t_layer_config_ranges::value_type& layer, const int idx)
{
    Line line = Line{ "", "" };
    ConfigOptionDef def;
    def.label = "";
    def.gui_type = "";
    def.type = coFloat;
    def.width = field_width;

    std::string label = (boost::format("min_z_%d") % idx).str();
    def.set_default_value(new ConfigOptionFloat(layer.first.first));
    line.append_option(Option(def, label));

    label = (boost::format("max_z_%d") % idx).str();
    def.set_default_value(new ConfigOptionFloat(layer.first.second));
    line.append_option(Option(def, label));

    label = (boost::format("layer_height_%d") % idx).str();
    def.set_default_value(new ConfigOptionFloat(layer.second.option("layer_height")->getFloat()));
    line.append_option(Option(def, label));

    return line;
}

void ObjectLayers::create_layers_list()
{
    for (const auto layer : m_object->layer_config_ranges)
    {
        auto create_btns = [this, layer](wxWindow* parent) {
            auto sizer = new wxBoxSizer(wxHORIZONTAL);

            auto del_btn = new ScalableButton(parent, wxID_ANY, m_bmp_delete);
            del_btn->SetToolTip(_(L("Remove layer")));

            sizer->Add(del_btn, 0, wxRIGHT, em_unit(parent));

            del_btn->Bind(wxEVT_BUTTON, [this, layer](wxEvent &event) {
                wxGetApp().obj_list()->del_layer_range(layer.first);
            });

            auto add_btn = new ScalableButton(parent, wxID_ANY, m_bmp_add);
            add_btn->SetToolTip(_(L("Add layer")));

            sizer->Add(add_btn, 0, wxRIGHT, em_unit(parent));

            add_btn->Bind(wxEVT_BUTTON, [this, layer](wxEvent &event) {
                wxGetApp().obj_list()->add_layer_range(layer.first);
            });

            return sizer;
        };

        Line line = create_new_layer(layer, m_og->get_grid_sizer()->GetEffectiveRowsCount()-1);
        line.append_widget(create_btns);
        m_og->append_line(line);
    }
}

void ObjectLayers::create_layer(int id)
{
    t_layer_config_ranges::iterator layer_range = m_object->layer_config_ranges.begin();

    // May be not a best solution #ys_FIXME
    while (id > 0 && layer_range != m_object->layer_config_ranges.end()) {
        ++layer_range;
        id--;
    }
        
    m_og->append_line(create_new_layer(*layer_range, m_og->get_grid_sizer()->GetEffectiveRowsCount()-1));
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
    if (!m_object || m_object->layer_config_ranges.empty()) return;

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
        create_layer(objects_ctrl->GetModel()->GetLayerIdByItem(item));
    
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

void ObjectLayers::on_change(t_config_option_key opt_key, const boost::any& value)
{

}

} //namespace GUI
} //namespace Slic3r 