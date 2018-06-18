#ifndef slic3r_GUI_wxExtensions_hpp_
#define slic3r_GUI_wxExtensions_hpp_

#include <wx/checklst.h>
#include <wx/combo.h>
#include <wx/dataview.h>
#include <wx/dc.h>
#include <wx/collpane.h>
#include <wx/wupdlock.h>
#include <wx/button.h>
#include <wx/sizer.h>

#include <vector>
#include <set>

class wxCheckListBoxComboPopup : public wxCheckListBox, public wxComboPopup
{
    static const unsigned int DefaultWidth;
    static const unsigned int DefaultHeight;
    static const unsigned int DefaultItemHeight;

    wxString m_text;

    // Events sent on mouseclick are quite complex. Function OnListBoxSelection is supposed to pass the event to the checkbox, which works fine on
    // Win. On OSX and Linux the events are generated differently - clicking on the checkbox square generates the event twice (and the square
    // therefore seems not to respond).
    // This enum is meant to save current state of affairs, i.e., if the event forwarding is ok to do or not. It is only used on Linux
    // and OSX by some #ifdefs. It also stores information whether OnListBoxSelection is supposed to change the checkbox status,
    // or if it changed status on its own already (which happens when the square is clicked). More comments in OnCheckListBox(...)
    // There indeed is a better solution, maybe making a custom event used for the event passing to distinguish the original and passed message
    // and blocking one of them on OSX and Linux. Feel free to refactor, but carefully test on all platforms.
    enum class OnCheckListBoxFunction{
        FreeToProceed,
        RefuseToProceed,
        WasRefusedLastTime
    } m_check_box_events_status = OnCheckListBoxFunction::FreeToProceed;


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



// ***  PrusaCollapsiblePane  *** 
// ----------------------------------------------------------------------------
class PrusaCollapsiblePane : public wxCollapsiblePane
{
public:
	PrusaCollapsiblePane() {}
	PrusaCollapsiblePane(wxWindow *parent,
		wxWindowID winid,
		const wxString& label,
		const wxPoint& pos = wxDefaultPosition,
		const wxSize& size = wxDefaultSize,
		long style = wxCP_DEFAULT_STYLE,
		const wxValidator& val = wxDefaultValidator,
		const wxString& name = wxCollapsiblePaneNameStr)
	{
		Create(parent, winid, label, pos, size, style, val, name);
	}
	~PrusaCollapsiblePane() {}

	void OnStateChange(const wxSize& sz); //override/hide of OnStateChange from wxCollapsiblePane
	virtual bool Show(bool show = true) override {
		wxCollapsiblePane::Show(show);
		OnStateChange(GetBestSize());
		return true;
	}
};


// ***  PrusaCollapsiblePaneMSW  ***  used only #ifdef __WXMSW__
// ----------------------------------------------------------------------------
#ifdef __WXMSW__
class PrusaCollapsiblePaneMSW : public PrusaCollapsiblePane//wxCollapsiblePane
{
	wxButton*	m_pDisclosureTriangleButton = nullptr;
	wxBitmap	m_bmp_close;
	wxBitmap	m_bmp_open;
public:
	PrusaCollapsiblePaneMSW() {}
	PrusaCollapsiblePaneMSW(	wxWindow *parent,
							wxWindowID winid,
							const wxString& label,
							const wxPoint& pos = wxDefaultPosition,
							const wxSize& size = wxDefaultSize,
							long style = wxCP_DEFAULT_STYLE,
							const wxValidator& val = wxDefaultValidator,
							const wxString& name = wxCollapsiblePaneNameStr)
	{
		Create(parent, winid, label, pos, size, style, val, name);
	}

	~PrusaCollapsiblePaneMSW() {}

	bool Create(wxWindow *parent,
				wxWindowID id,
				const wxString& label,
				const wxPoint& pos,
				const wxSize& size,
				long style,
				const wxValidator& val,
				const wxString& name);

	void UpdateBtnBmp();
	void SetLabel(const wxString &label) override;
	bool Layout() override;
	void Collapse(bool collapse) override;
};
#endif //__WXMSW__

// *****************************************************************************
// ----------------------------------------------------------------------------
// PrusaObjectDataViewModelNode: a node inside PrusaObjectDataViewModel
// ----------------------------------------------------------------------------

class PrusaObjectDataViewModelNode;
WX_DEFINE_ARRAY_PTR(PrusaObjectDataViewModelNode*, MyObjectTreeModelNodePtrArray);

class PrusaObjectDataViewModelNode
{
	PrusaObjectDataViewModelNode*	m_parent;
	MyObjectTreeModelNodePtrArray   m_children;
public:
	PrusaObjectDataViewModelNode(const wxString &name, int instances_count=1, int scale=100) {
		m_parent	= NULL;
		m_name		= name;
		m_copy		= wxString::Format("%d", instances_count);
		m_scale		= wxString::Format("%d%%", scale);
		m_type		= "object";
		m_volume_id	= -1;
	}

	PrusaObjectDataViewModelNode(	PrusaObjectDataViewModelNode* parent,
									const wxString& sub_obj, 
									const wxIcon& icon, 
									int volume_id=-1) {
		m_parent	= parent;
		m_name		= sub_obj;
		m_copy		= wxEmptyString;
		m_scale		= wxEmptyString;
		m_icon		= icon;
		m_type		= "volume";
		m_volume_id	= volume_id;
	}

	~PrusaObjectDataViewModelNode()
	{
		// free all our children nodes
		size_t count = m_children.GetCount();
		for (size_t i = 0; i < count; i++)
		{
			PrusaObjectDataViewModelNode *child = m_children[i];
			delete child;
		}
	}
	
	wxString				m_name;
	wxIcon					m_icon;
	wxString				m_copy;
	wxString				m_scale;
	std::string				m_type;
	int						m_volume_id;
	bool					m_container = false;

	bool IsContainer() const
	{
		return m_container;
	}

	PrusaObjectDataViewModelNode* GetParent()
	{
		return m_parent;
	}
	MyObjectTreeModelNodePtrArray& GetChildren()
	{
		return m_children;
	}
	PrusaObjectDataViewModelNode* GetNthChild(unsigned int n)
	{
		return m_children.Item(n);
	}
	void Insert(PrusaObjectDataViewModelNode* child, unsigned int n)
	{
		m_children.Insert(child, n);
	}
	void Append(PrusaObjectDataViewModelNode* child)
	{
		if (!m_container)
			m_container = true;
		m_children.Add(child);
	}
	void RemoveAllChildren()
	{
		if (GetChildCount() == 0)
			return;
		for (size_t id = GetChildCount() - 1; id >= 0; --id)
		{
			if (m_children.Item(id)->GetChildCount() > 0)
				m_children[id]->RemoveAllChildren();
			auto node = m_children[id];
			m_children.RemoveAt(id);
			delete node;
		}
	}

	size_t GetChildCount() const
	{
		return m_children.GetCount();
	}

	bool SetValue(const wxVariant &variant, unsigned int col)
	{
		switch (col)
		{
		case 0:{
			wxDataViewIconText data;
			data << variant;
			m_icon = data.GetIcon();
			m_name = data.GetText();
			return true;}
		case 1:
			m_copy = variant.GetString();
			return true;
		case 2:
			m_scale = variant.GetString();
			return true;
		default:
			printf("MyObjectTreeModel::SetValue: wrong column");
		}
		return false;
	}
	void SetIcon(const wxIcon &icon)
	{
		m_icon = icon;
	}
	
	void SetType(const std::string& type){
		m_type = type;
	}	
	const std::string& GetType(){
		return m_type;
	}

	const int GetVolumeId(){
		return m_volume_id;
	}
};

// ----------------------------------------------------------------------------
// PrusaObjectDataViewModel
// ----------------------------------------------------------------------------

class PrusaObjectDataViewModel :public wxDataViewModel
{
	std::vector<PrusaObjectDataViewModelNode*> m_objects;
public:
	PrusaObjectDataViewModel(){}
	~PrusaObjectDataViewModel()
	{
		for (auto object : m_objects)
			delete object;		
	}

	wxDataViewItem Add(wxString &name);
	wxDataViewItem Add(wxString &name, int instances_count, int scale);
	wxDataViewItem AddChild(const wxDataViewItem &parent_item, 
							const wxString &name, 
							const wxIcon& icon);
	wxDataViewItem Delete(const wxDataViewItem &item);
	void DeleteAll();
	wxDataViewItem GetItemById(int obj_idx);
	int GetIdByItem(wxDataViewItem& item);
	int GetVolumeIdByItem(wxDataViewItem& item);
	bool IsEmpty() { return m_objects.empty(); }

	// helper method for wxLog

	wxString GetName(const wxDataViewItem &item) const;
	wxString GetCopy(const wxDataViewItem &item) const;
	wxString GetScale(const wxDataViewItem &item) const;

	// helper methods to change the model

	virtual unsigned int GetColumnCount() const override { return 3;}
	virtual wxString GetColumnType(unsigned int col) const override{ return wxT("string"); }

	virtual void GetValue(wxVariant &variant,
		const wxDataViewItem &item, unsigned int col) const override;
	virtual bool SetValue(const wxVariant &variant,
		const wxDataViewItem &item, unsigned int col) override;
	bool SetValue(const wxVariant &variant, const int item_idx, unsigned int col);

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
