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
#include <wx/slider.h>
#include <wx/menu.h>

#include <vector>
#include <set>
#include <functional>

namespace Slic3r {
	enum class ModelVolumeType : int;
};

wxMenuItem* append_menu_item(wxMenu* menu, int id, const wxString& string, const wxString& description, 
    std::function<void(wxCommandEvent& event)> cb, const wxBitmap& icon, wxEvtHandler* event_handler = nullptr);
wxMenuItem* append_menu_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent& event)> cb, const std::string& icon = "", wxEvtHandler* event_handler = nullptr);

wxMenuItem* append_submenu(wxMenu* menu, wxMenu* sub_menu, int id, const wxString& string, const wxString& description, 
    const std::string& icon = "");

wxMenuItem* append_menu_radio_item(wxMenu* menu, int id, const wxString& string, const wxString& description, 
    std::function<void(wxCommandEvent& event)> cb, wxEvtHandler* event_handler);

wxBitmap create_scaled_bitmap(const std::string& bmp_name);

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
	wxString    m_strLabel;
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
// PrusaDataViewBitmapText: helper class used by PrusaBitmapTextRenderer
// ----------------------------------------------------------------------------

class PrusaDataViewBitmapText : public wxObject
{
public:
    PrusaDataViewBitmapText(const wxString &text = wxEmptyString,
        const wxBitmap& bmp = wxNullBitmap) :
        m_text(text), m_bmp(bmp)
    { }

    PrusaDataViewBitmapText(const PrusaDataViewBitmapText &other)
        : wxObject(),
        m_text(other.m_text),
        m_bmp(other.m_bmp)
    { }

    void SetText(const wxString &text)      { m_text = text; }
    wxString GetText() const                { return m_text; }
    void SetBitmap(const wxBitmap &bmp)      { m_bmp = bmp; }
    const wxBitmap &GetBitmap() const       { return m_bmp; }

    bool IsSameAs(const PrusaDataViewBitmapText& other) const {
        return m_text == other.m_text && m_bmp.IsSameAs(other.m_bmp);
    }

    bool operator==(const PrusaDataViewBitmapText& other) const {
        return IsSameAs(other);
    }

    bool operator!=(const PrusaDataViewBitmapText& other) const {
        return !IsSameAs(other);
    }

private:
    wxString    m_text;
    wxBitmap    m_bmp;

    wxDECLARE_DYNAMIC_CLASS(PrusaDataViewBitmapText);
};
DECLARE_VARIANT_OBJECT(PrusaDataViewBitmapText)


// ----------------------------------------------------------------------------
// PrusaObjectDataViewModelNode: a node inside PrusaObjectDataViewModel
// ----------------------------------------------------------------------------

enum ItemType {
    itUndef = 0,
    itObject = 1,
    itVolume = 2,
    itInstanceRoot = 4,
    itInstance = 8,
    itSettings = 16
};

class PrusaObjectDataViewModelNode;
WX_DEFINE_ARRAY_PTR(PrusaObjectDataViewModelNode*, MyObjectTreeModelNodePtrArray);

class PrusaObjectDataViewModelNode
{
	PrusaObjectDataViewModelNode*	m_parent;
	MyObjectTreeModelNodePtrArray   m_children;
    wxBitmap                        m_empty_bmp;
    size_t                          m_volumes_cnt = 0;
    std::vector< std::string >      m_opt_categories;
public:
    PrusaObjectDataViewModelNode(const wxString &name, 
                                 const wxString& extruder) {
		m_parent	= NULL;
		m_name		= name;
		m_type		= itObject;
#ifdef __WXGTK__
        // it's necessary on GTK because of control have to know if this item will be container
        // in another case you couldn't to add subitem for this item
        // it will be produce "segmentation fault"
        m_container = true;
#endif  //__WXGTK__
        m_extruder = extruder;
		set_object_action_icon();
	}

	PrusaObjectDataViewModelNode(	PrusaObjectDataViewModelNode* parent,
									const wxString& sub_obj_name, 
									const wxBitmap& bmp, 
                                    const wxString& extruder, 
                                    const int idx = -1 ) {
		m_parent	= parent;
		m_name		= sub_obj_name;
		m_bmp		= bmp;
		m_type		= itVolume;
        m_idx       = idx;
        m_extruder = extruder;
#ifdef __WXGTK__
        // it's necessary on GTK because of control have to know if this item will be container
        // in another case you couldn't to add subitem for this item
        // it will be produce "segmentation fault"
        m_container = true;
#endif  //__WXGTK__
		set_part_action_icon();
    }

    PrusaObjectDataViewModelNode(   PrusaObjectDataViewModelNode* parent, const ItemType type) :
                                    m_parent(parent),
                                    m_type(type),
                                    m_extruder(wxEmptyString)
	{
        if (type == itSettings) {
            m_name = "Settings to modified";
        }
        else if (type == itInstanceRoot) {
            m_name = "Instances"; 
#ifdef __WXGTK__
            m_container = true;
#endif  //__WXGTK__
        }
        else if (type == itInstance) {
            m_idx = parent->GetChildCount();
            m_name = wxString::Format("Instance_%d", m_idx+1);
            set_part_action_icon();
        }
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
    wxBitmap&               m_bmp = m_empty_bmp;
    ItemType				m_type;
    int                     m_idx = -1;
	bool					m_container = false;
	wxString				m_extruder = "default";
	wxBitmap				m_action_icon;

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
		if (!m_container)
			m_container = true;
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
		for (int id = int(GetChildCount()) - 1; id >= 0; --id)
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
            PrusaDataViewBitmapText data;
			data << variant;
            m_bmp = data.GetBitmap();
			m_name = data.GetText();
			return true;}
		case 1:
			m_extruder = variant.GetString();
			return true;
		case 2:
			m_action_icon << variant;
			return true;
		default:
			printf("MyObjectTreeModel::SetValue: wrong column");
		}
		return false;
	}

	void SetBitmap(const wxBitmap &icon)
	{
		m_bmp = icon;
	}

    ItemType GetType() const {
        return m_type;
    }

	void SetIdx(const int& idx) {
		m_idx = idx;
        // update name if this node is instance
        if (m_type == itInstance)
            m_name = wxString::Format("Instance_%d", m_idx + 1);
	}

	int GetIdx() const {
		return m_idx;
	}

	// use this function only for childrens
	void AssignAllVal(PrusaObjectDataViewModelNode& from_node)
	{
		// ! Don't overwrite other values because of equality of this values for all children --
		m_name = from_node.m_name;
        m_bmp = from_node.m_bmp;
        m_idx = from_node.m_idx;
        m_extruder = from_node.m_extruder;
        m_type = from_node.m_type;
	}

	bool SwapChildrens(int frst_id, int scnd_id) {
		if (GetChildCount() < 2 || 
			frst_id < 0 || frst_id >= GetChildCount() || 
			scnd_id < 0 || scnd_id >= GetChildCount())
			return false;

		PrusaObjectDataViewModelNode new_scnd = *GetNthChild(frst_id);
		PrusaObjectDataViewModelNode new_frst = *GetNthChild(scnd_id);

        new_scnd.m_idx = m_children.Item(scnd_id)->m_idx;
        new_frst.m_idx = m_children.Item(frst_id)->m_idx;

		m_children.Item(frst_id)->AssignAllVal(new_frst);
		m_children.Item(scnd_id)->AssignAllVal(new_scnd);
		return true;
	}

	// Set action icons for node
	void set_object_action_icon();
	void set_part_action_icon();
    bool update_settings_digest(const std::vector<std::string>& categories);
private:
    friend class PrusaObjectDataViewModel;
};

// ----------------------------------------------------------------------------
// PrusaObjectDataViewModel
// ----------------------------------------------------------------------------

// custom message the model sends to associated control to notify a last volume deleted from the object:
wxDECLARE_EVENT(wxCUSTOMEVT_LAST_VOLUME_IS_DELETED, wxCommandEvent);

class PrusaObjectDataViewModel :public wxDataViewModel
{
	std::vector<PrusaObjectDataViewModelNode*>  m_objects;
    std::vector<wxBitmap*>                      m_volume_bmps;

    wxDataViewCtrl*                             m_ctrl{ nullptr };

public:
    PrusaObjectDataViewModel();
    ~PrusaObjectDataViewModel();

	wxDataViewItem Add(const wxString &name, const int extruder);
	wxDataViewItem AddVolumeChild(const wxDataViewItem &parent_item, 
							const wxString &name, 
                            const Slic3r::ModelVolumeType volume_type,
                            const int extruder = 0,
                            const bool create_frst_child = true);
	wxDataViewItem AddSettingsChild(const wxDataViewItem &parent_item);
    wxDataViewItem AddInstanceChild(const wxDataViewItem &parent_item, size_t num);
	wxDataViewItem Delete(const wxDataViewItem &item);
	wxDataViewItem DeleteLastInstance(const wxDataViewItem &parent_item, size_t num);
	void DeleteAll();
    void DeleteChildren(wxDataViewItem& parent);
    void DeleteVolumeChildren(wxDataViewItem& parent);
    void DeleteSettings(const wxDataViewItem& parent);
	wxDataViewItem GetItemById(int obj_idx);
	wxDataViewItem GetItemByVolumeId(int obj_idx, int volume_idx);
	wxDataViewItem GetItemByInstanceId(int obj_idx, int inst_idx);
	int GetIdByItem(const wxDataViewItem& item) const;
    int GetIdByItemAndType(const wxDataViewItem& item, const ItemType type) const;
    int GetObjectIdByItem(const wxDataViewItem& item) const;
    int GetVolumeIdByItem(const wxDataViewItem& item) const;
    int GetInstanceIdByItem(const wxDataViewItem& item) const;
    void GetItemInfo(const wxDataViewItem& item, ItemType& type, int& obj_idx, int& idx);
    int GetRowByItem(const wxDataViewItem& item) const;
    bool IsEmpty() { return m_objects.empty(); }

	// helper method for wxLog

	wxString GetName(const wxDataViewItem &item) const;
    wxBitmap& GetBitmap(const wxDataViewItem &item) const;

	// helper methods to change the model

	virtual unsigned int GetColumnCount() const override { return 3;}
	virtual wxString GetColumnType(unsigned int col) const override{ return wxT("string"); }

	virtual void GetValue(wxVariant &variant,
		const wxDataViewItem &item, unsigned int col) const override;
	virtual bool SetValue(const wxVariant &variant,
		const wxDataViewItem &item, unsigned int col) override;
	bool SetValue(const wxVariant &variant, const int item_idx, unsigned int col);

// 	wxDataViewItem MoveChildUp(const wxDataViewItem &item);
// 	wxDataViewItem MoveChildDown(const wxDataViewItem &item);
    // For parent move child from cur_volume_id place to new_volume_id 
    // Remaining items will moved up/down accordingly
    wxDataViewItem ReorganizeChildren(int cur_volume_id, 
                                      int new_volume_id,
                                      const wxDataViewItem &parent);

	virtual bool IsEnabled(const wxDataViewItem &item, unsigned int col) const override;

	virtual wxDataViewItem GetParent(const wxDataViewItem &item) const override;
    // get object item
    wxDataViewItem GetTopParent(const wxDataViewItem &item) const;
	virtual bool IsContainer(const wxDataViewItem &item) const override;
	virtual unsigned int GetChildren(const wxDataViewItem &parent,
		wxDataViewItemArray &array) const override;

	// Is the container just a header or an item with all columns
	// In our case it is an item with all columns 
	virtual bool HasContainerColumns(const wxDataViewItem& WXUNUSED(item)) const override {	return true; }

    ItemType GetItemType(const wxDataViewItem &item) const ;
    wxDataViewItem    GetItemByType(const wxDataViewItem &parent_item, ItemType type) const;
    wxDataViewItem    GetSettingsItem(const wxDataViewItem &item) const;
    wxDataViewItem    GetInstanceRootItem(const wxDataViewItem &item) const;
    bool    IsSettingsItem(const wxDataViewItem &item) const;
    void    UpdateSettingsDigest(const wxDataViewItem &item, const std::vector<std::string>& categories);

    void    SetVolumeBitmaps(const std::vector<wxBitmap*>& volume_bmps) { m_volume_bmps = volume_bmps; }
    void    SetVolumeType(const wxDataViewItem &item, const Slic3r::ModelVolumeType type);

    void    SetAssociatedControl(wxDataViewCtrl* ctrl) { m_ctrl = ctrl; }
};

// ----------------------------------------------------------------------------
// PrusaBitmapTextRenderer
// ----------------------------------------------------------------------------
#if ENABLE_NONCUSTOM_DATA_VIEW_RENDERING
class PrusaBitmapTextRenderer : public wxDataViewRenderer
#else
class PrusaBitmapTextRenderer : public wxDataViewCustomRenderer
#endif //ENABLE_NONCUSTOM_DATA_VIEW_RENDERING
{
public:
    PrusaBitmapTextRenderer(wxDataViewCellMode mode =
#ifdef __WXOSX__
                                                        wxDATAVIEW_CELL_INERT
#else
                                                        wxDATAVIEW_CELL_EDITABLE
#endif

                            ,int align = wxDVR_DEFAULT_ALIGNMENT
#if ENABLE_NONCUSTOM_DATA_VIEW_RENDERING
                            );
#else
                            ) : wxDataViewCustomRenderer(wxT("PrusaDataViewBitmapText"), mode, align) {}
#endif //ENABLE_NONCUSTOM_DATA_VIEW_RENDERING

    bool SetValue(const wxVariant &value);
    bool GetValue(wxVariant &value) const;
#if ENABLE_NONCUSTOM_DATA_VIEW_RENDERING && wxUSE_ACCESSIBILITY
    virtual wxString GetAccessibleDescription() const override;
#endif // wxUSE_ACCESSIBILITY && ENABLE_NONCUSTOM_DATA_VIEW_RENDERING

    virtual bool Render(wxRect cell, wxDC *dc, int state);
    virtual wxSize GetSize() const;

    bool        HasEditorCtrl() const override
    {
#ifdef __WXOSX__
        return false;
#else
        return true;
#endif        
    }
    wxWindow*   CreateEditorCtrl(wxWindow* parent, 
                                 wxRect labelRect, 
                                 const wxVariant& value) override;
    bool        GetValueFromEditorCtrl( wxWindow* ctrl, 
                                        wxVariant& value) override;
    bool        WasCanceled() const { return m_was_unusable_symbol; }

private:
    PrusaDataViewBitmapText m_value;
    bool                    m_was_unusable_symbol {false};
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


// ----------------------------------------------------------------------------
// PrusaDoubleSlider
// ----------------------------------------------------------------------------

// custom message the slider sends to its parent to notify a tick-change:
wxDECLARE_EVENT(wxCUSTOMEVT_TICKSCHANGED, wxEvent);

enum SelectedSlider {
    ssUndef,
    ssLower,
    ssHigher
};
enum TicksAction{
    taOnIcon,
    taAdd,
    taDel
};

class PrusaDoubleSlider : public wxControl
{
public:
    PrusaDoubleSlider(
        wxWindow *parent,
        wxWindowID id,
        int lowerValue, 
        int higherValue, 
        int minValue, 
        int maxValue,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxSL_VERTICAL,
        const wxValidator& val = wxDefaultValidator,
        const wxString& name = wxEmptyString);
    ~PrusaDoubleSlider() {}

    int GetMinValue() const { return m_min_value; }
    int GetMaxValue() const { return m_max_value; }
    double GetMinValueD()  { return m_values.empty() ? 0. : m_values[m_min_value].second; }
    double GetMaxValueD() { return m_values.empty() ? 0. : m_values[m_max_value].second; }
    int GetLowerValue() const { return m_lower_value; }
    int GetHigherValue() const { return m_higher_value; }
    int GetActiveValue() const;
    double GetLowerValueD()  { return get_double_value(ssLower); }
    double GetHigherValueD() { return get_double_value(ssHigher); }
    wxSize DoGetBestSize() const override;
    void SetLowerValue(const int lower_val);
    void SetHigherValue(const int higher_val);
    // Set low and high slider position. If the span is non-empty, disable the "one layer" mode.
    void SetSelectionSpan(const int lower_val, const int higher_val);
    void SetMaxValue(const int max_value);
    void SetKoefForLabels(const double koef) {
        m_label_koef = koef;
    }
    void SetSliderValues(const std::vector<std::pair<int, double>>& values) {
        m_values = values;
    }
    void ChangeOneLayerLock();
    std::vector<double> GetTicksValues() const;
    void SetTicksValues(const std::vector<double>& heights);
    void EnableTickManipulation(bool enable = true) {
        m_is_enabled_tick_manipulation = enable;
    }
    void DisableTickManipulation() {
        EnableTickManipulation(false);
    }

	bool is_horizontal() const { return m_style == wxSL_HORIZONTAL; }
	bool is_one_layer() const { return m_is_one_layer; }
    bool is_lower_at_min() const { return m_lower_value == m_min_value; }
    bool is_higher_at_max() const { return m_higher_value == m_max_value; }
    bool is_full_span() const { return this->is_lower_at_min() && this->is_higher_at_max(); }

    void OnPaint(wxPaintEvent& ) { render();}
    void OnLeftDown(wxMouseEvent& event);
    void OnMotion(wxMouseEvent& event);
    void OnLeftUp(wxMouseEvent& event);
    void OnEnterWin(wxMouseEvent& event) { enter_window(event, true); }
    void OnLeaveWin(wxMouseEvent& event) { enter_window(event, false); }
    void OnWheel(wxMouseEvent& event);
    void OnKeyDown(wxKeyEvent &event);
    void OnKeyUp(wxKeyEvent &event);
    void OnRightDown(wxMouseEvent& event);
    void OnRightUp(wxMouseEvent& event);

protected:
 
    void    render();
    void    draw_focus_rect();
    void    draw_action_icon(wxDC& dc, const wxPoint pt_beg, const wxPoint pt_end);
    void    draw_scroll_line(wxDC& dc, const int lower_pos, const int higher_pos);
    void    draw_thumb(wxDC& dc, const wxCoord& pos_coord, const SelectedSlider& selection);
    void    draw_thumbs(wxDC& dc, const wxCoord& lower_pos, const wxCoord& higher_pos);
    void    draw_ticks(wxDC& dc);
    void    draw_colored_band(wxDC& dc);
    void    draw_one_layer_icon(wxDC& dc);
    void    draw_thumb_item(wxDC& dc, const wxPoint& pos, const SelectedSlider& selection);
    void    draw_info_line_with_icon(wxDC& dc, const wxPoint& pos, SelectedSlider selection);
    void    draw_thumb_text(wxDC& dc, const wxPoint& pos, const SelectedSlider& selection) const;

    void    update_thumb_rect(const wxCoord& begin_x, const wxCoord& begin_y, const SelectedSlider& selection);
    void    detect_selected_slider(const wxPoint& pt);
    void    correct_lower_value();
    void    correct_higher_value();
    void    move_current_thumb(const bool condition);
    void    action_tick(const TicksAction action);
    void    enter_window(wxMouseEvent& event, const bool enter);

    bool    is_point_in_rect(const wxPoint& pt, const wxRect& rect);
    int     is_point_near_tick(const wxPoint& pt);

    double      get_scroll_step();
    wxString    get_label(const SelectedSlider& selection) const;
    void        get_lower_and_higher_position(int& lower_pos, int& higher_pos);
    int         get_value_from_position(const wxCoord x, const wxCoord y);
    wxCoord     get_position_from_value(const int value);
    wxSize      get_size();
    void        get_size(int *w, int *h);
    double      get_double_value(const SelectedSlider& selection);

private:
    bool        is_osx { false };
    wxFont      m_font;
    int         m_min_value;
    int         m_max_value;
    int         m_lower_value;
    int         m_higher_value;
    wxBitmap    m_bmp_thumb_higher;
    wxBitmap    m_bmp_thumb_lower;
    wxBitmap    m_bmp_add_tick_on;
    wxBitmap    m_bmp_add_tick_off;
    wxBitmap    m_bmp_del_tick_on;
    wxBitmap    m_bmp_del_tick_off;
    wxBitmap    m_bmp_one_layer_lock_on;
    wxBitmap    m_bmp_one_layer_lock_off;
    wxBitmap    m_bmp_one_layer_unlock_on;
    wxBitmap    m_bmp_one_layer_unlock_off;
    SelectedSlider  m_selection;
    bool        m_is_left_down = false;
    bool        m_is_right_down = false;
    bool        m_is_one_layer = false;
    bool        m_is_focused = false;
    bool        m_is_action_icon_focesed = false;
    bool        m_is_one_layer_icon_focesed = false;
    bool        m_is_enabled_tick_manipulation = true;

    wxRect      m_rect_lower_thumb;
    wxRect      m_rect_higher_thumb;
    wxRect      m_rect_tick_action;
    wxRect      m_rect_one_layer_icon;
    wxSize      m_thumb_size;
    int         m_tick_icon_dim;
    int         m_lock_icon_dim;
    long        m_style;
    float       m_label_koef = 1.0;

// control's view variables
    wxCoord SLIDER_MARGIN; // margin around slider

    wxPen   DARK_ORANGE_PEN;
    wxPen   ORANGE_PEN;
    wxPen   LIGHT_ORANGE_PEN;

    wxPen   DARK_GREY_PEN;
    wxPen   GREY_PEN;
    wxPen   LIGHT_GREY_PEN;

    std::vector<wxPen*> line_pens;
    std::vector<wxPen*> segm_pens;
    std::set<int>       m_ticks;
    std::vector<std::pair<int,double>> m_values;
};


// ----------------------------------------------------------------------------
// PrusaLockButton
// ----------------------------------------------------------------------------

class PrusaLockButton : public wxButton
{
public:
    PrusaLockButton(
        wxWindow *parent,
        wxWindowID id,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize);
    ~PrusaLockButton() {}

    void    OnButton(wxCommandEvent& event);
    void    OnEnterBtn(wxMouseEvent& event) { enter_button(true); event.Skip(); }
    void    OnLeaveBtn(wxMouseEvent& event) { enter_button(false); event.Skip(); }

    bool    IsLocked() const { return m_is_pushed; }
    void    SetLock(bool lock);

protected:
    void    enter_button(const bool enter);

private:
    bool        m_is_pushed = false;

    wxBitmap    m_bmp_lock_on;
    wxBitmap    m_bmp_lock_off;
    wxBitmap    m_bmp_unlock_on;
    wxBitmap    m_bmp_unlock_off;
};


// ----------------------------------------------------------------------------
// PrusaModeButton
// ----------------------------------------------------------------------------

class PrusaModeButton : public wxButton
{
public:
    PrusaModeButton(
        wxWindow *parent,
        wxWindowID id,
        const wxString& mode = wxEmptyString,
        const wxBitmap& bmp_on = wxNullBitmap,
        const wxSize& size = wxDefaultSize,
        const wxPoint& pos = wxDefaultPosition);
    ~PrusaModeButton() {}

    void    OnButton(wxCommandEvent& event);
    void    OnEnterBtn(wxMouseEvent& event) { focus_button(true); event.Skip(); }
    void    OnLeaveBtn(wxMouseEvent& event) { focus_button(m_is_selected); event.Skip(); }

    void    SetState(const bool state);

protected:
    void    focus_button(const bool focus);

private:
    bool        m_is_selected = false;

    wxBitmap    m_bmp_on;
    wxBitmap    m_bmp_off;
    wxString    m_tt_selected;
    wxString    m_tt_focused;
};



// ----------------------------------------------------------------------------
// PrusaModeSizer
// ----------------------------------------------------------------------------

class PrusaModeSizer : public wxFlexGridSizer
{
public:
    PrusaModeSizer( wxWindow *parent, int hgap = 10);
    ~PrusaModeSizer() {}

    void SetMode(const /*ConfigOptionMode*/int mode);

private:
    std::vector<PrusaModeButton*> mode_btns;
};



// ----------------------------------------------------------------------------
// PrusaMenu
// ----------------------------------------------------------------------------

class PrusaMenu : public wxMenu
{
public:
    PrusaMenu(const wxString& title, long style = 0)
        : wxMenu(title, style) {}

    PrusaMenu(long style = 0)
        : wxMenu(style) {}

    ~PrusaMenu() {}

    void DestroySeparators();

    wxMenuItem* m_separator_frst { nullptr };    // use like separator before settings item
    wxMenuItem* m_separator_scnd { nullptr };   // use like separator between settings items
};


// ******************************* EXPERIMENTS **********************************************
// ******************************************************************************************


#endif // slic3r_GUI_wxExtensions_hpp_
