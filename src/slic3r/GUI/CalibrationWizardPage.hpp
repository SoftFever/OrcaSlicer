#ifndef slic3r_GUI_CalibrationWizardPage_hpp_
#define slic3r_GUI_CalibrationWizardPage_hpp_

#include "Widgets/Button.hpp"
#include "Event.hpp"

namespace Slic3r { namespace GUI {

wxDECLARE_EVENT(EVT_CALIBRATIONPAGE_PREV, IntEvent);
wxDECLARE_EVENT(EVT_CALIBRATIONPAGE_NEXT, IntEvent);

class CalibrationWizard;
enum ButtonType
{
    Start,
    Back,
    Next,
    Calibrate,
    Recalibrate,
    Save,
};

class PageButton : public Button
{
public:
    PageButton(wxWindow* parent, wxString text, ButtonType type);
    void SetButtonType(ButtonType rhs_type) { m_type = rhs_type; }
    ButtonType GetButtonType() { return m_type; }
    ~PageButton() {};
private:
    ButtonType m_type;
};

class CalibrationWizardPage : public wxPanel 
{
public:
    CalibrationWizardPage(wxWindow* parent, bool has_split_line, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~CalibrationWizardPage() {};

    CalibrationWizardPage* get_prev_page() { return m_prev_page; }
    CalibrationWizardPage* get_next_page() { return m_next_page; }
    void set_prev_page(CalibrationWizardPage* prev) { m_prev_page = prev; }
    void set_next_page(CalibrationWizardPage* next) { m_next_page = next; }
    CalibrationWizardPage* chain(CalibrationWizardPage* next)
    {
        set_next_page(next);
        next->set_prev_page(this);
        return next;
    }

    wxBoxSizer* get_top_vsizer() { return m_top_sizer; }
    wxBoxSizer* get_left_vsizer() { return m_left_sizer; }
    wxBoxSizer* get_right_content_vsizer() { return m_right_content_sizer; }
    PageButton* get_prev_btn() { return m_btn_prev; }
    PageButton* get_next_btn() { return m_btn_next; }

    void set_page_title(wxString title) { m_title->SetLabel(title); }
    void set_page_index(wxString index) { m_index->SetLabel(index); }

private:
    bool m_has_middle_line;

    wxStaticText* m_title;
    wxStaticText* m_index;
    wxBoxSizer* m_top_sizer;
    wxBoxSizer* m_left_sizer;
    wxBoxSizer* m_right_content_sizer;
    wxBoxSizer* m_right_btn_sizer;
    PageButton* m_btn_prev;
    PageButton* m_btn_next;

    CalibrationWizardPage* m_prev_page{nullptr};
    CalibrationWizardPage* m_next_page{nullptr};

    void on_click_prev(wxCommandEvent&);
    void on_click_next(wxCommandEvent&);
};

}} // namespace Slic3r::GUI

#endif