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

namespace Slic3r
{
namespace GUI
{

wxDEFINE_EVENT(EVT_OBJ_LIST_OBJECT_SELECT, SimpleEvent);

// pt_FFF
FreqSettingsBundle FREQ_SETTINGS_BUNDLE_FFF =
{
    { L("Layers and Perimeters"), { "layer_height" , "perimeters", "top_solid_layers", "bottom_solid_layers" } },
    { L("Infill")               , { "fill_density", "fill_pattern" } },
    { L("Support material")     , { "support_material", "support_material_auto", "support_material_threshold", 
                                    "support_material_pattern", "support_material_buildplate_only",
                                    "support_material_spacing" } },
    { L("Extruders")            , { "wipe_into_infill", "wipe_into_objects" } }
};

// pt_SLA
FreqSettingsBundle FREQ_SETTINGS_BUNDLE_SLA =
{
    { L("Pad and Support")      , { "supports_enable", "pad_enable" } }
};

// Note: id accords to type of the sub-object (adding volume), so sequence of the menu items is important
std::vector<std::pair<std::string, std::string>> ADD_VOLUME_MENU_ITEMS = { 
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
    wxGetApp().plater()->take_snapshot(snapshot_name);
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
        CATEGORY_ICON[L("Layers and Perimeters")]    = create_scaled_bitmap(nullptr, "layers");
        CATEGORY_ICON[L("Infill")]                   = create_scaled_bitmap(nullptr, "infill");
        CATEGORY_ICON[L("Support material")]         = create_scaled_bitmap(nullptr, "support");
        CATEGORY_ICON[L("Speed")]                    = create_scaled_bitmap(nullptr, "time");
        CATEGORY_ICON[L("Extruders")]                = create_scaled_bitmap(nullptr, "funnel");
        CATEGORY_ICON[L("Extrusion Width")]          = create_scaled_bitmap(nullptr, "funnel");
//         CATEGORY_ICON[L("Skirt and brim")]          = create_scaled_bitmap(nullptr, "skirt+brim"); 
//         CATEGORY_ICON[L("Speed > Acceleration")]    = create_scaled_bitmap(nullptr, "time");
        CATEGORY_ICON[L("Advanced")]                 = create_scaled_bitmap(nullptr, "wrench");
        // ptSLA
        CATEGORY_ICON[L("Supports")]                 = create_scaled_bitmap(nullptr, "support"/*"sla_supports"*/);
        CATEGORY_ICON[L("Pad")]                      = create_scaled_bitmap(nullptr, "pad");
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
        if (wxGetKeyState(WXK_SHIFT))
        {
            wxDataViewItemArray sels;
            GetSelections(sels);
            if (sels.front() == m_last_selected_item)
                m_last_selected_item = sels.back();
            else
                m_last_selected_item = event.GetItem();
        }
        else
            m_last_selected_item = event.GetItem();

        selection_changed();
#ifndef __WXMSW__
        set_tooltip_for_item(get_mouse_position_in_control());
#endif //__WXMSW__        
    });

#ifdef __WXOSX__
    // Key events are not correctly processed by the wxDataViewCtrl on OSX.
    // Our patched wxWidgets process the keyboard accelerators.
    // On the other hand, using accelerators will break in-place editing on Windows & Linux/GTK (there is no in-place editing working on OSX for wxDataViewCtrl for now).
//    Bind(wxEVT_KEY_DOWN, &ObjectList::OnChar, this);
    {
        // Accelerators
        wxAcceleratorEntry entries[6];
        entries[0].Set(wxACCEL_CTRL, (int) 'C',    wxID_COPY);
        entries[1].Set(wxACCEL_CTRL, (int) 'X',    wxID_CUT);
        entries[2].Set(wxACCEL_CTRL, (int) 'V',    wxID_PASTE);
        entries[3].Set(wxACCEL_CTRL, (int) 'A',    wxID_SELECTALL);
        entries[4].Set(wxACCEL_NORMAL, WXK_DELETE, wxID_DELETE);
        entries[5].Set(wxACCEL_NORMAL, WXK_BACK,   wxID_DELETE);
        wxAcceleratorTable accel(6, entries);
        SetAcceleratorTable(accel);

        this->Bind(wxEVT_MENU, [this](wxCommandEvent &evt) { this->copy();                      }, wxID_COPY);
        this->Bind(wxEVT_MENU, [this](wxCommandEvent &evt) { this->paste();                     }, wxID_PASTE);
        this->Bind(wxEVT_MENU, [this](wxCommandEvent &evt) { this->select_item_all_children();  }, wxID_SELECTALL);
        this->Bind(wxEVT_MENU, [this](wxCommandEvent &evt) { this->remove();                    }, wxID_DELETE);
    }
#else __WXOSX__
    Bind(wxEVT_CHAR, [this](wxKeyEvent& event) { key_event(event); }); // doesn't work on OSX
#endif

#ifdef __WXMSW__
    GetMainWindow()->Bind(wxEVT_MOTION, [this](wxMouseEvent& event) {
        set_tooltip_for_item(/*event.GetPosition()*/get_mouse_position_in_control());
        event.Skip();
    });
#endif //__WXMSW__

    Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU,  &ObjectList::OnContextMenu,     this);

    Bind(wxEVT_DATAVIEW_ITEM_BEGIN_DRAG,    &ObjectList::OnBeginDrag,       this);
    Bind(wxEVT_DATAVIEW_ITEM_DROP_POSSIBLE, &ObjectList::OnDropPossible,    this);
    Bind(wxEVT_DATAVIEW_ITEM_DROP,          &ObjectList::OnDrop,            this);

    Bind(wxEVT_DATAVIEW_ITEM_EDITING_DONE,  &ObjectList::OnEditingDone,     this);

    Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, &ObjectList::ItemValueChanged,  this);

    Bind(wxCUSTOMEVT_LAST_VOLUME_IS_DELETED, [this](wxCommandEvent& e)   { last_volume_is_deleted(e.GetInt()); });

    Bind(wxEVT_SIZE, ([this](wxSizeEvent &e) { this->EnsureVisible(this->GetCurrentItem()); e.Skip(); }));
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

    // column 0(Icon+Text) of the view control: 
    // And Icon can be consisting of several bitmaps
    AppendColumn(new wxDataViewColumn(_(L("Name")), new BitmapTextRenderer(),
        0, 20*wxGetApp().em_unit()/*200*/, wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE));

    // column 1 of the view control:
    AppendColumn(create_objects_list_extruder_column(4));

    // column 2 of the view control:
    AppendBitmapColumn(" ", 2, wxDATAVIEW_CELL_INERT, int(2.5 * wxGetApp().em_unit())/*25*/,
        wxALIGN_CENTER_HORIZONTAL, wxDATAVIEW_COL_RESIZABLE);
}

void ObjectList::create_popup_menus()
{
    // create popup menus for object and part
    create_object_popupmenu(&m_menu_object);
    create_part_popupmenu(&m_menu_part);
    create_sla_object_popupmenu(&m_menu_sla_object);
    create_instance_popupmenu(&m_menu_instance);
}

void ObjectList::get_selected_item_indexes(int& obj_idx, int& vol_idx, const wxDataViewItem& input_item/* = wxDataViewItem(0)*/)
{
    const wxDataViewItem item = input_item == wxDataViewItem(0) ? GetSelection() : input_item;

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
    wxString tooltip = wxString::Format(_(L("Auto-repaired (%d errors):\n")), errors);

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
            tooltip += wxString::Format("\t%d %s\n", error.second, _(error.first));

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

    if (!item)
    {
        GetMainWindow()->SetToolTip(""); // hide tooltip
        return;
    }

    if (col->GetTitle() == " " && GetSelectedItemsCount()<2)
        GetMainWindow()->SetToolTip(_(L("Right button click the icon to change the object settings")));
    else if (col->GetTitle() == _("Name"))
    {
#ifdef __WXMSW__
        if (pt.x < 2 * wxGetApp().em_unit() || pt.x > 4 * wxGetApp().em_unit()) {
            GetMainWindow()->SetToolTip(""); // hide tooltip
            return;
        }
#endif //__WXMSW__
        int obj_idx, vol_idx;
        get_selected_item_indexes(obj_idx, vol_idx, item);
        GetMainWindow()->SetToolTip(get_mesh_errors_list(obj_idx, vol_idx));
    }
    else
        GetMainWindow()->SetToolTip(""); // hide tooltip
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

    const int obj_idx = type & itObject ? m_objects_model->GetIdByItem(item) :
                        m_objects_model->GetIdByItem(m_objects_model->GetTopParent(item));

    const int vol_idx = type & itVolume ? m_objects_model->GetVolumeIdByItem(item) : -1;

    assert(obj_idx >= 0 || ((type & itVolume) && vol_idx >=0));
    return type & itVolume ?(*m_objects)[obj_idx]->volumes[vol_idx]->config :
           type & itLayer  ?(*m_objects)[obj_idx]->layer_config_ranges[m_objects_model->GetLayerRangeByItem(item)] :
                            (*m_objects)[obj_idx]->config;
}

wxDataViewColumn* ObjectList::create_objects_list_extruder_column(int extruders_count)
{
    wxArrayString choices;
    choices.Add(_(L("default")));
    for (int i = 1; i <= extruders_count; ++i)
        choices.Add(wxString::Format("%d", i));
    wxDataViewChoiceRenderer *c =
        new wxDataViewChoiceRenderer(choices, wxDATAVIEW_CELL_EDITABLE, wxALIGN_CENTER_HORIZONTAL);
    wxDataViewColumn* column = new wxDataViewColumn(_(L("Extruder")), c, 1, 
                               8*wxGetApp().em_unit()/*80*/, wxALIGN_CENTER_HORIZONTAL, wxDATAVIEW_COL_RESIZABLE);
    return column;
}

void ObjectList::update_extruder_values_for_items(const int max_extruder)
{
    for (int i = 0; i < m_objects->size(); ++i)
    {
        wxDataViewItem item = m_objects_model->GetItemById(i);
        if (!item) continue;
            
        auto object = (*m_objects)[i];
        wxString extruder;
        if (!object->config.has("extruder") ||
            object->config.option<ConfigOptionInt>("extruder")->value > max_extruder)
            extruder = _(L("default"));
        else
            extruder = wxString::Format("%d", object->config.option<ConfigOptionInt>("extruder")->value);

        m_objects_model->SetValue(extruder, item, 1);

        if (object->volumes.size() > 1) {
            for (auto id = 0; id < object->volumes.size(); id++) {
                item = m_objects_model->GetItemByVolumeId(i, id);
                if (!item) continue;
                if (!object->volumes[id]->config.has("extruder") ||
                    object->volumes[id]->config.option<ConfigOptionInt>("extruder")->value > max_extruder)
                    extruder = _(L("default"));
                else
                    extruder = wxString::Format("%d", object->volumes[id]->config.option<ConfigOptionInt>("extruder")->value); 

                m_objects_model->SetValue(extruder, item, 1);
            }
        }
    }
}

void ObjectList::update_objects_list_extruder_column(int extruders_count)
{
    if (!this) return; // #ys_FIXME
    if (printer_technology() == ptSLA)
        extruders_count = 1;

    wxDataViewChoiceRenderer* ch_render = dynamic_cast<wxDataViewChoiceRenderer*>(GetColumn(1)->GetRenderer());
    if (ch_render->GetChoices().GetCount() - 1 == extruders_count)
        return;
    
    m_prevent_update_extruder_in_config = true;

    if (m_objects && extruders_count > 1)
        update_extruder_values_for_items(extruders_count);

    // delete old 2nd column
    DeleteColumn(GetColumn(1));
    // insert new created 3rd column
    InsertColumn(1, create_objects_list_extruder_column(extruders_count));
    // set show/hide for this column 
    set_extruder_column_hidden(extruders_count <= 1);
    //a workaround for a wrong last column width updating under OSX 
    GetColumn(2)->SetWidth(25);

    m_prevent_update_extruder_in_config = false;
}

void ObjectList::set_extruder_column_hidden(const bool hide) const
{
    GetColumn(1)->SetHidden(hide);
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

    wxVariant variant;
    m_objects_model->GetValue(variant, item, 1);
    const wxString selection = variant.GetString();

    if (!m_config || selection.empty())
        return;

    const int extruder = /*selection.size() > 1 ? 0 : */atoi(selection.c_str());
    m_config->set_key_value("extruder", new ConfigOptionInt(extruder));

    // update scene
    wxGetApp().plater()->update();
}

void ObjectList::update_name_in_model(const wxDataViewItem& item) const 
{
    const int obj_idx = m_objects_model->GetObjectIdByItem(item);
    if (obj_idx < 0) return;

    if (m_objects_model->GetParent(item) == wxDataViewItem(0)) {
        (*m_objects)[obj_idx]->name = m_objects_model->GetName(item).ToUTF8().data();
        return;
    }

    const int volume_id = m_objects_model->GetVolumeIdByItem(item);
    if (volume_id < 0) return;
    (*m_objects)[obj_idx]->volumes[volume_id]->name = m_objects_model->GetName(item).ToUTF8().data();
}

void ObjectList::init_icons()
{
    m_bmp_solidmesh         = ScalableBitmap(nullptr, ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::MODEL_PART)        ].second);
    m_bmp_modifiermesh      = ScalableBitmap(nullptr, ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::PARAMETER_MODIFIER)].second);
    m_bmp_support_enforcer  = ScalableBitmap(nullptr, ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::SUPPORT_ENFORCER)  ].second);
    m_bmp_support_blocker   = ScalableBitmap(nullptr, ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::SUPPORT_BLOCKER)   ].second); 

    m_bmp_vector.reserve(4); // bitmaps for different types of parts 
    m_bmp_vector.push_back(&m_bmp_solidmesh.bmp());         
    m_bmp_vector.push_back(&m_bmp_modifiermesh.bmp());      
    m_bmp_vector.push_back(&m_bmp_support_enforcer.bmp());  
    m_bmp_vector.push_back(&m_bmp_support_blocker.bmp());   


    // Set volumes default bitmaps for the model
    m_objects_model->SetVolumeBitmaps(m_bmp_vector);

    // init icon for manifold warning
    m_bmp_manifold_warning  = ScalableBitmap(nullptr, "exclamation");
    // Set warning bitmap for the model
    m_objects_model->SetWarningBitmap(&m_bmp_manifold_warning.bmp());

    // init bitmap for "Add Settings" context menu
    m_bmp_cog               = ScalableBitmap(nullptr, "cog");
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
        // Note: `this` isn't passed to create_scaled_bitmap() here because of bugs in the widget,
        // see note in PresetBundle::load_compatible_bitmaps()

        // ptFFF
        CATEGORY_ICON[L("Layers and Perimeters")]    = create_scaled_bitmap(nullptr, "layers");
        CATEGORY_ICON[L("Infill")]                   = create_scaled_bitmap(nullptr, "infill");
        CATEGORY_ICON[L("Support material")]         = create_scaled_bitmap(nullptr, "support");
        CATEGORY_ICON[L("Speed")]                    = create_scaled_bitmap(nullptr, "time");
        CATEGORY_ICON[L("Extruders")]                = create_scaled_bitmap(nullptr, "funnel");
        CATEGORY_ICON[L("Extrusion Width")]          = create_scaled_bitmap(nullptr, "funnel");
//         CATEGORY_ICON[L("Skirt and brim")]          = create_scaled_bitmap(nullptr, "skirt+brim"); 
//         CATEGORY_ICON[L("Speed > Acceleration")]    = create_scaled_bitmap(nullptr, "time");
        CATEGORY_ICON[L("Advanced")]                 = create_scaled_bitmap(nullptr, "wrench");
        // ptSLA
        CATEGORY_ICON[L("Supports")]                 = create_scaled_bitmap(nullptr, "support"/*"sla_supports"*/);
        CATEGORY_ICON[L("Pad")]                      = create_scaled_bitmap(nullptr, "pad");
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
        auto opt_keys = volume->config.keys();
        if (!opt_keys.empty() && !((opt_keys.size() == 1) && (opt_keys[0] == "extruder")))
            select_item(m_objects_model->AddSettingsChild(vol_item));

        items.Add(vol_item);
    }

    changed_object(obj_idx);

    if (items.size() > 1)
    {
        m_selection_mode = smVolume;
        m_last_selected_item = wxDataViewItem(0);
    }

    select_items(items);
#ifndef __WXOSX__ //#ifdef __WXMSW__ // #ys_FIXME
    selection_changed();
#endif //no __WXOSX__ //__WXMSW__
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
#ifndef __WXOSX__ //#ifdef __WXMSW__ // #ys_FIXME
    selection_changed();
#endif //no __WXOSX__ //__WXMSW__
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
    wxDataViewItem item;
    wxDataViewColumn* col;
    const wxPoint pt = get_mouse_position_in_control();
    HitTest(pt, item, col);
    if (!item)
#ifdef __WXOSX__ // temporary workaround for OSX 
        // after Yosemite OS X version, HitTest return undefined item
        item = GetSelection();
    if (item)
        show_context_menu();
    else
        printf("undefined item\n");
    return;
#else
        return;
#endif // __WXOSX__
    const wxString title = col->GetTitle();

    if (title == " ")
        show_context_menu();
    else if (title == _("Name"))
    {
        int obj_idx, vol_idx;
        get_selected_item_indexes(obj_idx, vol_idx, item);

        if (is_windows10() && get_mesh_errors_count(obj_idx, vol_idx) > 0 && 
            pt.x > 2*wxGetApp().em_unit() && pt.x < 4*wxGetApp().em_unit() )
            fix_through_netfabb();
    }

#ifndef __WXMSW__
    GetMainWindow()->SetToolTip(""); // hide tooltip
#endif //__WXMSW__
}

void ObjectList::show_context_menu()
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
    if (item)
    {
        const ItemType type = m_objects_model->GetItemType(item);
        if (!(type & (itObject | itVolume | itLayer | itInstance)))
            return;

        wxMenu* menu = type & itInstance ? &m_menu_instance :
                       type & itLayer ? &m_menu_layer :
                       m_objects_model->GetParent(item) != wxDataViewItem(0) ? &m_menu_part :
                       printer_technology() == ptFFF ? &m_menu_object : &m_menu_sla_object;

        if (!(type & itInstance))
            append_menu_item_settings(menu);

        wxGetApp().plater()->PopupMenu(menu);
    }
}

void ObjectList::copy()
{
    if (m_selection_mode & smLayer)
        fill_layer_config_ranges_cache();
    else
        wxPostEvent((wxEvtHandler*)wxGetApp().plater()->canvas3D()->get_wxglcanvas(), SimpleEvent(EVT_GLTOOLBAR_COPY));
}

void ObjectList::paste()
{
    if (!m_layer_config_ranges_cache.empty())
        paste_layers_into_list();
    else
        wxPostEvent((wxEvtHandler*)wxGetApp().plater()->canvas3D()->get_wxglcanvas(), SimpleEvent(EVT_GLTOOLBAR_PASTE));
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
    else if (wxGetKeyState(wxKeyCode('A')) && wxGetKeyState(WXK_CONTROL/*WXK_SHIFT*/))
        select_item_all_children();
    else if (wxGetKeyState(wxKeyCode('C')) && wxGetKeyState(WXK_CONTROL)) 
        copy();
    else if (wxGetKeyState(wxKeyCode('V')) && wxGetKeyState(WXK_CONTROL))
        paste();
    else
        event.Skip();
}
#endif /* __WXOSX__ */

void ObjectList::OnBeginDrag(wxDataViewEvent &event)
{
    const wxDataViewItem item(event.GetItem());

    const bool mult_sel = multiple_selection();

    if (mult_sel && !selected_instances_of_same_object() ||
        !mult_sel && (GetSelection() != item ||
        m_objects_model->GetParent(item) == wxDataViewItem(0) ) ) {
        event.Veto();
        return;
    }
   
    const ItemType& type = m_objects_model->GetItemType(item);
    if (!(type & (itVolume | itInstance))) {
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
    return  m_dragged_data.type() == itInstance && !item.IsOk()     ||
            m_dragged_data.type() == itVolume && item.IsOk() &&
            m_objects_model->GetItemType(item) == itVolume &&
            m_dragged_data.obj_idx() == m_objects_model->GetObjectIdByItem(item);
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
        take_snapshot(_(L("Instances to Separated Objects")));
        instances_to_separated_object(m_dragged_data.obj_idx(), m_dragged_data.inst_idxs());
        m_dragged_data.clear();
        return;
    }

    const int from_volume_id = m_dragged_data.sub_obj_idx();
    int to_volume_id = m_objects_model->GetVolumeIdByItem(item);

// It looks like a fixed in current version of the wxWidgets
// #ifdef __WXGTK__
//     /* Under GTK, DnD moves an item between another two items.
//     * And event.GetItem() return item, which is under "insertion line"
//     * So, if we move item down we should to decrease the to_volume_id value
//     **/
//     if (to_volume_id > from_volume_id) to_volume_id--;
// #endif // __WXGTK__

    take_snapshot(_(L("Remov Volume(s)")));

    auto& volumes = (*m_objects)[m_dragged_data.obj_idx()]->volumes;
    auto delta = to_volume_id < from_volume_id ? -1 : 1;
    int cnt = 0;
    for (int id = from_volume_id; cnt < abs(from_volume_id - to_volume_id); id += delta, cnt++)
        std::swap(volumes[id], volumes[id + delta]);

    select_item(m_objects_model->ReorganizeChildren(from_volume_id, to_volume_id,
                                                    m_objects_model->GetParent(item)));

    changed_object(m_dragged_data.obj_idx());

    m_dragged_data.clear();
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
    const FreqSettingsBundle& bundle = printer_technology() == ptSLA ? 
                                       FREQ_SETTINGS_BUNDLE_SLA : FREQ_SETTINGS_BUNDLE_FFF;

    for (auto& it : bundle)
    {
        if (bundle_name == _(it.first))
            return it.second;
    }
#if 0
    // if "Quick menu" is selected
    FreqSettingsBundle& bundle_quick = printer_technology() == ptSLA ?
                                       m_freq_settings_sla: m_freq_settings_fff;

    for (auto& it : bundle_quick)
    {
        if ( bundle_name == wxString::Format(_(L("Quick Add Settings (%s)")), _(it.first)) )
            return it.second;
    }
#endif

	static std::vector<std::string> empty;
	return empty;
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
        if (category.empty() ||
            (category == "Extruders" && extruders_cnt == 1)) continue;

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

    settings_menu_hierarchy settings_menu;
    const bool is_part = m_objects_model->GetParent(GetSelection()) != wxDataViewItem(0);
    get_options_menu(settings_menu, is_part);
    std::vector< std::pair<std::string, std::string> > *settings_list = nullptr;

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
        FreqSettingsBundle& freq_settings = printer_technology() == ptSLA ?
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


    // Add settings item for object
    update_settings_item();
}

void ObjectList::get_freq_settings_choice(const wxString& bundle_name)
{
    std::vector<std::string> options = get_options_for_bundle(bundle_name);

    /* Because of we couldn't edited layer_height for ItVolume from settings list,
     * correct options according to the selected item type :
     * remove "layer_height" option
     */
    if ((m_objects_model->GetItemType(GetSelection()) & itVolume) && bundle_name == _("Layers and Perimeters")) {
        const auto layer_height_it = std::find(options.begin(), options.end(), "layer_height");
        if (layer_height_it != options.end())
            options.erase(layer_height_it);
    }

    assert(m_config);
    auto opt_keys = m_config->keys();

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

    // Add settings item for object
    update_settings_item();
}

void ObjectList::update_settings_item()
{
    auto item = GetSelection();
    if (item) {
        if (m_objects_model->GetItemType(item) == itInstance)
            item = m_objects_model->GetTopParent(item);
        const auto settings_item = m_objects_model->IsSettingsItem(item) ? item : m_objects_model->GetSettingsItem(item);
        select_item(settings_item ? settings_item :
            m_objects_model->AddSettingsChild(item));

        // update object selection on Plater
        if (!m_prevent_canvas_selection_update)
            update_selections_on_canvas();
    }
    else {
        auto panel = wxGetApp().sidebar().scrolled_panel();
        panel->Freeze();
        wxGetApp().obj_settings()->UpdateAndShow(true);
        panel->Thaw();
    }
}

wxMenu* ObjectList::append_submenu_add_generic(wxMenu* menu, const ModelVolumeType type) {
    auto sub_menu = new wxMenu;

    if (wxGetApp().get_mode() == comExpert) {
    append_menu_item(sub_menu, wxID_ANY, _(L("Load")) + " " + dots, "",
        [this, type](wxCommandEvent&) { load_subobject(type); }, "", menu);
    sub_menu->AppendSeparator();
    }

    for (auto& item : { L("Box"), L("Cylinder"), L("Sphere"), L("Slab") }) {
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

    if (mode == comAdvanced) {
        append_menu_item(menu, wxID_ANY, _(ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::MODEL_PART)].first), "",
            [this](wxCommandEvent&) { load_subobject(ModelVolumeType::MODEL_PART); }, ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::MODEL_PART)].second);
    }
    if (mode == comSimple) {
        append_menu_item(menu, wxID_ANY, _(ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::SUPPORT_ENFORCER)].first), "",
            [this](wxCommandEvent&) { load_generic_subobject(L("Box"), ModelVolumeType::SUPPORT_ENFORCER); },
            ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::SUPPORT_ENFORCER)].second);
        append_menu_item(menu, wxID_ANY, _(ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::SUPPORT_BLOCKER)].first), "",
            [this](wxCommandEvent&) { load_generic_subobject(L("Box"), ModelVolumeType::SUPPORT_BLOCKER); },
            ADD_VOLUME_MENU_ITEMS[int(ModelVolumeType::SUPPORT_BLOCKER)].second);

        return;
    }
    
    for (int type = mode == comExpert ? 0 : 1 ; type < ADD_VOLUME_MENU_ITEMS.size(); type++)
    {
        auto& item = ADD_VOLUME_MENU_ITEMS[type];

        wxMenu* sub_menu = append_submenu_add_generic(menu, ModelVolumeType(type));
        append_submenu(menu, sub_menu, wxID_ANY, _(item.first), "", item.second);
    }
}

wxMenuItem* ObjectList::append_menu_item_split(wxMenu* menu) 
{
    return append_menu_item(menu, wxID_ANY, _(L("Split to parts")), "",
        [this](wxCommandEvent&) { split(); }, "split_parts_SMALL", menu, 
        [this]() { return is_splittable(); }, wxGetApp().plater());
}

wxMenuItem* ObjectList::append_menu_item_layers_editing(wxMenu* menu) 
{
    return append_menu_item(menu, wxID_ANY, _(L("Edit Layers")), "",
        [this](wxCommandEvent&) { layers_editing(); }, "layers", menu);
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
        settings_id = menu->FindItem(wxString::Format(_(L("Quick Add Settings (%s)")), _(it.first)));
        if (settings_id != wxNOT_FOUND)
            menu->Destroy(settings_id);
    }
    for (auto& it : m_freq_settings_sla)
    {
        settings_id = menu->FindItem(wxString::Format(_(L("Quick Add Settings (%s)")), _(it.first)));
        if (settings_id != wxNOT_FOUND)
            menu->Destroy(settings_id);
    }
#endif
    menu->DestroySeparators(); // delete old separators

    const auto sel_vol = get_selected_model_volume();
    if (sel_vol && sel_vol->type() >= ModelVolumeType::SUPPORT_ENFORCER)
        return nullptr;

    const ConfigOptionMode mode = wxGetApp().get_mode();
    if (mode == comSimple)
        return nullptr;

    // Create new items for settings popupmenu

    if (printer_technology() == ptFFF ||
        menu->GetMenuItems().size() > 0 && !menu->GetMenuItems().back()->IsSeparator())
        menu->SetFirstSeparator();

    // Add frequently settings
    create_freq_settings_popupmenu(menu);

    if (mode == comAdvanced)
        return nullptr;

    menu->SetSecondSeparator();

    // Add full settings list
    auto  menu_item = new wxMenuItem(menu, wxID_ANY, menu_name);
    menu_item->SetBitmap(m_bmp_cog.bmp());

    menu_item->SetSubMenu(create_settings_popupmenu(menu));

    return menu->Append(menu_item);
}

wxMenuItem* ObjectList::append_menu_item_change_type(wxMenu* menu)
{
    return append_menu_item(menu, wxID_ANY, _(L("Change type")), "",
        [this](wxCommandEvent&) { change_part_type(); }, "", menu);

}

wxMenuItem* ObjectList::append_menu_item_instance_to_object(wxMenu* menu, wxWindow* parent)
{
    return append_menu_item(menu, wxID_ANY, _(L("Set as a Separated Object")), "",
        [this](wxCommandEvent&) { split_instances(); }, "", menu, [](){return wxGetApp().plater()->can_set_instance_to_object(); }, parent);
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

void ObjectList::append_menu_item_change_extruder(wxMenu* menu) const
{
    const wxString name = _(L("Change extruder"));
    // Delete old menu item
    const int item_id = menu->FindItem(name);
    if (item_id != wxNOT_FOUND)
        menu->Destroy(item_id);

    const int extruders_cnt = extruders_count();
    const wxDataViewItem item = GetSelection();
    if (item && extruders_cnt > 1)
    {
        DynamicPrintConfig& config = get_item_config(item);

        const int initial_extruder = !config.has("extruder") ? 0 :
                                      config.option<ConfigOptionInt>("extruder")->value;

        wxMenu* extruder_selection_menu = new wxMenu();

        for (int i = 0; i <= extruders_cnt; i++)
        {
            const wxString& item_name = i == 0 ? _(L("Default")) : wxString::Format("%d", i);

            append_menu_radio_item(extruder_selection_menu, wxID_ANY, item_name, "",
                [this, i](wxCommandEvent&) { set_extruder_for_selected_items(i); }, menu)->Check(i == initial_extruder);
        }

        menu->AppendSubMenu(extruder_selection_menu, name, _(L("Select new extruder for the object/part")));
    }
}

void ObjectList::append_menu_item_delete(wxMenu* menu)
{
    append_menu_item(menu, wxID_ANY, _(L("Delete")), "",
        [this](wxCommandEvent&) { remove(); }, "", menu);
}

void ObjectList::append_menu_item_scale_selection_to_fit_print_volume(wxMenu* menu)
{
    append_menu_item(menu, wxID_ANY, _(L("Scale to print volume")), _(L("Scale the selected object to fit the print volume")),
        [this](wxCommandEvent&) { wxGetApp().plater()->scale_selection_to_fit_print_volume(); }, "", menu);
}

void ObjectList::create_object_popupmenu(wxMenu *menu)
{
#ifdef __WXOSX__  
    append_menu_items_osx(menu);
#endif // __WXOSX__

    append_menu_item_export_stl(menu);
    append_menu_item_fix_through_netfabb(menu);
    append_menu_item_scale_selection_to_fit_print_volume(menu);

    // Split object to parts
    append_menu_item_split(menu);
    menu->AppendSeparator();

    // Layers Editing for object
    append_menu_item_layers_editing(menu);
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

    append_menu_item_fix_through_netfabb(menu);
    append_menu_item_export_stl(menu);

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

    /* New behavior logic:
     * 1. Split Object to several separated object, if ALL instances are selected
     * 2. Separate selected instances from the initial object to the separated object,
     *    if some (not all) instances are selected
     */
    wxGetApp().plater()->Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) {
//         evt.Enable(can_split_instances()); }, m_menu_item_split_instances->GetId());
        evt.SetText(wxGetApp().plater()->canvas3D()->get_selection().is_single_full_object() ? 
                    _(L("Set as a Separated Objects")) : _(L("Set as a Separated Object")));
    }, m_menu_item_split_instances->GetId());
}

wxMenu* ObjectList::create_settings_popupmenu(wxMenu *parent_menu)
{
    wxMenu *menu = new wxMenu;

    settings_menu_hierarchy settings_menu;
    const bool is_part = m_objects_model->GetParent(GetSelection()) != wxDataViewItem(0);
    get_options_menu(settings_menu, is_part);

    for (auto cat : settings_menu) {
        append_menu_item(menu, wxID_ANY, _(cat.first), "",
                        [menu, this](wxCommandEvent& event) { get_settings_choice(menu->GetLabel(event.GetId())); }, 
                        CATEGORY_ICON.find(cat.first) == CATEGORY_ICON.end() ? wxNullBitmap : CATEGORY_ICON.at(cat.first), parent_menu); 
    }

    return menu;
}

void ObjectList::create_freq_settings_popupmenu(wxMenu *menu)
{
    // Add default settings bundles
    const FreqSettingsBundle& bundle = printer_technology() == ptFFF ?
                                     FREQ_SETTINGS_BUNDLE_FFF : FREQ_SETTINGS_BUNDLE_SLA;

    const int extruders_cnt = extruders_count();

    for (auto& it : bundle) {
        if (it.first.empty() || it.first == "Extruders" && extruders_cnt == 1) 
            continue;

        append_menu_item(menu, wxID_ANY, _(it.first), "",
                        [menu, this](wxCommandEvent& event) { get_freq_settings_choice(menu->GetLabel(event.GetId())); }, 
                        CATEGORY_ICON.find(it.first) == CATEGORY_ICON.end() ? wxNullBitmap : CATEGORY_ICON.at(it.first), menu); 
    }
#if 0
    // Add "Quick" settings bundles
    const FreqSettingsBundle& bundle_quick = printer_technology() == ptFFF ?
                                             m_freq_settings_fff : m_freq_settings_sla;

    for (auto& it : bundle_quick) {
        if (it.first.empty() || it.first == "Extruders" && extruders_cnt == 1) 
            continue;

        append_menu_item(menu, wxID_ANY, wxString::Format(_(L("Quick Add Settings (%s)")), _(it.first)), "",
                        [menu, this](wxCommandEvent& event) { get_freq_settings_choice(menu->GetLabel(event.GetId())); }, 
                        CATEGORY_ICON.find(it.first) == CATEGORY_ICON.end() ? wxNullBitmap : CATEGORY_ICON.at(it.first), menu); 
    }
#endif
}

void ObjectList::update_opt_keys(t_config_option_keys& opt_keys)
{
    auto full_current_opts = get_options(false);
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

    wxDataViewItem sel_item;
    for (const auto& volume : volumes_info )
        sel_item = m_objects_model->AddVolumeChild(item, volume.first, type, volume.second);
        
    if (sel_item)
        select_item(sel_item);
}

void ObjectList::load_part( ModelObject* model_object,
                            std::vector<std::pair<wxString, bool>> &volumes_info,
                            ModelVolumeType type)
{
    wxWindow* parent = wxGetApp().tab_panel()->GetPage(0);

    wxArrayString input_files;
    wxGetApp().import_model(parent, input_files);
    for (int i = 0; i < input_files.size(); ++i) {
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

void ObjectList::load_generic_subobject(const std::string& type_name, const ModelVolumeType type)
{
    const auto obj_idx = get_selected_obj_idx();
    if (obj_idx < 0) 
        return;

    const Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();
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

    const wxString name = _(L("Generic")) + "-" + _(type_name);
    TriangleMesh mesh;

    double side = wxGetApp().plater()->canvas3D()->get_size_proportional_to_max_bed_size(0.1);

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
        mesh = make_cube(instance_bb.size().x()*1.5, instance_bb.size().y()*1.5, instance_bb.size().z()*0.5);
    mesh.repair();
    
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

    new_volume->name = into_u8(name);
    // set a default extruder value, since user can't add it manually
    new_volume->config.set_key_value("extruder", new ConfigOptionInt(0));

    changed_object(obj_idx);

    const auto object_item = m_objects_model->GetTopParent(GetSelection());
    select_item(m_objects_model->AddVolumeChild(object_item, name, type, 
        new_volume->get_mesh_errors_count()>0));
#ifndef __WXOSX__ //#ifdef __WXMSW__ // #ys_FIXME
    selection_changed();
#endif //no __WXOSX__ //__WXMSW__
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
    if (type & itUndef)
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

    m_objects_model->Delete(item);
}

void ObjectList::del_settings_from_config(const wxDataViewItem& parent_item)
{
    const bool is_layer_settings = m_objects_model->GetItemType(parent_item) == itLayer;

    const int opt_cnt = m_config->keys().size();
    if (opt_cnt == 1 && m_config->has("extruder") || 
        is_layer_settings && opt_cnt == 2 && m_config->has("extruder") && m_config->has("layer_height"))
        return;

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
            Slic3r::GUI::show_error(nullptr, _(L("You can't delete the last solid part from object.")));
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
            Slic3r::GUI::show_error(nullptr, _(L("You can't delete the last intance from object.")));
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
    if (volume->split(nozzle_dmrs_cnt) == 1) {
        wxMessageBox(_(L("The selected object couldn't be split because it contains only one part.")));
        return;
    }

    take_snapshot(_(L("Split to Parts")));

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
        auto opt_keys = volume->config.keys();
        if ( !(opt_keys.size() == 1 && opt_keys[0] == "extruder") ) {
            select_item(m_objects_model->AddSettingsChild(vol_item));
            Expand(vol_item);
        }
    }

    if (parent == item)
        Expand(parent);

    changed_object(obj_idx);
}

void ObjectList::layers_editing()
{
    const auto item = GetSelection();
    const int obj_idx = get_selected_obj_idx();
    if (!item || obj_idx < 0)
        return;

    const wxDataViewItem obj_item = m_objects_model->GetTopParent(item);
    wxDataViewItem layers_item = m_objects_model->GetLayerRootItem(obj_item);

    // if it doesn't exist now
    if (!layers_item.IsOk())
    {
        t_layer_config_ranges& ranges = object(obj_idx)->layer_config_ranges;

        // set some default value
        if (ranges.empty())
            ranges[{ 0.0f, 2.0f }] = get_default_layer_config(obj_idx);

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
        return wxDataViewItem(0);

    // create LayerRoot item
    wxDataViewItem layers_item = m_objects_model->AddLayersRoot(obj_item);

    // and create Layer item(s) according to the layer_config_ranges
    for (const auto range : object(obj_idx)->layer_config_ranges)
        add_layer_item(range.first, layers_item);

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
    const Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();
    return selection.is_multiple_full_instance() || selection.is_single_full_instance();
}

// NO_PARAMETERS function call means that changed object index will be determine from Selection() 
void ObjectList::changed_object(const int obj_idx/* = -1*/) const 
{
    wxGetApp().plater()->changed_object(obj_idx < 0 ? get_selected_obj_idx() : obj_idx);
}

void ObjectList::part_selection_changed()
{
    int obj_idx = -1;
    int volume_id = -1;
    m_config = nullptr;
    wxString og_name = wxEmptyString;

    bool update_and_show_manipulations = false;
    bool update_and_show_settings = false;
    bool update_and_show_layers = false;

    const auto item = GetSelection();

    if ( multiple_selection() || item && m_objects_model->GetItemType(item) == itInstanceRoot ) 
    {
        og_name = _(L("Group manipulation"));

        const Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();
        // don't show manipulation panel for case of all Object's parts selection 
        update_and_show_manipulations = !selection.is_single_full_instance();
    }
    else
    {
        if (item)
        {
            if (m_objects_model->GetParent(item) == wxDataViewItem(0)) {
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
                    og_name = type & itLayerRoot ? _(L("Layers Editing")) : _(L("Layer Editing"));
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
            wxGetApp().obj_manipul()->get_og()->set_value("object_name", m_objects_model->GetName(item));
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

void ObjectList::add_object_to_list(size_t obj_idx)
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
            auto opt_keys = volume->config.keys();
            if (!opt_keys.empty() && !(opt_keys.size() == 1 && opt_keys[0] == "extruder")) {
                select_item(m_objects_model->AddSettingsChild(vol_item));
                Expand(vol_item);
            }
        }
        Expand(item);
    }

    // add instances to the object, if it has those
    if (model_object->instances.size()>1)
        increase_object_instances(obj_idx, model_object->instances.size());

    // add settings to the object, if it has those
    auto opt_keys = model_object->config.keys();
    if (!opt_keys.empty() && !(opt_keys.size() == 1 && opt_keys[0] == "extruder")) {
        select_item(m_objects_model->AddSettingsChild(item));
        Expand(item);
    }

    // Add layers if it has
    add_layer_root_item(item);

#ifndef __WXOSX__ 
    selection_changed();
#endif //__WXMSW__
}

void ObjectList::delete_object_from_list()
{
    auto item = GetSelection();
    if (!item) 
        return;
    if (m_objects_model->GetParent(item) == wxDataViewItem(0))
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
                    m_objects_model->SetValue(extruder, m_objects_model->GetItemById(item->obj_idx), 1);
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

    wxDataViewItemArray sels;
    GetSelections(sels);

    wxDataViewItem  parent = wxDataViewItem(0);

    for (auto& item : sels)
    {
        if (m_objects_model->GetParent(item) == wxDataViewItem(0))
            delete_from_model_and_list(itObject, m_objects_model->GetIdByItem(item), -1);
        else {
            if (m_objects_model->GetItemType(item) & itLayer) {
                parent = m_objects_model->GetParent(item);
                wxDataViewItemArray children;
                if (m_objects_model->GetChildren(parent, children) == 1)
                    parent = m_objects_model->GetTopParent(item);
            }
            else if (sels.size() == 1)
                select_item(m_objects_model->GetParent(item));
            
            del_subobject_item(item);
        }
    }

    if (parent)
        select_item(parent);
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

double get_min_layer_height(const int extruder_idx)
{
    const DynamicPrintConfig& config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    return config.opt_float("min_layer_height", extruder_idx <= 0 ? 0 : extruder_idx-1);
}

double get_max_layer_height(const int extruder_idx)
{
    const DynamicPrintConfig& config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
    return config.opt_float("max_layer_height", extruder_idx <= 0 ? 0 : extruder_idx-1);
}

void ObjectList::add_layer_range_after_current(const t_layer_height_range& current_range)
{
    const int obj_idx = get_selected_obj_idx();
    if (obj_idx < 0) return;

    const wxDataViewItem layers_item = GetSelection();

    t_layer_config_ranges& ranges = object(obj_idx)->layer_config_ranges;

    const t_layer_height_range& last_range = (--ranges.end())->first;
    
    if (current_range == last_range)
    {
        const t_layer_height_range& new_range = { last_range.second, last_range.second + 2.0f };
        ranges[new_range] = get_default_layer_config(obj_idx);
        add_layer_item(new_range, layers_item);
    }
    else
    {
        const t_layer_height_range& next_range = (++ranges.find(current_range))->first;

        if (current_range.second > next_range.first)
            return; // range division has no sense
        
        const int layer_idx = m_objects_model->GetItemIdByLayerRange(obj_idx, next_range);
        if (layer_idx < 0)
            return;

        if (current_range.second == next_range.first)
        {
            const auto old_config = ranges.at(next_range);

            const coordf_t delta = (next_range.second - next_range.first);
            if (delta < get_min_layer_height(old_config.opt_int("extruder"))/*0.05f*/) // next range division has no sense 
                return; 

            const coordf_t midl_layer = next_range.first + 0.5f * delta;
            
            t_layer_height_range new_range = { midl_layer, next_range.second };

            // delete old layer

            wxDataViewItem layer_item = m_objects_model->GetItemByLayerRange(obj_idx, next_range);
            del_subobject_item(layer_item);

            // create new 2 layers instead of deleted one

            ranges[new_range] = old_config;
            add_layer_item(new_range, layers_item, layer_idx);

            new_range = { current_range.second, midl_layer };
            ranges[new_range] = get_default_layer_config(obj_idx);
            add_layer_item(new_range, layers_item, layer_idx);
        }
        else
        {
            const t_layer_height_range new_range = { current_range.second, next_range.first };
            ranges[new_range] = get_default_layer_config(obj_idx);
            add_layer_item(new_range, layers_item, layer_idx);
        }        
    }

    changed_object(obj_idx);

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

    if (config.keys().size() > 2)
        select_item(m_objects_model->AddSettingsChild(layer_item));
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
        return true;
    }

    return false;
}

bool ObjectList::edit_layer_range(const t_layer_height_range& range, const t_layer_height_range& new_range)
{
    const int obj_idx = get_selected_obj_idx();
    if (obj_idx < 0) return false;

    const ItemType sel_type = m_objects_model->GetItemType(GetSelection());

    t_layer_config_ranges& ranges = object(obj_idx)->layer_config_ranges;

    const DynamicPrintConfig config = ranges[range];

    ranges.erase(range);
    ranges[new_range] = config;

    wxDataViewItem root_item = m_objects_model->GetLayerRootItem(m_objects_model->GetItemById(obj_idx));
    m_objects_model->DeleteChildren(root_item);

    if (root_item.IsOk())
        // create Layer item(s) according to the layer_config_ranges
        for (const auto r : ranges)
            add_layer_item(r.first, root_item);

    select_item(sel_type&itLayer ? m_objects_model->GetItemByLayerRange(obj_idx, new_range) : root_item);
    Expand(root_item);

    return true;
}

void ObjectList::init_objects()
{
    m_objects = wxGetApp().model_objects();
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

void ObjectList::update_selections()
{
    const Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();
    wxDataViewItemArray sels;

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
        }
    }
    else if (selection.is_any_volume() || selection.is_any_modifier())
    {
        for (auto idx : selection.get_volume_idxs()) {
            const auto gl_vol = selection.get_volume(idx);
			if (gl_vol->volume_idx() >= 0)
                // Only add GLVolumes with non-negative volume_ids. GLVolumes with negative volume ids
                // are not associated with ModelVolumes, but they are temporarily generated by the backend
                // (for example, SLA supports or SLA pad).
                sels.Add(m_objects_model->GetItemByVolumeId(gl_vol->object_idx(), gl_vol->volume_idx()));
        }
        m_selection_mode = smVolume;
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
                    if (obj_ins.second.find(glv_ins_idx) != obj_ins.second.end()) {
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

    if (sels.size() == 0)
        m_selection_mode = smUndef;
    
    select_items(sels);

    // Scroll selected Item in the middle of an object list
    this->EnsureVisible(this->GetCurrentItem());
}

void ObjectList::update_selections_on_canvas()
{
    Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();

    const int sel_cnt = GetSelectedItemsCount();
    if (sel_cnt == 0) {
        selection.clear();
        wxGetApp().plater()->canvas3D()->update_gizmos_on_off_state();
        return;
    }

    auto add_to_selection = [this](const wxDataViewItem& item, Selection& selection, int instance_idx, bool as_single_selection)
    {
        const ItemType& type = m_objects_model->GetItemType(item);
        const int obj_idx = m_objects_model->GetObjectIdByItem(item);

        if (type == itVolume) {
            const int vol_idx = m_objects_model->GetVolumeIdByItem(item);
            selection.add_volume(obj_idx, vol_idx, std::max(instance_idx, 0), as_single_selection);
        }
        else if (type == itInstance) {
            const int inst_idx = m_objects_model->GetInstanceIdByItem(item);
            selection.add_instance(obj_idx, inst_idx, as_single_selection);
        }
        else
            selection.add_object(obj_idx, as_single_selection);
    };

    // stores current instance idx before to clear the selection
    int instance_idx = selection.get_instance_idx();

    if (sel_cnt == 1) {
        wxDataViewItem item = GetSelection();
        if (m_objects_model->GetItemType(item) & (itSettings | itInstanceRoot | itLayerRoot | itLayer))
            add_to_selection(m_objects_model->GetParent(item), selection, instance_idx, true);
        else
            add_to_selection(item, selection, instance_idx, true);

        wxGetApp().plater()->canvas3D()->update_gizmos_on_off_state();
        wxGetApp().plater()->canvas3D()->render();
        return;
    }
    
    wxDataViewItemArray sels;
    GetSelections(sels);

    selection.clear();
    for (auto item: sels)
        add_to_selection(item, selection, instance_idx, false);

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
        for (int i = 0; i < m_objects->size(); i++)
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
    // All items are unselected 
    if (!GetSelection())
    {
        m_last_selected_item = wxDataViewItem(0);
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
        type & itVolume   && !(m_selection_mode & smVolume  ) ||
        type & itLayer    && !(m_selection_mode & smLayer   ) ||
        type & itInstance && !(m_selection_mode & smInstance)
        )
    {
        // Inform user why selection isn't complited
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

        for (const auto child : children)
            if (IsSelected(child) && m_objects_model->GetItemType(child) & item_type)
                sels.Add(child);

        // If some part is selected, unselect all items except of selected parts of the current object
        UnselectAll();
        SetSelections(sels);
    }
    else
    {
        for (const auto item : sels)
        {
            if (!IsSelected(item)) // if this item is unselected now (from previous actions)
                continue;

            if (m_objects_model->GetItemType(item) & itSettings) {
                Unselect(item);
                continue;
            }

            const wxDataViewItem& parent = m_objects_model->GetParent(item);
            if (parent != wxDataViewItem(0) && IsSelected(parent))
                Unselect(parent);
            else
            {
                wxDataViewItemArray unsels;
                m_objects_model->GetAllChildren(item, unsels);
                for (const auto unsel_item : unsels)
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
        m_last_selected_item = wxDataViewItem(0);

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

    take_snapshot(_(L("Paste from Clipboard")));

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
        select_item(m_objects_model->AddSettingsChild(item));
    }
}

void ObjectList::last_volume_is_deleted(const int obj_idx)
{

    if (obj_idx < 0 || m_objects->empty() ||
        obj_idx <= m_objects->size() ||
        (*m_objects)[obj_idx]->volumes.empty())
        return;
    auto volume = (*m_objects)[obj_idx]->volumes[0];

    // clear volume's config values
    volume->config.clear();

    // set a default extruder value, since user can't add it manually
    volume->config.set_key_value("extruder", new ConfigOptionInt(0));
}

bool ObjectList::has_multi_part_objects()
{
    if (!m_objects_model->IsEmpty()) {
        wxDataViewItemArray items;
        m_objects_model->GetChildren(wxDataViewItem(0), items);

        for (auto& item : items)
            if (m_objects_model->GetItemByType(item, itVolume))
                return true;
    }
    return false;
}

void ObjectList::update_settings_items()
{
    m_prevent_canvas_selection_update = true;
    wxDataViewItemArray sel;
    GetSelections(sel); // stash selection

    wxDataViewItemArray items;
    m_objects_model->GetChildren(wxDataViewItem(0), items);

    for (auto& item : items) {        
        const wxDataViewItem& settings_item = m_objects_model->GetSettingsItem(item);
        select_item(settings_item ? settings_item : m_objects_model->AddSettingsChild(item));

        // If settings item was deleted from the list, 
        // it's need to be deleted from selection array, if it was there
        if (settings_item != m_objects_model->GetSettingsItem(item) && 
            sel.Index(settings_item) != wxNOT_FOUND) {
            sel.Remove(settings_item);
        }
    }

    // restore selection:
    SetSelections(sel);
    m_prevent_canvas_selection_update = false;
}

// Update settings item for item had it
void ObjectList::update_settings_item_and_selection(wxDataViewItem item, wxDataViewItemArray& selections)
{
    const wxDataViewItem& settings_item = m_objects_model->GetSettingsItem(item);
    select_item(settings_item ? settings_item : m_objects_model->AddSettingsChild(item));

    // If settings item was deleted from the list, 
    // it's need to be deleted from selection array, if it was there
    if (settings_item != m_objects_model->GetSettingsItem(item) &&
        selections.Index(settings_item) != wxNOT_FOUND) {
        selections.Remove(settings_item);

        // Select item, if settings_item doesn't exist for item anymore, but was selected
        if (selections.Index(item) == wxNOT_FOUND)
            selections.Add(item);
    }
}

void ObjectList::update_object_list_by_printer_technology()
{
    m_prevent_canvas_selection_update = true;
    wxDataViewItemArray sel;
    GetSelections(sel); // stash selection

    wxDataViewItemArray object_items;
    m_objects_model->GetChildren(wxDataViewItem(0), object_items);

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
    for (int inst_idx = model_object->instances.size() - 1; inst_idx >= 0; inst_idx--)
    {
        if (find(inst_idxs.begin(), inst_idxs.end(), inst_idx) != inst_idxs.end())
            continue;
        model_object->delete_instance(inst_idx);
    }

    // Add new object to the object_list
    add_object_to_list(m_objects->size() - 1);

    for (std::set<int>::const_reverse_iterator it = inst_idxs.rbegin(); it != inst_idxs.rend(); ++it)
    {
        // delete selected instance from the object
        del_subobject_from_object(obj_idx, *it, itInstance);
        delete_instance_from_list(obj_idx, *it);
    }
}

void ObjectList::instances_to_separated_objects(const int obj_idx)
{
    const int inst_cnt = (*m_objects)[obj_idx]->instances.size();

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
        add_object_to_list(m_objects->size() - 1);

        // delete current instance from the initial object
        del_subobject_from_object(obj_idx, i, itInstance);
        delete_instance_from_list(obj_idx, i);
    }
}

void ObjectList::split_instances()
{
    const Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();
    const int obj_idx = selection.get_object_idx();
    if (obj_idx == -1)
        return;

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
    m_objects_model->GetValue(valueOld, item, 0);

    DataViewBitmapText bmpText;
    bmpText << valueOld;

    // But replace the text with the value entered by user.
    bmpText.SetText(new_name);

    wxVariant value;    
    value << bmpText;
    m_objects_model->SetValue(value, item, 0);
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

    GetColumn(0)->SetWidth(19 * em);
    GetColumn(1)->SetWidth( 8 * em);
    GetColumn(2)->SetWidth( 2 * em);

    // rescale all icons, used by ObjectList
    msw_rescale_icons();

    // rescale/update existing items with bitmaps
    m_objects_model->Rescale();

    // rescale menus
    for (MenuWithSeparators* menu : { &m_menu_object, 
                                      &m_menu_part, 
                                      &m_menu_sla_object, 
                                      &m_menu_instance, 
                                      &m_menu_layer })
        msw_rescale_menu(menu);

    Layout();
}

void ObjectList::ItemValueChanged(wxDataViewEvent &event)
{
    if (event.GetColumn() == 0)
        update_name_in_model(event.GetItem());
    else if (event.GetColumn() == 1)
        update_extruder_in_config(event.GetItem());
}

void ObjectList::OnEditingDone(wxDataViewEvent &event)
{
    if (event.GetColumn() != 0)
        return;

    const auto renderer = dynamic_cast<BitmapTextRenderer*>(GetColumn(0)->GetRenderer());

    if (renderer->WasCanceled())
		wxTheApp->CallAfter([this]{
			show_error(this, _(L("The supplied name is not valid;")) + "\n" +
				             _(L("the following characters are not allowed:")) + " <>:/\\|?*\"");
		});
}

void ObjectList::show_multi_selection_menu()
{
    wxDataViewItemArray sels;
    GetSelections(sels);

    for (const wxDataViewItem& item : sels)
        if (!(m_objects_model->GetItemType(item) & (itVolume | itObject)))
            // show this menu only for Object(s)/Volume(s) selection
            return;

    wxMenu* menu = new wxMenu();

    if (extruders_count() > 1)
        append_menu_item(menu, wxID_ANY, _(L("Set extruder for selected items")),
            _(L("Select extruder number for selected objects and/or parts")),
            [this](wxCommandEvent&) { extruder_selection(); }, "", menu);

    PopupMenu(menu);
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
        m_objects_model->SetValue(extruder_str, type & itInstance ? m_objects_model->GetTopParent(item) : item, 1);

        const int obj_idx = type & itObject ? m_objects_model->GetIdByItem(item) :
                            m_objects_model->GetIdByItem(m_objects_model->GetTopParent(item));

        wxGetApp().plater()->canvas3D()->ensure_on_bed(obj_idx);
    }

    // update scene
    wxGetApp().plater()->update();
}

void ObjectList::recreate_object_list()
{
    m_prevent_list_events = true;
    m_prevent_canvas_selection_update = true;

    m_objects_model->DeleteAll();

    size_t obj_idx = 0;
    while (obj_idx < m_objects->size()) {
        add_object_to_list(obj_idx);
        ++obj_idx;
    }

    m_prevent_canvas_selection_update = false;
    m_prevent_list_events = false;
}

} //namespace GUI
} //namespace Slic3r 