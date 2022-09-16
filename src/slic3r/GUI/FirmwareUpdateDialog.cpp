#include "FirmwareUpdateDialog.hpp"
#include <slic3r/GUI/I18N.hpp>
#include <wx/dcgraph.h>
#include <wx/dcmemory.h>
#include <slic3r/GUI/Widgets/Label.hpp>


namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_UPGRADE_FIRMWARE, wxCommandEvent);

FirmwareUpdateDialog::FirmwareUpdateDialog(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style)
      : DPIDialog(parent, id, _L("Upgrade firmware"), pos, size, style)
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    auto* main_sizer = new wxBoxSizer(wxVERTICAL);
    auto* button_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxPanel* m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));

    m_button_confirm = new Button(this, _L("Confirm"));
    m_button_confirm->SetFont(Label::Body_14);
    m_button_confirm->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_confirm->SetCornerRadius(FromDIP(12));
    StateColor confirm_btn_bg(std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    m_button_confirm->SetBackgroundColor(confirm_btn_bg);
    m_button_confirm->SetBorderColor(wxColour(0, 174, 66));
    m_button_confirm->SetTextColor(*wxWHITE);

    m_button_close = new Button(this, _L("Cancel"));
    m_button_close->SetFont(Label::Body_14);
    m_button_close->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_close->SetCornerRadius(FromDIP(12));
    StateColor close_btn_bg(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Hovered),
        std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));
    m_button_close->SetBackgroundColor(close_btn_bg);
    m_button_close->SetBorderColor(wxColour(38, 46, 48));
    m_button_close->SetTextColor(wxColour(38, 46, 48));

    button_sizer->AddStretchSpacer();
    button_sizer->Add(m_button_confirm);
    button_sizer->AddSpacer(FromDIP(20));
    button_sizer->Add(m_button_close);

    main_sizer->Add(m_line_top, 0, wxEXPAND, 0);
    main_sizer->AddSpacer(wxSize(FromDIP(475), FromDIP(100)).y);
    main_sizer->Add(button_sizer, 0, wxBOTTOM | wxRIGHT | wxEXPAND, FromDIP(25));

    SetSizer(main_sizer);

    CenterOnParent();

    this->SetSize(wxSize(wxSize(FromDIP(475), FromDIP(100)).x, -1));
    this->SetMinSize(wxSize(wxSize(FromDIP(475), FromDIP(100)).x, -1));
    Layout();
    Fit();
    this->Bind(wxEVT_PAINT, &FirmwareUpdateDialog::OnPaint, this);
    m_button_confirm->Bind(wxEVT_BUTTON, &FirmwareUpdateDialog::on_button_confirm, this);
    m_button_close->Bind(wxEVT_BUTTON, &FirmwareUpdateDialog::on_button_close, this);
}

FirmwareUpdateDialog::~FirmwareUpdateDialog() {}

void FirmwareUpdateDialog::SetHint(const wxString& hint){
    firm_up_hint = hint;
}

void FirmwareUpdateDialog::OnPaint(wxPaintEvent& event){
    wxPaintDC dc(this);
    render(dc);
}

void FirmwareUpdateDialog::render(wxDC& dc) {
    wxSize     size = GetSize();

    dc.SetFont(Label::Body_14);
    dc.SetTextForeground(text_color);
    wxPoint pos_start = wxPoint(FromDIP(25), FromDIP(25));

    wxSize firm_up_hint_size = dc.GetTextExtent(firm_up_hint);
    wxPoint pos_firm_up_hint = pos_start;

    if (firm_up_hint_size.x + pos_firm_up_hint.x + FromDIP(25) > wxSize(FromDIP(475), FromDIP(100)).x) {
        bool is_ch = false;
        if (firm_up_hint[0] > 0x80 && firm_up_hint[1] > 0x80)
            is_ch = true;

        wxString fisrt_line;
        wxString remaining_line;

        wxString  count_txt;
        int new_line_pos = 0;
        for (int i = 0; i < firm_up_hint.length(); i++) {
            count_txt += firm_up_hint[i];
            auto text_size = dc.GetTextExtent(count_txt);
            if (text_size.x + pos_firm_up_hint.x + FromDIP(25) < wxSize(FromDIP(475), FromDIP(100)).x)
            {
                if (firm_up_hint[i] == ' ' ||  firm_up_hint[i] == '\n')
                    new_line_pos = i;
            }
            else {
                if (!is_ch) {
                    fisrt_line = firm_up_hint.SubString(0, new_line_pos);
                    remaining_line = firm_up_hint.SubString(new_line_pos + 1, firm_up_hint.length());
                    break;
                }
                else {
                    fisrt_line = firm_up_hint.SubString(0, i);
                    remaining_line = firm_up_hint.SubString(i, firm_up_hint.length());
                    break;
                }
                count_txt = "";
            }
        }
        dc.DrawText(fisrt_line, pos_firm_up_hint);


        count_txt = "";
        new_line_pos = 0;
        for (int i = 0; i < remaining_line.length(); i++) {
            count_txt += remaining_line[i];
            auto text_size = dc.GetTextExtent(count_txt);
            if (text_size.x + FromDIP(25) + FromDIP(25) < wxSize(FromDIP(475), FromDIP(100)).x)
            {
                if (remaining_line[i] == ' ' || remaining_line[i] == '\n')
                    new_line_pos = i;
            }
            else {
                if (!is_ch){
                    remaining_line[new_line_pos] = '\n';
                }
                else {
                    remaining_line.insert(i, '\n');
                }
                count_txt = "";
            }
        }
        wxPoint pos_txt = pos_firm_up_hint;
        pos_txt.y += dc.GetCharHeight();
        dc.DrawText(remaining_line, pos_txt);
    }
    else
        dc.DrawText(firm_up_hint, pos_firm_up_hint);
}

void FirmwareUpdateDialog::on_button_confirm(wxCommandEvent& event) {
    wxCommandEvent evt(EVT_UPGRADE_FIRMWARE, GetId());
    event.SetEventObject(this);
    GetEventHandler()->ProcessEvent(evt);

    if (this->IsModal())
        this->EndModal(wxID_OK);
    else
        this->Close();
}

void FirmwareUpdateDialog::on_button_close(wxCommandEvent& event) {
    this->Close();
}

void FirmwareUpdateDialog::on_dpi_changed(const wxRect& suggested_rect) {
    m_button_confirm->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_confirm->SetCornerRadius(FromDIP(12));
    m_button_close->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_close->SetCornerRadius(FromDIP(12));
    Layout();
}

}} // namespace Slic3r::GUI