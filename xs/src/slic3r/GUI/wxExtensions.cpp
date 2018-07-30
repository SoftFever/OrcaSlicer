#include "wxExtensions.hpp"

#include "GUI.hpp"
#include "../../libslic3r/Utils.hpp"

#include <wx/sizer.h>
#include <wx/statline.h>

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
													const wxIcon& icon)
{
	PrusaObjectDataViewModelNode *root = (PrusaObjectDataViewModelNode*)parent_item.GetID();
	if (!root) return wxDataViewItem(0);

	if (root->GetChildren().Count() == 0)
	{
		auto icon_solid_mesh = wxIcon(Slic3r::GUI::from_u8(Slic3r::var("object.png")), wxBITMAP_TYPE_PNG);//(Slic3r::var("package.png")), wxBITMAP_TYPE_PNG);
		auto node = new PrusaObjectDataViewModelNode(root, root->m_name, icon_solid_mesh, 0);
		root->Append(node);
		// notify control
		wxDataViewItem child((void*)node);
		ItemAdded(parent_item, child);
	}

	auto volume_id = root->GetChildCount();
	auto node = new PrusaObjectDataViewModelNode(root, name, icon, volume_id);
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
		node_parent->m_container = false;
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
// ----------------------------------------------------------------------------
// MyMusicTreeModel
// ----------------------------------------------------------------------------

MyMusicTreeModel::MyMusicTreeModel()
{
    m_root = new MyMusicTreeModelNode(NULL, "My Music");

    // setup pop music
    m_pop = new MyMusicTreeModelNode(m_root, "Pop music");
    m_pop->Append(
        new MyMusicTreeModelNode(m_pop, "You are not alone", "Michael Jackson", 1995));
    m_pop->Append(
        new MyMusicTreeModelNode(m_pop, "Take a bow", "Madonna", 1994));
    m_root->Append(m_pop);

    // setup classical music
    m_classical = new MyMusicTreeModelNode(m_root, "Classical music");
    m_ninth = new MyMusicTreeModelNode(m_classical, "Ninth symphony",
        "Ludwig van Beethoven", 1824);
    m_classical->Append(m_ninth);
    m_classical->Append(new MyMusicTreeModelNode(m_classical, "German Requiem",
        "Johannes Brahms", 1868));
    m_root->Append(m_classical);

    m_classicalMusicIsKnownToControl = false;
}

wxString MyMusicTreeModel::GetTitle(const wxDataViewItem &item) const
{
    MyMusicTreeModelNode *node = (MyMusicTreeModelNode*)item.GetID();
    if (!node)      // happens if item.IsOk()==false
        return wxEmptyString;

    return node->m_title;
}

wxString MyMusicTreeModel::GetArtist(const wxDataViewItem &item) const
{
    MyMusicTreeModelNode *node = (MyMusicTreeModelNode*)item.GetID();
    if (!node)      // happens if item.IsOk()==false
        return wxEmptyString;

    return node->m_artist;
}

int MyMusicTreeModel::GetYear(const wxDataViewItem &item) const
{
    MyMusicTreeModelNode *node = (MyMusicTreeModelNode*)item.GetID();
    if (!node)      // happens if item.IsOk()==false
        return 2000;

    return node->m_year;
}

void MyMusicTreeModel::AddToClassical(const wxString &title, const wxString &artist,
    unsigned int year)
{
    if (!m_classical)
    {
        wxASSERT(m_root);

        // it was removed: restore it
        m_classical = new MyMusicTreeModelNode(m_root, "Classical music");
        m_root->Append(m_classical);

        // notify control
        wxDataViewItem child((void*)m_classical);
        wxDataViewItem parent((void*)m_root);
        ItemAdded(parent, child);
    }

    // add to the classical music node a new node:
    MyMusicTreeModelNode *child_node =
        new MyMusicTreeModelNode(m_classical, title, artist, year);
    m_classical->Append(child_node);

    // FIXME: what's m_classicalMusicIsKnownToControl for?
    if (m_classicalMusicIsKnownToControl)
    {
        // notify control
        wxDataViewItem child((void*)child_node);
        wxDataViewItem parent((void*)m_classical);
        ItemAdded(parent, child);
    }
}

void MyMusicTreeModel::Delete(const wxDataViewItem &item)
{
    MyMusicTreeModelNode *node = (MyMusicTreeModelNode*)item.GetID();
    if (!node)      // happens if item.IsOk()==false
        return;

    wxDataViewItem parent(node->GetParent());
    if (!parent.IsOk())
    {
        wxASSERT(node == m_root);

        // don't make the control completely empty:
        printf("Cannot remove the root item!");
        return;
    }

    // is the node one of those we keep stored in special pointers?
    if (node == m_pop)
        m_pop = NULL;
    else if (node == m_classical)
        m_classical = NULL;
    else if (node == m_ninth)
        m_ninth = NULL;

    // first remove the node from the parent's array of children;
    // NOTE: MyMusicTreeModelNodePtrArray is only an array of _pointers_
    //       thus removing the node from it doesn't result in freeing it
    node->GetParent()->GetChildren().Remove(node);

    // free the node
    delete node;

    // notify control
    ItemDeleted(parent, item);
}

int MyMusicTreeModel::Compare(const wxDataViewItem &item1, const wxDataViewItem &item2,
    unsigned int column, bool ascending) const
{
    wxASSERT(item1.IsOk() && item2.IsOk());
    // should never happen

    if (IsContainer(item1) && IsContainer(item2))
    {
        wxVariant value1, value2;
        GetValue(value1, item1, 0);
        GetValue(value2, item2, 0);

        wxString str1 = value1.GetString();
        wxString str2 = value2.GetString();
        int res = str1.Cmp(str2);
        if (res) return res;

        // items must be different
        wxUIntPtr litem1 = (wxUIntPtr)item1.GetID();
        wxUIntPtr litem2 = (wxUIntPtr)item2.GetID();

        return litem1 - litem2;
    }

    return wxDataViewModel::Compare(item1, item2, column, ascending);
}

void MyMusicTreeModel::GetValue(wxVariant &variant,
    const wxDataViewItem &item, unsigned int col) const
{
    wxASSERT(item.IsOk());

    MyMusicTreeModelNode *node = (MyMusicTreeModelNode*)item.GetID();
    switch (col)
    {
    case 0:
        variant = node->m_title;
        break;
    case 1:
        variant = node->m_artist;
        break;
    case 2:
        variant = (long)node->m_year;
        break;
    case 3:
        variant = node->m_quality;
        break;
    case 4:
        variant = 80L;  // all music is very 80% popular
        break;
    case 5:
        if (GetYear(item) < 1900)
            variant = "old";
        else
            variant = "new";
        break;

    default:
        printf("MyMusicTreeModel::GetValue: wrong column %d", col);
    }
}

bool MyMusicTreeModel::SetValue(const wxVariant &variant,
    const wxDataViewItem &item, unsigned int col)
{
    wxASSERT(item.IsOk());

    MyMusicTreeModelNode *node = (MyMusicTreeModelNode*)item.GetID();
    switch (col)
    {
    case 0:
        node->m_title = variant.GetString();
        return true;
    case 1:
        node->m_artist = variant.GetString();
        return true;
    case 2:
        node->m_year = variant.GetLong();
        return true;
    case 3:
        node->m_quality = variant.GetString();
        return true;

    default:
        printf("MyMusicTreeModel::SetValue: wrong column");
    }
    return false;
}

bool MyMusicTreeModel::IsEnabled(const wxDataViewItem &item,
    unsigned int col) const
{
    wxASSERT(item.IsOk());

    MyMusicTreeModelNode *node = (MyMusicTreeModelNode*)item.GetID();

    // disable Beethoven's ratings, his pieces can only be good
    return !(col == 3 && node->m_artist.EndsWith("Beethoven"));
}

wxDataViewItem MyMusicTreeModel::GetParent(const wxDataViewItem &item) const
{
    // the invisible root node has no parent
    if (!item.IsOk())
        return wxDataViewItem(0);

    MyMusicTreeModelNode *node = (MyMusicTreeModelNode*)item.GetID();

    // "MyMusic" also has no parent
    if (node == m_root)
        return wxDataViewItem(0);

    return wxDataViewItem((void*)node->GetParent());
}

bool MyMusicTreeModel::IsContainer(const wxDataViewItem &item) const
{
    // the invisble root node can have children
    // (in our model always "MyMusic")
    if (!item.IsOk())
        return true;

    MyMusicTreeModelNode *node = (MyMusicTreeModelNode*)item.GetID();
    return node->IsContainer();
}

unsigned int MyMusicTreeModel::GetChildren(const wxDataViewItem &parent,
    wxDataViewItemArray &array) const
{
    MyMusicTreeModelNode *node = (MyMusicTreeModelNode*)parent.GetID();
    if (!node)
    {
        array.Add(wxDataViewItem((void*)m_root));
        return 1;
    }

    if (node == m_classical)
    {
        MyMusicTreeModel *model = (MyMusicTreeModel*)(const MyMusicTreeModel*) this;
        model->m_classicalMusicIsKnownToControl = true;
    }

    if (node->GetChildCount() == 0)
    {
        return 0;
    }

    unsigned int count = node->GetChildren().GetCount();
    for (unsigned int pos = 0; pos < count; pos++)
    {
        MyMusicTreeModelNode *child = node->GetChildren().Item(pos);
        array.Add(wxDataViewItem((void*)child));
    }

    return count;
}

// *****************************************************************************
