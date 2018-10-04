#include "GUI_ObjectList.hpp"
#include "GUI_App.hpp"

#include "OptionsGroup.hpp"
#include "PresetBundle.hpp"
#include "Tab.hpp"
#include "wxExtensions.hpp"

// #include "Model.hpp"
// #include "LambdaObjectDialog.hpp"
// #include "../../libslic3r/Utils.hpp"
// 
// #include <wx/msgdlg.h>
// #include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
// #include "Geometry.hpp"
#include "slic3r/Utils/FixModelByWin10.hpp"

// 
// #include <wx/glcanvas.h>
// #include "3DScene.hpp"

namespace Slic3r
{
namespace GUI
{

ObjectList::ObjectList(wxWindow* parent) :
    m_parent(parent)
{
//     wxBoxSizer* sizer;
    // create control
    create_objects_ctrl();

    // describe control behavior 
    m_objects_ctrl->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [](wxEvent& event) {
        object_ctrl_selection_changed();
#ifndef __WXMSW__
        set_tooltip_for_item(get_mouse_position_in_control());
#endif //__WXMSW__        
    });

    m_objects_ctrl->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, [](wxDataViewEvent& event) {
        object_ctrl_context_menu();
        //		event.Skip();
    });

    m_objects_ctrl->Bind(wxEVT_CHAR, [](wxKeyEvent& event) { object_ctrl_key_event(event); }); // doesn't work on OSX

#ifdef __WXMSW__
    // Extruder value changed
    m_objects_ctrl->Bind(wxEVT_CHOICE, [](wxCommandEvent& event) { update_extruder_in_config(event.GetString()); });

    m_objects_ctrl->GetMainWindow()->Bind(wxEVT_MOTION, [this](wxMouseEvent& event) {
        set_tooltip_for_item(event.GetPosition());
        event.Skip();
    });
#else
    // equivalent to wxEVT_CHOICE on __WXMSW__
    m_objects_ctrl->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, [](wxDataViewEvent& event) { object_ctrl_item_value_change(event); });
#endif //__WXMSW__

    m_objects_ctrl->Bind(wxEVT_DATAVIEW_ITEM_BEGIN_DRAG, [](wxDataViewEvent& e) {on_begin_drag(e); });
    m_objects_ctrl->Bind(wxEVT_DATAVIEW_ITEM_DROP_POSSIBLE, [](wxDataViewEvent& e) {on_drop_possible(e); });
    m_objects_ctrl->Bind(wxEVT_DATAVIEW_ITEM_DROP, [](wxDataViewEvent& e) {on_drop(e); });
}

void ObjectList::create_objects_ctrl()
{
    m_objects_ctrl = new wxDataViewCtrl(m_parent, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    m_objects_ctrl->SetMinSize(wxSize(-1, 150)); // TODO - Set correct height according to the opened/closed objects

    m_sizer = new wxBoxSizer(wxVERTICAL);
    m_sizer->Add(m_objects_ctrl, 1, wxGROW | wxLEFT, 20);

    m_objects_model = new PrusaObjectDataViewModel;
    m_objects_ctrl->AssociateModel(m_objects_model);
#if wxUSE_DRAG_AND_DROP && wxUSE_UNICODE
    m_objects_ctrl->EnableDragSource(wxDF_UNICODETEXT);
    m_objects_ctrl->EnableDropTarget(wxDF_UNICODETEXT);
#endif // wxUSE_DRAG_AND_DROP && wxUSE_UNICODE

    // column 0(Icon+Text) of the view control: 
    // And Icon can be consisting of several bitmaps
    m_objects_ctrl->AppendColumn(new wxDataViewColumn(_(L("Name")), new PrusaBitmapTextRenderer(),
        0, 200, wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE));

    // column 1 of the view control:
    m_objects_ctrl->AppendTextColumn(_(L("Copy")), 1, wxDATAVIEW_CELL_INERT, 45,
        wxALIGN_CENTER_HORIZONTAL, wxDATAVIEW_COL_RESIZABLE);

    // column 2 of the view control:
    m_objects_ctrl->AppendColumn(create_objects_list_extruder_column(4));

    // column 3 of the view control:
    m_objects_ctrl->AppendBitmapColumn(" ", 3, wxDATAVIEW_CELL_INERT, 25,
        wxALIGN_CENTER_HORIZONTAL, wxDATAVIEW_COL_RESIZABLE);
}

// ModelObjectPtrs& ObjectList::get_objects()
// {
//     return wxGetApp().mainframe->m_plater->model().objects;
// }


void ObjectList::set_tooltip_for_item(const wxPoint& pt)
{
    wxDataViewItem item;
    wxDataViewColumn* col;
    m_objects_ctrl->HitTest(pt, item, col);
    if (!item) return;

    if (col->GetTitle() == " ")
        m_objects_ctrl->GetMainWindow()->SetToolTip(_(L("Right button click the icon to change the object settings")));
//     else if (col->GetTitle() == _("Name") &&
//         m_objects_model->GetIcon(item).GetRefData() == m_icon_manifold_warning.GetRefData()) {
//         int obj_idx = m_objects_model->GetIdByItem(item);
//         auto& stats = (*m_objects)[obj_idx]->volumes[0]->mesh.stl.stats;
//         int errors = stats.degenerate_facets + stats.edges_fixed + stats.facets_removed +
//             stats.facets_added + stats.facets_reversed + stats.backwards_edges;
// 
//         wxString tooltip = wxString::Format(_(L("Auto-repaired (%d errors):\n")), errors);
// 
//         std::map<std::string, int> error_msg;
//         error_msg[L("degenerate facets")] = stats.degenerate_facets;
//         error_msg[L("edges fixed")] = stats.edges_fixed;
//         error_msg[L("facets removed")] = stats.facets_removed;
//         error_msg[L("facets added")] = stats.facets_added;
//         error_msg[L("facets reversed")] = stats.facets_reversed;
//         error_msg[L("backwards edges")] = stats.backwards_edges;
// 
//         for (auto error : error_msg)
//         {
//             if (error.second > 0)
//                 tooltip += wxString::Format(_("\t%d %s\n"), error.second, error.first);
//         }
// // OR
// //             tooltip += wxString::Format(_(L("%d degenerate facets, %d edges fixed, %d facets removed, "
// //                                             "%d facets added, %d facets reversed, %d backwards edges")),
// //                                             stats.degenerate_facets, stats.edges_fixed, stats.facets_removed,
// //                                             stats.facets_added, stats.facets_reversed, stats.backwards_edges);
// 
//         if (is_windows10())
//             tooltip += _(L("Right button click the icon to fix STL through Netfabb"));
// 
//         m_objects_ctrl->GetMainWindow()->SetToolTip(tooltip);
//     }
    else
        m_objects_ctrl->GetMainWindow()->SetToolTip(""); // hide tooltip
}

wxPoint ObjectList::get_mouse_position_in_control() {
    const wxPoint& pt = wxGetMousePosition();
    wxWindow* win = m_objects_ctrl->GetMainWindow();
    return wxPoint(pt.x - win->GetScreenPosition().x,
        pt.y - win->GetScreenPosition().y);
}

wxDataViewColumn* ObjectList::create_objects_list_extruder_column(int extruders_count)
{
    wxArrayString choices;
    choices.Add("default");
    for (int i = 1; i <= extruders_count; ++i)
        choices.Add(wxString::Format("%d", i));
    wxDataViewChoiceRenderer *c =
        new wxDataViewChoiceRenderer(choices, wxDATAVIEW_CELL_EDITABLE, wxALIGN_CENTER_HORIZONTAL);
    wxDataViewColumn* column = new wxDataViewColumn(_(L("Extruder")), c, 2, 60, wxALIGN_CENTER_HORIZONTAL, wxDATAVIEW_COL_RESIZABLE);
    return column;
}

void ObjectList::update_objects_list_extruder_column(int extruders_count)
{
    if (!m_objects_ctrl) return; // #ys_FIXME
    if (wxGetApp().preset_bundle->printers.get_selected_preset().printer_technology() == ptSLA)
        extruders_count = 1;

    // delete old 3rd column
    m_objects_ctrl->DeleteColumn(m_objects_ctrl->GetColumn(2));
    // insert new created 3rd column
    m_objects_ctrl->InsertColumn(2, create_objects_list_extruder_column(extruders_count));
    // set show/hide for this column 
    set_extruder_column_hidden(extruders_count <= 1);
}



} //namespace GUI
} //namespace Slic3r 