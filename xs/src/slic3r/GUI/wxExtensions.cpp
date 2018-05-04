#include "wxExtensions.hpp"

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

    wxComboCtrl* cmb = GetComboCtrl();
    if (cmb != nullptr)
    {
        wxCommandEvent event(wxEVT_CHECKLISTBOX, cmb->GetId());
        event.SetEventObject(cmb);
        cmb->ProcessWindowEvent(event);
    }

    evt.Skip();
}

void wxCheckListBoxComboPopup::OnListBoxSelection(wxCommandEvent& evt)
{
    // transforms list box item selection event into checklistbox item toggle event 

    int selId = GetSelection();
    if (selId != wxNOT_FOUND)
    {
        Check((unsigned int)selId, !IsChecked((unsigned int)selId));
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

// *****************************************************************************
// ----------------------------------------------------------------------------
// MyObjectTreeModel
// ----------------------------------------------------------------------------

MyObjectTreeModel::MyObjectTreeModel()
{
	auto root1 = new MyObjectTreeModelNode("Object1");
	m_objects.emplace(root1);

	auto root2 = new MyObjectTreeModelNode("Object2");
	m_objects.emplace(root2);
	root2->Append(new MyObjectTreeModelNode(root2, "SubObject1"));
	root2->Append(new MyObjectTreeModelNode(root2, "SubObject2"));
	root2->Append(new MyObjectTreeModelNode(root2, "SubObject3"));

	auto root3 = new MyObjectTreeModelNode("Object3");
	m_objects.emplace(root3);
	auto root4 = new MyObjectTreeModelNode("Object4");
	m_objects.emplace(root4);
	root4->Append(new MyObjectTreeModelNode(root2, "SubObject1"));
	root4->Append(new MyObjectTreeModelNode(root2, "SubObject2"));
	root4->Append(new MyObjectTreeModelNode(root2, "SubObject3"));
}

wxString MyObjectTreeModel::GetName(const wxDataViewItem &item) const
{
	MyObjectTreeModelNode *node = (MyObjectTreeModelNode*)item.GetID();
	if (!node)      // happens if item.IsOk()==false
		return wxEmptyString;

	return node->m_name;
}

wxString MyObjectTreeModel::GetCopyCnt(const wxDataViewItem &item) const
{
	MyObjectTreeModelNode *node = (MyObjectTreeModelNode*)item.GetID();
	if (!node)      // happens if item.IsOk()==false
		return wxEmptyString;

	return node->m_copy;
}

wxString MyObjectTreeModel::GetScale(const wxDataViewItem &item) const
{
	MyObjectTreeModelNode *node = (MyObjectTreeModelNode*)item.GetID();
	if (!node)      // happens if item.IsOk()==false
		return wxEmptyString;

	return node->m_scale;
}

// void MyObjectTreeModel::Delete(const wxDataViewItem &item)
// {
// 
// }

void MyObjectTreeModel::GetValue(wxVariant &variant, const wxDataViewItem &item, unsigned int col) const
{
	wxASSERT(item.IsOk());

	MyObjectTreeModelNode *node = (MyObjectTreeModelNode*)item.GetID();
	switch (col)
	{
	case 0:
		variant = node->m_name;
		break;
	case 1:
		variant = node->m_copy;
		break;
	case 2:
		variant = node->m_scale;
		break;
	default:
		;// wxLogError("MyMusicTreeModel::GetValue: wrong column %d", col);
	}
}

bool MyObjectTreeModel::SetValue(const wxVariant &variant, const wxDataViewItem &item, unsigned int col)
{
	wxASSERT(item.IsOk());

	MyObjectTreeModelNode *node = (MyObjectTreeModelNode*)item.GetID();
	switch (col)
	{
	case 0:
		node->m_name = variant.GetString();
		return true;
	case 1:
		node->m_copy = variant.GetString();
		return true;
	case 2:
		node->m_scale = variant.GetString();
		return true;

	default:;
		//		wxLogError("MyObjectTreeModel::SetValue: wrong column");
	}
	return false;
}

// bool MyObjectTreeModel::IsEnabled(const wxDataViewItem &item, unsigned int col) const
// {
// 
// }

wxDataViewItem MyObjectTreeModel::GetParent(const wxDataViewItem &item) const
{
	// the invisible root node has no parent
	if (!item.IsOk())
		return wxDataViewItem(0);

	MyObjectTreeModelNode *node = (MyObjectTreeModelNode*)item.GetID();

	// objects nodes also has no parent
	if (m_objects.find(node) != m_objects.end())
		return wxDataViewItem(0);

	return wxDataViewItem((void*)node->GetParent());
}

bool MyObjectTreeModel::IsContainer(const wxDataViewItem &item) const
{
	// the invisble root node can have children
	if (!item.IsOk())
		return true;

	MyObjectTreeModelNode *node = (MyObjectTreeModelNode*)item.GetID();
	return node->IsContainer();
}

unsigned int MyObjectTreeModel::GetChildren(const wxDataViewItem &parent, wxDataViewItemArray &array) const
{
	MyObjectTreeModelNode *node = (MyObjectTreeModelNode*)parent.GetID();
	if (!node)
	{
		for (auto object: m_objects)
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
		MyObjectTreeModelNode *child = node->GetChildren().Item(pos);
		array.Add(wxDataViewItem((void*)child));
	}

	return count;
}

// *****************************************************************************
// ----------------------------------------------------------------------------
// MyMusicTreeModel
// ----------------------------------------------------------------------------

MyMusicTreeModel::MyMusicTreeModel()
{
	m_root = new MyMusicTreeModelNode(NULL, "");// , "My Music");

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
		//wxLogError("Cannot remove the root item!");
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
		;// wxLogError("MyMusicTreeModel::GetValue: wrong column %d", col);
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

	default:;
//		wxLogError("MyMusicTreeModel::SetValue: wrong column");
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



