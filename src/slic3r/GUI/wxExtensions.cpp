#include "wxExtensions.hpp"

#include "libslic3r/Utils.hpp"
#include "libslic3r/Model.hpp"

#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/dcclient.h>
#include <wx/numformatter.h>

#include "BitmapCache.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"

wxDEFINE_EVENT(wxCUSTOMEVT_TICKSCHANGED, wxEvent);
wxDEFINE_EVENT(wxCUSTOMEVT_LAST_VOLUME_IS_DELETED, wxCommandEvent);

wxMenuItem* append_menu_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent& event)> cb, const wxBitmap& icon, wxEvtHandler* event_handler)
{
    if (id == wxID_ANY)
        id = wxNewId();

    wxMenuItem* item = menu->Append(id, string, description);
    item->SetBitmap(icon);

#ifdef __WXMSW__
    if (event_handler != nullptr && event_handler != menu)
        event_handler->Bind(wxEVT_MENU, cb, id);
    else
#endif // __WXMSW__
        menu->Bind(wxEVT_MENU, cb, id);

    return item;
}

wxMenuItem* append_menu_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent& event)> cb, const std::string& icon, wxEvtHandler* event_handler)
{
    const wxBitmap& bmp = !icon.empty() ? wxBitmap(Slic3r::var(icon), wxBITMAP_TYPE_PNG) : wxNullBitmap;
    return append_menu_item(menu, id, string, description, cb, bmp, event_handler);
}

wxMenuItem* append_submenu(wxMenu* menu, wxMenu* sub_menu, int id, const wxString& string, const wxString& description, const std::string& icon)
{
    if (id == wxID_ANY)
        id = wxNewId();

    wxMenuItem* item = new wxMenuItem(menu, id, string, description);
    if (!icon.empty())
        item->SetBitmap(wxBitmap(Slic3r::var(icon), wxBITMAP_TYPE_PNG));

    item->SetSubMenu(sub_menu);
    menu->Append(item);

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

// ----------------------------------------------------------------------------
// ***  PrusaCollapsiblePane  ***    
// ----------------------------------------------------------------------------
void PrusaCollapsiblePane::OnStateChange(const wxSize& sz)
{
#ifdef __WXOSX__
	wxCollapsiblePane::OnStateChange(sz);
#else
	SetSize(sz);

	if (this->HasFlag(wxCP_NO_TLW_RESIZE))
	{
		// the user asked to explicitly handle the resizing itself...
		return;
	}

	auto top = GetParent(); //right_panel
	if (!top)
		return;

	wxSizer *sizer = top->GetSizer();
	if (!sizer)
		return;

	const wxSize newBestSize = sizer->ComputeFittingClientSize(top);
	top->SetMinClientSize(newBestSize);

	wxWindowUpdateLocker noUpdates_p(top->GetParent());
	// we shouldn't attempt to resize a maximized window, whatever happens
	// 	if (!top->IsMaximized())
	// 		top->SetClientSize(newBestSize);
	top->GetParent()->Layout();
	top->Refresh();
#endif //__WXOSX__
}

// ----------------------------------------------------------------------------
// ***  PrusaCollapsiblePaneMSW  ***    used only #ifdef __WXMSW__
// ----------------------------------------------------------------------------
#ifdef __WXMSW__
bool PrusaCollapsiblePaneMSW::Create(wxWindow *parent, wxWindowID id, const wxString& label, 
	const wxPoint& pos, const wxSize& size, long style, const wxValidator& val, const wxString& name)
{
	if (!wxControl::Create(parent, id, pos, size, style, val, name))
		return false;
	m_pStaticLine = NULL;
	m_strLabel = label;

	// sizer containing the expand button and possibly a static line
	m_sz = new wxBoxSizer(wxHORIZONTAL);

	m_bmp_close.LoadFile(Slic3r::GUI::from_u8(Slic3r::var("disclosure_triangle_close.png")), wxBITMAP_TYPE_PNG);
	m_bmp_open.LoadFile(Slic3r::GUI::from_u8(Slic3r::var("disclosure_triangle_open.png")), wxBITMAP_TYPE_PNG);

	m_pDisclosureTriangleButton = new wxButton(this, wxID_ANY, m_strLabel, wxPoint(0, 0),
		wxDefaultSize, wxBU_EXACTFIT | wxNO_BORDER);
	UpdateBtnBmp();
	m_pDisclosureTriangleButton->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event)
	{
		if (event.GetEventObject() != m_pDisclosureTriangleButton)
		{
			event.Skip();
			return;
		}

		Collapse(!IsCollapsed());

		// this change was generated by the user - send the event
		wxCollapsiblePaneEvent ev(this, GetId(), IsCollapsed());
		GetEventHandler()->ProcessEvent(ev);
	});

	m_sz->Add(m_pDisclosureTriangleButton, 0, wxLEFT | wxTOP | wxBOTTOM, GetBorder());

	// do not set sz as our sizers since we handle the pane window without using sizers
	m_pPane = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
		wxTAB_TRAVERSAL | wxNO_BORDER, wxT("wxCollapsiblePanePane"));

    wxColour&& clr = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
	m_pDisclosureTriangleButton->SetBackgroundColour(clr);
	this->SetBackgroundColour(clr);
	m_pPane->SetBackgroundColour(clr);

	// start as collapsed:
	m_pPane->Hide();

	return true;
}

void PrusaCollapsiblePaneMSW::UpdateBtnBmp()
{
	if (IsCollapsed())
		m_pDisclosureTriangleButton->SetBitmap(m_bmp_close);
	else{
		m_pDisclosureTriangleButton->SetBitmap(m_bmp_open);
		// To updating button bitmap it's needed to lost focus on this button, so
		// we set focus to mainframe 
		//GetParent()->GetParent()->GetParent()->SetFocus();
		//or to pane
		GetPane()->SetFocus();
	}
	Layout();
}

void PrusaCollapsiblePaneMSW::SetLabel(const wxString &label)
{
	m_strLabel = label;
	m_pDisclosureTriangleButton->SetLabel(m_strLabel);
	Layout();
}

bool PrusaCollapsiblePaneMSW::Layout()
{
	if (!m_pDisclosureTriangleButton || !m_pPane || !m_sz)
		return false;     // we need to complete the creation first!

	wxSize oursz(GetSize());

	// move & resize the button and the static line
	m_sz->SetDimension(0, 0, oursz.GetWidth(), m_sz->GetMinSize().GetHeight());
	m_sz->Layout();

	if (IsExpanded())
	{
		// move & resize the container window
		int yoffset = m_sz->GetSize().GetHeight() + GetBorder();
		m_pPane->SetSize(0, yoffset,
			oursz.x, oursz.y - yoffset);

		// this is very important to make the pane window layout show correctly
		m_pPane->Layout();
	}

	return true;
}

void PrusaCollapsiblePaneMSW::Collapse(bool collapse)
{
	// optimization
	if (IsCollapsed() == collapse)
		return;

	InvalidateBestSize();

	// update our state
	m_pPane->Show(!collapse);

	// update button bitmap
	UpdateBtnBmp();

	OnStateChange(GetBestSize());
}
#endif //__WXMSW__

// *****************************************************************************
// ----------------------------------------------------------------------------
// PrusaObjectDataViewModelNode
// ----------------------------------------------------------------------------

void PrusaObjectDataViewModelNode::set_object_action_icon() {
	m_action_icon = wxBitmap(Slic3r::GUI::from_u8(Slic3r::var("add_object.png")), wxBITMAP_TYPE_PNG);
}
void  PrusaObjectDataViewModelNode::set_part_action_icon() {
	m_action_icon = wxBitmap(Slic3r::GUI::from_u8(Slic3r::var("cog.png")), wxBITMAP_TYPE_PNG);
}

Slic3r::GUI::BitmapCache *m_bitmap_cache = nullptr;
bool PrusaObjectDataViewModelNode::update_settings_digest(const std::vector<std::string>& categories)
{
    if (m_type != itSettings || m_opt_categories == categories)
        return false;

    m_opt_categories = categories;
    m_name = wxEmptyString;
    m_bmp = m_empty_bmp;

    std::map<std::string, wxBitmap>& categories_icon = Slic3r::GUI::wxGetApp().obj_list()->CATEGORY_ICON;

    for (auto& cat : m_opt_categories)
        m_name += cat + "; ";
    if (!m_name.IsEmpty())
        m_name.erase(m_name.Length()-2, 2); // Delete last "; "

    wxBitmap *bmp = m_bitmap_cache->find(m_name.ToStdString());
    if (bmp == nullptr) {
        std::vector<wxBitmap> bmps;
        for (auto& cat : m_opt_categories)
            bmps.emplace_back(categories_icon.find(cat) == categories_icon.end() ?
                              wxNullBitmap : categories_icon.at(cat));
        bmp = m_bitmap_cache->insert(m_name.ToStdString(), bmps);
    }

    m_bmp = *bmp;

    return true;
}

// *****************************************************************************
// ----------------------------------------------------------------------------
// PrusaObjectDataViewModel
// ----------------------------------------------------------------------------

PrusaObjectDataViewModel::PrusaObjectDataViewModel()
{
    m_bitmap_cache = new Slic3r::GUI::BitmapCache;
}

PrusaObjectDataViewModel::~PrusaObjectDataViewModel()
{
    for (auto object : m_objects)
			delete object;
    delete m_bitmap_cache;
    m_bitmap_cache = nullptr;
}

wxDataViewItem PrusaObjectDataViewModel::Add(const wxString &name, const int extruder)
{
    const wxString extruder_str = extruder == 0 ? "default" : wxString::Format("%d", extruder);
	auto root = new PrusaObjectDataViewModelNode(name, extruder_str);
	m_objects.push_back(root);
	// notify control
	wxDataViewItem child((void*)root);
	wxDataViewItem parent((void*)NULL);
	ItemAdded(parent, child);
	return child;
}

wxDataViewItem PrusaObjectDataViewModel::AddVolumeChild(const wxDataViewItem &parent_item,
													const wxString &name,
                                                    const int volume_type,
                                                    const int extruder/* = 0*/,
                                                    const bool create_frst_child/* = true*/)
{
	PrusaObjectDataViewModelNode *root = (PrusaObjectDataViewModelNode*)parent_item.GetID();
	if (!root) return wxDataViewItem(0);

    wxString extruder_str = extruder == 0 ? "default" : wxString::Format("%d", extruder);

    // because of istance_root is a last item of the object
    int insert_position = root->GetChildCount() - 1;
    if (insert_position < 0 || root->GetNthChild(insert_position)->m_type != itInstanceRoot)
        insert_position = -1;

    if (create_frst_child && root->m_volumes_cnt == 0)
	{
		const auto node = new PrusaObjectDataViewModelNode(root, root->m_name, *m_volume_bmps[0], extruder_str, 0);
        insert_position < 0 ? root->Append(node) : root->Insert(node, insert_position);
		// notify control
		const wxDataViewItem child((void*)node);
		ItemAdded(parent_item, child);

        root->m_volumes_cnt++;
        if (insert_position > 0) insert_position++;
	}

    const auto node = new PrusaObjectDataViewModelNode(root, name, *m_volume_bmps[volume_type], extruder_str, root->m_volumes_cnt);
    insert_position < 0 ? root->Append(node) : root->Insert(node, insert_position);
	// notify control
	const wxDataViewItem child((void*)node);
    ItemAdded(parent_item, child);
    root->m_volumes_cnt++;

	return child;
}

wxDataViewItem PrusaObjectDataViewModel::AddSettingsChild(const wxDataViewItem &parent_item)
{
    PrusaObjectDataViewModelNode *root = (PrusaObjectDataViewModelNode*)parent_item.GetID();
    if (!root) return wxDataViewItem(0);

    const auto node = new PrusaObjectDataViewModelNode(root, itSettings);
    root->Insert(node, 0);
    // notify control
    const wxDataViewItem child((void*)node);
    ItemAdded(parent_item, child);
    return child;
}

int get_istances_root_idx(PrusaObjectDataViewModelNode *parent_node)
{
    // because of istance_root is a last item of the object
    const int inst_root_idx = parent_node->GetChildCount()-1;

    if (inst_root_idx < 0 || parent_node->GetNthChild(inst_root_idx)->m_type == itInstanceRoot) 
        return inst_root_idx;
    
    return -1;
}

wxDataViewItem PrusaObjectDataViewModel::AddInstanceChild(const wxDataViewItem &parent_item, size_t num)
{
    PrusaObjectDataViewModelNode *parent_node = (PrusaObjectDataViewModelNode*)parent_item.GetID();
    if (!parent_node) return wxDataViewItem(0);

    // Check and create/get instances root node
    const int inst_root_id = get_istances_root_idx(parent_node);

    PrusaObjectDataViewModelNode *inst_root_node = inst_root_id < 0 ? 
                                                   new PrusaObjectDataViewModelNode(parent_node, itInstanceRoot) :
                                                   parent_node->GetNthChild(inst_root_id);
    const wxDataViewItem inst_root_item((void*)inst_root_node);

    if (inst_root_id < 0) {
        parent_node->Append(inst_root_node);
        // notify control
        ItemAdded(parent_item, inst_root_item);
//         if (num == 1) num++;
    }

    // Add instance nodes
    PrusaObjectDataViewModelNode *instance_node = nullptr;    
    size_t counter = 0;
    while (counter < num) {
        instance_node = new PrusaObjectDataViewModelNode(inst_root_node, itInstance);
        inst_root_node->Append(instance_node);
        // notify control
        const wxDataViewItem instance_item((void*)instance_node);
        ItemAdded(inst_root_item, instance_item);
        ++counter;
    }

    return wxDataViewItem((void*)instance_node);
}

wxDataViewItem PrusaObjectDataViewModel::Delete(const wxDataViewItem &item)
{
	auto ret_item = wxDataViewItem(0);
	PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();
	if (!node)      // happens if item.IsOk()==false
		return ret_item;

	auto node_parent = node->GetParent();
	wxDataViewItem parent(node_parent);

	// first remove the node from the parent's array of children;
	// NOTE: MyObjectTreeModelNodePtrArray is only an array of _pointers_
	//       thus removing the node from it doesn't result in freeing it
	if (node_parent) {
        if (node->m_type == itInstanceRoot)
        {
            for (int i = node->GetChildCount() - 1; i > 0; i--)
                Delete(wxDataViewItem(node->GetNthChild(i)));
            return parent;
        }

		auto id = node_parent->GetChildren().Index(node);
        auto idx = node->GetIdx();


        if (node->m_type == itVolume) {
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

            PrusaObjectDataViewModelNode *last_instance_node = node_parent->GetNthChild(0);
            node_parent->GetChildren().Remove(last_instance_node);
            delete last_instance_node;
            ItemDeleted(parent, wxDataViewItem(last_instance_node));

            PrusaObjectDataViewModelNode *obj_node = node_parent->GetParent();
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
        if (node_parent->GetChildCount() <= 3)
        {
            int vol_cnt = 0;
            int vol_idx = 0;
            for (int i = 0; i < node_parent->GetChildCount(); ++i) {
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

                PrusaObjectDataViewModelNode *last_child_node = node_parent->GetNthChild(vol_idx);
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
		auto id = it - m_objects.begin();
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

wxDataViewItem PrusaObjectDataViewModel::DeleteLastInstance(const wxDataViewItem &parent_item, size_t num)
{
    auto ret_item = wxDataViewItem(0);
    PrusaObjectDataViewModelNode *parent_node = (PrusaObjectDataViewModelNode*)parent_item.GetID();
    if (!parent_node) return ret_item;

    const int inst_root_id = get_istances_root_idx(parent_node);
    if (inst_root_id < 0) return ret_item;

    wxDataViewItemArray items;
    PrusaObjectDataViewModelNode *inst_root_node = parent_node->GetNthChild(inst_root_id);
    const wxDataViewItem inst_root_item((void*)inst_root_node);

    const int inst_cnt = inst_root_node->GetChildCount();
    const bool delete_inst_root_item = inst_cnt - num < 2 ? true : false;

    int stop = delete_inst_root_item ? 0 : inst_cnt - num;
    for (int i = inst_cnt - 1; i >= stop;--i) {
        PrusaObjectDataViewModelNode *last_instance_node = inst_root_node->GetNthChild(i);
        inst_root_node->GetChildren().Remove(last_instance_node);
        delete last_instance_node;
        ItemDeleted(inst_root_item, wxDataViewItem(last_instance_node));
    }

    if (delete_inst_root_item) {
        ret_item = parent_item;
        parent_node->GetChildren().Remove(inst_root_node);
        ItemDeleted(parent_item, inst_root_item);
#ifndef __WXGTK__
        if (parent_node->GetChildCount() == 0)
            parent_node->m_container = false;
#endif //__WXGTK__
    }

    return ret_item;
}

void PrusaObjectDataViewModel::DeleteAll()
{
	while (!m_objects.empty())
	{
		auto object = m_objects.back();
// 		object->RemoveAllChildren();
		Delete(wxDataViewItem(object));	
	}
}

void PrusaObjectDataViewModel::DeleteChildren(wxDataViewItem& parent)
{
    PrusaObjectDataViewModelNode *root = (PrusaObjectDataViewModelNode*)parent.GetID();
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

void PrusaObjectDataViewModel::DeleteVolumeChildren(wxDataViewItem& parent)
{
    PrusaObjectDataViewModelNode *root = (PrusaObjectDataViewModelNode*)parent.GetID();
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

void PrusaObjectDataViewModel::DeleteSettings(const wxDataViewItem& parent)
{
    PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)parent.GetID();
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

wxDataViewItem PrusaObjectDataViewModel::GetItemById(int obj_idx)
{
	if (obj_idx >= m_objects.size())
	{
		printf("Error! Out of objects range.\n");
		return wxDataViewItem(0);
	}
	return wxDataViewItem(m_objects[obj_idx]);
}


wxDataViewItem PrusaObjectDataViewModel::GetItemByVolumeId(int obj_idx, int volume_idx)
{
	if (obj_idx >= m_objects.size() || obj_idx < 0) {
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

wxDataViewItem PrusaObjectDataViewModel::GetItemByInstanceId(int obj_idx, int inst_idx)
{
    if (obj_idx >= m_objects.size() || obj_idx < 0) {
        printf("Error! Out of objects range.\n");
        return wxDataViewItem(0);
    }

    auto instances_item = GetInstanceRootItem(wxDataViewItem(m_objects[obj_idx]));
    if (!instances_item)
        return wxDataViewItem(0);

    auto parent = (PrusaObjectDataViewModelNode*)instances_item.GetID();;
    for (size_t i = 0; i < parent->GetChildCount(); i++)
        if (parent->GetNthChild(i)->m_idx == inst_idx)
            return wxDataViewItem(parent->GetNthChild(i));

    return wxDataViewItem(0);
}

int PrusaObjectDataViewModel::GetIdByItem(const wxDataViewItem& item) const
{
	wxASSERT(item.IsOk());

	PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();
	auto it = find(m_objects.begin(), m_objects.end(), node);
	if (it == m_objects.end())
		return -1;

	return it - m_objects.begin();
}

int PrusaObjectDataViewModel::GetIdByItemAndType(const wxDataViewItem& item, const ItemType type) const
{
	wxASSERT(item.IsOk());

	PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();
	if (!node || node->m_type != type)
		return -1;
	return node->GetIdx();
}

int PrusaObjectDataViewModel::GetObjectIdByItem(const wxDataViewItem& item) const
{
    return GetIdByItem(GetTopParent(item));
}

int PrusaObjectDataViewModel::GetVolumeIdByItem(const wxDataViewItem& item) const
{
    return GetIdByItemAndType(item, itVolume);
}

int PrusaObjectDataViewModel::GetInstanceIdByItem(const wxDataViewItem& item) const 
{
    return GetIdByItemAndType(item, itInstance);
}

void PrusaObjectDataViewModel::GetItemInfo(const wxDataViewItem& item, ItemType& type, int& obj_idx, int& idx)
{
    wxASSERT(item.IsOk());
    type = itUndef;

    PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();
    if (!node || node->GetIdx() <-1 || node->GetIdx() ==-1 && !(node->GetType() & (itObject | itSettings | itInstanceRoot)))
        return;

    idx = node->GetIdx();
    type = node->GetType();

    PrusaObjectDataViewModelNode *parent_node = node->GetParent();
    if (!parent_node) return;
    if (type == itInstance)
        parent_node = node->GetParent()->GetParent();
    if (!parent_node || parent_node->m_type != itObject) { type = itUndef; return; }

    auto it = find(m_objects.begin(), m_objects.end(), parent_node);
    if (it != m_objects.end())
        obj_idx = it - m_objects.begin();
    else
        type = itUndef;
}

int PrusaObjectDataViewModel::GetRowByItem(const wxDataViewItem& item) const
{
    if (m_objects.empty())
        return -1;

    int row_num = 0;
    
    for (int i = 0; i < m_objects.size(); i++)
    {
        row_num++;
        if (item == wxDataViewItem(m_objects[i]))
            return row_num;

        for (int j = 0; j < m_objects[i]->GetChildCount(); j++)
        {
            row_num++;
            PrusaObjectDataViewModelNode* cur_node = m_objects[i]->GetNthChild(j);
            if (item == wxDataViewItem(cur_node))
                return row_num;

            if (cur_node->m_type == itVolume && cur_node->GetChildCount() == 1)
                row_num++;
            if (cur_node->m_type == itInstanceRoot)
            {
                row_num++;
                for (int t = 0; t < cur_node->GetChildCount(); t++)
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

wxString PrusaObjectDataViewModel::GetName(const wxDataViewItem &item) const
{
	PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();
	if (!node)      // happens if item.IsOk()==false
		return wxEmptyString;

	return node->m_name;
}

wxBitmap& PrusaObjectDataViewModel::GetBitmap(const wxDataViewItem &item) const
{
    PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();
    return node->m_bmp;
}

void PrusaObjectDataViewModel::GetValue(wxVariant &variant, const wxDataViewItem &item, unsigned int col) const
{
	wxASSERT(item.IsOk());

	PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();
	switch (col)
	{
	case 0:
        variant << PrusaDataViewBitmapText(node->m_name, node->m_bmp);
		break;
	case 1:
		variant = node->m_extruder;
		break;
	case 2:
		variant << node->m_action_icon;
		break;
	default:
		;
	}
}

bool PrusaObjectDataViewModel::SetValue(const wxVariant &variant, const wxDataViewItem &item, unsigned int col)
{
	wxASSERT(item.IsOk());

	PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();
	return node->SetValue(variant, col);
}

bool PrusaObjectDataViewModel::SetValue(const wxVariant &variant, const int item_idx, unsigned int col)
{
	if (item_idx < 0 || item_idx >= m_objects.size())
		return false;

	return m_objects[item_idx]->SetValue(variant, col);
}
/*
wxDataViewItem PrusaObjectDataViewModel::MoveChildUp(const wxDataViewItem &item)
{
	auto ret_item = wxDataViewItem(0);
	wxASSERT(item.IsOk());
	PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();
	if (!node)      // happens if item.IsOk()==false
		return ret_item;

	auto node_parent = node->GetParent();
	if (!node_parent) // If isn't part, but object
		return ret_item;

	auto volume_id = node->GetVolumeId();
	if (0 < volume_id && volume_id < node_parent->GetChildCount()) {
		node_parent->SwapChildrens(volume_id - 1, volume_id);
		ret_item = wxDataViewItem(node_parent->GetNthChild(volume_id - 1));
		ItemChanged(item);
		ItemChanged(ret_item);
	}
	else
		ret_item = wxDataViewItem(node_parent->GetNthChild(0));
	return ret_item;
}

wxDataViewItem PrusaObjectDataViewModel::MoveChildDown(const wxDataViewItem &item)
{
	auto ret_item = wxDataViewItem(0);
	wxASSERT(item.IsOk());
	PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();
	if (!node)      // happens if item.IsOk()==false
		return ret_item;

	auto node_parent = node->GetParent();
	if (!node_parent) // If isn't part, but object
		return ret_item;

	auto volume_id = node->GetVolumeId();
	if (0 <= volume_id && volume_id+1 < node_parent->GetChildCount()) {
		node_parent->SwapChildrens(volume_id + 1, volume_id);
		ret_item = wxDataViewItem(node_parent->GetNthChild(volume_id + 1));
		ItemChanged(item);
		ItemChanged(ret_item);
	}
	else
		ret_item = wxDataViewItem(node_parent->GetNthChild(node_parent->GetChildCount()-1));
	return ret_item;
}
*/
wxDataViewItem PrusaObjectDataViewModel::ReorganizeChildren(int current_volume_id, int new_volume_id, const wxDataViewItem &parent)
{
    auto ret_item = wxDataViewItem(0);
    if (current_volume_id == new_volume_id)
        return ret_item;
    wxASSERT(parent.IsOk());
    PrusaObjectDataViewModelNode *node_parent = (PrusaObjectDataViewModelNode*)parent.GetID();
    if (!node_parent)      // happens if item.IsOk()==false
        return ret_item;

    const size_t shift = node_parent->GetChildren().Item(0)->m_type == itSettings ? 1 : 0;

    PrusaObjectDataViewModelNode *deleted_node = node_parent->GetNthChild(current_volume_id+shift);
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

bool PrusaObjectDataViewModel::IsEnabled(const wxDataViewItem &item, unsigned int col) const
{
    wxASSERT(item.IsOk());
    PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();

    // disable extruder selection for the non "itObject|itVolume" item
    return !(col == 1 && node->m_extruder.IsEmpty());
}

wxDataViewItem PrusaObjectDataViewModel::GetParent(const wxDataViewItem &item) const
{
	// the invisible root node has no parent
	if (!item.IsOk())
		return wxDataViewItem(0);

	PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();

	// objects nodes has no parent too
    if (node->m_type == itObject)
		return wxDataViewItem(0);

	return wxDataViewItem((void*)node->GetParent());
}

wxDataViewItem PrusaObjectDataViewModel::GetTopParent(const wxDataViewItem &item) const
{
	// the invisible root node has no parent
	if (!item.IsOk())
		return wxDataViewItem(0);

	PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();
    if (node->m_type == itObject)
        return item;

    PrusaObjectDataViewModelNode *parent_node = node->GetParent();
    while (parent_node->m_type != itObject)
    {
        node = parent_node;
        parent_node = node->GetParent();
    }

    return wxDataViewItem((void*)parent_node);
}

bool PrusaObjectDataViewModel::IsContainer(const wxDataViewItem &item) const
{
	// the invisible root node can have children
	if (!item.IsOk())
		return true;

	PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();
	return node->IsContainer();
}

unsigned int PrusaObjectDataViewModel::GetChildren(const wxDataViewItem &parent, wxDataViewItemArray &array) const
{
	PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)parent.GetID();
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
		PrusaObjectDataViewModelNode *child = node->GetChildren().Item(pos);
		array.Add(wxDataViewItem((void*)child));
	}

	return count;
}

ItemType PrusaObjectDataViewModel::GetItemType(const wxDataViewItem &item) const 
{
    if (!item.IsOk())
        return itUndef;
    PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();
    return node->m_type;
}

wxDataViewItem PrusaObjectDataViewModel::GetItemByType(const wxDataViewItem &parent_item, ItemType type) const 
{
    if (!parent_item.IsOk())
        return wxDataViewItem(0);

    PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)parent_item.GetID();
    if (node->GetChildCount() == 0)
        return wxDataViewItem(0);

    for (int i = 0; i < node->GetChildCount(); i++) {
        if (node->GetNthChild(i)->m_type == type)
            return wxDataViewItem((void*)node->GetNthChild(i));
    }

    return wxDataViewItem(0);
}

wxDataViewItem PrusaObjectDataViewModel::GetSettingsItem(const wxDataViewItem &item) const
{
    return GetItemByType(item, itSettings);
}

wxDataViewItem PrusaObjectDataViewModel::GetInstanceRootItem(const wxDataViewItem &item) const
{
    return GetItemByType(item, itInstanceRoot);
}

bool PrusaObjectDataViewModel::IsSettingsItem(const wxDataViewItem &item) const
{
    if (!item.IsOk())
        return false;
    PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();
    return node->m_type == itSettings;
}



void PrusaObjectDataViewModel::UpdateSettingsDigest(const wxDataViewItem &item, 
                                                    const std::vector<std::string>& categories)
{
    if (!item.IsOk()) return;
    PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();
    if (!node->update_settings_digest(categories))
        return;
    ItemChanged(item);
}

void PrusaObjectDataViewModel::SetVolumeType(const wxDataViewItem &item, const int type)
{
    if (!item.IsOk() || GetItemType(item) != itVolume) 
        return;

    PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();
    node->SetBitmap(*m_volume_bmps[type]);
    ItemChanged(item);
}

//-----------------------------------------------------------------------------
// PrusaDataViewBitmapText
//-----------------------------------------------------------------------------

wxIMPLEMENT_DYNAMIC_CLASS(PrusaDataViewBitmapText, wxObject)

IMPLEMENT_VARIANT_OBJECT(PrusaDataViewBitmapText)

// ---------------------------------------------------------
// PrusaIconTextRenderer
// ---------------------------------------------------------

#if ENABLE_NONCUSTOM_DATA_VIEW_RENDERING
PrusaBitmapTextRenderer::PrusaBitmapTextRenderer(wxDataViewCellMode mode /*= wxDATAVIEW_CELL_EDITABLE*/, 
                                                 int align /*= wxDVR_DEFAULT_ALIGNMENT*/): 
wxDataViewRenderer(wxT("PrusaDataViewBitmapText"), mode, align)
{
    SetMode(mode);
    SetAlignment(align);
}
#endif // ENABLE_NONCUSTOM_DATA_VIEW_RENDERING

bool PrusaBitmapTextRenderer::SetValue(const wxVariant &value)
{
    m_value << value;
    return true;
}

bool PrusaBitmapTextRenderer::GetValue(wxVariant& WXUNUSED(value)) const
{
    return false;
}

#if ENABLE_NONCUSTOM_DATA_VIEW_RENDERING && wxUSE_ACCESSIBILITY
wxString PrusaBitmapTextRenderer::GetAccessibleDescription() const
{
    return m_value.GetText();
}
#endif // wxUSE_ACCESSIBILITY && ENABLE_NONCUSTOM_DATA_VIEW_RENDERING

bool PrusaBitmapTextRenderer::Render(wxRect rect, wxDC *dc, int state)
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

wxSize PrusaBitmapTextRenderer::GetSize() const
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


wxWindow* PrusaBitmapTextRenderer::CreateEditorCtrl(wxWindow* parent, wxRect labelRect, const wxVariant& value)
{
    wxDataViewCtrl* const dv_ctrl = GetOwner()->GetOwner();
    PrusaObjectDataViewModel* const model = dynamic_cast<PrusaObjectDataViewModel*>(dv_ctrl->GetModel());

    if ( !(model->GetItemType(dv_ctrl->GetSelection()) & (itVolume | itObject)) )
        return nullptr;

    PrusaDataViewBitmapText data;
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

bool PrusaBitmapTextRenderer::GetValueFromEditorCtrl(wxWindow* ctrl, wxVariant& value)
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
    GetView()->GetModel()->GetValue(valueOld, m_item, 0); 
    
    PrusaDataViewBitmapText bmpText;
    bmpText << valueOld;

    // But replace the text with the value entered by user.
    bmpText.SetText(text_editor->GetValue());

    value << bmpText;
    return true;
}

// ----------------------------------------------------------------------------
// PrusaDoubleSlider
// ----------------------------------------------------------------------------
PrusaDoubleSlider::PrusaDoubleSlider(wxWindow *parent,
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
#ifndef __WXOSX__ // SetDoubleBuffered exists on Win and Linux/GTK, but is missing on OSX
    SetDoubleBuffered(true);
#endif //__WXOSX__

    m_bmp_thumb_higher = wxBitmap(style == wxSL_HORIZONTAL ? Slic3r::GUI::from_u8(Slic3r::var("right_half_circle.png")) :
                                                             Slic3r::GUI::from_u8(Slic3r::var("up_half_circle.png")), wxBITMAP_TYPE_PNG);
    m_bmp_thumb_lower  = wxBitmap(style == wxSL_HORIZONTAL ? Slic3r::GUI::from_u8(Slic3r::var("left_half_circle.png")) :
                                                             Slic3r::GUI::from_u8(Slic3r::var("down_half_circle.png")), wxBITMAP_TYPE_PNG);
    m_thumb_size = m_bmp_thumb_lower.GetSize();

    m_bmp_add_tick_on  = wxBitmap(Slic3r::GUI::from_u8(Slic3r::var("colorchange_add_on.png")), wxBITMAP_TYPE_PNG);
    m_bmp_add_tick_off = wxBitmap(Slic3r::GUI::from_u8(Slic3r::var("colorchange_add_off.png")), wxBITMAP_TYPE_PNG);
    m_bmp_del_tick_on  = wxBitmap(Slic3r::GUI::from_u8(Slic3r::var("colorchange_delete_on.png")), wxBITMAP_TYPE_PNG);
    m_bmp_del_tick_off = wxBitmap(Slic3r::GUI::from_u8(Slic3r::var("colorchange_delete_off.png")), wxBITMAP_TYPE_PNG);
    m_tick_icon_dim = m_bmp_add_tick_on.GetSize().x;

    m_bmp_one_layer_lock_on    = wxBitmap(Slic3r::GUI::from_u8(Slic3r::var("one_layer_lock_on.png")), wxBITMAP_TYPE_PNG);
    m_bmp_one_layer_lock_off   = wxBitmap(Slic3r::GUI::from_u8(Slic3r::var("one_layer_lock_off.png")), wxBITMAP_TYPE_PNG);
    m_bmp_one_layer_unlock_on  = wxBitmap(Slic3r::GUI::from_u8(Slic3r::var("one_layer_unlock_on.png")), wxBITMAP_TYPE_PNG);
    m_bmp_one_layer_unlock_off = wxBitmap(Slic3r::GUI::from_u8(Slic3r::var("one_layer_unlock_off.png")), wxBITMAP_TYPE_PNG);
    m_lock_icon_dim = m_bmp_one_layer_lock_on.GetSize().x;

    m_selection = ssUndef;

    // slider events
    Bind(wxEVT_PAINT,       &PrusaDoubleSlider::OnPaint,    this);
    Bind(wxEVT_LEFT_DOWN,   &PrusaDoubleSlider::OnLeftDown, this);
    Bind(wxEVT_MOTION,      &PrusaDoubleSlider::OnMotion,   this);
    Bind(wxEVT_LEFT_UP,     &PrusaDoubleSlider::OnLeftUp,   this);
    Bind(wxEVT_MOUSEWHEEL,  &PrusaDoubleSlider::OnWheel,    this);
    Bind(wxEVT_ENTER_WINDOW,&PrusaDoubleSlider::OnEnterWin, this);
    Bind(wxEVT_LEAVE_WINDOW,&PrusaDoubleSlider::OnLeaveWin, this);
    Bind(wxEVT_KEY_DOWN,    &PrusaDoubleSlider::OnKeyDown,  this);
    Bind(wxEVT_KEY_UP,      &PrusaDoubleSlider::OnKeyUp,    this);
    Bind(wxEVT_RIGHT_DOWN,  &PrusaDoubleSlider::OnRightDown,this);
    Bind(wxEVT_RIGHT_UP,    &PrusaDoubleSlider::OnRightUp,  this);

    // control's view variables
    SLIDER_MARGIN     = 4 + (style == wxSL_HORIZONTAL ? m_bmp_thumb_higher.GetWidth() : m_bmp_thumb_higher.GetHeight());

    DARK_ORANGE_PEN   = wxPen(wxColour(253, 84, 2));
    ORANGE_PEN        = wxPen(wxColour(253, 126, 66));
    LIGHT_ORANGE_PEN  = wxPen(wxColour(254, 177, 139));

    DARK_GREY_PEN     = wxPen(wxColour(128, 128, 128));
    GREY_PEN          = wxPen(wxColour(164, 164, 164));
    LIGHT_GREY_PEN    = wxPen(wxColour(204, 204, 204));

    line_pens = { &DARK_GREY_PEN, &GREY_PEN, &LIGHT_GREY_PEN };
    segm_pens = { &DARK_ORANGE_PEN, &ORANGE_PEN, &LIGHT_ORANGE_PEN };
}

int PrusaDoubleSlider::GetActiveValue() const
{
    return m_selection == ssLower ?
    m_lower_value : m_selection == ssHigher ?
                m_higher_value : -1;
}

wxSize PrusaDoubleSlider::DoGetBestSize() const
{
    const wxSize size = wxControl::DoGetBestSize();
    if (size.x > 1 && size.y > 1)
        return size;
    const int new_size = is_horizontal() ? 80 : 120;
    return wxSize(new_size, new_size);
}

void PrusaDoubleSlider::SetLowerValue(const int lower_val)
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

void PrusaDoubleSlider::SetHigherValue(const int higher_val)
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

void PrusaDoubleSlider::SetSelectionSpan(const int lower_val, const int higher_val)
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

void PrusaDoubleSlider::SetMaxValue(const int max_value)
{
    m_max_value = max_value;
    Refresh();
    Update();
}

void PrusaDoubleSlider::draw_scroll_line(wxDC& dc, const int lower_pos, const int higher_pos)
{
    int width;
    int height;
    get_size(&width, &height);

    wxCoord line_beg_x = is_horizontal() ? SLIDER_MARGIN : width*0.5 - 1;
    wxCoord line_beg_y = is_horizontal() ? height*0.5 - 1 : SLIDER_MARGIN;
    wxCoord line_end_x = is_horizontal() ? width - SLIDER_MARGIN + 1 : width*0.5 - 1;
    wxCoord line_end_y = is_horizontal() ? height*0.5 - 1 : height - SLIDER_MARGIN + 1;

    wxCoord segm_beg_x = is_horizontal() ? lower_pos : width*0.5 - 1;
    wxCoord segm_beg_y = is_horizontal() ? height*0.5 - 1 : lower_pos-1;
    wxCoord segm_end_x = is_horizontal() ? higher_pos : width*0.5 - 1;
    wxCoord segm_end_y = is_horizontal() ? height*0.5 - 1 : higher_pos-1;

    for (int id = 0; id < line_pens.size(); id++)
    {
        dc.SetPen(*line_pens[id]);
        dc.DrawLine(line_beg_x, line_beg_y, line_end_x, line_end_y);
        dc.SetPen(*segm_pens[id]);
        dc.DrawLine(segm_beg_x, segm_beg_y, segm_end_x, segm_end_y);
        if (is_horizontal())
            line_beg_y = line_end_y = segm_beg_y = segm_end_y += 1;
        else
            line_beg_x = line_end_x = segm_beg_x = segm_end_x += 1;
    }
}

double PrusaDoubleSlider::get_scroll_step()
{
    const wxSize sz = get_size();
    const int& slider_len = m_style == wxSL_HORIZONTAL ? sz.x : sz.y;
    return double(slider_len - SLIDER_MARGIN * 2) / (m_max_value - m_min_value);
}

// get position on the slider line from entered value
wxCoord PrusaDoubleSlider::get_position_from_value(const int value)
{
    const double step = get_scroll_step();
    const int val = is_horizontal() ? value : m_max_value - value;
    return wxCoord(SLIDER_MARGIN + int(val*step + 0.5));
}

wxSize PrusaDoubleSlider::get_size()
{
    int w, h;
    get_size(&w, &h);
    return wxSize(w, h);
}

void PrusaDoubleSlider::get_size(int *w, int *h)
{
    GetSize(w, h);
    is_horizontal() ? *w -= m_lock_icon_dim : *h -= m_lock_icon_dim;
}

double PrusaDoubleSlider::get_double_value(const SelectedSlider& selection)
{
    if (m_values.empty() || m_lower_value<0)
        return 0.0;
    if (m_values.size() <= m_higher_value) {
        correct_higher_value();
        return m_values.back().second;
    }
    return m_values[selection == ssLower ? m_lower_value : m_higher_value].second;
}

std::vector<double> PrusaDoubleSlider::GetTicksValues() const
{
    std::vector<double> values;

    if (!m_values.empty())
        for (auto tick : m_ticks)
            values.push_back(m_values[tick].second);

    return values;
}

void PrusaDoubleSlider::SetTicksValues(const std::vector<double>& heights)
{
    if (m_values.empty())
        return;

    m_ticks.clear();
    unsigned int i = 0;
    for (auto h : heights) {
        while (i < m_values.size() && m_values[i].second - 1e-6 < h)
            ++i;
        if (i == m_values.size())
            return;
        m_ticks.insert(i-1);
    }

}

void PrusaDoubleSlider::get_lower_and_higher_position(int& lower_pos, int& higher_pos)
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

void PrusaDoubleSlider::draw_focus_rect()
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

void PrusaDoubleSlider::render()
{
    SetBackgroundColour(GetParent()->GetBackgroundColour());
    draw_focus_rect();

    wxPaintDC dc(this);
    wxFont font = dc.GetFont();
    const wxFont smaller_font = font.Smaller();
    dc.SetFont(smaller_font);

    const wxCoord lower_pos = get_position_from_value(m_lower_value);
    const wxCoord higher_pos = get_position_from_value(m_higher_value);

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

    //draw color print ticks
    draw_one_layer_icon(dc);
}

void PrusaDoubleSlider::draw_action_icon(wxDC& dc, const wxPoint pt_beg, const wxPoint pt_end)
{
    const int tick = m_selection == ssLower ? m_lower_value : m_higher_value;
    wxBitmap* icon = m_is_action_icon_focesed ? &m_bmp_add_tick_off : &m_bmp_add_tick_on;
    if (m_ticks.find(tick) != m_ticks.end())
        icon = m_is_action_icon_focesed ? &m_bmp_del_tick_off : &m_bmp_del_tick_on;

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

void PrusaDoubleSlider::draw_info_line_with_icon(wxDC& dc, const wxPoint& pos, const SelectedSlider selection)
{
    if (m_selection == selection) {
        //draw info line
        dc.SetPen(DARK_ORANGE_PEN);
        const wxPoint pt_beg = is_horizontal() ? wxPoint(pos.x, pos.y - m_thumb_size.y) : wxPoint(pos.x - m_thumb_size.x, pos.y - 1);
        const wxPoint pt_end = is_horizontal() ? wxPoint(pos.x, pos.y + m_thumb_size.y) : wxPoint(pos.x + m_thumb_size.x, pos.y - 1);
        dc.DrawLine(pt_beg, pt_end);

        //draw action icon
        if (m_is_enabled_tick_manipulation)
            draw_action_icon(dc, pt_beg, pt_end);
    }
}

wxString PrusaDoubleSlider::get_label(const SelectedSlider& selection) const
{
    const int value = selection == ssLower ? m_lower_value : m_higher_value;

    if (m_label_koef == 1.0 && m_values.empty())
        return wxString::Format("%d", value);
    if (value >= m_values.size())
        return "ErrVal";

    const wxString str = m_values.empty() ? 
                         wxNumberFormatter::ToString(m_label_koef*value, 2, wxNumberFormatter::Style_None) :
                         wxNumberFormatter::ToString(m_values[value].second, 2, wxNumberFormatter::Style_None);
    return wxString::Format("%s\n(%d)", str, m_values.empty() ? value : m_values[value].first);
}

void PrusaDoubleSlider::draw_thumb_text(wxDC& dc, const wxPoint& pos, const SelectedSlider& selection) const
{
    if ((m_is_one_layer || m_higher_value==m_lower_value) && selection != m_selection || !selection) 
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

void PrusaDoubleSlider::draw_thumb_item(wxDC& dc, const wxPoint& pos, const SelectedSlider& selection)
{
    wxCoord x_draw, y_draw;
    if (selection == ssLower) {
        if (is_horizontal()) {
            x_draw = pos.x - m_thumb_size.x;
            y_draw = pos.y - int(0.5*m_thumb_size.y);
        }
        else {
            x_draw = pos.x - int(0.5*m_thumb_size.x);
            y_draw = pos.y;
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
    dc.DrawBitmap(selection == ssLower ? m_bmp_thumb_lower : m_bmp_thumb_higher, x_draw, y_draw);

    // Update thumb rect
    update_thumb_rect(x_draw, y_draw, selection);
}

void PrusaDoubleSlider::draw_thumb(wxDC& dc, const wxCoord& pos_coord, const SelectedSlider& selection)
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

void PrusaDoubleSlider::draw_thumbs(wxDC& dc, const wxCoord& lower_pos, const wxCoord& higher_pos)
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

void PrusaDoubleSlider::draw_ticks(wxDC& dc)
{
    dc.SetPen(m_is_enabled_tick_manipulation ? DARK_GREY_PEN : LIGHT_GREY_PEN );
    int height, width;
    get_size(&width, &height);
    const wxCoord mid = is_horizontal() ? 0.5*height : 0.5*width;
    for (auto tick : m_ticks)
    {
        const wxCoord pos = get_position_from_value(tick);

        is_horizontal() ?   dc.DrawLine(pos, mid-14, pos, mid-9) :
                            dc.DrawLine(mid - 14, pos - 1, mid - 9, pos - 1);
        is_horizontal() ?   dc.DrawLine(pos, mid+14, pos, mid+9) :
                            dc.DrawLine(mid + 14, pos - 1, mid + 9, pos - 1);
    }
}

void PrusaDoubleSlider::draw_one_layer_icon(wxDC& dc)
{
    wxBitmap* icon = m_is_one_layer ?
                     m_is_one_layer_icon_focesed ? &m_bmp_one_layer_lock_off : &m_bmp_one_layer_lock_on :
                     m_is_one_layer_icon_focesed ? &m_bmp_one_layer_unlock_off : &m_bmp_one_layer_unlock_on;

    int width, height;
    get_size(&width, &height);

    wxCoord x_draw, y_draw;
    is_horizontal() ? x_draw = width-2 : x_draw = 0.5*width - 0.5*m_lock_icon_dim;
    is_horizontal() ? y_draw = 0.5*height - 0.5*m_lock_icon_dim : y_draw = height-2;

    dc.DrawBitmap(*icon, x_draw, y_draw);

    //update rect of the lock/unlock icon
    m_rect_one_layer_icon = wxRect(x_draw, y_draw, m_lock_icon_dim, m_lock_icon_dim);
}

void PrusaDoubleSlider::update_thumb_rect(const wxCoord& begin_x, const wxCoord& begin_y, const SelectedSlider& selection)
{
    const wxRect& rect = wxRect(begin_x, begin_y, m_thumb_size.x, m_thumb_size.y);
    if (selection == ssLower)
        m_rect_lower_thumb = rect;
    else
        m_rect_higher_thumb = rect;
}

int PrusaDoubleSlider::get_value_from_position(const wxCoord x, const wxCoord y)
{
    const int height = get_size().y;
    const double step = get_scroll_step();
    
    if (is_horizontal()) 
        return int(double(x - SLIDER_MARGIN) / step + 0.5);
    else 
        return int(m_min_value + double(height - SLIDER_MARGIN - y) / step + 0.5);
}

void PrusaDoubleSlider::detect_selected_slider(const wxPoint& pt, const bool is_mouse_wheel /*= false*/)
{
    if (is_mouse_wheel)
    {
        if (is_horizontal()) {
            m_selection = pt.x <= m_rect_lower_thumb.GetRight() ? ssLower :
                          pt.x >= m_rect_higher_thumb.GetLeft() ? ssHigher : ssUndef;
        }
        else {
            m_selection = pt.y >= m_rect_lower_thumb.GetTop() ? ssLower :
                          pt.y <= m_rect_higher_thumb.GetBottom() ? ssHigher : ssUndef;            
        }
        return;
    }

    m_selection = is_point_in_rect(pt, m_rect_lower_thumb) ? ssLower :
                  is_point_in_rect(pt, m_rect_higher_thumb) ? ssHigher : ssUndef;
}

bool PrusaDoubleSlider::is_point_in_rect(const wxPoint& pt, const wxRect& rect)
{
    if (rect.GetLeft() <= pt.x && pt.x <= rect.GetRight() && 
        rect.GetTop()  <= pt.y && pt.y <= rect.GetBottom())
        return true;
    return false;
}

int PrusaDoubleSlider::is_point_near_tick(const wxPoint& pt)
{
    for (auto tick : m_ticks) {
        const wxCoord pos = get_position_from_value(tick);

        if (is_horizontal()) {
            if (pos - 4 <= pt.x && pt.x <= pos + 4)
                return tick;
        }
        else {
            if (pos - 4 <= pt.y && pt.y <= pos + 4) 
                return tick;
        }
    }
    return -1;
}

void PrusaDoubleSlider::ChangeOneLayerLock()
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

void PrusaDoubleSlider::OnLeftDown(wxMouseEvent& event)
{
    this->CaptureMouse();
    wxClientDC dc(this);
    wxPoint pos = event.GetLogicalPosition(dc);
    if (is_point_in_rect(pos, m_rect_tick_action) && m_is_enabled_tick_manipulation) {
        action_tick(taOnIcon);
        return;
    }

    m_is_left_down = true;
    if (is_point_in_rect(pos, m_rect_one_layer_icon)) {
        m_is_one_layer = !m_is_one_layer;
        if (!m_is_one_layer) {
            SetLowerValue(m_min_value);
            SetHigherValue(m_max_value);
        }
        m_selection == ssLower ? correct_lower_value() : correct_higher_value();
        if (!m_selection) m_selection = ssHigher;
    }
    else
        detect_selected_slider(pos);

    if (!m_selection && m_is_enabled_tick_manipulation) {
        const auto tick = is_point_near_tick(pos);
        if (tick >= 0)
        {
            if (abs(tick - m_lower_value) < abs(tick - m_higher_value)) {
                SetLowerValue(tick);
                correct_lower_value();
                m_selection = ssLower;
            }
            else {
                SetHigherValue(tick);
                correct_higher_value();
                m_selection = ssHigher;
            }
        }
    }

    Refresh();
    Update();
    event.Skip();
}

void PrusaDoubleSlider::correct_lower_value()
{
    if (m_lower_value < m_min_value)
        m_lower_value = m_min_value;
    else if (m_lower_value > m_max_value)
        m_lower_value = m_max_value;
    
    if (m_lower_value >= m_higher_value && m_lower_value <= m_max_value || m_is_one_layer)
        m_higher_value = m_lower_value;
}

void PrusaDoubleSlider::correct_higher_value()
{
    if (m_higher_value > m_max_value)
        m_higher_value = m_max_value;
    else if (m_higher_value < m_min_value)
        m_higher_value = m_min_value;
    
    if (m_higher_value <= m_lower_value && m_higher_value >= m_min_value || m_is_one_layer)
        m_lower_value = m_higher_value;
}

void PrusaDoubleSlider::OnMotion(wxMouseEvent& event)
{
    bool action = false;

    const wxClientDC dc(this);
    const wxPoint pos = event.GetLogicalPosition(dc);
    m_is_one_layer_icon_focesed = is_point_in_rect(pos, m_rect_one_layer_icon);
    if (!m_is_left_down && !m_is_one_layer) {
        m_is_action_icon_focesed = is_point_in_rect(pos, m_rect_tick_action);
    }
    else if (m_is_left_down || m_is_right_down) {
        if (m_selection == ssLower) {
            m_lower_value = get_value_from_position(pos.x, pos.y);
            correct_lower_value();
            action = true;
        }
        else if (m_selection == ssHigher) {
            m_higher_value = get_value_from_position(pos.x, pos.y);
            correct_higher_value();
            action = true;
        }
    }
    Refresh();
    Update();
    event.Skip();

    if (action)
    {
        wxCommandEvent e(wxEVT_SCROLL_CHANGED);
        e.SetEventObject(this);
        ProcessWindowEvent(e);
    }
}

void PrusaDoubleSlider::OnLeftUp(wxMouseEvent& event)
{
    this->ReleaseMouse();
    m_is_left_down = false;
    Refresh();
    Update();
    event.Skip();

    wxCommandEvent e(wxEVT_SCROLL_CHANGED);
    e.SetEventObject(this);
    ProcessWindowEvent(e);
}

void PrusaDoubleSlider::enter_window(wxMouseEvent& event, const bool enter)
{
    m_is_focused = enter;
    Refresh();
    Update();
    event.Skip();
}

// "condition" have to be true for:
//    -  value increase (if wxSL_VERTICAL)
//    -  value decrease (if wxSL_HORIZONTAL) 
void PrusaDoubleSlider::move_current_thumb(const bool condition)
{
    m_is_one_layer = wxGetKeyState(WXK_CONTROL);
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

void PrusaDoubleSlider::action_tick(const TicksAction action)
{
    if (m_selection == ssUndef)
        return;

    const int tick = m_selection == ssLower ? m_lower_value : m_higher_value;

    if (action == taOnIcon) {
        if (!m_ticks.insert(tick).second)
            m_ticks.erase(tick);
    }
    else {
        const auto it = m_ticks.find(tick);
        if (it == m_ticks.end() && action == taAdd)
            m_ticks.insert(tick);
        else if (it != m_ticks.end() && action == taDel)
            m_ticks.erase(tick);
    }

    wxPostEvent(this->GetParent(), wxCommandEvent(wxCUSTOMEVT_TICKSCHANGED));
    Refresh();
    Update();
}

void PrusaDoubleSlider::OnWheel(wxMouseEvent& event)
{
    wxClientDC dc(this);
    wxPoint pos = event.GetLogicalPosition(dc);
    detect_selected_slider(pos, true);

    if (m_selection == ssUndef)
        return;

    move_current_thumb(event.GetWheelRotation() > 0);
}

void PrusaDoubleSlider::OnKeyDown(wxKeyEvent &event)
{
    const int key = event.GetKeyCode();
    if (key == '+' || key == WXK_NUMPAD_ADD)
        action_tick(taAdd);
    else if (key == '-' || key == 390 || key == WXK_DELETE || key == WXK_BACK)
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
}

void PrusaDoubleSlider::OnKeyUp(wxKeyEvent &event)
{
    if (event.GetKeyCode() == WXK_CONTROL)
        m_is_one_layer = false;
    Refresh();
    Update();
    event.Skip();
}

void PrusaDoubleSlider::OnRightDown(wxMouseEvent& event)
{
    this->CaptureMouse();
    const wxClientDC dc(this);
    detect_selected_slider(event.GetLogicalPosition(dc));
    if (!m_selection)
        return;

    if (m_selection == ssLower)
        m_higher_value = m_lower_value;
    else
        m_lower_value = m_higher_value;

    m_is_right_down = m_is_one_layer = true;

    Refresh();
    Update();
    event.Skip();
}

void PrusaDoubleSlider::OnRightUp(wxMouseEvent& event)
{
    this->ReleaseMouse();
    m_is_right_down = m_is_one_layer = false;

    Refresh();
    Update();
    event.Skip();
}


// ----------------------------------------------------------------------------
// PrusaLockButton
// ----------------------------------------------------------------------------

PrusaLockButton::PrusaLockButton(   wxWindow *parent, 
                                    wxWindowID id, 
                                    const wxPoint& pos /*= wxDefaultPosition*/, 
                                    const wxSize& size /*= wxDefaultSize*/):
                                    wxButton(parent, id, wxEmptyString, pos, size, wxBU_EXACTFIT | wxNO_BORDER)
{
    m_bmp_lock_on = wxBitmap(Slic3r::GUI::from_u8(Slic3r::var("one_layer_lock_on.png")), wxBITMAP_TYPE_PNG);
    m_bmp_lock_off = wxBitmap(Slic3r::GUI::from_u8(Slic3r::var("one_layer_lock_off.png")), wxBITMAP_TYPE_PNG);
    m_bmp_unlock_on = wxBitmap(Slic3r::GUI::from_u8(Slic3r::var("one_layer_unlock_on.png")), wxBITMAP_TYPE_PNG);
    m_bmp_unlock_off = wxBitmap(Slic3r::GUI::from_u8(Slic3r::var("one_layer_unlock_off.png")), wxBITMAP_TYPE_PNG);
    m_lock_icon_dim = m_bmp_lock_on.GetSize().x;

#ifdef __WXMSW__
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif // __WXMSW__
    SetBitmap(m_bmp_unlock_on);

    //button events
    Bind(wxEVT_BUTTON,          &PrusaLockButton::OnButton, this);
    Bind(wxEVT_ENTER_WINDOW,    &PrusaLockButton::OnEnterBtn, this);
    Bind(wxEVT_LEAVE_WINDOW,    &PrusaLockButton::OnLeaveBtn, this);
}

void PrusaLockButton::OnButton(wxCommandEvent& event)
{
    m_is_pushed = !m_is_pushed;
    enter_button(true);

    event.Skip();
}

void PrusaLockButton::enter_button(const bool enter)
{
    wxBitmap* icon = m_is_pushed ?
        enter ? &m_bmp_lock_off     : &m_bmp_lock_on :
        enter ? &m_bmp_unlock_off   : &m_bmp_unlock_on;
    SetBitmap(*icon);

    Refresh();
    Update();
}

// ************************************** EXPERIMENTS ***************************************

// *****************************************************************************



