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
    m_grid_sizer = new wxFlexGridSizer(3, 5, 5); // "Min Z", "Max Z", "Layer height" & buttons sizer
    m_grid_sizer->SetFlexibleDirection(wxHORIZONTAL);

    // Legend for object layers
    for (const std::string col : { "Min Z", "Max Z", "Layer height" }) {
        auto temp = new wxStaticText(m_parent, wxID_ANY, _(L(col)), wxDefaultPosition, /*size*/wxDefaultSize, wxST_ELLIPSIZE_MIDDLE);
        temp->SetFont(Slic3r::GUI::wxGetApp().normal_font());
        temp->SetBackgroundStyle(wxBG_STYLE_PAINT);
        temp->SetFont(wxGetApp().bold_font());

        m_grid_sizer->Add(temp);
    }

    m_og->sizer->Clear(true);
    m_og->sizer->Add(m_grid_sizer, 0, wxEXPAND | wxALL, wxOSX ? 0 : 5);

    m_bmp_delete    = ScalableBitmap(parent, "remove_copies"/*"cross"*/);
    m_bmp_add       = ScalableBitmap(parent, "add_copies");
}

wxSizer* ObjectLayers::create_layer_without_buttons(const t_layer_config_ranges::value_type& layer)
{
    // Add control for the "Min Z"
    auto temp = new LayerRangeEditor(m_parent, double_to_string(layer.first.first),
                                     [layer](coordf_t min_z) {
        wxGetApp().obj_list()->edit_layer_range(layer.first, { min_z, layer.first.second });
    });

    m_grid_sizer->Add(temp);

    // Add control for the "Max Z"
    temp = new LayerRangeEditor(m_parent, double_to_string(layer.first.second),
                                [layer](coordf_t max_z) {
        wxGetApp().obj_list()->edit_layer_range(layer.first, { layer.first.first, max_z });
    });
    
    m_grid_sizer->Add(temp);

    // Add control for the "Layer height"
    temp = new LayerRangeEditor(m_parent, 
                                double_to_string(layer.second.option("layer_height")->getFloat()), 
                                [layer](coordf_t layer_height) {
        wxGetApp().obj_list()->edit_layer_range(layer.first, layer_height);
    }, false );

    auto sizer = new wxBoxSizer(wxHORIZONTAL); 
    sizer->Add(temp);
    m_grid_sizer->Add(sizer);

    return sizer;
}

void ObjectLayers::create_layers_list()
{
    for (const auto layer : m_object->layer_config_ranges)
    {
        auto sizer = create_layer_without_buttons(layer);

        wxWindow* parent = m_parent;
        auto del_btn = new ScalableButton(parent, wxID_ANY, m_bmp_delete);
        del_btn->SetToolTip(_(L("Remove layer")));

        sizer->Add(del_btn, 0, wxRIGHT | wxLEFT, em_unit(parent));

        del_btn->Bind(wxEVT_BUTTON, [this, layer](wxEvent &event) {
            wxGetApp().obj_list()->del_layer_range(layer.first);
        });

        auto add_btn = new ScalableButton(parent, wxID_ANY, m_bmp_add);
        add_btn->SetToolTip(_(L("Add layer")));

        sizer->Add(add_btn, 0, wxRIGHT, em_unit(parent));

        add_btn->Bind(wxEVT_BUTTON, [this, layer](wxEvent &event) {
            wxGetApp().obj_list()->add_layer_range(layer.first);
        });
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

    create_layer_without_buttons(*layer_range);
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

    const int cols = m_grid_sizer->GetEffectiveColsCount();
    const int rows = m_grid_sizer->GetEffectiveRowsCount();
    for (int idx = cols*rows-1; idx >= cols; idx--) {
        wxSizerItem* t = m_grid_sizer->GetItem(idx);
        if (t->IsSizer())
            t->GetSizer()->Clear(true);
        else
            t->DeleteWindows();
        m_grid_sizer->Remove(idx);
    }

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

LayerRangeEditor::LayerRangeEditor( wxWindow* parent,
                                    const wxString& value,
                                    std::function<void(coordf_t)> edit_fn,
                                    const bool deletable_after_change
                                    ) :
    wxTextCtrl(parent, wxID_ANY, value, wxDefaultPosition, 
               wxSize(field_width * em_unit(parent), wxDefaultCoord), wxTE_PROCESS_ENTER)
{
    this->SetFont(wxGetApp().normal_font());
    
    this->Bind(wxEVT_TEXT_ENTER, ([this, edit_fn](wxEvent& e)
    {
        m_enter_pressed = true;
        edit_fn(get_value());
    }), this->GetId());

    this->Bind(wxEVT_KILL_FOCUS, ([this, edit_fn, deletable_after_change](wxEvent& e)
    {
        if (!deletable_after_change)
            e.Skip();
        if (!m_enter_pressed) {
            m_enter_pressed = false;
            edit_fn(get_value());
        }
    }), this->GetId());


    this->Bind(wxEVT_CHAR, ([this](wxKeyEvent& event)
    {
        // select all text using Ctrl+A
        if (wxGetKeyState(wxKeyCode('A')) && wxGetKeyState(WXK_CONTROL))
            this->SetSelection(-1, -1); //select all
        event.Skip();
    }));
}

coordf_t LayerRangeEditor::get_value()
{
    wxString str = GetValue();

    coordf_t layer_height;
    // Replace the first occurence of comma in decimal number.
    str.Replace(",", ".", false);
    if (str == ".")
        layer_height = 0.0;
    else
    {
        if (!str.ToCDouble(&layer_height) || layer_height < 0.0f)
        {
            show_error(m_parent, _(L("Invalid numeric input.")));
            SetValue(double_to_string(layer_height));
        }
    }

    return layer_height;
}

} //namespace GUI
} //namespace Slic3r 