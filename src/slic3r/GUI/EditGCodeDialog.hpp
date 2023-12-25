#ifndef slic3r_EditGCodeDialog_hpp_
#define slic3r_EditGCodeDialog_hpp_

#include <vector>

#include <wx/gdicmn.h>
#include <slic3r/GUI/Widgets/Button.hpp>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include "libslic3r/Preset.hpp"

class wxListBox;
class wxTextCtrl;
class ScalableButton;

namespace Slic3r {

namespace GUI {

class ParamsViewCtrl;

//------------------------------------------
//          EditGCodeDialog
//------------------------------------------

class EditGCodeDialog : public DPIDialog
{
    ParamsViewCtrl*     m_params_list   {nullptr};
    ScalableButton*     m_add_btn       {nullptr};
    wxTextCtrl*         m_gcode_editor  {nullptr};
    wxStaticText*       m_param_label   {nullptr};

public:
    EditGCodeDialog(wxWindow*parent, const std::string&key, const std::string&value);
    ~EditGCodeDialog() {}

    std::string get_edited_gcode() const;

    void init_params_list();
    void add_selected_value_to_gcode();
    void bind_list_and_button();

protected:
    std::unordered_map<int, Button *> m_button_list;

    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override;
    wxBoxSizer* EditGCodeDialog::create_btn_sizer(long flags);
};




// ----------------------------------------------------------------------------
//                  ParamsModelNode: a node inside ParamsModel
// ----------------------------------------------------------------------------

class ParamsNode;
using ParamsNodePtrArray = std::vector<std::unique_ptr<ParamsNode>>;

// Discard and Cancel buttons are always but next buttons are optional
enum class GroupParamsType {
    SlicingState,
    PrintSettings,
    MaterialSettings,
    PrinterSettings,
};
using GroupParamsNodePtrMap = std::map<GroupParamsType, std::unique_ptr<ParamsNode>>;

enum class SubSlicingState {
    Undef,
    ReadOnly,
    ReadWrite,
};
using SubSlicingStateNodePtrMap = std::map<SubSlicingState, std::unique_ptr<ParamsNode>>;

enum class ParamType {
    Undef,
    Scalar,
    Vector,
    FilamentVector,
};

// On all of 3 different platforms Bitmap+Text icon column looks different 
// because of Markup text is missed or not implemented.
// As a temporary workaround, we will use:
// MSW - DataViewBitmapText (our custom renderer wxBitmap + wxString, supported Markup text)
// OSX - -//-, but Markup text is not implemented right now
// GTK - wxDataViewIconText (wxWidgets for GTK renderer wxIcon + wxString, supported Markup text)
class ParamsNode
{
    ParamsNode*         m_parent{ nullptr };
    ParamsNodePtrArray  m_children;

    GroupParamsType     m_group_type{ GroupParamsType::SlicingState };
    SubSlicingState     m_sub_type  { SubSlicingState::Undef };
    ParamType           m_param_type{ ParamType::Undef };

    // TODO/FIXME:
    // the GTK version of wxDVC (in particular wxDataViewCtrlInternal::ItemAdded)
    // needs to know in advance if a node is or _will be_ a container.
    // Thus implementing:
    //   bool IsContainer() const
    //    { return m_children.size()>0; }
    // doesn't work with wxGTK when DiffModel::AddToClassical is called
    // AND the classical node was removed (a new node temporary without children
    // would be added to the control)
    bool                m_container{ true };

public:

#ifdef __linux__
    wxIcon      icon;
#else
    wxBitmap    icon;
#endif //__linux__
    std::string icon_name;
    std::string param_key;
    wxString    text;

    // Group params(root) node
    ParamsNode(GroupParamsType type);

    // sub SlicingState node
    ParamsNode(ParamsNode* parent, SubSlicingState sub_type);

    // parametre node
    ParamsNode( ParamsNode*         parent, 
                ParamType           param_type,
                const std::string&  param_key,
                SubSlicingState     subgroup_type);

    bool             IsContainer()      const { return m_container; }
    bool             IsGroupNode()      const { return m_parent == nullptr; }
    bool             IsSubGroupNode()   const { return m_sub_type != SubSlicingState::Undef; }
    bool             IsParamNode()      const { return m_param_type != ParamType::Undef; }

    ParamsNode* GetParent() { return m_parent; }
    ParamsNodePtrArray& GetChildren() { return m_children; }

    void Append(std::unique_ptr<ParamsNode> child) { m_children.emplace_back(std::move(child)); }
};


// ----------------------------------------------------------------------------
//                  ParamsModel
// ----------------------------------------------------------------------------

class ParamsModel : public wxDataViewModel
{
    GroupParamsNodePtrMap       m_group_nodes;
    SubSlicingStateNodePtrMap   m_sub_slicing_state_nodes;

    wxDataViewCtrl*             m_ctrl{ nullptr };

public:

    ParamsModel();
    ~ParamsModel() override = default;

    void            SetAssociatedControl(wxDataViewCtrl* ctrl) { m_ctrl = ctrl; }

    wxDataViewItem  AppendGroup(GroupParamsType     type);

    wxDataViewItem  AppendSubGroup( GroupParamsType type,
                                    SubSlicingState sub_type);

    wxDataViewItem  AppendParam(GroupParamsType     type,
                                ParamType           param_type,
                                const std::string&  param_key,
                                SubSlicingState     subgroup_type = SubSlicingState::Undef);

    wxString        GetParamName(wxDataViewItem item);
    std::string     GetParamKey(wxDataViewItem item);

    void            Rescale();

    void            Clear();

    wxDataViewItem  GetParent(const wxDataViewItem& item) const override;
    unsigned int    GetChildren(const wxDataViewItem& parent, wxDataViewItemArray& array) const override;

    void GetValue(wxVariant& variant, const wxDataViewItem& item, unsigned int col) const override;
    bool SetValue(const wxVariant& variant, const wxDataViewItem& item, unsigned int col) override;

    bool IsContainer(const wxDataViewItem& item) const override;
    // Is the container just a header or an item with all columns
    // In our case it is an item with all columns
    bool HasContainerColumns(const wxDataViewItem& WXUNUSED(item)) const override { return true; }
};


// ----------------------------------------------------------------------------
//                  ParamsViewCtrl
// ----------------------------------------------------------------------------

class ParamsViewCtrl : public wxDataViewCtrl
{
    int                     m_em_unit;

    //struct ItemData
    //{
    //    std::string     opt_key;
    //    wxString        opt_name;
    //    wxString        old_val;
    //    wxString        mod_val;
    //    wxString        new_val;
    //    Preset::Type    type;
    //    bool            is_long{ false };
    //};

    //// tree items related to the options
    //std::map<wxDataViewItem, ItemData> m_items_map;

public:
    ParamsViewCtrl(wxWindow* parent, wxSize size);
    ~ParamsViewCtrl() override {
        if (model) {
            Clear();
            model->DecRef();
        }
    }

    ParamsModel* model{ nullptr };

    wxDataViewItem AppendGroup( GroupParamsType type);

    wxDataViewItem AppendParam( GroupParamsType     group_type,
                                ParamType           param_type,
                                const std::string&  param_key,
                                SubSlicingState     group_subgroup_type = SubSlicingState::Undef);

    wxString        GetValue(wxDataViewItem item);
    wxString        GetSelectedValue();
    std::string     GetSelectedParamKey();

    void    Clear();
    void    Rescale(int em = 0);

    void    set_em_unit(int em) { m_em_unit = em; }
};


} // namespace GUI
} // namespace Slic3r

#endif
