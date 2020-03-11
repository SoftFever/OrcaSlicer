#include "libslic3r/libslic3r.h"
#include "GUI_ObjectList.hpp"
#include "GUI_ObjectManipulation.hpp"
#include "GUI_ObjectLayers.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"

#include "OptionsGroup.hpp"
#include "PresetBundle.hpp"
#include "Tab.hpp"
#include "wxExtensions.hpp"
#include "libslic3r/Model.hpp"
#include "LambdaObjectDialog.hpp"
#include "GLCanvas3D.hpp"
#include "Selection.hpp"

#include <boost/algorithm/string.hpp>
#include "slic3r/Utils/FixModelByWin10.hpp"

#ifdef __WXMSW__
#include "wx/uiaction.h"
#endif /* __WXMSW__ */

namespace Slic3r
{
namespace GUI
{

wxDEFINE_EVENT(EVT_OBJ_LIST_OBJECT_SELECT, SimpleEvent);

// pt_FFF
static SettingsBundle FREQ_SETTINGS_BUNDLE_FFF =
{
    { L("Layers and Perimeters"), { "layer_height" , "perimeters", "top_solid_layers", "bottom_solid_layers" } },
    { L("Infill")               , { "fill_density", "fill_pattern" } },
    { L("Support material")     , { "support_material", "support_material_auto", "support_material_threshold", 
                                    "support_material_pattern", "support_material_buildplate_only",
                                    "support_material_spacing" } },
    { L("Wipe options")            , { "wipe_into_infill", "wipe_into_objects" } }
};

// pt_SLA
static SettingsBundle FREQ_SETTINGS_BUNDLE_SLA =
{
    { L("Pad and Support")      , { "supports_enable", "pad_enable" } }
};

// Note: id accords to type of the sub-object (adding volume), so sequence of the menu items is important
static std::vector<std::pair<std::string, std::string>> ADD_VOLUME_MENU_ITEMS = {
//     menu_item Name            menu_item bitmap name
    {L("Add part"),              "add_part" },           // ~ModelVolumeType::MODEL_PART
    {L("Add modifier"),          "add_modifier"},        // ~ModelVolumeType::PARAMETER_MODIFIER
    {L("Add support enforcer"),  "support_enforcer"},    // ~ModelVolumeType::SUPPORT_ENFORCER
    {L("Add support blocker"),   "support_blocker"}      // ~ModelVolumeType::SUPPORT_BLOCKER
};

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
    return wxGetApp().extruders_cnt();
}

static void take_snapshot(const wxString& snapshot_name) 
{
    Plater* plater = wxGetApp().plater();
    if (plater)
        plater->take_snapshot(snapshot_name);
}

ObjectList::ObjectList(wxWindow* parent) :
    wxDataViewCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxDV_MULTIPLE),
    m_parent(parent)
{
    // Fill CATEGORY_ICON
    {
        // Note: `this` isn't passed to create_scaled_bitmap() here because of bugs in the widget,
        // see note in PresetBundle::load_compatible_bitmaps()

        // ptFFF
        CATEGORY_ICON[L("Layers and Perimeters")]    = create_scaled_bitmap("layers");
        CATEGORY_ICON[L("Infill")]                   = create_scaled_bitmap("infill");
        CATEGORY_ICON[L("Support material")]         = create_scaled_bitmap("support");
        CATEGORY_ICON[L("Speed")]                    = create_scaled_bitmap("time");
        CATEGORY_ICON[L("Extruders")]                = create_scaled_bitmap("funnel");
        CATEGORY_ICON[L("Extrusion Width")]          = create_scaled_bitmap("funnel");
        CATEGORY_ICON[L("Wipe options")]             = create_scaled_bitmap("funnel");
//         CATEGORY_ICON[L("Skirt and brim")]          = create_scaled_bitmap("skirt+brim"); 
//         CATEGORY_ICON[L("Speed > Acceleration")]    = create_scaled_bitmap("time");
        CATEGORY_ICON[L("Advanced")]                 = create_scaled_bitmap("wrench");
        // ptSLA
        CATEGORY_ICON[L("Supports")]                 = create_scaled_bitmap("support"/*"sla_supports"*/);
        CATEGORY_ICON[L("Pad")]                      = create_scaled_bitmap("pad");
        CATEGORY_ICON[L("Hollowing")]                = create_scaled_bitmap("hollowing");
    }

    // create control
    create_objects_ctrl();

    init_icons();

    // describe control behavior 
    Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [this](wxDataViewEvent& event) {
#ifndef __APPLE__
        // On Windows and Linux, forces a kill focus emulation on the object manipulator fields because this event handler is called
        // before the kill focus event handler on the object manipulator when changing selection in the list, invalidating the object
        // manipulator cache with the following call to selection_changed()
        wxGetApp().obj_manipul()->emulate_kill_focus();
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
		    this->HitTest(get_mouse_position_in_control(), item, col);
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
        set_tooltip_for_item(get_mouse_position_in_control());
#endif //__WXMSW__

#ifndef __WXOSX__
        list_manipulation();
#endif //__WXOSX__
    });

#ifdef __WXOSX__
    // Key events are not correctly processed by the wxDataViewCtrl on OSX.
    // Our patched wxWidgets process the keyboard accelerators.
    // On the other hand, using accelerators will break in-place editing on Windows & Linux/GTK (there is no in-place editing working on OSX for wxDataViewCtrl for now).
//    Bind(wxEVT_KEY_DOWN, &ObjectList::OnChar, this);
    {
        // Accelerators
        wxAcceleratorEntry entries[8];
        entries[0].Set(wxACCEL_CTRL, (int) 'C',    wxID_COPY);
        entries[1].Set(wxACCEL_CTRL, (int) 'X',    wxID_CUT);
        entries[2].Set(wxACCEL_CTRL, (int) 'V',    wxID_PASTE);
        entries[3].Set(wxACCEL_CTRL, (int) 'A',    wxID_SELECTALL);
        entries[4].Set(wxACCEL_CTRL, (int) 'Z',    wxID_UNDO);
        entries[5].Set(wxACCEL_CTRL, (int) 'Y',    wxID_REDO);
        entries[6].Set(wxACCEL_NORMAL, WXK_DELETE, wxID_DELETE);
        entries[7].Set(wxACCEL_NORMAL, WXK_BACK,   wxID_DELETE);
        wxAcceleratorTable accel(8, entries);
        SetAcceleratorTable(accel);

        this->Bind(wxEVT_MENU, [this](wxCommandEvent &evt) { this->copy();                      }, wxID_COPY);
        this->Bind(wxEVT_MENU, [this](wxCommandEvent &evt) { this->paste();                     }, wxID_PASTE);
        this->Bind(wxEVT_MENU, [this](wxCommandEvent &evt) { this->select_item_all_children();  }, wxID_SELECTALL);
        this->Bind(wxEVT_MENU, [this](wxCommandEvent &evt) { this->remove();                    }, wxID_DELETE);
        this->Bind(wxEVT_MENU, [this](wxCommandEvent &evt) { this->undo();  					}, wxID_UNDO);
        this->Bind(wxEVT_MENU, [this](wxCommandEvent &evt) { this->redo();                    	}, wxID_REDO);
    }
#else //__WXOSX__
    Bind(wxEVT_CHAR, [this](wxKeyEvent& event) { key_event(event); }); // doesn't work on OSX
#endif

#ifdef __WXMSW__
    GetMainWindow()->Bind(wxEVT_MOTION, [this](wxMouseEvent& event) {
        set_tooltip_for_item(get_mouse_position_in_control());
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

void ObjectList::create_objects_ctrl()
{
    /* Temporary workaround for the correct behavior of the Scrolled sidebar panel:
     * 1. set a height of the list to some big value 
     * 2. change it to the normal min value (15 * wxGetApp().em_unit()) after first whole Mainframe updating/layouting
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
    AppendColumn(new wxDataViewColumn(_(L("Name")), new BitmapTextRenderer(this),
        colName, 20*em, wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE));

    // column PrintableProperty (Icon) of the view control:
    AppendBitmapColumn(" ", colPrint, wxDATAVIEW_CELL_INERT, 3*em,
        wxALIGN_CENTER_HORIZONTAL, wxDATAVIEW_COL_RESIZABLE);

    // column Extruder of the view control:
    AppendColumn(new wxDataViewColumn(_(L("Extruder")), new BitmapChoiceRenderer(),
        colExtruder, 8*em, wxALIGN_CENTER_HORIZONTAL, wxDATAVIEW_COL_RESIZABLE));

    // column ItemEditing of the view control:
    AppendBitmapColumn(_(L("Editing")), colEditing, wxDATAVIEW_CELL_INERT, 3*em,
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

void ObjectList::create_popup_menus()
{
    // create popup menus for object and part
    create_object_popupmenu(&m_menu_object);
    create_part_popupmenu(&m_menu_part);
    create_sla_object_popupmenu(&m_menu_sla_object);
    create_instance_popupmenu(&m_menu_instance);
    create_default_popupmenu(&m_menu_default);
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
    wxString tooltip = wxString::Format(_(L("Auto-repaired (%d errors):")), errors) + "\n";

    const stl_stats& stats = vol_idx == -1 ?
                            (*m_objects)[obj_idx]->get_object_stl_stats() :
                            (*m_objects)[obj_idx]->volumes[vol_idx]->mesh().stl.stats;

    std::map<std::string, int> error_msg = {
        { L("degenerate facets"),   stats.degenerate_facets },
        { L("edges fixed"),         stats.edges_fixed       },
        { L("facets removed"),      stats.facets_removed    },
        { L("facets added"),        stats.facets_added      },
        { L("facets reversed"),     stats.facets_reversed   },
        { L("backwards edges"),     stats.backwards_edges   }
    };

    for (const auto& error : error_msg)
        if (error.second > 0)
            tooltip += from_u8((boost::format("\t%1% %2%\n") % error.second % _utf8(error.first)).str());

    if (is_windows10())
        tooltip += _(L("Right button click the icon to fix STL through Netfabb"));

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

wxPoint ObjectList::get_mouse_position_in_control()
{
    const wxPoint& pt = wxGetMousePosition();
//     wxWindow* win = GetMainWindow();
//     wxPoint screen_pos = win->GetScreenPosition();
    return wxPoint(pt.x - /*win->*/GetScreenPosition().x, pt.y - /*win->*/GetScreenPosition().y);
}

int ObjectList::get_selected_obj_idx() const
{
    if (GetSelectedItemsCount() == 1)
        return m_objects_model->GetIdByItem(m_objects_model->GetTopParent(GetSelection()));

    return -1;
}

DynamicPrintConfig& ObjectList::get_item_config(const wxDataViewItem& item) const 
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
            size_t(object->config.option<ConfigOptionInt>("extruder")->value) > max_extruder)
            extruder = _(L("default"));
        else
            extruder = wxString::Format("%d", object->config.option<ConfigOptionInt>("extruder")->value);

        m_objects_model->SetExtruder(extruder, item);

        if (object->volumes.size() > 1) {
            for (size_t id = 0; id < object->volumes.size(); id++) {
                item = m_objects_model->GetItemByVolumeId(i, id);
                if (!item) continue;
                if (!object->volumes[id]->config.has("extruder") ||
                    size_t(object->volumes[id]->config.option<ConfigOptionInt>("extruder")->value) > max_extruder)
                    extruder = _(L("default"));
                else
                    extruder = wxString::Format("%d", object->volumes[id]->config.option<ConfigOptionInt>("extruder")->value); 

                m_objects_model->SetExtruder(extruder, item);
            }
        }
    }
}

void ObjectList::update_objects_list_extruder_column(size_t extruders_count)
{
    if (!this) return; // #ys_FIXME
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

void ObjectList::init_icons()
{
    m_bmp_solidmesh         = ScalableBitmap(this, ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::MODEL_PART)        ].second);
    m_bmp_modifiermesh      = ScalableBitmap(this, ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::PARAMETER_MODIFIER)].second);
    m_bmp_support_enforcer  = ScalableBitmap(this, ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::SUPPORT_ENFORCER)  ].second);
    m_bmp_support_blocker   = ScalableBitmap(this, ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::SUPPORT_BLOCKER)   ].second); 

    m_bmp_vector.reserve(4); // bitmaps for different types of parts 
    m_bmp_vector.push_back(&m_bmp_solidmesh.bmp());         
    m_bmp_vector.push_back(&m_bmp_modifiermesh.bmp());      
    m_bmp_vector.push_back(&m_bmp_support_enforcer.bmp());  
    m_bmp_vector.push_back(&m_bmp_support_blocker.bmp());   


    // Set volumes default bitmaps for the model
    m_objects_model->SetVolumeBitmaps(m_bmp_vector);

    // init icon for manifold warning
    m_bmp_manifold_warning  = ScalableBitmap(this, "exclamation");
    // Set warning bitmap for the model
    m_objects_model->SetWarningBitmap(&m_bmp_manifold_warning.bmp());

    // init bitmap for "Add Settings" context menu
    m_bmp_cog               = ScalableBitmap(this, "cog");
}

void ObjectList::msw_rescale_icons()
{
    m_bmp_vector.clear();
    m_bmp_vector.reserve(4); // bitmaps for different types of parts 
    for (ScalableBitmap* bitmap : { &m_bmp_solidmesh,            // Add part
                                    &m_bmp_modifiermesh,         // Add modifier
                                    &m_bmp_support_enforcer,     // Add support enforcer
                                    &m_bmp_support_blocker })    // Add support blocker                                                           
    {
        bitmap->msw_rescale();
        m_bmp_vector.push_back(& bitmap->bmp());
    }
    // Set volumes default bitmaps for the model
    m_objects_model->SetVolumeBitmaps(m_bmp_vector);

    m_bmp_manifold_warning.msw_rescale();
    // Set warning bitmap for the model
    m_objects_model->SetWarningBitmap(&m_bmp_manifold_warning.bmp());

    m_bmp_cog.msw_rescale();


    // Update CATEGORY_ICON according to new scale
    {
        // ptFFF
        CATEGORY_ICON[L("Layers and Perimeters")]    = create_scaled_bitmap("layers");
        CATEGORY_ICON[L("Infill")]                   = create_scaled_bitmap("infill");
        CATEGORY_ICON[L("Support material")]         = create_scaled_bitmap("support");
        CATEGORY_ICON[L("Speed")]                    = create_scaled_bitmap("time");
        CATEGORY_ICON[L("Extruders")]                = create_scaled_bitmap("funnel");
        CATEGORY_ICON[L("Extrusion Width")]          = create_scaled_bitmap("funnel");
        CATEGORY_ICON[L("Wipe options")]             = create_scaled_bitmap("funnel");
//         CATEGORY_ICON[L("Skirt and brim")]          = create_scaled_bitmap("skirt+brim"); 
//         CATEGORY_ICON[L("Speed > Acceleration")]    = create_scaled_bitmap("time");
        CATEGORY_ICON[L("Advanced")]                 = create_scaled_bitmap("wrench");
        // ptSLA
        CATEGORY_ICON[L("Supports")]                 = create_scaled_bitmap("support"/*"sla_supports"*/);
        CATEGORY_ICON[L("Pad")]                      = create_scaled_bitmap("pad");
    }
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

void ObjectList::fill_layer_config_ranges_cache()
{
    wxDataViewItemArray sel_layers;
    GetSelections(sel_layers);

    const int obj_idx = m_objects_model->GetObjectIdByItem(sel_layers[0]);
    if (obj_idx < 0 || (int)m_objects->size() <= obj_idx)
        return;

    const t_layer_config_ranges& ranges = object(obj_idx)->layer_config_ranges;
    m_layer_config_ranges_cache.clear();

    for (const auto layer_item : sel_layers)
        if (m_objects_model->GetItemType(layer_item) & itLayer) {
            auto range = m_objects_model->GetLayerRangeByItem(layer_item);
            auto it = ranges.find(range);
            if (it != ranges.end())
                m_layer_config_ranges_cache[it->first] = it->second;
        }
}

void ObjectList::paste_layers_into_list()
{
    const int obj_idx = m_objects_model->GetObjectIdByItem(GetSelection());

    if (obj_idx < 0 || (int)m_objects->size() <= obj_idx || 
        m_layer_config_ranges_cache.empty() || printer_technology() == ptSLA)
        return;

    const wxDataViewItem object_item = m_objects_model->GetItemById(obj_idx);
    wxDataViewItem layers_item = m_objects_model->GetLayerRootItem(object_item);
    if (layers_item)
        m_objects_model->Delete(layers_item);

    t_layer_config_ranges& ranges = object(obj_idx)->layer_config_ranges;

    // and create Layer item(s) according to the layer_config_ranges
    for (const auto range : m_layer_config_ranges_cache)
        ranges.emplace(range);

    layers_item = add_layer_root_item(object_item);

    changed_object(obj_idx);

    select_item(layers_item);
#ifndef __WXOSX__
    selection_changed();
#endif //no __WXOSX__
}

void ObjectList::paste_volumes_into_list(int obj_idx, const ModelVolumePtrs& volumes)
{
    if ((obj_idx < 0) || ((int)m_objects->size() <= obj_idx))
        return;

    if (volumes.empty())
        return;

    const auto object_item = m_objects_model->GetItemById(obj_idx);

    wxDataViewItemArray items;

    for (const ModelVolume* volume : volumes)
    {
        const wxDataViewItem& vol_item = m_objects_model->AddVolumeChild(object_item, wxString::FromUTF8(volume->name.c_str()), volume->type(), 
            volume->get_mesh_errors_count()>0 ,
            volume->config.has("extruder") ? volume->config.option<ConfigOptionInt>("extruder")->value : 0);
        add_settings_item(vol_item, &volume->config);
        items.Add(vol_item);
    }

    changed_object(obj_idx);

    if (items.size() > 1)
    {
        m_selection_mode = smVolume;
        m_last_selected_item = wxDataViewItem(nullptr);
    }

    select_items(items);
//#ifndef __WXOSX__ //#ifdef __WXMSW__ // #ys_FIXME
    selection_changed();
//#endif //no __WXOSX__ //__WXMSW__
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
//#ifndef __WXOSX__ //#ifdef __WXMSW__ // #ys_FIXME
    selection_changed();
//#endif //no __WXOSX__ //__WXMSW__
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

void ObjectList::OnContextMenu(wxDataViewEvent&)
{
    // Do not show the context menu if the user pressed the right mouse button on the 3D scene and released it on the objects list
    GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
    bool evt_context_menu = (canvas != nullptr) ? !canvas->is_mouse_dragging() : true;
    if (!evt_context_menu)
        canvas->mouse_up_cleanup();

    list_manipulation(evt_context_menu);
}

void ObjectList::list_manipulation(bool evt_context_menu/* = false*/)
{
    wxDataViewItem item;
    wxDataViewColumn* col = nullptr;
    const wxPoint pt = get_mouse_position_in_control();
    HitTest(pt, item, col);

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

    const wxString title = col->GetTitle();

    if (title == " ")
        toggle_printable_state(item);
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
                pt.x > 2*wxGetApp().em_unit() && pt.x < 4*wxGetApp().em_unit() )
                fix_through_netfabb();
        }
    }
    // workaround for extruder editing under OSX 
    else if (wxOSX && evt_context_menu && title == _("Extruder"))
        extruder_editing();

#ifndef __WXMSW__
    GetMainWindow()->SetToolTip(""); // hide tooltip
#endif //__WXMSW__
}

void ObjectList::show_context_menu(const bool evt_context_menu)
{
    if (multiple_selection())
    {
        if (selected_instances_of_same_object())
            wxGetApp().plater()->PopupMenu(&m_menu_instance);
        else
            show_multi_selection_menu();

        return;
    }

    const auto item = GetSelection();
    wxMenu* menu {nullptr};
    if (item)
    {
        const ItemType type = m_objects_model->GetItemType(item);
        if (!(type & (itObject | itVolume | itLayer | itInstance)))
            return;

        menu = type & itInstance ? &m_menu_instance :
                       type & itLayer ? &m_menu_layer :
                       m_objects_model->GetParent(item) != wxDataViewItem(nullptr) ? &m_menu_part :
                       printer_technology() == ptFFF ? &m_menu_object : &m_menu_sla_object;

        if (!(type & itInstance))
            append_menu_item_settings(menu);
    }
    else if (evt_context_menu)
        menu = &m_menu_default;

    if (menu)
        wxGetApp().plater()->PopupMenu(menu);
}

void ObjectList::extruder_editing()
{
    wxDataViewItem item = GetSelection();
    if (!item || !(m_objects_model->GetItemType(item) & (itVolume | itObject)))
        return;

    const int column_width = GetColumn(colExtruder)->GetWidth() + wxSystemSettings::GetMetric(wxSYS_VSCROLL_X) + 5;

    wxPoint pos = get_mouse_position_in_control();
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
    // if (m_selection_mode & smLayer)
    //     fill_layer_config_ranges_cache();
    // else {
    //     m_layer_config_ranges_cache.clear();
        wxPostEvent((wxEvtHandler*)wxGetApp().plater()->canvas3D()->get_wxglcanvas(), SimpleEvent(EVT_GLTOOLBAR_COPY));
    // }
}

void ObjectList::paste()
{
    // if (!m_layer_config_ranges_cache.empty())
    //     paste_layers_into_list();
    // else
        wxPostEvent((wxEvtHandler*)wxGetApp().plater()->canvas3D()->get_wxglcanvas(), SimpleEvent(EVT_GLTOOLBAR_PASTE));
}

void ObjectList::undo()
{
	wxGetApp().plater()->undo();
}

void ObjectList::redo()
{
	wxGetApp().plater()->redo();	
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
    if (m_dragged_data.type() & itVolume)
        return m_dragged_data.obj_idx() == m_objects_model->GetObjectIdByItem(item);

    return true;
}

void ObjectList::OnDropPossible(wxDataViewEvent &event)
{
    const wxDataViewItem& item = event.GetItem();

    if (!can_drop(item))
        event.Veto();
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

// It looks like a fixed in current version of the wxWidgets
// #ifdef __WXGTK__
//     /* Under GTK, DnD moves an item between another two items.
//     * And event.GetItem() return item, which is under "insertion line"
//     * So, if we move item down we should to decrease the to_volume_id value
//     **/
//     if (to_volume_id > from_volume_id) to_volume_id--;
// #endif // __WXGTK__

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


// Context Menu

std::vector<std::string> ObjectList::get_options(const bool is_part)
{
    if (printer_technology() == ptSLA) {
        SLAPrintObjectConfig full_sla_config;
        auto options = full_sla_config.keys();
        options.erase(find(options.begin(), options.end(), "layer_height"));
        return options;
    }

    PrintRegionConfig reg_config;
    auto options = reg_config.keys();
    if (!is_part) {
        PrintObjectConfig obj_config;
        std::vector<std::string> obj_options = obj_config.keys();
        options.insert(options.end(), obj_options.begin(), obj_options.end());
    }
    return options;
}
    
const std::vector<std::string>& ObjectList::get_options_for_bundle(const wxString& bundle_name)
{
    const SettingsBundle& bundle = printer_technology() == ptSLA ? 
                                       FREQ_SETTINGS_BUNDLE_SLA : FREQ_SETTINGS_BUNDLE_FFF;

    for (auto& it : bundle)
    {
        if (bundle_name == _(it.first))
            return it.second;
    }
#if 0
    // if "Quick menu" is selected
    SettingsBundle& bundle_quick = printer_technology() == ptSLA ?
                                       m_freq_settings_sla: m_freq_settings_fff;

    for (auto& it : bundle_quick)
    {
        if ( bundle_name == from_u8((boost::format(_utf8(L("Quick Add Settings (%s)"))) % _(it.first)).str()) )
            return it.second;
    }
#endif

	static std::vector<std::string> empty;
	return empty;
}

static bool improper_category(const std::string& category, const int extruders_cnt, const bool is_object_settings = true)
{
    return  category.empty() || 
            (extruders_cnt == 1 && (category == "Extruders" || category == "Wipe options" )) ||
            (!is_object_settings && category == "Support material");
}

void ObjectList::get_options_menu(settings_menu_hierarchy& settings_menu, const bool is_part)
{
    auto options = get_options(is_part);

    const int extruders_cnt = extruders_count();

    DynamicPrintConfig config;
    for (auto& option : options)
    {
        auto const opt = config.def()->get(option);
        auto category = opt->category;
        if (improper_category(category, extruders_cnt, !is_part))
            continue;

        const std::string& label = !opt->full_label.empty() ? opt->full_label : opt->label;
        std::pair<std::string, std::string> option_label(option, label);
        std::vector< std::pair<std::string, std::string> > new_category;
        auto& cat_opt_label = settings_menu.find(category) == settings_menu.end() ? new_category : settings_menu.at(category);
        cat_opt_label.push_back(option_label);
        if (cat_opt_label.size() == 1)
            settings_menu[category] = cat_opt_label;
    }
}

void ObjectList::get_settings_choice(const wxString& category_name)
{
    wxArrayString names;
    wxArrayInt selections;

    /* If we try to add settings for object/part from 3Dscene,
     * for the second try there is selected ItemSettings in ObjectList.
     * So, check if selected item isn't SettingsItem. And get a SettingsItem's parent item, if yes
     */
    const wxDataViewItem selected_item = GetSelection();
    wxDataViewItem item = m_objects_model->GetItemType(selected_item) & itSettings ? m_objects_model->GetParent(selected_item) : selected_item;

    const ItemType item_type = m_objects_model->GetItemType(item);

    settings_menu_hierarchy settings_menu;
    const bool is_part = item_type & (itVolume | itLayer);
    get_options_menu(settings_menu, is_part);
    std::vector< std::pair<std::string, std::string> > *settings_list = nullptr;

    if (!m_config)
        m_config = &get_item_config(item);

    assert(m_config);
    auto opt_keys = m_config->keys();

    for (auto& cat : settings_menu)
    {
        if (_(cat.first) == category_name) {
            int sel = 0;
            for (auto& pair : cat.second) {
                names.Add(_(pair.second));
                if (find(opt_keys.begin(), opt_keys.end(), pair.first) != opt_keys.end())
                    selections.Add(sel);
                sel++;
            }
            settings_list = &cat.second;
            break;
        }
    }

    if (!settings_list)
        return;

    if (wxGetSelectedChoices(selections, _(L("Select showing settings")), category_name, names) == -1)
        return;

    const int selection_cnt = selections.size();
#if 0
    if (selection_cnt > 0) 
    {
        // Add selected items to the "Quick menu"
        SettingsBundle& freq_settings = printer_technology() == ptSLA ?
                                            m_freq_settings_sla : m_freq_settings_fff;
        bool changed_existing = false;

        std::vector<std::string> tmp_freq_cat = {};
        
        for (auto& cat : freq_settings)
        {
            if (_(cat.first) == category_name)
            {
                std::vector<std::string>& freq_settings_category = cat.second;
                freq_settings_category.clear();
                freq_settings_category.reserve(selection_cnt);
                for (auto sel : selections)
                    freq_settings_category.push_back((*settings_list)[sel].first);

                changed_existing = true;
                break;
            }
        }

        if (!changed_existing)
        {
            // Create new "Quick menu" item
            for (auto& cat : settings_menu)
            {
                if (_(cat.first) == category_name)
                {
                    freq_settings[cat.first] = std::vector<std::string> {};

                    std::vector<std::string>& freq_settings_category = freq_settings.find(cat.first)->second;
                    freq_settings_category.reserve(selection_cnt);
                    for (auto sel : selections)
                        freq_settings_category.push_back((*settings_list)[sel].first);
                    break;
                }
            }
        }
    }
#endif

    const wxString snapshot_text =  item_type & itLayer   ? _(L("Add Settings for Layers")) :
                                    item_type & itVolume  ? _(L("Add Settings for Sub-object")) :
                                                            _(L("Add Settings for Object"));
    take_snapshot(snapshot_text);

    std::vector <std::string> selected_options;
    selected_options.reserve(selection_cnt);
    for (auto sel : selections)
        selected_options.push_back((*settings_list)[sel].first);

    const DynamicPrintConfig& from_config = printer_technology() == ptFFF ? 
                                            wxGetApp().preset_bundle->prints.get_edited_preset().config : 
                                            wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;

    for (auto& setting : (*settings_list))
    {
        auto& opt_key = setting.first;
        if (find(opt_keys.begin(), opt_keys.end(), opt_key) != opt_keys.end() &&
            find(selected_options.begin(), selected_options.end(), opt_key) == selected_options.end())
            m_config->erase(opt_key);

        if (find(opt_keys.begin(), opt_keys.end(), opt_key) == opt_keys.end() &&
            find(selected_options.begin(), selected_options.end(), opt_key) != selected_options.end()) {
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
    show_settings(add_settings_item(item, m_config));
}

void ObjectList::get_freq_settings_choice(const wxString& bundle_name)
{
    std::vector<std::string> options = get_options_for_bundle(bundle_name);
    const Selection& selection = scene_selection();
    const wxDataViewItem sel_item = // when all instances in object are selected
                                    GetSelectedItemsCount() > 1 && selection.is_single_full_object() ? 
                                    m_objects_model->GetItemById(selection.get_object_idx()) : 
                                    GetSelection();

    /* If we try to add settings for object/part from 3Dscene,
     * for the second try there is selected ItemSettings in ObjectList.
     * So, check if selected item isn't SettingsItem. And get a SettingsItem's parent item, if yes
     */
    wxDataViewItem item = m_objects_model->GetItemType(sel_item) & itSettings ? m_objects_model->GetParent(sel_item) : sel_item;
    const ItemType item_type = m_objects_model->GetItemType(item);

    /* Because of we couldn't edited layer_height for ItVolume from settings list,
     * correct options according to the selected item type :
     * remove "layer_height" option
     */
    if ((item_type & itVolume) && bundle_name == _("Layers and Perimeters")) {
        const auto layer_height_it = std::find(options.begin(), options.end(), "layer_height");
        if (layer_height_it != options.end())
            options.erase(layer_height_it);
    }

    if (!m_config)
        m_config = &get_item_config(item);

    assert(m_config);
    auto opt_keys = m_config->keys();

    const wxString snapshot_text = item_type & itLayer  ? _(L("Add Settings Bundle for Height range")) :
                                   item_type & itVolume ? _(L("Add Settings Bundle for Sub-object")) :
                                                          _(L("Add Settings Bundle for Object"));
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
    show_settings(add_settings_item(item, m_config));
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

wxMenu* ObjectList::append_submenu_add_generic(wxMenu* menu, const ModelVolumeType type) {
    auto sub_menu = new wxMenu;

    if (wxGetApp().get_mode() == comExpert && type != ModelVolumeType::INVALID) {
    append_menu_item(sub_menu, wxID_ANY, _(L("Load")) + " " + dots, "",
        [this, type](wxCommandEvent&) { load_subobject(type); }, "", menu);
    sub_menu->AppendSeparator();
    }

    for (auto& item : { L("Box"), L("Cylinder"), L("Sphere"), L("Slab") })
    {
        if (type == ModelVolumeType::INVALID && strncmp(item, "Slab", 4) == 0)
            continue;
        append_menu_item(sub_menu, wxID_ANY, _(item), "",
            [this, type, item](wxCommandEvent&) { load_generic_subobject(item, type); }, "", menu);
    }

    return sub_menu;
}

void ObjectList::append_menu_items_add_volume(wxMenu* menu)
{
    // Update "add" items(delete old & create new)  settings popupmenu
    for (auto& item : ADD_VOLUME_MENU_ITEMS){
        const auto settings_id = menu->FindItem(_(item.first));
        if (settings_id != wxNOT_FOUND)
            menu->Destroy(settings_id);
    }

    const ConfigOptionMode mode = wxGetApp().get_mode();

    wxWindow* parent = wxGetApp().plater();

    if (mode == comAdvanced) {
        append_menu_item(menu, wxID_ANY, _(ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::MODEL_PART)].first), "",
            [this](wxCommandEvent&) { load_subobject(ModelVolumeType::MODEL_PART); }, 
            ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::MODEL_PART)].second, nullptr,
            [this]() { return is_instance_or_object_selected(); }, parent);
    }
    if (mode == comSimple) {
        append_menu_item(menu, wxID_ANY, _(ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::SUPPORT_ENFORCER)].first), "",
            [this](wxCommandEvent&) { load_generic_subobject(L("Box"), ModelVolumeType::SUPPORT_ENFORCER); },
            ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::SUPPORT_ENFORCER)].second, nullptr,
            [this]() { return is_instance_or_object_selected(); }, parent);
        append_menu_item(menu, wxID_ANY, _(ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::SUPPORT_BLOCKER)].first), "",
            [this](wxCommandEvent&) { load_generic_subobject(L("Box"), ModelVolumeType::SUPPORT_BLOCKER); },
            ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::SUPPORT_BLOCKER)].second, nullptr,
            [this]() { return is_instance_or_object_selected(); }, parent);

        return;
    }
    
    for (size_t type = (mode == comExpert ? 0 : 1) ; type < ADD_VOLUME_MENU_ITEMS.size(); type++)
    {
        auto& item = ADD_VOLUME_MENU_ITEMS[type];

        wxMenu* sub_menu = append_submenu_add_generic(menu, ModelVolumeType(type));
        append_submenu(menu, sub_menu, wxID_ANY, _(item.first), "", item.second,
            [this]() { return is_instance_or_object_selected(); }, parent);
    }
}

wxMenuItem* ObjectList::append_menu_item_split(wxMenu* menu) 
{
    return append_menu_item(menu, wxID_ANY, _(L("Split to parts")), "",
        [this](wxCommandEvent&) { split(); }, "split_parts_SMALL", menu, 
        [this]() { return is_splittable(); }, wxGetApp().plater());
}

bool ObjectList::is_instance_or_object_selected()
{
    const Selection& selection = scene_selection();
    return selection.is_single_full_instance() || selection.is_single_full_object();
}

wxMenuItem* ObjectList::append_menu_item_layers_editing(wxMenu* menu, wxWindow* parent)
{
    return append_menu_item(menu, wxID_ANY, _(L("Height range Modifier")), "",
        [this](wxCommandEvent&) { layers_editing(); }, "edit_layers_all", menu,
        [this]() { return is_instance_or_object_selected(); }, parent);
}

wxMenuItem* ObjectList::append_menu_item_settings(wxMenu* menu_) 
{
    MenuWithSeparators* menu = dynamic_cast<MenuWithSeparators*>(menu_);

    const wxString menu_name = _(L("Add settings"));
    // Delete old items from settings popupmenu
    auto settings_id = menu->FindItem(menu_name);
    if (settings_id != wxNOT_FOUND)
        menu->Destroy(settings_id);

    for (auto& it : FREQ_SETTINGS_BUNDLE_FFF)
    {
        settings_id = menu->FindItem(_(it.first));
        if (settings_id != wxNOT_FOUND)
            menu->Destroy(settings_id);
    }
    for (auto& it : FREQ_SETTINGS_BUNDLE_SLA)
    {
        settings_id = menu->FindItem(_(it.first));
        if (settings_id != wxNOT_FOUND)
            menu->Destroy(settings_id);
    }
#if 0
    for (auto& it : m_freq_settings_fff)
    {
        settings_id = menu->FindItem(from_u8((boost::format(_utf8(L("Quick Add Settings (%s)"))) % _(it.first)).str()));
        if (settings_id != wxNOT_FOUND)
            menu->Destroy(settings_id);
    }
    for (auto& it : m_freq_settings_sla)
    {
        settings_id = menu->FindItem(from_u8((boost::format(_utf8(L("Quick Add Settings (%s)"))) % _(it.first)).str()));
        if (settings_id != wxNOT_FOUND)
            menu->Destroy(settings_id);
    }
#endif
    menu->DestroySeparators(); // delete old separators

    // If there are selected more then one instance but not all of them
    // don't add settings menu items
    const Selection& selection = scene_selection();
    if (selection.is_multiple_full_instance() && !selection.is_single_full_object() || 
        selection.is_multiple_volume() || selection.is_mixed() ) // more than one volume(part) is selected on the scene
        return nullptr;

    const auto sel_vol = get_selected_model_volume();
    if (sel_vol && sel_vol->type() >= ModelVolumeType::SUPPORT_ENFORCER)
        return nullptr;

    const ConfigOptionMode mode = wxGetApp().get_mode();
    if (mode == comSimple)
        return nullptr;

    // Create new items for settings popupmenu

    if (printer_technology() == ptFFF ||
       (menu->GetMenuItems().size() > 0 && !menu->GetMenuItems().back()->IsSeparator()))
        menu->SetFirstSeparator();

    // Add frequently settings
    const ItemType item_type = m_objects_model->GetItemType(GetSelection());
    if (item_type == itUndef && !selection.is_single_full_object())
        return nullptr;
    const bool is_object_settings = item_type & itObject || item_type & itInstance ||
                                    // multi-selection in ObjectList, but full_object in Selection
                                    (item_type == itUndef && selection.is_single_full_object()); 
    create_freq_settings_popupmenu(menu, is_object_settings);

    if (mode == comAdvanced)
        return nullptr;

    menu->SetSecondSeparator();

    // Add full settings list
    auto  menu_item = new wxMenuItem(menu, wxID_ANY, menu_name);
    menu_item->SetBitmap(m_bmp_cog.bmp());

    menu_item->SetSubMenu(create_settings_popupmenu(menu));

    return menu->Append(menu_item);
}

wxMenuItem* ObjectList::append_menu_item_change_type(wxMenu* menu, wxWindow* parent/* = nullptr*/)
{
    return append_menu_item(menu, wxID_ANY, _(L("Change type")), "",
        [this](wxCommandEvent&) { change_part_type(); }, "", menu, 
        [this]() {
            wxDataViewItem item = GetSelection();
            return item.IsOk() || m_objects_model->GetItemType(item) == itVolume;
        }, parent);
}

wxMenuItem* ObjectList::append_menu_item_instance_to_object(wxMenu* menu, wxWindow* parent)
{
    wxMenuItem* menu_item = append_menu_item(menu, wxID_ANY, _(L("Set as a Separated Object")), "",
        [this](wxCommandEvent&) { split_instances(); }, "", menu);

    /* New behavior logic:
     * 1. Split Object to several separated object, if ALL instances are selected
     * 2. Separate selected instances from the initial object to the separated object,
     *    if some (not all) instances are selected
     */
    parent->Bind(wxEVT_UPDATE_UI, [](wxUpdateUIEvent& evt)
    {
        const Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();
        evt.SetText(selection.is_single_full_object() ?
        _(L("Set as a Separated Objects")) : _(L("Set as a Separated Object")));

        evt.Enable(wxGetApp().plater()->can_set_instance_to_object());
    }, menu_item->GetId());

    return menu_item;
}

wxMenuItem* ObjectList::append_menu_item_printable(wxMenu* menu, wxWindow* /*parent*/)
{
    return append_menu_check_item(menu, wxID_ANY, _(L("Printable")), "", [this](wxCommandEvent&) {
        const Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();
        wxDataViewItem item;
        if (GetSelectedItemsCount() > 1 && selection.is_single_full_object())
            item = m_objects_model->GetItemById(selection.get_object_idx());
        else
            item = GetSelection();

        if (item)
            toggle_printable_state(item);
    }, menu);
}

void ObjectList::append_menu_items_osx(wxMenu* menu)
{
    append_menu_item(menu, wxID_ANY, _(L("Rename")), "",
        [this](wxCommandEvent&) { rename_item(); }, "", menu);
    
    menu->AppendSeparator();
}

wxMenuItem* ObjectList::append_menu_item_fix_through_netfabb(wxMenu* menu)
{
    if (!is_windows10())
        return nullptr;
    Plater* plater = wxGetApp().plater();
    wxMenuItem* menu_item = append_menu_item(menu, wxID_ANY, _(L("Fix through the Netfabb")), "",
        [this](wxCommandEvent&) { fix_through_netfabb(); }, "", menu, 
        [plater]() {return plater->can_fix_through_netfabb(); }, plater);
    menu->AppendSeparator();

    return menu_item;
}

void ObjectList::append_menu_item_export_stl(wxMenu* menu) const 
{
    append_menu_item(menu, wxID_ANY, _(L("Export as STL")) + dots, "",
        [](wxCommandEvent&) { wxGetApp().plater()->export_stl(false, true); }, "", menu);
    menu->AppendSeparator();
}

void ObjectList::append_menu_item_reload_from_disk(wxMenu* menu) const
{
    append_menu_item(menu, wxID_ANY, _(L("Reload from disk")), _(L("Reload the selected volumes from disk")),
        [this](wxCommandEvent&) { wxGetApp().plater()->reload_from_disk(); }, "", menu,
        []() { return wxGetApp().plater()->can_reload_from_disk(); }, wxGetApp().plater());
}

void ObjectList::append_menu_item_change_extruder(wxMenu* menu)
{
    const std::vector<wxString> names = {_(L("Change extruder")), _(L("Set extruder for selected items")) };
    // Delete old menu item
    for (const wxString& name : names) {
        const int item_id = menu->FindItem(name);
        if (item_id != wxNOT_FOUND)
            menu->Destroy(item_id);
    }

    const int extruders_cnt = extruders_count();
    if (extruders_cnt <= 1)
        return;

    wxDataViewItemArray sels;
    GetSelections(sels);
    if (sels.IsEmpty())
        return;

    std::vector<wxBitmap*> icons = get_extruder_color_icons(true);
    wxMenu* extruder_selection_menu = new wxMenu();
    const wxString& name = sels.Count()==1 ? names[0] : names[1];

    int initial_extruder = -1; // negative value for multiple object/part selection
    if (sels.Count()==1) {
        DynamicPrintConfig& config = get_item_config(sels[0]);
        initial_extruder = !config.has("extruder") ? 0 : 
                            config.option<ConfigOptionInt>("extruder")->value;
    }

    for (int i = 0; i <= extruders_cnt; i++)
    {
        bool is_active_extruder = i == initial_extruder;
        int icon_idx = i == 0 ? 0 : i - 1;

        const wxString& item_name = (i == 0 ? _(L("Default")) : wxString::Format(_(L("Extruder %d")), i)) +
                                    (is_active_extruder ? " (" + _(L("active")) + ")" : "");

        append_menu_item(extruder_selection_menu, wxID_ANY, item_name, "",
            [this, i](wxCommandEvent&) { set_extruder_for_selected_items(i); }, *icons[icon_idx], menu,
            [is_active_extruder]() { return !is_active_extruder; }, GUI::wxGetApp().plater());

    }

    menu->AppendSubMenu(extruder_selection_menu, name);
}

void ObjectList::append_menu_item_delete(wxMenu* menu)
{
    append_menu_item(menu, wxID_ANY, _(L("Delete")), "",
        [this](wxCommandEvent&) { remove(); }, "", menu);
}

void ObjectList::append_menu_item_scale_selection_to_fit_print_volume(wxMenu* menu)
{
    append_menu_item(menu, wxID_ANY, _(L("Scale to print volume")), _(L("Scale the selected object to fit the print volume")),
        [](wxCommandEvent&) { wxGetApp().plater()->scale_selection_to_fit_print_volume(); }, "", menu);
}

void ObjectList::create_object_popupmenu(wxMenu *menu)
{
#ifdef __WXOSX__  
    append_menu_items_osx(menu);
#endif // __WXOSX__

    append_menu_item_reload_from_disk(menu);
    append_menu_item_export_stl(menu);
    append_menu_item_fix_through_netfabb(menu);
    append_menu_item_scale_selection_to_fit_print_volume(menu);

    // Split object to parts
    append_menu_item_split(menu);
    menu->AppendSeparator();

    // Layers Editing for object
    append_menu_item_layers_editing(menu, wxGetApp().plater());
    menu->AppendSeparator();

    // rest of a object_menu will be added later in:
    // - append_menu_items_add_volume() -> for "Add (volumes)"
    // - append_menu_item_settings() -> for "Add (settings)"
}

void ObjectList::create_sla_object_popupmenu(wxMenu *menu)
{
#ifdef __WXOSX__  
    append_menu_items_osx(menu);
#endif // __WXOSX__

    append_menu_item_reload_from_disk(menu);
    append_menu_item_export_stl(menu);
    append_menu_item_fix_through_netfabb(menu);
    // rest of a object_sla_menu will be added later in:
    // - append_menu_item_settings() -> for "Add (settings)"
}

void ObjectList::create_part_popupmenu(wxMenu *menu)
{
#ifdef __WXOSX__  
    append_menu_items_osx(menu);
#endif // __WXOSX__

    append_menu_item_reload_from_disk(menu);
    append_menu_item_export_stl(menu);
    append_menu_item_fix_through_netfabb(menu);

    append_menu_item_split(menu);

    // Append change part type
    menu->AppendSeparator();
    append_menu_item_change_type(menu);

    // rest of a object_sla_menu will be added later in:
    // - append_menu_item_settings() -> for "Add (settings)"
}

void ObjectList::create_instance_popupmenu(wxMenu*menu)
{
    m_menu_item_split_instances = append_menu_item_instance_to_object(menu, wxGetApp().plater());
}

void ObjectList::create_default_popupmenu(wxMenu*menu)
{
    wxMenu* sub_menu = append_submenu_add_generic(menu, ModelVolumeType::INVALID);
    append_submenu(menu, sub_menu, wxID_ANY, _(L("Add Shape")), "", "add_part", 
        [](){return true;}, this);
}

wxMenu* ObjectList::create_settings_popupmenu(wxMenu *parent_menu)
{
    wxMenu *menu = new wxMenu;

    settings_menu_hierarchy settings_menu;

    /* If we try to add settings for object/part from 3Dscene, 
     * for the second try there is selected ItemSettings in ObjectList.
     * So, check if selected item isn't SettingsItem. And get a SettingsItem's parent item, if yes
     */
    const wxDataViewItem selected_item = GetSelection();
    wxDataViewItem item = m_objects_model->GetItemType(selected_item) & itSettings ? m_objects_model->GetParent(selected_item) : selected_item;

    const bool is_part = !(m_objects_model->GetItemType(item) == itObject || scene_selection().is_single_full_object());
    get_options_menu(settings_menu, is_part);

    for (auto cat : settings_menu) {
        append_menu_item(menu, wxID_ANY, _(cat.first), "",
                        [menu, this](wxCommandEvent& event) { get_settings_choice(menu->GetLabel(event.GetId())); }, 
                        CATEGORY_ICON.find(cat.first) == CATEGORY_ICON.end() ? wxNullBitmap : CATEGORY_ICON.at(cat.first), parent_menu,
                        [this]() { return true; }, wxGetApp().plater());
    }

    return menu;
}

void ObjectList::create_freq_settings_popupmenu(wxMenu *menu, const bool is_object_settings/* = true*/)
{
    // Add default settings bundles
    const SettingsBundle& bundle = printer_technology() == ptFFF ?
                                     FREQ_SETTINGS_BUNDLE_FFF : FREQ_SETTINGS_BUNDLE_SLA;

    const int extruders_cnt = extruders_count();

    for (auto& it : bundle) {
        if (improper_category(it.first, extruders_cnt, is_object_settings)) 
            continue;

        append_menu_item(menu, wxID_ANY, _(it.first), "",
                        [menu, this](wxCommandEvent& event) { get_freq_settings_choice(menu->GetLabel(event.GetId())); }, 
                        CATEGORY_ICON.find(it.first) == CATEGORY_ICON.end() ? wxNullBitmap : CATEGORY_ICON.at(it.first), menu,
                        [this]() { return true; }, wxGetApp().plater());
    }
#if 0
    // Add "Quick" settings bundles
    const SettingsBundle& bundle_quick = printer_technology() == ptFFF ?
                                             m_freq_settings_fff : m_freq_settings_sla;

    for (auto& it : bundle_quick) {
        if (improper_category(it.first, extruders_cnt))
            continue;

        append_menu_item(menu, wxID_ANY, from_u8((boost::format(_utf8(L("Quick Add Settings (%s)"))) % _(it.first)).str()), "",
                        [menu, this](wxCommandEvent& event) { get_freq_settings_choice(menu->GetLabel(event.GetId())); }, 
                        CATEGORY_ICON.find(it.first) == CATEGORY_ICON.end() ? wxNullBitmap : CATEGORY_ICON.at(it.first), menu,
                        [this]() { return true; }, wxGetApp().plater());
    }
#endif
}

void ObjectList::update_opt_keys(t_config_option_keys& opt_keys, const bool is_object)
{
    auto full_current_opts = get_options(!is_object);
    for (int i = opt_keys.size()-1; i >= 0; --i)
        if (find(full_current_opts.begin(), full_current_opts.end(), opt_keys[i]) == full_current_opts.end())
            opt_keys.erase(opt_keys.begin() + i);
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

    take_snapshot(_(L("Load Part")));

    std::vector<std::pair<wxString, bool>> volumes_info;
    load_part((*m_objects)[obj_idx], volumes_info, type);


    changed_object(obj_idx);
    if (type == ModelVolumeType::MODEL_PART)
        // update printable state on canvas
        wxGetApp().plater()->canvas3D()->update_instance_printable_state_for_object((size_t)obj_idx);

    wxDataViewItem sel_item;
    for (const auto& volume : volumes_info )
        sel_item = m_objects_model->AddVolumeChild(item, volume.first, type, volume.second);
        
    if (sel_item)
        select_item(sel_item);

//#ifndef __WXOSX__ //#ifdef __WXMSW__ // #ys_FIXME
    selection_changed();
//#endif //no __WXOSX__ //__WXMSW__
}

void ObjectList::load_part( ModelObject* model_object,
                            std::vector<std::pair<wxString, bool>> &volumes_info,
                            ModelVolumeType type)
{
    wxWindow* parent = wxGetApp().tab_panel()->GetPage(0);

    wxArrayString input_files;
    wxGetApp().import_model(parent, input_files);
    for (size_t i = 0; i < input_files.size(); ++i) {
        std::string input_file = input_files.Item(i).ToUTF8().data();

        Model model;
        try {
            model = Model::read_from_file(input_file);
        }
        catch (std::exception &e) {
            auto msg = _(L("Error!")) + " " + input_file + " : " + e.what() + ".";
            show_error(parent, msg);
            exit(1);
        }

        for (auto object : model.objects) {
            Vec3d delta = Vec3d::Zero();
            if (model_object->origin_translation != Vec3d::Zero())
            {
                object->center_around_origin();
                delta = model_object->origin_translation - object->origin_translation;
            }
            for (auto volume : object->volumes) {
                volume->translate(delta);
                auto new_volume = model_object->add_volume(*volume);
                new_volume->set_type(type);
                new_volume->name = boost::filesystem::path(input_file).filename().string();

                volumes_info.push_back(std::make_pair(from_u8(new_volume->name), new_volume->get_mesh_errors_count()>0));

                // set a default extruder value, since user can't add it manually
                new_volume->config.set_key_value("extruder", new ConfigOptionInt(0));
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
    ModelVolume *new_volume = model_object.add_volume(std::move(mesh));
    new_volume->set_type(type);

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

    changed_object(obj_idx);
    if (type == ModelVolumeType::MODEL_PART)
        // update printable state on canvas
        wxGetApp().plater()->canvas3D()->update_instance_printable_state_for_object((size_t)obj_idx);

    const auto object_item = m_objects_model->GetTopParent(GetSelection());
    select_item(m_objects_model->AddVolumeChild(object_item, name, type, 
        new_volume->get_mesh_errors_count()>0));
//#ifndef __WXOSX__ //#ifdef __WXMSW__ // #ys_FIXME
    selection_changed();
//#endif //no __WXOSX__ //__WXMSW__
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

    take_snapshot(_(L("Add Shape")));

    // Create mesh
    BoundingBoxf3 bb;
    TriangleMesh mesh = create_mesh(type_name, bb);

    // Add mesh to model as a new object
    Model& model = wxGetApp().plater()->model();
    const wxString name = _(L("Shape")) + "-" + _(type_name);

#ifdef _DEBUG
    check_model_ids_validity(model);
#endif /* _DEBUG */

    std::vector<size_t> object_idxs;
    ModelObject* new_object = model.add_object();
    new_object->name = into_u8(name);
    new_object->add_instance(); // each object should have at list one instance

    ModelVolume* new_volume = new_object->add_volume(mesh);
    new_volume->name = into_u8(name);
    // set a default extruder value, since user can't add it manually
    new_volume->config.set_key_value("extruder", new ConfigOptionInt(0));
    new_object->invalidate_bounding_box();

    new_object->center_around_origin();
    new_object->ensure_on_bed();

    const BoundingBoxf bed_shape = wxGetApp().plater()->bed_shape_bb();
    new_object->instances[0]->set_offset(Slic3r::to_3d(bed_shape.center().cast<double>(), -new_object->origin_translation(2)));

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

    if (type & itSettings)
        del_settings_from_config(m_objects_model->GetParent(item));
    else if (type & itInstanceRoot && obj_idx != -1)
        del_instances_from_object(obj_idx);
    else if (type & itLayerRoot && obj_idx != -1)
        del_layers_from_object(obj_idx);
    else if (type & itLayer && obj_idx != -1)
        del_layer_from_object(obj_idx, m_objects_model->GetLayerRangeByItem(item));
    else if (idx == -1)
        return;
    else if (!del_subobject_from_object(obj_idx, idx, type))
        return;

    // If last volume item with warning was deleted, unmark object item
    if (type & itVolume && (*m_objects)[obj_idx]->get_mesh_errors_count() == 0)
        m_objects_model->DeleteWarningIcon(m_objects_model->GetParent(item));

    // If last two Instances of object is selected, show the message about impossible action
    bool show_msg = false;
    if (type & itInstance) { 
        wxDataViewItemArray instances;
        m_objects_model->GetChildren(m_objects_model->GetParent(item), instances);
        if (instances.Count() == 2 && IsSelected(instances[0]) && IsSelected(instances[1]))
            show_msg = true;
    }

    m_objects_model->Delete(item);

    if (show_msg)
        Slic3r::GUI::show_error(nullptr, _(L("Last instance of an object cannot be deleted.")));
}

void ObjectList::del_settings_from_config(const wxDataViewItem& parent_item)
{
    const bool is_layer_settings = m_objects_model->GetItemType(parent_item) == itLayer;

    const size_t opt_cnt = m_config->keys().size();
    if ((opt_cnt == 1 && m_config->has("extruder")) ||
        (is_layer_settings && opt_cnt == 2 && m_config->has("extruder") && m_config->has("layer_height")))
        return;

    take_snapshot(_(L("Delete Settings")));

    int extruder = -1;
    if (m_config->has("extruder"))
        extruder = m_config->option<ConfigOptionInt>("extruder")->value;

    coordf_t layer_height = 0.0;
    if (is_layer_settings)
        layer_height = m_config->opt_float("layer_height");

    m_config->clear();

    if (extruder >= 0)
        m_config->set_key_value("extruder", new ConfigOptionInt(extruder));
    if (is_layer_settings)
        m_config->set_key_value("layer_height", new ConfigOptionFloat(layer_height));
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
	if (obj_idx == 1000)
		// Cannot delete a wipe tower.
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
            volume->is_modifier() ? ModelVolumeType::PARAMETER_MODIFIER : ModelVolumeType::MODEL_PART,
            volume->get_mesh_errors_count()>0,
            volume->config.has("extruder") ?
            volume->config.option<ConfigOptionInt>("extruder")->value : 0,
            false);
        // add settings to the part, if it has those
        add_settings_item(vol_item, &volume->config);
    }

    model_object->input_file.clear();

    if (parent == item)
        Expand(parent);

    changed_object(obj_idx);
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
            ranges[{ 0.0f, 2.0f }] = get_default_layer_config(obj_idx);
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

bool ObjectList::is_splittable()
{
    const wxDataViewItem item = GetSelection();
    if (!item) return false;

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

    if ( multiple_selection() || (item && m_objects_model->GetItemType(item) == itInstanceRoot ))
    {
        og_name = _(L("Group manipulation"));

        const Selection& selection = scene_selection();
        // don't show manipulation panel for case of all Object's parts selection 
        update_and_show_manipulations = !selection.is_single_full_instance();
    }
    else
    {
        if (item)
        {
            if (m_objects_model->GetParent(item) == wxDataViewItem(nullptr)) {
                obj_idx = m_objects_model->GetIdByItem(item);
                og_name = _(L("Object manipulation"));
                m_config = &(*m_objects)[obj_idx]->config;
                update_and_show_manipulations = true;
            }
            else {
                obj_idx = m_objects_model->GetObjectIdByItem(item);
                
                const ItemType type = m_objects_model->GetItemType(item);
                if (type & itSettings) {
                    const auto parent = m_objects_model->GetParent(item);
                    const ItemType parent_type = m_objects_model->GetItemType(parent);

                    if (parent_type & itObject) {
                        og_name = _(L("Object Settings to modify"));
                        m_config = &(*m_objects)[obj_idx]->config;
                    }
                    else if (parent_type & itVolume) {
                        og_name = _(L("Part Settings to modify"));
                        volume_id = m_objects_model->GetVolumeIdByItem(parent);
                        m_config = &(*m_objects)[obj_idx]->volumes[volume_id]->config;
                    }
                    else if (parent_type & itLayer) {
                        og_name = _(L("Layer range Settings to modify"));
                        m_config = &get_item_config(parent);
                    }
                    update_and_show_settings = true;
                }
                else if (type & itVolume) {
                    og_name = _(L("Part manipulation"));
                    volume_id = m_objects_model->GetVolumeIdByItem(item);
                    m_config = &(*m_objects)[obj_idx]->volumes[volume_id]->config;
                    update_and_show_manipulations = true;
                }
                else if (type & itInstance) {
                    og_name = _(L("Instance manipulation"));
                    update_and_show_manipulations = true;

                    // fill m_config by object's values
                    m_config = &(*m_objects)[obj_idx]->config;
                }
                else if (type & (itLayerRoot|itLayer)) {
                    og_name = type & itLayerRoot ? _(L("Height ranges")) : _(L("Settings for height range"));
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

SettingsBundle ObjectList::get_item_settings_bundle(const DynamicPrintConfig* config, const bool is_object_settings)
{
    auto opt_keys = config->keys();
    if (opt_keys.empty())
        return SettingsBundle();

    update_opt_keys(opt_keys, is_object_settings); // update options list according to print technology

    if (opt_keys.empty())
        return SettingsBundle();

    const int extruders_cnt = wxGetApp().extruders_edited_cnt();

    SettingsBundle bundle;
    for (auto& opt_key : opt_keys)
    {
        auto category = config->def()->get(opt_key)->category;
        if (improper_category(category, extruders_cnt, is_object_settings))
            continue;

        std::vector< std::string > new_category;

        auto& cat_opt = bundle.find(category) == bundle.end() ? new_category : bundle.at(category);
        cat_opt.push_back(opt_key);
        if (cat_opt.size() == 1)
            bundle[category] = cat_opt;
    }

    return bundle;
}

// Add new SettingsItem for parent_item if it doesn't exist, or just update a digest according to new config
wxDataViewItem ObjectList::add_settings_item(wxDataViewItem parent_item, const DynamicPrintConfig* config)
{
    wxDataViewItem ret = wxDataViewItem(nullptr);

    if (!parent_item)
        return ret;

    const bool is_object_settings = m_objects_model->GetItemType(parent_item) == itObject;
    SettingsBundle cat_options = get_item_settings_bundle(config, is_object_settings);
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

void ObjectList::add_object_to_list(size_t obj_idx, bool call_selection_changed)
{
    auto model_object = (*m_objects)[obj_idx];
    const wxString& item_name = from_u8(model_object->name);
    const auto item = m_objects_model->Add(item_name,
                      !model_object->config.has("extruder") ? 0 :
                      model_object->config.option<ConfigOptionInt>("extruder")->value,
                      get_mesh_errors_count(obj_idx) > 0);

    // add volumes to the object
    if (model_object->volumes.size() > 1) {
        for (const ModelVolume* volume : model_object->volumes) {
            const wxDataViewItem& vol_item = m_objects_model->AddVolumeChild(item,
                from_u8(volume->name),
                volume->type(),
                volume->get_mesh_errors_count()>0,
                !volume->config.has("extruder") ? 0 :
                volume->config.option<ConfigOptionInt>("extruder")->value,
                false);
            add_settings_item(vol_item, &volume->config);
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
    add_settings_item(item, &model_object->config);

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
        select_item(m_objects_model->Delete(item));
    else
        select_item(m_objects_model->Delete(m_objects_model->GetParent(item)));
}

void ObjectList::delete_object_from_list(const size_t obj_idx)
{
    select_item(m_objects_model->Delete(m_objects_model->GetItemById(obj_idx)));
}

void ObjectList::delete_volume_from_list(const size_t obj_idx, const size_t vol_idx)
{
    select_item(m_objects_model->Delete(m_objects_model->GetItemByVolumeId(obj_idx, vol_idx)));
}

void ObjectList::delete_instance_from_list(const size_t obj_idx, const size_t inst_idx)
{
    select_item(m_objects_model->Delete(m_objects_model->GetItemByInstanceId(obj_idx, inst_idx)));
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
                    const wxString extruder = wxString::Format("%d", (*m_objects)[item->obj_idx]->config.option<ConfigOptionInt>("extruder")->value);
                    m_objects_model->SetExtruder(extruder, m_objects_model->GetItemById(item->obj_idx));
                }
                wxGetApp().plater()->canvas3D()->ensure_on_bed(item->obj_idx);
            }
            else
                m_objects_model->Delete(m_objects_model->GetItemByInstanceId(item->obj_idx, item->sub_obj_idx));
        }
    }
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
    select_item(m_objects_model->AddInstanceChild(m_objects_model->GetItemById(obj_idx), num));
}

void ObjectList::decrease_object_instances(const size_t obj_idx, const size_t num)
{
    select_item(m_objects_model->DeleteLastInstance(m_objects_model->GetItemById(obj_idx), num));
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

void ObjectList::select_current_object(int idx)
{
    m_prevent_list_events = true;
    UnselectAll();
    if (idx >= 0)
        Select(m_objects_model->GetItemById(idx));
    part_selection_changed();
    m_prevent_list_events = false;
}

void ObjectList::select_current_volume(int idx, int vol_idx)
{
    if (vol_idx < 0) {
        select_current_object(idx);
        return;
    }
    m_prevent_list_events = true;
    UnselectAll();
    if (idx >= 0)
        Select(m_objects_model->GetItemByVolumeId(idx, vol_idx));
    part_selection_changed();
    m_prevent_list_events = false;
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
    return config.opt_float("min_layer_height", extruder_idx <= 0 ? 0 : extruder_idx-1);
}

static double get_max_layer_height(const int extruder_idx)
{
    const DynamicPrintConfig& config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    int extruder_idx_zero_based = extruder_idx <= 0 ? 0 : extruder_idx-1;
    double max_layer_height = config.opt_float("max_layer_height", extruder_idx_zero_based);

    // In case max_layer_height is set to zero, it should default to 75 % of nozzle diameter:
    if (max_layer_height < EPSILON)
        max_layer_height = 0.75 * config.opt_float("nozzle_diameter", extruder_idx_zero_based);

    return max_layer_height;
}

void ObjectList::add_layer_range_after_current(const t_layer_height_range current_range)
{
    const int obj_idx = get_selected_obj_idx();
    if (obj_idx < 0) 
        // This should not happen.
        return;

    const wxDataViewItem layers_item = GetSelection();

    t_layer_config_ranges& ranges = object(obj_idx)->layer_config_ranges;
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
        ranges[new_range] = get_default_layer_config(obj_idx);
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
                // Splitting the currnet layer heigth range to two.
                const auto old_config = ranges.at(next_range);
                const coordf_t delta = (next_range.second - next_range.first);
                if (delta >= get_min_layer_height(old_config.opt_int("extruder"))/*0.05f*/) {
                    const coordf_t midl_layer = next_range.first + 0.5 * delta;
                    t_layer_height_range new_range = { midl_layer, next_range.second };

                    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("Add Height Range")));
                    changed = true;

                    // create new 2 layers instead of deleted one
                    // delete old layer

                    wxDataViewItem layer_item = m_objects_model->GetItemByLayerRange(obj_idx, next_range);
                    del_subobject_item(layer_item);

                    ranges[new_range] = old_config;
                    add_layer_item(new_range, layers_item, layer_idx);

                    new_range = { current_range.second, midl_layer };
                    ranges[new_range] = get_default_layer_config(obj_idx);
                    add_layer_item(new_range, layers_item, layer_idx);
                }
            }
            else
            {
                // Filling in a gap between the current and a new layer height range with a new one.
                take_snapshot(_(L("Add Height Range")));
                changed = true;

                const t_layer_height_range new_range = { current_range.second, next_range.first };
                ranges[new_range] = get_default_layer_config(obj_idx);
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

void ObjectList::add_layer_item(const t_layer_height_range& range, 
                                const wxDataViewItem layers_item, 
                                const int layer_idx /* = -1*/)
{
    const int obj_idx = m_objects_model->GetObjectIdByItem(layers_item);
    if (obj_idx < 0) return;

    const DynamicPrintConfig& config = object(obj_idx)->layer_config_ranges[range];
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
    const int obj_idx = get_selected_obj_idx();
    if (obj_idx < 0) 
        return false;

    DynamicPrintConfig* config = &object(obj_idx)->layer_config_ranges[range];
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
    const int obj_idx = get_selected_obj_idx();
    if (obj_idx < 0) return false;

    take_snapshot(_(L("Edit Height Range")));

    const ItemType sel_type = m_objects_model->GetItemType(GetSelection());

    t_layer_config_ranges& ranges = object(obj_idx)->layer_config_ranges;

    const DynamicPrintConfig config = ranges[range];

    ranges.erase(range);
    ranges[new_range] = config;
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

    if (dont_update_ui)
        return true;

    select_item(sel_type&itLayer ? m_objects_model->GetItemByLayerRange(obj_idx, new_range) : root_item);
    Expand(root_item);

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

    // We doesn't update selection if SettingsItem for the current object/part is selected
//     if (GetSelectedItemsCount() == 1 && m_objects_model->GetItemType(GetSelection()) == itSettings )
    if (GetSelectedItemsCount() == 1 && m_objects_model->GetItemType(GetSelection()) & (itSettings | itLayerRoot | itLayer))
    {
        const auto item = GetSelection();
        if (selection.is_single_full_object()) {
            if (m_objects_model->GetObjectIdByItem(item) == selection.get_object_idx())
                return;
            sels.Add(m_objects_model->GetItemById(selection.get_object_idx()));
        }
        if (selection.is_single_volume() || selection.is_any_modifier()) {
            const auto gl_vol = selection.get_volume(*selection.get_volume_idxs().begin());
            if (m_objects_model->GetVolumeIdByItem(m_objects_model->GetParent(item)) == gl_vol->volume_idx())
                return;
        }

        // but if there is selected only one of several instances by context menu,
        // then select this instance in ObjectList
        if (selection.is_single_full_instance())
            sels.Add(m_objects_model->GetItemByInstanceId(selection.get_object_idx(), selection.get_instance_idx()));
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
        if (m_objects_model->GetItemType(item) & (itSettings | itInstanceRoot | itLayerRoot | itLayer))
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

void ObjectList::select_items(const wxDataViewItemArray& sels)
{
    m_prevent_list_events = true;

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
    auto item = GetSelection();
    if (!item || m_objects_model->GetItemType(item) != itVolume)
        return nullptr;

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

    const ModelVolumeType type = volume->type();
    if (type == ModelVolumeType::MODEL_PART)
    {
        const int obj_idx = get_selected_obj_idx();
        if (obj_idx < 0) return;

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

    const wxString names[] = { _(L("Part")), _(L("Modifier")), _(L("Support Enforcer")), _(L("Support Blocker")) };
    
    auto new_type = ModelVolumeType(wxGetSingleChoiceIndex(_(L("Type:")), _(L("Select type of part")), wxArrayString(4, names), int(type)));

	if (new_type == type || new_type == ModelVolumeType::INVALID)
        return;

    take_snapshot(_(L("Change Part Type")));

    const auto item = GetSelection();
    volume->set_type(new_type);
    m_objects_model->SetVolumeType(item, new_type);

    changed_object(get_selected_obj_idx());

    // Update settings showing, if we have it
    //(we show additional settings for Part and Modifier and hide it for Support Blocker/Enforcer)
    const auto settings_item = m_objects_model->GetSettingsItem(item);
    if (settings_item && 
        (new_type == ModelVolumeType::SUPPORT_ENFORCER || new_type == ModelVolumeType::SUPPORT_BLOCKER)) {
        m_objects_model->Delete(settings_item);
    }
    else if (!settings_item && 
              (new_type == ModelVolumeType::MODEL_PART || new_type == ModelVolumeType::PARAMETER_MODIFIER)) {
        add_settings_item(item, &volume->config);
    }
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
    select_item(add_settings_item(obj_item, &get_item_config(obj_item)));
}

// Update settings item for item had it
void ObjectList::update_settings_item_and_selection(wxDataViewItem item, wxDataViewItemArray& selections)
{
    const wxDataViewItem old_settings_item = m_objects_model->GetSettingsItem(item);
    const wxDataViewItem new_settings_item = add_settings_item(item, &get_item_config(item));

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

void ObjectList::update_object_menu()
{
    append_menu_items_add_volume(&m_menu_object);
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
    const int em = wxGetApp().em_unit();
    // update min size !!! A width of control shouldn't be a wxDefaultCoord
    SetMinSize(wxSize(1, 15 * em));

    GetColumn(colName    )->SetWidth(20 * em);
    GetColumn(colPrint   )->SetWidth( 3 * em);
    GetColumn(colExtruder)->SetWidth( 8 * em);
    GetColumn(colEditing )->SetWidth( 3 * em);

    // rescale all icons, used by ObjectList
    msw_rescale_icons();

    // rescale/update existing items with bitmaps
    m_objects_model->Rescale();

    // rescale menus
    for (MenuWithSeparators* menu : { &m_menu_object, 
                                      &m_menu_part, 
                                      &m_menu_sla_object, 
                                      &m_menu_instance, 
                                      &m_menu_layer,
                                      &m_menu_default})
        msw_rescale_menu(menu);

    Layout();
}

void ObjectList::ItemValueChanged(wxDataViewEvent &event)
{
    if (event.GetColumn() == colName)
        update_name_in_model(event.GetItem());
    else if (event.GetColumn() == colExtruder)
        update_extruder_in_config(event.GetItem());
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

void ObjectList::show_multi_selection_menu()
{
    wxDataViewItemArray sels;
    GetSelections(sels);

    for (const wxDataViewItem& item : sels)
        if (!(m_objects_model->GetItemType(item) & (itVolume | itObject | itInstance)))
            // show this menu only for Objects(Instances mixed with Objects)/Volumes selection
            return;

    wxMenu* menu = new wxMenu();

    if (extruders_count() > 1)
        append_menu_item_change_extruder(menu);

    append_menu_item(menu, wxID_ANY, _(L("Reload from disk")), _(L("Reload the selected volumes from disk")),
        [this](wxCommandEvent&) { wxGetApp().plater()->reload_from_disk(); }, "", menu, []() {
        return wxGetApp().plater()->can_reload_from_disk();
    }, wxGetApp().plater());

    wxGetApp().plater()->PopupMenu(menu);
}

void ObjectList::extruder_selection()
{
    wxArrayString choices;
    choices.Add(_(L("default")));
    for (int i = 1; i <= extruders_count(); ++i)
        choices.Add(wxString::Format("%d", i));

    const wxString& selected_extruder = wxGetSingleChoice(_(L("Select extruder number:")),
                                                          _(L("This extruder will be set for selected items")),
                                                          choices, 0, this);
    if (selected_extruder.IsEmpty())
        return;

    const int extruder_num = selected_extruder == _(L("default")) ? 0 : atoi(selected_extruder.c_str());

//          /* Another variant for an extruder selection */
//     extruder_num = wxGetNumberFromUser(_(L("Attention!!! \n"
//                                              "It's a possibile to set an extruder number \n"
//                                              "for whole Object(s) and/or object Part(s), \n"
//                                              "not for an Instance. ")), 
//                                          _(L("Enter extruder number:")),
//                                          _(L("This extruder will be set for selected items")), 
//                                          1, 1, 5, this);

    set_extruder_for_selected_items(extruder_num);
}

void ObjectList::set_extruder_for_selected_items(const int extruder) const 
{
    wxDataViewItemArray sels;
    GetSelections(sels);

    if (!sels.empty())
        take_snapshot(_(L("Change Extruders")));

    for (const wxDataViewItem& item : sels)
    {
        DynamicPrintConfig& config = get_item_config(item);
        
        if (config.has("extruder")) {
            if (extruder == 0)
                config.erase("extruder");
            else
                config.option<ConfigOptionInt>("extruder")->value = extruder;
        }
        else if (extruder > 0)
            config.set_key_value("extruder", new ConfigOptionInt(extruder));

        const wxString extruder_str = extruder == 0 ? wxString (_(L("default"))) : 
                                      wxString::Format("%d", config.option<ConfigOptionInt>("extruder")->value);

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

void ObjectList::toggle_printable_state(wxDataViewItem item)
{
    const ItemType type = m_objects_model->GetItemType(item);
    if (!(type&(itObject|itInstance/*|itVolume*/)))
        return;

    if (type & itObject)
    {
        const int obj_idx = m_objects_model->GetObjectIdByItem(item);
        ModelObject* object = (*m_objects)[obj_idx];

        // get object's printable and change it
        const bool printable = !m_objects_model->IsPrintable(item);

        const wxString snapshot_text = from_u8((boost::format("%1% %2%")
                                                  % (printable ? _(L("Set Printable")) : _(L("Set Unprintable")))
                                                  % object->name).str());
        take_snapshot(snapshot_text);

        // set printable value for all instances in object
        for (auto inst : object->instances)
            inst->printable = printable;

        // update printable state on canvas
        wxGetApp().plater()->canvas3D()->update_instance_printable_state_for_object((size_t)obj_idx);

        // update printable state in ObjectList
        m_objects_model->SetObjectPrintableState(printable ? piPrintable : piUnprintable , item);
    }
    else
        wxGetApp().plater()->canvas3D()->get_selection().toggle_instance_printable_state(); 

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
