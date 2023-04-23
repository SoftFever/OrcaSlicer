#include "CalibrationWizardPage.hpp"
#include "CalibrationWizard.hpp"
#include "I18N.hpp"

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_CALIBRATIONPAGE_PREV, IntEvent);
wxDEFINE_EVENT(EVT_CALIBRATIONPAGE_NEXT, IntEvent);

PageButton::PageButton(wxWindow* parent, wxString text, ButtonType type)
    : m_type(type),
    Button(parent, text)
{
    StateColor btn_bg_green(std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));

    StateColor btn_bd_green(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Enabled));

    StateColor btn_bd_white(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));

    StateColor btn_text_green(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Enabled));

    StateColor btn_text_white(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));

    switch (m_type)
    {
    case Slic3r::GUI::Back:
    case Slic3r::GUI::Recalibrate:
        SetBackgroundColor(btn_bg_white);
        SetBorderColor(btn_bd_white);
        SetTextColor(btn_text_white);
        break;
    case Slic3r::GUI::Start:
    case Slic3r::GUI::Next:
    case Slic3r::GUI::Calibrate:
    case Slic3r::GUI::Save:
        SetBackgroundColor(btn_bg_green);
        SetBorderColor(btn_bd_green);
        SetTextColor(btn_text_green);
        break;
    default:
        break;
    }

    SetBackgroundColour(*wxWHITE);
    SetFont(Label::Body_13);
    SetMinSize(wxSize(-1, FromDIP(24)));
    SetCornerRadius(FromDIP(12));
}

CalibrationWizardPage::CalibrationWizardPage(wxWindow* parent, bool has_split_line, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : m_has_middle_line(has_split_line),
    wxPanel(parent, id, pos, size, style)
{
    SetBackgroundColour(*wxWHITE);

    wxBoxSizer* page_sizer;
    page_sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* title_sizer;
    title_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_title = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 0);
    m_title->Wrap(-1);
    m_title->SetFont(Label::Head_16);
    title_sizer->Add(m_title, 0, wxALL | wxEXPAND, 0);
    title_sizer->Add(0, 0, 1, wxEXPAND, 0);

    m_index = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 0);
    m_index->Wrap(-1);
    m_index->SetFont(Label::Head_16);
    title_sizer->Add(m_index, 0, wxALL, 0);

    page_sizer->Add(title_sizer, 0, wxEXPAND, 0);

    page_sizer->Add(0, FromDIP(20), 0, wxEXPAND, 0);

    m_top_sizer = new wxBoxSizer(wxVERTICAL);

    page_sizer->Add(m_top_sizer, 0, wxEXPAND, 0);

    wxBoxSizer* horiz_sizer;
    horiz_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_left_sizer = new wxBoxSizer(wxVERTICAL);
    horiz_sizer->Add(m_left_sizer, 1, wxEXPAND, 0);

    auto middle_line = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(1, -1), wxTAB_TRAVERSAL);
    middle_line->SetBackgroundColour(wxColour(0, 0, 0));

    horiz_sizer->Add(middle_line, 0, wxEXPAND | wxRIGHT | wxLEFT, FromDIP(20));

    if (!has_split_line) {
        middle_line->Hide();
    }

    auto right_sizer = new wxBoxSizer(wxVERTICAL);

    m_right_content_sizer = new wxBoxSizer(wxVERTICAL);

    right_sizer->Add(m_right_content_sizer, 0, wxEXPAND, 0);

    m_right_btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_right_btn_sizer->Add(0, 0, 1, wxEXPAND, 0);
    m_btn_prev = new PageButton(this, "Back", Back);
    m_right_btn_sizer->Add(m_btn_prev, 0);
    m_right_btn_sizer->AddSpacer(FromDIP(10));
    m_btn_next = new PageButton(this, "Next", Next);
    m_right_btn_sizer->Add(m_btn_next, 0);

    right_sizer->Add(m_right_btn_sizer, 0, wxEXPAND, 0);

    horiz_sizer->Add(right_sizer, 1, wxEXPAND, 0);

    page_sizer->Add(horiz_sizer, 1, wxEXPAND, 0);

    this->SetSizer(page_sizer);
    this->Layout();
    page_sizer->Fit(this);

    m_btn_prev->Bind(wxEVT_BUTTON, &CalibrationWizardPage::on_click_prev, this);
    m_btn_next->Bind(wxEVT_BUTTON, &CalibrationWizardPage::on_click_next, this);
}

void CalibrationWizardPage::on_click_prev(wxCommandEvent&)
{
    IntEvent e(EVT_CALIBRATIONPAGE_PREV, static_cast<int>(m_btn_prev->GetButtonType()), m_parent);
    m_parent->GetEventHandler()->ProcessEvent(e);
}

void CalibrationWizardPage::on_click_next(wxCommandEvent&)
{
    IntEvent e(EVT_CALIBRATIONPAGE_NEXT, static_cast<int>(m_btn_next->GetButtonType()), m_parent);
    m_parent->GetEventHandler()->ProcessEvent(e);
}

}}