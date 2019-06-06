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

wxSizer* ObjectLayers::create_layer(const t_layer_height_range& range) 
{
    const bool is_last_edited_range = range == m_last_edited_range;

    // Add control for the "Min Z"

    auto temp = new LayerRangeEditor(m_parent, double_to_string(range.first),
        [range, this](coordf_t min_z)
    {
        if (fabs(min_z - range.first) < EPSILON) {
            m_selection_type = sitUndef;
            return false;       // LayersList would not be updated/recreated
        }

        // data for next focusing
        m_last_edited_range = { min_z, range.second };
        m_selection_type = sitMinZ;

        wxGetApp().obj_list()->edit_layer_range(range, m_last_edited_range);
        return true;            // LayersList will be updated/recreated
    });

    if (is_last_edited_range && m_selection_type == sitMinZ) {
        temp->SetFocus();
        temp->SetInsertionPointEnd();
    }

    m_grid_sizer->Add(temp);

    // Add control for the "Max Z"

    temp = new LayerRangeEditor(m_parent, double_to_string(range.second),
                                [range, this](coordf_t max_z)
    {
        if (fabs(max_z - range.second) < EPSILON) {
            m_selection_type = sitUndef;
            return false;       // LayersList would not be updated/recreated
        }

        // data for next focusing
        m_last_edited_range = { range.first, max_z };
        m_selection_type = sitMaxZ;

        wxGetApp().obj_list()->edit_layer_range(range, m_last_edited_range);
        return true;            // LayersList will not be updated/recreated
    });

    if (is_last_edited_range && m_selection_type == sitMaxZ) {
        temp->SetFocus();
        temp->SetInsertionPointEnd();
    }

    m_grid_sizer->Add(temp);

    // Add control for the "Layer height"

    temp = new LayerRangeEditor(m_parent,
                                double_to_string(m_object->layer_config_ranges[range].option("layer_height")->getFloat()),
                                [range, this](coordf_t layer_height)
    {
        wxGetApp().obj_list()->edit_layer_range(range, layer_height);
        return false;           // LayersList would not be updated/recreated
    });

    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(temp);
    m_grid_sizer->Add(sizer);

    return sizer;
}

void ObjectLayers::create_layers_list()
{
    for (const auto layer : m_object->layer_config_ranges)
    {
        const t_layer_height_range& range = layer.first;
        auto sizer = create_layer(range);

        auto del_btn = new ScalableButton(m_parent, wxID_ANY, m_bmp_delete);
        del_btn->SetToolTip(_(L("Remove layer")));

        sizer->Add(del_btn, 0, wxRIGHT | wxLEFT, em_unit(m_parent));

        del_btn->Bind(wxEVT_BUTTON, [this, range](wxEvent &event) {
            wxGetApp().obj_list()->del_layer_range(range);
        });

        auto add_btn = new ScalableButton(m_parent, wxID_ANY, m_bmp_add);
        add_btn->SetToolTip(_(L("Add layer")));

        sizer->Add(add_btn, 0, wxRIGHT, em_unit(m_parent));

        add_btn->Bind(wxEVT_BUTTON, [this, range](wxEvent &event) {
            wxGetApp().obj_list()->add_layer_range_after_current(range);
        });
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
        create_layer(objects_ctrl->GetModel()->GetLayerRangeByItem(item));
    
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
                                    std::function<bool(coordf_t)> edit_fn
                                    ) :
    wxTextCtrl(parent, wxID_ANY, value, wxDefaultPosition, 
               wxSize(8 * em_unit(parent), wxDefaultCoord), wxTE_PROCESS_ENTER)
{
    this->SetFont(wxGetApp().normal_font());
    
    this->Bind(wxEVT_TEXT_ENTER, ([this, edit_fn](wxEvent& e)
    {
        m_enter_pressed     = true;
        // If LayersList wasn't updated/recreated, we can call wxEVT_KILL_FOCUS.Skip()
        if ( !edit_fn(get_value()) )
            m_call_kill_focus = true;
    }), this->GetId());

    this->Bind(wxEVT_KILL_FOCUS, ([this, edit_fn](wxEvent& e)
    {
        if (!m_enter_pressed) {
            m_enter_pressed = false;

            // If LayersList wasn't updated/recreated, we should call e.Skip()
            if ( !edit_fn(get_value()) )
                e.Skip();
        }
        else if (m_call_kill_focus)
            e.Skip();
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