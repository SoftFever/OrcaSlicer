#include "libslic3r/libslic3r.h"
#include "libslic3r/PresetBundle.hpp"
#include "GUI_ObjectList.hpp"
#include "GUI_Factories.hpp"
#include "GUI_ObjectManipulation.hpp"
#include "GUI_ObjectLayers.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "Plater.hpp"
#include "BitmapComboBox.hpp"
#if ENABLE_PROJECT_DIRTY_STATE
#include "MainFrame.hpp"
#endif // ENABLE_PROJECT_DIRTY_STATE

#include "OptionsGroup.hpp"
#include "Tab.hpp"
#include "wxExtensions.hpp"
#include "libslic3r/Model.hpp"
#include "GLCanvas3D.hpp"
#include "Selection.hpp"
#include "format.hpp"

#include <boost/algorithm/string.hpp>
#include <wx/progdlg.h>
#include <wx/listbook.h>
#include <wx/numformatter.h>

#include "slic3r/Utils/FixModelByWin10.hpp"

#ifdef __WXMSW__
#include "wx/uiaction.h"
#endif /* __WXMSW__ */

namespace Slic3r
{
namespace GUI
{

wxDEFINE_EVENT(EVT_OBJ_LIST_OBJECT_SELECT, SimpleEvent);

static PrinterTechnology printer_technology()
{
    return wxGetApp().preset_bundle->printers.get_selected_preset().printer_technology();
}

static const Selection& scene_selection()
{
    return wxGetApp().plater()->canvas3D()->get_selection();
}

// Config from current edited printer preset
static DynamicPrintConfig& printer_config()
{
    return wxGetApp().preset_bundle->printers.get_edited_preset().config;
}

static int extruders_count()
{
    return wxGetApp().extruders_edited_cnt();
}

static void take_snapshot(const wxString& snapshot_name) 
{
    Plater* plater = wxGetApp().plater();
    if (plater)
        plater->take_snapshot(snapshot_name);
}

ObjectList::ObjectList(wxWindow* parent) :
    wxDataViewCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, 
#ifdef _WIN32
        wxBORDER_SIMPLE | 
#endif
        wxDV_MULTIPLE)
{
    wxGetApp().UpdateDVCDarkUI(this, true);

    // create control
    create_objects_ctrl();

    // describe control behavior 
    Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [this](wxDataViewEvent& event) {
        // detect the current mouse position here, to pass it to list_manipulation() method
        // if we detect it later, the user may have moved the mouse pointer while calculations are performed, and this would mess-up the HitTest() call performed into list_manipulation()
        // see: https://github.com/prusa3d/PrusaSlicer/issues/3802
#ifndef __WXOSX__
        const wxPoint mouse_pos = this->get_mouse_position_in_control();
#endif

#ifndef __APPLE__
        // On Windows and Linux:
        // It's not invoked KillFocus event for "temporary" panels (like "Manipulation panel", "Settings", "Layer ranges"),
        // if we change selection in object list.
        // see https://github.com/prusa3d/PrusaSlicer/issues/3303
        // But, if we call SetFocus() for ObjectList it will cause an invoking of a KillFocus event for "temporary" panels  
        this->SetFocus();
#else
        // To avoid selection update from SetSelection() and UnselectAll() under osx
        if (m_prevent_list_events)
            return;
#endif // __APPLE__

        /* For multiple selection with pressed SHIFT, 
         * event.GetItem() returns value of a first item in selection list 
         * instead of real last clicked item.
         * So, let check last selected item in such strange way
         */
#ifdef __WXMSW__
		// Workaround for entering the column editing mode on Windows. Simulate keyboard enter when another column of the active line is selected.
		int new_selected_column = -1;
#endif //__WXMSW__
        if (wxGetKeyState(WXK_SHIFT))
        {
            wxDataViewItemArray sels;
            GetSelections(sels);
            if (! sels.empty() && sels.front() == m_last_selected_item)
                m_last_selected_item = sels.back();
            else
                m_last_selected_item = event.GetItem();
        }
        else {
  	      	wxDataViewItem    new_selected_item  = event.GetItem();
#ifdef __WXMSW__
			// Workaround for entering the column editing mode on Windows. Simulate keyboard enter when another column of the active line is selected.
		    wxDataViewItem    item;
		    wxDataViewColumn *col;
		    this->HitTest(this->get_mouse_position_in_control(), item, col);
		    new_selected_column = (col == nullptr) ? -1 : (int)col->GetModelColumn();
	        if (new_selected_item == m_last_selected_item && m_last_selected_column != -1 && m_last_selected_column != new_selected_column) {
	        	// Mouse clicked on another column of the active row. Simulate keyboard enter to enter the editing mode of the current column.
	        	wxUIActionSimulator sim;
				sim.Char(WXK_RETURN);
	        }
#endif //__WXMSW__
	        m_last_selected_item = new_selected_item;
        }
#ifdef __WXMSW__
        m_last_selected_column = new_selected_column;
#endif //__WXMSW__

        selection_changed();
#ifndef __WXMSW__
        set_tooltip_for_item(this->get_mouse_position_in_control());
#endif //__WXMSW__

#ifndef __WXOSX__
        list_manipulation(mouse_pos);
#endif //__WXOSX__
    });

#ifdef __WXOSX__
    // Key events are not correctly processed by the wxDataViewCtrl on OSX.
    // Our patched wxWidgets process the keyboard accelerators.
    // On the other hand, using accelerators will break in-place editing on Windows & Linux/GTK (there is no in-place editing working on OSX for wxDataViewCtrl for now).
//    Bind(wxEVT_KEY_DOWN, &ObjectList::OnChar, this);
    {
        // Accelerators
        wxAcceleratorEntry entries[33];
        entries[0].Set(wxACCEL_CTRL, (int)'C', wxID_COPY);
        entries[1].Set(wxACCEL_CTRL, (int)'X', wxID_CUT);
        entries[2].Set(wxACCEL_CTRL, (int)'V', wxID_PASTE);
        entries[3].Set(wxACCEL_CTRL, (int)'A', wxID_SELECTALL);
        entries[4].Set(wxACCEL_CTRL, (int)'Z', wxID_UNDO);
        entries[5].Set(wxACCEL_CTRL, (int)'Y', wxID_REDO);
        entries[6].Set(wxACCEL_NORMAL, WXK_DELETE, wxID_DELETE);
        entries[7].Set(wxACCEL_NORMAL, WXK_BACK, wxID_DELETE);
        entries[8].Set(wxACCEL_NORMAL, int('+'), wxID_ADD);
        entries[9].Set(wxACCEL_NORMAL, WXK_NUMPAD_ADD, wxID_ADD);
        entries[10].Set(wxACCEL_NORMAL, int('-'), wxID_REMOVE);
        entries[11].Set(wxACCEL_NORMAL, WXK_NUMPAD_SUBTRACT, wxID_REMOVE);
        entries[12].Set(wxACCEL_NORMAL, int('p'), wxID_PRINT);

        int numbers_cnt = 1;
        for (auto char_number : { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9' }) {
            entries[12 + numbers_cnt].Set(wxACCEL_NORMAL, int(char_number), wxID_LAST + numbers_cnt);
            entries[22 + numbers_cnt].Set(wxACCEL_NORMAL, WXK_NUMPAD0 + numbers_cnt - 1, wxID_LAST + numbers_cnt);
            numbers_cnt++;
        }
        wxAcceleratorTable accel(33, entries);
        SetAcceleratorTable(accel);

        this->Bind(wxEVT_MENU, [this](wxCommandEvent &evt) { this->copy();                      }, wxID_COPY);
        this->Bind(wxEVT_MENU, [this](wxCommandEvent &evt) { this->paste();                     }, wxID_PASTE);
        this->Bind(wxEVT_MENU, [this](wxCommandEvent &evt) { this->select_item_all_children();  }, wxID_SELECTALL);
        this->Bind(wxEVT_MENU, [this](wxCommandEvent &evt) { this->remove();                    }, wxID_DELETE);
        this->Bind(wxEVT_MENU, [this](wxCommandEvent &evt) { this->undo();  					}, wxID_UNDO);
        this->Bind(wxEVT_MENU, [this](wxCommandEvent &evt) { this->redo();                    	}, wxID_REDO);
        this->Bind(wxEVT_MENU, [this](wxCommandEvent &evt) { this->increase_instances();        }, wxID_ADD);
        this->Bind(wxEVT_MENU, [this](wxCommandEvent &evt) { this->decrease_instances();        }, wxID_REMOVE);
        this->Bind(wxEVT_MENU, [this](wxCommandEvent &evt) { this->toggle_printable_state();    }, wxID_PRINT);
        
        for (int i = 0; i < 10; i++)
            this->Bind(wxEVT_MENU, [this, i](wxCommandEvent &evt) {
                if (extruders_count() > 1 && i <= extruders_count())
                    this->set_extruder_for_selected_items(i);
            }, wxID_LAST+i+1);
    }
#else //__WXOSX__
    Bind(wxEVT_CHAR, [this](wxKeyEvent& event) { key_event(event); }); // doesn't work on OSX
#endif

#ifdef __WXMSW__
    GetMainWindow()->Bind(wxEVT_MOTION, [this](wxMouseEvent& event) {
        set_tooltip_for_item(this->get_mouse_position_in_control());
        event.Skip();
    });
#endif //__WXMSW__

    Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU,  &ObjectList::OnContextMenu,     this);

    Bind(wxEVT_DATAVIEW_ITEM_BEGIN_DRAG,    &ObjectList::OnBeginDrag,       this);
    Bind(wxEVT_DATAVIEW_ITEM_DROP_POSSIBLE, &ObjectList::OnDropPossible,    this);
    Bind(wxEVT_DATAVIEW_ITEM_DROP,          &ObjectList::OnDrop,            this);

#ifdef __WXMSW__
    Bind(wxEVT_DATAVIEW_ITEM_EDITING_STARTED, &ObjectList::OnEditingStarted,  this);
#endif /* __WXMSW__ */
    Bind(wxEVT_DATAVIEW_ITEM_EDITING_DONE,    &ObjectList::OnEditingDone,     this);

    Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, &ObjectList::ItemValueChanged,  this);

    Bind(wxCUSTOMEVT_LAST_VOLUME_IS_DELETED, [this](wxCommandEvent& e)   { last_volume_is_deleted(e.GetInt()); });

    Bind(wxEVT_SIZE, ([this](wxSizeEvent &e) { 
#ifdef __WXGTK__
	// On GTK, the EnsureVisible call is postponed to Idle processing (see wxDataViewCtrl::m_ensureVisibleDefered).
	// So the postponed EnsureVisible() call is planned for an item, which may not exist at the Idle processing time, if this wxEVT_SIZE
	// event is succeeded by a delete of the currently active item. We are trying our luck by postponing the wxEVT_SIZE triggered EnsureVisible(),
	// which seems to be working as of now.
    this->CallAfter([this](){ ensure_current_item_visible(); });
#else
    ensure_current_item_visible();
#endif
	e.Skip();
	}));
}

ObjectList::~ObjectList()
{
}

void ObjectList::set_min_height()
{
    if (m_items_count == size_t(-1))
        m_items_count = 7;
    int list_min_height = lround(2.25 * (m_items_count + 1) * wxGetApp().em_unit()); // +1 is for height of control header
    this->SetMinSize(wxSize(1, list_min_height));
}

void ObjectList::update_min_height()
{
    wxDataViewItemArray all_items;
    m_objects_model->GetAllChildren(wxDataViewItem(nullptr), all_items);
    size_t items_cnt = all_items.Count();
    if (items_cnt < 7)
        items_cnt = 7;
    else if (items_cnt >= 15)
        items_cnt = 15;
    
    if (m_items_count == items_cnt)
        return;

    m_items_count = items_cnt;
    set_min_height();
}


void ObjectList::create_objects_ctrl()
{
    /* Temporary workaround for the correct behavior of the Scrolled sidebar panel:
     * 1. set a height of the list to some big value 
     * 2. change it to the normal(meaningful) min value after first whole Mainframe updating/layouting
     */
    SetMinSize(wxSize(-1, 3000));

    m_sizer = new wxBoxSizer(wxVERTICAL);
    m_sizer->Add(this, 1, wxGROW);

    m_objects_model = new ObjectDataViewModel;
    AssociateModel(m_objects_model);
    m_objects_model->SetAssociatedControl(this);
#if wxUSE_DRAG_AND_DROP && wxUSE_UNICODE
    EnableDragSource(wxDF_UNICODETEXT);
    EnableDropTarget(wxDF_UNICODETEXT);
#endif // wxUSE_DRAG_AND_DROP && wxUSE_UNICODE

    const int em = wxGetApp().em_unit();

    // column ItemName(Icon+Text) of the view control: 
    // And Icon can be consisting of several bitmaps
    BitmapTextRenderer* bmp_text_renderer = new BitmapTextRenderer();
    bmp_text_renderer->set_can_create_editor_ctrl_function([this]() {
        return m_objects_model->GetItemType(GetSelection()) & (itVolume | itObject);
    });
    AppendColumn(new wxDataViewColumn(_L("Name"), bmp_text_renderer,
        colName, 20*em, wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE));

    // column PrintableProperty (Icon) of the view control:
    AppendBitmapColumn(" ", colPrint, wxDATAVIEW_CELL_INERT, 3*em,
        wxALIGN_CENTER_HORIZONTAL, wxDATAVIEW_COL_RESIZABLE);

    // column Extruder of the view control:
    BitmapChoiceRenderer* bmp_choice_renderer = new BitmapChoiceRenderer();
    bmp_choice_renderer->set_can_create_editor_ctrl_function([this]() {
        return m_objects_model->GetItemType(GetSelection()) & (itVolume | itLayer | itObject);
    });
    bmp_choice_renderer->set_default_extruder_idx([this]() {
        return m_objects_model->GetDefaultExtruderIdx(GetSelection());
    });
    AppendColumn(new wxDataViewColumn(_L("Extruder"), bmp_choice_renderer,
        colExtruder, 8*em, wxALIGN_CENTER_HORIZONTAL, wxDATAVIEW_COL_RESIZABLE));

    // column ItemEditing of the view control:
    AppendBitmapColumn(_L("Editing"), colEditing, wxDATAVIEW_CELL_INERT, 3*em,
        wxALIGN_CENTER_HORIZONTAL, wxDATAVIEW_COL_RESIZABLE);

    // For some reason under OSX on 4K(5K) monitors in wxDataViewColumn constructor doesn't set width of column.
    // Therefore, force set column width.
    if (wxOSX)
    {
        GetColumn(colName)->SetWidth(20*em);
        GetColumn(colPrint)->SetWidth(3*em);
        GetColumn(colExtruder)->SetWidth(8*em);
        GetColumn(colEditing) ->SetWidth(7*em);
    }
}

void ObjectList::get_selected_item_indexes(int& obj_idx, int& vol_idx, const wxDataViewItem& input_item/* = wxDataViewItem(nullptr)*/)
{
    const wxDataViewItem item = input_item == wxDataViewItem(nullptr) ? GetSelection() : input_item;

    if (!item)
    {
        obj_idx = vol_idx = -1;
        return;
    }

    const ItemType type = m_objects_model->GetItemType(item);

    obj_idx =   type & itObject ? m_objects_model->GetIdByItem(item) :
                type & itVolume ? m_objects_model->GetIdByItem(m_objects_model->GetTopParent(item)) : -1;

    vol_idx =   type & itVolume ? m_objects_model->GetVolumeIdByItem(item) : -1;
}

void ObjectList::get_selection_indexes(std::vector<int>& obj_idxs, std::vector<int>& vol_idxs)
{
    wxDataViewItemArray sels;
    GetSelections(sels);
    assert(!sels.IsEmpty());

    if ( m_objects_model->GetItemType(sels[0]) & itVolume || 
        (sels.Count()==1 && m_objects_model->GetItemType(m_objects_model->GetParent(sels[0])) & itVolume) ) {
        for (wxDataViewItem item : sels) {
            obj_idxs.emplace_back(m_objects_model->GetIdByItem(m_objects_model->GetTopParent(item)));

            if (sels.Count() == 1 && m_objects_model->GetItemType(m_objects_model->GetParent(item)) & itVolume)
                item = m_objects_model->GetParent(item);

            assert(m_objects_model->GetItemType(item) & itVolume);
            vol_idxs.emplace_back(m_objects_model->GetVolumeIdByItem(item));
        }
    }
    else {
        for (wxDataViewItem item : sels) {
            const ItemType type = m_objects_model->GetItemType(item);
            obj_idxs.emplace_back(type & itObject ? m_objects_model->GetIdByItem(item) :
                                  m_objects_model->GetIdByItem(m_objects_model->GetTopParent(item)));
        }
    }

    std::sort(obj_idxs.begin(), obj_idxs.end(), std::greater<int>());
    obj_idxs.erase(std::unique(obj_idxs.begin(), obj_idxs.end()), obj_idxs.end());
}

int ObjectList::get_mesh_errors_count(const int obj_idx, const int vol_idx /*= -1*/) const
{
    if (obj_idx < 0)
        return 0;

    return (*m_objects)[obj_idx]->get_mesh_errors_count(vol_idx);
}

wxString ObjectList::get_mesh_errors_list(const int obj_idx, const int vol_idx /*= -1*/) const
{    
    const int errors = get_mesh_errors_count(obj_idx, vol_idx);

    if (errors == 0)
        return ""; // hide tooltip

    // Create tooltip string, if there are errors 
    wxString tooltip = format_wxstr(_L_PLURAL("Auto-repaired %1$d error", "Auto-repaired %1$d errors", errors), errors) + ":\n";

    const stl_stats& stats = vol_idx == -1 ?
                            (*m_objects)[obj_idx]->get_object_stl_stats() :
                            (*m_objects)[obj_idx]->volumes[vol_idx]->mesh().stl.stats;

    if (stats.degenerate_facets > 0)
        tooltip += "\t" + format_wxstr(_L_PLURAL("%1$d degenerate facet", "%1$d degenerate facets", stats.degenerate_facets), stats.degenerate_facets) + "\n";
    if (stats.edges_fixed > 0)
        tooltip += "\t" + format_wxstr(_L_PLURAL("%1$d edge fixed", "%1$d edges fixed", stats.edges_fixed), stats.edges_fixed) + "\n";
    if (stats.facets_removed > 0)
        tooltip += "\t" + format_wxstr(_L_PLURAL("%1$d facet removed", "%1$d facets removed", stats.facets_removed), stats.facets_removed) + "\n";
    if (stats.facets_added > 0)
        tooltip += "\t" + format_wxstr(_L_PLURAL("%1$d facet added", "%1$d facets added", stats.facets_added), stats.facets_added) + "\n";
    if (stats.facets_reversed > 0)
        tooltip += "\t" + format_wxstr(_L_PLURAL("%1$d facet reversed", "%1$d facets reversed", stats.facets_reversed), stats.facets_reversed) + "\n";
    if (stats.backwards_edges > 0)
        tooltip += "\t" + format_wxstr(_L_PLURAL("%1$d backwards edge", "%1$d backwards edges", stats.backwards_edges), stats.backwards_edges) + "\n";

    if (is_windows10())
        tooltip += _L("Right button click the icon to fix STL through Netfabb");

    return tooltip;
}

wxString ObjectList::get_mesh_errors_list()
{
    if (!GetSelection())
        return "";

    int obj_idx, vol_idx;
    get_selected_item_indexes(obj_idx, vol_idx);

    return get_mesh_errors_list(obj_idx, vol_idx);
}

void ObjectList::set_tooltip_for_item(const wxPoint& pt)
{
    wxDataViewItem item;
    wxDataViewColumn* col;
    HitTest(pt, item, col);

    /* GetMainWindow() return window, associated with wxDataViewCtrl.
     * And for this window we should to set tooltips.
     * Just this->SetToolTip(tooltip) => has no effect.
     */

    if (!item || GetSelectedItemsCount() > 1)
    {
        GetMainWindow()->SetToolTip(""); // hide tooltip
        return;
    }

    wxString tooltip = "";

    if (col->GetTitle() == _(L("Editing")))
#ifdef __WXOSX__
        tooltip = _(L("Right button click the icon to change the object settings"));
#else
        tooltip = _(L("Click the icon to change the object settings"));
#endif //__WXMSW__
    else if (col->GetTitle() == " ")
#ifdef __WXOSX__
        tooltip = _(L("Right button click the icon to change the object printable property"));
#else
        tooltip = _(L("Click the icon to change the object printable property"));
#endif //__WXMSW__
    else if (col->GetTitle() == _("Name") && (pt.x >= 2 * wxGetApp().em_unit() && pt.x <= 4 * wxGetApp().em_unit()))
    {
        int obj_idx, vol_idx;
        get_selected_item_indexes(obj_idx, vol_idx, item);
        tooltip = get_mesh_errors_list(obj_idx, vol_idx);
    }
    
    GetMainWindow()->SetToolTip(tooltip);
}

int ObjectList::get_selected_obj_idx() const
{
    if (GetSelectedItemsCount() == 1)
        return m_objects_model->GetIdByItem(m_objects_model->GetTopParent(GetSelection()));

    return -1;
}

ModelConfig& ObjectList::get_item_config(const wxDataViewItem& item) const 
{
    assert(item);
    const ItemType type = m_objects_model->GetItemType(item);

    const int obj_idx = m_objects_model->GetObjectIdByItem(item);
    const int vol_idx = type & itVolume ? m_objects_model->GetVolumeIdByItem(item) : -1;

    assert(obj_idx >= 0 || ((type & itVolume) && vol_idx >=0));
    return type & itVolume ?(*m_objects)[obj_idx]->volumes[vol_idx]->config :
           type & itLayer  ?(*m_objects)[obj_idx]->layer_config_ranges[m_objects_model->GetLayerRangeByItem(item)] :
                            (*m_objects)[obj_idx]->config;
}

void ObjectList::update_extruder_values_for_items(const size_t max_extruder)
{
    for (size_t i = 0; i < m_objects->size(); ++i)
    {
        wxDataViewItem item = m_objects_model->GetItemById(i);
        if (!item) continue;
            
        auto object = (*m_objects)[i];
        wxString extruder;
        if (!object->config.has("extruder") ||
            size_t(object->config.extruder()) > max_extruder)
            extruder = _(L("default"));
        else
            extruder = wxString::Format("%d", object->config.extruder());

        m_objects_model->SetExtruder(extruder, item);

        if (object->volumes.size() > 1) {
            for (size_t id = 0; id < object->volumes.size(); id++) {
                item = m_objects_model->GetItemByVolumeId(i, id);
                if (!item) continue;
                if (!object->volumes[id]->config.has("extruder") ||
                    size_t(object->volumes[id]->config.extruder()) > max_extruder)
                    extruder = _(L("default"));
                else
                    extruder = wxString::Format("%d", object->volumes[id]->config.extruder()); 

                m_objects_model->SetExtruder(extruder, item);
            }
        }
    }
}

void ObjectList::update_objects_list_extruder_column(size_t extruders_count)
{
    if (printer_technology() == ptSLA)
        extruders_count = 1;

    m_prevent_update_extruder_in_config = true;

    if (m_objects && extruders_count > 1)
        update_extruder_values_for_items(extruders_count);

    update_extruder_colors();

    // set show/hide for this column 
    set_extruder_column_hidden(extruders_count <= 1);
    //a workaround for a wrong last column width updating under OSX 
    GetColumn(colEditing)->SetWidth(25);

    m_prevent_update_extruder_in_config = false;
}

void ObjectList::update_extruder_colors()
{
    m_objects_model->UpdateColumValues(colExtruder);
}

void ObjectList::set_extruder_column_hidden(const bool hide) const
{
    GetColumn(colExtruder)->SetHidden(hide);
}

void ObjectList::update_extruder_in_config(const wxDataViewItem& item)
{
    if (m_prevent_update_extruder_in_config)
        return;

    const ItemType item_type = m_objects_model->GetItemType(item);
    if (item_type & itObject) {
        const int obj_idx = m_objects_model->GetIdByItem(item);
        m_config = &(*m_objects)[obj_idx]->config;
    }
    else {
        const int obj_idx = m_objects_model->GetIdByItem(m_objects_model->GetTopParent(item));
        if (item_type & itVolume)
        {
        const int volume_id = m_objects_model->GetVolumeIdByItem(item);
        if (obj_idx < 0 || volume_id < 0)
            return;
        m_config = &(*m_objects)[obj_idx]->volumes[volume_id]->config;
        }
        else if (item_type & itLayer)
            m_config = &get_item_config(item);
    }

    if (!m_config)
        return;

    take_snapshot(_(L("Change Extruder")));

    const int extruder = m_objects_model->GetExtruderNumber(item);
    m_config->set_key_value("extruder", new ConfigOptionInt(extruder));

    // update scene
    wxGetApp().plater()->update();
}

void ObjectList::update_name_in_model(const wxDataViewItem& item) const 
{
    const int obj_idx = m_objects_model->GetObjectIdByItem(item);
    if (obj_idx < 0) return;
    const int volume_id = m_objects_model->GetVolumeIdByItem(item);

    take_snapshot(volume_id < 0 ? _(L("Rename Object")) : _(L("Rename Sub-object")));

    if (m_objects_model->GetItemType(item) & itObject) {
        (*m_objects)[obj_idx]->name = m_objects_model->GetName(item).ToUTF8().data();
        return;
    }

    if (volume_id < 0) return;
    (*m_objects)[obj_idx]->volumes[volume_id]->name = m_objects_model->GetName(item).ToUTF8().data();
}

void ObjectList::selection_changed()
{
    if (m_prevent_list_events) return;

    fix_multiselection_conflicts();

    // update object selection on Plater
    if (!m_prevent_canvas_selection_update)
        update_selections_on_canvas();

    // to update the toolbar and info sizer
    if (!GetSelection() || m_objects_model->GetItemType(GetSelection()) == itObject) {
        auto event = SimpleEvent(EVT_OBJ_LIST_OBJECT_SELECT);
        event.SetEventObject(this);
        wxPostEvent(this, event);
    }

    if (const wxDataViewItem item = GetSelection())
    {
        const ItemType type = m_objects_model->GetItemType(item);
        // to correct visual hints for layers editing on the Scene
        if (type & (itLayer|itLayerRoot)) {
            wxGetApp().obj_layers()->reset_selection();
            
            if (type & itLayerRoot)
                wxGetApp().plater()->canvas3D()->handle_sidebar_focus_event("", false);
            else {
                wxGetApp().obj_layers()->set_selectable_range(m_objects_model->GetLayerRangeByItem(item));
                wxGetApp().obj_layers()->update_scene_from_editor_selection();
            }
        }
    }

    part_selection_changed();
}

void ObjectList::copy_layers_to_clipboard()
{
    wxDataViewItemArray sel_layers;
    GetSelections(sel_layers);

    const int obj_idx = m_objects_model->GetObjectIdByItem(sel_layers.front());
    if (obj_idx < 0 || (int)m_objects->size() <= obj_idx)
        return;

    const t_layer_config_ranges& ranges = object(obj_idx)->layer_config_ranges;
    t_layer_config_ranges& cache_ranges = m_clipboard.get_ranges_cache();

    if (sel_layers.Count() == 1 && m_objects_model->GetItemType(sel_layers.front()) & itLayerRoot)
    {
        cache_ranges.clear();
        cache_ranges = ranges;
        return;
    }

    for (const auto& layer_item : sel_layers)
        if (m_objects_model->GetItemType(layer_item) & itLayer) {
            auto range = m_objects_model->GetLayerRangeByItem(layer_item);
            auto it = ranges.find(range);
            if (it != ranges.end())
                cache_ranges[it->first] = it->second;
        }
}

void ObjectList::paste_layers_into_list()
{
    const int obj_idx = m_objects_model->GetObjectIdByItem(GetSelection());
    t_layer_config_ranges& cache_ranges = m_clipboard.get_ranges_cache();

    if (obj_idx < 0 || (int)m_objects->size() <= obj_idx || 
        cache_ranges.empty() || printer_technology() == ptSLA)
        return;

    const wxDataViewItem object_item = m_objects_model->GetItemById(obj_idx);
    wxDataViewItem layers_item = m_objects_model->GetLayerRootItem(object_item);
    if (layers_item)
        m_objects_model->Delete(layers_item);

    t_layer_config_ranges& ranges = object(obj_idx)->layer_config_ranges;

    // and create Layer item(s) according to the layer_config_ranges
    for (const auto& range : cache_ranges)
        ranges.emplace(range);

    layers_item = add_layer_root_item(object_item);

    changed_object(obj_idx);

    select_item(layers_item);
#ifndef __WXOSX__
    selection_changed();
#endif //no __WXOSX__
}

void ObjectList::copy_settings_to_clipboard()
{
    wxDataViewItem item = GetSelection();
    assert(item.IsOk());
    if (m_objects_model->GetItemType(item) & itSettings)
        item = m_objects_model->GetParent(item);

    m_clipboard.get_config_cache() = get_item_config(item).get();
}

void ObjectList::paste_settings_into_list()
{
    wxDataViewItem item = GetSelection();
    assert(item.IsOk());
    if (m_objects_model->GetItemType(item) & itSettings)
        item = m_objects_model->GetParent(item);

    ItemType item_type = m_objects_model->GetItemType(item);
    if(!(item_type & (itObject | itVolume |itLayer)))
        return;

    DynamicPrintConfig& config_cache = m_clipboard.get_config_cache();
    assert(!config_cache.empty());

    auto keys = config_cache.keys();
    auto part_options = SettingsFactory::get_options(true);

    for (const std::string& opt_key: keys) {
        if (item_type & (itVolume | itLayer) &&
            std::find(part_options.begin(), part_options.end(), opt_key) == part_options.end())
            continue; // we can't to add object specific options for the part's(itVolume | itLayer) config 

        const ConfigOption* option = config_cache.option(opt_key);
        if (option)
            m_config->set_key_value(opt_key, option->clone());
    }

    // Add settings item for object/sub-object and show them 
    show_settings(add_settings_item(item, &m_config->get()));
}

void ObjectList::paste_volumes_into_list(int obj_idx, const ModelVolumePtrs& volumes)
{
    if ((obj_idx < 0) || ((int)m_objects->size() <= obj_idx))
        return;

    if (volumes.empty())
        return;

    wxDataViewItemArray items = reorder_volumes_and_get_selection(obj_idx, [volumes](const ModelVolume* volume) {
        return std::find(volumes.begin(), volumes.end(), volume) != volumes.end(); });
    if (items.size() > 1) {
        m_selection_mode = smVolume;
        m_last_selected_item = wxDataViewItem(nullptr);
    }

    select_items(items);
    selection_changed();
}

void ObjectList::paste_objects_into_list(const std::vector<size_t>& object_idxs)
{
    if (object_idxs.empty())
        return;

    wxDataViewItemArray items;
    for (const size_t object : object_idxs)
    {
        add_object_to_list(object);
        items.Add(m_objects_model->GetItemById(object));
    }

    wxGetApp().plater()->changed_objects(object_idxs);

    select_items(items);
    selection_changed();
}

#ifdef __WXOSX__
/*
void ObjectList::OnChar(wxKeyEvent& event)
{
    if (event.GetKeyCode() == WXK_BACK){
        remove();
    }
    else if (wxGetKeyState(wxKeyCode('A')) && wxGetKeyState(WXK_SHIFT))
        select_item_all_children();

    event.Skip();
}
*/
#endif /* __WXOSX__ */

void ObjectList::OnContextMenu(wxDataViewEvent& evt)
{
    // The mouse position returned by get_mouse_position_in_control() here is the one at the time the mouse button is released (mouse up event)
    wxPoint mouse_pos = this->get_mouse_position_in_control();

    // Do not show the context menu if the user pressed the right mouse button on the 3D scene and released it on the objects list
    GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
    bool evt_context_menu = (canvas != nullptr) ? !canvas->is_mouse_dragging() : true;
    if (!evt_context_menu)
        canvas->mouse_up_cleanup();

    list_manipulation(mouse_pos, evt_context_menu);
}

void ObjectList::list_manipulation(const wxPoint& mouse_pos, bool evt_context_menu/* = false*/)
{
    // Interesting fact: when mouse_pos.x < 0, HitTest(mouse_pos, item, col) returns item = null, but column = last column.
    // So, when mouse was moved to scene immediately after clicking in ObjectList, in the scene will be shown context menu for the Editing column.
    // see: https://github.com/prusa3d/PrusaSlicer/issues/3802
    if (mouse_pos.x < 0)
        return;

    wxDataViewItem item;
    wxDataViewColumn* col = nullptr;
    HitTest(mouse_pos, item, col);

    if (m_extruder_editor)
        m_extruder_editor->Hide();

    /* Note: Under OSX right click doesn't send "selection changed" event.
     * It means that Selection() will be return still previously selected item.
     * Thus under OSX we should force UnselectAll(), when item and col are nullptr,
     * and select new item otherwise.
     */

    if (!item) {
        if (col == nullptr) {
            if (wxOSX && !multiple_selection())
                UnselectAll();
            else if (!evt_context_menu) 
                // Case, when last item was deleted and under GTK was called wxEVT_DATAVIEW_SELECTION_CHANGED,
                // which invoked next list_manipulation(false)
                return;
        }

        if (evt_context_menu) {
            show_context_menu(evt_context_menu);
            return;
        }
    }

    if (wxOSX && item && col) {
        wxDataViewItemArray sels;
        GetSelections(sels);
        UnselectAll();
        if (sels.Count() > 1)
            SetSelections(sels);
        else
            Select(item);
    }

    if (col != nullptr) 
    {
	    const wxString title = col->GetTitle();
	    if (title == " ")
	        toggle_printable_state();
	    else if (title == _("Editing"))
	        show_context_menu(evt_context_menu);
	    else if (title == _("Name"))
	    {
	        if (wxOSX)
	            show_context_menu(evt_context_menu); // return context menu under OSX (related to #2909)

	        if (is_windows10())
	        {
	            int obj_idx, vol_idx;
	            get_selected_item_indexes(obj_idx, vol_idx, item);

	            if (get_mesh_errors_count(obj_idx, vol_idx) > 0 && 
	                mouse_pos.x > 2 * wxGetApp().em_unit() && mouse_pos.x < 4 * wxGetApp().em_unit())
	                fix_through_netfabb();
	        }
	    }
	    // workaround for extruder editing under OSX 
	    else if (wxOSX && evt_context_menu && title == _("Extruder"))
	        extruder_editing();
	}

#ifndef __WXMSW__
    GetMainWindow()->SetToolTip(""); // hide tooltip
#endif //__WXMSW__
}

void ObjectList::show_context_menu(const bool evt_context_menu)
{
    wxMenu* menu {nullptr};
    Plater* plater = wxGetApp().plater();

    if (multiple_selection())
    {
        if (selected_instances_of_same_object())
            menu = plater->instance_menu();
        else
            menu = plater->multi_selection_menu();
    }
    else {
        const auto item = GetSelection();
        if (item)
        {
            const ItemType type = m_objects_model->GetItemType(item);
            if (!(type & (itObject | itVolume | itLayer | itInstance)))
                return;

            menu =  type & itInstance                                           ? plater->instance_menu() :
                    type & itLayer                                              ? plater->layer_menu() :
                    m_objects_model->GetParent(item) != wxDataViewItem(nullptr) ? plater->part_menu() :
                    printer_technology() == ptFFF                               ? plater->object_menu() : plater->sla_object_menu();
        }
        else if (evt_context_menu)
            menu = plater->default_menu();
    }

    if (menu)
        plater->PopupMenu(menu);
}

void ObjectList::extruder_editing()
{
    wxDataViewItem item = GetSelection();
    if (!item || !(m_objects_model->GetItemType(item) & (itVolume | itObject)))
        return;

    const int column_width = GetColumn(colExtruder)->GetWidth() + wxSystemSettings::GetMetric(wxSYS_VSCROLL_X) + 5;

    wxPoint pos = this->get_mouse_position_in_control();
    wxSize size = wxSize(column_width, -1);
    pos.x = GetColumn(colName)->GetWidth() + GetColumn(colPrint)->GetWidth() + 5;
    pos.y -= GetTextExtent("m").y;

    apply_extruder_selector(&m_extruder_editor, this, L("default"), pos, size);

    m_extruder_editor->SetSelection(m_objects_model->GetExtruderNumber(item));
    m_extruder_editor->Show();

    auto set_extruder = [this]()
    {
        wxDataViewItem item = GetSelection();
        if (!item) return;

        const int selection = m_extruder_editor->GetSelection();
        if (selection >= 0) 
            m_objects_model->SetExtruder(m_extruder_editor->GetString(selection), item);

        m_extruder_editor->Hide();
        update_extruder_in_config(item);
    };

    // to avoid event propagation to other sidebar items
    m_extruder_editor->Bind(wxEVT_COMBOBOX, [set_extruder](wxCommandEvent& evt)
    {
        set_extruder();
        evt.StopPropagation();
    });
}

void ObjectList::copy()
{
    wxPostEvent((wxEvtHandler*)wxGetApp().plater()->canvas3D()->get_wxglcanvas(), SimpleEvent(EVT_GLTOOLBAR_COPY));
}

void ObjectList::paste()
{
    wxPostEvent((wxEvtHandler*)wxGetApp().plater()->canvas3D()->get_wxglcanvas(), SimpleEvent(EVT_GLTOOLBAR_PASTE));
}

bool ObjectList::copy_to_clipboard()
{
    wxDataViewItemArray sels;
    GetSelections(sels);
    if (sels.IsEmpty())
        return false;
    ItemType type = m_objects_model->GetItemType(sels.front());
    if (!(type & (itSettings | itLayer | itLayerRoot))) {
        m_clipboard.reset();
        return false;
    }

    if (type & itSettings)
        copy_settings_to_clipboard();
    if (type & (itLayer | itLayerRoot))
        copy_layers_to_clipboard();

    m_clipboard.set_type(type);
    return true;
}

bool ObjectList::paste_from_clipboard()
{
    if (!(m_clipboard.get_type() & (itSettings | itLayer | itLayerRoot))) {
        m_clipboard.reset();
        return false;
    }

    if (m_clipboard.get_type() & itSettings)
        paste_settings_into_list();
    if (m_clipboard.get_type() & (itLayer | itLayerRoot))
        paste_layers_into_list();

    return true;
}

void ObjectList::undo()
{
	wxGetApp().plater()->undo();
}

void ObjectList::redo()
{
	wxGetApp().plater()->redo();	
}

void ObjectList::increase_instances()
{
    wxGetApp().plater()->increase_instances(1);
}

void ObjectList::decrease_instances()
{
    wxGetApp().plater()->decrease_instances(1);
}

#ifndef __WXOSX__
void ObjectList::key_event(wxKeyEvent& event)
{
    if (event.GetKeyCode() == WXK_TAB)
        Navigate(event.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward);
    else if (event.GetKeyCode() == WXK_DELETE
#ifdef __WXOSX__
        || event.GetKeyCode() == WXK_BACK
#endif //__WXOSX__
        ) {
        remove();
    }
    else if (event.GetKeyCode() == WXK_F5)
        wxGetApp().plater()->reload_all_from_disk();
    else if (wxGetKeyState(wxKeyCode('A')) && wxGetKeyState(WXK_CONTROL/*WXK_SHIFT*/))
        select_item_all_children();
    else if (wxGetKeyState(wxKeyCode('C')) && wxGetKeyState(WXK_CONTROL)) 
        copy();
    else if (wxGetKeyState(wxKeyCode('V')) && wxGetKeyState(WXK_CONTROL))
        paste();
    else if (wxGetKeyState(wxKeyCode('Y')) && wxGetKeyState(WXK_CONTROL))
        redo();
    else if (wxGetKeyState(wxKeyCode('Z')) && wxGetKeyState(WXK_CONTROL))
        undo();
    else if (event.GetUnicodeKey() == '+')
        increase_instances();
    else if (event.GetUnicodeKey() == '-')
        decrease_instances();
    else if (event.GetUnicodeKey() == 'p')
        toggle_printable_state();
    else if (extruders_count() > 1) {
        std::vector<wxChar> numbers = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9' };
        wxChar key_char = event.GetUnicodeKey();
        if (std::find(numbers.begin(), numbers.end(), key_char) != numbers.end()) {
            long extruder_number;
            if (wxNumberFormatter::FromString(wxString(key_char), &extruder_number) &&
                extruders_count() >= extruder_number)
                set_extruder_for_selected_items(int(extruder_number));
        }
        else
            event.Skip();
    }
    else
        event.Skip();
}
#endif /* __WXOSX__ */

void ObjectList::OnBeginDrag(wxDataViewEvent &event)
{
    const wxDataViewItem item(event.GetItem());

    const bool mult_sel = multiple_selection();

    if ((mult_sel && !selected_instances_of_same_object()) ||
        (!mult_sel && (GetSelection() != item)) ) {
        event.Veto();
        return;
    }
   
    const ItemType& type = m_objects_model->GetItemType(item);
    if (!(type & (itVolume | itObject | itInstance))) {
        event.Veto();
        return;
    }

    if (mult_sel)
    {
        m_dragged_data.init(m_objects_model->GetObjectIdByItem(item),type);
        std::set<int>& sub_obj_idxs = m_dragged_data.inst_idxs();
        wxDataViewItemArray sels;
        GetSelections(sels);
        for (auto sel : sels )
            sub_obj_idxs.insert(m_objects_model->GetInstanceIdByItem(sel));
    }
    else if (type & itObject)
        m_dragged_data.init(m_objects_model->GetIdByItem(item), type);
    else
        m_dragged_data.init(m_objects_model->GetObjectIdByItem(item), 
                            type&itVolume ? m_objects_model->GetVolumeIdByItem(item) :
                                        m_objects_model->GetInstanceIdByItem(item), 
                            type);

    /* Under MSW or OSX, DnD moves an item to the place of another selected item
    * But under GTK, DnD moves an item between another two items.
    * And as a result - call EVT_CHANGE_SELECTION to unselect all items.
    * To prevent such behavior use m_prevent_list_events
    **/
    m_prevent_list_events = true;//it's needed for GTK

    /* Under GTK, DnD requires to the wxTextDataObject been initialized with some valid value,
     * so set some nonempty string
     */
    wxTextDataObject* obj = new wxTextDataObject;
    obj->SetText("Some text");//it's needed for GTK

    event.SetDataObject(obj);
    event.SetDragFlags(wxDrag_DefaultMove); // allows both copy and move;
}

bool ObjectList::can_drop(const wxDataViewItem& item) const 
{
    // move instance(s) or object on "empty place" of ObjectList
    if ( (m_dragged_data.type() & (itInstance | itObject)) && !item.IsOk() )
        return true;

    // type of moved item should be the same as a "destination" item
    if (!item.IsOk() || !(m_dragged_data.type() & (itVolume|itObject)) || 
        m_objects_model->GetItemType(item) != m_dragged_data.type() )
        return false;

    // move volumes inside one object only
    if (m_dragged_data.type() & itVolume) {
        if (m_dragged_data.obj_idx() != m_objects_model->GetObjectIdByItem(item))
            return false;
        wxDataViewItem dragged_item = m_objects_model->GetItemByVolumeId(m_dragged_data.obj_idx(), m_dragged_data.sub_obj_idx());
        if (!dragged_item)
            return false;
        ModelVolumeType item_v_type = m_objects_model->GetVolumeType(item);
        ModelVolumeType dragged_item_v_type = m_objects_model->GetVolumeType(dragged_item);

        if (dragged_item_v_type == item_v_type && dragged_item_v_type != ModelVolumeType::MODEL_PART)
            return true;
        if ((wxGetApp().app_config->get("order_volumes") == "1" && dragged_item_v_type != item_v_type) ||   // we can't reorder volumes outside of types
            item_v_type >= ModelVolumeType::SUPPORT_BLOCKER)        // support blockers/enforcers can't change its place
            return false; 

        bool only_one_solid_part = true;
        auto& volumes = (*m_objects)[m_dragged_data.obj_idx()]->volumes;
        for (size_t cnt, id = cnt = 0; id < volumes.size() && cnt < 2; id ++)
            if (volumes[id]->type() == ModelVolumeType::MODEL_PART) {
                if (++cnt > 1)
                    only_one_solid_part = false;
            }

        if (dragged_item_v_type == ModelVolumeType::MODEL_PART) {
            if (only_one_solid_part)
                return false;
            return (m_objects_model->GetVolumeIdByItem(item) == 0 ||
                    (m_dragged_data.sub_obj_idx()==0 && volumes[1]->type() == ModelVolumeType::MODEL_PART) ||
                    (m_dragged_data.sub_obj_idx()!=0 && volumes[0]->type() == ModelVolumeType::MODEL_PART));
        }
        if ((dragged_item_v_type == ModelVolumeType::NEGATIVE_VOLUME || dragged_item_v_type == ModelVolumeType::PARAMETER_MODIFIER)) {
            if (only_one_solid_part)
                return false;
            return m_objects_model->GetVolumeIdByItem(item) != 0;
        }
        
        return false;
    }

    return true;
}

void ObjectList::OnDropPossible(wxDataViewEvent &event)
{
    const wxDataViewItem& item = event.GetItem();

    if (!can_drop(item)) {
        event.Veto();
        m_prevent_list_events = false;
    }
}

void ObjectList::OnDrop(wxDataViewEvent &event)
{
    const wxDataViewItem& item = event.GetItem();

    if (!can_drop(item))
    {
        event.Veto();
        m_dragged_data.clear();
        return;
    }

    if (m_dragged_data.type() == itInstance)
    {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(),_(L("Instances to Separated Objects")));
        instances_to_separated_object(m_dragged_data.obj_idx(), m_dragged_data.inst_idxs());
        m_dragged_data.clear();
        return;
    }

    take_snapshot(_((m_dragged_data.type() == itVolume) ? L("Volumes in Object reordered") : L("Object reordered")));

    if (m_dragged_data.type() & itVolume)
    {
        int from_volume_id = m_dragged_data.sub_obj_idx();
        int to_volume_id   = m_objects_model->GetVolumeIdByItem(item);
        int delta = to_volume_id < from_volume_id ? -1 : 1;

        auto& volumes = (*m_objects)[m_dragged_data.obj_idx()]->volumes;

        int cnt = 0;
        for (int id = from_volume_id; cnt < abs(from_volume_id - to_volume_id); id += delta, cnt++)
            std::swap(volumes[id], volumes[id + delta]);

        select_item(m_objects_model->ReorganizeChildren(from_volume_id, to_volume_id, m_objects_model->GetParent(item)));

    }
    else if (m_dragged_data.type() & itObject)
    {
        int from_obj_id = m_dragged_data.obj_idx();
        int to_obj_id   = item.IsOk() ? m_objects_model->GetIdByItem(item) : ((int)m_objects->size()-1);
        int delta = to_obj_id < from_obj_id ? -1 : 1;

        int cnt = 0;
        for (int id = from_obj_id; cnt < abs(from_obj_id - to_obj_id); id += delta, cnt++)
            std::swap((*m_objects)[id], (*m_objects)[id + delta]);

        select_item(m_objects_model->ReorganizeObjects(from_obj_id, to_obj_id));
    }

    changed_object(m_dragged_data.obj_idx());

    m_dragged_data.clear();

    wxGetApp().plater()->set_current_canvas_as_dirty();
}

void ObjectList::add_category_to_settings_from_selection(const std::vector< std::pair<std::string, bool> >& category_options, wxDataViewItem item)
{
    if (category_options.empty())
        return;

    const ItemType item_type = m_objects_model->GetItemType(item);

    if (!m_config)
        m_config = &get_item_config(item);

    assert(m_config);
    auto opt_keys = m_config->keys();

    const wxString snapshot_text =  item_type & itLayer   ? _L("Add Settings for Layers") :
                                    item_type & itVolume  ? _L("Add Settings for Sub-object") :
                                                            _L("Add Settings for Object");
    take_snapshot(snapshot_text);

    const DynamicPrintConfig& from_config = printer_technology() == ptFFF ? 
                                            wxGetApp().preset_bundle->prints.get_edited_preset().config : 
                                            wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;

    for (auto& opt : category_options) {
        auto& opt_key = opt.first;
        if (find(opt_keys.begin(), opt_keys.end(), opt_key) != opt_keys.end() && !opt.second)
            m_config->erase(opt_key);

        if (find(opt_keys.begin(), opt_keys.end(), opt_key) == opt_keys.end() && opt.second) {
            const ConfigOption* option = from_config.option(opt_key);
            if (!option) {
                // if current option doesn't exist in prints.get_edited_preset(),
                // get it from default config values
                option = DynamicPrintConfig::new_from_defaults_keys({ opt_key })->option(opt_key);
            }
            m_config->set_key_value(opt_key, option->clone());
        }
    }

    // Add settings item for object/sub-object and show them 
    if (!(item_type & (itObject | itVolume | itLayer)))
        item = m_objects_model->GetTopParent(item);
    show_settings(add_settings_item(item, &m_config->get()));
}

void ObjectList::add_category_to_settings_from_frequent(const std::vector<std::string>& options, wxDataViewItem item)
{
    const ItemType item_type = m_objects_model->GetItemType(item);

    if (!m_config)
        m_config = &get_item_config(item);

    assert(m_config);
    auto opt_keys = m_config->keys();

    const wxString snapshot_text = item_type & itLayer  ? _L("Add Settings Bundle for Height range") :
                                   item_type & itVolume ? _L("Add Settings Bundle for Sub-object") :
                                                          _L("Add Settings Bundle for Object");
    take_snapshot(snapshot_text);

    const DynamicPrintConfig& from_config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    for (auto& opt_key : options)
    {
        if (find(opt_keys.begin(), opt_keys.end(), opt_key) == opt_keys.end()) {
            const ConfigOption* option = from_config.option(opt_key);
            if (!option) {
                // if current option doesn't exist in prints.get_edited_preset(),
                // get it from default config values
                option = DynamicPrintConfig::new_from_defaults_keys({ opt_key })->option(opt_key);
            }
            m_config->set_key_value(opt_key, option->clone());
        }
    }

    // Add settings item for object/sub-object and show them 
    if (!(item_type & (itObject | itVolume | itLayer)))
        item = m_objects_model->GetTopParent(item);
    show_settings(add_settings_item(item, &m_config->get()));
}

void ObjectList::show_settings(const wxDataViewItem settings_item)
{
    if (!settings_item)
        return;

    select_item(settings_item);
    
    // update object selection on Plater
    if (!m_prevent_canvas_selection_update)
        update_selections_on_canvas();
}

bool ObjectList::is_instance_or_object_selected()
{
    const Selection& selection = scene_selection();
    return selection.is_single_full_instance() || selection.is_single_full_object();
}

void ObjectList::load_subobject(ModelVolumeType type)
{
    wxDataViewItem item = GetSelection();
    // we can add volumes for Object or Instance
    if (!item || !(m_objects_model->GetItemType(item)&(itObject|itInstance)))
        return;
    const int obj_idx = m_objects_model->GetObjectIdByItem(item);

    if (obj_idx < 0) return;

    // Get object item, if Instance is selected
    if (m_objects_model->GetItemType(item)&itInstance)
        item = m_objects_model->GetItemById(obj_idx);

    take_snapshot(_L("Load Part"));

    std::vector<ModelVolume*> volumes;
    load_part((*m_objects)[obj_idx], volumes, type);
    wxDataViewItemArray items = reorder_volumes_and_get_selection(obj_idx, [volumes](const ModelVolume* volume) {
        return std::find(volumes.begin(), volumes.end(), volume) != volumes.end(); });

    if (type == ModelVolumeType::MODEL_PART)
        // update printable state on canvas
        wxGetApp().plater()->canvas3D()->update_instance_printable_state_for_object((size_t)obj_idx);

    if (items.size() > 1) {
        m_selection_mode = smVolume;
        m_last_selected_item = wxDataViewItem(nullptr);
    }
    select_items(items);

    selection_changed();
}

void ObjectList::load_part(ModelObject* model_object, std::vector<ModelVolume*>& added_volumes, ModelVolumeType type)
{
    wxWindow* parent = wxGetApp().tab_panel()->GetPage(0);

    wxArrayString input_files;
    wxGetApp().import_model(parent, input_files);

    wxProgressDialog dlg(_L("Loading") + dots, "", 100, wxGetApp().plater(), wxPD_AUTO_HIDE);
    wxBusyCursor busy;

    for (size_t i = 0; i < input_files.size(); ++i) {
        std::string input_file = input_files.Item(i).ToUTF8().data();

        dlg.Update(static_cast<int>(100.0f * static_cast<float>(i) / static_cast<float>(input_files.size())),
            _L("Loading file") + ": " + from_path(boost::filesystem::path(input_file).filename()));
        dlg.Fit();

        Model model;
        try {
            model = Model::read_from_file(input_file);
        }
        catch (std::exception &e) {
            auto msg = _L("Error!") + " " + input_file + " : " + e.what() + ".";
            show_error(parent, msg);
            exit(1);
        }

        for (auto object : model.objects) {
            Vec3d delta = Vec3d::Zero();
            if (model_object->origin_translation != Vec3d::Zero()) {
                object->center_around_origin();
                delta = model_object->origin_translation - object->origin_translation;
            }
            for (auto volume : object->volumes) {
                volume->translate(delta);
                auto new_volume = model_object->add_volume(*volume, type);
                new_volume->name = boost::filesystem::path(input_file).filename().string();
                // set a default extruder value, since user can't add it manually
                new_volume->config.set_key_value("extruder", new ConfigOptionInt(0));

                added_volumes.push_back(new_volume);
            }
        }
    }

}

static TriangleMesh create_mesh(const std::string& type_name, const BoundingBoxf3& bb)
{
    TriangleMesh mesh;

    const double side = wxGetApp().plater()->canvas3D()->get_size_proportional_to_max_bed_size(0.1);

    if (type_name == "Box")
        // Sitting on the print bed, left front front corner at (0, 0).
        mesh = make_cube(side, side, side);
    else if (type_name == "Cylinder")
        // Centered around 0, sitting on the print bed.
        // The cylinder has the same volume as the box above.
        mesh = make_cylinder(0.564 * side, side);
    else if (type_name == "Sphere")
        // Centered around 0, half the sphere below the print bed, half above.
        // The sphere has the same volume as the box above.
        mesh = make_sphere(0.62 * side, PI / 18);
    else if (type_name == "Slab")
        // Sitting on the print bed, left front front corner at (0, 0).
        mesh = make_cube(bb.size().x() * 1.5, bb.size().y() * 1.5, bb.size().z() * 0.5);
    mesh.repair();

    return mesh;
}

void ObjectList::load_generic_subobject(const std::string& type_name, const ModelVolumeType type)
{
    if (type == ModelVolumeType::INVALID) {
        load_shape_object(type_name);
        return;
    }

    const int obj_idx = get_selected_obj_idx();
    if (obj_idx < 0) 
        return;

    const Selection& selection = scene_selection();
    assert(obj_idx == selection.get_object_idx());

    /** Any changes of the Object's composition is duplicated for all Object's Instances
      * So, It's enough to take a bounding box of a first selected Instance and calculate Part(generic_subobject) position
      */
    int instance_idx = *selection.get_instance_idxs().begin();
    assert(instance_idx != -1);
    if (instance_idx == -1)
        return;

    take_snapshot(_(L("Add Generic Subobject")));

    // Selected object
    ModelObject  &model_object = *(*m_objects)[obj_idx];
    // Bounding box of the selected instance in world coordinate system including the translation, without modifiers.
    BoundingBoxf3 instance_bb = model_object.instance_bounding_box(instance_idx);

    TriangleMesh mesh = create_mesh(type_name, instance_bb);
    
	// Mesh will be centered when loading.
    ModelVolume *new_volume = model_object.add_volume(std::move(mesh), type);

    if (instance_idx != -1)
    {
        // First (any) GLVolume of the selected instance. They all share the same instance matrix.
        const GLVolume* v = selection.get_volume(*selection.get_volume_idxs().begin());
        // Transform the new modifier to be aligned with the print bed.
		const BoundingBoxf3 mesh_bb = new_volume->mesh().bounding_box();
        new_volume->set_transformation(Geometry::Transformation::volume_to_bed_transformation(v->get_instance_transformation(), mesh_bb));
        // Set the modifier position.
        auto offset = (type_name == "Slab") ?
            // Slab: Lift to print bed
			Vec3d(0., 0., 0.5 * mesh_bb.size().z() + instance_bb.min.z() - v->get_instance_offset().z()) :
            // Translate the new modifier to be pickable: move to the left front corner of the instance's bounding box, lift to print bed.
            Vec3d(instance_bb.max(0), instance_bb.min(1), instance_bb.min(2)) + 0.5 * mesh_bb.size() - v->get_instance_offset();
        new_volume->set_offset(v->get_instance_transformation().get_matrix(true).inverse() * offset);
    }

    const wxString name = _(L("Generic")) + "-" + _(type_name);
    new_volume->name = into_u8(name);
    // set a default extruder value, since user can't add it manually
    new_volume->config.set_key_value("extruder", new ConfigOptionInt(0));

    select_item([this, obj_idx, new_volume]() {
        wxDataViewItem sel_item;

        wxDataViewItemArray items = reorder_volumes_and_get_selection(obj_idx, [new_volume](const ModelVolume* volume) { return volume == new_volume; });
        if (!items.IsEmpty())
            sel_item = items.front();

        return sel_item;
    });
    if (type == ModelVolumeType::MODEL_PART)
        // update printable state on canvas
        wxGetApp().plater()->canvas3D()->update_instance_printable_state_for_object((size_t)obj_idx);

    selection_changed();
}

void ObjectList::load_shape_object(const std::string& type_name)
{
    const Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();
    assert(selection.get_object_idx() == -1); // Add nothing is something is selected on 3DScene
    if (selection.get_object_idx() != -1)
        return;

    const int obj_idx = m_objects->size();
    if (obj_idx < 0)
        return;

    take_snapshot(_L("Add Shape"));

    // Create mesh
    BoundingBoxf3 bb;
    TriangleMesh mesh = create_mesh(type_name, bb);
    load_mesh_object(mesh, _L("Shape") + "-" + _(type_name));
#if ENABLE_PROJECT_DIRTY_STATE
    wxGetApp().mainframe->update_title();
#endif // ENABLE_PROJECT_DIRTY_STATE
}

void ObjectList::load_mesh_object(const TriangleMesh &mesh, const wxString &name, bool center)
{   
    // Add mesh to model as a new object
    Model& model = wxGetApp().plater()->model();

#ifdef _DEBUG
    check_model_ids_validity(model);
#endif /* _DEBUG */
    
    std::vector<size_t> object_idxs;
    auto bb = mesh.bounding_box();
    ModelObject* new_object = model.add_object();
    new_object->name = into_u8(name);
    new_object->add_instance(); // each object should have at list one instance
    
    ModelVolume* new_volume = new_object->add_volume(mesh);
    new_object->sort_volumes(wxGetApp().app_config->get("order_volumes") == "1");
    new_volume->name = into_u8(name);
    // set a default extruder value, since user can't add it manually
    new_volume->config.set_key_value("extruder", new ConfigOptionInt(0));
    new_object->invalidate_bounding_box();
    new_object->translate(-bb.center());

    if (center) {
        const BoundingBoxf bed_shape = wxGetApp().plater()->bed_shape_bb();
        new_object->instances[0]->set_offset(Slic3r::to_3d(bed_shape.center().cast<double>(), -new_object->origin_translation(2)));
    } else {
        new_object->instances[0]->set_offset(bb.center());
    }

    new_object->ensure_on_bed();

    object_idxs.push_back(model.objects.size() - 1);
#ifdef _DEBUG
    check_model_ids_validity(model);
#endif /* _DEBUG */
    
    paste_objects_into_list(object_idxs);

#ifdef _DEBUG
    check_model_ids_validity(model);
#endif /* _DEBUG */
}

void ObjectList::del_object(const int obj_idx)
{
    wxGetApp().plater()->delete_object_from_model(obj_idx);
}

// Delete subobject
void ObjectList::del_subobject_item(wxDataViewItem& item)
{
    if (!item) return;

    int obj_idx, idx;
    ItemType type;

    m_objects_model->GetItemInfo(item, type, obj_idx, idx);
    if (type == itUndef)
        return;

    wxDataViewItem parent = m_objects_model->GetParent(item);

    if (type & itSettings)
        del_settings_from_config(parent);
    else if (type & itInstanceRoot && obj_idx != -1)
        del_instances_from_object(obj_idx);
    else if (type & itLayerRoot && obj_idx != -1)
        del_layers_from_object(obj_idx);
    else if (type & itLayer && obj_idx != -1)
        del_layer_from_object(obj_idx, m_objects_model->GetLayerRangeByItem(item));
    else if (type & itInfo && obj_idx != -1) {
        Unselect(item);
        Select(parent);
    }
    else if (idx == -1)
        return;
    else if (!del_subobject_from_object(obj_idx, idx, type))
        return;

    // If last volume item with warning was deleted, unmark object item
    if (type & itVolume && (*m_objects)[obj_idx]->get_mesh_errors_count() == 0)
        m_objects_model->DeleteWarningIcon(parent);

    m_objects_model->Delete(item);
    update_info_items(obj_idx);
}

void ObjectList::del_settings_from_config(const wxDataViewItem& parent_item)
{
    const bool is_layer_settings = m_objects_model->GetItemType(parent_item) == itLayer;

    const size_t opt_cnt = m_config->keys().size();
    if ((opt_cnt == 1 && m_config->has("extruder")) ||
        (is_layer_settings && opt_cnt == 2 && m_config->has("extruder") && m_config->has("layer_height")))
        return;

    take_snapshot(_(L("Delete Settings")));

    int extruder = m_config->has("extruder") ? m_config->extruder() : -1;

    coordf_t layer_height = 0.0;
    if (is_layer_settings)
        layer_height = m_config->opt_float("layer_height");

    m_config->clear();

    if (extruder >= 0)
        m_config->set_key_value("extruder", new ConfigOptionInt(extruder));
    if (is_layer_settings)
        m_config->set_key_value("layer_height", new ConfigOptionFloat(layer_height));

    changed_object();
}

void ObjectList::del_instances_from_object(const int obj_idx)
{
    auto& instances = (*m_objects)[obj_idx]->instances;
    if (instances.size() <= 1)
        return;

    take_snapshot(_(L("Delete All Instances from Object")));

    while ( instances.size()> 1)
        instances.pop_back();

    (*m_objects)[obj_idx]->invalidate_bounding_box(); // ? #ys_FIXME

    changed_object(obj_idx);
}

void ObjectList::del_layer_from_object(const int obj_idx, const t_layer_height_range& layer_range)
{
    const auto del_range = object(obj_idx)->layer_config_ranges.find(layer_range);
    if (del_range == object(obj_idx)->layer_config_ranges.end())
        return;

    take_snapshot(_(L("Delete Height Range")));
        
    object(obj_idx)->layer_config_ranges.erase(del_range);

    changed_object(obj_idx);
}

void ObjectList::del_layers_from_object(const int obj_idx)
{
    object(obj_idx)->layer_config_ranges.clear();

    changed_object(obj_idx);
}

bool ObjectList::del_subobject_from_object(const int obj_idx, const int idx, const int type)
{
    assert(idx >= 0);
	if (obj_idx == 1000 || idx<0)
		// Cannot delete a wipe tower or volume with negative id
		return false;

    ModelObject* object = (*m_objects)[obj_idx];

    if (type == itVolume) {
        const auto volume = object->volumes[idx];

        // if user is deleting the last solid part, throw error
        int solid_cnt = 0;
        for (auto vol : object->volumes)
            if (vol->is_model_part())
                ++solid_cnt;
        if (volume->is_model_part() && solid_cnt == 1) {
            Slic3r::GUI::show_error(nullptr, _(L("From Object List You can't delete the last solid part from object.")));
            return false;
        }

        take_snapshot(_(L("Delete Subobject")));

        object->delete_volume(idx);

        if (object->volumes.size() == 1)
        {
            const auto last_volume = object->volumes[0];
            if (!last_volume->config.empty()) {
                object->config.apply(last_volume->config);
                last_volume->config.clear();

                // update extruder color in ObjectList
                wxDataViewItem obj_item = m_objects_model->GetItemById(obj_idx);
                if (obj_item) {
                    wxString extruder = object->config.has("extruder") ? wxString::Format("%d", object->config.extruder()) : _L("default");
                    m_objects_model->SetExtruder(extruder, obj_item);
                }
                // add settings to the object, if it has them
                add_settings_item(obj_item, &object->config.get());
            }
        }
    }
    else if (type == itInstance) {
        if (object->instances.size() == 1) {
            Slic3r::GUI::show_error(nullptr, _(L("Last instance of an object cannot be deleted.")));
            return false;
        }

        take_snapshot(_(L("Delete Instance")));
        object->delete_instance(idx);
    }
    else
        return false;

    changed_object(obj_idx);

    return true;
}

void ObjectList::split()
{
    const auto item = GetSelection();
    const int obj_idx = get_selected_obj_idx();
    if (!item || obj_idx < 0)
        return;

    ModelVolume* volume;
    if (!get_volume_by_item(item, volume)) return;
    DynamicPrintConfig&	config = printer_config();
	const ConfigOption *nozzle_dmtrs_opt = config.option("nozzle_diameter", false);
	const auto nozzle_dmrs_cnt = (nozzle_dmtrs_opt == nullptr) ? size_t(1) : dynamic_cast<const ConfigOptionFloats*>(nozzle_dmtrs_opt)->values.size();
    if (!volume->is_splittable()) {
        wxMessageBox(_(L("The selected object couldn't be split because it contains only one part.")));
        return;
    }

    take_snapshot(_(L("Split to Parts")));

    volume->split(nozzle_dmrs_cnt);

    wxBusyCursor wait;

    auto model_object = (*m_objects)[obj_idx];

    auto parent = m_objects_model->GetTopParent(item);
    if (parent)
        m_objects_model->DeleteVolumeChildren(parent);
    else
        parent = item;

    for (const ModelVolume* volume : model_object->volumes) {
        const wxDataViewItem& vol_item = m_objects_model->AddVolumeChild(parent, from_u8(volume->name),
            volume->type(),// is_modifier() ? ModelVolumeType::PARAMETER_MODIFIER : ModelVolumeType::MODEL_PART,
            volume->get_mesh_errors_count()>0,
            volume->config.has("extruder") ? volume->config.extruder() : 0,
            false);
        // add settings to the part, if it has those
        add_settings_item(vol_item, &volume->config.get());
    }

    model_object->input_file.clear();

    if (parent == item)
        Expand(parent);

    changed_object(obj_idx);
}

void ObjectList::merge(bool to_multipart_object)
{
    // merge selected objects to the multipart object
    if (to_multipart_object)
    {
        auto get_object_idxs = [this](std::vector<int>& obj_idxs, wxDataViewItemArray& sels)
        {
            // check selections and split instances to the separated objects...
            bool instance_selection = false;
            for (wxDataViewItem item : sels)
                if (m_objects_model->GetItemType(item) & itInstance) {
                    instance_selection = true;
                    break;
                }

            if (!instance_selection)
            {
                for (wxDataViewItem item : sels) {
                    assert(m_objects_model->GetItemType(item) & itObject);
                    obj_idxs.emplace_back(m_objects_model->GetIdByItem(item));
                }
                return;
            }

            // map of obj_idx -> set of selected instance_idxs
            std::map<int, std::set<int>> sel_map;
            std::set<int> empty_set;
            for (wxDataViewItem item : sels) {
                if (m_objects_model->GetItemType(item) & itObject)
                {
                    int obj_idx = m_objects_model->GetIdByItem(item);
                    int inst_cnt = (*m_objects)[obj_idx]->instances.size();
                    if (inst_cnt == 1)
                        sel_map.emplace(obj_idx, empty_set);
                    else
                        for (int i = 0; i < inst_cnt; i++)
                            sel_map[obj_idx].emplace(i);
                    continue;
                }
                int obj_idx = m_objects_model->GetIdByItem(m_objects_model->GetTopParent(item));
                sel_map[obj_idx].emplace(m_objects_model->GetInstanceIdByItem(item));
            }

            // all objects, created from the instances will be added to the end of list
            int new_objects_cnt = 0; // count of this new objects

            for (auto map_item : sel_map)
            {
                int obj_idx = map_item.first;
                // object with just 1 instance
                if (map_item.second.empty()) {
                    obj_idxs.emplace_back(obj_idx);
                    continue;
                }

                // object with selected all instances
                if ((*m_objects)[map_item.first]->instances.size() == map_item.second.size()) {
                    instances_to_separated_objects(obj_idx);
                    // first instance stay on its own place and another all add to the end of list :
                    obj_idxs.emplace_back(obj_idx);
                    new_objects_cnt += map_item.second.size() - 1;
                    continue;
                }

                // object with selected some of instances 
                instances_to_separated_object(obj_idx, map_item.second);

                if (map_item.second.size() == 1)
                    new_objects_cnt += 1;
                else {// we should split to separate instances last object
                    instances_to_separated_objects(m_objects->size() - 1);
                    // all instances will stay at the end of list :
                    new_objects_cnt += map_item.second.size();
                }
            }

            // all instatnces are extracted to the separate objects and should be selected
            m_prevent_list_events = true;
            sels.Clear();
            for (int obj_idx : obj_idxs)
                sels.Add(m_objects_model->GetItemById(obj_idx));
            int obj_cnt = m_objects->size();
            for (int obj_idx = obj_cnt - new_objects_cnt; obj_idx < obj_cnt; obj_idx++) {
                sels.Add(m_objects_model->GetItemById(obj_idx));
                obj_idxs.emplace_back(obj_idx);
            }
            UnselectAll();
            SetSelections(sels);
            assert(!sels.IsEmpty());
            m_prevent_list_events = false;
        };

        std::vector<int> obj_idxs;
        wxDataViewItemArray sels;
        GetSelections(sels);
        assert(!sels.IsEmpty());

        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _L("Merge"));

        get_object_idxs(obj_idxs, sels);

        // resulted objects merge to the one
        Model* model = (*m_objects)[0]->get_model();
        ModelObject* new_object = model->add_object();
        new_object->name = _u8L("Merged");
        ModelConfig &config = new_object->config;

        for (int obj_idx : obj_idxs)
        {
            ModelObject* object = (*m_objects)[obj_idx];

            const Geometry::Transformation& transformation = object->instances[0]->get_transformation();
            Vec3d scale     = transformation.get_scaling_factor();
            Vec3d mirror    = transformation.get_mirror();
            Vec3d rotation  = transformation.get_rotation();

            if (object->id() == (*m_objects)[obj_idxs.front()]->id())
                new_object->add_instance();
            Transform3d     volume_offset_correction = new_object->instances[0]->get_transformation().get_matrix().inverse() * transformation.get_matrix();

            // merge volumes
            for (const ModelVolume* volume : object->volumes) {
                ModelVolume* new_volume = new_object->add_volume(*volume);

                //set rotation
                Vec3d vol_rot = new_volume->get_rotation() + rotation;
                new_volume->set_rotation(vol_rot);

                // set scale
                Vec3d vol_sc_fact = new_volume->get_scaling_factor().cwiseProduct(scale);
                new_volume->set_scaling_factor(vol_sc_fact);

                // set mirror
                Vec3d vol_mirror = new_volume->get_mirror().cwiseProduct(mirror);
                new_volume->set_mirror(vol_mirror);

                // set offset
                Vec3d vol_offset = volume_offset_correction* new_volume->get_offset();
                new_volume->set_offset(vol_offset);
            }
            new_object->sort_volumes(wxGetApp().app_config->get("order_volumes") == "1");

            // merge settings
            auto new_opt_keys = config.keys();
            const ModelConfig& from_config = object->config;
            auto opt_keys = from_config.keys();

            for (auto& opt_key : opt_keys) {
                if (find(new_opt_keys.begin(), new_opt_keys.end(), opt_key) == new_opt_keys.end()) {
                    const ConfigOption* option = from_config.option(opt_key);
                    if (!option) {
                        // if current option doesn't exist in prints.get_edited_preset(),
                        // get it from default config values
                        option = DynamicPrintConfig::new_from_defaults_keys({ opt_key })->option(opt_key);
                    }
                    config.set_key_value(opt_key, option->clone());
                }
            }
            // save extruder value if it was set
            if (object->volumes.size() == 1 && find(opt_keys.begin(), opt_keys.end(), "extruder") != opt_keys.end()) {
                ModelVolume* volume = new_object->volumes.back();
                const ConfigOption* option = from_config.option("extruder");
                if (option)
                    volume->config.set_key_value("extruder", option->clone());
            }

            // merge layers
            for (const auto& range : object->layer_config_ranges)
                new_object->layer_config_ranges.emplace(range);
        }
        // remove selected objects
        remove();

        // Add new object(merged) to the object_list
        add_object_to_list(m_objects->size() - 1);
        select_item(m_objects_model->GetItemById(m_objects->size() - 1));
        update_selections_on_canvas();
    }
    // merge all parts to the one single object
    // all part's settings will be lost
    else
    {
        wxDataViewItem item = GetSelection();
        if (!item)
            return;
        const int obj_idx = m_objects_model->GetIdByItem(item);
        if (obj_idx == -1)
            return;

        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _L("Merge all parts to the one single object"));

        ModelObject* model_object = (*m_objects)[obj_idx];
        model_object->merge();

        m_objects_model->DeleteVolumeChildren(item);

        changed_object(obj_idx);
    }
}

void ObjectList::layers_editing()
{
    const Selection& selection = scene_selection();
    const int obj_idx = selection.get_object_idx();
    wxDataViewItem item = obj_idx >= 0 && GetSelectedItemsCount() > 1 && selection.is_single_full_object() ? 
                          m_objects_model->GetItemById(obj_idx) :
                          GetSelection();

    if (!item)
        return;

    const wxDataViewItem obj_item = m_objects_model->GetTopParent(item);
    wxDataViewItem layers_item = m_objects_model->GetLayerRootItem(obj_item);

    // if it doesn't exist now
    if (!layers_item.IsOk())
    {
        t_layer_config_ranges& ranges = object(obj_idx)->layer_config_ranges;

        // set some default value
        if (ranges.empty()) {
            take_snapshot(_(L("Add Layers")));
            ranges[{ 0.0f, 2.0f }].assign_config(get_default_layer_config(obj_idx));
        }

        // create layer root item
        layers_item = add_layer_root_item(obj_item);
    }
    if (!layers_item.IsOk())
        return;

    // to correct visual hints for layers editing on the Scene, reset previous selection
    wxGetApp().obj_layers()->reset_selection();
    wxGetApp().plater()->canvas3D()->handle_sidebar_focus_event("", false);

    // select LayerRoor item and expand
    select_item(layers_item);
    Expand(layers_item);
}

wxDataViewItem ObjectList::add_layer_root_item(const wxDataViewItem obj_item)
{
    const int obj_idx = m_objects_model->GetIdByItem(obj_item);
    if (obj_idx < 0 || 
        object(obj_idx)->layer_config_ranges.empty() ||
        printer_technology() == ptSLA)
        return wxDataViewItem(nullptr);

    // create LayerRoot item
    wxDataViewItem layers_item = m_objects_model->AddLayersRoot(obj_item);

    // and create Layer item(s) according to the layer_config_ranges
    for (const auto& range : object(obj_idx)->layer_config_ranges)
        add_layer_item(range.first, layers_item);

    Expand(layers_item);
    return layers_item;
}

DynamicPrintConfig ObjectList::get_default_layer_config(const int obj_idx)
{
    DynamicPrintConfig config;
    coordf_t layer_height = object(obj_idx)->config.has("layer_height") ? 
                            object(obj_idx)->config.opt_float("layer_height") : 
                            wxGetApp().preset_bundle->prints.get_edited_preset().config.opt_float("layer_height");
    config.set_key_value("layer_height",new ConfigOptionFloat(layer_height));
    config.set_key_value("extruder",    new ConfigOptionInt(0));

    return config;
}

bool ObjectList::get_volume_by_item(const wxDataViewItem& item, ModelVolume*& volume)
{
    auto obj_idx = get_selected_obj_idx();
    if (!item || obj_idx < 0)
        return false;
    const auto volume_id = m_objects_model->GetVolumeIdByItem(item);
    const bool split_part = m_objects_model->GetItemType(item) == itVolume;

    // object is selected
    if (volume_id < 0) {
        if ( split_part || (*m_objects)[obj_idx]->volumes.size() > 1 ) 
            return false;
        volume = (*m_objects)[obj_idx]->volumes[0];
    }
    // volume is selected
    else
        volume = (*m_objects)[obj_idx]->volumes[volume_id];
    
    return true;
}

bool ObjectList::is_splittable(bool to_objects)
{
    const wxDataViewItem item = GetSelection();
    if (!item) return false;

    if (to_objects)
    {
        ItemType type = m_objects_model->GetItemType(item);
        if (type == itVolume)
            return false;
        if (type == itObject || m_objects_model->GetItemType(m_objects_model->GetParent(item)) == itObject) {
            auto obj_idx = get_selected_obj_idx();
            if (obj_idx < 0)
                return false;
            if ((*m_objects)[obj_idx]->volumes.size() > 1)
                return true;
            return (*m_objects)[obj_idx]->volumes[0]->is_splittable();
        }
        return false;
    }

    ModelVolume* volume;
    if (!get_volume_by_item(item, volume) || !volume)
        return false;

    return volume->is_splittable();
}

bool ObjectList::selected_instances_of_same_object()
{
    wxDataViewItemArray sels;
    GetSelections(sels);

    const int obj_idx = m_objects_model->GetObjectIdByItem(sels.front());

    for (auto item : sels) {
        if (! (m_objects_model->GetItemType(item) & itInstance) ||
            obj_idx != m_objects_model->GetObjectIdByItem(item))
            return false;
    }
    return true;
}

bool ObjectList::can_split_instances()
{
    const Selection& selection = scene_selection();
    return selection.is_multiple_full_instance() || selection.is_single_full_instance();
}

bool ObjectList::can_merge_to_multipart_object() const
{
    if (printer_technology() == ptSLA)
        return false;

    wxDataViewItemArray sels;
    GetSelections(sels);
    if (sels.IsEmpty())
        return false;

    // should be selected just objects
    for (wxDataViewItem item : sels)
        if (!(m_objects_model->GetItemType(item) & (itObject | itInstance)))
            return false;

    return true;
}

bool ObjectList::can_merge_to_single_object() const
{
    int obj_idx = get_selected_obj_idx();
    if (obj_idx < 0)
        return false;

    // selected object should be multipart
    return (*m_objects)[obj_idx]->volumes.size() > 1;
}

// NO_PARAMETERS function call means that changed object index will be determine from Selection() 
void ObjectList::changed_object(const int obj_idx/* = -1*/) const 
{
    wxGetApp().plater()->changed_object(obj_idx < 0 ? get_selected_obj_idx() : obj_idx);
}

void ObjectList::part_selection_changed()
{
    if (m_extruder_editor) m_extruder_editor->Hide();
    int obj_idx = -1;
    int volume_id = -1;
    m_config = nullptr;
    wxString og_name = wxEmptyString;

    bool update_and_show_manipulations = false;
    bool update_and_show_settings = false;
    bool update_and_show_layers = false;

    const auto item = GetSelection();

    if ( multiple_selection() || (item && m_objects_model->GetItemType(item) == itInstanceRoot )) {
        og_name = _L("Group manipulation");

        const Selection& selection = scene_selection();
        // don't show manipulation panel for case of all Object's parts selection 
        update_and_show_manipulations = !selection.is_single_full_instance();
    }
    else {
        if (item) {
            const ItemType type = m_objects_model->GetItemType(item);
            const wxDataViewItem parent = m_objects_model->GetParent(item);
            const ItemType parent_type = m_objects_model->GetItemType(parent);
            obj_idx = m_objects_model->GetObjectIdByItem(item);

            if (parent == wxDataViewItem(nullptr)
             || type == itInfo) {
                og_name = _L("Object manipulation");
                m_config = &(*m_objects)[obj_idx]->config;
                update_and_show_manipulations = true;

                if (type == itInfo) {
                    InfoItemType info_type = m_objects_model->GetInfoItemType(item);
                    if (info_type != InfoItemType::VariableLayerHeight) {
                        GLGizmosManager::EType gizmo_type = info_type == InfoItemType::CustomSupports ? GLGizmosManager::EType::FdmSupports :
                                                            info_type == InfoItemType::CustomSeam     ? GLGizmosManager::EType::Seam :
                                                                                                        GLGizmosManager::EType::MmuSegmentation;
                        GLGizmosManager& gizmos_mgr = wxGetApp().plater()->canvas3D()->get_gizmos_manager();
                        if (gizmos_mgr.get_current_type() != gizmo_type)
                            gizmos_mgr.open_gizmo(gizmo_type);
                    } else
                        wxGetApp().plater()->toggle_layers_editing(true);
                }
            }
            else {
                if (type & itSettings) {
                    if (parent_type & itObject) {
                        og_name = _L("Object Settings to modify");
                        m_config = &(*m_objects)[obj_idx]->config;
                    }
                    else if (parent_type & itVolume) {
                        og_name = _L("Part Settings to modify");
                        volume_id = m_objects_model->GetVolumeIdByItem(parent);
                        m_config = &(*m_objects)[obj_idx]->volumes[volume_id]->config;
                    }
                    else if (parent_type & itLayer) {
                        og_name = _L("Layer range Settings to modify");
                        m_config = &get_item_config(parent);
                    }
                    update_and_show_settings = true;
                }
                else if (type & itVolume) {
                    og_name = _L("Part manipulation");
                    volume_id = m_objects_model->GetVolumeIdByItem(item);
                    m_config = &(*m_objects)[obj_idx]->volumes[volume_id]->config;
                    update_and_show_manipulations = true;
                }
                else if (type & itInstance) {
                    og_name = _L("Instance manipulation");
                    update_and_show_manipulations = true;

                    // fill m_config by object's values
                    m_config = &(*m_objects)[obj_idx]->config;
                }
                else if (type & (itLayerRoot|itLayer)) {
                    og_name = type & itLayerRoot ? _L("Height ranges") : _L("Settings for height range");
                    update_and_show_layers = true;

                    if (type & itLayer)
                        m_config = &get_item_config(item);
                }
            }
        }
    }

    m_selected_object_id = obj_idx;

    if (update_and_show_manipulations) {
        wxGetApp().obj_manipul()->get_og()->set_name(" " + og_name + " ");

        if (item) {
            // wxGetApp().obj_manipul()->get_og()->set_value("object_name", m_objects_model->GetName(item));
            wxGetApp().obj_manipul()->update_item_name(m_objects_model->GetName(item));
            wxGetApp().obj_manipul()->update_warning_icon_state(get_mesh_errors_list(obj_idx, volume_id));
        }
    }

    if (update_and_show_settings)
        wxGetApp().obj_settings()->get_og()->set_name(" " + og_name + " ");

    if (printer_technology() == ptSLA)
        update_and_show_layers = false;
    else if (update_and_show_layers)
        wxGetApp().obj_layers()->get_og()->set_name(" " + og_name + " ");

    update_min_height();

    Sidebar& panel = wxGetApp().sidebar();
    panel.Freeze();

    wxGetApp().plater()->canvas3D()->handle_sidebar_focus_event("", false);
    wxGetApp().obj_manipul() ->UpdateAndShow(update_and_show_manipulations);
    wxGetApp().obj_settings()->UpdateAndShow(update_and_show_settings);
    wxGetApp().obj_layers()  ->UpdateAndShow(update_and_show_layers);
    wxGetApp().sidebar().show_info_sizer();

    panel.Layout();
    panel.Thaw();
}

// Add new SettingsItem for parent_item if it doesn't exist, or just update a digest according to new config
wxDataViewItem ObjectList::add_settings_item(wxDataViewItem parent_item, const DynamicPrintConfig* config)
{
    wxDataViewItem ret = wxDataViewItem(nullptr);

    if (!parent_item)
        return ret;

    const bool is_object_settings = m_objects_model->GetItemType(parent_item) == itObject;
    if (!is_object_settings) {
        ModelVolumeType volume_type = m_objects_model->GetVolumeType(parent_item);
        if (volume_type == ModelVolumeType::NEGATIVE_VOLUME || volume_type == ModelVolumeType::SUPPORT_BLOCKER || volume_type == ModelVolumeType::SUPPORT_ENFORCER)
            return ret;
    }

    SettingsFactory::Bundle cat_options = SettingsFactory::get_bundle(config, is_object_settings);
    if (cat_options.empty())
        return ret;

    std::vector<std::string> categories;
    categories.reserve(cat_options.size());
    for (auto& cat : cat_options)
        categories.push_back(cat.first);

    if (m_objects_model->GetItemType(parent_item) & itInstance)
        parent_item = m_objects_model->GetTopParent(parent_item);

    ret = m_objects_model->IsSettingsItem(parent_item) ? parent_item : m_objects_model->GetSettingsItem(parent_item);

    if (!ret) ret = m_objects_model->AddSettingsChild(parent_item);

    m_objects_model->UpdateSettingsDigest(ret, categories);
    Expand(parent_item);

    return ret;
}


void ObjectList::update_info_items(size_t obj_idx)
{
    const ModelObject* model_object = (*m_objects)[obj_idx];
    wxDataViewItem item_obj = m_objects_model->GetItemById(obj_idx);
    assert(item_obj.IsOk());

    for (InfoItemType type : {InfoItemType::CustomSupports,
                              InfoItemType::CustomSeam,
                              InfoItemType::MmuSegmentation,
                              InfoItemType::VariableLayerHeight}) {
        wxDataViewItem item = m_objects_model->GetInfoItemByType(item_obj, type);
        bool shows = item.IsOk();
        bool should_show = false;

        switch (type) {
        case InfoItemType::CustomSupports :
        case InfoItemType::CustomSeam :
        case InfoItemType::MmuSegmentation :
            should_show = printer_technology() == ptFFF
                       && std::any_of(model_object->volumes.begin(), model_object->volumes.end(),
                                      [type](const ModelVolume *mv) {
                                          return !(type == InfoItemType::CustomSupports ? mv->supported_facets.empty() :
                                                   type == InfoItemType::CustomSeam     ? mv->seam_facets.empty() :
                                                                                          mv->mmu_segmentation_facets.empty());
                                      });
            break;

        case InfoItemType::VariableLayerHeight :
            should_show = printer_technology() == ptFFF
                       && ! model_object->layer_height_profile.empty();
            break;
        default: break;
        }

        if (! shows && should_show) {
            m_objects_model->AddInfoChild(item_obj, type);
            Expand(item_obj);
        }
        else if (shows && ! should_show) {
            Unselect(item);
            m_objects_model->Delete(item);
            Select(item_obj);
        }
    }
}



void ObjectList::add_object_to_list(size_t obj_idx, bool call_selection_changed)
{
    auto model_object = (*m_objects)[obj_idx];
    const wxString& item_name = from_u8(model_object->name);
    const auto item = m_objects_model->Add(item_name,
                      model_object->config.has("extruder") ? model_object->config.extruder() : 0,
                      get_mesh_errors_count(obj_idx) > 0);

    update_info_items(obj_idx);

    // add volumes to the object
    if (model_object->volumes.size() > 1) {
        for (const ModelVolume* volume : model_object->volumes) {
            const wxDataViewItem& vol_item = m_objects_model->AddVolumeChild(item,
                from_u8(volume->name),
                volume->type(),
                volume->get_mesh_errors_count()>0,
                volume->config.has("extruder") ? volume->config.extruder() : 0,
                false);
            add_settings_item(vol_item, &volume->config.get());
        }
        Expand(item);
    }

    // add instances to the object, if it has those
    if (model_object->instances.size()>1)
    {
        std::vector<bool> print_idicator(model_object->instances.size());
        for (size_t i = 0; i < model_object->instances.size(); ++i)
            print_idicator[i] = model_object->instances[i]->printable;

        const wxDataViewItem object_item = m_objects_model->GetItemById(obj_idx);
        m_objects_model->AddInstanceChild(object_item, print_idicator);
        Expand(m_objects_model->GetInstanceRootItem(object_item));
    }
    else
        m_objects_model->SetPrintableState(model_object->instances[0]->printable ? piPrintable : piUnprintable, obj_idx);

    // add settings to the object, if it has those
    add_settings_item(item, &model_object->config.get());

    // Add layers if it has
    add_layer_root_item(item);

#ifndef __WXOSX__ 
    if (call_selection_changed)
	    selection_changed();
#endif //__WXMSW__
}

void ObjectList::delete_object_from_list()
{
    auto item = GetSelection();
    if (!item) 
        return;
    if (m_objects_model->GetParent(item) == wxDataViewItem(nullptr))
        select_item([this, item]() { return m_objects_model->Delete(item); });
    else
        select_item([this, item]() { return m_objects_model->Delete(m_objects_model->GetParent(item)); });
}

void ObjectList::delete_object_from_list(const size_t obj_idx)
{
    select_item([this, obj_idx]() { return m_objects_model->Delete(m_objects_model->GetItemById(obj_idx)); });
}

void ObjectList::delete_volume_from_list(const size_t obj_idx, const size_t vol_idx)
{
    select_item([this, obj_idx, vol_idx]() { return m_objects_model->Delete(m_objects_model->GetItemByVolumeId(obj_idx, vol_idx)); });
}

void ObjectList::delete_instance_from_list(const size_t obj_idx, const size_t inst_idx)
{
    select_item([this, obj_idx, inst_idx]() { return m_objects_model->Delete(m_objects_model->GetItemByInstanceId(obj_idx, inst_idx)); });
}

void ObjectList::delete_from_model_and_list(const ItemType type, const int obj_idx, const int sub_obj_idx)
{
    if ( !(type&(itObject|itVolume|itInstance)) )
        return;

    take_snapshot(_(L("Delete Selected Item")));

    if (type&itObject) {
        del_object(obj_idx);
        delete_object_from_list(obj_idx);
    }
    else {
        del_subobject_from_object(obj_idx, sub_obj_idx, type);

        type == itVolume ? delete_volume_from_list(obj_idx, sub_obj_idx) :
            delete_instance_from_list(obj_idx, sub_obj_idx);
    }
}

void ObjectList::delete_from_model_and_list(const std::vector<ItemForDelete>& items_for_delete)
{
    if (items_for_delete.empty())
        return;

    m_prevent_list_events = true;
    for (std::vector<ItemForDelete>::const_reverse_iterator item = items_for_delete.rbegin(); item != items_for_delete.rend(); ++item)
    {
        if (!(item->type&(itObject | itVolume | itInstance)))
            continue;
        if (item->type&itObject) {
            del_object(item->obj_idx);
            m_objects_model->Delete(m_objects_model->GetItemById(item->obj_idx));
        }
        else {
            if (!del_subobject_from_object(item->obj_idx, item->sub_obj_idx, item->type))
                continue;
            if (item->type&itVolume)
            {
                m_objects_model->Delete(m_objects_model->GetItemByVolumeId(item->obj_idx, item->sub_obj_idx));
                if ((*m_objects)[item->obj_idx]->volumes.size() == 1 && 
                    (*m_objects)[item->obj_idx]->config.has("extruder"))
                {
                    const wxString extruder = wxString::Format("%d", (*m_objects)[item->obj_idx]->config.extruder());
                    m_objects_model->SetExtruder(extruder, m_objects_model->GetItemById(item->obj_idx));
                }
                wxGetApp().plater()->canvas3D()->ensure_on_bed(item->obj_idx);
            }
            else
                m_objects_model->Delete(m_objects_model->GetItemByInstanceId(item->obj_idx, item->sub_obj_idx));
        }
    }
    m_prevent_list_events = true;
    part_selection_changed();
}

void ObjectList::delete_all_objects_from_list()
{
    m_prevent_list_events = true;
    this->UnselectAll();
    m_objects_model->DeleteAll();
    m_prevent_list_events = false;
    part_selection_changed();
}

void ObjectList::increase_object_instances(const size_t obj_idx, const size_t num)
{
    select_item([this, obj_idx, num]() { return m_objects_model->AddInstanceChild(m_objects_model->GetItemById(obj_idx), num); });
    selection_changed();
}

void ObjectList::decrease_object_instances(const size_t obj_idx, const size_t num)
{
    select_item([this, obj_idx, num]() { return m_objects_model->DeleteLastInstance(m_objects_model->GetItemById(obj_idx), num); });
}

void ObjectList::unselect_objects()
{
    if (!GetSelection())
        return;

    m_prevent_list_events = true;
    UnselectAll();
    part_selection_changed();
    m_prevent_list_events = false;
}

void ObjectList::select_object_item(bool is_msr_gizmo)
{
    if (wxDataViewItem item = GetSelection()) {
        ItemType type = m_objects_model->GetItemType(item);
        bool is_volume_item = type == itVolume || (type == itSettings && m_objects_model->GetItemType(m_objects_model->GetParent(item)) == itVolume);
        if ((is_msr_gizmo && is_volume_item) || type == itObject)
            return;

        if (wxDataViewItem obj_item = m_objects_model->GetTopParent(item)) {
            m_prevent_list_events = true;
            UnselectAll();
            Select(obj_item);
            part_selection_changed();
            m_prevent_list_events = false;
        }
    }
}

static void update_selection(wxDataViewItemArray& sels, ObjectList::SELECTION_MODE mode, ObjectDataViewModel* model)
{
    if (mode == ObjectList::smInstance)
    {
        for (auto& item : sels)
        {
            ItemType type = model->GetItemType(item);
            if (type == itObject)
                continue;
            if (type == itInstanceRoot) {
                wxDataViewItem obj_item = model->GetParent(item);
                sels.Remove(item);
                sels.Add(obj_item);
                update_selection(sels, mode, model);
                return;
            }
            if (type == itInstance)
            {
                wxDataViewItemArray instances;
                model->GetChildren(model->GetParent(item), instances);
                assert(instances.Count() > 0);
                size_t selected_instances_cnt = 0;
                for (auto& inst : instances) {
                    if (sels.Index(inst) == wxNOT_FOUND)
                        break;
                    selected_instances_cnt++;
                }

                if (selected_instances_cnt == instances.Count()) 
                {
                    wxDataViewItem obj_item = model->GetTopParent(item);
                    for (auto& inst : instances)
                        sels.Remove(inst);
                    sels.Add(obj_item);
                    update_selection(sels, mode, model);
                    return;
                }
            }
            else
                return;
        }
    }
}

void ObjectList::remove()
{
    if (GetSelectedItemsCount() == 0)
        return;

    auto delete_item = [this](wxDataViewItem item)
    {
        wxDataViewItem parent = m_objects_model->GetParent(item);
        ItemType type = m_objects_model->GetItemType(item);
        if (type & itObject)
            delete_from_model_and_list(itObject, m_objects_model->GetIdByItem(item), -1);
        else {
            if (type & (itLayer | itInstance)) {
                // In case there is just one layer or two instances and we delete it, del_subobject_item will
                // also remove the parent item. Selection should therefore pass to the top parent (object).
                wxDataViewItemArray children;
                if (m_objects_model->GetChildren(parent, children) == (type & itLayer ? 1 : 2))
                    parent = m_objects_model->GetTopParent(item);
            }

            del_subobject_item(item);
        }

        return parent;
    };

    wxDataViewItemArray sels;
    GetSelections(sels);

    wxDataViewItem parent = wxDataViewItem(nullptr);

    if (sels.Count() == 1)
        parent = delete_item(GetSelection());
    else
    {
        SELECTION_MODE sels_mode = m_selection_mode;
        UnselectAll();
        update_selection(sels, sels_mode, m_objects_model);

        Plater::TakeSnapshot snapshot = Plater::TakeSnapshot(wxGetApp().plater(), _(L("Delete Selected")));

        for (auto& item : sels)
        {
            if (m_objects_model->InvalidItem(item)) // item can be deleted for this moment (like last 2 Instances or Volumes)
                continue;
            parent = delete_item(item);
        }
    }

    if (parent && !m_objects_model->InvalidItem(parent)) {
        select_item(parent);
        update_selections_on_canvas();
    }
}

void ObjectList::del_layer_range(const t_layer_height_range& range)
{
    const int obj_idx = get_selected_obj_idx();
    if (obj_idx < 0) return;

    t_layer_config_ranges& ranges = object(obj_idx)->layer_config_ranges;

    wxDataViewItem selectable_item = GetSelection();

    if (ranges.size() == 1)
        selectable_item = m_objects_model->GetParent(selectable_item);

    wxDataViewItem layer_item = m_objects_model->GetItemByLayerRange(obj_idx, range);
    del_subobject_item(layer_item);

    select_item(selectable_item);
}

static double get_min_layer_height(const int extruder_idx)
{
    const DynamicPrintConfig& config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    return config.opt_float("min_layer_height", std::max(0, extruder_idx - 1));
}

static double get_max_layer_height(const int extruder_idx)
{
    const DynamicPrintConfig& config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    int extruder_idx_zero_based = std::max(0, extruder_idx - 1);
    double max_layer_height = config.opt_float("max_layer_height", extruder_idx_zero_based);

    // In case max_layer_height is set to zero, it should default to 75 % of nozzle diameter:
    if (max_layer_height < EPSILON)
        max_layer_height = 0.75 * config.opt_float("nozzle_diameter", extruder_idx_zero_based);

    return max_layer_height;
}

// When editing this function, please synchronize the conditions with can_add_new_range_after_current().
void ObjectList::add_layer_range_after_current(const t_layer_height_range current_range)
{
    const int obj_idx = get_selected_obj_idx();
    assert(obj_idx >= 0);
    if (obj_idx < 0) 
        // This should not happen.
        return;

    const wxDataViewItem layers_item = GetSelection();

    auto& ranges = object(obj_idx)->layer_config_ranges;
    auto it_range = ranges.find(current_range);
    assert(it_range != ranges.end());
    if (it_range == ranges.end())
        // This shoudl not happen.
        return;

    auto it_next_range = it_range;
    bool changed = false;
    if (++ it_next_range == ranges.end())
    {
        // Adding a new layer height range after the last one.
        take_snapshot(_(L("Add Height Range")));
        changed = true;

        const t_layer_height_range new_range = { current_range.second, current_range.second + 2. };
        ranges[new_range].assign_config(get_default_layer_config(obj_idx));
        add_layer_item(new_range, layers_item);
    }
    else if (const std::pair<coordf_t, coordf_t> &next_range = it_next_range->first; current_range.second <= next_range.first)
    {
        const int layer_idx = m_objects_model->GetItemIdByLayerRange(obj_idx, next_range);
        assert(layer_idx >= 0);
        if (layer_idx >= 0) 
        {
            if (current_range.second == next_range.first)
            {
                // Splitting the next layer height range to two.
                const auto old_config = ranges.at(next_range);
                const coordf_t delta = next_range.second - next_range.first;
                // Layer height of the current layer.
                const coordf_t old_min_layer_height = get_min_layer_height(old_config.opt_int("extruder"));
                // Layer height of the layer to be inserted.
                const coordf_t new_min_layer_height = get_min_layer_height(0);
                if (delta >= old_min_layer_height + new_min_layer_height - EPSILON) {
                    const coordf_t middle_layer_z = (new_min_layer_height > 0.5 * delta) ?
	                    next_range.second - new_min_layer_height :
                    	next_range.first + std::max(old_min_layer_height, 0.5 * delta);
                    t_layer_height_range new_range = { middle_layer_z, next_range.second };

                    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("Add Height Range")));
                    changed = true;

                    // create new 2 layers instead of deleted one
                    // delete old layer

                    wxDataViewItem layer_item = m_objects_model->GetItemByLayerRange(obj_idx, next_range);
                    del_subobject_item(layer_item);

                    ranges[new_range] = old_config;
                    add_layer_item(new_range, layers_item, layer_idx);

                    new_range = { current_range.second, middle_layer_z };
                    ranges[new_range].assign_config(get_default_layer_config(obj_idx));
                    add_layer_item(new_range, layers_item, layer_idx);
                }
            }
            else if (next_range.first - current_range.second >= get_min_layer_height(0) - EPSILON)
            {
                // Filling in a gap between the current and a new layer height range with a new one.
                take_snapshot(_(L("Add Height Range")));
                changed = true;

                const t_layer_height_range new_range = { current_range.second, next_range.first };
                ranges[new_range].assign_config(get_default_layer_config(obj_idx));
                add_layer_item(new_range, layers_item, layer_idx);
            }
        }
    }

    if (changed)
        changed_object(obj_idx);

    // The layer range panel is updated even if this function does not change the layer ranges, as the panel update
    // may have been postponed from the "kill focus" event of a text field, if the focus was lost for the "add layer" button.
    // select item to update layers sizer
    select_item(layers_item);
}

// Returning an empty string means that the layer could be added after the current layer.
// Otherwise an error tooltip is returned.
// When editing this function, please synchronize the conditions with add_layer_range_after_current().
wxString ObjectList::can_add_new_range_after_current(const t_layer_height_range current_range)
{
    const int obj_idx = get_selected_obj_idx();
    assert(obj_idx >= 0);
    if (obj_idx < 0)
        // This should not happen.
        return "ObjectList assert";

    auto& ranges = object(obj_idx)->layer_config_ranges;
    auto it_range = ranges.find(current_range);
    assert(it_range != ranges.end());
    if (it_range == ranges.end())
        // This shoudl not happen.
        return "ObjectList assert";

    auto it_next_range = it_range;
    if (++ it_next_range == ranges.end())
    	// Adding a layer after the last layer is always possible.
        return "";
    
    if (const std::pair<coordf_t, coordf_t>& next_range = it_next_range->first; current_range.second <= next_range.first)
    {
        if (current_range.second == next_range.first) {
            if (next_range.second - next_range.first < get_min_layer_height(it_next_range->second.opt_int("extruder")) + get_min_layer_height(0) - EPSILON)
                return _(L("Cannot insert a new layer range after the current layer range.\n"
                	       "The next layer range is too thin to be split to two\n"
                	       "without violating the minimum layer height."));
        } else if (next_range.first - current_range.second < get_min_layer_height(0) - EPSILON) {
            return _(L("Cannot insert a new layer range between the current and the next layer range.\n"
            	       "The gap between the current layer range and the next layer range\n"
            	       "is thinner than the minimum layer height allowed."));
        }
    } else
	    return _(L("Cannot insert a new layer range after the current layer range.\n"
	    		   "Current layer range overlaps with the next layer range."));

	// All right, new layer height range could be inserted.
	return "";
}

void ObjectList::add_layer_item(const t_layer_height_range& range, 
                                const wxDataViewItem layers_item, 
                                const int layer_idx /* = -1*/)
{
    const int obj_idx = m_objects_model->GetObjectIdByItem(layers_item);
    if (obj_idx < 0) return;

    const DynamicPrintConfig& config = object(obj_idx)->layer_config_ranges[range].get();
    if (!config.has("extruder"))
        return;

    const auto layer_item = m_objects_model->AddLayersChild(layers_item, 
                                                            range, 
                                                            config.opt_int("extruder"),
                                                            layer_idx);
    add_settings_item(layer_item, &config);
}

bool ObjectList::edit_layer_range(const t_layer_height_range& range, coordf_t layer_height)
{
    // Use m_selected_object_id instead of get_selected_obj_idx()
    // because of get_selected_obj_idx() return obj_idx for currently selected item.
    // But edit_layer_range(...) function can be called, when Selection in ObjectList could be changed
    const int obj_idx = m_selected_object_id ; 
    if (obj_idx < 0) 
        return false;

    ModelConfig* config = &object(obj_idx)->layer_config_ranges[range];
    if (fabs(layer_height - config->opt_float("layer_height")) < EPSILON)
        return false;

    const int extruder_idx = config->opt_int("extruder");

    if (layer_height >= get_min_layer_height(extruder_idx) && 
        layer_height <= get_max_layer_height(extruder_idx)) 
    {
        config->set_key_value("layer_height", new ConfigOptionFloat(layer_height));
        changed_object(obj_idx);
        return true;
    }

    return false;
}

bool ObjectList::edit_layer_range(const t_layer_height_range& range, const t_layer_height_range& new_range, bool dont_update_ui)
{
    // Use m_selected_object_id instead of get_selected_obj_idx()
    // because of get_selected_obj_idx() return obj_idx for currently selected item.
    // But edit_layer_range(...) function can be called, when Selection in ObjectList could be changed
    const int obj_idx = m_selected_object_id;
    if (obj_idx < 0) return false;

    take_snapshot(_L("Edit Height Range"));

    const ItemType sel_type = m_objects_model->GetItemType(GetSelection());

    auto& ranges = object(obj_idx)->layer_config_ranges;

    {
        ModelConfig config = std::move(ranges[range]);
        ranges.erase(range);
        ranges[new_range] = std::move(config);
    }

    changed_object(obj_idx);
    
    wxDataViewItem root_item = m_objects_model->GetLayerRootItem(m_objects_model->GetItemById(obj_idx));
    // To avoid update selection after deleting of a selected item (under GTK)
    // set m_prevent_list_events to true
    m_prevent_list_events = true;
    m_objects_model->DeleteChildren(root_item);

    if (root_item.IsOk()) {
        // create Layer item(s) according to the layer_config_ranges
        for (const auto& r : ranges)
            add_layer_item(r.first, root_item);
    }

    // if this function was invoked from wxEVT_CHANGE_SELECTION selected item could be other than itLayer or itLayerRoot      
    if (!dont_update_ui && (sel_type & (itLayer | itLayerRoot)))
        select_item(sel_type&itLayer ? m_objects_model->GetItemByLayerRange(obj_idx, new_range) : root_item);

    Expand(root_item);

    m_prevent_list_events = false;
    return true;
}

void ObjectList::init_objects()
{
    m_objects = &wxGetApp().model().objects;
}

bool ObjectList::multiple_selection() const 
{
    return GetSelectedItemsCount() > 1;
}

bool ObjectList::is_selected(const ItemType type) const
{
    const wxDataViewItem& item = GetSelection();
    if (item)
        return m_objects_model->GetItemType(item) == type;

    return false;
}

int ObjectList::get_selected_layers_range_idx() const
{
    const wxDataViewItem& item = GetSelection();
    if (!item) 
        return -1;

    const ItemType type = m_objects_model->GetItemType(item);
    if (type & itSettings && m_objects_model->GetItemType(m_objects_model->GetParent(item)) != itLayer)
        return -1;

    return m_objects_model->GetLayerIdByItem(type & itLayer ? item : m_objects_model->GetParent(item));
}

void ObjectList::update_selections()
{
    const Selection& selection = scene_selection();
    wxDataViewItemArray sels;

    if ( ( m_selection_mode & (smSettings|smLayer|smLayerRoot) ) == 0)
        m_selection_mode = smInstance;

    // We doesn't update selection if itSettings | itLayerRoot | itLayer Item for the current object/part is selected
    if (GetSelectedItemsCount() == 1 && m_objects_model->GetItemType(GetSelection()) & (itSettings | itLayerRoot | itLayer))
    {
        const auto item = GetSelection();
        if (selection.is_single_full_object()) {
            if (m_objects_model->GetItemType(m_objects_model->GetParent(item)) & itObject &&
                m_objects_model->GetObjectIdByItem(item) == selection.get_object_idx() )
                return;
            sels.Add(m_objects_model->GetItemById(selection.get_object_idx()));
        }
        else if (selection.is_single_volume() || selection.is_any_modifier()) {
            const auto gl_vol = selection.get_volume(*selection.get_volume_idxs().begin());
            if (m_objects_model->GetVolumeIdByItem(m_objects_model->GetParent(item)) == gl_vol->volume_idx())
                return;
        }
        // but if there is selected only one of several instances by context menu,
        // then select this instance in ObjectList
        else if (selection.is_single_full_instance())
            sels.Add(m_objects_model->GetItemByInstanceId(selection.get_object_idx(), selection.get_instance_idx()));
        // Can be the case, when we have selected itSettings | itLayerRoot | itLayer in the ObjectList and selected object/instance in the Scene
        // and then select some object/instance in 3DScene using Ctrt+left click
        // see https://github.com/prusa3d/PrusaSlicer/issues/5517
        else {
            // Unselect all items in ObjectList
            m_last_selected_item = wxDataViewItem(nullptr);
            m_prevent_list_events = true;
            UnselectAll();
            m_prevent_list_events = false;
            // call this function again to update selection from the canvas
            update_selections();
            return;
        }
    }
    else if (selection.is_single_full_object() || selection.is_multiple_full_object())
    {
        const Selection::ObjectIdxsToInstanceIdxsMap& objects_content = selection.get_content();
        // it's impossible to select Settings, Layer or LayerRoot for several objects
        if (!selection.is_multiple_full_object() && (m_selection_mode & (smSettings | smLayer | smLayerRoot)))
        {
            auto obj_idx = objects_content.begin()->first;
            wxDataViewItem obj_item = m_objects_model->GetItemById(obj_idx);
            if (m_selection_mode & smSettings)
            {
                if (m_selected_layers_range_idx < 0)
                    sels.Add(m_objects_model->GetSettingsItem(obj_item));
                else
                    sels.Add(m_objects_model->GetSettingsItem(m_objects_model->GetItemByLayerId(obj_idx, m_selected_layers_range_idx)));
            }
            else if (m_selection_mode & smLayerRoot)
                sels.Add(m_objects_model->GetLayerRootItem(obj_item));
            else if (m_selection_mode & smLayer) {
                if (m_selected_layers_range_idx >= 0)
                    sels.Add(m_objects_model->GetItemByLayerId(obj_idx, m_selected_layers_range_idx));
                else
                    sels.Add(obj_item);
            }
        }
        else {
        for (const auto& object : objects_content) {
            if (object.second.size() == 1)          // object with 1 instance                
                sels.Add(m_objects_model->GetItemById(object.first));
            else if (object.second.size() > 1)      // object with several instances                
            {
                wxDataViewItemArray current_sels;
                GetSelections(current_sels);
                const wxDataViewItem frst_inst_item = m_objects_model->GetItemByInstanceId(object.first, 0);

                bool root_is_selected = false;
                for (const auto& item:current_sels)
                    if (item == m_objects_model->GetParent(frst_inst_item) || 
                        item == m_objects_model->GetTopParent(frst_inst_item)) {
                        root_is_selected = true;
                        sels.Add(item);
                        break;
                    }
                if (root_is_selected)
                    continue;

                const Selection::InstanceIdxsList& instances = object.second;
                for (const auto& inst : instances)
                    sels.Add(m_objects_model->GetItemByInstanceId(object.first, inst));
            }
        } }
    }
    else if (selection.is_any_volume() || selection.is_any_modifier())
    {
        if (m_selection_mode & smSettings)
        {
            const auto idx = *selection.get_volume_idxs().begin();
            const auto gl_vol = selection.get_volume(idx);
            if (gl_vol->volume_idx() >= 0) {
                // Only add GLVolumes with non-negative volume_ids. GLVolumes with negative volume ids
                // are not associated with ModelVolumes, but they are temporarily generated by the backend
                // (for example, SLA supports or SLA pad).
                wxDataViewItem vol_item = m_objects_model->GetItemByVolumeId(gl_vol->object_idx(), gl_vol->volume_idx());
                sels.Add(m_objects_model->GetSettingsItem(vol_item));
            }
        }
        else {
        for (auto idx : selection.get_volume_idxs()) {
            const auto gl_vol = selection.get_volume(idx);
			if (gl_vol->volume_idx() >= 0)
                // Only add GLVolumes with non-negative volume_ids. GLVolumes with negative volume ids
                // are not associated with ModelVolumes, but they are temporarily generated by the backend
                // (for example, SLA supports or SLA pad).
                sels.Add(m_objects_model->GetItemByVolumeId(gl_vol->object_idx(), gl_vol->volume_idx()));
        }
        m_selection_mode = smVolume; }
    }
    else if (selection.is_single_full_instance() || selection.is_multiple_full_instance())
    {
        for (auto idx : selection.get_instance_idxs()) {            
            sels.Add(m_objects_model->GetItemByInstanceId(selection.get_object_idx(), idx));
        }
    }
    else if (selection.is_mixed())
    {
        const Selection::ObjectIdxsToInstanceIdxsMap& objects_content_list = selection.get_content();

        for (auto idx : selection.get_volume_idxs()) {
            const auto gl_vol = selection.get_volume(idx);
            const auto& glv_obj_idx = gl_vol->object_idx();
            const auto& glv_ins_idx = gl_vol->instance_idx();

            bool is_selected = false;

            for (auto obj_ins : objects_content_list) {
                if (obj_ins.first == glv_obj_idx) {
                    if (obj_ins.second.find(glv_ins_idx) != obj_ins.second.end() &&
                        !selection.is_from_single_instance() ) // a case when volumes of different types are selected
                    {
                        if (glv_ins_idx == 0 && (*m_objects)[glv_obj_idx]->instances.size() == 1)
                            sels.Add(m_objects_model->GetItemById(glv_obj_idx));
                        else
                            sels.Add(m_objects_model->GetItemByInstanceId(glv_obj_idx, glv_ins_idx));

                        is_selected = true;
                        break;
                    }
                }
            }

            if (is_selected)
                continue;

            const auto& glv_vol_idx = gl_vol->volume_idx();
            if (glv_vol_idx == 0 && (*m_objects)[glv_obj_idx]->volumes.size() == 1)
                sels.Add(m_objects_model->GetItemById(glv_obj_idx));
            else
                sels.Add(m_objects_model->GetItemByVolumeId(glv_obj_idx, glv_vol_idx));
        }
    }

    if (sels.size() == 0 || m_selection_mode & smSettings)
        m_selection_mode = smUndef;
    
    select_items(sels);

    // Scroll selected Item in the middle of an object list
    ensure_current_item_visible();
}

void ObjectList::update_selections_on_canvas()
{
    Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();

    const int sel_cnt = GetSelectedItemsCount();
    if (sel_cnt == 0) {
        selection.remove_all();
        wxGetApp().plater()->canvas3D()->update_gizmos_on_off_state();
        return;
    }

    std::vector<unsigned int> volume_idxs;
    Selection::EMode mode = Selection::Volume;
    bool single_selection = sel_cnt == 1;
    auto add_to_selection = [this, &volume_idxs, &single_selection](const wxDataViewItem& item, const Selection& selection, int instance_idx, Selection::EMode& mode)
    {
        const ItemType& type = m_objects_model->GetItemType(item);
        const int obj_idx = m_objects_model->GetObjectIdByItem(item);

        if (type == itVolume) {
            const int vol_idx = m_objects_model->GetVolumeIdByItem(item);
            std::vector<unsigned int> idxs = selection.get_volume_idxs_from_volume(obj_idx, std::max(instance_idx, 0), vol_idx);
            volume_idxs.insert(volume_idxs.end(), idxs.begin(), idxs.end());
        }
        else if (type == itInstance) {
            const int inst_idx = m_objects_model->GetInstanceIdByItem(item);
            mode = Selection::Instance;
            std::vector<unsigned int> idxs = selection.get_volume_idxs_from_instance(obj_idx, inst_idx);
            volume_idxs.insert(volume_idxs.end(), idxs.begin(), idxs.end());
        }
        else
        {
            mode = Selection::Instance;
            single_selection &= (obj_idx != selection.get_object_idx());
            std::vector<unsigned int> idxs = selection.get_volume_idxs_from_object(obj_idx);
            volume_idxs.insert(volume_idxs.end(), idxs.begin(), idxs.end());
        }
    };

    // stores current instance idx before to clear the selection
    int instance_idx = selection.get_instance_idx();

    if (sel_cnt == 1) {
        wxDataViewItem item = GetSelection();
        if (m_objects_model->GetItemType(item) & (itSettings | itInstanceRoot | itLayerRoot | itLayer | itInfo))
            add_to_selection(m_objects_model->GetParent(item), selection, instance_idx, mode);
        else
            add_to_selection(item, selection, instance_idx, mode);
    }
    else
    {
        wxDataViewItemArray sels;
        GetSelections(sels);

        // clear selection before adding new elements 
        selection.clear(); //OR remove_all()? 

        for (auto item : sels)
        {
            add_to_selection(item, selection, instance_idx, mode);
        }
    }

    if (selection.contains_all_volumes(volume_idxs))
    {
        // remove
        volume_idxs = selection.get_missing_volume_idxs_from(volume_idxs);
        if (volume_idxs.size() > 0)
        {
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("Selection-Remove from list")));
            selection.remove_volumes(mode, volume_idxs);
        }
    }
    else
    {
        // add
        volume_idxs = selection.get_unselected_volume_idxs_from(volume_idxs);
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("Selection-Add from list")));
        selection.add_volumes(mode, volume_idxs, single_selection);
    }

    wxGetApp().plater()->canvas3D()->update_gizmos_on_off_state();
    wxGetApp().plater()->canvas3D()->render();
}

void ObjectList::select_item(const wxDataViewItem& item)
{
    if (! item.IsOk()) { return; }

    m_prevent_list_events = true;

    UnselectAll();
    Select(item);
    part_selection_changed();

    m_prevent_list_events = false;
}

void ObjectList::select_item(std::function<wxDataViewItem()> get_item)
{
    if (!get_item) 
        return;

    m_prevent_list_events = true;

    wxDataViewItem item = get_item();
    if (item.IsOk()) {
        UnselectAll();
        Select(item);
        part_selection_changed();
    }

    m_prevent_list_events = false;
}

void ObjectList::select_items(const wxDataViewItemArray& sels)
{
    m_prevent_list_events = true;

    m_last_selected_item = sels.empty() ? wxDataViewItem(nullptr) : sels.back();

    UnselectAll();
    SetSelections(sels);
    part_selection_changed();

    m_prevent_list_events = false;
}

void ObjectList::select_all()
{
    SelectAll();
    selection_changed();
}

void ObjectList::select_item_all_children()
{
    wxDataViewItemArray sels;

    // There is no selection before OR some object is selected   =>  select all objects
    if (!GetSelection() || m_objects_model->GetItemType(GetSelection()) == itObject) {
        for (size_t i = 0; i < m_objects->size(); i++)
            sels.Add(m_objects_model->GetItemById(i));
        m_selection_mode = smInstance;
    }
    else {
        const auto item = GetSelection();
        const ItemType item_type = m_objects_model->GetItemType(item);
        // Some volume/layer/instance is selected    =>  select all volumes/layers/instances inside the current object
        if (item_type & (itVolume | itInstance | itLayer))
            m_objects_model->GetChildren(m_objects_model->GetParent(item), sels);

        m_selection_mode = item_type&itVolume ? smVolume : 
                           item_type&itLayer  ? smLayer  : smInstance;
    }

    SetSelections(sels);
    selection_changed();
}

// update selection mode for non-multiple selection
void ObjectList::update_selection_mode()
{
    m_selected_layers_range_idx=-1;
    // All items are unselected 
    if (!GetSelection())
    {
        m_last_selected_item = wxDataViewItem(nullptr);
        m_selection_mode = smUndef;
        return;
    }

    const ItemType type = m_objects_model->GetItemType(GetSelection());
    m_selection_mode =  type & itSettings ? smUndef  :
                        type & itLayer    ? smLayer  :
                        type & itVolume   ? smVolume : smInstance;
}

// check last selected item. If is it possible to select it
bool ObjectList::check_last_selection(wxString& msg_str)
{
    if (!m_last_selected_item)
        return true;
        
    const bool is_shift_pressed = wxGetKeyState(WXK_SHIFT);

    /* We can't mix Volumes, Layers and Objects/Instances.
     * So, show information about it
     */
    const ItemType type = m_objects_model->GetItemType(m_last_selected_item);

    // check a case of a selection of the same type items from different Objects
    auto impossible_multi_selection = [type, this](const ItemType item_type, const SELECTION_MODE selection_mode) {
        if (!(type & item_type && m_selection_mode & selection_mode))
            return false;

        wxDataViewItemArray sels;
        GetSelections(sels);
        for (const auto& sel : sels)
            if (sel != m_last_selected_item &&
                m_objects_model->GetTopParent(sel) != m_objects_model->GetTopParent(m_last_selected_item))
                return true;

        return false;
    };

    if (impossible_multi_selection(itVolume, smVolume) ||
        impossible_multi_selection(itLayer,  smLayer ) ||
        type & itSettings ||
        (type & itVolume   && !(m_selection_mode & smVolume  )) ||
        (type & itLayer    && !(m_selection_mode & smLayer   )) ||
        (type & itInstance && !(m_selection_mode & smInstance))
        )
    {
        // Inform user why selection isn't completed
        const wxString item_type = m_selection_mode & smInstance ? _(L("Object or Instance")) : 
                                   m_selection_mode & smVolume   ? _(L("Part")) : _(L("Layer"));

        msg_str = wxString::Format( _(L("Unsupported selection")) + "\n\n" + 
                                    _(L("You started your selection with %s Item.")) + "\n" +
                                    _(L("In this mode you can select only other %s Items%s")), 
                                    item_type, item_type,
                                    m_selection_mode == smInstance ? "." : 
                                                        " " + _(L("of a current Object")));

        // Unselect last selected item, if selection is without SHIFT
        if (!is_shift_pressed) {
            Unselect(m_last_selected_item);
            show_info(this, msg_str, _(L("Info")));
        }
        
        return is_shift_pressed;
    }

    return true;
}

void ObjectList::fix_multiselection_conflicts()
{
    if (GetSelectedItemsCount() <= 1) {
        update_selection_mode();
        return;
    }

    wxString msg_string;
    if (!check_last_selection(msg_string))
        return;

    m_prevent_list_events = true;

    wxDataViewItemArray sels;
    GetSelections(sels);

    if (m_selection_mode & (smVolume|smLayer))
    {
        // identify correct parent of the initial selected item
        const wxDataViewItem& parent = m_objects_model->GetParent(m_last_selected_item == sels.front() ? sels.back() : sels.front());

        sels.clear();
        wxDataViewItemArray children; // selected volumes from current parent
        m_objects_model->GetChildren(parent, children);

        const ItemType item_type = m_selection_mode & smVolume ? itVolume : itLayer;

        for (const auto& child : children)
            if (IsSelected(child) && m_objects_model->GetItemType(child) & item_type)
                sels.Add(child);

        // If some part is selected, unselect all items except of selected parts of the current object
        UnselectAll();
        SetSelections(sels);
    }
    else
    {
        for (const auto& item : sels)
        {
            if (!IsSelected(item)) // if this item is unselected now (from previous actions)
                continue;

            if (m_objects_model->GetItemType(item) & itSettings) {
                Unselect(item);
                continue;
            }

            const wxDataViewItem& parent = m_objects_model->GetParent(item);
            if (parent != wxDataViewItem(nullptr) && IsSelected(parent))
                Unselect(parent);
            else
            {
                wxDataViewItemArray unsels;
                m_objects_model->GetAllChildren(item, unsels);
                for (const auto& unsel_item : unsels)
                    Unselect(unsel_item);
            }

            if (m_objects_model->GetItemType(item) & itVolume)
                Unselect(item);

            m_selection_mode = smInstance;
        }
    }

    if (!msg_string.IsEmpty())
        show_info(this, msg_string, _(L("Info")));

    if (!IsSelected(m_last_selected_item))
        m_last_selected_item = wxDataViewItem(nullptr);

    m_prevent_list_events = false;
}

ModelVolume* ObjectList::get_selected_model_volume()
{
    wxDataViewItem item = GetSelection();
    if (!item)
        return nullptr;
    if (m_objects_model->GetItemType(item) != itVolume) {
        if (m_objects_model->GetItemType(m_objects_model->GetParent(item)) == itVolume)
            item = m_objects_model->GetParent(item);
        else
            return nullptr;
    }

    const auto vol_idx = m_objects_model->GetVolumeIdByItem(item);
    const auto obj_idx = get_selected_obj_idx();
    if (vol_idx < 0 || obj_idx < 0)
        return nullptr;

    return (*m_objects)[obj_idx]->volumes[vol_idx];
}

void ObjectList::change_part_type()
{
    ModelVolume* volume = get_selected_model_volume();
    if (!volume)
        return;

    const int obj_idx = get_selected_obj_idx();
    if (obj_idx < 0) return;

    const ModelVolumeType type = volume->type();
    if (type == ModelVolumeType::MODEL_PART)
    {
        int model_part_cnt = 0;
        for (auto vol : (*m_objects)[obj_idx]->volumes) {
            if (vol->type() == ModelVolumeType::MODEL_PART)
                ++model_part_cnt;
        }

        if (model_part_cnt == 1) {
            Slic3r::GUI::show_error(nullptr, _(L("You can't change a type of the last solid part of the object.")));
            return;
        }
    }

    const wxString names[] = { _L("Part"), _L("Negative Volume"), _L("Modifier"), _L("Support Blocker"), _L("Support Enforcer") };
    auto new_type = ModelVolumeType(wxGetSingleChoiceIndex(_L("Type:"), _L("Select type of part"), wxArrayString(5, names), int(type)));

	if (new_type == type || new_type == ModelVolumeType::INVALID)
        return;

    take_snapshot(_L("Change Part Type"));

    volume->set_type(new_type);
    wxDataViewItemArray sel = reorder_volumes_and_get_selection(obj_idx, [volume](const ModelVolume* vol) { return vol == volume; });
    if (!sel.IsEmpty())
        select_item(sel.front());
}

void ObjectList::last_volume_is_deleted(const int obj_idx)
{

    if (obj_idx < 0 || size_t(obj_idx) >= m_objects->size() || (*m_objects)[obj_idx]->volumes.size() != 1)
        return;

    auto volume = (*m_objects)[obj_idx]->volumes.front();

    // clear volume's config values
    volume->config.clear();

    // set a default extruder value, since user can't add it manually
    volume->config.set_key_value("extruder", new ConfigOptionInt(0));
}

void ObjectList::update_and_show_object_settings_item()
{
    const wxDataViewItem item = GetSelection();
    if (!item) return;

    const wxDataViewItem& obj_item = m_objects_model->IsSettingsItem(item) ? m_objects_model->GetParent(item) : item;
    select_item([this, obj_item](){ return add_settings_item(obj_item, &get_item_config(obj_item).get()); });
}

// Update settings item for item had it
void ObjectList::update_settings_item_and_selection(wxDataViewItem item, wxDataViewItemArray& selections)
{
    const wxDataViewItem old_settings_item = m_objects_model->GetSettingsItem(item);
    const wxDataViewItem new_settings_item = add_settings_item(item, &get_item_config(item).get());

    if (!new_settings_item && old_settings_item)
        m_objects_model->Delete(old_settings_item);

    // if ols settings item was is selected area
    if (selections.Index(old_settings_item) != wxNOT_FOUND)
    {
        // If settings item was just updated
        if (old_settings_item == new_settings_item)
        {
            Sidebar& panel = wxGetApp().sidebar();
            panel.Freeze();

            // update settings list
            wxGetApp().obj_settings()->UpdateAndShow(true);

            panel.Layout();
            panel.Thaw();
        }
        else
        // If settings item was deleted from the list, 
        // it's need to be deleted from selection array, if it was there
        {
            selections.Remove(old_settings_item);

            // Select item, if settings_item doesn't exist for item anymore, but was selected
            if (selections.Index(item) == wxNOT_FOUND) {
                selections.Add(item);
                select_item(item); // to correct update of the SettingsList and ManipulationPanel sizers
            }
        }
    }
}

void ObjectList::update_object_list_by_printer_technology()
{
    m_prevent_canvas_selection_update = true;
    wxDataViewItemArray sel;
    GetSelections(sel); // stash selection

    wxDataViewItemArray object_items;
    m_objects_model->GetChildren(wxDataViewItem(nullptr), object_items);

    for (auto& object_item : object_items) {
        // update custom supports info
        update_info_items(m_objects_model->GetObjectIdByItem(object_item));

        // Update Settings Item for object
        update_settings_item_and_selection(object_item, sel);

        // Update settings for Volumes
        wxDataViewItemArray all_object_subitems;
        m_objects_model->GetChildren(object_item, all_object_subitems);
        for (auto item : all_object_subitems)
            if (m_objects_model->GetItemType(item) & itVolume)
                // update settings for volume
                update_settings_item_and_selection(item, sel);

        // Update Layers Items
        wxDataViewItem layers_item = m_objects_model->GetLayerRootItem(object_item);
        if (!layers_item)
            layers_item = add_layer_root_item(object_item);
        else if (printer_technology() == ptSLA) {
            // If layers root item will be deleted from the list, so
            // it's need to be deleted from selection array, if it was there
            wxDataViewItemArray del_items;
            bool some_layers_was_selected = false;
            m_objects_model->GetAllChildren(layers_item, del_items);
            for (auto& del_item:del_items)
                if (sel.Index(del_item) != wxNOT_FOUND) {
                    some_layers_was_selected = true;
                    sel.Remove(del_item);
                }
            if (sel.Index(layers_item) != wxNOT_FOUND) {
                some_layers_was_selected = true;
                sel.Remove(layers_item);
            }

            // delete all "layers" items
            m_objects_model->Delete(layers_item);

            // Select object_item, if layers_item doesn't exist for item anymore, but was some of layer items was/were selected
            if (some_layers_was_selected)
                sel.Add(object_item);
        }
        else {
            wxDataViewItemArray all_obj_layers;
            m_objects_model->GetChildren(layers_item, all_obj_layers);

            for (auto item : all_obj_layers)
                // update settings for layer
                update_settings_item_and_selection(item, sel);
        }
    }

    // restore selection:
    SetSelections(sel);
    m_prevent_canvas_selection_update = false;
}

void ObjectList::instances_to_separated_object(const int obj_idx, const std::set<int>& inst_idxs)
{
    if ((*m_objects)[obj_idx]->instances.size() == inst_idxs.size())
    {
        instances_to_separated_objects(obj_idx);
        return;
    }

    // create new object from selected instance  
    ModelObject* model_object = (*m_objects)[obj_idx]->get_model()->add_object(*(*m_objects)[obj_idx]);
    for (int inst_idx = int(model_object->instances.size()) - 1; inst_idx >= 0; inst_idx--)
    {
        if (find(inst_idxs.begin(), inst_idxs.end(), inst_idx) != inst_idxs.end())
            continue;
        model_object->delete_instance(inst_idx);
    }

    // Add new object to the object_list
    const size_t new_obj_indx = static_cast<size_t>(m_objects->size() - 1);
    add_object_to_list(new_obj_indx);

    for (std::set<int>::const_reverse_iterator it = inst_idxs.rbegin(); it != inst_idxs.rend(); ++it)
    {
        // delete selected instance from the object
        del_subobject_from_object(obj_idx, *it, itInstance);
        delete_instance_from_list(obj_idx, *it);
    }

    // update printable state for new volumes on canvas3D
    wxGetApp().plater()->canvas3D()->update_instance_printable_state_for_object(new_obj_indx);
}

void ObjectList::instances_to_separated_objects(const int obj_idx)
{
    const int inst_cnt = (*m_objects)[obj_idx]->instances.size();

    std::vector<size_t> object_idxs;

    for (int i = inst_cnt-1; i > 0 ; i--)
    {
        // create new object from initial
        ModelObject* object = (*m_objects)[obj_idx]->get_model()->add_object(*(*m_objects)[obj_idx]);
        for (int inst_idx = object->instances.size() - 1; inst_idx >= 0; inst_idx--)
        {
            if (inst_idx == i)
                continue;
            // delete unnecessary instances
            object->delete_instance(inst_idx);
        }

        // Add new object to the object_list
        const size_t new_obj_indx = static_cast<size_t>(m_objects->size() - 1);
        add_object_to_list(new_obj_indx);
        object_idxs.push_back(new_obj_indx);

        // delete current instance from the initial object
        del_subobject_from_object(obj_idx, i, itInstance);
        delete_instance_from_list(obj_idx, i);
    }

    // update printable state for new volumes on canvas3D
    wxGetApp().plater()->canvas3D()->update_instance_printable_state_for_objects(object_idxs);
}

void ObjectList::split_instances()
{
    const Selection& selection = scene_selection();
    const int obj_idx = selection.get_object_idx();
    if (obj_idx == -1)
        return;

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("Instances to Separated Objects")));

    if (selection.is_single_full_object())
    {
        instances_to_separated_objects(obj_idx);
        return;
    }

    const int inst_idx = selection.get_instance_idx();
    const std::set<int> inst_idxs = inst_idx < 0 ?
                                    selection.get_instance_idxs() :
                                    std::set<int>{ inst_idx };

    instances_to_separated_object(obj_idx, inst_idxs);
}

void ObjectList::rename_item()
{
    const wxDataViewItem item = GetSelection();
    if (!item || !(m_objects_model->GetItemType(item) & (itVolume | itObject)))
        return ;

    const wxString new_name = wxGetTextFromUser(_(L("Enter new name"))+":", _(L("Renaming")), 
                                                m_objects_model->GetName(item), this);

    if (new_name.IsEmpty())
        return;

    bool is_unusable_symbol = false;
    std::string chosen_name = Slic3r::normalize_utf8_nfc(new_name.ToUTF8());
    const char* unusable_symbols = "<>:/\\|?*\"";
    for (size_t i = 0; i < std::strlen(unusable_symbols); i++) {
        if (chosen_name.find_first_of(unusable_symbols[i]) != std::string::npos) {
            is_unusable_symbol = true;
        }
    }

    if (is_unusable_symbol) {
        show_error(this, _(L("The supplied name is not valid;")) + "\n" +
            _(L("the following characters are not allowed:")) + " <>:/\\|?*\"");
        return;
    }

    // The icon can't be edited so get its old value and reuse it.
    wxVariant valueOld;
    m_objects_model->GetValue(valueOld, item, colName);

    DataViewBitmapText bmpText;
    bmpText << valueOld;

    // But replace the text with the value entered by user.
    bmpText.SetText(new_name);

    wxVariant value;    
    value << bmpText;
    m_objects_model->SetValue(value, item, colName);
    m_objects_model->ItemChanged(item);

    update_name_in_model(item);
}

void ObjectList::fix_through_netfabb() 
{
    int obj_idx, vol_idx;
    get_selected_item_indexes(obj_idx, vol_idx);

    wxGetApp().plater()->fix_through_netfabb(obj_idx, vol_idx);
    
    update_item_error_icon(obj_idx, vol_idx);
}

void ObjectList::update_item_error_icon(const int obj_idx, const int vol_idx) const 
{
    const wxDataViewItem item = vol_idx <0 ? m_objects_model->GetItemById(obj_idx) :
                                m_objects_model->GetItemByVolumeId(obj_idx, vol_idx);
    if (!item)
        return;

    if (get_mesh_errors_count(obj_idx, vol_idx) == 0)
    {
        // if whole object has no errors more,
        if (get_mesh_errors_count(obj_idx) == 0)
            // unmark all items in the object
            m_objects_model->DeleteWarningIcon(vol_idx >= 0 ? m_objects_model->GetParent(item) : item, true);
        else
            // unmark fixed item only
            m_objects_model->DeleteWarningIcon(item);
    }
}

void ObjectList::msw_rescale()
{
    set_min_height();

    const int em = wxGetApp().em_unit();

    GetColumn(colName    )->SetWidth(20 * em);
    GetColumn(colPrint   )->SetWidth( 3 * em);
    GetColumn(colExtruder)->SetWidth( 8 * em);
    GetColumn(colEditing )->SetWidth( 3 * em);

    // rescale/update existing items with bitmaps
    m_objects_model->Rescale();

    Layout();
}

void ObjectList::sys_color_changed()
{
    wxGetApp().UpdateDVCDarkUI(this, true);

    msw_rescale();
}

void ObjectList::ItemValueChanged(wxDataViewEvent &event)
{
    if (event.GetColumn() == colName)
        update_name_in_model(event.GetItem());
    else if (event.GetColumn() == colExtruder) {
        wxDataViewItem item = event.GetItem();
        if (m_objects_model->GetItemType(item) == itObject)
            m_objects_model->UpdateVolumesExtruderBitmap(item);
        update_extruder_in_config(item);
    }
}

#ifdef __WXMSW__
// Workaround for entering the column editing mode on Windows. Simulate keyboard enter when another column of the active line is selected.
// Here the last active column is forgotten, so when leaving the editing mode, the next mouse click will not enter the editing mode of the newly selected column.
void ObjectList::OnEditingStarted(wxDataViewEvent &event)
{
	m_last_selected_column = -1;
}
#endif //__WXMSW__

void ObjectList::OnEditingDone(wxDataViewEvent &event)
{
    if (event.GetColumn() != colName)
        return;

    const auto renderer = dynamic_cast<BitmapTextRenderer*>(GetColumn(colName)->GetRenderer());

    if (renderer->WasCanceled())
		wxTheApp->CallAfter([this]{
			show_error(this, _(L("The supplied name is not valid;")) + "\n" +
				             _(L("the following characters are not allowed:")) + " <>:/\\|?*\"");
		});

#ifdef __WXMSW__
	// Workaround for entering the column editing mode on Windows. Simulate keyboard enter when another column of the active line is selected.
	// Here the last active column is forgotten, so when leaving the editing mode, the next mouse click will not enter the editing mode of the newly selected column.
	m_last_selected_column = -1;
#endif //__WXMSW__

    Plater* plater = wxGetApp().plater();
    if (plater)
        plater->set_current_canvas_as_dirty();
}

void ObjectList::set_extruder_for_selected_items(const int extruder) const 
{
    wxDataViewItemArray sels;
    GetSelections(sels);

    if (sels.empty())
        return;

    take_snapshot(_L("Change Extruders"));

    for (const wxDataViewItem& item : sels)
    {
        ModelConfig& config = get_item_config(item);
        
        if (config.has("extruder")) {
            if (extruder == 0)
                config.erase("extruder");
            else
                config.set("extruder", extruder);
        }
        else if (extruder > 0)
            config.set_key_value("extruder", new ConfigOptionInt(extruder));

        const wxString extruder_str = extruder == 0 ? wxString (_(L("default"))) : 
                                      wxString::Format("%d", config.extruder());

        auto const type = m_objects_model->GetItemType(item);

        /* We can change extruder for Object/Volume only.
         * So, if Instance is selected, get its Object item and change it
         */
        m_objects_model->SetExtruder(extruder_str, type & itInstance ? m_objects_model->GetTopParent(item) : item);

        const int obj_idx = type & itObject ? m_objects_model->GetIdByItem(item) :
                            m_objects_model->GetIdByItem(m_objects_model->GetTopParent(item));

        wxGetApp().plater()->canvas3D()->ensure_on_bed(obj_idx);
    }

    // update scene
    wxGetApp().plater()->update();
}

wxDataViewItemArray ObjectList::reorder_volumes_and_get_selection(int obj_idx, std::function<bool(const ModelVolume*)> add_to_selection/* = nullptr*/)
{
    wxDataViewItemArray items;

    ModelObject* object = (*m_objects)[obj_idx];
    if (object->volumes.size() <= 1)
        return items;

    object->sort_volumes(wxGetApp().app_config->get("order_volumes") == "1");

    wxDataViewItem object_item = m_objects_model->GetItemById(obj_idx);
    m_objects_model->DeleteVolumeChildren(object_item);

    for (const ModelVolume* volume : object->volumes) {
        wxDataViewItem vol_item = m_objects_model->AddVolumeChild(object_item, from_u8(volume->name),
            volume->type(),
            volume->get_mesh_errors_count() > 0,
            volume->config.has("extruder") ? volume->config.extruder() : 0,
            false);
        // add settings to the part, if it has those
        add_settings_item(vol_item, &volume->config.get());

        if (add_to_selection && add_to_selection(volume))
            items.Add(vol_item);
    }

    changed_object(obj_idx);
    return items;
}

void ObjectList::apply_volumes_order()
{
    if (wxGetApp().app_config->get("order_volumes") != "1" || !m_objects)
        return;

    for (size_t obj_idx = 0; obj_idx < m_objects->size(); obj_idx++)
        reorder_volumes_and_get_selection(obj_idx);
}

void ObjectList::update_after_undo_redo()
{
    m_prevent_canvas_selection_update = true;

    Plater::SuppressSnapshots suppress(wxGetApp().plater());

    // Unselect all objects before deleting them, so that no change of selection is emitted during deletion.

    /* To avoid execution of selection_changed() 
     * from wxEVT_DATAVIEW_SELECTION_CHANGED emitted from DeleteAll(), 
     * wrap this two functions into m_prevent_list_events *
     * */
    m_prevent_list_events = true;
    this->UnselectAll();
    m_objects_model->DeleteAll();
    m_prevent_list_events = false;

    size_t obj_idx = 0;
    std::vector<size_t> obj_idxs;
    obj_idxs.reserve(m_objects->size());
    while (obj_idx < m_objects->size()) {
        add_object_to_list(obj_idx, false);
        obj_idxs.push_back(obj_idx);
        ++obj_idx;
    }

    update_selections();

    m_prevent_canvas_selection_update = false;

    // update printable states on canvas
    wxGetApp().plater()->canvas3D()->update_instance_printable_state_for_objects(obj_idxs);
    // update scene
    wxGetApp().plater()->update();
}

void ObjectList::update_printable_state(int obj_idx, int instance_idx)
{
    ModelObject* object = (*m_objects)[obj_idx];

    const PrintIndicator printable = object->instances[instance_idx]->printable ? piPrintable : piUnprintable;
    if (object->instances.size() == 1)
        instance_idx = -1;

    m_objects_model->SetPrintableState(printable, obj_idx, instance_idx);
}

void ObjectList::toggle_printable_state()
{
    wxDataViewItemArray sels;
    GetSelections(sels);
    if (sels.IsEmpty())
        return;

    wxDataViewItem frst_item = sels[0];

    ItemType type = m_objects_model->GetItemType(frst_item);
    if (!(type & (itObject | itInstance)))
        return;


    int obj_idx = m_objects_model->GetObjectIdByItem(frst_item);
    int inst_idx = type == itObject ? 0 : m_objects_model->GetInstanceIdByItem(frst_item);
    bool printable = !object(obj_idx)->instances[inst_idx]->printable;

    const wxString snapshot_text =  sels.Count() > 1 ? 
                                    (printable ? _L("Set Printable group") : _L("Set Unprintable group")) :
                                    object(obj_idx)->instances.size() == 1 ? 
                                    format_wxstr("%1% %2%", (printable ? _L("Set Printable") : _L("Set Unprintable")), from_u8(object(obj_idx)->name)) :
                                    (printable ? _L("Set Printable Instance") : _L("Set Unprintable Instance"));
    take_snapshot(snapshot_text);

    std::vector<size_t> obj_idxs;
    for (auto item : sels)
    {
        type = m_objects_model->GetItemType(item);
        if (!(type & (itObject | itInstance)))
            continue;

        obj_idx = m_objects_model->GetObjectIdByItem(item);
        ModelObject* obj = object(obj_idx);

        obj_idxs.emplace_back(static_cast<size_t>(obj_idx));

        // set printable value for selected instance/instances in object
        if (type == itInstance) {
            inst_idx = m_objects_model->GetInstanceIdByItem(item);
            obj->instances[m_objects_model->GetInstanceIdByItem(item)]->printable = printable;
        }
        else
            for (auto inst : obj->instances)
                inst->printable = printable;

        // update printable state in ObjectList
        m_objects_model->SetObjectPrintableState(printable ? piPrintable : piUnprintable, item);
    }

    sort(obj_idxs.begin(), obj_idxs.end());
    obj_idxs.erase(unique(obj_idxs.begin(), obj_idxs.end()), obj_idxs.end());

    // update printable state on canvas
    wxGetApp().plater()->canvas3D()->update_instance_printable_state_for_objects(obj_idxs);

    // update scene
    wxGetApp().plater()->update();
}

ModelObject* ObjectList::object(const int obj_idx) const
{
    if (obj_idx < 0)
        return nullptr;

    return (*m_objects)[obj_idx];
}

} //namespace GUI
} //namespace Slic3r 
