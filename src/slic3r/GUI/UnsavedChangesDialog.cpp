#include "UnsavedChangesDialog.hpp"

#include <cstddef>
#include <string>
#include <boost/algorithm/string.hpp>
#include <boost/optional.hpp>
#include <boost/nowide/convert.hpp>

#include "wx/dataview.h"

#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "Tab.hpp"

#define FTS_FUZZY_MATCH_IMPLEMENTATION
#include "fts_fuzzy_match.h"

#include "imgui/imconfig.h"

using boost::optional;

namespace Slic3r {

namespace GUI {

// ----------------------------------------------------------------------------
//                  ModelNode: a node inside UnsavedChangesModel
// ----------------------------------------------------------------------------

// preset(root) node
ModelNode::ModelNode(const wxString& text, Preset::Type preset_type) :
    m_parent(nullptr),
    m_preset_type(preset_type),
    m_text(text)
{
}

// group node
ModelNode::ModelNode(ModelNode* parent, const wxString& text, const std::string& icon_name) :
    m_parent(parent),
    m_text(text)
{
}

// group node
ModelNode::ModelNode(ModelNode* parent, const wxString& text, bool is_option) :
    m_parent(parent),
    m_text(text),
    m_container(!is_option)
{
}


// ----------------------------------------------------------------------------
//                          UnsavedChangesModel
// ----------------------------------------------------------------------------

UnsavedChangesModel::UnsavedChangesModel(wxWindow* parent)
{
    int icon_id = 0;
    for (const std::string& icon : { "cog", "printer", "sla_printer", "spool", "resin" })
        m_icon[icon_id++] = ScalableBitmap(parent, icon);

    m_root = new ModelNode("Preset", Preset::TYPE_INVALID);
}

UnsavedChangesModel::~UnsavedChangesModel()
{
    delete m_root;
}

void UnsavedChangesModel::GetValue(wxVariant& variant, const wxDataViewItem& item, unsigned int col) const
{
    wxASSERT(item.IsOk());

    ModelNode* node = (ModelNode*)item.GetID();
    switch (col)
    {
    case colToggle:
        variant = node->m_toggle;
        break;
    case colTypeIcon:
        variant << node->m_type_icon;
        break;
    case colGroupIcon:
        variant << node->m_group_icon;
        break;
    case colMarkedText:
        variant =node->m_text;
        break;
    case colOldValue:
        variant =node->m_text;
        break;
    case colNewValue:
        variant =node->m_text;
        break;

    default:
        wxLogError("UnsavedChangesModel::GetValue: wrong column %d", col);
    }
}

bool UnsavedChangesModel::SetValue(const wxVariant& variant, const wxDataViewItem& item, unsigned int col)
{
    assert(item.IsOk());

    ModelNode* node = (ModelNode*)item.GetID();
    switch (col)
    {
    case colToggle:
        node->m_toggle = variant.GetBool();
        return true;
    case colTypeIcon:
        node->m_type_icon << variant;
        return true;
    case colGroupIcon:
        node->m_group_icon << variant;
        return true;
    case colMarkedText:
        node->m_text = variant.GetString();
        return true;
    case colOldValue:
        node->m_text = variant.GetString();
        return true;
    case colNewValue:
        node->m_text = variant.GetString();
        return true;
    default:
        wxLogError("UnsavedChangesModel::SetValue: wrong column");
    }
    return false;
}

bool UnsavedChangesModel::IsEnabled(const wxDataViewItem& item, unsigned int col) const
{
    assert(item.IsOk());

    ModelNode* node = (ModelNode*)item.GetID();

    // disable unchecked nodes
    return !node->IsToggle();
}

wxDataViewItem UnsavedChangesModel::GetParent(const wxDataViewItem& item) const
{
    // the invisible root node has no parent
    if (!item.IsOk())
        return wxDataViewItem(nullptr);

    ModelNode* node = (ModelNode*)item.GetID();

    // "MyMusic" also has no parent
    if (node == m_root)
        return wxDataViewItem(nullptr);

    return wxDataViewItem((void*)node->GetParent());
}

bool UnsavedChangesModel::IsContainer(const wxDataViewItem& item) const
{
    // the invisble root node can have children
    if (!item.IsOk())
        return true;

    ModelNode* node = (ModelNode*)item.GetID();
    return node->IsContainer();
}

unsigned int UnsavedChangesModel::GetChildren(const wxDataViewItem& parent, wxDataViewItemArray& array) const
{
    ModelNode* node = (ModelNode*)parent.GetID();
    if (!node) {
        array.Add(wxDataViewItem((void*)m_root));
        return 1;
    }

    if (node->GetChildCount() == 0)
        return 0;

    unsigned int count = node->GetChildren().GetCount();
    for (unsigned int pos = 0; pos < count; pos++) {
        ModelNode* child = node->GetChildren().Item(pos);
        array.Add(wxDataViewItem((void*)child));
    }

    return count;
}


wxString UnsavedChangesModel::GetColumnType(unsigned int col) const
{
    if (col == colToggle)
        return "bool";
    
    if (col < colMarkedText)
        return "wxBitmap";
    
    return "string";
}


//------------------------------------------
//          UnsavedChangesDialog
//------------------------------------------

UnsavedChangesDialog::UnsavedChangesDialog(Preset::Type type)
    : DPIDialog(NULL, wxID_ANY, _L("Unsaved Changes"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    wxColour bgr_clr = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    SetBackgroundColour(bgr_clr);

    int border = 10;
    int em = em_unit();

    changes_tree = new wxDataViewCtrl(this, wxID_ANY, wxDefaultPosition, wxSize(em * 80, em * 60), wxBORDER_SIMPLE);
    changes_tree_model = new UnsavedChangesModel(this);
    changes_tree->AssociateModel(changes_tree_model);

    changes_tree->AppendToggleColumn(L"\u2610", UnsavedChangesModel::colToggle);//2610,11,12 //2714
    changes_tree->AppendBitmapColumn("", UnsavedChangesModel::colTypeIcon);
    changes_tree->AppendBitmapColumn("", UnsavedChangesModel::colGroupIcon);

    wxDataViewTextRenderer* const markupRenderer = new wxDataViewTextRenderer();

#ifdef SUPPORTS_MARKUP
    markupRenderer->EnableMarkup();
#endif

    changes_tree->AppendColumn(new wxDataViewColumn("", markupRenderer, UnsavedChangesModel::colMarkedText, wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT));
    changes_tree->AppendColumn(new wxDataViewColumn("Old value", markupRenderer, UnsavedChangesModel::colOldValue, wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT));
    changes_tree->AppendColumn(new wxDataViewColumn("New value", markupRenderer, UnsavedChangesModel::colNewValue, wxCOL_WIDTH_AUTOSIZE, wxALIGN_LEFT));

    wxStdDialogButtonSizer* cancel_btn = this->CreateStdDialogButtonSizer(wxCANCEL);

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    topSizer->Add(new wxStaticText(this, wxID_ANY, _L("There is unsaved changes for the current preset") + ":"), 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(changes_tree, 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(cancel_btn, 0, wxEXPAND | wxALL, border);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);
}

void UnsavedChangesDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    const int& em = em_unit();

    msw_buttons_rescale(this, em, { wxID_CANCEL });

    const wxSize& size = wxSize(80 * em, 60 * em);
    SetMinSize(size);

    Fit();
    Refresh();
}

void UnsavedChangesDialog::on_sys_color_changed()
{
    // msw_rescale updates just icons, so use it
//    changes_tree_model->msw_rescale();

    Refresh();
}


}

}    // namespace Slic3r::GUI
