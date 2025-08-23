#ifndef slic3r_GUI_MultiChoiceDialog_hpp_
#define slic3r_GUI_MultiChoiceDialog_hpp_

#include "GUI_Utils.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/TextInput.hpp"
#include "Widgets/DialogButtons.hpp"

#include <wx/wx.h>
#include <vector>
#include <wx/scrolwin.h>

#include <wx/menu.h>
#include <map>


namespace Slic3r { namespace GUI {

class CheckList : public wxWindow
{
public:
    CheckList(
        wxWindow* parent,
        const wxArrayString& choices = wxArrayString(),
        long scroll_style = wxVSCROLL
    );

    wxArrayInt GetSelections();
    void SetSelections(wxArrayInt sel_array);
    void Check(int i, bool checked);
    bool IsChecked(int i);

    //void SetSize(const wxSize& size);
    void Filter(const wxString& filterText);
    void SelectAll(bool value);
    void SelectVisible(bool value);

private:
    void ShowMenu(wxMouseEvent &e);

    std::vector<wxCheckBox*> m_checks;
    int               m_list_size;
    bool              m_first_load;
    wxBoxSizer*       w_sizer;

    wxPanel*          f_bar;
    wxBoxSizer*       f_sizer;
    TextInput*        m_filter_box;
    wxTextCtrl*       m_filter_ctrl;
    wxBoxSizer*       fb_sizer;
    wxStaticBitmap*   m_menu_button;

    wxScrolledWindow* m_scroll_area;
    wxBoxSizer*       s_sizer;

    wxStaticText*     m_info;
    wxString          m_info_nonsel;
    wxString          m_info_allsel;
    wxString          m_info_empty;

    ScalableBitmap    m_search;
    ScalableBitmap    m_menu;
};

class MultiChoiceDialog : public DPIDialog
{
public:
    MultiChoiceDialog(
        wxWindow*            parent  = nullptr,
        const wxString&      message = wxEmptyString,
        const wxString&      caption = wxEmptyString,
        const wxArrayString& choices = wxArrayString()
    );
    ~MultiChoiceDialog();

    wxArrayInt GetSelections() const;

    void SetSelections(wxArrayInt sel_array);

protected:
    CheckList*    m_check_list;
    wxArrayInt    m_selected_indices;
    void on_dpi_changed(const wxRect &suggested_rect) override;
};

}} // namespace Slic3r::GUI

#endif