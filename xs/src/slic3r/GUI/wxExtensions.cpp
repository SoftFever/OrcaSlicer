#include "wxExtensions.hpp"

#include "GUI.hpp"
#include "../../libslic3r/Utils.hpp"

#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/dcclient.h>

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

	wxColour& clr = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
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

// *****************************************************************************
// ----------------------------------------------------------------------------
// PrusaObjectDataViewModel
// ----------------------------------------------------------------------------

wxDataViewItem PrusaObjectDataViewModel::Add(wxString &name)
{
	auto root = new PrusaObjectDataViewModelNode(name);
	m_objects.push_back(root);
	// notify control
	wxDataViewItem child((void*)root);
	wxDataViewItem parent((void*)NULL);
	ItemAdded(parent, child);
	return child;
}

wxDataViewItem PrusaObjectDataViewModel::Add(wxString &name, int instances_count, int scale)
{
	auto root = new PrusaObjectDataViewModelNode(name, instances_count, scale);
	m_objects.push_back(root);
	// notify control
	wxDataViewItem child((void*)root);
	wxDataViewItem parent((void*)NULL);
	ItemAdded(parent, child);
	return child;
}

wxDataViewItem PrusaObjectDataViewModel::AddChild(	const wxDataViewItem &parent_item,
													const wxString &name,
													const wxIcon& icon,
                                                    const int extruder/* = 0*/,
                                                    const bool create_frst_child/* = true*/)
{
	PrusaObjectDataViewModelNode *root = (PrusaObjectDataViewModelNode*)parent_item.GetID();
	if (!root) return wxDataViewItem(0);

    wxString extruder_str = extruder == 0 ? "default" : wxString::Format("%d", extruder);

    if (root->GetChildren().Count() == 0 && create_frst_child)
	{
		auto icon_solid_mesh = wxIcon(Slic3r::GUI::from_u8(Slic3r::var("object.png")), wxBITMAP_TYPE_PNG);//(Slic3r::var("package.png")), wxBITMAP_TYPE_PNG);
		auto node = new PrusaObjectDataViewModelNode(root, root->m_name, icon_solid_mesh, extruder_str, 0);
		root->Append(node);
		// notify control
		wxDataViewItem child((void*)node);
		ItemAdded(parent_item, child);
	}

	auto volume_id = root->GetChildCount();
	auto node = new PrusaObjectDataViewModelNode(root, name, icon, extruder_str, volume_id);
	root->Append(node);
	// notify control
	wxDataViewItem child((void*)node);
	ItemAdded(parent_item, child);
	return child;
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
	if (node_parent){
		auto id = node_parent->GetChildren().Index(node);
		auto v_id = node->GetVolumeId();
		node_parent->GetChildren().Remove(node);
		if (id > 0){ 
			if(id == node_parent->GetChildCount()) id--;
			ret_item = wxDataViewItem(node_parent->GetChildren().Item(id));
		}

		//update volume_id value for remaining child-nodes
		auto children = node_parent->GetChildren();
		for (size_t i = 0; i < node_parent->GetChildCount(); i++)
		{
			auto volume_id = children[i]->GetVolumeId();
			if (volume_id > v_id)
				children[i]->SetVolumeId(volume_id-1);
		}
	}
	else
	{
		auto it = find(m_objects.begin(), m_objects.end(), node);
		auto id = it - m_objects.begin();
		if (it != m_objects.end())
			m_objects.erase(it);
		if (id > 0){ 
			if(id == m_objects.size()) id--;
			ret_item = wxDataViewItem(m_objects[id]);
		}
	}
	// free the node
	delete node;

	// set m_containet to FALSE if parent has no child
	if (node_parent && node_parent->GetChildCount() == 0){
#ifndef __WXGTK__
		node_parent->m_container = false;
#endif //__WXGTK__
		ret_item = parent;
	}

	// notify control
	ItemDeleted(parent, item);
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

wxDataViewItem PrusaObjectDataViewModel::GetItemById(int obj_idx)
{
	if (obj_idx >= m_objects.size())
	{
		printf("Error! Out of objects range.\n");
		return wxDataViewItem(0);
	}
	return wxDataViewItem(m_objects[obj_idx]);
}


int PrusaObjectDataViewModel::GetIdByItem(wxDataViewItem& item)
{
	wxASSERT(item.IsOk());

	PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();
	auto it = find(m_objects.begin(), m_objects.end(), node);
	if (it == m_objects.end())
		return -1;

	return it - m_objects.begin();
}

int PrusaObjectDataViewModel::GetVolumeIdByItem(wxDataViewItem& item)
{
	wxASSERT(item.IsOk());

	PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();
	if (!node)      // happens if item.IsOk()==false
		return -1;
	return node->GetVolumeId();
}

wxString PrusaObjectDataViewModel::GetName(const wxDataViewItem &item) const
{
	PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();
	if (!node)      // happens if item.IsOk()==false
		return wxEmptyString;

	return node->m_name;
}

wxString PrusaObjectDataViewModel::GetCopy(const wxDataViewItem &item) const
{
	PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();
	if (!node)      // happens if item.IsOk()==false
		return wxEmptyString;

	return node->m_copy;
}

wxString PrusaObjectDataViewModel::GetScale(const wxDataViewItem &item) const
{
	PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();
	if (!node)      // happens if item.IsOk()==false
		return wxEmptyString;

	return node->m_scale;
}

wxIcon& PrusaObjectDataViewModel::GetIcon(const wxDataViewItem &item) const
{
    PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();
    return node->m_icon;
}

void PrusaObjectDataViewModel::GetValue(wxVariant &variant, const wxDataViewItem &item, unsigned int col) const
{
	wxASSERT(item.IsOk());

	PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();
	switch (col)
	{
	case 0:{
		const wxDataViewIconText data(node->m_name, node->m_icon);
		variant << data;
		break;}
	case 1:
		variant = node->m_copy;
		break;
	case 2:
		variant = node->m_scale;
		break;
	case 3:
		variant = node->m_extruder;
		break;
	case 4:
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
	if (0 < volume_id && volume_id < node_parent->GetChildCount()){
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
	if (0 <= volume_id && volume_id+1 < node_parent->GetChildCount()){
		node_parent->SwapChildrens(volume_id + 1, volume_id);
		ret_item = wxDataViewItem(node_parent->GetNthChild(volume_id + 1));
		ItemChanged(item);
		ItemChanged(ret_item);
	}
	else
		ret_item = wxDataViewItem(node_parent->GetNthChild(node_parent->GetChildCount()-1));
	return ret_item;
}

wxDataViewItem PrusaObjectDataViewModel::ReorganizeChildren(int current_volume_id, int new_volume_id, const wxDataViewItem &parent)
{
    auto ret_item = wxDataViewItem(0);
    if (current_volume_id == new_volume_id)
        return ret_item;
    wxASSERT(parent.IsOk());
    PrusaObjectDataViewModelNode *node_parent = (PrusaObjectDataViewModelNode*)parent.GetID();
    if (!node_parent)      // happens if item.IsOk()==false
        return ret_item;

    PrusaObjectDataViewModelNode *deleted_node = node_parent->GetNthChild(current_volume_id);
    node_parent->GetChildren().Remove(deleted_node);
    ItemDeleted(parent, wxDataViewItem(deleted_node));
    node_parent->Insert(deleted_node, new_volume_id);
    ItemAdded(parent, wxDataViewItem(deleted_node));

    //update volume_id value for child-nodes
    auto children = node_parent->GetChildren();
    int id_frst = current_volume_id < new_volume_id ? current_volume_id : new_volume_id;
    int id_last = current_volume_id > new_volume_id ? current_volume_id : new_volume_id;
    for (int id = id_frst; id <= id_last; ++id)
        children[id]->SetVolumeId(id);

    return wxDataViewItem(node_parent->GetNthChild(new_volume_id));
}

// bool MyObjectTreeModel::IsEnabled(const wxDataViewItem &item, unsigned int col) const
// {
// 
// }

wxDataViewItem PrusaObjectDataViewModel::GetParent(const wxDataViewItem &item) const
{
	// the invisible root node has no parent
	if (!item.IsOk())
		return wxDataViewItem(0);

	PrusaObjectDataViewModelNode *node = (PrusaObjectDataViewModelNode*)item.GetID();

	// objects nodes has no parent too
	if (find(m_objects.begin(), m_objects.end(),node) != m_objects.end())
		return wxDataViewItem(0);

	return wxDataViewItem((void*)node->GetParent());
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

// ************************************** EXPERIMENTS ***************************************
PrusaDoubleSlider::PrusaDoubleSlider(   wxWindow *parent,
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
    wxControl(parent, id, pos, size, wxBORDER_NONE),
    m_lower_value(lowerValue), m_higher_value (higherValue), 
    m_min_value(minValue), m_max_value(maxValue),
    m_style(style)
{
    SetDoubleBuffered(true);

    if (m_style != wxSL_HORIZONTAL && m_style != wxSL_VERTICAL)
        m_style = wxSL_HORIZONTAL;

    m_thumb_higher = wxBitmap(style == wxSL_HORIZONTAL ? Slic3r::GUI::from_u8(Slic3r::var("right_half_circle.png")) :
                                                         Slic3r::GUI::from_u8(Slic3r::var("up_half_circle.png")), wxBITMAP_TYPE_PNG);
    m_thumb_lower  = wxBitmap(style == wxSL_HORIZONTAL ? Slic3r::GUI::from_u8(Slic3r::var("left_half_circle.png")) :
                                                         Slic3r::GUI::from_u8(Slic3r::var("down_half_circle.png")), wxBITMAP_TYPE_PNG);
    
    
    m_selection = ssUndef;

    // slider events
    Bind(wxEVT_PAINT,       &PrusaDoubleSlider::OnPaint,    this);
    Bind(wxEVT_LEFT_DOWN,   &PrusaDoubleSlider::OnLeftDown, this);
    Bind(wxEVT_MOTION,      &PrusaDoubleSlider::OnMotion,   this);
    Bind(wxEVT_LEFT_UP,     &PrusaDoubleSlider::OnLeftUp,   this);
    Bind(wxEVT_LEAVE_WINDOW,&PrusaDoubleSlider::OnLeftUp,   this);
    Bind(wxEVT_MOUSEWHEEL,  &PrusaDoubleSlider::OnWheel,    this);

    // control's view variables
    SLIDER_MARGIN     = 2 + (style == wxSL_HORIZONTAL ? m_thumb_higher.GetWidth() : m_thumb_higher.GetHeight());

    DARK_ORANGE_PEN   = wxPen(wxColour(253, 84, 2));
    ORANGE_PEN        = wxPen(wxColour(253, 126, 66));
    LIGHT_ORANGE_PEN  = wxPen(wxColour(254, 177, 139));

    DARK_GREY_PEN     = wxPen(wxColour(128, 128, 128));
    GREY_PEN          = wxPen(wxColour(164, 164, 164));
    LIGHT_GREY_PEN    = wxPen(wxColour(204, 204, 204));

    line_pens = { &DARK_GREY_PEN, &GREY_PEN, &LIGHT_GREY_PEN };
    segm_pens = { &DARK_ORANGE_PEN, &ORANGE_PEN, &LIGHT_ORANGE_PEN };
}

void PrusaDoubleSlider::SetLowerValue(const int lower_val)
{
    m_lower_value = lower_val;
    Refresh();
    Update();
}

void PrusaDoubleSlider::SetHigherValue(const int higher_val)
{
    m_higher_value = higher_val;
    Refresh();
    Update();
}

void PrusaDoubleSlider::draw_scroll_line(wxDC& dc, const int lower_pos, const int higher_pos)
{
    int width;
    int height;
    GetSize(&width, &height);

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
    const wxSize sz = GetSize();
    const int& slider_len = m_style == wxSL_HORIZONTAL ? sz.x : sz.y;
    return double(slider_len - SLIDER_MARGIN * 2) / (m_max_value - m_min_value);
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

void PrusaDoubleSlider::OnPaint(wxPaintEvent& event)
{
    SetBackgroundColour(GetParent()->GetBackgroundColour());

    wxPaintDC dc(this);    
    int width, height;
    GetSize(&width, &height);

    int lower_pos, higher_pos;
    get_lower_and_higher_position(lower_pos, higher_pos);

    // draw line
    draw_scroll_line(dc, lower_pos, higher_pos);

    //lower slider:
    wxPoint pos = is_horizontal() ? wxPoint(lower_pos, height*0.5) : wxPoint(0.5*width, lower_pos);
    draw_lower_thumb(dc, pos);

    //higher slider:
    pos = is_horizontal() ? wxPoint(higher_pos, height*0.5) : wxPoint(0.5*width, higher_pos);
    draw_higher_thumb(dc, pos);
}

void PrusaDoubleSlider::draw_lower_thumb(wxDC& dc, const wxPoint& pos)
{
    // Draw thumb
    wxCoord x_draw, y_draw;
    const wxSize thumb_size = m_thumb_lower.GetSize();
    if (is_horizontal()) {
        x_draw = pos.x - thumb_size.x;
        y_draw = pos.y - int(0.5*thumb_size.y);
    }
    else {
        x_draw = pos.x - int(0.5*thumb_size.x);
        y_draw = pos.y;
    }
    dc.DrawBitmap(m_thumb_lower, x_draw, y_draw);

    // Draw thumb text
    wxCoord text_width, text_height;
    dc.GetTextExtent(wxString::Format("%d", m_lower_value), &text_width, &text_height);
    wxPoint text_pos = is_horizontal() ? wxPoint(pos.x + 1, pos.y + thumb_size.x) :
                                         wxPoint(pos.x + thumb_size.y, pos.y - 1 - text_height);
    dc.DrawText(wxString::Format("%d", m_lower_value), text_pos);

    // Update thumb rect
    m_rect_lower_thumb = wxRect(x_draw, y_draw, thumb_size.x, thumb_size.y);
}


void PrusaDoubleSlider::draw_higher_thumb(wxDC& dc, const wxPoint& pos)
{
    wxCoord x_draw, y_draw;
    const wxSize thumb_size = m_thumb_higher.GetSize();
    if (is_horizontal()) {
        x_draw = pos.x;
        y_draw = pos.y - int(0.5*thumb_size.y);
    }
    else {
        x_draw = pos.x - int(0.5*thumb_size.x);
        y_draw = pos.y - thumb_size.y;
    }
    dc.DrawBitmap(m_thumb_higher, x_draw, y_draw);

    // Draw thumb text
    wxCoord text_width, text_height;
    dc.GetTextExtent(wxString::Format("%d", m_higher_value), &text_width, &text_height);
    wxPoint text_pos = is_horizontal() ? wxPoint(pos.x - text_width-1,                pos.y - thumb_size.x - text_height) :
                                         wxPoint(pos.x - text_width-1 - thumb_size.y, pos.y + 1);
    dc.DrawText(wxString::Format("%d", m_higher_value), text_pos);

    // Update thumb rect
    m_rect_higher_thumb = wxRect(x_draw, y_draw, thumb_size.x, thumb_size.y);
}

int PrusaDoubleSlider::position_to_value(wxDC& dc, const wxCoord x, const wxCoord y)
{
    int width, height;
    dc.GetSize(&width, &height);
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

void PrusaDoubleSlider::OnLeftDown(wxMouseEvent& event)
{
    m_is_left_down = true;
    wxClientDC dc(this);
    wxPoint pos = event.GetLogicalPosition(dc);
    detect_selected_slider(pos);
    event.Skip();
}

void PrusaDoubleSlider::correct_lower_value()
{
    if (m_lower_value < m_min_value)
        m_lower_value = m_min_value;
    else if (m_lower_value >= m_higher_value && m_lower_value <= m_max_value)
        m_higher_value = m_lower_value;
    else if (m_lower_value > m_max_value)
        m_lower_value = m_max_value;
}

void PrusaDoubleSlider::correct_higher_value()
{
    if (m_higher_value > m_max_value)
        m_higher_value = m_max_value;
    else if (m_higher_value <= m_lower_value && m_higher_value >= m_min_value)
        m_lower_value = m_higher_value;
    else if (m_higher_value < m_min_value)
        m_higher_value = m_min_value;
}

void PrusaDoubleSlider::OnMotion(wxMouseEvent& event)
{
    if (!m_is_left_down || m_selection == ssUndef)
        return;

    wxClientDC dc(this);
    wxPoint pos = event.GetLogicalPosition(dc);

    if (m_selection == ssLower) {
        m_lower_value = position_to_value(dc, pos.x, pos.y);
        correct_lower_value();
    }
    else if (m_selection == ssHigher) {
        m_higher_value = position_to_value(dc, pos.x, pos.y);
        correct_higher_value();
    }

    Refresh();
    Update();
    event.Skip();
}

void PrusaDoubleSlider::OnLeftUp(wxMouseEvent& event)
{
    m_is_left_down = false;
    event.Skip();

    wxCommandEvent e(wxEVT_SCROLL_CHANGED);
    e.SetEventObject(this);
    ProcessWindowEvent(e);
}

void PrusaDoubleSlider::OnWheel(wxMouseEvent& event)
{
    wxClientDC dc(this);
    wxPoint pos = event.GetLogicalPosition(dc);
    detect_selected_slider(pos, true);

    if (m_selection == ssUndef)
        return;

    int delta = event.GetWheelRotation() > 0 ? -1 : 1;
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
// *****************************************************************************
