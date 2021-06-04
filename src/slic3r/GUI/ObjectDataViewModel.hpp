#ifndef slic3r_GUI_ObjectDataViewModel_hpp_
#define slic3r_GUI_ObjectDataViewModel_hpp_

#include <wx/dataview.h>
#include <vector>

#include "ExtraRenderers.hpp"

namespace Slic3r {

enum class ModelVolumeType : int;

namespace GUI {

typedef double                          coordf_t;
typedef std::pair<coordf_t, coordf_t>   t_layer_height_range;

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
    itInfo          = 128
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

enum class InfoItemType
{
    Undef,
    CustomSupports,
    CustomSeam,
    MmuSegmentation,
    VariableLayerHeight
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
    ModelVolumeType                 m_volume_type;
    InfoItemType                    m_info_item_type {InfoItemType::Undef};

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
                            Slic3r::ModelVolumeType type,
                            const wxBitmap& bmp,
                            const wxString& extruder,
                            const int idx = -1);

    ObjectDataViewModelNode(ObjectDataViewModelNode* parent,
                            const t_layer_height_range& layer_range,
                            const int idx = -1,
                            const wxString& extruder = wxEmptyString );

    ObjectDataViewModelNode(ObjectDataViewModelNode* parent, const ItemType type);
    ObjectDataViewModelNode(ObjectDataViewModelNode* parent, const InfoItemType type);

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
    void            SetVolumeType(ModelVolumeType type) { m_volume_type = type; }
    void            SetBitmap(const wxBitmap &icon) { m_bmp = icon; }
    void            SetExtruder(const wxString &extruder) { m_extruder = extruder; }
    const wxBitmap& GetBitmap() const               { return m_bmp; }
    const wxString& GetName() const                 { return m_name; }
    ItemType        GetType() const                 { return m_type; }
    InfoItemType    GetInfoItemType() const         { return m_info_item_type; }
	void			SetIdx(const int& idx);
	int             GetIdx() const                  { return m_idx; }
    ModelVolumeType GetVolumeType()                 { return m_volume_type; }
	t_layer_height_range    GetLayerRange() const   { return m_layer_range; }
    wxString        GetExtruder()                   { return m_extruder; }
    PrintIndicator  IsPrintable() const             { return m_printable; }
    void            UpdateExtruderAndColorIcon(wxString extruder = "");

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

    // Set action and extruder(if any exist) icons for node
    void        set_action_and_extruder_icons();
    // set extruder icon for node
    void        set_extruder_icon();
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
    std::vector<wxBitmap>                       m_volume_bmps;
    wxBitmap                                    m_warning_bmp;
    wxBitmap                                    m_info_bmp;

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
    wxDataViewItem AddInfoChild(const wxDataViewItem &parent_item, InfoItemType info_type);
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

    unsigned int    GetColumnCount() const override { return 3;}
    wxString        GetColumnType(unsigned int col) const override;

    void GetValue(  wxVariant &variant,
                    const wxDataViewItem &item,
                    unsigned int col) const override;
    bool SetValue(  const wxVariant &variant,
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

    bool    IsEnabled(const wxDataViewItem &item, unsigned int col) const override;

    wxDataViewItem  GetParent(const wxDataViewItem &item) const override;
    // get object item
    wxDataViewItem          GetTopParent(const wxDataViewItem &item) const;
    bool            IsContainer(const wxDataViewItem &item) const override;
    unsigned int    GetChildren(const wxDataViewItem &parent, wxDataViewItemArray &array) const override;
    void GetAllChildren(const wxDataViewItem &parent,wxDataViewItemArray &array) const;
    // Is the container just a header or an item with all columns
    // In our case it is an item with all columns
    bool    HasContainerColumns(const wxDataViewItem& WXUNUSED(item)) const override {	return true; }

    ItemType        GetItemType(const wxDataViewItem &item) const;
    InfoItemType    GetInfoItemType(const wxDataViewItem &item) const;
    wxDataViewItem  GetItemByType(  const wxDataViewItem &parent_item,
                                    ItemType type) const;
    wxDataViewItem  GetSettingsItem(const wxDataViewItem &item) const;
    wxDataViewItem  GetInstanceRootItem(const wxDataViewItem &item) const;
    wxDataViewItem  GetLayerRootItem(const wxDataViewItem &item) const;
    wxDataViewItem  GetInfoItemByType(const wxDataViewItem &parent_item, InfoItemType type) const;

    bool    IsSettingsItem(const wxDataViewItem &item) const;
    void    UpdateSettingsDigest(   const wxDataViewItem &item,
                                    const std::vector<std::string>& categories);

    bool    IsPrintable(const wxDataViewItem &item) const;
    void    UpdateObjectPrintable(wxDataViewItem parent_item);
    void    UpdateInstancesPrintable(wxDataViewItem parent_item);

    void    SetVolumeType(const wxDataViewItem &item, const Slic3r::ModelVolumeType type);
    ModelVolumeType GetVolumeType(const wxDataViewItem &item);
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
    void        UpdateVolumesExtruderBitmap(wxDataViewItem object_item);
    int         GetDefaultExtruderIdx(wxDataViewItem item);

private:
    wxDataViewItem  AddRoot(const wxDataViewItem& parent_item, const ItemType root_type);
    wxDataViewItem  AddInstanceRoot(const wxDataViewItem& parent_item);
    void            AddAllChildren(const wxDataViewItem& parent);
};


}
}


#endif // slic3r_GUI_ObjectDataViewModel_hpp_
