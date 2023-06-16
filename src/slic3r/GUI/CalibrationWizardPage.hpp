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

enum class PageType {
    Start,
    Preset,
    Calibration,
    CoarseSave,
    FineCalibration,
    Save,
    Finish,
};

class CalibrationWizardPage : public wxPanel 
{
public:
    CalibrationWizardPage(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
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

    wxBoxSizer* get_top_hsizer() { return m_top_sizer; }
    wxBoxSizer* get_content_vsizer() { return m_content_sizer; }
    wxBoxSizer* get_btn_hsizer() { return m_btn_sizer; }
    PageButton* get_prev_btn() { return m_btn_prev; }
    PageButton* get_next_btn() { return m_btn_next; }
    PageType get_page_type() { return m_page_type; }

    void set_page_type(PageType type) { m_page_type = type; }
    void set_page_title(wxString title) { m_title->SetLabel(title); }
    void set_highlight_step_text(PageType page_type);

private:
    wxStaticText* m_title;
    wxBoxSizer* m_top_sizer;
    wxStaticText* m_preset_text;
    wxStaticText* m_calibration_text;
    wxStaticText* m_record_text;
    wxBoxSizer* m_content_sizer;
    wxBoxSizer* m_btn_sizer;
    PageButton* m_btn_prev;
    PageButton* m_btn_next;
    PageType m_page_type;

    CalibrationWizardPage* m_prev_page{nullptr};
    CalibrationWizardPage* m_next_page{nullptr};

    void on_click_prev(wxCommandEvent&);
    void on_click_next(wxCommandEvent&);
};

}} // namespace Slic3r::GUI

#endif