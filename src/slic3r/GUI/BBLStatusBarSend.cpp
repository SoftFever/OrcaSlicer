#include "BBLStatusBarSend.hpp"

#include <wx/timer.h>
#include <wx/gauge.h>
#include <wx/button.h>
#include <wx/statusbr.h>
#include <wx/frame.h>
#include "wx/evtloop.h"
#include <wx/gdicmn.h>
#include "GUI_App.hpp"

#include "I18N.hpp"

#include <iostream>


namespace Slic3r {


BBLStatusBarSend::BBLStatusBarSend(wxWindow *parent, int id)
 : m_self{new wxPanel(parent, id == -1 ? wxID_ANY : id)} 
    , m_sizer(new wxBoxSizer(wxHORIZONTAL))
{
    m_self->SetBackgroundColour(wxColour(255,255,255));

    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *m_sizer_bottom = new wxBoxSizer(wxHORIZONTAL);

    m_status_text = new wxStaticText(m_self, wxID_ANY, L(""), wxDefaultPosition, wxSize(m_self->FromDIP(280), -1), 0);
    m_status_text->SetForegroundColour(wxColour(107, 107, 107));
    m_status_text->SetFont(::Label::Body_13);
    m_status_text->Wrap(m_self->FromDIP(280));
    m_status_text->SetSize(wxSize(m_self->FromDIP(280), m_self->FromDIP(46)));
    m_status_text->SetMaxSize(wxSize(m_self->FromDIP(280), m_self->FromDIP(46)));

    //m_status_text->SetSize()
   

    m_prog = new wxGauge(m_self, wxID_ANY, 100, wxDefaultPosition, wxSize(-1, m_self->FromDIP(6)), wxGA_HORIZONTAL);
    m_prog->SetValue(0);

  /*  block_left = new wxWindow(m_prog, wxID_ANY, wxPoint(0, 0), wxSize(2, m_prog->GetSize().GetHeight() * 2));
      block_left->SetBackgroundColour(wxColour(255, 255, 255));
      block_right = new wxWindow(m_prog, wxID_ANY, wxPoint(m_prog->GetSize().GetWidth() - 2, 0), wxSize(2, m_prog->GetSize().GetHeight() * 2));
      block_right->SetBackgroundColour(wxColour(255, 255, 255));*/

    m_sizer_bottom->Add(m_prog, 1, wxALIGN_CENTER, 0);

     StateColor btn_bd_white(std::pair<wxColour, int>(*wxWHITE, StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));

    m_cancelbutton = new Button(m_self, _L("Cancel"));
    m_cancelbutton->SetMinSize(wxSize(m_self->FromDIP(64), m_self->FromDIP(24)));
    m_cancelbutton->SetBackgroundColor(wxColour(255, 255, 255));
    m_cancelbutton->SetBorderColor(btn_bd_white);
    m_cancelbutton->SetCornerRadius(m_self->FromDIP(12));
    m_cancelbutton->Bind(wxEVT_BUTTON, 
        [this](wxCommandEvent &evt) {
        m_was_cancelled = true;
        if (m_cancel_cb_fina)
            m_cancel_cb_fina();
    });

    m_stext_percent = new wxStaticText(m_self, wxID_ANY, L(""), wxDefaultPosition, wxDefaultSize, 0);
    m_stext_percent->SetForegroundColour(wxColour(107, 107, 107));
    m_stext_percent->SetFont(::Label::Body_13);
    m_stext_percent->Wrap(-1);
    m_sizer_bottom->Add(m_stext_percent, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, 10);

    m_sizer_bottom->Add(m_cancelbutton, 0, wxALIGN_CENTER, 0);

    m_sizer_body->Add(m_status_text, 0, 0, 0);
    m_sizer_body->Add(0, 0, 0, wxTOP, 1);
    m_sizer_body->Add(m_sizer_bottom, 1, wxEXPAND, 0);

    m_sizer->Add(m_sizer_body, 1, wxALIGN_CENTER, 0);

    m_self->SetSizer(m_sizer);
    m_self->Layout();
    m_sizer->Fit(m_self);
    //set_prog_block();
}

void BBLStatusBarSend::set_prog_block()
{
    //block_left->SetPosition(wxPoint(0, 0));
    //block_right->SetPosition(wxPoint(m_prog->GetSize().GetWidth() - 2, 0));
}

int BBLStatusBarSend::get_progress() const
{
    return m_prog->GetValue();
}

void BBLStatusBarSend::set_progress(int val)
{
    //set_prog_block();

    if(val < 0)
        return;

    //add the logic for arrange/orient jobs, which don't call stop_busy
    if (!m_prog->IsShown()) {
        m_sizer->Show(m_prog);
        m_sizer->Show(m_cancelbutton);
    }
    m_prog->SetValue(val);
    set_percent_text(wxString::Format("%d%%", val));
    m_sizer->Layout();
}

int BBLStatusBarSend::get_range() const
{
    return m_prog->GetRange();
}

void BBLStatusBarSend::set_range(int val)
{
    if(val != m_prog->GetRange()) {
        m_prog->SetRange(val);
    }
}

void BBLStatusBarSend::show_progress(bool show)
{
    if (show) {
        m_sizer->Show(m_prog);
        m_sizer->Layout();
    }
    else {
        m_sizer->Hide(m_prog);
        m_sizer->Layout();
    }
}

void BBLStatusBarSend::start_busy(int rate)
{
    m_busy = true;
    show_progress(true);
    show_cancel_button();
}

void BBLStatusBarSend::stop_busy()
{
    show_progress(false);
    hide_cancel_button();
    m_prog->SetValue(0);
    m_sizer->Layout();
    m_busy = false;
}

void BBLStatusBarSend::set_cancel_callback_fina(BBLStatusBarSend::CancelFn ccb) 
{ 
    m_cancel_cb_fina = ccb; 
     if (ccb) {
        m_sizer->Show(m_cancelbutton);
    } else {
        m_sizer->Hide(m_cancelbutton);
    }
}

void BBLStatusBarSend::set_cancel_callback(BBLStatusBarSend::CancelFn ccb) {
    /*  m_cancel_cb = ccb;
      if (ccb) {
          m_sizer->Show(m_cancelbutton);
      }
      else {
          m_sizer->Hide(m_cancelbutton);
      }
      m_sizer->Layout();*/
}

wxPanel* BBLStatusBarSend::get_panel()
{
    return m_self;
}

bool BBLStatusBarSend::is_english_text(wxString str)
{
    std::regex reg("^[0-9a-zA-Z]+$");
    std::smatch matchResult;

    std::string pattern_Special = "{}[]<>~!@#$%^&*(),.?/ :";
    for (auto i = 0; i < str.Length(); i++) {
        std::string regex_str = wxString(str[i]).ToStdString();
        if (std::regex_match(regex_str, matchResult, reg)) {
            continue;
        }
        else {
            int result = pattern_Special.find(regex_str.c_str());
            if (result < 0 || result > pattern_Special.length()) {
                return false;
            }
        }
    }
    return true;
}

wxString BBLStatusBarSend::format_text(wxStaticText* st, wxString str, int warp)
{
    int index = 0;
    if (!str.empty()) {
        while ((index = str.find('\n', index)) != string::npos) {
            str.erase(index, 1);
        }
    }

    wxString out_txt = str;
    wxString count_txt = "";
    int      new_line_pos = 0;

    for (int i = 0; i < str.length(); i++) {
        auto text_size = st->GetTextExtent(count_txt);
        if (text_size.x < warp) {
            count_txt += str[i];
        }
        else {
            out_txt.insert(i - 1, '\n');
            count_txt = "";
        }
    }
    return out_txt;
}

void BBLStatusBarSend::set_status_text(const wxString& txt)
{
    //auto txtss = "Sending the printing task has timed out.\nPlease try again!";
    //auto txtss = "The printing project is being uploaded... 25%%";
    //m_status_text->SetLabelText(txtss);
    wxString str = format_text(m_status_text,txt,280);
    m_status_text->SetLabelText(str);
    //if (is_english_text(str)) m_status_text->Wrap(m_self->FromDIP(280));
}

void BBLStatusBarSend::set_percent_text(const wxString &txt)
{
    m_stext_percent->SetLabelText(txt);
}

void BBLStatusBarSend::set_status_text(const std::string& txt)
{ 
    this->set_status_text(txt.c_str());
}

void BBLStatusBarSend::set_status_text(const char *txt)
{ 
    this->set_status_text(wxString::FromUTF8(txt));
}

void BBLStatusBarSend::msw_rescale() { 
    //set_prog_block();
    m_cancelbutton->SetMinSize(wxSize(m_self->FromDIP(56), m_self->FromDIP(24)));
}

wxString BBLStatusBarSend::get_status_text() const
{
    return m_status_text->GetLabelText();
}

bool BBLStatusBarSend::update_status(wxString &msg, bool &was_cancel, int percent, bool yield)
{
    //auto test_txt = _L("Unkown Error.") + _L("status=150, body=Timeout was reached: Connection timed out after 10009 milliseconds [Error 28]");
    set_status_text(msg);

    if (percent >= 0)
        this->set_progress(percent);

    if (yield)
        wxEventLoopBase::GetActive()->YieldFor(wxEVT_CATEGORY_UI | wxEVT_CATEGORY_USER_INPUT);
    was_cancel = m_was_cancelled;
    return true;
}

void BBLStatusBarSend::reset()
{
    set_status_text("");
    m_was_cancelled = false;
    set_progress(0);
    set_percent_text(wxString::Format("%d%%", 0));
}


void BBLStatusBarSend::set_font(const wxFont &font)
{
    m_self->SetFont(font);
}

void BBLStatusBarSend::show_cancel_button()
{
    m_sizer->Show(m_cancelbutton);
    m_sizer->Layout();
}

void BBLStatusBarSend::hide_cancel_button()
{
    m_sizer->Hide(m_cancelbutton);
    m_sizer->Layout();
}

void BBLStatusBarSend::change_button_label(wxString name) 
{
    m_cancelbutton->SetLabel(name);
}

}
