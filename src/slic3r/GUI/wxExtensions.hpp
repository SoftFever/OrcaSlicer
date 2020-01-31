#ifndef slic3r_GUI_wxExtensions_hpp_
#define slic3r_GUI_wxExtensions_hpp_

#include <wx/checklst.h>
#include <wx/combo.h>
#include <wx/dataview.h>
#include <wx/dc.h>
#include <wx/wupdlock.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/menu.h>
#include <wx/wx.h>

#include <vector>
#include <set>
#include <functional>

namespace Slic3r {
    enum class ModelVolumeType : int;
};

typedef double                          coordf_t;
typedef std::pair<coordf_t, coordf_t>   t_layer_height_range;

#ifdef __WXMSW__
void                msw_rescale_menu(wxMenu* menu);
#else /* __WXMSW__ */
inline void         msw_rescale_menu(wxMenu* /* menu */) {}
#endif /* __WXMSW__ */

wxMenuItem* append_menu_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent& event)> cb, const wxBitmap& icon, wxEvtHandler* event_handler = nullptr,
    std::function<bool()> const cb_condition = []() { return true;}, wxWindow* parent = nullptr);
wxMenuItem* append_menu_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent& event)> cb, const std::string& icon = "", wxEvtHandler* event_handler = nullptr,
    std::function<bool()> const cb_condition = []() { return true; }, wxWindow* parent = nullptr);

wxMenuItem* append_submenu(wxMenu* menu, wxMenu* sub_menu, int id, const wxString& string, const wxString& description,
    const std::string& icon = "",
    std::function<bool()> const cb_condition = []() { return true; }, wxWindow* parent = nullptr);

wxMenuItem* append_menu_radio_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent& event)> cb, wxEvtHandler* event_handler);

wxMenuItem* append_menu_check_item(wxMenu* menu, int id, const wxString& string, const wxString& description,
    std::function<void(wxCommandEvent& event)> cb, wxEvtHandler* event_handler);

void enable_menu_item(wxUpdateUIEvent& evt, std::function<bool()> const cb_condition, wxMenuItem* item, wxWindow* win);

class wxDialog;
class wxBitmapComboBox;

void    edit_tooltip(wxString& tooltip);
void    msw_buttons_rescale(wxDialog* dlg, const int em_unit, const std::vector<int>& btn_ids);
int     em_unit(wxWindow* win);

wxBitmap create_scaled_bitmap(const std::string& bmp_name, wxWindow *win = nullptr, 
    const int px_cnt = 16, const bool grayscale = false);

std::vector<wxBitmap*> get_extruder_color_icons(bool thin_icon = false);
void apply_extruder_selector(wxBitmapComboBox** ctrl,
                             wxWindow* parent,
                             const std::string& first_item = "",
                             wxPoint pos = wxDefaultPosition,
                             wxSize size = wxDefaultSize,
                             bool use_thin_icon = false);

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

namespace Slic3r {
namespace GUI {
// ***  PresetBitmapComboBox  ***

// BitmapComboBox used to presets list on Sidebar and Tabs
class PresetBitmapComboBox: public wxBitmapComboBox
{
public:
    PresetBitmapComboBox(wxWindow* parent, const wxSize& size = wxDefaultSize);
    ~PresetBitmapComboBox() {}

#ifdef __APPLE__
protected:
    /* For PresetBitmapComboBox we use bitmaps that are created from images that are already scaled appropriately for Retina
     * (Contrary to the intuition, the `scale` argument for Bitmap's constructor doesn't mean
     * "please scale this to such and such" but rather
     * "the wxImage is already sized for backing scale such and such". )
     * Unfortunately, the constructor changes the size of wxBitmap too.
     * Thus We need to use unscaled size value for bitmaps that we use
     * to avoid scaled size of control items.
     * For this purpose control drawing methods and
     * control size calculation methods (virtual) are overridden.
     **/
    virtual bool OnAddBitmap(const wxBitmap& bitmap) override;
    virtual void OnDrawItem(wxDC& dc, const wxRect& rect, int item, int flags) const override;
#endif
};

}
}


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


// ----------------------------------------------------------------------------
// DataViewBitmapText: helper class used by PrusaBitmapTextRenderer
// ----------------------------------------------------------------------------

class DataViewBitmapText : public wxObject
{
public:
    DataViewBitmapText( const wxString &text = wxEmptyString,
                        const wxBitmap& bmp = wxNullBitmap) :
        m_text(text),
        m_bmp(bmp)
    { }

    DataViewBitmapText(const DataViewBitmapText &other)
        : wxObject(),
        m_text(other.m_text),
        m_bmp(other.m_bmp)
    { }

    void SetText(const wxString &text)      { m_text = text; }
    wxString GetText() const                { return m_text; }
    void SetBitmap(const wxBitmap &bmp)     { m_bmp = bmp; }
    const wxBitmap &GetBitmap() const       { return m_bmp; }

    bool IsSameAs(const DataViewBitmapText& other) const {
        return m_text == other.m_text && m_bmp.IsSameAs(other.m_bmp);
    }

    bool operator==(const DataViewBitmapText& other) const {
        return IsSameAs(other);
    }

    bool operator!=(const DataViewBitmapText& other) const {
        return !IsSameAs(other);
    }

private:
    wxString    m_text;
    wxBitmap    m_bmp;

    wxDECLARE_DYNAMIC_CLASS(DataViewBitmapText);
};
DECLARE_VARIANT_OBJECT(DataViewBitmapText)


// ----------------------------------------------------------------------------
// ObjectDataViewModelNode: a node inside ObjectDataViewModel
// ----------------------------------------------------------------------------

enum ItemType {
    itUndef         = 0,
    itObject        = 1,
    itVolume        = 2,
    itInstanceRoot  = 4,
    itInstance      = 8,
    itSettings      = 16,
    itLayerRoot     = 32,
    itLayer         = 64,
};

enum ColumnNumber
{
    colName         = 0,    // item name
    colPrint           ,    // printable property
    colExtruder        ,    // extruder selection
    colEditing         ,    // item editing
};

enum PrintIndicator
{
    piUndef         = 0,    // no print indicator
    piPrintable        ,    // printable
    piUnprintable      ,    // unprintable
};

class ObjectDataViewModelNode;
WX_DEFINE_ARRAY_PTR(ObjectDataViewModelNode*, MyObjectTreeModelNodePtrArray);

class ObjectDataViewModelNode
{
    ObjectDataViewModelNode*	    m_parent;
    MyObjectTreeModelNodePtrArray   m_children;
    wxBitmap                        m_empty_bmp;
    size_t                          m_volumes_cnt = 0;
    std::vector< std::string >      m_opt_categories;
    t_layer_height_range            m_layer_range = { 0.0f, 0.0f };

    wxString				        m_name;
    wxBitmap&                       m_bmp = m_empty_bmp;
    ItemType				        m_type;
    int                             m_idx = -1;
    bool					        m_container = false;
    wxString				        m_extruder = "default";
    wxBitmap                        m_extruder_bmp;
    wxBitmap				        m_action_icon;
    PrintIndicator                  m_printable {piUndef};
    wxBitmap				        m_printable_icon;

    std::string                     m_action_icon_name = "";
    Slic3r::ModelVolumeType         m_volume_type;

public:
    ObjectDataViewModelNode(const wxString& name,
                            const wxString& extruder):
        m_parent(NULL),
        m_name(name),
        m_type(itObject),
        m_extruder(extruder)
    {
        set_action_and_extruder_icons();
        init_container();
	}

    ObjectDataViewModelNode(ObjectDataViewModelNode* parent,
                            const wxString& sub_obj_name,
                            const wxBitmap& bmp,
                            const wxString& extruder,
                            const int idx = -1 ) :
        m_parent	(parent),
        m_name		(sub_obj_name),
        m_type		(itVolume),
        m_idx       (idx),
        m_extruder  (extruder)
    {
        m_bmp = bmp;
        set_action_and_extruder_icons();
        init_container();
    }

    ObjectDataViewModelNode(ObjectDataViewModelNode* parent,
                            const t_layer_height_range& layer_range,
                            const int idx = -1,
                            const wxString& extruder = wxEmptyString );

    ObjectDataViewModelNode(ObjectDataViewModelNode* parent, const ItemType type);

    ~ObjectDataViewModelNode()
    {
        // free all our children nodes
        size_t count = m_children.GetCount();
        for (size_t i = 0; i < count; i++)
        {
            ObjectDataViewModelNode *child = m_children[i];
            delete child;
        }
#ifndef NDEBUG
        // Indicate that the object was deleted.
        m_idx = -2;
#endif /* NDEBUG */
    }

	void init_container();
	bool IsContainer() const
	{
		return m_container;
	}

    ObjectDataViewModelNode* GetParent()
    {
        assert(m_parent == nullptr || m_parent->valid());
        return m_parent;
    }
    MyObjectTreeModelNodePtrArray& GetChildren()
    {
        return m_children;
    }
    ObjectDataViewModelNode* GetNthChild(unsigned int n)
    {
        return m_children.Item(n);
    }
    void Insert(ObjectDataViewModelNode* child, unsigned int n)
    {
        if (!m_container)
            m_container = true;
        m_children.Insert(child, n);
    }
    void Append(ObjectDataViewModelNode* child)
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

    bool            SetValue(const wxVariant &variant, unsigned int col);

    void            SetBitmap(const wxBitmap &icon) { m_bmp = icon; }
    const wxBitmap& GetBitmap() const               { return m_bmp; }
    const wxString& GetName() const                 { return m_name; }
    ItemType        GetType() const                 { return m_type; }
	void			SetIdx(const int& idx);
	int             GetIdx() const                  { return m_idx; }
	t_layer_height_range    GetLayerRange() const   { return m_layer_range; }
    PrintIndicator  IsPrintable() const             { return m_printable; }

    // use this function only for childrens
    void AssignAllVal(ObjectDataViewModelNode& from_node)
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
            frst_id < 0 || (size_t)frst_id >= GetChildCount() ||
            scnd_id < 0 || (size_t)scnd_id >= GetChildCount())
            return false;

        ObjectDataViewModelNode new_scnd = *GetNthChild(frst_id);
        ObjectDataViewModelNode new_frst = *GetNthChild(scnd_id);

        new_scnd.m_idx = m_children.Item(scnd_id)->m_idx;
        new_frst.m_idx = m_children.Item(frst_id)->m_idx;

        m_children.Item(frst_id)->AssignAllVal(new_frst);
        m_children.Item(scnd_id)->AssignAllVal(new_scnd);
        return true;
    }

    // Set action icons for node
    void        set_action_and_extruder_icons();
	// Set printable icon for node
    void        set_printable_icon(PrintIndicator printable);

    void        update_settings_digest_bitmaps();
    bool        update_settings_digest(const std::vector<std::string>& categories);
    int         volume_type() const { return int(m_volume_type); }
    void        msw_rescale();

#ifndef NDEBUG
    bool 		valid();
#endif /* NDEBUG */
    bool        invalid() const { return m_idx < -1; }

private:
    friend class ObjectDataViewModel;
};

// ----------------------------------------------------------------------------
// ObjectDataViewModel
// ----------------------------------------------------------------------------

// custom message the model sends to associated control to notify a last volume deleted from the object:
wxDECLARE_EVENT(wxCUSTOMEVT_LAST_VOLUME_IS_DELETED, wxCommandEvent);

class ObjectDataViewModel :public wxDataViewModel
{
    std::vector<ObjectDataViewModelNode*>       m_objects;
    std::vector<wxBitmap*>                      m_volume_bmps;
    wxBitmap*                                   m_warning_bmp { nullptr };

    wxDataViewCtrl*                             m_ctrl { nullptr };

public:
    ObjectDataViewModel();
    ~ObjectDataViewModel();

    wxDataViewItem Add( const wxString &name,
                        const int extruder,
                        const bool has_errors = false);
    wxDataViewItem AddVolumeChild(  const wxDataViewItem &parent_item,
                                    const wxString &name,
                                    const Slic3r::ModelVolumeType volume_type,
                                    const bool has_errors = false,
                                    const int extruder = 0,
                                    const bool create_frst_child = true);
    wxDataViewItem AddSettingsChild(const wxDataViewItem &parent_item);
    wxDataViewItem AddInstanceChild(const wxDataViewItem &parent_item, size_t num);
    wxDataViewItem AddInstanceChild(const wxDataViewItem &parent_item, const std::vector<bool>& print_indicator);
    wxDataViewItem AddLayersRoot(const wxDataViewItem &parent_item);
    wxDataViewItem AddLayersChild(  const wxDataViewItem &parent_item,
                                    const t_layer_height_range& layer_range,
                                    const int extruder = 0,
                                    const int index = -1);
    wxDataViewItem Delete(const wxDataViewItem &item);
    wxDataViewItem DeleteLastInstance(const wxDataViewItem &parent_item, size_t num);
    void DeleteAll();
    void DeleteChildren(wxDataViewItem& parent);
    void DeleteVolumeChildren(wxDataViewItem& parent);
    void DeleteSettings(const wxDataViewItem& parent);
    wxDataViewItem GetItemById(int obj_idx);
    wxDataViewItem GetItemById(const int obj_idx, const int sub_obj_idx, const ItemType parent_type);
    wxDataViewItem GetItemByVolumeId(int obj_idx, int volume_idx);
    wxDataViewItem GetItemByInstanceId(int obj_idx, int inst_idx);
    wxDataViewItem GetItemByLayerId(int obj_idx, int layer_idx);
    wxDataViewItem GetItemByLayerRange(const int obj_idx, const t_layer_height_range& layer_range);
    int  GetItemIdByLayerRange(const int obj_idx, const t_layer_height_range& layer_range);
    int  GetIdByItem(const wxDataViewItem& item) const;
    int  GetIdByItemAndType(const wxDataViewItem& item, const ItemType type) const;
    int  GetObjectIdByItem(const wxDataViewItem& item) const;
    int  GetVolumeIdByItem(const wxDataViewItem& item) const;
    int  GetInstanceIdByItem(const wxDataViewItem& item) const;
    int  GetLayerIdByItem(const wxDataViewItem& item) const;
    void GetItemInfo(const wxDataViewItem& item, ItemType& type, int& obj_idx, int& idx);
    int  GetRowByItem(const wxDataViewItem& item) const;
    bool IsEmpty() { return m_objects.empty(); }
    bool InvalidItem(const wxDataViewItem& item);

    // helper method for wxLog

    wxString    GetName(const wxDataViewItem &item) const;
    wxBitmap&   GetBitmap(const wxDataViewItem &item) const;
    wxString    GetExtruder(const wxDataViewItem &item) const;
    int         GetExtruderNumber(const wxDataViewItem &item) const;

    // helper methods to change the model

    virtual unsigned int    GetColumnCount() const override { return 3;}
    virtual wxString        GetColumnType(unsigned int col) const override{ return wxT("string"); }

    virtual void GetValue(  wxVariant &variant,
                            const wxDataViewItem &item,
                            unsigned int col) const override;
    virtual bool SetValue(  const wxVariant &variant,
                            const wxDataViewItem &item,
                            unsigned int col) override;
    bool SetValue(  const wxVariant &variant,
                    const int item_idx,
                    unsigned int col);

    void SetExtruder(const wxString& extruder, wxDataViewItem item);

    // For parent move child from cur_volume_id place to new_volume_id
    // Remaining items will moved up/down accordingly
    wxDataViewItem  ReorganizeChildren( const int cur_volume_id,
                                        const int new_volume_id,
                                        const wxDataViewItem &parent);
    wxDataViewItem  ReorganizeObjects( int current_id, int new_id);

    virtual bool    IsEnabled(const wxDataViewItem &item, unsigned int col) const override;

    virtual wxDataViewItem  GetParent(const wxDataViewItem &item) const override;
    // get object item
    wxDataViewItem          GetTopParent(const wxDataViewItem &item) const;
    virtual bool            IsContainer(const wxDataViewItem &item) const override;
    virtual unsigned int    GetChildren(const wxDataViewItem &parent,
                                        wxDataViewItemArray &array) const override;
    void GetAllChildren(const wxDataViewItem &parent,wxDataViewItemArray &array) const;
    // Is the container just a header or an item with all columns
    // In our case it is an item with all columns
    virtual bool    HasContainerColumns(const wxDataViewItem& WXUNUSED(item)) const override {	return true; }

    ItemType        GetItemType(const wxDataViewItem &item) const ;
    wxDataViewItem  GetItemByType(  const wxDataViewItem &parent_item,
                                    ItemType type) const;
    wxDataViewItem  GetSettingsItem(const wxDataViewItem &item) const;
    wxDataViewItem  GetInstanceRootItem(const wxDataViewItem &item) const;
    wxDataViewItem  GetLayerRootItem(const wxDataViewItem &item) const;
    bool    IsSettingsItem(const wxDataViewItem &item) const;
    void    UpdateSettingsDigest(   const wxDataViewItem &item,
                                    const std::vector<std::string>& categories);

    bool    IsPrintable(const wxDataViewItem &item) const;
    void    UpdateObjectPrintable(wxDataViewItem parent_item);
    void    UpdateInstancesPrintable(wxDataViewItem parent_item);

    void    SetVolumeBitmaps(const std::vector<wxBitmap*>& volume_bmps) { m_volume_bmps = volume_bmps; }
    void    SetWarningBitmap(wxBitmap* bitmap)                          { m_warning_bmp = bitmap; }
    void    SetVolumeType(const wxDataViewItem &item, const Slic3r::ModelVolumeType type);
    wxDataViewItem SetPrintableState( PrintIndicator printable, int obj_idx,
                                      int subobj_idx = -1, 
                                      ItemType subobj_type = itInstance);
    wxDataViewItem SetObjectPrintableState(PrintIndicator printable, wxDataViewItem obj_item);

    void    SetAssociatedControl(wxDataViewCtrl* ctrl) { m_ctrl = ctrl; }
    // Rescale bitmaps for existing Items
    void    Rescale();

    wxBitmap    GetVolumeIcon(const Slic3r::ModelVolumeType vol_type,
                              const bool is_marked = false);
    void        DeleteWarningIcon(const wxDataViewItem& item, const bool unmark_object = false);
    t_layer_height_range    GetLayerRangeByItem(const wxDataViewItem& item) const;

    bool        UpdateColumValues(unsigned col);
    void        UpdateExtruderBitmap(wxDataViewItem item);

private:
    wxDataViewItem AddRoot(const wxDataViewItem& parent_item, const ItemType root_type);
    wxDataViewItem AddInstanceRoot(const wxDataViewItem& parent_item);
};

// ----------------------------------------------------------------------------
// BitmapTextRenderer
// ----------------------------------------------------------------------------
#if ENABLE_NONCUSTOM_DATA_VIEW_RENDERING
class BitmapTextRenderer : public wxDataViewRenderer
#else
class BitmapTextRenderer : public wxDataViewCustomRenderer
#endif //ENABLE_NONCUSTOM_DATA_VIEW_RENDERING
{
public:
    BitmapTextRenderer( wxWindow* parent,
                        wxDataViewCellMode mode =
#ifdef __WXOSX__
                                                        wxDATAVIEW_CELL_INERT
#else
                                                        wxDATAVIEW_CELL_EDITABLE
#endif

                            ,int align = wxDVR_DEFAULT_ALIGNMENT
#if ENABLE_NONCUSTOM_DATA_VIEW_RENDERING
                            );
#else
                            ) : 
    wxDataViewCustomRenderer(wxT("DataViewBitmapText"), mode, align),
    m_parent(parent)
    {}
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
    DataViewBitmapText  m_value;
    bool                m_was_unusable_symbol {false};
    wxWindow*           m_parent {nullptr};
};


// ----------------------------------------------------------------------------
// BitmapChoiceRenderer
// ----------------------------------------------------------------------------

class BitmapChoiceRenderer : public wxDataViewCustomRenderer
{
public:
    BitmapChoiceRenderer(wxDataViewCellMode mode =
#ifdef __WXOSX__
                                                    wxDATAVIEW_CELL_INERT
#else
                                                    wxDATAVIEW_CELL_EDITABLE
#endif
                         ,int align = wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL
        ) : wxDataViewCustomRenderer(wxT("DataViewBitmapText"), mode, align) {}

    bool SetValue(const wxVariant& value);
    bool GetValue(wxVariant& value) const;

    virtual bool Render(wxRect cell, wxDC* dc, int state);
    virtual wxSize GetSize() const;

    bool        HasEditorCtrl() const override { return true; }
    wxWindow*   CreateEditorCtrl(wxWindow* parent,
                                 wxRect labelRect,
                                 const wxVariant& value) override;
    bool        GetValueFromEditorCtrl( wxWindow* ctrl,
                                        wxVariant& value) override;

private:
    DataViewBitmapText  m_value;
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
// ScalableBitmap
// ----------------------------------------------------------------------------

class ScalableBitmap
{
public:
    ScalableBitmap() {};
    ScalableBitmap( wxWindow *parent,
                    const std::string& icon_name = "",
                    const int px_cnt = 16);

    ~ScalableBitmap() {}

    wxSize  GetBmpSize() const;
    int     GetBmpWidth() const;
    int     GetBmpHeight() const;

    void                msw_rescale();

    const wxBitmap&     bmp() const { return m_bmp; }
    wxBitmap&           bmp()       { return m_bmp; }
    const std::string&  name() const{ return m_icon_name; }

    int                 px_cnt()const           {return m_px_cnt;}

private:
    wxWindow*       m_parent{ nullptr };
    wxBitmap        m_bmp = wxBitmap();
    std::string     m_icon_name = "";
    int             m_px_cnt {16};
};


// ----------------------------------------------------------------------------
// LockButton
// ----------------------------------------------------------------------------

class LockButton : public wxButton
{
public:
    LockButton(
        wxWindow *parent,
        wxWindowID id,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize);
    ~LockButton() {}

    void    OnButton(wxCommandEvent& event);

    bool    IsLocked() const                { return m_is_pushed; }
    void    SetLock(bool lock);

    // create its own Enable/Disable functions to not really disabled button because of tooltip enabling
    void    enable()                        { m_disabled = false; }
    void    disable()                       { m_disabled = true;  }

    void    msw_rescale();

protected:
    void    update_button_bitmaps();

private:
    bool        m_is_pushed = false;
    bool        m_disabled = false;

    ScalableBitmap    m_bmp_lock_closed;
    ScalableBitmap    m_bmp_lock_closed_f;
    ScalableBitmap    m_bmp_lock_open;
    ScalableBitmap    m_bmp_lock_open_f;
};


// ----------------------------------------------------------------------------
// ScalableButton
// ----------------------------------------------------------------------------

class ScalableButton : public wxButton
{
public:
    ScalableButton(){}
    ScalableButton(
        wxWindow *          parent,
        wxWindowID          id,
        const std::string&  icon_name = "",
        const wxString&     label = wxEmptyString,
        const wxSize&       size = wxDefaultSize,
        const wxPoint&      pos = wxDefaultPosition,
        long                style = wxBU_EXACTFIT | wxNO_BORDER);

    ScalableButton(
        wxWindow *          parent,
        wxWindowID          id,
        const ScalableBitmap&  bitmap,
        const wxString&     label = wxEmptyString,
        long                style = wxBU_EXACTFIT | wxNO_BORDER);

    ~ScalableButton() {}

    void SetBitmap_(const ScalableBitmap& bmp);
    void SetBitmapDisabled_(const ScalableBitmap &bmp);
    int  GetBitmapHeight();

    void    msw_rescale();

private:
    wxWindow*       m_parent;
    std::string     m_current_icon_name = "";
    std::string     m_disabled_icon_name = "";
    int             m_width {-1}; // should be multiplied to em_unit
    int             m_height{-1}; // should be multiplied to em_unit

    // bitmap dimensions 
    int             m_px_cnt{ 16 };
};


// ----------------------------------------------------------------------------
// ModeButton
// ----------------------------------------------------------------------------

class ModeButton : public ScalableButton
{
public:
    ModeButton(
        wxWindow*           parent,
        wxWindowID          id,
        const std::string&  icon_name = "",
        const wxString&     mode = wxEmptyString,
        const wxSize&       size = wxDefaultSize,
        const wxPoint&      pos = wxDefaultPosition);
    ~ModeButton() {}

    void    OnButton(wxCommandEvent& event);
    void    OnEnterBtn(wxMouseEvent& event) { focus_button(true); event.Skip(); }
    void    OnLeaveBtn(wxMouseEvent& event) { focus_button(m_is_selected); event.Skip(); }

    void    SetState(const bool state);

protected:
    void    focus_button(const bool focus);

private:
    bool        m_is_selected = false;

    wxString    m_tt_selected;
    wxString    m_tt_focused;
};



// ----------------------------------------------------------------------------
// ModeSizer
// ----------------------------------------------------------------------------

class ModeSizer : public wxFlexGridSizer
{
public:
    ModeSizer( wxWindow *parent, int hgap = 0);
    ~ModeSizer() {}

    void SetMode(const /*ConfigOptionMode*/int mode);

    void msw_rescale();

private:
    std::vector<ModeButton*> m_mode_btns;
};



// ----------------------------------------------------------------------------
// MenuWithSeparators
// ----------------------------------------------------------------------------

class MenuWithSeparators : public wxMenu
{
public:
    MenuWithSeparators(const wxString& title, long style = 0)
        : wxMenu(title, style) {}

    MenuWithSeparators(long style = 0)
        : wxMenu(style) {}

    ~MenuWithSeparators() {}

    void DestroySeparators();
    void SetFirstSeparator();
    void SetSecondSeparator();

private:
    wxMenuItem* m_separator_frst { nullptr };    // use like separator before settings item
    wxMenuItem* m_separator_scnd { nullptr };   // use like separator between settings items
};



#endif // slic3r_GUI_wxExtensions_hpp_
