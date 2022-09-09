#include "ReleaseNote.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "libslic3r/Thread.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_Preview.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "Widgets/ProgressDialog.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Widgets/StaticBox.hpp"

#include <wx/progdlg.h>
#include <wx/clipbrd.h>
#include <wx/dcgraph.h>
#include <miniz.h>
#include <algorithm>
#include "Plater.hpp"
#include "BitmapCache.hpp"

namespace Slic3r { namespace GUI {

ReleaseNoteDialog::ReleaseNoteDialog(Plater *plater /*= nullptr*/)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, _L("Release Note"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);
    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto        m_line_top   = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(30));

    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_body->Add(0, 0, 0, wxLEFT, FromDIP(38));

    auto sm = create_scaled_bitmap("BambuStudio", nullptr,  70); 
    auto brand = new wxStaticBitmap(this, wxID_ANY, sm, wxDefaultPosition, wxSize(FromDIP(70), FromDIP(70)));

    m_sizer_body->Add(brand, 0, wxALL, 0);

    m_sizer_body->Add(0, 0, 0, wxRIGHT, FromDIP(25));

    wxBoxSizer *m_sizer_right = new wxBoxSizer(wxVERTICAL);

    m_text_up_info = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
    m_text_up_info->SetFont(::Label::Head_14);
    m_text_up_info->SetForegroundColour(wxColour(0x26, 0x2E, 0x30));
    m_text_up_info->Wrap(-1);
    m_sizer_right->Add(m_text_up_info, 0, 0, 0);

    m_sizer_right->Add(0, 0, 1, wxTOP, FromDIP(15));

    m_scrollwindw_release_note = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(560), FromDIP(430)), wxVSCROLL);
    m_scrollwindw_release_note->SetScrollRate(5, 5);
    m_scrollwindw_release_note->SetBackgroundColour(wxColour(0xF8, 0xF8, 0xF8));
    m_scrollwindw_release_note->SetMaxSize(wxSize(FromDIP(560), FromDIP(430)));

    m_sizer_right->Add(m_scrollwindw_release_note, 0, wxEXPAND | wxRIGHT, FromDIP(20));
    m_sizer_body->Add(m_sizer_right, 1, wxBOTTOM | wxEXPAND, FromDIP(30));
    m_sizer_main->Add(m_sizer_body, 0, wxEXPAND, 0);

    SetSizer(m_sizer_main);
    Layout();
    m_sizer_main->Fit(this);

    Centre(wxBOTH);
}

ReleaseNoteDialog::~ReleaseNoteDialog() {}


void ReleaseNoteDialog::on_dpi_changed(const wxRect &suggested_rect)
{
}

void ReleaseNoteDialog::update_release_note(wxString release_note, std::string version) 
{ 
    m_text_up_info->SetLabel(wxString::Format(_L("version %s update information :"), version));
    wxBoxSizer * sizer_text_release_note = new wxBoxSizer(wxVERTICAL);
    auto        m_staticText_release_note = new wxStaticText(m_scrollwindw_release_note, wxID_ANY, release_note, wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_release_note->Wrap(FromDIP(530));
    sizer_text_release_note->Add(m_staticText_release_note, 0, wxALL, 5);
    m_scrollwindw_release_note->SetSizer(sizer_text_release_note);
    m_scrollwindw_release_note->Layout();
}

void UpdateVersionDialog::alter_choice(wxCommandEvent& event)
{
    wxGetApp().set_skip_version(m_remind_choice->GetValue());
}

UpdateVersionDialog::UpdateVersionDialog(wxWindow *parent)
    : DPIDialog(parent, wxID_ANY, _L("New version of Bambu Studio"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);
    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto        m_line_top   = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(30));

    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_body->Add(0, 0, 0, wxLEFT, FromDIP(38));

    auto sm    = create_scaled_bitmap("BambuStudio", nullptr, 70);
    auto brand = new wxStaticBitmap(this, wxID_ANY, sm, wxDefaultPosition, wxSize(FromDIP(70), FromDIP(70)));

    m_sizer_body->Add(brand, 0, wxALL, 0);

    m_sizer_body->Add(0, 0, 0, wxRIGHT, FromDIP(25));

    wxBoxSizer *m_sizer_right = new wxBoxSizer(wxVERTICAL);

    m_text_up_info = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
    m_text_up_info->SetFont(::Label::Head_14);
    m_text_up_info->SetForegroundColour(wxColour(0x26, 0x2E, 0x30));
    m_text_up_info->Wrap(-1);
    m_sizer_right->Add(m_text_up_info, 0, 0, 0);

    m_sizer_right->Add(0, 0, 1, wxTOP, FromDIP(15));

    m_scrollwindw_release_note = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(560), FromDIP(430)), wxVSCROLL);
    m_scrollwindw_release_note->SetScrollRate(5, 5);
    m_scrollwindw_release_note->SetBackgroundColour(wxColour(0xF8, 0xF8, 0xF8));
    m_scrollwindw_release_note->SetMaxSize(wxSize(FromDIP(560), FromDIP(430)));

    m_remind_choice = new wxCheckBox( this, wxID_ANY, _L("Don't remind me of this version again"), wxDefaultPosition, wxDefaultSize, 0 );
    m_remind_choice->SetValue(false);
    m_remind_choice->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, &UpdateVersionDialog::alter_choice,this);

    auto sizer_button = new wxBoxSizer(wxHORIZONTAL);

   
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                            std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                            std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

    m_button_ok = new Button(this, _L("OK"));
    m_button_ok->SetBackgroundColor(btn_bg_green);
    m_button_ok->SetBorderColor(*wxWHITE);
    m_button_ok->SetTextColor(*wxWHITE);
    m_button_ok->SetFont(Label::Body_12);
    m_button_ok->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetCornerRadius(FromDIP(12));

    m_button_ok->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
        EndModal(wxID_YES);
    });

    m_button_cancel = new Button(this, _L("Cancel"));
    m_button_cancel->SetBackgroundColor(*wxWHITE);
    m_button_cancel->SetBorderColor(wxColour(38, 46, 48));
    m_button_cancel->SetFont(Label::Body_12);
    m_button_cancel->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetCornerRadius(FromDIP(12));

    m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) { 
        EndModal(wxID_NO); 
    });
    
    sizer_button->Add(m_remind_choice, 0, wxALL | wxEXPAND, FromDIP(5));
    sizer_button->AddStretchSpacer();
    sizer_button->Add(m_button_ok, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_cancel, 0, wxALL, FromDIP(5));
    

    m_sizer_right->Add(m_scrollwindw_release_note, 0, wxEXPAND | wxRIGHT, FromDIP(20));
    m_sizer_right->Add(sizer_button, 0, wxEXPAND | wxRIGHT, FromDIP(20));
    
    m_sizer_body->Add(m_sizer_right, 1, wxBOTTOM | wxEXPAND, FromDIP(8));
    m_sizer_main->Add(m_sizer_body, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxBOTTOM, 10);

    SetSizer(m_sizer_main);
    Layout();
    m_sizer_main->Fit(this);

    Centre(wxBOTH);
}

UpdateVersionDialog::~UpdateVersionDialog() {}


void UpdateVersionDialog::on_dpi_changed(const wxRect &suggested_rect) {
    m_button_ok->Rescale();
    m_button_cancel->Rescale();
}

void UpdateVersionDialog::update_version_info(wxString release_note, wxString version)
{ 
   m_text_up_info->SetLabel(wxString::Format(_L("Click to download new version in default browser: %s"), version));
    wxBoxSizer *sizer_text_release_note   = new wxBoxSizer(wxVERTICAL);
    auto        m_staticText_release_note = new wxStaticText(m_scrollwindw_release_note, wxID_ANY, release_note, wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_release_note->Wrap(FromDIP(530));
    sizer_text_release_note->Add(m_staticText_release_note, 0, wxALL, 5);
    m_scrollwindw_release_note->SetSizer(sizer_text_release_note);
    m_scrollwindw_release_note->Layout();
}

 }} // namespace Slic3r::GUI
