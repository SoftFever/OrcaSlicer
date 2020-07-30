#ifndef slic3r_UnsavedChangesDialog_hpp_
#define slic3r_UnsavedChangesDialog_hpp_

#include <vector>
#include <map>

#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/listctrl.h>
#include <wx/dataview.h>

#include <wx/combo.h>

#include <wx/checkbox.h>
#include <wx/dialog.h>

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

class ModelNode
{
    ModelNode*          m_parent;
    ModelNodePtrArray   m_children;
    wxBitmap            m_empty_bmp;
    Preset::Type        m_preset_type {Preset::TYPE_INVALID};

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

public:

    bool        m_toggle {true};
    wxBitmap    m_type_icon;
    wxBitmap    m_group_icon;
    wxString    m_text;
    wxString    m_old_value;
    wxString    m_new_value;

    // preset(root) node
    ModelNode(const wxString& text, Preset::Type preset_type);

    // group node
    ModelNode(ModelNode* parent, const wxString& text, const std::string& icon_name);

    // group node
    ModelNode(ModelNode* parent, const wxString& text, bool is_option);

    ~ModelNode() {
        // free all our children nodes
        size_t count = m_children.GetCount();
        for (size_t i = 0; i < count; i++) {
            ModelNode* child = m_children[i];
            delete child;
        }
    }

    bool                IsContainer() const         { return m_container; }
    bool                IsToggle() const            { return m_toggle; }

    ModelNode*          GetParent()                 { return m_parent; }
    ModelNodePtrArray&  GetChildren()               { return m_children; }
    ModelNode*          GetNthChild(unsigned int n) { return m_children.Item(n); }
    unsigned int        GetChildCount() const       { return m_children.GetCount(); }

    void Insert(ModelNode* child, unsigned int n) { m_children.Insert(child, n); }
    void Append(ModelNode* child) { m_children.Add(child); }
};


// ----------------------------------------------------------------------------
//                  UnsavedChangesModel
// ----------------------------------------------------------------------------

class UnsavedChangesModel : public wxDataViewModel
{
    ModelNode* m_root;
    ScalableBitmap                          m_icon[5];

public:
    enum {
        colToggle,
        colTypeIcon,
        colGroupIcon,
        colMarkedText,
        colOldValue,
        colNewValue,
        colMax
    };

    UnsavedChangesModel(wxWindow* parent);
    ~UnsavedChangesModel();

    virtual unsigned int    GetColumnCount() const override { return colMax; }
    virtual wxString        GetColumnType(unsigned int col) const override;

    virtual wxDataViewItem  GetParent(const wxDataViewItem& item) const override;
    virtual unsigned int    GetChildren(const wxDataViewItem& parent, wxDataViewItemArray& array) const override;

    virtual void GetValue(wxVariant& variant, const wxDataViewItem& item, unsigned int col) const override;
    virtual bool SetValue(const wxVariant& variant, const wxDataViewItem& item, unsigned int col) override;

    virtual bool IsEnabled(const wxDataViewItem& item, unsigned int col) const override;
    virtual bool IsContainer(const wxDataViewItem& item) const override;

};


//------------------------------------------
//          UnsavedChangesDialog
//------------------------------------------
class UnsavedChangesDialog : public DPIDialog
{
    wxDataViewCtrl*         changes_tree{ nullptr };
    UnsavedChangesModel*    changes_tree_model{ nullptr };

public:
    UnsavedChangesDialog(Preset::Type type);
    ~UnsavedChangesDialog() {}

    void ProcessSelection(wxDataViewItem selection);

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override;
};


} 
}

#endif //slic3r_UnsavedChangesDialog_hpp_
