#ifndef slic3r_UnsavedChangesDialog_hpp_
#define slic3r_UnsavedChangesDialog_hpp_

#include <wx/dataview.h>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include "libslic3r/Preset.hpp"

namespace Slic3r {

namespace GUI{

// ----------------------------------------------------------------------------
//                  ModelNode: a node inside UnsavedChangesModel
// ----------------------------------------------------------------------------

class ModelNode;
WX_DEFINE_ARRAY_PTR(ModelNode*, ModelNodePtrArray);

// On all of 3 different platforms Bitmap+Text icon column looks different 
// because of Markup text is missed or not implemented.
// As a temporary workaround, we will use:
// MSW - DataViewBitmapText (our custom renderer wxBitmap + wxString, supported Markup text)
// OSX - -//-, but Markup text is not implemented right now
// GTK - wxDataViewIconText (wxWidgets for GTK renderer wxIcon + wxString, supported Markup text)
class ModelNode
{
    wxWindow* m_parent_win{ nullptr };

    ModelNode*          m_parent;
    ModelNodePtrArray   m_children;
    wxBitmap            m_empty_bmp;
    Preset::Type        m_preset_type {Preset::TYPE_INVALID};

    // saved values for colors if they exist
    wxString            m_old_color;
    wxString            m_new_color;

    // TODO/FIXME:
    // the GTK version of wxDVC (in particular wxDataViewCtrlInternal::ItemAdded)
    // needs to know in advance if a node is or _will be_ a container.
    // Thus implementing:
    //   bool IsContainer() const
    //    { return m_children.GetCount()>0; }
    // doesn't work with wxGTK when UnsavedChangesModel::AddToClassical is called
    // AND the classical node was removed (a new node temporary without children
    // would be added to the control)
    bool                m_container {true};

    wxBitmap            get_bitmap(const wxString& color);

public:

    bool        m_toggle {true};
#ifdef __linux__
    wxIcon      m_icon;
#else
    wxBitmap    m_icon;
#endif //__linux__
    wxBitmap    m_old_color_bmp;
    wxBitmap    m_new_color_bmp;
    wxString    m_text;
    wxString    m_old_value;
    wxString    m_new_value;

    // preset(root) node
    ModelNode(Preset::Type preset_type, const wxString& text);

    // category node
    ModelNode(ModelNode* parent, const wxString& text, const std::string& icon_name);

    // group node
    ModelNode(ModelNode* parent, const wxString& text);

    // option node
    ModelNode(ModelNode* parent, const wxString& text, const wxString& old_value, const wxString& new_value);

    ~ModelNode() {
        // free all our children nodes
        size_t count = m_children.GetCount();
        for (size_t i = 0; i < count; i++) {
            ModelNode* child = m_children[i];
            delete child;
        }
    }

    bool                IsContainer() const         { return m_container; }
    bool                IsToggled() const           { return m_toggle; }
    void                Toggle(bool toggle = true)  { m_toggle = toggle; }
    bool                IsRoot() const              { return m_parent == nullptr; }
    Preset::Type        type() const                { return m_preset_type; }
    const wxString&     text() const                { return m_text; }

    ModelNode*          GetParent()                 { return m_parent; }
    ModelNodePtrArray&  GetChildren()               { return m_children; }
    ModelNode*          GetNthChild(unsigned int n) { return m_children.Item(n); }
    unsigned int        GetChildCount() const       { return m_children.GetCount(); }

    void Insert(ModelNode* child, unsigned int n)   { m_children.Insert(child, n); }
    void Append(ModelNode* child)                   { m_children.Add(child); }

    void UpdateEnabling();
};


// ----------------------------------------------------------------------------
//                  UnsavedChangesModel
// ----------------------------------------------------------------------------

class UnsavedChangesModel : public wxDataViewModel
{
    wxWindow*               m_parent_win {nullptr};
    std::vector<ModelNode*> m_preset_nodes;

    wxDataViewCtrl*         m_ctrl{ nullptr };

    ModelNode *AddOption(ModelNode *group_node,
                         wxString   option_name,
                         wxString   old_value,
                         wxString   new_value);
    ModelNode *AddOptionWithGroup(ModelNode *category_node,
                                  wxString   group_name,
                                  wxString   option_name,
                                  wxString   old_value,
                                  wxString   new_value);
    ModelNode *AddOptionWithGroupAndCategory(ModelNode *preset_node,
                                             wxString   category_name,
                                             wxString   group_name,
                                             wxString   option_name,
                                             wxString   old_value,
                                             wxString   new_value);

public:
    enum {
        colToggle,
        colIconText,
        colOldValue,
        colNewValue,
        colMax
    };

    UnsavedChangesModel(wxWindow* parent);
    ~UnsavedChangesModel();

    void            SetAssociatedControl(wxDataViewCtrl* ctrl) { m_ctrl = ctrl; }

    wxDataViewItem  AddPreset(Preset::Type type, wxString preset_name);
    wxDataViewItem  AddOption(Preset::Type type, wxString category_name, wxString group_name, wxString option_name,
                              wxString old_value, wxString new_value);

    void            UpdateItemEnabling(wxDataViewItem item);

    unsigned int    GetColumnCount() const override { return colMax; }
    wxString        GetColumnType(unsigned int col) const override;

    wxDataViewItem  GetParent(const wxDataViewItem& item) const override;
    unsigned int    GetChildren(const wxDataViewItem& parent, wxDataViewItemArray& array) const override;

    void GetValue(wxVariant& variant, const wxDataViewItem& item, unsigned int col) const override;
    bool SetValue(const wxVariant& variant, const wxDataViewItem& item, unsigned int col) override;

    bool IsEnabled(const wxDataViewItem& item, unsigned int col) const override;
    bool IsContainer(const wxDataViewItem& item) const override;
    // Is the container just a header or an item with all columns
    // In our case it is an item with all columns
    bool HasContainerColumns(const wxDataViewItem& WXUNUSED(item)) const override { return true; }
};


//------------------------------------------
//          UnsavedChangesDialog
//------------------------------------------
class UnsavedChangesDialog : public DPIDialog
{
    wxDataViewCtrl*         m_tree          { nullptr };
    UnsavedChangesModel*    m_tree_model    { nullptr };

    int m_save_btn_id       { wxID_ANY };
    int m_move_btn_id       { wxID_ANY };
    int m_continue_btn_id   { wxID_ANY };

    enum class Action {
        Save,
        Move,
        Continue
    } m_action;

public:
    UnsavedChangesDialog(Preset::Type type);
    ~UnsavedChangesDialog() {}

    void update(Preset::Type type);
    void item_value_changed(wxDataViewEvent &event);
    void close(Action action);

    bool save_preset() const    { return m_action == Action::Save;      }
    bool move_preset() const    { return m_action == Action::Move;      }
    bool just_continue() const  { return m_action == Action::Continue;  }

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override;
};


} 
}

#endif //slic3r_UnsavedChangesDialog_hpp_
