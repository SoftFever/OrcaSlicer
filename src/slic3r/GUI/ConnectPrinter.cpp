#include "ConnectPrinter.hpp"
#include "GUI_App.hpp"
#include <slic3r/GUI/I18N.hpp>
#include <slic3r/GUI/Widgets/Label.hpp>
#include "libslic3r/AppConfig.hpp"

namespace Slic3r { namespace GUI {
ConnectPrinterDialog::ConnectPrinterDialog(wxWindow *parent, wxWindowID id, const wxString &title, const wxPoint &pos, const wxSize &size, long style)
    : DPIDialog(parent, id, _L("ConnectPrinter(LAN)"), pos, size, style)
{
    SetBackgroundColour(*wxWHITE);
    this->SetSizeHints(wxDefaultSize, wxDefaultSize);

    wxBoxSizer *main_sizer;
    main_sizer = new wxBoxSizer(wxHORIZONTAL);

    main_sizer->Add(FromDIP(40), 0);

    wxBoxSizer *sizer_top;
    sizer_top = new wxBoxSizer(wxVERTICAL);

    sizer_top->Add(0, FromDIP(40));

    m_staticText_connection_code = new wxStaticText(this, wxID_ANY, _L("Please input the printer access code:"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_connection_code->SetFont(Label::Body_15);
    m_staticText_connection_code->SetForegroundColour(wxColour(50, 58, 61));
    m_staticText_connection_code->Wrap(-1);
    sizer_top->Add(m_staticText_connection_code, 0, wxALL, 0);

    sizer_top->Add(0, FromDIP(10));

    wxBoxSizer *sizer_connect;
    sizer_connect = new wxBoxSizer(wxHORIZONTAL);

    m_textCtrl_code = new TextInput(this, wxEmptyString);
    m_textCtrl_code->GetTextCtrl()->SetMaxLength(10);
    m_textCtrl_code->SetFont(Label::Body_14);
    m_textCtrl_code->SetCornerRadius(FromDIP(5));
    m_textCtrl_code->SetSize(wxSize(FromDIP(330), FromDIP(40)));
    m_textCtrl_code->SetMinSize(wxSize(FromDIP(330), FromDIP(40)));
    m_textCtrl_code->GetTextCtrl()->SetSize(wxSize(-1, FromDIP(22)));
    m_textCtrl_code->GetTextCtrl()->SetMinSize(wxSize(-1, FromDIP(22)));
    m_textCtrl_code->SetBackgroundColour(*wxWHITE);
    m_textCtrl_code->GetTextCtrl()->SetForegroundColour(wxColour(107, 107, 107));
    sizer_connect->Add(m_textCtrl_code, 0, wxALL | wxALIGN_CENTER_VERTICAL, 0);

    sizer_connect->Add(FromDIP(20), 0);

    m_button_confirm = new Button(this, _L("Confirm"));
    m_button_confirm->SetFont(Label::Body_12);
    m_button_confirm->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_confirm->SetCornerRadius(FromDIP(12));
    m_button_confirm->SetTextColor(wxColour("#FFFFFE"));

    StateColor btn_bg(
        std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(105, 75, 124), StateColor::Normal)
    );

    StateColor btn_bd(std::pair<wxColour, int>(wxColour(105, 75, 124), StateColor::Normal));

    StateColor btn_text(std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));

    m_button_confirm->SetBackgroundColor(btn_bg);
    m_button_confirm->SetBorderColor(btn_bd);
    m_button_confirm->SetTextColor(btn_text);

    sizer_connect->Add(m_button_confirm, 0, wxALL | wxALIGN_CENTER_VERTICAL, 0);

    sizer_connect->Add(FromDIP(60), 0);

    sizer_top->Add(sizer_connect);

    sizer_top->Add(0, FromDIP(35));

    m_staticText_hints = new wxStaticText(this, wxID_ANY, _L("You can find it in \"Settings > Network > Connection code\"\non the printer, as shown in the figure:"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_hints->SetFont(Label::Body_15);
    m_staticText_hints->SetForegroundColour(wxColour(50, 58, 61));
    m_staticText_hints->Wrap(-1);
    sizer_top->Add(m_staticText_hints, 0, wxALL, 0);

    sizer_top->Add(0, FromDIP(25));

    wxBoxSizer *sizer_diagram;
    sizer_diagram = new wxBoxSizer(wxHORIZONTAL);

    m_bitmap_diagram = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(FromDIP(340), -1), 0);
    m_bitmap_diagram->SetBitmap(m_diagram_img);
    sizer_diagram->Add(m_bitmap_diagram);

    sizer_top->Add(sizer_diagram);

    sizer_top->Add(0, FromDIP(40), 0, wxEXPAND, 0);

    main_sizer->Add(sizer_top);

    this->SetSizer(main_sizer);
    this->Layout();
    this->Fit();
    CentreOnParent();

    m_textCtrl_code->Bind(wxEVT_TEXT, &ConnectPrinterDialog::on_input_enter, this);
    m_button_confirm->Bind(wxEVT_BUTTON, &ConnectPrinterDialog::on_button_confirm, this);
    wxGetApp().UpdateDlgDarkUI(this);
}

ConnectPrinterDialog::~ConnectPrinterDialog() {}

void ConnectPrinterDialog::end_modal(wxStandardID id)
{
    EndModal(id);
}

void ConnectPrinterDialog::init_bitmap()
{
    AppConfig *config = get_app_config();
    std::string language = config->get("language");

    if (m_obj) {
        std::string img_str = DeviceManager::get_printer_diagram_img(m_obj->printer_type);
        if(img_str.empty()){img_str = "input_access_code_x1"; }

        if (language == "zh_CN") {
            m_diagram_bmp = create_scaled_bitmap(img_str+"_cn", nullptr, 190);
        }
        else {
            m_diagram_bmp = create_scaled_bitmap(img_str+"_en", nullptr, 190);
        }
    }
    else{
        if (language == "zh_CN") {
            m_diagram_bmp = create_scaled_bitmap("input_access_code_x1_cn", nullptr, 190);
        }
        else {
            m_diagram_bmp = create_scaled_bitmap("input_access_code_x1_en", nullptr, 190);
        }
    }
    m_diagram_img = m_diagram_bmp.ConvertToImage();
    auto bmp_size = m_diagram_bmp.GetSize();
    float scale = (float)FromDIP(340) / (float)bmp_size.x;
    m_diagram_img.Rescale(FromDIP(340), bmp_size.y * scale);
    m_bitmap_diagram->SetBitmap(m_diagram_img);
    Fit();
}

void ConnectPrinterDialog::set_machine_object(MachineObject* obj)
{
    m_obj = obj;
    init_bitmap();
}

void ConnectPrinterDialog::on_input_enter(wxCommandEvent& evt)
{
    m_input_access_code = evt.GetString();
}


void ConnectPrinterDialog::on_button_confirm(wxCommandEvent &event)
{
    wxString code = m_textCtrl_code->GetTextCtrl()->GetValue();
    for (char c : code) {
        if (!('0' <= c && c <= '9' || 'a' <= c && c <= 'z' || 'A' <= c && c <= 'Z')) {
            show_error(this, _L("Invalid input."));
            return;
        }
    }
    if (m_obj) {
        m_obj->set_user_access_code(code.ToStdString());
        if (m_need_connect) {
            wxGetApp().getDeviceManager()->set_selected_machine(m_obj->dev_id);
        }
    }
    EndModal(wxID_OK);
}

void ConnectPrinterDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    init_bitmap();
    m_bitmap_diagram->SetBitmap(m_diagram_img);
    m_textCtrl_code->GetTextCtrl()->SetSize(wxSize(-1, FromDIP(22)));
    m_textCtrl_code->GetTextCtrl()->SetMinSize(wxSize(-1, FromDIP(22)));

    m_button_confirm->SetCornerRadius(FromDIP(12));
    m_button_confirm->Rescale();

    Layout();
    this->Refresh();
}
}} // namespace Slic3r::GUI