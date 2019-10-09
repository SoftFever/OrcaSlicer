#include "wxExtensions.hpp"

#include <stdexcept>
#include <cmath>

#include "libslic3r/Utils.hpp"
#include "libslic3r/Model.hpp"

#include <wx/sizer.h>
#include <wx/bmpcbox.h>
#include <wx/statline.h>
#include <wx/dcclient.h>
#include <wx/numformatter.h>

#include <boost/algorithm/string/replace.hpp>

#include "BitmapCache.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "libslic3r/GCode/PreviewData.hpp"
#include "I18N.hpp"
#include "GUI_Utils.hpp"
#include "PresetBundle.hpp"
#include "../Utils/MacDarkMode.hpp"

using Slic3r::GUI::from_u8;

wxDEFINE_EVENT(wxCUSTOMEVT_TICKSCHANGED, wxEvent);
wxDEFINE_EVENT(wxCUSTOMEVT_LAST_VOLUME_IS_DELETED, wxCommandEvent);

#ifndef __WXGTK__// msw_menuitem_bitmaps is used for MSW and OSX
static std::map<int, std::string> msw_menuitem_bitmaps;
#ifdef __WXMSW__
void msw_rescale_menu(wxMenu* menu)
{
	struct update_icons {
		static void run(wxMenuItem* item) {
			const auto it = msw_menuitem_bitmaps.find(item->GetId());
			if (it != msw_menuitem_bitmaps.end()) {
				const wxBitmap& item_icon = create_scaled_bitmap(nullptr, it->second);
				if (item_icon.IsOk())
					item->SetBitmap(item_icon);
			}
			if (item->IsSubMenu())
				for (wxMenuItem *sub_item : item->GetSubMenu()->GetMenuItems())
					update_icons::run(sub_item);
		}
	};

	for (wxMenuItem *item : menu->GetMenuItems())
		update_icons::run(item);
}
#endif /* __WXMSW__ */
#endif /* no __WXGTK__ */

void enable_menu_item(wxUpdateUIEvent& evt, std::function<bool()> const cb_condition, wxMenuItem* item)
{
    const bool enable = cb_condition();
    evt.Enable(enable);

#ifdef __WXOSX__
    const auto it = msw_menuitem_bitmaps.find(item->GetId());
    if (it != msw_menuitem_bitmaps.end())
    {
        const wxBitmap& item_icon = create_scaled_bitmap(nullptr, it->second, 16, false, !enable);
        if (item_icon.IsOk())
            item->SetBitmap(item_icon);
    }
#endif // __WXOSX__
}

wxMenuItem* append_menu_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent& event)> cb, const wxBitmap& icon, wxEvtHandler* event_handler,
    std::function<bool()> const cb_condition, wxWindow* parent)
{
    if (id == wxID_ANY)
        id = wxNewId();

    auto *item = new wxMenuItem(menu, id, string, description);
    if (icon.IsOk()) {
        item->SetBitmap(icon);
    }
    menu->Append(item);

#ifdef __WXMSW__
    if (event_handler != nullptr && event_handler != menu)
        event_handler->Bind(wxEVT_MENU, cb, id);
    else
#endif // __WXMSW__
        menu->Bind(wxEVT_MENU, cb, id);

    if (parent) {
        parent->Bind(wxEVT_UPDATE_UI, [cb_condition, item](wxUpdateUIEvent& evt) {
            enable_menu_item(evt, cb_condition, item); }, id);
    }

    return item;
}

wxMenuItem* append_menu_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent& event)> cb, const std::string& icon, wxEvtHandler* event_handler,
    std::function<bool()> const cb_condition, wxWindow* parent)
{
    if (id == wxID_ANY)
        id = wxNewId();

    const wxBitmap& bmp = !icon.empty() ? create_scaled_bitmap(nullptr, icon) : wxNullBitmap;   // FIXME: pass window ptr
//#ifdef __WXMSW__
#ifndef __WXGTK__
    if (bmp.IsOk())
        msw_menuitem_bitmaps[id] = icon;
#endif /* __WXMSW__ */

    return append_menu_item(menu, id, string, description, cb, bmp, event_handler, cb_condition, parent);
}

wxMenuItem* append_submenu(wxMenu* menu, wxMenu* sub_menu, int id, const wxString& string, const wxString& description, const std::string& icon,
    std::function<bool()> const cb_condition, wxWindow* parent)
{
    if (id == wxID_ANY)
        id = wxNewId();

    wxMenuItem* item = new wxMenuItem(menu, id, string, description);
    if (!icon.empty()) {
        item->SetBitmap(create_scaled_bitmap(nullptr, icon));    // FIXME: pass window ptr
//#ifdef __WXMSW__
#ifndef __WXGTK__
        msw_menuitem_bitmaps[id] = icon;
#endif /* __WXMSW__ */
    }

    item->SetSubMenu(sub_menu);
    menu->Append(item);

    if (parent) {
        parent->Bind(wxEVT_UPDATE_UI, [cb_condition, item](wxUpdateUIEvent& evt) {
            enable_menu_item(evt, cb_condition, item); }, id);
    }

    return item;
}

wxMenuItem* append_menu_radio_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent& event)> cb, wxEvtHandler* event_handler)
{
    if (id == wxID_ANY)
        id = wxNewId();

    wxMenuItem* item = menu->AppendRadioItem(id, string, description);

#ifdef __WXMSW__
    if (event_handler != nullptr && event_handler != menu)
        event_handler->Bind(wxEVT_MENU, cb, id);
    else
#endif // __WXMSW__
        menu->Bind(wxEVT_MENU, cb, id);

    return item;
}

wxMenuItem* append_menu_check_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent& event)> cb, wxEvtHandler* event_handler)
{
    if (id == wxID_ANY)
        id = wxNewId();

    wxMenuItem* item = menu->AppendCheckItem(id, string, description);

#ifdef __WXMSW__
    if (event_handler != nullptr && event_handler != menu)
        event_handler->Bind(wxEVT_MENU, cb, id);
    else
#endif // __WXMSW__
        menu->Bind(wxEVT_MENU, cb, id);

    return item;
}

const unsigned int wxCheckListBoxComboPopup::DefaultWidth = 200;
const unsigned int wxCheckListBoxComboPopup::DefaultHeight = 200;
const unsigned int wxCheckListBoxComboPopup::DefaultItemHeight = 18;

bool wxCheckListBoxComboPopup::Create(wxWindow* parent)
{
    return wxCheckListBox::Create(parent, wxID_HIGHEST + 1, wxPoint(0, 0));
}

wxWindow* wxCheckListBoxComboPopup::GetControl()
{
    return this;
}

void wxCheckListBoxComboPopup::SetStringValue(const wxString& value)
{
    m_text = value;
}

wxString wxCheckListBoxComboPopup::GetStringValue() const
{
    return m_text;
}

wxSize wxCheckListBoxComboPopup::GetAdjustedSize(int minWidth, int prefHeight, int maxHeight)
{
    // matches owner wxComboCtrl's width
    // and sets height dinamically in dependence of contained items count

    wxComboCtrl* cmb = GetComboCtrl();
    if (cmb != nullptr)
    {
        wxSize size = GetComboCtrl()->GetSize();

        unsigned int count = GetCount();
        if (count > 0)
            size.SetHeight(count * DefaultItemHeight);
        else
            size.SetHeight(DefaultHeight);

        return size;
    }
    else
        return wxSize(DefaultWidth, DefaultHeight);
}

void wxCheckListBoxComboPopup::OnKeyEvent(wxKeyEvent& evt)
{
    // filters out all the keys which are not working properly
    switch (evt.GetKeyCode())
    {
    case WXK_LEFT:
    case WXK_UP:
    case WXK_RIGHT:
    case WXK_DOWN:
    case WXK_PAGEUP:
    case WXK_PAGEDOWN:
    case WXK_END:
    case WXK_HOME:
    case WXK_NUMPAD_LEFT:
    case WXK_NUMPAD_UP:
    case WXK_NUMPAD_RIGHT:
    case WXK_NUMPAD_DOWN:
    case WXK_NUMPAD_PAGEUP:
    case WXK_NUMPAD_PAGEDOWN:
    case WXK_NUMPAD_END:
    case WXK_NUMPAD_HOME:
    {
        break;
    }
    default:
    {
        evt.Skip();
        break;
    }
    }
}

void wxCheckListBoxComboPopup::OnCheckListBox(wxCommandEvent& evt)
{
    // forwards the checklistbox event to the owner wxComboCtrl

    if (m_check_box_events_status == OnCheckListBoxFunction::FreeToProceed )
    {
        wxComboCtrl* cmb = GetComboCtrl();
        if (cmb != nullptr) {
            wxCommandEvent event(wxEVT_CHECKLISTBOX, cmb->GetId());
            event.SetEventObject(cmb);
            cmb->ProcessWindowEvent(event);
        }
    }

    evt.Skip();

    #ifndef _WIN32  // events are sent differently on OSX+Linux vs Win (more description in header file)
        if ( m_check_box_events_status == OnCheckListBoxFunction::RefuseToProceed )
            // this happens if the event was resent by OnListBoxSelection - next call to OnListBoxSelection is due to user clicking the text, so the function should
            // explicitly change the state on the checkbox
            m_check_box_events_status = OnCheckListBoxFunction::WasRefusedLastTime;
        else
            // if the user clicked the checkbox square, this event was sent before OnListBoxSelection was called, so we don't want it to resend it
            m_check_box_events_status = OnCheckListBoxFunction::RefuseToProceed;
    #endif
}

void wxCheckListBoxComboPopup::OnListBoxSelection(wxCommandEvent& evt)
{
    // transforms list box item selection event into checklistbox item toggle event 

    int selId = GetSelection();
    if (selId != wxNOT_FOUND)
    {
        #ifndef _WIN32
            if (m_check_box_events_status == OnCheckListBoxFunction::RefuseToProceed)
        #endif
                Check((unsigned int)selId, !IsChecked((unsigned int)selId));

        m_check_box_events_status = OnCheckListBoxFunction::FreeToProceed; // so the checkbox reacts to square-click the next time

        SetSelection(wxNOT_FOUND);
        wxCommandEvent event(wxEVT_CHECKLISTBOX, GetId());
        event.SetInt(selId);
        event.SetEventObject(this);
        ProcessEvent(event);
    }
}


// ***  wxDataViewTreeCtrlComboPopup  ***

const unsigned int wxDataViewTreeCtrlComboPopup::DefaultWidth = 270;
const unsigned int wxDataViewTreeCtrlComboPopup::DefaultHeight = 200;
const unsigned int wxDataViewTreeCtrlComboPopup::DefaultItemHeight = 22;

bool wxDataViewTreeCtrlComboPopup::Create(wxWindow* parent)
{
	return wxDataViewTreeCtrl::Create(parent, wxID_ANY/*HIGHEST + 1*/, wxPoint(0, 0), wxDefaultSize/*wxSize(270, -1)*/, wxDV_NO_HEADER);
}
/*
wxSize wxDataViewTreeCtrlComboPopup::GetAdjustedSize(int minWidth, int prefHeight, int maxHeight)
{
	// matches owner wxComboCtrl's width
	// and sets height dinamically in dependence of contained items count
	wxComboCtrl* cmb = GetComboCtrl();
	if (cmb != nullptr)
	{
		wxSize size = GetComboCtrl()->GetSize();
		if (m_cnt_open_items > 0)
			size.SetHeight(m_cnt_open_items * DefaultItemHeight);
		else
			size.SetHeight(DefaultHeight);

		return size;
	}
	else
		return wxSize(DefaultWidth, DefaultHeight);
}
*/
void wxDataViewTreeCtrlComboPopup::OnKeyEvent(wxKeyEvent& evt)
{
	// filters out all the keys which are not working properly
	if (evt.GetKeyCode() == WXK_UP)
	{
		return;
	}
	else if (evt.GetKeyCode() == WXK_DOWN)
	{
		return;
	}
	else
	{
		evt.Skip();
		return;
	}
}

void wxDataViewTreeCtrlComboPopup::OnDataViewTreeCtrlSelection(wxCommandEvent& evt)
{
	wxComboCtrl* cmb = GetComboCtrl();
	auto selected = GetItemText(GetSelection());
	cmb->SetText(selected);
}

// edit tooltip : change Slic3r to SLIC3R_APP_KEY
// Temporary workaround for localization
void edit_tooltip(wxString& tooltip)
{
    tooltip.Replace("Slic3r", SLIC3R_APP_KEY, true);
}

/* Function for rescale of buttons in Dialog under MSW if dpi is changed.
 * btn_ids - vector of buttons identifiers
 */
void msw_buttons_rescale(wxDialog* dlg, const int em_unit, const std::vector<int>& btn_ids)
{
    const wxSize& btn_size = wxSize(-1, int(2.5f * em_unit + 0.5f));

    for (int btn_id : btn_ids) {
        // There is a case [FirmwareDialog], when we have wxControl instead of wxButton
        // so let casting everything to the wxControl
        wxControl* btn = static_cast<wxControl*>(dlg->FindWindowById(btn_id, dlg));
        if (btn)
            btn->SetMinSize(btn_size);
    }
}

/* Function for getting of em_unit value from correct parent.
 * In most of cases it is m_em_unit value from GUI_App,
 * but for DPIDialogs it's its own value. 
 * This value will be used to correct rescale after moving between 
 * Displays with different HDPI */
int em_unit(wxWindow* win)
{
    if (win)
    {
        wxTopLevelWindow *toplevel = Slic3r::GUI::find_toplevel_parent(win);
        Slic3r::GUI::DPIDialog* dlg = dynamic_cast<Slic3r::GUI::DPIDialog*>(toplevel);
        if (dlg)
            return dlg->em_unit();
        Slic3r::GUI::DPIFrame* frame = dynamic_cast<Slic3r::GUI::DPIFrame*>(toplevel);
        if (frame)
            return frame->em_unit();
    }
    
    return Slic3r::GUI::wxGetApp().em_unit();
}

// If an icon has horizontal orientation (width > height) call this function with is_horizontal = true
wxBitmap create_scaled_bitmap(wxWindow *win, const std::string& bmp_name_in, 
    const int px_cnt/* = 16*/, const bool is_horizontal /* = false*/, const bool grayscale/* = false*/)
{
    static Slic3r::GUI::BitmapCache cache;

#ifdef __APPLE__
    // Note: win->GetContentScaleFactor() is not used anymore here because it tends to
    // return bogus results quite often (such as 1.0 on Retina or even 0.0).
    // We're using the max scaling factor across all screens because it's very likely to be good enough.

    static float max_scaling_factor = NAN;
    if (std::isnan(max_scaling_factor)) {
        max_scaling_factor = Slic3r::GUI::mac_max_scaling_factor();
    }
    const float scale_factor = win != nullptr ? max_scaling_factor : 1.0f;
#else
    (void)(win);
    const float scale_factor = 1.0f;
#endif

    unsigned int height, width = height = 0;
    unsigned int& scale_base = is_horizontal ? width : height;

    scale_base = (unsigned int)(em_unit(win) * px_cnt * 0.1f + 0.5f);

    std::string bmp_name = bmp_name_in;
    boost::replace_last(bmp_name, ".png", "");

    // Try loading an SVG first, then PNG if SVG is not found:
    wxBitmap *bmp = cache.load_svg(bmp_name, width, height, scale_factor, grayscale);
    if (bmp == nullptr) {
        bmp = cache.load_png(bmp_name, width, height, grayscale);
    }

    if (bmp == nullptr) {
        // Neither SVG nor PNG has been found, raise error
        throw std::runtime_error("Could not load bitmap: " + bmp_name);
    }

    return *bmp;
}


Slic3r::GUI::BitmapCache* m_bitmap_cache = nullptr;
/*static*/ std::vector<wxBitmap*> get_extruder_color_icons()
{
    // Create the bitmap with color bars.
    std::vector<wxBitmap*> bmps;
    std::vector<std::string> colors = Slic3r::GUI::wxGetApp().plater()->get_extruder_colors_from_plater_config();

    if (bmps.empty())
        return bmps;

    unsigned char rgb[3];

    /* It's supposed that standard size of an icon is 36px*16px for 100% scaled display.
     * So set sizes for solid_colored icons used for filament preset
     * and scale them in respect to em_unit value
     */
    const double em = Slic3r::GUI::wxGetApp().em_unit();
    const int icon_width = lround(3.2 * em);
    const int icon_height = lround(1.6 * em);

    for (const std::string& color : colors)
    {
        wxBitmap* bitmap = m_bitmap_cache->find(color);
        if (bitmap == nullptr) {
            // Paint the color icon.
            Slic3r::PresetBundle::parse_color(color, rgb);
            bitmap = m_bitmap_cache->insert(color, m_bitmap_cache->mksolid(icon_width, icon_height, rgb));
        }
        bmps.emplace_back(bitmap);
    }

    return bmps;
}


static wxBitmap get_extruder_color_icon(size_t extruder_idx)
{
    // Create the bitmap with color bars.
    std::vector<wxBitmap*> bmps = get_extruder_color_icons();
    if (bmps.empty())
        return wxNullBitmap;

    return *bmps[extruder_idx >= bmps.size() ? 0 : extruder_idx];
}

// *****************************************************************************
// ----------------------------------------------------------------------------
// ObjectDataViewModelNode
// ----------------------------------------------------------------------------

void ObjectDataViewModelNode::init_container()
{
#ifdef __WXGTK__
    // it's necessary on GTK because of control have to know if this item will be container
    // in another case you couldn't to add subitem for this item
    // it will be produce "segmentation fault"
    m_container = true;
#endif  //__WXGTK__
}

#define LAYER_ROOT_ICON "edit_layers_all"
#define LAYER_ICON      "edit_layers_some"

ObjectDataViewModelNode::ObjectDataViewModelNode(ObjectDataViewModelNode* parent, const ItemType type) :
    m_parent(parent),
    m_type(type),
    m_extruder(wxEmptyString)
{
    if (type == itSettings)
        m_name = "Settings to modified";
    else if (type == itInstanceRoot)
        m_name = _(L("Instances"));
    else if (type == itInstance)
    {
        m_idx = parent->GetChildCount();
        m_name = wxString::Format(_(L("Instance %d")), m_idx + 1);

        set_action_and_extruder_icons();
    }
    else if (type == itLayerRoot)
    {
        m_bmp = create_scaled_bitmap(nullptr, LAYER_ROOT_ICON);    // FIXME: pass window ptr
        m_name = _(L("Layers"));
    }

    if (type & (itInstanceRoot | itLayerRoot))
        init_container();
}

ObjectDataViewModelNode::ObjectDataViewModelNode(ObjectDataViewModelNode* parent, 
                                                 const t_layer_height_range& layer_range,
                                                 const int idx /*= -1 */, 
                                                 const wxString& extruder) :
    m_parent(parent),
    m_type(itLayer),
    m_idx(idx),
    m_layer_range(layer_range),
    m_extruder(extruder)
{
    const int children_cnt = parent->GetChildCount();
    if (idx < 0)
        m_idx = children_cnt;
    else
    {
        // update indexes for another Laeyr Nodes
        for (int i = m_idx; i < children_cnt; i++)
            parent->GetNthChild(i)->SetIdx(i + 1);
    }
    const std::string label_range = (boost::format(" %.2f-%.2f ") % layer_range.first % layer_range.second).str();
    m_name = _(L("Range")) + label_range + "(" + _(L("mm")) + ")";
    m_bmp = create_scaled_bitmap(nullptr, LAYER_ICON);    // FIXME: pass window ptr

    set_action_and_extruder_icons();
    init_container();
}

#ifndef NDEBUG
bool ObjectDataViewModelNode::valid()
{
	// Verify that the object was not deleted yet.
	assert(m_idx >= -1);
	return m_idx >= -1;
}
#endif /* NDEBUG */

void ObjectDataViewModelNode::set_action_and_extruder_icons()
{
    m_action_icon_name = m_type & itObject              ? "advanced_plus" : 
                         m_type & (itVolume | itLayer)  ? "cog" : /*m_type & itInstance*/ "set_separate_obj";
    m_action_icon = create_scaled_bitmap(nullptr, m_action_icon_name);    // FIXME: pass window ptr

    // set extruder bitmap
    int extruder_idx = atoi(m_extruder.c_str());
    if (extruder_idx > 0) --extruder_idx;
    m_extruder_bmp = get_extruder_color_icon(extruder_idx);
}

void ObjectDataViewModelNode::set_printable_icon(PrintIndicator printable)
{
    m_printable = printable;
    m_printable_icon = m_printable == piUndef ? m_empty_bmp :
                       create_scaled_bitmap(nullptr, m_printable == piPrintable ? "eye_open.png" : "eye_closed.png");
}

void ObjectDataViewModelNode::update_settings_digest_bitmaps()
{
    m_bmp = m_empty_bmp;

    std::map<std::string, wxBitmap>& categories_icon = Slic3r::GUI::wxGetApp().obj_list()->CATEGORY_ICON;

    std::string scaled_bitmap_name = m_name.ToUTF8().data();
    scaled_bitmap_name += "-em" + std::to_string(Slic3r::GUI::wxGetApp().em_unit());

    wxBitmap *bmp = m_bitmap_cache->find(scaled_bitmap_name);
    if (bmp == nullptr) {
        std::vector<wxBitmap> bmps;
        for (auto& cat : m_opt_categories)
            bmps.emplace_back(  categories_icon.find(cat) == categories_icon.end() ?
                                wxNullBitmap : categories_icon.at(cat));
        bmp = m_bitmap_cache->insert(scaled_bitmap_name, bmps);
    }

    m_bmp = *bmp;
}

bool ObjectDataViewModelNode::update_settings_digest(const std::vector<std::string>& categories)
{
    if (m_type != itSettings || m_opt_categories == categories)
        return false;

    m_opt_categories = categories;
    m_name = wxEmptyString;

    for (auto& cat : m_opt_categories)
        m_name += _(cat) + "; ";
    if (!m_name.IsEmpty())
        m_name.erase(m_name.Length()-2, 2); // Delete last "; "

    update_settings_digest_bitmaps();

    return true;
}

void ObjectDataViewModelNode::msw_rescale()
{
    if (!m_action_icon_name.empty())
        m_action_icon = create_scaled_bitmap(nullptr, m_action_icon_name);

    if (m_printable != piUndef)
        m_printable_icon = create_scaled_bitmap(nullptr, m_printable == piPrintable ? "eye_open.png" : "eye_closed.png");

    if (!m_opt_categories.empty())
        update_settings_digest_bitmaps();
}

bool ObjectDataViewModelNode::SetValue(const wxVariant& variant, unsigned col)
{
    switch (col)
    {
    case colPrint:
        m_printable_icon << variant;
        return true;
    case colName: {
        DataViewBitmapText data;
        data << variant;
        m_bmp = data.GetBitmap();
        m_name = data.GetText();
        return true; }
    case colExtruder: {
        DataViewBitmapText data;
        data << variant;
        m_extruder_bmp = data.GetBitmap();
        m_extruder = data.GetText() == "0" ? _(L("default")) : data.GetText();
        return true; }
    case colEditing:
        m_action_icon << variant;
        return true;
    default:
        printf("MyObjectTreeModel::SetValue: wrong column");
    }
    return false;
}

void ObjectDataViewModelNode::SetIdx(const int& idx)
{
    m_idx = idx;
    // update name if this node is instance
    if (m_type == itInstance)
        m_name = wxString::Format(_(L("Instance %d")), m_idx + 1);
}

// *****************************************************************************
// ----------------------------------------------------------------------------
// ObjectDataViewModel
// ----------------------------------------------------------------------------

static int get_root_idx(ObjectDataViewModelNode *parent_node, const ItemType root_type)
{
    // because of istance_root and layers_root are at the end of the list, so
    // start locking from the end
    for (int root_idx = parent_node->GetChildCount() - 1; root_idx >= 0; root_idx--)
    {
        // if there is SettingsItem or VolumeItem, then RootItems don't exist in current ObjectItem 
        if (parent_node->GetNthChild(root_idx)->GetType() & (itSettings | itVolume))
            break;
        if (parent_node->GetNthChild(root_idx)->GetType() & root_type)
            return root_idx;
    }

    return -1;
}

ObjectDataViewModel::ObjectDataViewModel()
{
    m_bitmap_cache = new Slic3r::GUI::BitmapCache;
}

ObjectDataViewModel::~ObjectDataViewModel()
{
    for (auto object : m_objects)
			delete object;
    delete m_bitmap_cache;
    m_bitmap_cache = nullptr;
}

wxDataViewItem ObjectDataViewModel::Add(const wxString &name, 
                                        const int extruder,
                                        const bool has_errors/* = false*/)
{
    const wxString extruder_str = extruder == 0 ? _(L("default")) : wxString::Format("%d", extruder);
	auto root = new ObjectDataViewModelNode(name, extruder_str);
    // Add error icon if detected auto-repaire
    if (has_errors)
        root->m_bmp = *m_warning_bmp;

    m_objects.push_back(root);
	// notify control
	wxDataViewItem child((void*)root);
	wxDataViewItem parent((void*)NULL);

	ItemAdded(parent, child);
	return child;
}

wxDataViewItem ObjectDataViewModel::AddVolumeChild( const wxDataViewItem &parent_item,
                                                    const wxString &name,
                                                    const Slic3r::ModelVolumeType volume_type,
                                                    const bool has_errors/* = false*/,
                                                    const int extruder/* = 0*/,
                                                    const bool create_frst_child/* = true*/)
{
	ObjectDataViewModelNode *root = (ObjectDataViewModelNode*)parent_item.GetID();
	if (!root) return wxDataViewItem(0);

    wxString extruder_str = extruder == 0 ? _(L("default")) : wxString::Format("%d", extruder);

    // get insertion position according to the existed Layers and/or Instances Items
    int insert_position = get_root_idx(root, itLayerRoot);
    if (insert_position < 0)
        insert_position = get_root_idx(root, itInstanceRoot);

    const bool obj_errors = root->m_bmp.IsOk();

    if (create_frst_child && root->m_volumes_cnt == 0)
    {
        const Slic3r::ModelVolumeType type = Slic3r::ModelVolumeType::MODEL_PART;
        const auto node = new ObjectDataViewModelNode(root, root->m_name, GetVolumeIcon(type, obj_errors), extruder_str, 0);
        node->m_volume_type = type;

        insert_position < 0 ? root->Append(node) : root->Insert(node, insert_position);
		// notify control
		const wxDataViewItem child((void*)node);
		ItemAdded(parent_item, child);

        root->m_volumes_cnt++;
        if (insert_position >= 0) insert_position++;
	}

    const auto node = new ObjectDataViewModelNode(root, name, GetVolumeIcon(volume_type, has_errors), extruder_str, root->m_volumes_cnt);
    insert_position < 0 ? root->Append(node) : root->Insert(node, insert_position);

    // if part with errors is added, but object wasn't marked, then mark it
    if (!obj_errors && has_errors)
        root->SetBitmap(*m_warning_bmp);

	// notify control
    const wxDataViewItem child((void*)node);
    ItemAdded(parent_item, child);
    root->m_volumes_cnt++;

    node->m_volume_type = volume_type;

	return child;
}

wxDataViewItem ObjectDataViewModel::AddSettingsChild(const wxDataViewItem &parent_item)
{
    ObjectDataViewModelNode *root = (ObjectDataViewModelNode*)parent_item.GetID();
    if (!root) return wxDataViewItem(0);

    const auto node = new ObjectDataViewModelNode(root, itSettings);
    root->Insert(node, 0);
    // notify control
    const wxDataViewItem child((void*)node);
    ItemAdded(parent_item, child);
    return child;
}

/* return values:
 * true     => root_node is created and added to the parent_root
 * false    => root node alredy exists
*/
static bool append_root_node(ObjectDataViewModelNode *parent_node, 
                             ObjectDataViewModelNode **root_node, 
                             const ItemType root_type)
{
    const int inst_root_id = get_root_idx(parent_node, root_type);

    *root_node = inst_root_id < 0 ?
                new ObjectDataViewModelNode(parent_node, root_type) :
                parent_node->GetNthChild(inst_root_id);
    
    if (inst_root_id < 0) {
        if ((root_type&itInstanceRoot) ||
            ( (root_type&itLayerRoot) && get_root_idx(parent_node, itInstanceRoot)<0) )
            parent_node->Append(*root_node);
        else if (root_type&itLayerRoot)
            parent_node->Insert(*root_node, static_cast<unsigned int>(get_root_idx(parent_node, itInstanceRoot)));
        return true;
    }

    return false;
}

wxDataViewItem ObjectDataViewModel::AddRoot(const wxDataViewItem &parent_item, ItemType root_type)
{
    ObjectDataViewModelNode *parent_node = (ObjectDataViewModelNode*)parent_item.GetID();
    if (!parent_node) return wxDataViewItem(0);

    // get InstanceRoot node
    ObjectDataViewModelNode *root_node { nullptr };
    const bool appended = append_root_node(parent_node, &root_node, root_type);
    if (!root_node) return wxDataViewItem(0);

    const wxDataViewItem root_item((void*)root_node);

    if (appended)
        ItemAdded(parent_item, root_item);// notify control
    return root_item;
}

wxDataViewItem ObjectDataViewModel::AddInstanceRoot(const wxDataViewItem &parent_item)
{
    return AddRoot(parent_item, itInstanceRoot);
}

wxDataViewItem ObjectDataViewModel::AddInstanceChild(const wxDataViewItem &parent_item, size_t num)
{
    std::vector<bool> print_indicator(num, true);

    // if InstanceRoot item isn't created for this moment
    if (!GetInstanceRootItem(parent_item).IsOk())
        // use object's printable state to first instance
        print_indicator[0] = IsPrintable(parent_item);
    
    return wxDataViewItem((void*)AddInstanceChild(parent_item, print_indicator));
}

wxDataViewItem ObjectDataViewModel::AddInstanceChild(const wxDataViewItem& parent_item,
                                                     const std::vector<bool>& print_indicator)
{
    const wxDataViewItem inst_root_item = AddInstanceRoot(parent_item);
    if (!inst_root_item) return wxDataViewItem(0);

    ObjectDataViewModelNode* inst_root_node = (ObjectDataViewModelNode*)inst_root_item.GetID();

    // Add instance nodes
    ObjectDataViewModelNode *instance_node = nullptr;    
    size_t counter = 0;
    while (counter < print_indicator.size()) {
        instance_node = new ObjectDataViewModelNode(inst_root_node, itInstance);

        instance_node->set_printable_icon(print_indicator[counter] ? piPrintable : piUnprintable);

        inst_root_node->Append(instance_node);
        // notify control
        const wxDataViewItem instance_item((void*)instance_node);
        ItemAdded(inst_root_item, instance_item);
        ++counter;
    }

    // update object_node printable property
    UpdateObjectPrintable(parent_item);

    return wxDataViewItem((void*)instance_node);
}

void ObjectDataViewModel::UpdateObjectPrintable(wxDataViewItem parent_item)
{
    const wxDataViewItem inst_root_item = GetInstanceRootItem(parent_item);
    if (!inst_root_item) 
        return;

    ObjectDataViewModelNode* inst_root_node = (ObjectDataViewModelNode*)inst_root_item.GetID();

    const size_t child_cnt = inst_root_node->GetChildren().Count();
    PrintIndicator obj_pi = piUnprintable;
    for (size_t i=0; i < child_cnt; i++)
        if (inst_root_node->GetNthChild(i)->IsPrintable() & piPrintable) {
            obj_pi = piPrintable;
            break;
        }
    // and set printable state for object_node to piUndef
    ObjectDataViewModelNode* obj_node = (ObjectDataViewModelNode*)parent_item.GetID();
    obj_node->set_printable_icon(obj_pi);
    ItemChanged(parent_item);
}

// update printable property for all instances from object
void ObjectDataViewModel::UpdateInstancesPrintable(wxDataViewItem parent_item)
{
    const wxDataViewItem inst_root_item = GetInstanceRootItem(parent_item);
    if (!inst_root_item) 
        return;

    ObjectDataViewModelNode* obj_node = (ObjectDataViewModelNode*)parent_item.GetID();
    const PrintIndicator obj_pi = obj_node->IsPrintable();

    ObjectDataViewModelNode* inst_root_node = (ObjectDataViewModelNode*)inst_root_item.GetID();
    const size_t child_cnt = inst_root_node->GetChildren().Count();

    for (size_t i=0; i < child_cnt; i++)
    {
        ObjectDataViewModelNode* inst_node = inst_root_node->GetNthChild(i);
        // and set printable state for object_node to piUndef
        inst_node->set_printable_icon(obj_pi);
        ItemChanged(wxDataViewItem((void*)inst_node));
    }
}

bool ObjectDataViewModel::IsPrintable(const wxDataViewItem& item) const
{
    ObjectDataViewModelNode* node = (ObjectDataViewModelNode*)item.GetID();
    if (!node)
        return false;

    return node->IsPrintable() == piPrintable;
}

wxDataViewItem ObjectDataViewModel::AddLayersRoot(const wxDataViewItem &parent_item)
{
    return AddRoot(parent_item, itLayerRoot);
}

wxDataViewItem ObjectDataViewModel::AddLayersChild(const wxDataViewItem &parent_item, 
                                                   const t_layer_height_range& layer_range,
                                                   const int extruder/* = 0*/, 
                                                   const int index /* = -1*/)
{
    ObjectDataViewModelNode *parent_node = (ObjectDataViewModelNode*)parent_item.GetID();
    if (!parent_node) return wxDataViewItem(0);

    wxString extruder_str = extruder == 0 ? _(L("default")) : wxString::Format("%d", extruder);

    // get LayerRoot node
    ObjectDataViewModelNode *layer_root_node;
    wxDataViewItem layer_root_item;

    if (parent_node->GetType() & itLayerRoot) {
        layer_root_node = parent_node;
        layer_root_item = parent_item;
    }
    else {
        const int root_idx = get_root_idx(parent_node, itLayerRoot);
        if (root_idx < 0) return wxDataViewItem(0);
        layer_root_node = parent_node->GetNthChild(root_idx);
        layer_root_item = wxDataViewItem((void*)layer_root_node);
    }

    // Add layer node
    ObjectDataViewModelNode *layer_node = new ObjectDataViewModelNode(layer_root_node, layer_range, index, extruder_str);
    if (index < 0)
        layer_root_node->Append(layer_node);
    else
        layer_root_node->Insert(layer_node, index);

    // notify control
    const wxDataViewItem layer_item((void*)layer_node);
    ItemAdded(layer_root_item, layer_item);

    return layer_item;
}

wxDataViewItem ObjectDataViewModel::Delete(const wxDataViewItem &item)
{
	auto ret_item = wxDataViewItem(0);
	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
	if (!node)      // happens if item.IsOk()==false
		return ret_item;

	auto node_parent = node->GetParent();
	wxDataViewItem parent(node_parent);

	// first remove the node from the parent's array of children;
	// NOTE: MyObjectTreeModelNodePtrArray is only an array of _pointers_
	//       thus removing the node from it doesn't result in freeing it
	if (node_parent) {
        if (node->m_type & (itInstanceRoot|itLayerRoot))
        {
            // node can be deleted by the Delete, let's check its type while we safely can
            bool is_instance_root = (node->m_type & itInstanceRoot);

            for (int i = int(node->GetChildCount() - 1); i >= (is_instance_root ? 1 : 0); i--)
                Delete(wxDataViewItem(node->GetNthChild(i)));

            return parent;
        }

		auto id = node_parent->GetChildren().Index(node);
        auto idx = node->GetIdx();


        if (node->m_type & (itVolume|itLayer)) {
            node_parent->m_volumes_cnt--;
            DeleteSettings(item);
        }
		node_parent->GetChildren().Remove(node);

		if (id > 0) { 
			if(id == node_parent->GetChildCount()) id--;
			ret_item = wxDataViewItem(node_parent->GetChildren().Item(id));
		}

		//update idx value for remaining child-nodes
		auto children = node_parent->GetChildren();
        for (size_t i = 0; i < node_parent->GetChildCount() && idx>=0; i++)
		{
            auto cur_idx = children[i]->GetIdx();
			if (cur_idx > idx)
				children[i]->SetIdx(cur_idx-1);
		}

        // if there is last instance item, delete both of it and instance root item
        if (node_parent->GetChildCount() == 1 && node_parent->GetNthChild(0)->m_type == itInstance)
        {
            delete node;
            ItemDeleted(parent, item);

            ObjectDataViewModelNode *last_instance_node = node_parent->GetNthChild(0);
            PrintIndicator last_instance_printable = last_instance_node->IsPrintable();
            node_parent->GetChildren().Remove(last_instance_node);
            delete last_instance_node;
            ItemDeleted(parent, wxDataViewItem(last_instance_node));

            ObjectDataViewModelNode *obj_node = node_parent->GetParent();
            obj_node->set_printable_icon(last_instance_printable);
            obj_node->GetChildren().Remove(node_parent);
            delete node_parent;
            ret_item = wxDataViewItem(obj_node);

#ifndef __WXGTK__
            if (obj_node->GetChildCount() == 0)
                obj_node->m_container = false;
#endif //__WXGTK__
            ItemDeleted(ret_item, wxDataViewItem(node_parent));
            return ret_item;
        }

        if (node->m_type & itInstance)
            UpdateObjectPrintable(wxDataViewItem(node_parent->GetParent()));

        // if there was last layer item, delete this one and layers root item
        if (node_parent->GetChildCount() == 0 && node_parent->m_type == itLayerRoot)
        {
            ObjectDataViewModelNode *obj_node = node_parent->GetParent();
            obj_node->GetChildren().Remove(node_parent);
            delete node_parent;
            ret_item = wxDataViewItem(obj_node);

#ifndef __WXGTK__
            if (obj_node->GetChildCount() == 0)
                obj_node->m_container = false;
#endif //__WXGTK__
            ItemDeleted(ret_item, wxDataViewItem(node_parent));
            return ret_item;
        }

        // if there is last volume item after deleting, delete this last volume too
        if (node_parent->GetChildCount() <= 3) // 3??? #ys_FIXME
        {
            int vol_cnt = 0;
            int vol_idx = 0;
            for (size_t i = 0; i < node_parent->GetChildCount(); ++i) {
                if (node_parent->GetNthChild(i)->GetType() == itVolume) {
                    vol_idx = i;
                    vol_cnt++;
                }
                if (vol_cnt > 1)
                    break;
            }

            if (vol_cnt == 1) {
                delete node;
                ItemDeleted(parent, item);

                ObjectDataViewModelNode *last_child_node = node_parent->GetNthChild(vol_idx);
                DeleteSettings(wxDataViewItem(last_child_node));
                node_parent->GetChildren().Remove(last_child_node);
                node_parent->m_volumes_cnt = 0;
                delete last_child_node;

#ifndef __WXGTK__
                if (node_parent->GetChildCount() == 0)
                    node_parent->m_container = false;
#endif //__WXGTK__
                ItemDeleted(parent, wxDataViewItem(last_child_node));

                wxCommandEvent event(wxCUSTOMEVT_LAST_VOLUME_IS_DELETED);
                auto it = find(m_objects.begin(), m_objects.end(), node_parent);
                event.SetInt(it == m_objects.end() ? -1 : it - m_objects.begin());
                wxPostEvent(m_ctrl, event);

                ret_item = parent;

                return ret_item;
            }
        }
	}
	else
	{
		auto it = find(m_objects.begin(), m_objects.end(), node);
        size_t id = it - m_objects.begin();
		if (it != m_objects.end())
		{
            // Delete all sub-items
            int i = m_objects[id]->GetChildCount() - 1;
            while (i >= 0) {
                Delete(wxDataViewItem(m_objects[id]->GetNthChild(i)));
                i = m_objects[id]->GetChildCount() - 1;
            }
			m_objects.erase(it);
        }
		if (id > 0) { 
			if(id == m_objects.size()) id--;
			ret_item = wxDataViewItem(m_objects[id]);
		}
	}
	// free the node
	delete node;

	// set m_containet to FALSE if parent has no child
	if (node_parent) {
#ifndef __WXGTK__
        if (node_parent->GetChildCount() == 0)
            node_parent->m_container = false;
#endif //__WXGTK__
		ret_item = parent;
	}

	// notify control
	ItemDeleted(parent, item);
	return ret_item;
}

wxDataViewItem ObjectDataViewModel::DeleteLastInstance(const wxDataViewItem &parent_item, size_t num)
{
    auto ret_item = wxDataViewItem(0);
    ObjectDataViewModelNode *parent_node = (ObjectDataViewModelNode*)parent_item.GetID();
    if (!parent_node) return ret_item;

    const int inst_root_id = get_root_idx(parent_node, itInstanceRoot);
    if (inst_root_id < 0) return ret_item;

    wxDataViewItemArray items;
    ObjectDataViewModelNode *inst_root_node = parent_node->GetNthChild(inst_root_id);
    const wxDataViewItem inst_root_item((void*)inst_root_node);

    const int inst_cnt = inst_root_node->GetChildCount();
    const bool delete_inst_root_item = inst_cnt - num < 2 ? true : false;

    PrintIndicator last_inst_printable = piUndef;

    int stop = delete_inst_root_item ? 0 : inst_cnt - num;
    for (int i = inst_cnt - 1; i >= stop;--i) {
        ObjectDataViewModelNode *last_instance_node = inst_root_node->GetNthChild(i);
        if (i==0) last_inst_printable = last_instance_node->IsPrintable();
        inst_root_node->GetChildren().Remove(last_instance_node);
        delete last_instance_node;
        ItemDeleted(inst_root_item, wxDataViewItem(last_instance_node));
    }

    if (delete_inst_root_item) {
        ret_item = parent_item;
        parent_node->GetChildren().Remove(inst_root_node);
        parent_node->set_printable_icon(last_inst_printable);
        ItemDeleted(parent_item, inst_root_item);
        ItemChanged(parent_item);
#ifndef __WXGTK__
        if (parent_node->GetChildCount() == 0)
            parent_node->m_container = false;
#endif //__WXGTK__
    }

    // update object_node printable property
    UpdateObjectPrintable(parent_item);

    return ret_item;
}

void ObjectDataViewModel::DeleteAll()
{
	while (!m_objects.empty())
	{
		auto object = m_objects.back();
// 		object->RemoveAllChildren();
		Delete(wxDataViewItem(object));	
	}
}

void ObjectDataViewModel::DeleteChildren(wxDataViewItem& parent)
{
    ObjectDataViewModelNode *root = (ObjectDataViewModelNode*)parent.GetID();
    if (!root)      // happens if item.IsOk()==false
        return;

    // first remove the node from the parent's array of children;
    // NOTE: MyObjectTreeModelNodePtrArray is only an array of _pointers_
    //       thus removing the node from it doesn't result in freeing it
    auto& children = root->GetChildren();
    for (int id = root->GetChildCount() - 1; id >= 0; --id)
    {
        auto node = children[id];
        auto item = wxDataViewItem(node);
        children.RemoveAt(id);

        if (node->m_type == itVolume)
            root->m_volumes_cnt--;

        // free the node
        delete node;

        // notify control
        ItemDeleted(parent, item);
    }

    // set m_containet to FALSE if parent has no child
#ifndef __WXGTK__
        root->m_container = false;
#endif //__WXGTK__
}

void ObjectDataViewModel::DeleteVolumeChildren(wxDataViewItem& parent)
{
    ObjectDataViewModelNode *root = (ObjectDataViewModelNode*)parent.GetID();
    if (!root)      // happens if item.IsOk()==false
        return;

    // first remove the node from the parent's array of children;
    // NOTE: MyObjectTreeModelNodePtrArray is only an array of _pointers_
    //       thus removing the node from it doesn't result in freeing it
    auto& children = root->GetChildren();
    for (int id = root->GetChildCount() - 1; id >= 0; --id)
    {
        auto node = children[id];
        if (node->m_type != itVolume)
            continue;

        auto item = wxDataViewItem(node);
        DeleteSettings(item);
        children.RemoveAt(id);

        // free the node
        delete node;

        // notify control
        ItemDeleted(parent, item);
    }
    root->m_volumes_cnt = 0;

    // set m_containet to FALSE if parent has no child
#ifndef __WXGTK__
    root->m_container = false;
#endif //__WXGTK__
}

void ObjectDataViewModel::DeleteSettings(const wxDataViewItem& parent)
{
    ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)parent.GetID();
    if (!node) return;

    // if volume has a "settings"item, than delete it before volume deleting
    if (node->GetChildCount() > 0 && node->GetNthChild(0)->GetType() == itSettings) {
        auto settings_node = node->GetNthChild(0);
        auto settings_item = wxDataViewItem(settings_node);
        node->GetChildren().RemoveAt(0);
        delete settings_node;
        ItemDeleted(parent, settings_item);
    }
}

wxDataViewItem ObjectDataViewModel::GetItemById(int obj_idx)
{
    if (size_t(obj_idx) >= m_objects.size())
	{
		printf("Error! Out of objects range.\n");
		return wxDataViewItem(0);
	}
	return wxDataViewItem(m_objects[obj_idx]);
}


wxDataViewItem ObjectDataViewModel::GetItemByVolumeId(int obj_idx, int volume_idx)
{
    if (size_t(obj_idx) >= m_objects.size()) {
		printf("Error! Out of objects range.\n");
		return wxDataViewItem(0);
	}

    auto parent = m_objects[obj_idx];
    if (parent->GetChildCount() == 0 ||
        (parent->GetChildCount() == 1 && parent->GetNthChild(0)->GetType() & itSettings )) {
        if (volume_idx == 0)
            return GetItemById(obj_idx);

        printf("Error! Object has no one volume.\n");
        return wxDataViewItem(0);
    }

    for (size_t i = 0; i < parent->GetChildCount(); i++)
        if (parent->GetNthChild(i)->m_idx == volume_idx && parent->GetNthChild(i)->GetType() & itVolume)
            return wxDataViewItem(parent->GetNthChild(i));

    return wxDataViewItem(0);
}

wxDataViewItem ObjectDataViewModel::GetItemById(const int obj_idx, const int sub_obj_idx, const ItemType parent_type)
{
    if (size_t(obj_idx) >= m_objects.size()) {
        printf("Error! Out of objects range.\n");
        return wxDataViewItem(0);
    }

    auto item = GetItemByType(wxDataViewItem(m_objects[obj_idx]), parent_type);
    if (!item)
        return wxDataViewItem(0);

    auto parent = (ObjectDataViewModelNode*)item.GetID();
    for (size_t i = 0; i < parent->GetChildCount(); i++)
        if (parent->GetNthChild(i)->m_idx == sub_obj_idx)
            return wxDataViewItem(parent->GetNthChild(i));

    return wxDataViewItem(0);
}

wxDataViewItem ObjectDataViewModel::GetItemByInstanceId(int obj_idx, int inst_idx)
{
    return GetItemById(obj_idx, inst_idx, itInstanceRoot);
}

wxDataViewItem ObjectDataViewModel::GetItemByLayerId(int obj_idx, int layer_idx)
{
    return GetItemById(obj_idx, layer_idx, itLayerRoot);
}

wxDataViewItem ObjectDataViewModel::GetItemByLayerRange(const int obj_idx, const t_layer_height_range& layer_range)
{
    if (size_t(obj_idx) >= m_objects.size()) {
        printf("Error! Out of objects range.\n");
        return wxDataViewItem(0);
    }

    auto item = GetItemByType(wxDataViewItem(m_objects[obj_idx]), itLayerRoot);
    if (!item)
        return wxDataViewItem(0);

    auto parent = (ObjectDataViewModelNode*)item.GetID();
    for (size_t i = 0; i < parent->GetChildCount(); i++)
        if (parent->GetNthChild(i)->m_layer_range == layer_range)
            return wxDataViewItem(parent->GetNthChild(i));

    return wxDataViewItem(0);
}

int  ObjectDataViewModel::GetItemIdByLayerRange(const int obj_idx, const t_layer_height_range& layer_range)
{
    wxDataViewItem item = GetItemByLayerRange(obj_idx, layer_range);
    if (!item)
        return -1;

    return GetLayerIdByItem(item);
}

int ObjectDataViewModel::GetIdByItem(const wxDataViewItem& item) const
{
	if(!item.IsOk())
        return -1;

	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
	auto it = find(m_objects.begin(), m_objects.end(), node);
	if (it == m_objects.end())
		return -1;

	return it - m_objects.begin();
}

int ObjectDataViewModel::GetIdByItemAndType(const wxDataViewItem& item, const ItemType type) const
{
	wxASSERT(item.IsOk());

	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
	if (!node || node->m_type != type)
		return -1;
	return node->GetIdx();
}

int ObjectDataViewModel::GetObjectIdByItem(const wxDataViewItem& item) const
{
    return GetIdByItem(GetTopParent(item));
}

int ObjectDataViewModel::GetVolumeIdByItem(const wxDataViewItem& item) const
{
    return GetIdByItemAndType(item, itVolume);
}

int ObjectDataViewModel::GetInstanceIdByItem(const wxDataViewItem& item) const 
{
    return GetIdByItemAndType(item, itInstance);
}

int ObjectDataViewModel::GetLayerIdByItem(const wxDataViewItem& item) const 
{
    return GetIdByItemAndType(item, itLayer);
}

t_layer_height_range ObjectDataViewModel::GetLayerRangeByItem(const wxDataViewItem& item) const
{
    wxASSERT(item.IsOk());

    ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
    if (!node || node->m_type != itLayer)
        return { 0.0f, 0.0f };
    return node->GetLayerRange();
}

bool ObjectDataViewModel::UpdateColumValues(unsigned col)
{
    switch (col)
    {
    case colPrint:
    case colName:
    case colEditing:
        return true;
    case colExtruder:
    {
        wxDataViewItemArray items;
        GetAllChildren(wxDataViewItem(nullptr), items);

        if (items.IsEmpty()) return false;

        for (auto item : items)
            UpdateExtruderBitmap(item);

        return true;
    }
    default:
        printf("MyObjectTreeModel::SetValue: wrong column");
    }
    return false;
}


void ObjectDataViewModel::UpdateExtruderBitmap(wxDataViewItem item)
{
    wxString extruder = GetExtruder(item);
    if (extruder.IsEmpty())
        return;

    // set extruder bitmap
    int extruder_idx = atoi(extruder.c_str());
    if (extruder_idx > 0) --extruder_idx;

    const DataViewBitmapText extruder_val(extruder, get_extruder_color_icon(extruder_idx));

    wxVariant value;
    value << extruder_val;

    SetValue(value, item, colExtruder);
}

void ObjectDataViewModel::GetItemInfo(const wxDataViewItem& item, ItemType& type, int& obj_idx, int& idx)
{
    wxASSERT(item.IsOk());
    type = itUndef;

    ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
    if (!node || 
        node->GetIdx() <-1 || 
        ( node->GetIdx() == -1 && 
         !(node->GetType() & (itObject | itSettings | itInstanceRoot | itLayerRoot/* | itLayer*/))
        )
       )
        return;

    idx = node->GetIdx();
    type = node->GetType();

    ObjectDataViewModelNode *parent_node = node->GetParent();
    if (!parent_node) return;

    // get top parent (Object) node
    while (parent_node->m_type != itObject)
        parent_node = parent_node->GetParent();

    auto it = find(m_objects.begin(), m_objects.end(), parent_node);
    if (it != m_objects.end())
        obj_idx = it - m_objects.begin();
    else
        type = itUndef;
}

int ObjectDataViewModel::GetRowByItem(const wxDataViewItem& item) const
{
    if (m_objects.empty())
        return -1;

    int row_num = 0;
    
    for (size_t i = 0; i < m_objects.size(); i++)
    {
        row_num++;
        if (item == wxDataViewItem(m_objects[i]))
            return row_num;

        for (size_t j = 0; j < m_objects[i]->GetChildCount(); j++)
        {
            row_num++;
            ObjectDataViewModelNode* cur_node = m_objects[i]->GetNthChild(j);
            if (item == wxDataViewItem(cur_node))
                return row_num;

            if (cur_node->m_type == itVolume && cur_node->GetChildCount() == 1)
                row_num++;
            if (cur_node->m_type == itInstanceRoot)
            {
                row_num++;
                for (size_t t = 0; t < cur_node->GetChildCount(); t++)
                {
                    row_num++;
                    if (item == wxDataViewItem(cur_node->GetNthChild(t)))
                        return row_num;
                }
            }
        }        
    }

    return -1;
}

bool ObjectDataViewModel::InvalidItem(const wxDataViewItem& item)
{
    if (!item)
        return true;

    ObjectDataViewModelNode* node = (ObjectDataViewModelNode*)item.GetID();
    if (!node || node->invalid()) 
        return true;

    return false;
}

wxString ObjectDataViewModel::GetName(const wxDataViewItem &item) const
{
	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
	if (!node)      // happens if item.IsOk()==false
		return wxEmptyString;

	return node->m_name;
}

wxBitmap& ObjectDataViewModel::GetBitmap(const wxDataViewItem &item) const
{
    ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
    return node->m_bmp;
}

wxString ObjectDataViewModel::GetExtruder(const wxDataViewItem& item) const
{
	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
	if (!node)      // happens if item.IsOk()==false
		return wxEmptyString;

	return node->m_extruder;
}

int ObjectDataViewModel::GetExtruderNumber(const wxDataViewItem& item) const
{
	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
	if (!node)      // happens if item.IsOk()==false
		return 0;

	return atoi(node->m_extruder.c_str());
}

void ObjectDataViewModel::GetValue(wxVariant &variant, const wxDataViewItem &item, unsigned int col) const
{
	wxASSERT(item.IsOk());

	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
	switch (col)
	{
	case colPrint:
		variant << node->m_printable_icon;
		break;
	case colName:
        variant << DataViewBitmapText(node->m_name, node->m_bmp);
		break;
	case colExtruder:
		variant << DataViewBitmapText(node->m_extruder, node->m_extruder_bmp);
		break;
	case colEditing:
		variant << node->m_action_icon;
		break;
	default:
		;
	}
}

bool ObjectDataViewModel::SetValue(const wxVariant &variant, const wxDataViewItem &item, unsigned int col)
{
	wxASSERT(item.IsOk());

	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
	return node->SetValue(variant, col);
}

bool ObjectDataViewModel::SetValue(const wxVariant &variant, const int item_idx, unsigned int col)
{
    if (size_t(item_idx) >= m_objects.size())
		return false;

	return m_objects[item_idx]->SetValue(variant, col);
}

void ObjectDataViewModel::SetExtruder(const wxString& extruder, wxDataViewItem item)
{
    DataViewBitmapText extruder_val;
    extruder_val.SetText(extruder);

    // set extruder bitmap
    int extruder_idx = atoi(extruder.c_str());
    if (extruder_idx > 0) --extruder_idx;
    extruder_val.SetBitmap(get_extruder_color_icon(extruder_idx));

    wxVariant value;
    value << extruder_val;
    
    SetValue(value, item, colExtruder);
}

wxDataViewItem ObjectDataViewModel::ReorganizeChildren( const int current_volume_id, 
                                                        const int new_volume_id, 
                                                        const wxDataViewItem &parent)
{
    auto ret_item = wxDataViewItem(0);
    if (current_volume_id == new_volume_id)
        return ret_item;
    wxASSERT(parent.IsOk());
    ObjectDataViewModelNode *node_parent = (ObjectDataViewModelNode*)parent.GetID();
    if (!node_parent)      // happens if item.IsOk()==false
        return ret_item;

    const size_t shift = node_parent->GetChildren().Item(0)->m_type == itSettings ? 1 : 0;

    ObjectDataViewModelNode *deleted_node = node_parent->GetNthChild(current_volume_id+shift);
    node_parent->GetChildren().Remove(deleted_node);
    ItemDeleted(parent, wxDataViewItem(deleted_node));
    node_parent->Insert(deleted_node, new_volume_id+shift);
    ItemAdded(parent, wxDataViewItem(deleted_node));
    const auto settings_item = GetSettingsItem(wxDataViewItem(deleted_node));
    if (settings_item)
        ItemAdded(wxDataViewItem(deleted_node), settings_item);

    //update volume_id value for child-nodes
    auto children = node_parent->GetChildren();
    int id_frst = current_volume_id < new_volume_id ? current_volume_id : new_volume_id;
    int id_last = current_volume_id > new_volume_id ? current_volume_id : new_volume_id;
    for (int id = id_frst; id <= id_last; ++id)
        children[id+shift]->SetIdx(id);

    return wxDataViewItem(node_parent->GetNthChild(new_volume_id+shift));
}

bool ObjectDataViewModel::IsEnabled(const wxDataViewItem &item, unsigned int col) const
{
    wxASSERT(item.IsOk());
    ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();

    // disable extruder selection for the non "itObject|itVolume" item
    return !(col == colExtruder && node->m_extruder.IsEmpty());
}

wxDataViewItem ObjectDataViewModel::GetParent(const wxDataViewItem &item) const
{
	// the invisible root node has no parent
	if (!item.IsOk())
		return wxDataViewItem(0);

	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
	assert(node != nullptr && node->valid());

	// objects nodes has no parent too
    if (node->m_type == itObject)
		return wxDataViewItem(0);

	return wxDataViewItem((void*)node->GetParent());
}

wxDataViewItem ObjectDataViewModel::GetTopParent(const wxDataViewItem &item) const
{
	// the invisible root node has no parent
	if (!item.IsOk())
		return wxDataViewItem(0);

	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
    if (node->m_type == itObject)
        return item;

    ObjectDataViewModelNode *parent_node = node->GetParent();
    while (parent_node->m_type != itObject)
        parent_node = parent_node->GetParent();

    return wxDataViewItem((void*)parent_node);
}

bool ObjectDataViewModel::IsContainer(const wxDataViewItem &item) const
{
	// the invisible root node can have children
	if (!item.IsOk())
		return true;

	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
	return node->IsContainer();
}

unsigned int ObjectDataViewModel::GetChildren(const wxDataViewItem &parent, wxDataViewItemArray &array) const
{
	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)parent.GetID();
	if (!node)
	{
		for (auto object : m_objects)
			array.Add(wxDataViewItem((void*)object));
		return m_objects.size();
	}

	if (node->GetChildCount() == 0)
	{
		return 0;
	}

	unsigned int count = node->GetChildren().GetCount();
	for (unsigned int pos = 0; pos < count; pos++)
	{
		ObjectDataViewModelNode *child = node->GetChildren().Item(pos);
		array.Add(wxDataViewItem((void*)child));
	}

	return count;
}

void ObjectDataViewModel::GetAllChildren(const wxDataViewItem &parent, wxDataViewItemArray &array) const
{
	ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)parent.GetID();
	if (!node) {
		for (auto object : m_objects)
			array.Add(wxDataViewItem((void*)object));
	}
	else if (node->GetChildCount() == 0)
		return;
    else {
        const size_t count = node->GetChildren().GetCount();
        for (size_t pos = 0; pos < count; pos++) {
            ObjectDataViewModelNode *child = node->GetChildren().Item(pos);
            array.Add(wxDataViewItem((void*)child));
        }
    }

    wxDataViewItemArray new_array = array;
    for (const auto item : new_array)
    {
        wxDataViewItemArray children;
        GetAllChildren(item, children);
        WX_APPEND_ARRAY(array, children);
    }
}

ItemType ObjectDataViewModel::GetItemType(const wxDataViewItem &item) const 
{
    if (!item.IsOk())
        return itUndef;
    ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
    return node->m_type < 0 ? itUndef : node->m_type;
}

wxDataViewItem ObjectDataViewModel::GetItemByType(const wxDataViewItem &parent_item, ItemType type) const 
{
    if (!parent_item.IsOk())
        return wxDataViewItem(0);

    ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)parent_item.GetID();
    if (node->GetChildCount() == 0)
        return wxDataViewItem(0);

    for (size_t i = 0; i < node->GetChildCount(); i++) {
        if (node->GetNthChild(i)->m_type == type)
            return wxDataViewItem((void*)node->GetNthChild(i));
    }

    return wxDataViewItem(0);
}

wxDataViewItem ObjectDataViewModel::GetSettingsItem(const wxDataViewItem &item) const
{
    return GetItemByType(item, itSettings);
}

wxDataViewItem ObjectDataViewModel::GetInstanceRootItem(const wxDataViewItem &item) const
{
    return GetItemByType(item, itInstanceRoot);
}

wxDataViewItem ObjectDataViewModel::GetLayerRootItem(const wxDataViewItem &item) const
{
    return GetItemByType(item, itLayerRoot);
}

bool ObjectDataViewModel::IsSettingsItem(const wxDataViewItem &item) const
{
    if (!item.IsOk())
        return false;
    ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
    return node->m_type == itSettings;
}

void ObjectDataViewModel::UpdateSettingsDigest(const wxDataViewItem &item, 
                                                    const std::vector<std::string>& categories)
{
    if (!item.IsOk()) return;
    ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
    if (!node->update_settings_digest(categories))
        return;
    ItemChanged(item);
}

void ObjectDataViewModel::SetVolumeType(const wxDataViewItem &item, const Slic3r::ModelVolumeType type)
{
    if (!item.IsOk() || GetItemType(item) != itVolume) 
        return;

    ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
    node->SetBitmap(*m_volume_bmps[int(type)]);
    ItemChanged(item);
}

wxDataViewItem ObjectDataViewModel::SetPrintableState(
    PrintIndicator  printable,
    int             obj_idx,
    int             subobj_idx /* = -1*/,
    ItemType        subobj_type/* = itInstance*/)
{
    wxDataViewItem item = wxDataViewItem(0);
    if (subobj_idx < 0)
        item = GetItemById(obj_idx);
    else
        item =  subobj_type&itInstance ? GetItemByInstanceId(obj_idx, subobj_idx) :
                GetItemByVolumeId(obj_idx, subobj_idx);

    ObjectDataViewModelNode* node = (ObjectDataViewModelNode*)item.GetID();
    if (!node)
        return wxDataViewItem(0);
    node->set_printable_icon(printable);
    ItemChanged(item);

    if (subobj_idx >= 0)
        UpdateObjectPrintable(GetItemById(obj_idx));

    return item;
}

wxDataViewItem ObjectDataViewModel::SetObjectPrintableState(
    PrintIndicator  printable,
    wxDataViewItem  obj_item)
{
    ObjectDataViewModelNode* node = (ObjectDataViewModelNode*)obj_item.GetID();
    if (!node)
        return wxDataViewItem(0);
    node->set_printable_icon(printable);
    ItemChanged(obj_item);

    UpdateInstancesPrintable(obj_item);

    return obj_item;
}

void ObjectDataViewModel::Rescale()
{
    wxDataViewItemArray all_items;
    GetAllChildren(wxDataViewItem(0), all_items);

    for (wxDataViewItem item : all_items)
    {
        if (!item.IsOk())
            continue;

        ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();
        node->msw_rescale();

        switch (node->m_type)
        {
        case itObject:
            if (node->m_bmp.IsOk()) node->m_bmp = *m_warning_bmp;
            break;
        case itVolume:
            node->m_bmp = GetVolumeIcon(node->m_volume_type, node->m_bmp.GetWidth() != node->m_bmp.GetHeight());
            break;
        case itLayerRoot:
            node->m_bmp = create_scaled_bitmap(nullptr, LAYER_ROOT_ICON);    // FIXME: pass window ptr
            break;
        case itLayer:
            node->m_bmp = create_scaled_bitmap(nullptr, LAYER_ICON);    // FIXME: pass window ptr
            break;
        default: break;
        }

        ItemChanged(item);
    }
}

wxBitmap ObjectDataViewModel::GetVolumeIcon(const Slic3r::ModelVolumeType vol_type, const bool is_marked/* = false*/)
{
    if (!is_marked)
        return *m_volume_bmps[static_cast<int>(vol_type)];

    std::string scaled_bitmap_name = "warning" + std::to_string(static_cast<int>(vol_type));
    scaled_bitmap_name += "-em" + std::to_string(Slic3r::GUI::wxGetApp().em_unit());

    wxBitmap *bmp = m_bitmap_cache->find(scaled_bitmap_name);
    if (bmp == nullptr) {
        std::vector<wxBitmap> bmps;

        bmps.emplace_back(*m_warning_bmp);
        bmps.emplace_back(*m_volume_bmps[static_cast<int>(vol_type)]);

        bmp = m_bitmap_cache->insert(scaled_bitmap_name, bmps);
    }

    return *bmp;
}

void ObjectDataViewModel::DeleteWarningIcon(const wxDataViewItem& item, const bool unmark_object/* = false*/)
{
    if (!item.IsOk())
        return;

    ObjectDataViewModelNode *node = (ObjectDataViewModelNode*)item.GetID();

    if (!node->GetBitmap().IsOk() || !(node->GetType() & (itVolume | itObject)))
        return;

    if (node->GetType() & itVolume) {
        node->SetBitmap(*m_volume_bmps[static_cast<int>(node->volume_type())]);
        return;
    }

    node->SetBitmap(wxNullBitmap);
    if (unmark_object)
    {
        wxDataViewItemArray children;
        GetChildren(item, children);
        for (const wxDataViewItem& child : children)
            DeleteWarningIcon(child);
    }
}

//-----------------------------------------------------------------------------
// DataViewBitmapText
//-----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(DataViewBitmapText, wxObject)

IMPLEMENT_VARIANT_OBJECT(DataViewBitmapText)

// ---------------------------------------------------------
// BitmapTextRenderer
// ---------------------------------------------------------

#if ENABLE_NONCUSTOM_DATA_VIEW_RENDERING
BitmapTextRenderer::BitmapTextRenderer(wxDataViewCellMode mode /*= wxDATAVIEW_CELL_EDITABLE*/, 
                                                 int align /*= wxDVR_DEFAULT_ALIGNMENT*/): 
wxDataViewRenderer(wxT("PrusaDataViewBitmapText"), mode, align)
{
    SetMode(mode);
    SetAlignment(align);
}
#endif // ENABLE_NONCUSTOM_DATA_VIEW_RENDERING

bool BitmapTextRenderer::SetValue(const wxVariant &value)
{
    m_value << value;
    return true;
}

bool BitmapTextRenderer::GetValue(wxVariant& WXUNUSED(value)) const
{
    return false;
}

#if ENABLE_NONCUSTOM_DATA_VIEW_RENDERING && wxUSE_ACCESSIBILITY
wxString BitmapTextRenderer::GetAccessibleDescription() const
{
    return m_value.GetText();
}
#endif // wxUSE_ACCESSIBILITY && ENABLE_NONCUSTOM_DATA_VIEW_RENDERING

bool BitmapTextRenderer::Render(wxRect rect, wxDC *dc, int state)
{
    int xoffset = 0;

    const wxBitmap& icon = m_value.GetBitmap();
    if (icon.IsOk())
    {
        dc->DrawBitmap(icon, rect.x, rect.y + (rect.height - icon.GetHeight()) / 2);
        xoffset = icon.GetWidth() + 4;
    }

    RenderText(m_value.GetText(), xoffset, rect, dc, state);

    return true;
}

wxSize BitmapTextRenderer::GetSize() const
{
    if (!m_value.GetText().empty())
    {
        wxSize size = GetTextExtent(m_value.GetText());

        if (m_value.GetBitmap().IsOk())
            size.x += m_value.GetBitmap().GetWidth() + 4;
        return size;
    }
    return wxSize(80, 20);
}


wxWindow* BitmapTextRenderer::CreateEditorCtrl(wxWindow* parent, wxRect labelRect, const wxVariant& value)
{
    wxDataViewCtrl* const dv_ctrl = GetOwner()->GetOwner();
    ObjectDataViewModel* const model = dynamic_cast<ObjectDataViewModel*>(dv_ctrl->GetModel());

    if ( !(model->GetItemType(dv_ctrl->GetSelection()) & (itVolume | itObject)) )
        return nullptr;

    DataViewBitmapText data;
    data << value;

    m_was_unusable_symbol = false;

    wxPoint position = labelRect.GetPosition();
    if (data.GetBitmap().IsOk()) {
        const int bmp_width = data.GetBitmap().GetWidth();
        position.x += bmp_width;
        labelRect.SetWidth(labelRect.GetWidth() - bmp_width);
    }

    wxTextCtrl* text_editor = new wxTextCtrl(parent, wxID_ANY, data.GetText(),
                                             position, labelRect.GetSize(), wxTE_PROCESS_ENTER);
    text_editor->SetInsertionPointEnd();
    text_editor->SelectAll();

    return text_editor;
}

bool BitmapTextRenderer::GetValueFromEditorCtrl(wxWindow* ctrl, wxVariant& value)
{
    wxTextCtrl* text_editor = wxDynamicCast(ctrl, wxTextCtrl);
    if (!text_editor || text_editor->GetValue().IsEmpty())
        return false;

    std::string chosen_name = Slic3r::normalize_utf8_nfc(text_editor->GetValue().ToUTF8());
    const char* unusable_symbols = "<>:/\\|?*\"";
    for (size_t i = 0; i < std::strlen(unusable_symbols); i++) {
        if (chosen_name.find_first_of(unusable_symbols[i]) != std::string::npos) {
            m_was_unusable_symbol = true;
            return false;
        }
    }

    // The icon can't be edited so get its old value and reuse it.
    wxVariant valueOld;
    GetView()->GetModel()->GetValue(valueOld, m_item, colName); 
    
    DataViewBitmapText bmpText;
    bmpText << valueOld;

    // But replace the text with the value entered by user.
    bmpText.SetText(text_editor->GetValue());

    value << bmpText;
    return true;
}

// ----------------------------------------------------------------------------
// BitmapChoiceRenderer
// ----------------------------------------------------------------------------

bool BitmapChoiceRenderer::SetValue(const wxVariant& value)
{
    m_value << value;
    return true;
}

bool BitmapChoiceRenderer::GetValue(wxVariant& value) const 
{
    value << m_value;
    return true;
}

bool BitmapChoiceRenderer::Render(wxRect rect, wxDC* dc, int state)
{
    int xoffset = 0;

    const wxBitmap& icon = m_value.GetBitmap();
    if (icon.IsOk())
    {
        dc->DrawBitmap(icon, rect.x, rect.y + (rect.height - icon.GetHeight()) / 2);
        xoffset = icon.GetWidth() + 4;
    }

    if (rect.height==0)
        rect.height= icon.GetHeight();
    RenderText(m_value.GetText(), xoffset, rect, dc, state);

    return true;
}

wxSize BitmapChoiceRenderer::GetSize() const
{
    wxSize sz = GetTextExtent(m_value.GetText());

    if (m_value.GetBitmap().IsOk())
        sz.x += m_value.GetBitmap().GetWidth() + 4;

    return sz;
}


wxWindow* BitmapChoiceRenderer::CreateEditorCtrl(wxWindow* parent, wxRect labelRect, const wxVariant& value)
{
    wxDataViewCtrl* const dv_ctrl = GetOwner()->GetOwner();
    ObjectDataViewModel* const model = dynamic_cast<ObjectDataViewModel*>(dv_ctrl->GetModel());

    if (!(model->GetItemType(dv_ctrl->GetSelection()) & (itVolume | itObject)))
        return nullptr;

    std::vector<wxBitmap*> icons = get_extruder_color_icons();
    if (icons.empty())
        return nullptr;

    DataViewBitmapText data;
    data << value;

    auto c_editor = new wxBitmapComboBox(parent, wxID_ANY, wxEmptyString,
        labelRect.GetTopLeft(), wxSize(labelRect.GetWidth(), -1), 
        0, nullptr , wxCB_READONLY);

    int i=0;
    for (wxBitmap* bmp : icons) {
        if (i==0) {
            c_editor->Append(_(L("default")), *bmp);
            ++i;
        }

        c_editor->Append(wxString::Format("%d", i), *bmp);
        ++i;
    }
    c_editor->SetSelection(atoi(data.GetText().c_str()));

    // to avoid event propagation to other sidebar items
    c_editor->Bind(wxEVT_COMBOBOX, [](wxCommandEvent& evt) { evt.StopPropagation(); });

    return c_editor;
}

bool BitmapChoiceRenderer::GetValueFromEditorCtrl(wxWindow* ctrl, wxVariant& value)
{
    wxBitmapComboBox* c = (wxBitmapComboBox*)ctrl;
    int selection = c->GetSelection();
    if (selection < 0)
        return false;
   
    DataViewBitmapText bmpText;

    bmpText.SetText(c->GetString(selection));
    bmpText.SetBitmap(c->GetItemBitmap(selection));

    value << bmpText;
    return true;
}

// ----------------------------------------------------------------------------
// DoubleSlider
// ----------------------------------------------------------------------------
DoubleSlider::DoubleSlider( wxWindow *parent,
                            wxWindowID id,
                            int lowerValue, 
                            int higherValue, 
                            int minValue, 
                            int maxValue,
                            const wxPoint& pos,
                            const wxSize& size,
                            long style,
                            const wxValidator& val,
                            const wxString& name) : 
    wxControl(parent, id, pos, size, wxWANTS_CHARS | wxBORDER_NONE),
    m_lower_value(lowerValue), m_higher_value (higherValue), 
    m_min_value(minValue), m_max_value(maxValue),
    m_style(style == wxSL_HORIZONTAL || style == wxSL_VERTICAL ? style: wxSL_HORIZONTAL)
{
#ifdef __WXOSX__ 
    is_osx = true;
#endif //__WXOSX__
    if (!is_osx)
        SetDoubleBuffered(true);// SetDoubleBuffered exists on Win and Linux/GTK, but is missing on OSX

    m_bmp_thumb_higher = (style == wxSL_HORIZONTAL ? ScalableBitmap(this, "right_half_circle.png") : ScalableBitmap(this, "up_half_circle.png",   16, true));
    m_bmp_thumb_lower  = (style == wxSL_HORIZONTAL ? ScalableBitmap(this, "left_half_circle.png" ) : ScalableBitmap(this, "down_half_circle.png", 16, true));
    m_thumb_size = m_bmp_thumb_lower.bmp().GetSize();

    m_bmp_add_tick_on  = ScalableBitmap(this, "colorchange_add_on.png");
    m_bmp_add_tick_off = ScalableBitmap(this, "colorchange_add_off.png");
    m_bmp_del_tick_on  = ScalableBitmap(this, "colorchange_delete_on.png");
    m_bmp_del_tick_off = ScalableBitmap(this, "colorchange_delete_off.png");
    m_tick_icon_dim = m_bmp_add_tick_on.bmp().GetSize().x;

    m_bmp_one_layer_lock_on    = ScalableBitmap(this, "one_layer_lock_on.png");
    m_bmp_one_layer_lock_off   = ScalableBitmap(this, "one_layer_lock_off.png");
    m_bmp_one_layer_unlock_on  = ScalableBitmap(this, "one_layer_unlock_on.png");
    m_bmp_one_layer_unlock_off = ScalableBitmap(this, "one_layer_unlock_off.png");
    m_lock_icon_dim = m_bmp_one_layer_lock_on.bmp().GetSize().x;

    m_bmp_revert               = ScalableBitmap(this, "undo");
    m_revert_icon_dim = m_bmp_revert.bmp().GetSize().x;

    m_selection = ssUndef;

    // slider events
    Bind(wxEVT_PAINT,       &DoubleSlider::OnPaint,    this);
    Bind(wxEVT_CHAR,        &DoubleSlider::OnChar,     this);
    Bind(wxEVT_LEFT_DOWN,   &DoubleSlider::OnLeftDown, this);
    Bind(wxEVT_MOTION,      &DoubleSlider::OnMotion,   this);
    Bind(wxEVT_LEFT_UP,     &DoubleSlider::OnLeftUp,   this);
    Bind(wxEVT_MOUSEWHEEL,  &DoubleSlider::OnWheel,    this);
    Bind(wxEVT_ENTER_WINDOW,&DoubleSlider::OnEnterWin, this);
    Bind(wxEVT_LEAVE_WINDOW,&DoubleSlider::OnLeaveWin, this);
    Bind(wxEVT_KEY_DOWN,    &DoubleSlider::OnKeyDown,  this);
    Bind(wxEVT_KEY_UP,      &DoubleSlider::OnKeyUp,    this);
    Bind(wxEVT_RIGHT_DOWN,  &DoubleSlider::OnRightDown,this);
    Bind(wxEVT_RIGHT_UP,    &DoubleSlider::OnRightUp,  this);

    // control's view variables
    SLIDER_MARGIN     = 4 + Slic3r::GUI::wxGetApp().em_unit();

    DARK_ORANGE_PEN   = wxPen(wxColour(253, 84, 2));
    ORANGE_PEN        = wxPen(wxColour(253, 126, 66));
    LIGHT_ORANGE_PEN  = wxPen(wxColour(254, 177, 139));

    DARK_GREY_PEN     = wxPen(wxColour(128, 128, 128));
    GREY_PEN          = wxPen(wxColour(164, 164, 164));
    LIGHT_GREY_PEN    = wxPen(wxColour(204, 204, 204));

    m_line_pens = { &DARK_GREY_PEN, &GREY_PEN, &LIGHT_GREY_PEN };
    m_segm_pens = { &DARK_ORANGE_PEN, &ORANGE_PEN, &LIGHT_ORANGE_PEN };

    const wxFont& font = GetFont();
    m_font = is_osx ? font.Smaller().Smaller() : font.Smaller();
}

void DoubleSlider::msw_rescale()
{
    const wxFont& font = Slic3r::GUI::wxGetApp().normal_font();
    m_font = is_osx ? font.Smaller().Smaller() : font.Smaller();

    m_bmp_thumb_higher.msw_rescale();
    m_bmp_thumb_lower .msw_rescale();
    m_thumb_size = m_bmp_thumb_lower.bmp().GetSize();

    m_bmp_add_tick_on .msw_rescale();
    m_bmp_add_tick_off.msw_rescale();
    m_bmp_del_tick_on .msw_rescale();
    m_bmp_del_tick_off.msw_rescale();
    m_tick_icon_dim = m_bmp_add_tick_on.bmp().GetSize().x;

    m_bmp_one_layer_lock_on   .msw_rescale();
    m_bmp_one_layer_lock_off  .msw_rescale();
    m_bmp_one_layer_unlock_on .msw_rescale();
    m_bmp_one_layer_unlock_off.msw_rescale();
    m_lock_icon_dim = m_bmp_one_layer_lock_on.bmp().GetSize().x;

    m_bmp_revert.msw_rescale();
    m_revert_icon_dim = m_bmp_revert.bmp().GetSize().x;

    SLIDER_MARGIN = 4 + Slic3r::GUI::wxGetApp().em_unit();

    SetMinSize(get_min_size());
    GetParent()->Layout();
}

int DoubleSlider::GetActiveValue() const
{
    return m_selection == ssLower ?
    m_lower_value : m_selection == ssHigher ?
                m_higher_value : -1;
}

wxSize DoubleSlider::get_min_size() const 
{
    const int min_side = is_horizontal() ?
        (is_osx ? 8 : 6) * Slic3r::GUI::wxGetApp().em_unit() :
        /*(is_osx ? 10 : 8)*/10 * Slic3r::GUI::wxGetApp().em_unit();

    return wxSize(min_side, min_side);
}

wxSize DoubleSlider::DoGetBestSize() const
{
    const wxSize size = wxControl::DoGetBestSize();
    if (size.x > 1 && size.y > 1)
        return size;
    return get_min_size();
}

void DoubleSlider::SetLowerValue(const int lower_val)
{
    m_selection = ssLower;
    m_lower_value = lower_val;
    correct_lower_value();
    Refresh();
    Update();

    wxCommandEvent e(wxEVT_SCROLL_CHANGED);
    e.SetEventObject(this);
    ProcessWindowEvent(e);
}

void DoubleSlider::SetHigherValue(const int higher_val)
{
    m_selection = ssHigher;
    m_higher_value = higher_val;
    correct_higher_value();
    Refresh();
    Update();

    wxCommandEvent e(wxEVT_SCROLL_CHANGED);
    e.SetEventObject(this);
    ProcessWindowEvent(e);
}

void DoubleSlider::SetSelectionSpan(const int lower_val, const int higher_val)
{
    m_lower_value  = std::max(lower_val, m_min_value);
    m_higher_value = std::max(std::min(higher_val, m_max_value), m_lower_value);
    if (m_lower_value < m_higher_value)
        m_is_one_layer = false;

    Refresh();
    Update();

    wxCommandEvent e(wxEVT_SCROLL_CHANGED);
    e.SetEventObject(this);
    ProcessWindowEvent(e);
}

void DoubleSlider::SetMaxValue(const int max_value)
{
    m_max_value = max_value;
    Refresh();
    Update();
}

void DoubleSlider::draw_scroll_line(wxDC& dc, const int lower_pos, const int higher_pos)
{
    int width;
    int height;
    get_size(&width, &height);

    wxCoord line_beg_x = is_horizontal() ? SLIDER_MARGIN : width*0.5 - 1;
    wxCoord line_beg_y = is_horizontal() ? height*0.5 - 1 : SLIDER_MARGIN;
    wxCoord line_end_x = is_horizontal() ? width - SLIDER_MARGIN + 1 : width*0.5 - 1;
    wxCoord line_end_y = is_horizontal() ? height*0.5 - 1 : height - SLIDER_MARGIN + 1;

    wxCoord segm_beg_x = is_horizontal() ? lower_pos : width*0.5 - 1;
    wxCoord segm_beg_y = is_horizontal() ? height*0.5 - 1 : lower_pos/*-1*/;
    wxCoord segm_end_x = is_horizontal() ? higher_pos : width*0.5 - 1;
    wxCoord segm_end_y = is_horizontal() ? height*0.5 - 1 : higher_pos-1;

    for (size_t id = 0; id < m_line_pens.size(); id++)
    {
        dc.SetPen(*m_line_pens[id]);
        dc.DrawLine(line_beg_x, line_beg_y, line_end_x, line_end_y);
        dc.SetPen(*m_segm_pens[id]);
        dc.DrawLine(segm_beg_x, segm_beg_y, segm_end_x, segm_end_y);
        if (is_horizontal())
            line_beg_y = line_end_y = segm_beg_y = segm_end_y += 1;
        else
            line_beg_x = line_end_x = segm_beg_x = segm_end_x += 1;
    }
}

double DoubleSlider::get_scroll_step()
{
    const wxSize sz = get_size();
    const int& slider_len = m_style == wxSL_HORIZONTAL ? sz.x : sz.y;
    return double(slider_len - SLIDER_MARGIN * 2) / (m_max_value - m_min_value);
}

// get position on the slider line from entered value
wxCoord DoubleSlider::get_position_from_value(const int value)
{
    const double step = get_scroll_step();
    const int val = is_horizontal() ? value : m_max_value - value;
    return wxCoord(SLIDER_MARGIN + int(val*step + 0.5));
}

wxSize DoubleSlider::get_size()
{
    int w, h;
    get_size(&w, &h);
    return wxSize(w, h);
}

void DoubleSlider::get_size(int *w, int *h)
{
    GetSize(w, h);
    is_horizontal() ? *w -= m_lock_icon_dim : *h -= m_lock_icon_dim;
}

double DoubleSlider::get_double_value(const SelectedSlider& selection)
{
    if (m_values.empty() || m_lower_value<0)
        return 0.0;
    if (m_values.size() <= m_higher_value) {
        correct_higher_value();
        return m_values.back();
    }
    return m_values[selection == ssLower ? m_lower_value : m_higher_value];
}

std::vector<double> DoubleSlider::GetTicksValues() const
{
    std::vector<double> values;

    const int val_size = m_values.size();
    if (!m_values.empty())
        // #ys_FIXME_COLOR
        // for (int tick : m_ticks) { 
        //     if (tick > val_size)
        //         break;
        //     values.push_back(m_values[tick]);
        // }
        for (const TICK_CODE& tick : m_ticks_) {
            if (tick.tick > val_size)
                break;
            values.push_back(m_values[tick.tick]);
        }

    return values;
}

void DoubleSlider::SetTicksValues(const std::vector<double>& heights)
{
    if (m_values.empty())
        return;

    // #ys_FIXME_COLOR
    // const bool was_empty = m_ticks.empty();
    //
    // m_ticks.clear();
    // for (auto h : heights) {
    //     auto it = std::lower_bound(m_values.begin(), m_values.end(), h - epsilon());
    //
    //     if (it == m_values.end())
    //         continue;
    //
    //     m_ticks.insert(it-m_values.begin());
    // }
    //
    // if (!was_empty && m_ticks.empty())
    //     // Switch to the "Feature type"/"Tool" from the very beginning of a new object slicing after deleting of the old one
    //     wxPostEvent(this->GetParent(), wxCommandEvent(wxCUSTOMEVT_TICKSCHANGED));

    const bool was_empty = m_ticks_.empty();

    m_ticks_.clear();
    for (auto h : heights) {
        auto it = std::lower_bound(m_values.begin(), m_values.end(), h - epsilon());

        if (it == m_values.end())
            continue;

        m_ticks_.insert(it-m_values.begin());
    }
    
    if (!was_empty && m_ticks_.empty())
        // Switch to the "Feature type"/"Tool" from the very beginning of a new object slicing after deleting of the old one
        wxPostEvent(this->GetParent(), wxCommandEvent(wxCUSTOMEVT_TICKSCHANGED));
}

void DoubleSlider::get_lower_and_higher_position(int& lower_pos, int& higher_pos)
{
    const double step = get_scroll_step();
    if (is_horizontal()) {
        lower_pos = SLIDER_MARGIN + int(m_lower_value*step + 0.5);
        higher_pos = SLIDER_MARGIN + int(m_higher_value*step + 0.5);
    }
    else {
        lower_pos = SLIDER_MARGIN + int((m_max_value - m_lower_value)*step + 0.5);
        higher_pos = SLIDER_MARGIN + int((m_max_value - m_higher_value)*step + 0.5);
    }
}

void DoubleSlider::draw_focus_rect()
{
    if (!m_is_focused) 
        return;
    const wxSize sz = GetSize();
    wxPaintDC dc(this);
    const wxPen pen = wxPen(wxColour(128, 128, 10), 1, wxPENSTYLE_DOT);
    dc.SetPen(pen);
    dc.SetBrush(wxBrush(wxColour(0, 0, 0), wxBRUSHSTYLE_TRANSPARENT));
    dc.DrawRectangle(1, 1, sz.x - 2, sz.y - 2);
}

void DoubleSlider::render()
{
    SetBackgroundColour(GetParent()->GetBackgroundColour());
    draw_focus_rect();

    wxPaintDC dc(this);
    dc.SetFont(m_font);

    const wxCoord lower_pos = get_position_from_value(m_lower_value);
    const wxCoord higher_pos = get_position_from_value(m_higher_value);

    // draw colored band on the background of a scroll line 
    // and only in a case of no-empty m_values
    draw_colored_band(dc);

    // draw line
    draw_scroll_line(dc, lower_pos, higher_pos);

//     //lower slider:
//     draw_thumb(dc, lower_pos, ssLower);
//     //higher slider:
//     draw_thumb(dc, higher_pos, ssHigher);

    // draw both sliders
    draw_thumbs(dc, lower_pos, higher_pos);

    //draw color print ticks
    draw_ticks(dc);

    //draw lock/unlock
    draw_one_layer_icon(dc);

    //draw revert bitmap (if it's shown)
    draw_revert_icon(dc);
}

void DoubleSlider::draw_action_icon(wxDC& dc, const wxPoint pt_beg, const wxPoint pt_end)
{
    const int tick = m_selection == ssLower ? m_lower_value : m_higher_value;

    // suppress add tick on first layer
    if (tick == 0)
        return;

    wxBitmap* icon = m_is_action_icon_focesed ? &m_bmp_add_tick_off.bmp() : &m_bmp_add_tick_on.bmp();
    // #ys_FIXME_COLOR
    // if (m_ticks.find(tick) != m_ticks.end())
    //     icon = m_is_action_icon_focesed ? &m_bmp_del_tick_off.bmp() : &m_bmp_del_tick_on.bmp();
    if (m_ticks_.find(tick) != m_ticks_.end())
        icon = m_is_action_icon_focesed ? &m_bmp_del_tick_off.bmp() : &m_bmp_del_tick_on.bmp();

    wxCoord x_draw, y_draw;
    is_horizontal() ? x_draw = pt_beg.x - 0.5*m_tick_icon_dim : y_draw = pt_beg.y - 0.5*m_tick_icon_dim;
    if (m_selection == ssLower)
        is_horizontal() ? y_draw = pt_end.y + 3 : x_draw = pt_beg.x - m_tick_icon_dim-2;
    else
        is_horizontal() ? y_draw = pt_beg.y - m_tick_icon_dim-2 : x_draw = pt_end.x + 3;

    dc.DrawBitmap(*icon, x_draw, y_draw);

    //update rect of the tick action icon
    m_rect_tick_action = wxRect(x_draw, y_draw, m_tick_icon_dim, m_tick_icon_dim);
}

void DoubleSlider::draw_info_line_with_icon(wxDC& dc, const wxPoint& pos, const SelectedSlider selection)
{
    if (m_selection == selection) {
        //draw info line
        dc.SetPen(DARK_ORANGE_PEN);
        const wxPoint pt_beg = is_horizontal() ? wxPoint(pos.x, pos.y - m_thumb_size.y) : wxPoint(pos.x - m_thumb_size.x, pos.y/* - 1*/);
        const wxPoint pt_end = is_horizontal() ? wxPoint(pos.x, pos.y + m_thumb_size.y) : wxPoint(pos.x + m_thumb_size.x, pos.y/* - 1*/);
        dc.DrawLine(pt_beg, pt_end);

        //draw action icon
        if (m_is_enabled_tick_manipulation)
            draw_action_icon(dc, pt_beg, pt_end);
    }
}

wxString DoubleSlider::get_label(const SelectedSlider& selection) const
{
    const int value = selection == ssLower ? m_lower_value : m_higher_value;

    if (m_label_koef == 1.0 && m_values.empty())
        return wxString::Format("%d", value);
    if (value >= m_values.size())
        return "ErrVal";

    const wxString str = m_values.empty() ? 
                         wxNumberFormatter::ToString(m_label_koef*value, 2, wxNumberFormatter::Style_None) :
                         wxNumberFormatter::ToString(m_values[value], 2, wxNumberFormatter::Style_None);
    return wxString::Format("%s\n(%d)", str, m_values.empty() ? value : value+1);
}

void DoubleSlider::draw_thumb_text(wxDC& dc, const wxPoint& pos, const SelectedSlider& selection) const
{
    if ( selection == ssUndef || 
        ((m_is_one_layer || m_higher_value==m_lower_value) && selection != m_selection) )
        return;
    wxCoord text_width, text_height;
    const wxString label = get_label(selection);
    dc.GetMultiLineTextExtent(label, &text_width, &text_height);
    wxPoint text_pos;
    if (selection ==ssLower)
        text_pos = is_horizontal() ? wxPoint(pos.x + 1, pos.y + m_thumb_size.x) :
                           wxPoint(pos.x + m_thumb_size.x+1, pos.y - 0.5*text_height - 1);
    else
        text_pos = is_horizontal() ? wxPoint(pos.x - text_width - 1, pos.y - m_thumb_size.x - text_height) :
                    wxPoint(pos.x - text_width - 1 - m_thumb_size.x, pos.y - 0.5*text_height + 1);
    dc.DrawText(label, text_pos);
}

void DoubleSlider::draw_thumb_item(wxDC& dc, const wxPoint& pos, const SelectedSlider& selection)
{
    wxCoord x_draw, y_draw;
    if (selection == ssLower) {
        if (is_horizontal()) {
            x_draw = pos.x - m_thumb_size.x;
            y_draw = pos.y - int(0.5*m_thumb_size.y);
        }
        else {
            x_draw = pos.x - int(0.5*m_thumb_size.x);
            y_draw = pos.y+1;
        }
    }
    else{
        if (is_horizontal()) {
            x_draw = pos.x;
            y_draw = pos.y - int(0.5*m_thumb_size.y);
        }
        else {
            x_draw = pos.x - int(0.5*m_thumb_size.x);
            y_draw = pos.y - m_thumb_size.y;
        }
    }
    dc.DrawBitmap(selection == ssLower ? m_bmp_thumb_lower.bmp() : m_bmp_thumb_higher.bmp(), x_draw, y_draw);

    // Update thumb rect
    update_thumb_rect(x_draw, y_draw, selection);
}

void DoubleSlider::draw_thumb(wxDC& dc, const wxCoord& pos_coord, const SelectedSlider& selection)
{
    //calculate thumb position on slider line
    int width, height;
    get_size(&width, &height);
    const wxPoint pos = is_horizontal() ? wxPoint(pos_coord, height*0.5) : wxPoint(0.5*width, pos_coord);

    // Draw thumb
    draw_thumb_item(dc, pos, selection);

    // Draw info_line
    draw_info_line_with_icon(dc, pos, selection);

    // Draw thumb text
    draw_thumb_text(dc, pos, selection);
}

void DoubleSlider::draw_thumbs(wxDC& dc, const wxCoord& lower_pos, const wxCoord& higher_pos)
{
    //calculate thumb position on slider line
    int width, height;
    get_size(&width, &height);
    const wxPoint pos_l = is_horizontal() ? wxPoint(lower_pos, height*0.5) : wxPoint(0.5*width, lower_pos);
    const wxPoint pos_h = is_horizontal() ? wxPoint(higher_pos, height*0.5) : wxPoint(0.5*width, higher_pos);

    // Draw lower thumb
    draw_thumb_item(dc, pos_l, ssLower);
    // Draw lower info_line
    draw_info_line_with_icon(dc, pos_l, ssLower);

    // Draw higher thumb
    draw_thumb_item(dc, pos_h, ssHigher);
    // Draw higher info_line
    draw_info_line_with_icon(dc, pos_h, ssHigher);
    // Draw higher thumb text
    draw_thumb_text(dc, pos_h, ssHigher);

    // Draw lower thumb text
    draw_thumb_text(dc, pos_l, ssLower);
}

void DoubleSlider::draw_ticks(wxDC& dc)
{
    if (!m_is_enabled_tick_manipulation)
        return;

    dc.SetPen(m_is_enabled_tick_manipulation ? DARK_GREY_PEN : LIGHT_GREY_PEN );
    int height, width;
    get_size(&width, &height);
    const wxCoord mid = is_horizontal() ? 0.5*height : 0.5*width;
    // #ys_FIXME_COLOR
    // for (auto tick : m_ticks)
    for (auto tick : m_ticks_)
    {
        // #ys_FIXME_COLOR
        // const wxCoord pos = get_position_from_value(tick);
        const wxCoord pos = get_position_from_value(tick.tick);

        is_horizontal() ?   dc.DrawLine(pos, mid-14, pos, mid-9) :
                            dc.DrawLine(mid - 14, pos/* - 1*/, mid - 9, pos/* - 1*/);
        is_horizontal() ?   dc.DrawLine(pos, mid+14, pos, mid+9) :
                            dc.DrawLine(mid + 14, pos/* - 1*/, mid + 9, pos/* - 1*/);
    }
}

void DoubleSlider::draw_colored_band(wxDC& dc)
{
    if (!m_is_enabled_tick_manipulation)
        return;

    int height, width;
    get_size(&width, &height);

    wxRect main_band = m_rect_lower_thumb;
    if (is_horizontal()) {
        main_band.SetLeft(SLIDER_MARGIN);
        main_band.SetRight(width - SLIDER_MARGIN + 1);
    }
    else {
        const int cut = 2;
        main_band.x += cut;
        main_band.width -= 2*cut;
        main_band.SetTop(SLIDER_MARGIN);
        main_band.SetBottom(height - SLIDER_MARGIN + 1);
    }

    // #ys_FIXME_COLOR
    // if (m_ticks.empty()) {
    if (m_ticks_.empty()) {
        dc.SetPen(GetParent()->GetBackgroundColour());
        dc.SetBrush(GetParent()->GetBackgroundColour());
        dc.DrawRectangle(main_band);
        return;
    }

    const std::vector<std::string>& colors = Slic3r::GCodePreviewData::ColorPrintColors();
    const size_t colors_cnt = colors.size();

    wxColour clr(colors[0]);
    dc.SetPen(clr);
    dc.SetBrush(clr);
    dc.DrawRectangle(main_band);

    size_t i = 1;
    // #ys_FIXME_COLOR
    // for (auto tick : m_ticks)
    for (auto tick : m_ticks_)
    {
        if (i == colors_cnt)
            i = 0;
        // #ys_FIXME_COLOR
        //const wxCoord pos = get_position_from_value(tick);
        const wxCoord pos = get_position_from_value(tick.tick);
        is_horizontal() ?   main_band.SetLeft(SLIDER_MARGIN + pos) :
                            main_band.SetBottom(pos-1);

        clr = wxColour(colors[i]);
        dc.SetPen(clr);
        dc.SetBrush(clr);
        dc.DrawRectangle(main_band);
        i++;
    }
}

void DoubleSlider::draw_one_layer_icon(wxDC& dc)
{
    const wxBitmap& icon = m_is_one_layer ?
                     m_is_one_layer_icon_focesed ? m_bmp_one_layer_lock_off.bmp()   : m_bmp_one_layer_lock_on.bmp() :
                     m_is_one_layer_icon_focesed ? m_bmp_one_layer_unlock_off.bmp() : m_bmp_one_layer_unlock_on.bmp();

    int width, height;
    get_size(&width, &height);

    wxCoord x_draw, y_draw;
    is_horizontal() ? x_draw = width-2 : x_draw = 0.5*width - 0.5*m_lock_icon_dim;
    is_horizontal() ? y_draw = 0.5*height - 0.5*m_lock_icon_dim : y_draw = height-2;

    dc.DrawBitmap(icon, x_draw, y_draw);

    //update rect of the lock/unlock icon
    m_rect_one_layer_icon = wxRect(x_draw, y_draw, m_lock_icon_dim, m_lock_icon_dim);
}

void DoubleSlider::draw_revert_icon(wxDC& dc)
{
    // #ys_FIXME_COLOR
    // if (m_ticks.empty() || !m_is_enabled_tick_manipulation)
    if (m_ticks_.empty() || !m_is_enabled_tick_manipulation)
        return;

    int width, height;
    get_size(&width, &height);

    wxCoord x_draw, y_draw;
    is_horizontal() ? x_draw = width-2 : x_draw = 0.25*SLIDER_MARGIN;
    is_horizontal() ? y_draw = 0.25*SLIDER_MARGIN: y_draw = height-2;

    dc.DrawBitmap(m_bmp_revert.bmp(), x_draw, y_draw);

    //update rect of the lock/unlock icon
    m_rect_revert_icon = wxRect(x_draw, y_draw, m_revert_icon_dim, m_revert_icon_dim);
}

void DoubleSlider::update_thumb_rect(const wxCoord& begin_x, const wxCoord& begin_y, const SelectedSlider& selection)
{
    const wxRect& rect = wxRect(begin_x, begin_y, m_thumb_size.x, m_thumb_size.y);
    if (selection == ssLower)
        m_rect_lower_thumb = rect;
    else
        m_rect_higher_thumb = rect;
}

int DoubleSlider::get_value_from_position(const wxCoord x, const wxCoord y)
{
    const int height = get_size().y;
    const double step = get_scroll_step();
    
    if (is_horizontal()) 
        return int(double(x - SLIDER_MARGIN) / step + 0.5);

    return int(m_min_value + double(height - SLIDER_MARGIN - y) / step + 0.5);
}

void DoubleSlider::detect_selected_slider(const wxPoint& pt)
{
    m_selection = is_point_in_rect(pt, m_rect_lower_thumb) ? ssLower :
                  is_point_in_rect(pt, m_rect_higher_thumb) ? ssHigher : ssUndef;
}

bool DoubleSlider::is_point_in_rect(const wxPoint& pt, const wxRect& rect)
{
    if (rect.GetLeft() <= pt.x && pt.x <= rect.GetRight() && 
        rect.GetTop()  <= pt.y && pt.y <= rect.GetBottom())
        return true;
    return false;
}

int DoubleSlider::is_point_near_tick(const wxPoint& pt)
{
    // #ys_FIXME_COLOR
    // for (auto tick : m_ticks) {
    for (auto tick : m_ticks_) {
        // #ys_FIXME_COLOR
        // const wxCoord pos = get_position_from_value(tick);
        const wxCoord pos = get_position_from_value(tick.tick);

        if (is_horizontal()) {
            if (pos - 4 <= pt.x && pt.x <= pos + 4)
                // #ys_FIXME_COLOR
                // return tick;
                return tick.tick;
        }
        else {
            if (pos - 4 <= pt.y && pt.y <= pos + 4) 
                // #ys_FIXME_COLOR
                // return tick;
                return tick.tick;
        }
    }
    return -1;
}

void DoubleSlider::ChangeOneLayerLock()
{
    m_is_one_layer = !m_is_one_layer;
    m_selection == ssLower ? correct_lower_value() : correct_higher_value();
    if (!m_selection) m_selection = ssHigher;

    Refresh();
    Update();

    wxCommandEvent e(wxEVT_SCROLL_CHANGED);
    e.SetEventObject(this);
    ProcessWindowEvent(e);
}

void DoubleSlider::OnLeftDown(wxMouseEvent& event)
{
    if (HasCapture())
        return;
    this->CaptureMouse();

    wxClientDC dc(this);
    wxPoint pos = event.GetLogicalPosition(dc);
    if (is_point_in_rect(pos, m_rect_tick_action) && m_is_enabled_tick_manipulation) {
        action_tick(taOnIcon);
        return;
    }

    m_is_left_down = true;
    if (is_point_in_rect(pos, m_rect_one_layer_icon)) {
        // switch on/off one layer mode
        m_is_one_layer = !m_is_one_layer;
        if (!m_is_one_layer) {
            SetLowerValue(m_min_value);
            SetHigherValue(m_max_value);
        }
        m_selection == ssLower ? correct_lower_value() : correct_higher_value();
        if (!m_selection) m_selection = ssHigher;
    }
    else if (is_point_in_rect(pos, m_rect_revert_icon) && m_is_enabled_tick_manipulation) {
        // discard all color changes
        SetLowerValue(m_min_value);
        SetHigherValue(m_max_value);

        m_selection == ssLower ? correct_lower_value() : correct_higher_value();
        if (!m_selection) m_selection = ssHigher;

        // #ys_FIXME_COLOR
        // m_ticks.clear();
        m_ticks_.clear();
        wxPostEvent(this->GetParent(), wxCommandEvent(wxCUSTOMEVT_TICKSCHANGED));
    }
    else
        detect_selected_slider(pos);

    if (!m_selection) {
        const int tick_val  = is_point_near_tick(pos);
        /* Set current thumb position to the nearest tick (if it is)
         * OR to a value corresponding to the mouse click
         * */ 
        const int mouse_val = tick_val >= 0 && m_is_enabled_tick_manipulation ? tick_val : 
                              get_value_from_position(pos.x, pos.y);
        if (mouse_val >= 0)
        {
            if (abs(mouse_val - m_lower_value) < abs(mouse_val - m_higher_value)) {
                SetLowerValue(mouse_val);
                correct_lower_value();
                m_selection = ssLower;
            }
            else {
                SetHigherValue(mouse_val);
                correct_higher_value();
                m_selection = ssHigher;
            }
        }
    }

    Refresh();
    Update();
    event.Skip();
}

void DoubleSlider::correct_lower_value()
{
    if (m_lower_value < m_min_value)
        m_lower_value = m_min_value;
    else if (m_lower_value > m_max_value)
        m_lower_value = m_max_value;
    
    if ((m_lower_value >= m_higher_value && m_lower_value <= m_max_value) || m_is_one_layer)
        m_higher_value = m_lower_value;
}

void DoubleSlider::correct_higher_value()
{
    if (m_higher_value > m_max_value)
        m_higher_value = m_max_value;
    else if (m_higher_value < m_min_value)
        m_higher_value = m_min_value;
    
    if ((m_higher_value <= m_lower_value && m_higher_value >= m_min_value) || m_is_one_layer)
        m_lower_value = m_higher_value;
}

void DoubleSlider::OnMotion(wxMouseEvent& event)
{
    bool action = false;

    const wxClientDC dc(this);
    const wxPoint pos = event.GetLogicalPosition(dc);

    m_is_one_layer_icon_focesed = is_point_in_rect(pos, m_rect_one_layer_icon);
    bool is_revert_icon_focused = false;

    if (!m_is_left_down && !m_is_one_layer) {
        m_is_action_icon_focesed = is_point_in_rect(pos, m_rect_tick_action);
        // #ys_FIXME_COLOR
        // is_revert_icon_focused = !m_ticks.empty() && is_point_in_rect(pos, m_rect_revert_icon);
        is_revert_icon_focused = !m_ticks_.empty() && is_point_in_rect(pos, m_rect_revert_icon);
    }
    else if (m_is_left_down || m_is_right_down) {
        if (m_selection == ssLower) {
            int current_value = m_lower_value;
            m_lower_value = get_value_from_position(pos.x, pos.y);
            correct_lower_value();
            action = (current_value != m_lower_value);
        }
        else if (m_selection == ssHigher) {
            int current_value = m_higher_value;
            m_higher_value = get_value_from_position(pos.x, pos.y);
            correct_higher_value();
            action = (current_value != m_higher_value);
        }
        if (m_is_right_down) m_is_mouse_move = true;
    }
    Refresh();
    Update();
    event.Skip();

    // Set tooltips with information for each icon
    const wxString tooltip = m_is_one_layer_icon_focesed    ? _(L("One layer mode"))    :
                             m_is_action_icon_focesed       ? _(L("Add/Del color change")) :
                             is_revert_icon_focused         ? _(L("Discard all color changes")) : "";
    this->SetToolTip(tooltip);

    if (action)
    {
        wxCommandEvent e(wxEVT_SCROLL_CHANGED);
        e.SetEventObject(this);
        e.SetString("moving");
        ProcessWindowEvent(e);
    }
}

void DoubleSlider::OnLeftUp(wxMouseEvent& event)
{
    if (!HasCapture())
        return;
    this->ReleaseMouse();
    m_is_left_down = false;
    Refresh();
    Update();
    event.Skip();

    wxCommandEvent e(wxEVT_SCROLL_CHANGED);
    e.SetEventObject(this);
    ProcessWindowEvent(e);
}

void DoubleSlider::enter_window(wxMouseEvent& event, const bool enter)
{
    m_is_focused = enter;
    Refresh();
    Update();
    event.Skip();
}

// "condition" have to be true for:
//    -  value increase (if wxSL_VERTICAL)
//    -  value decrease (if wxSL_HORIZONTAL) 
void DoubleSlider::move_current_thumb(const bool condition)
{
//     m_is_one_layer = wxGetKeyState(WXK_CONTROL);
    int delta = condition ? -1 : 1;
    if (is_horizontal())
        delta *= -1;

    if (m_selection == ssLower) {
        m_lower_value -= delta;
        correct_lower_value();
    }
    else if (m_selection == ssHigher) {
        m_higher_value -= delta;
        correct_higher_value();
    }
    Refresh();
    Update();

    wxCommandEvent e(wxEVT_SCROLL_CHANGED);
    e.SetEventObject(this);
    ProcessWindowEvent(e);
}

void DoubleSlider::action_tick(const TicksAction action)
{
    if (m_selection == ssUndef)
        return;

    const int tick = m_selection == ssLower ? m_lower_value : m_higher_value;

    // #ys_FIXME_COLOR
    // if (action == taOnIcon) {
    //     if (!m_ticks.insert(tick).second)
    //         m_ticks.erase(tick);
    // }
    // else {
    //     const auto it = m_ticks.find(tick);
    //     if (it == m_ticks.end() && action == taAdd)
    //         m_ticks.insert(tick);
    //     else if (it != m_ticks.end() && action == taDel)
    //         m_ticks.erase(tick);
    // }

    if (action == taOnIcon) {
        if (!m_ticks_.insert(TICK_CODE(tick)).second)
            m_ticks_.erase(TICK_CODE(tick));
    }
    else {
        const auto it = m_ticks_.find(tick);
        if (it == m_ticks_.end() && action == taAdd)
            m_ticks_.insert(tick);
        else if (it != m_ticks_.end() && action == taDel)
            m_ticks_.erase(tick);
    }

    wxPostEvent(this->GetParent(), wxCommandEvent(wxCUSTOMEVT_TICKSCHANGED));
    Refresh();
    Update();
}

void DoubleSlider::OnWheel(wxMouseEvent& event)
{
    // Set nearest to the mouse thumb as a selected, if there is not selected thumb
    if (m_selection == ssUndef) 
    {
        const wxPoint& pt = event.GetLogicalPosition(wxClientDC(this));
        
        if (is_horizontal())
            m_selection = abs(pt.x - m_rect_lower_thumb.GetRight()) <= 
                          abs(pt.x - m_rect_higher_thumb.GetLeft()) ? 
                          ssLower : ssHigher;
        else
            m_selection = abs(pt.y - m_rect_lower_thumb.GetTop()) <= 
                          abs(pt.y - m_rect_higher_thumb.GetBottom()) ? 
                          ssLower : ssHigher;
    }

    move_current_thumb(event.GetWheelRotation() > 0);
}

void DoubleSlider::OnKeyDown(wxKeyEvent &event)
{
    const int key = event.GetKeyCode();
    if (key == WXK_NUMPAD_ADD)
        action_tick(taAdd);
    else if (key == 390 || key == WXK_DELETE || key == WXK_BACK)
        action_tick(taDel);
    else if (is_horizontal())
    {
        if (key == WXK_LEFT || key == WXK_RIGHT)
            move_current_thumb(key == WXK_LEFT); 
        else if (key == WXK_UP || key == WXK_DOWN) {
            m_selection = key == WXK_UP ? ssHigher : ssLower;
            Refresh();
        }
    }
    else {
        if (key == WXK_LEFT || key == WXK_RIGHT) {
            m_selection = key == WXK_LEFT ? ssHigher : ssLower;
            Refresh();
        }
        else if (key == WXK_UP || key == WXK_DOWN)
            move_current_thumb(key == WXK_UP);
    }

    event.Skip(); // !Needed to have EVT_CHAR generated as well
}

void DoubleSlider::OnKeyUp(wxKeyEvent &event)
{
    if (event.GetKeyCode() == WXK_CONTROL)
        m_is_one_layer = false;
    Refresh();
    Update();
    event.Skip();
}

void DoubleSlider::OnChar(wxKeyEvent& event)
{
    const int key = event.GetKeyCode();
    if (key == '+')
        action_tick(taAdd);
    else if (key == '-')
        action_tick(taDel);
}

void DoubleSlider::OnRightDown(wxMouseEvent& event)
{
    if (HasCapture()) return;
    this->CaptureMouse();

    const wxClientDC dc(this);

    wxPoint pos = event.GetLogicalPosition(dc);
    if (is_point_in_rect(pos, m_rect_tick_action) && m_is_enabled_tick_manipulation)
    {
        const int tick = m_selection == ssLower ? m_lower_value : m_higher_value;
        // if on this Y doesn't exist tick
        // #ys_FIXME_COLOR
        // if (m_ticks.find(tick) == m_ticks.end())
        if (m_ticks_.find(tick) == m_ticks_.end())
        {
            // show context menu on OnRightUp()
            m_show_context_menu = true;
            return;
        }
    }

    detect_selected_slider(event.GetLogicalPosition(dc));
    if (!m_selection)
        return;

    if (m_selection == ssLower)
        m_higher_value = m_lower_value;
    else
        m_lower_value = m_higher_value;

    // set slider to "one layer" mode
    m_is_right_down = m_is_one_layer = true;

    Refresh();
    Update();
    event.Skip();
}

void DoubleSlider::OnRightUp(wxMouseEvent& event)
{
    if (!HasCapture())
        return;
    this->ReleaseMouse();
    m_is_right_down = m_is_one_layer = false;

    if (m_show_context_menu) {
        wxMenu menu;
    
        append_menu_item(&menu, wxID_ANY, _(L("Add color change")) + " (M600)", "",
            [this](wxCommandEvent&) { add_code("M600"); }, "colorchange_add_off.png", &menu);
    
        append_menu_item(&menu, wxID_ANY, _(L("Add pause SD print")) + " (M25)", "",
            [this](wxCommandEvent&) { add_code("M25"); }, "pause_add.png", &menu);
    
        append_menu_item(&menu, wxID_ANY, _(L("Add custom G-code")), "",
            [this](wxCommandEvent&) { add_code(""); }, "add_gcode", &menu);
    
        Slic3r::GUI::wxGetApp().plater()->PopupMenu(&menu);

        m_show_context_menu = false;
    }

    Refresh();
    Update();
    event.Skip();
}

void DoubleSlider::add_code(std::string code)
{
    const int tick = m_selection == ssLower ? m_lower_value : m_higher_value;
    // if on this Y doesn't exist tick
    if (m_ticks_.find(tick) == m_ticks_.end())
    {
        if (code.empty())
        {
            wxString msg_text = from_u8(_utf8(L("Enter custom G-code used on current layer"))) + " :";
            wxString msg_header = from_u8((boost::format(_utf8(L("Custom Gcode on current layer (%1% mm)."))) % m_values[tick]).str());

            // get custom gcode
            wxString custom_code = wxGetTextFromUser(msg_text, msg_header);

            if (custom_code.IsEmpty()) 
                return;
            code = custom_code.c_str();
        }

        m_ticks_.insert(TICK_CODE(tick, code));

        wxPostEvent(this->GetParent(), wxCommandEvent(wxCUSTOMEVT_TICKSCHANGED));
        Refresh();
        Update();
    }
}


// ----------------------------------------------------------------------------
// LockButton
// ----------------------------------------------------------------------------

LockButton::LockButton( wxWindow *parent, 
                        wxWindowID id, 
                        const wxPoint& pos /*= wxDefaultPosition*/, 
                        const wxSize& size /*= wxDefaultSize*/):
                        wxButton(parent, id, wxEmptyString, pos, size, wxBU_EXACTFIT | wxNO_BORDER)
{
    m_bmp_lock_closed   = ScalableBitmap(this, "lock_closed");
    m_bmp_lock_closed_f = ScalableBitmap(this, "lock_closed_f");
    m_bmp_lock_open     = ScalableBitmap(this, "lock_open");
    m_bmp_lock_open_f   = ScalableBitmap(this, "lock_open_f");

#ifdef __WXMSW__
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif // __WXMSW__
    SetBitmap(m_bmp_lock_open.bmp());
    SetBitmapDisabled(m_bmp_lock_open.bmp());
    SetBitmapHover(m_bmp_lock_closed_f.bmp());

    //button events
    Bind(wxEVT_BUTTON, &LockButton::OnButton, this);
}

void LockButton::OnButton(wxCommandEvent& event)
{
    if (m_disabled)
        return;

    m_is_pushed = !m_is_pushed;
    update_button_bitmaps();

    event.Skip();
}

void LockButton::SetLock(bool lock)
{
    m_is_pushed = lock;
    update_button_bitmaps();
}

void LockButton::msw_rescale()
{
    m_bmp_lock_closed.msw_rescale();
    m_bmp_lock_closed_f.msw_rescale();
    m_bmp_lock_open.msw_rescale();
    m_bmp_lock_open_f.msw_rescale();

    update_button_bitmaps();
}

void LockButton::update_button_bitmaps()
{
    SetBitmap(m_is_pushed ? m_bmp_lock_closed.bmp() : m_bmp_lock_open.bmp());
    SetBitmapHover(m_is_pushed ? m_bmp_lock_closed_f.bmp() : m_bmp_lock_open_f.bmp());

    Refresh();
    Update();
}



// ----------------------------------------------------------------------------
// ModeButton
// ----------------------------------------------------------------------------

ModeButton::ModeButton( wxWindow *          parent,
                        wxWindowID          id,
                        const std::string&  icon_name   /* = ""*/,
                        const wxString&     mode        /* = wxEmptyString*/,
                        const wxSize&       size        /* = wxDefaultSize*/,
                        const wxPoint&      pos         /* = wxDefaultPosition*/) :
    ScalableButton(parent, id, icon_name, mode, size, pos, wxBU_EXACTFIT)
{
    m_tt_focused = wxString::Format(_(L("Switch to the %s mode")), mode);
    m_tt_selected = wxString::Format(_(L("Current mode is %s")), mode);

    SetBitmapMargins(3, 0);

    //button events
    Bind(wxEVT_BUTTON,          &ModeButton::OnButton, this);
    Bind(wxEVT_ENTER_WINDOW,    &ModeButton::OnEnterBtn, this);
    Bind(wxEVT_LEAVE_WINDOW,    &ModeButton::OnLeaveBtn, this);
}

void ModeButton::OnButton(wxCommandEvent& event)
{
    m_is_selected = true;
    focus_button(m_is_selected);

    event.Skip();
}

void ModeButton::SetState(const bool state)
{
    m_is_selected = state;
    focus_button(m_is_selected);
    SetToolTip(state ? m_tt_selected : m_tt_focused);
}

void ModeButton::focus_button(const bool focus)
{
    const wxFont& new_font = focus ? 
                             Slic3r::GUI::wxGetApp().bold_font() : 
                             Slic3r::GUI::wxGetApp().normal_font();

    SetFont(new_font);
    SetForegroundColour(wxSystemSettings::GetColour(focus ? wxSYS_COLOUR_BTNTEXT : wxSYS_COLOUR_BTNSHADOW));

    Refresh();
    Update();
}


// ----------------------------------------------------------------------------
// ModeSizer
// ----------------------------------------------------------------------------

ModeSizer::ModeSizer(wxWindow *parent, int hgap/* = 0*/) :
    wxFlexGridSizer(3, 0, hgap)
{
    SetFlexibleDirection(wxHORIZONTAL);

    std::vector < std::pair < wxString, std::string >> buttons = {
        {_(L("Simple")),    "mode_simple_sq.png"},
        {_(L("Advanced")),  "mode_advanced_sq.png"},
        {_(L("Expert")),    "mode_expert_sq.png"}
    };

    auto modebtnfn = [](wxCommandEvent &event, int mode_id) {
        Slic3r::GUI::wxGetApp().save_mode(mode_id);
        event.Skip();
    };
    
    m_mode_btns.reserve(3);
    for (const auto& button : buttons) {
        m_mode_btns.push_back(new ModeButton(parent, wxID_ANY, button.second, button.first));

        m_mode_btns.back()->Bind(wxEVT_BUTTON, std::bind(modebtnfn, std::placeholders::_1, int(m_mode_btns.size() - 1)));
        Add(m_mode_btns.back());
    }
}

void ModeSizer::SetMode(const int mode)
{
    for (size_t m = 0; m < m_mode_btns.size(); m++)
        m_mode_btns[m]->SetState(int(m) == mode);
}


void ModeSizer::msw_rescale()
{
    for (size_t m = 0; m < m_mode_btns.size(); m++)
        m_mode_btns[m]->msw_rescale();
}

// ----------------------------------------------------------------------------
// MenuWithSeparators
// ----------------------------------------------------------------------------

void MenuWithSeparators::DestroySeparators()
{
    if (m_separator_frst) {
        Destroy(m_separator_frst);
        m_separator_frst = nullptr;
    }

    if (m_separator_scnd) {
        Destroy(m_separator_scnd);
        m_separator_scnd = nullptr;
    }
}

void MenuWithSeparators::SetFirstSeparator()
{
    m_separator_frst = this->AppendSeparator();
}

void MenuWithSeparators::SetSecondSeparator()
{
    m_separator_scnd = this->AppendSeparator();
}

// ----------------------------------------------------------------------------
// PrusaBitmap
// ----------------------------------------------------------------------------
ScalableBitmap::ScalableBitmap( wxWindow *parent, 
                                const std::string& icon_name/* = ""*/,
                                const int px_cnt/* = 16*/, 
                                const bool is_horizontal/*  = false*/):
    m_parent(parent), m_icon_name(icon_name),
    m_px_cnt(px_cnt), m_is_horizontal(is_horizontal)
{
    m_bmp = create_scaled_bitmap(parent, icon_name, px_cnt, is_horizontal);
}


void ScalableBitmap::msw_rescale()
{
    m_bmp = create_scaled_bitmap(m_parent, m_icon_name, m_px_cnt, m_is_horizontal);
}

// ----------------------------------------------------------------------------
// PrusaButton
// ----------------------------------------------------------------------------

ScalableButton::ScalableButton( wxWindow *          parent,
                                wxWindowID          id,
                                const std::string&  icon_name /*= ""*/,
                                const wxString&     label /* = wxEmptyString*/,
                                const wxSize&       size /* = wxDefaultSize*/,
                                const wxPoint&      pos /* = wxDefaultPosition*/,
                                long                style /*= wxBU_EXACTFIT | wxNO_BORDER*/) :
    m_current_icon_name(icon_name),
    m_parent(parent)
{
    Create(parent, id, label, pos, size, style);
#ifdef __WXMSW__
    if (style & wxNO_BORDER)
        SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif // __WXMSW__

    SetBitmap(create_scaled_bitmap(parent, icon_name));

    if (size != wxDefaultSize)
    {
        const int em = em_unit(parent);
        m_width = size.x/em;
        m_height= size.y/em;
    }
}


ScalableButton::ScalableButton( wxWindow *          parent, 
                                wxWindowID          id,
                                const ScalableBitmap&  bitmap,
                                const wxString&     label /*= wxEmptyString*/, 
                                long                style /*= wxBU_EXACTFIT | wxNO_BORDER*/) :
    m_current_icon_name(bitmap.name()),
    m_parent(parent)
{
    Create(parent, id, label, wxDefaultPosition, wxDefaultSize, style);
#ifdef __WXMSW__
    if (style & wxNO_BORDER)
        SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif // __WXMSW__

    SetBitmap(bitmap.bmp());
}

void ScalableButton::SetBitmap_(const ScalableBitmap& bmp)
{
    SetBitmap(bmp.bmp());
    m_current_icon_name = bmp.name();
}

void ScalableButton::SetBitmapDisabled_(const ScalableBitmap& bmp)
{
    SetBitmapDisabled(bmp.bmp());
    m_disabled_icon_name = bmp.name();
}

void ScalableButton::msw_rescale()
{
    SetBitmap(create_scaled_bitmap(m_parent, m_current_icon_name));
    if (!m_disabled_icon_name.empty())
        SetBitmapDisabled(create_scaled_bitmap(m_parent, m_disabled_icon_name));

    if (m_width > 0 || m_height>0)
    {
        const int em = em_unit(m_parent);
        wxSize size(m_width * em, m_height * em);
        SetMinSize(size);
    }
}




