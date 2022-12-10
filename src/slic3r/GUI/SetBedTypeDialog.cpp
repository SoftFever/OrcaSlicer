#include "SetBedTypeDialog.hpp"


namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_SET_BED_TYPE_CONFIRM, wxCommandEvent);

SetBedTypeDialog::SetBedTypeDialog(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style)
:DPIDialog(parent, id, title, pos, size, style)
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);
    wxBoxSizer* m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(300), -1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(5));

    wxBoxSizer* m_sizer_radiobutton = new wxBoxSizer(wxVERTICAL);

    m_rb_default_plate = create_item_radiobox(_L("Same as Global Bed Type"), this, wxEmptyString, FromDIP(5), btDefault);
    m_sizer_radiobutton->Add(m_rb_default_plate->GetParent(), 1, wxALL, FromDIP(5));
    m_rb_cool_plate = create_item_radiobox(_L("Cool Plate"), this, wxEmptyString, FromDIP(5), btPC);
    m_sizer_radiobutton->Add(m_rb_cool_plate->GetParent(), 1, wxALL, FromDIP(5));
    m_rb_eng_plate = create_item_radiobox(_L("Engineering Plate"), this, wxEmptyString, FromDIP(5), btEP);
    m_sizer_radiobutton->Add(m_rb_eng_plate->GetParent(), 1, wxALL, FromDIP(5) );
    m_rb_high_temp_plate = create_item_radiobox(_L("High Temp Plate"), this, wxEmptyString, FromDIP(5), btPEI);
    m_sizer_radiobutton->Add(m_rb_high_temp_plate->GetParent(), 1, wxALL, FromDIP(5));
    m_rb_texture_pei_plate = create_item_radiobox(_L("Textured PEI Plate"), this, wxEmptyString, FromDIP(5), btPTE);
    m_sizer_radiobutton->Add(m_rb_texture_pei_plate->GetParent(), 1, wxALL, FromDIP(5));

    m_sizer_main->Add(m_sizer_radiobutton, 0, wxEXPAND | wxALL, FromDIP(10));

    auto sizer_button = new wxBoxSizer(wxHORIZONTAL);
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));

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
    m_button_ok->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        int len = radio_buttons.size();
        for (int i = 0; i < len; ++i) {
            if (radio_buttons[i]->GetValue()) {
               wxCommandEvent evt(EVT_SET_BED_TYPE_CONFIRM, GetId());
               evt.SetInt(radio_buttons[i]->GetBedType());
               e.SetEventObject(this);
               GetEventHandler()->ProcessEvent(evt);
               break;
            }
        }
        if (this->IsModal())
            EndModal(wxID_YES);
        else
            this->Close();
        });

    m_button_cancel = new Button(this, _L("Cancel"));
    m_button_cancel->SetBackgroundColor(btn_bg_white);
    m_button_cancel->SetBorderColor(wxColour(38, 46, 48));
    m_button_cancel->SetFont(Label::Body_12);
    m_button_cancel->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetCornerRadius(FromDIP(12));
    m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        if (this->IsModal())
            EndModal(wxID_NO);
        else
            this->Close();
        });

    sizer_button->AddStretchSpacer();
    sizer_button->Add(m_button_ok, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_cancel, 0, wxALL, FromDIP(5));

    m_sizer_main->Add(sizer_button, 0, wxEXPAND, FromDIP(20));

    SetSizer(m_sizer_main);
    Layout();
    m_sizer_main->Fit(this);

    CenterOnParent();
}

SetBedTypeDialog::~SetBedTypeDialog()
{

}

BedTypeRadioBox* SetBedTypeDialog::create_item_radiobox(wxString title, wxWindow* parent, wxString tooltip, int padding_left, BedType bed_type)
{
    wxWindow *item = new wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(28)));
    item->SetBackgroundColour(*wxWHITE);

    BedTypeRadioBox* radiobox = new BedTypeRadioBox(item, bed_type);
    radiobox->SetPosition(wxPoint(padding_left, (item->GetSize().GetHeight() - radiobox->GetSize().GetHeight()) / 2));
    radio_buttons.push_back(radiobox);
    int btn_idx = radio_buttons.size() - 1;
    radiobox->Bind(wxEVT_LEFT_DOWN, [this, btn_idx](wxMouseEvent &e) {
        SetBedTypeDialog::select_curr_radiobox(btn_idx);
        });

    wxStaticText *text = new wxStaticText(item, wxID_ANY, title, wxDefaultPosition, wxDefaultSize);
    text->SetPosition(wxPoint(padding_left + radiobox->GetSize().GetWidth() + 10, (item->GetSize().GetHeight() - text->GetSize().GetHeight()) / 2));
    text->SetFont(Label::Body_14);
    text->SetForegroundColour(0x686868);
    text->Bind(wxEVT_LEFT_DOWN, [this, btn_idx](wxMouseEvent &e) {
        SetBedTypeDialog::select_curr_radiobox(btn_idx);
        });

    radiobox->SetToolTip(tooltip);
    text->SetToolTip(tooltip);
    return radiobox;
}

void SetBedTypeDialog::select_curr_radiobox(int btn_idx)
{
    int len = radio_buttons.size();
    for (int i = 0; i < len; ++i) {
        if (i == btn_idx)
            radio_buttons[i]->SetValue(true);
        else
            radio_buttons[i]->SetValue(false);
    }
}

void SetBedTypeDialog::sync_bed_type(BedType type)
{
    for (auto radio_box : radio_buttons) {
        if (radio_box->GetBedType() == type)
            radio_box->SetValue(true);
        else
            radio_box->SetValue(false);
    }
}

void SetBedTypeDialog::on_dpi_changed(const wxRect& suggested_rect)
{

}

}} // namespace Slic3r::GUI