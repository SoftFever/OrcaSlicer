#ifndef slic3r_GUI_wxExtensions_hpp_
#define slic3r_GUI_wxExtensions_hpp_

#include <wx/checklst.h>
#include <wx/combo.h>
#include <wx/dataview.h>
#include <wx/dc.h>
#include <vector>
#include <set>

class wxCheckListBoxComboPopup : public wxCheckListBox, public wxComboPopup
{
    static const unsigned int DefaultWidth;
    static const unsigned int DefaultHeight;
    static const unsigned int DefaultItemHeight;

    wxString m_text;

public:
    virtual bool Create(wxWindow* parent);
    virtual wxWindow* GetControl();
    virtual void SetStringValue(const wxString& value);
    virtual wxString GetStringValue() const;
    virtual wxSize GetAdjustedSize(int minWidth, int prefHeight, int maxHeight);

    virtual void OnKeyEvent(wxKeyEvent& evt);

    void OnCheckListBox(wxCommandEvent& evt);
    void OnListBoxSelection(wxCommandEvent& evt);
};


// ***  wxDataViewTreeCtrlComboBox  ***

class wxDataViewTreeCtrlComboPopup: public wxDataViewTreeCtrl, public wxComboPopup
{
	static const unsigned int DefaultWidth;
	static const unsigned int DefaultHeight;
	static const unsigned int DefaultItemHeight;

	wxString	m_text;
	int			m_cnt_open_items{0};

public:
	virtual bool		Create(wxWindow* parent);
	virtual wxWindow*	GetControl() { return this; }
	virtual void		SetStringValue(const wxString& value) { m_text = value; }
	virtual wxString	GetStringValue() const { return m_text; }
//	virtual wxSize		GetAdjustedSize(int minWidth, int prefHeight, int maxHeight);

	virtual void		OnKeyEvent(wxKeyEvent& evt);
	void				OnDataViewTreeCtrlSelection(wxCommandEvent& evt);
	void				SetItemsCnt(int cnt) { m_cnt_open_items = cnt; }
};

// *****************************************************************************
// ----------------------------------------------------------------------------
// MyObjectTreeModelNode: a node inside MyObjectTreeModel
// ----------------------------------------------------------------------------

class MyObjectTreeModelNode;
WX_DEFINE_ARRAY_PTR(MyObjectTreeModelNode*, MyObjectTreeModelNodePtrArray);

class MyObjectTreeModelNode
{
	MyObjectTreeModelNode*			m_parent;
	MyObjectTreeModelNodePtrArray   m_children;
public:
	MyObjectTreeModelNode(	const wxString &name) {
		m_parent	= NULL;
		m_name		= name;
		m_copy		= "1";
		m_scale		= "100%";
	}

	MyObjectTreeModelNode(	MyObjectTreeModelNode* parent,
							const wxString& sub_obj) {
		m_parent	= parent;
		m_name		= sub_obj;
		m_copy		= wxEmptyString;
		m_scale		= wxEmptyString;
	}

	~MyObjectTreeModelNode()
	{
		// free all our children nodes
		size_t count = m_children.GetCount();
		for (size_t i = 0; i < count; i++)
		{
			MyObjectTreeModelNode *child = m_children[i];
			delete child;
		}
	}
	
	wxString				m_name;
	wxString				m_copy;
	wxString				m_scale;
	bool					m_container = false;

	bool IsContainer() const
	{
		return m_container;
	}

	MyObjectTreeModelNode* GetParent()
	{
		return m_parent;
	}
	MyObjectTreeModelNodePtrArray& GetChildren()
	{
		return m_children;
	}
	MyObjectTreeModelNode* GetNthChild(unsigned int n)
	{
		return m_children.Item(n);
	}
	void Insert(MyObjectTreeModelNode* child, unsigned int n)
	{
		m_children.Insert(child, n);
	}
	void Append(MyObjectTreeModelNode* child)
	{
		if (!m_container)
			m_container = true;
		m_children.Add(child);
	}
	unsigned int GetChildCount() const
	{
		return m_children.GetCount();
	}
};

// ----------------------------------------------------------------------------
// MyObjectTreeModel
// ----------------------------------------------------------------------------

class MyObjectTreeModel :public wxDataViewModel
{
	std::set<MyObjectTreeModelNode*> m_objects;
public:
	MyObjectTreeModel();
	~MyObjectTreeModel()
	{
		for (auto object : m_objects)
			delete object;		
	}

	// helper method for wxLog

	wxString GetName(const wxDataViewItem &item) const;
	wxString GetCopy(const wxDataViewItem &item) const;
	wxString GetScale(const wxDataViewItem &item) const;

	// helper methods to change the model

// 	void AddToClassical(const wxString &title, const wxString &artist,
// 		unsigned int year);
// 	void Delete(const wxDataViewItem &item);

	virtual unsigned int GetColumnCount() const override { return 3;}
	virtual wxString GetColumnType(unsigned int col) const override{ return wxT("string"); }

	virtual void GetValue(wxVariant &variant,
		const wxDataViewItem &item, unsigned int col) const override;
	virtual bool SetValue(const wxVariant &variant,
		const wxDataViewItem &item, unsigned int col) override;

// 	virtual bool IsEnabled(const wxDataViewItem &item,
// 		unsigned int col) const override;

	virtual wxDataViewItem GetParent(const wxDataViewItem &item) const override;
	virtual bool IsContainer(const wxDataViewItem &item) const override;
	virtual unsigned int GetChildren(const wxDataViewItem &parent,
		wxDataViewItemArray &array) const override;

	// Is the container just a header or an item with all columns
	// In our case it is an item with all columns 
	virtual bool HasContainerColumns(const wxDataViewItem& WXUNUSED(item)) const override {	return true; }
};




// *****************************************************************************
// ----------------------------------------------------------------------------
// MyMusicTreeModelNode: a node inside MyMusicTreeModel
// ----------------------------------------------------------------------------

class MyMusicTreeModelNode;
WX_DEFINE_ARRAY_PTR(MyMusicTreeModelNode*, MyMusicTreeModelNodePtrArray);

class MyMusicTreeModelNode
{
public:
	MyMusicTreeModelNode(MyMusicTreeModelNode* parent,
		const wxString &title, const wxString &artist,
		unsigned int year)
	{
		m_parent = parent;

		m_title = title;
		m_artist = artist;
		m_year = year;
		m_quality = "good";

		m_container = false;
	}

	MyMusicTreeModelNode(MyMusicTreeModelNode* parent,
		const wxString &branch)
	{
		m_parent = parent;

		m_title = branch;
		m_year = -1;

		m_container = true;
	}

	~MyMusicTreeModelNode()
	{
		// free all our children nodes
		size_t count = m_children.GetCount();
		for (size_t i = 0; i < count; i++)
		{
			MyMusicTreeModelNode *child = m_children[i];
			delete child;
		}
	}

	bool IsContainer() const
	{
		return m_container;
	}

	MyMusicTreeModelNode* GetParent()
	{
		return m_parent;
	}
	MyMusicTreeModelNodePtrArray& GetChildren()
	{
		return m_children;
	}
	MyMusicTreeModelNode* GetNthChild(unsigned int n)
	{
		return m_children.Item(n);
	}
	void Insert(MyMusicTreeModelNode* child, unsigned int n)
	{
		m_children.Insert(child, n);
	}
	void Append(MyMusicTreeModelNode* child)
	{
		m_children.Add(child);
	}
	unsigned int GetChildCount() const
	{
		return m_children.GetCount();
	}

public:     // public to avoid getters/setters
	wxString                m_title;
	wxString                m_artist;
	int                     m_year;
	wxString                m_quality;

	// TODO/FIXME:
	// the GTK version of wxDVC (in particular wxDataViewCtrlInternal::ItemAdded)
	// needs to know in advance if a node is or _will be_ a container.
	// Thus implementing:
	//   bool IsContainer() const
	//    { return m_children.GetCount()>0; }
	// doesn't work with wxGTK when MyMusicTreeModel::AddToClassical is called
	// AND the classical node was removed (a new node temporary without children
	// would be added to the control)
	bool m_container;

private:
	MyMusicTreeModelNode          *m_parent;
	MyMusicTreeModelNodePtrArray   m_children;
};


// ----------------------------------------------------------------------------
// MyMusicTreeModel
// ----------------------------------------------------------------------------

/*
Implement this data model
Title               Artist               Year        Judgement
--------------------------------------------------------------------------
1: My Music:
2:  Pop music
3:  You are not alone   Michael Jackson      1995        good
4:  Take a bow          Madonna              1994        good
5:  Classical music
6:  Ninth Symphony      Ludwig v. Beethoven  1824        good
7:  German Requiem      Johannes Brahms      1868        good
*/

class MyMusicTreeModel : public wxDataViewModel
{
public:
	MyMusicTreeModel();
	~MyMusicTreeModel()
	{
		if (m_root)
			delete m_root;
		
	}

	// helper method for wxLog

	wxString GetTitle(const wxDataViewItem &item) const;
	wxString GetArtist(const wxDataViewItem &item) const;
	int GetYear(const wxDataViewItem &item) const;

	// helper methods to change the model

	void AddToClassical(const wxString &title, const wxString &artist,
		unsigned int year);
	void Delete(const wxDataViewItem &item);

	wxDataViewItem GetNinthItem() const
	{
		return wxDataViewItem(m_ninth);
	}

	// override sorting to always sort branches ascendingly

	int Compare(const wxDataViewItem &item1, const wxDataViewItem &item2,
		unsigned int column, bool ascending) const override/*wxOVERRIDE*/;

	// implementation of base class virtuals to define model

	virtual unsigned int GetColumnCount() const override/*wxOVERRIDE*/
	{
		return 6;
	}

	virtual wxString GetColumnType(unsigned int col) const override/*wxOVERRIDE*/
	{
		if (col == 2)
		return wxT("long");

		return wxT("string");
	}

	virtual void GetValue(wxVariant &variant,
		const wxDataViewItem &item, unsigned int col) const override/*wxOVERRIDE*/;
	virtual bool SetValue(const wxVariant &variant,
		const wxDataViewItem &item, unsigned int col) override/*wxOVERRIDE*/;

	virtual bool IsEnabled(const wxDataViewItem &item,
		unsigned int col) const override/*wxOVERRIDE*/;

	virtual wxDataViewItem GetParent(const wxDataViewItem &item) const override/*wxOVERRIDE*/;
	virtual bool IsContainer(const wxDataViewItem &item) const override/*wxOVERRIDE*/;
	virtual unsigned int GetChildren(const wxDataViewItem &parent,
		wxDataViewItemArray &array) const override/*wxOVERRIDE*/;

private:
	MyMusicTreeModelNode*   m_root;

	// pointers to some "special" nodes of the tree:
	MyMusicTreeModelNode*   m_pop;
	MyMusicTreeModelNode*   m_classical;
	MyMusicTreeModelNode*   m_ninth;

	// ??
	bool                    m_classicalMusicIsKnownToControl;
};

// ----------------------------------------------------------------------------
// MyCustomRenderer
// ----------------------------------------------------------------------------

class MyCustomRenderer : public wxDataViewCustomRenderer
{
public:
	// This renderer can be either activatable or editable, for demonstration
	// purposes. In real programs, you should select whether the user should be
	// able to activate or edit the cell and it doesn't make sense to switch
	// between the two -- but this is just an example, so it doesn't stop us.
	explicit MyCustomRenderer(wxDataViewCellMode mode)
		: wxDataViewCustomRenderer("string", mode, wxALIGN_CENTER)
	{ }

	virtual bool Render(wxRect rect, wxDC *dc, int state) override/*wxOVERRIDE*/
	{
		dc->SetBrush(*wxLIGHT_GREY_BRUSH);
		dc->SetPen(*wxTRANSPARENT_PEN);

		rect.Deflate(2);
		dc->DrawRoundedRectangle(rect, 5);

		RenderText(m_value,
			0, // no offset
			wxRect(dc->GetTextExtent(m_value)).CentreIn(rect),
			dc,
			state);
		return true;
	}

		virtual bool ActivateCell(const wxRect& WXUNUSED(cell),
		wxDataViewModel *WXUNUSED(model),
		const wxDataViewItem &WXUNUSED(item),
		unsigned int WXUNUSED(col),
		const wxMouseEvent *mouseEvent) override/*wxOVERRIDE*/
	{
		wxString position;
		if (mouseEvent)
			position = wxString::Format("via mouse at %d, %d", mouseEvent->m_x, mouseEvent->m_y);
		else
			position = "from keyboard";
//		wxLogMessage("MyCustomRenderer ActivateCell() %s", position);
		return false;
	}

		virtual wxSize GetSize() const override/*wxOVERRIDE*/
	{
		return wxSize(60, 20);
	}

		virtual bool SetValue(const wxVariant &value) override/*wxOVERRIDE*/
	{
		m_value = value.GetString();
		return true;
	}

		virtual bool GetValue(wxVariant &WXUNUSED(value)) const override/*wxOVERRIDE*/{ return true; }

		virtual bool HasEditorCtrl() const override/*wxOVERRIDE*/{ return true; }

		virtual wxWindow*
		CreateEditorCtrl(wxWindow* parent,
		wxRect labelRect,
		const wxVariant& value) override/*wxOVERRIDE*/
	{
		wxTextCtrl* text = new wxTextCtrl(parent, wxID_ANY, value,
		labelRect.GetPosition(),
		labelRect.GetSize(),
		wxTE_PROCESS_ENTER);
		text->SetInsertionPointEnd();

		return text;
	}

		virtual bool
			GetValueFromEditorCtrl(wxWindow* ctrl, wxVariant& value) override/*wxOVERRIDE*/
	{
		wxTextCtrl* text = wxDynamicCast(ctrl, wxTextCtrl);
		if (!text)
			return false;

		value = text->GetValue();

		return true;
	}

private:
	wxString m_value;
};
// *****************************************************************************



#endif // slic3r_GUI_wxExtensions_hpp_
