#include "GUI_ObjectLayers.hpp"
#include "GUI_ObjectList.hpp"

#include "OptionsGroup.hpp"
#include "GUI_App.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Model.hpp"
#include "GLCanvas3D.hpp"
#include "Plater.hpp"

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
    m_grid_sizer = new wxFlexGridSizer(3, 5, wxGetApp().em_unit()); // "Min Z", "Max Z", "Layer height" & buttons sizer
    m_grid_sizer->SetFlexibleDirection(wxHORIZONTAL);

    // Legend for object layers
    for (const std::string col : { L("Start at height"), L("Stop at height"), L("Layer height") }) {
        auto temp = new wxStaticText(m_parent, wxID_ANY, _(col), wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_MIDDLE);
        temp->SetBackgroundStyle(wxBG_STYLE_PAINT);
        temp->SetFont(wxGetApp().bold_font());

        m_grid_sizer->Add(temp);
    }

    m_og->activate();
    m_og->sizer->Clear(true);
    m_og->sizer->Add(m_grid_sizer, 0, wxEXPAND | wxALL, wxOSX ? 0 : 5);

    m_bmp_delete    = ScalableBitmap(parent, "remove_copies"/*"cross"*/);
    m_bmp_add       = ScalableBitmap(parent, "add_copies");
}

void ObjectLayers::select_editor(LayerRangeEditor* editor, const bool is_last_edited_range)
{
    if (is_last_edited_range && m_selection_type == editor->type()) {
    /* Workaround! Under OSX we should use CallAfter() for SetFocus() after LayerEditors "reorganizations", 
     * because of selected control's strange behavior: 
     * cursor is set to the control, but blue border - doesn't.
     * And as a result we couldn't edit this control.
     * */
#ifdef __WXOSX__
        wxTheApp->CallAfter([editor]() {
#endif
        editor->SetFocus();
        editor->SetInsertionPointEnd();
#ifdef __WXOSX__
        });
#endif
    }    
}

wxSizer* ObjectLayers::create_layer(const t_layer_height_range& range, PlusMinusButton *delete_button, PlusMinusButton *add_button) 
{
    const bool is_last_edited_range = range == m_selectable_range;

    auto set_focus_data = [range, this](const EditorType type)
    {
        m_selectable_range = range;
        m_selection_type = type;
    };

    auto update_focus_data = [range, this](const t_layer_height_range& new_range, EditorType type, bool enter_pressed)
    {
        // change selectable range for new one, if enter was pressed or if same range was selected
        if (enter_pressed || m_selectable_range == range)
            m_selectable_range = new_range;
        if (enter_pressed)
            m_selection_type = type;
    };

    // Add control for the "Min Z"

    auto editor = new LayerRangeEditor(this, double_to_string(range.first), etMinZ, set_focus_data, 
        [range, update_focus_data, this, delete_button, add_button](coordf_t min_z, bool enter_pressed, bool dont_update_ui) 
    {
        if (fabs(min_z - range.first) < EPSILON) {
            m_selection_type = etUndef;
            return false;
        }

        // data for next focusing
        coordf_t max_z = min_z < range.second ? range.second : min_z + 0.5;
        const t_layer_height_range new_range = { min_z, max_z };
        if (delete_button)
            delete_button->range = new_range;
        if (add_button)
            add_button->range = new_range;
        update_focus_data(new_range, etMinZ, enter_pressed);

        return wxGetApp().obj_list()->edit_layer_range(range, new_range, dont_update_ui);
    });

    select_editor(editor, is_last_edited_range);
    m_grid_sizer->Add(editor);

    // Add control for the "Max Z"

    editor = new LayerRangeEditor(this, double_to_string(range.second), etMaxZ, set_focus_data, 
        [range, update_focus_data, this, delete_button, add_button](coordf_t max_z, bool enter_pressed, bool dont_update_ui)
    {
        if (fabs(max_z - range.second) < EPSILON || range.first > max_z) {
            m_selection_type = etUndef;
            return false;       // LayersList would not be updated/recreated
        }

        // data for next focusing
        const t_layer_height_range& new_range = { range.first, max_z };
        if (delete_button)
            delete_button->range = new_range;
        if (add_button)
            add_button->range = new_range;
        update_focus_data(new_range, etMaxZ, enter_pressed);

        return wxGetApp().obj_list()->edit_layer_range(range, new_range, dont_update_ui);
    });

    select_editor(editor, is_last_edited_range);
    m_grid_sizer->Add(editor);

    // Add control for the "Layer height"

    editor = new LayerRangeEditor(this, double_to_string(m_object->layer_config_ranges[range].option("layer_height")->getFloat()), etLayerHeight, set_focus_data,
        [range](coordf_t layer_height, bool, bool)
    {
        return wxGetApp().obj_list()->edit_layer_range(range, layer_height);
    });

    select_editor(editor, is_last_edited_range);

    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(editor);

    auto temp = new wxStaticText(m_parent, wxID_ANY, _L("mm"));
    temp->SetBackgroundStyle(wxBG_STYLE_PAINT);
    temp->SetFont(wxGetApp().normal_font());
    sizer->Add(temp, 0, wxLEFT, wxGetApp().em_unit());

    m_grid_sizer->Add(sizer);

    return sizer;
}
    
void ObjectLayers::create_layers_list()
{
    for (const auto &layer : m_object->layer_config_ranges) {
        const t_layer_height_range& range = layer.first;
        auto del_btn = new PlusMinusButton(m_parent, m_bmp_delete, range);
        del_btn->SetToolTip(_L("Remove layer range"));

        auto add_btn = new PlusMinusButton(m_parent, m_bmp_add, range);
        wxString tooltip = wxGetApp().obj_list()->can_add_new_range_after_current(range);
        add_btn->SetToolTip(tooltip.IsEmpty() ? _L("Add layer range") : tooltip);
        add_btn->Enable(tooltip.IsEmpty());

        auto sizer = create_layer(range, del_btn, add_btn);
        sizer->Add(del_btn, 0, wxRIGHT | wxLEFT, em_unit(m_parent));
        sizer->Add(add_btn);

        del_btn->Bind(wxEVT_BUTTON, [del_btn](wxEvent &) {
            wxGetApp().obj_list()->del_layer_range(del_btn->range);
        });

        add_btn->Bind(wxEVT_BUTTON, [add_btn](wxEvent &) {
            wxGetApp().obj_list()->add_layer_range_after_current(add_btn->range);
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
        create_layer(objects_ctrl->GetModel()->GetLayerRangeByItem(item), nullptr, nullptr);

    m_parent->Layout();
}

void ObjectLayers::update_scene_from_editor_selection() const
{
    // needed to show the visual hints in 3D scene
    wxGetApp().plater()->canvas3D()->handle_layers_data_focus_event(m_selectable_range, m_selection_type);
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

    m_grid_sizer->SetHGap(wxGetApp().em_unit());

    // rescale edit-boxes
    const int cells_cnt = m_grid_sizer->GetCols() * m_grid_sizer->GetEffectiveRowsCount();
    for (int i = 0; i < cells_cnt; ++i) {
        const wxSizerItem* item = m_grid_sizer->GetItem(i);
        if (item->IsWindow()) {
            LayerRangeEditor* editor = dynamic_cast<LayerRangeEditor*>(item->GetWindow());
            if (editor != nullptr)
                editor->msw_rescale();
        }
        else if (item->IsSizer()) // case when we have editor with buttons
        {
            wxSizerItem* e_item = item->GetSizer()->GetItem(size_t(0)); // editor
            if (e_item->IsWindow()) {
                LayerRangeEditor* editor = dynamic_cast<LayerRangeEditor*>(e_item->GetWindow());
                if (editor != nullptr)
                    editor->msw_rescale();
            }

            if (item->GetSizer()->GetItemCount() > 2) // if there are Add/Del buttons
                for (size_t btn : {2, 3}) { // del_btn, add_btn
                    wxSizerItem* b_item = item->GetSizer()->GetItem(btn);
                    if (b_item->IsWindow()) {
                        auto button = dynamic_cast<PlusMinusButton*>(b_item->GetWindow());
                        if (button != nullptr)
                            button->msw_rescale();
                    }
                }
        }
    }
    m_grid_sizer->Layout();
}

void ObjectLayers::sys_color_changed()
{
    m_bmp_delete.msw_rescale();
    m_bmp_add.msw_rescale();

    // rescale edit-boxes
    const int cells_cnt = m_grid_sizer->GetCols() * m_grid_sizer->GetEffectiveRowsCount();
    for (int i = 0; i < cells_cnt; ++i) {
        const wxSizerItem* item = m_grid_sizer->GetItem(i);
        if (item->IsSizer()) {// case when we have editor with buttons
            for (size_t btn : {2, 3}) { // del_btn, add_btn
                wxSizerItem* b_item = item->GetSizer()->GetItem(btn);
                if (b_item->IsWindow()) {
                    auto button = dynamic_cast<PlusMinusButton*>(b_item->GetWindow());
                    if (button != nullptr)
                        button->msw_rescale();
                }
            }
        }
    }

#ifdef _WIN32
    m_og->sys_color_changed();
    for (int i = 0; i < cells_cnt; ++i) {
        const wxSizerItem* item = m_grid_sizer->GetItem(i);
        if (item->IsWindow()) {
            if (LayerRangeEditor* editor = dynamic_cast<LayerRangeEditor*>(item->GetWindow()))
                wxGetApp().UpdateDarkUI(editor);
        }
        else if (item->IsSizer()) {// case when we have editor with buttons
            if (wxSizerItem* e_item = item->GetSizer()->GetItem(size_t(0)); e_item->IsWindow()) {
                if (LayerRangeEditor* editor = dynamic_cast<LayerRangeEditor*>(e_item->GetWindow()))
                    wxGetApp().UpdateDarkUI(editor);
            }
        }
    }
#endif

}

void ObjectLayers::reset_selection()
{
    m_selectable_range = { 0.0, 0.0 };
    m_selection_type = etLayerHeight;
}

LayerRangeEditor::LayerRangeEditor( ObjectLayers* parent,
                                    const wxString& value,
                                    EditorType type,
                                    std::function<void(EditorType)> set_focus_data_fn,
                                    std::function<bool(coordf_t, bool, bool)>   edit_fn
                                    ) :
    m_valid_value(value),
    m_type(type),
    m_set_focus_data(set_focus_data_fn),
    wxTextCtrl(parent->m_parent, wxID_ANY, value, wxDefaultPosition, 
               wxSize(8 * em_unit(parent->m_parent), wxDefaultCoord), wxTE_PROCESS_ENTER
#ifdef _WIN32
        | wxBORDER_SIMPLE
#endif
    )
{
    this->SetFont(wxGetApp().normal_font());
    wxGetApp().UpdateDarkUI(this);

    // Reset m_enter_pressed flag to _false_, when value is editing
    this->Bind(wxEVT_TEXT, [this](wxEvent&) { m_enter_pressed = false; }, this->GetId());
    
    this->Bind(wxEVT_TEXT_ENTER, [this, edit_fn](wxEvent&)
    {
        m_enter_pressed     = true;
        // If LayersList wasn't updated/recreated, we can call wxEVT_KILL_FOCUS.Skip()
        if (m_type&etLayerHeight) {
            if (!edit_fn(get_value(), true, false))
                SetValue(m_valid_value);
            else
                m_valid_value = double_to_string(get_value());
            m_call_kill_focus = true;
        }
        else if (!edit_fn(get_value(), true, false)) {
            SetValue(m_valid_value);
            m_call_kill_focus = true;
        }
    }, this->GetId());

    this->Bind(wxEVT_KILL_FOCUS, [this, edit_fn](wxFocusEvent& e)
    {
        if (!m_enter_pressed) {
#ifndef __WXGTK__
            /* Update data for next editor selection.
             * But under GTK it looks like there is no information about selected control at e.GetWindow(),
             * so we'll take it from wxEVT_LEFT_DOWN event
             * */
            LayerRangeEditor* new_editor = dynamic_cast<LayerRangeEditor*>(e.GetWindow());
            if (new_editor)
                new_editor->set_focus_data();
#endif // not __WXGTK__
            // If LayersList wasn't updated/recreated, we should call e.Skip()
            if (m_type & etLayerHeight) {
                if (!edit_fn(get_value(), false, dynamic_cast<ObjectLayers::PlusMinusButton*>(e.GetWindow()) != nullptr))
                    SetValue(m_valid_value);
                else
                    m_valid_value = double_to_string(get_value());
                e.Skip();
            }
            else if (!edit_fn(get_value(), false, dynamic_cast<ObjectLayers::PlusMinusButton*>(e.GetWindow()) != nullptr)) {
                SetValue(m_valid_value);
                e.Skip();
            } 
        }
        else if (m_call_kill_focus) {
            m_call_kill_focus = false;
            e.Skip();
        }
    }, this->GetId());

    this->Bind(wxEVT_SET_FOCUS, [this, parent](wxFocusEvent& e)
    {
        set_focus_data();
        parent->update_scene_from_editor_selection();
        e.Skip();
    }, this->GetId());

#ifdef __WXGTK__ // Workaround! To take information about selectable range
    this->Bind(wxEVT_LEFT_DOWN, [this](wxEvent& e)
    {
        set_focus_data();
        e.Skip();
    }, this->GetId());
#endif //__WXGTK__

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
    const char dec_sep = is_decimal_separator_point() ? '.' : ',';
    const char dec_sep_alt = dec_sep == '.' ? ',' : '.';
    // Replace the first incorrect separator in decimal number.
    if (str.Replace(dec_sep_alt, dec_sep, false) != 0)
        SetValue(str);

    if (str == ".")
        layer_height = 0.0;
    else {
        if (!str.ToDouble(&layer_height) || layer_height < 0.0f) {
            show_error(m_parent, _L("Invalid numeric input."));
            SetValue(double_to_string(layer_height));
        }
    }

    return layer_height;
}

void LayerRangeEditor::msw_rescale()
{
    SetMinSize(wxSize(8 * wxGetApp().em_unit(), wxDefaultCoord));
}

} //namespace GUI
} //namespace Slic3r 
