#include "FilamentGroupPopup.hpp"
#include "GUI_App.hpp"
#include "MsgDialog.hpp"
#include "wx/dcgraph.h"
#include "I18N.hpp"
#include "PartPlate.hpp"

namespace Slic3r { namespace GUI {

static const wxColour LabelEnableColor = wxColour("#262E30");
static const wxColour LabelDisableColor = wxColour("#ACACAC");
static const wxColour GreyColor = wxColour("#6B6B6B");
static const wxColour GreenColor = wxColour("#00AE42");
static const wxColour BackGroundColor = wxColour("#FFFFFF");


static bool should_pop_up()
{
    const auto &preset_bundle    = wxGetApp().preset_bundle;
    const auto &full_config      = preset_bundle->full_config();
    const auto  nozzle_diameters = full_config.option<ConfigOptionFloatsNullable>("nozzle_diameter");
    return nozzle_diameters->size() > 1;
}

static FilamentMapMode get_prefered_map_mode()
{
    const static std::map<std::string, int> enum_keys_map = ConfigOptionEnum<FilamentMapMode>::get_enum_values();
    auto                                   &app_config    = wxGetApp().app_config;
    std::string                             mode_str      = app_config->get("prefered_filament_map_mode");
    auto                                    iter          = enum_keys_map.find(mode_str);
    if (iter == enum_keys_map.end()) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format("Could not get prefered_filament_map_mode from app config, use AutoForFlsuh mode");
        return FilamentMapMode::fmmAutoForFlush;
    }
    return FilamentMapMode(iter->second);
}

static void set_prefered_map_mode(FilamentMapMode mode)
{
    const static std::vector<std::string> enum_values = ConfigOptionEnum<FilamentMapMode>::get_enum_names();
    auto                                 &app_config  = wxGetApp().app_config;
    std::string                           mode_str;
    if (mode < enum_values.size()) mode_str = enum_values[mode];

    if (mode_str.empty()) BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format("Set empty prefered_filament_map_mode to app config");
    app_config->set("prefered_filament_map_mode", mode_str);
}


FilamentGroupPopup::FilamentGroupPopup(wxWindow *parent) : PopupWindow(parent, wxBORDER_NONE | wxPU_CONTAINS_CONTROLS)
{
    const wxString AutoForFlushLabel = _L("Filament-Saving Mode");
    const wxString AutoForMatchLabel = _L("Convenient Mode");
    const wxString ManualLabel       = _L("Manual Mode");

    const wxString AutoForFlushDetail = _L("Disregrad the filaments in AMS. Optimize filament usage "
        "by calculating the best arrangement for the left and right "
        "nozzles. Arrange the filaments on the printer based on "
        "the slicing results.");
    const wxString AutoForMatchDetail = _L("Based on the current filaments in the AMS, arrange the "
        "filaments to the left and right nozzles.");
    const wxString ManualDetail       = _L("Mannully arrange the filaments for the left and right nozzles.");

    const wxString AutoForFlushDesp = _L("(Arrange after slicing)");
    const wxString ManualDesp       = "";
    const wxString AutoForMatchDesp = _L("(Arrange before slicing)");
    const wxString MachineSyncTip = _L("(Please sync printer)");

    wxBoxSizer *top_sizer         = new wxBoxSizer(wxVERTICAL);
    const int   horizontal_margin = FromDIP(16);
    const int   vertical_margin   = FromDIP(15);
    const int   vertical_padding  = FromDIP(12);
    const int   ratio_spacing     = FromDIP(4);

    SetBackgroundColour(BackGroundColor);

    radio_btns.resize(ButtonType::btCount);
    button_labels.resize(ButtonType::btCount);
    button_desps.resize(ButtonType::btCount);
    detail_infos.resize(ButtonType::btCount);
    std::vector<wxString> btn_texts    = {AutoForFlushLabel, AutoForMatchLabel, ManualLabel};
    std::vector<wxString> btn_desps    = {AutoForFlushDesp, AutoForMatchDesp, ManualDesp};
    std::vector<wxString> mode_details = {AutoForFlushDetail, AutoForMatchDetail, ManualDetail};

    top_sizer->AddSpacer(vertical_margin);

    auto checked_bmp   = create_scaled_bitmap("map_mode_on", nullptr, 16);
    auto unchecked_bmp = create_scaled_bitmap("map_mode_off", nullptr, 16);

    for (size_t idx = 0; idx < ButtonType::btCount; ++idx) {
        wxBoxSizer *button_sizer = new wxBoxSizer(wxHORIZONTAL);
        radio_btns[idx]          = new wxBitmapButton(this, idx, unchecked_bmp, wxDefaultPosition, wxDefaultSize, wxNO_BORDER);
        radio_btns[idx]->SetBackgroundColour(BackGroundColor);

        button_labels[idx] = new Label(this, btn_texts[idx]);
        button_labels[idx]->SetBackgroundColour(BackGroundColor);
        button_labels[idx]->SetForegroundColour(LabelEnableColor);
        button_labels[idx]->SetFont(Label::Body_14);

        button_desps[idx] = new Label(this, btn_desps[idx]);
        button_desps[idx]->SetBackgroundColour(BackGroundColor);
        button_desps[idx]->SetForegroundColour(LabelEnableColor);
        button_desps[idx]->SetFont(Label::Body_14);

        button_sizer->Add(radio_btns[idx], 0, wxALIGN_CENTER_VERTICAL);
        button_sizer->AddSpacer(ratio_spacing);
        button_sizer->Add(button_labels[idx], 0, wxALIGN_CENTER_VERTICAL);
        button_sizer->Add(button_desps[idx], 0, wxALIGN_CENTER_VERTICAL);

        wxBoxSizer *label_sizer = new wxBoxSizer(wxHORIZONTAL);

        detail_infos[idx] = new Label(this, mode_details[idx]);
        detail_infos[idx]->SetBackgroundColour(BackGroundColor);
        detail_infos[idx]->SetForegroundColour(GreyColor);
        detail_infos[idx]->SetFont(Label::Body_12);
        detail_infos[idx]->Wrap(FromDIP(320));

        label_sizer->AddSpacer(radio_btns[idx]->GetRect().width + ratio_spacing);
        label_sizer->Add(detail_infos[idx], 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

        top_sizer->Add(button_sizer, 0, wxLEFT | wxRIGHT, horizontal_margin);
        top_sizer->Add(label_sizer, 0, wxLEFT | wxRIGHT, horizontal_margin);
        top_sizer->AddSpacer(vertical_padding);

        radio_btns[idx]->Bind(wxEVT_LEFT_DOWN, [this, idx](auto &) { OnRadioBtn(idx);});

        radio_btns[idx]->Bind(wxEVT_ENTER_WINDOW, [this, idx](auto &) { UpdateButtonStatus(idx); });
        radio_btns[idx]->Bind(wxEVT_LEAVE_WINDOW, [this](auto &) { UpdateButtonStatus(); });

        button_labels[idx]->Bind(wxEVT_LEFT_DOWN, [this, idx](auto &) { OnRadioBtn(idx);});
        button_labels[idx]->Bind(wxEVT_ENTER_WINDOW, [this, idx](auto &) { UpdateButtonStatus(idx); });
        button_labels[idx]->Bind(wxEVT_LEAVE_WINDOW, [this](auto &) { UpdateButtonStatus(); });
    }

    {
        wxBoxSizer *button_sizer = new wxBoxSizer(wxHORIZONTAL);

        auto* wiki_sizer = new wxBoxSizer(wxHORIZONTAL);
        wiki_link = new wxStaticText(this, wxID_ANY, _L("More info on wiki"));
        wiki_link->SetBackgroundColour(BackGroundColor);
        wiki_link->SetForegroundColour(GreenColor);
        wiki_link->SetFont(Label::Body_12.Underlined());
        wiki_link->SetCursor(wxCursor(wxCURSOR_HAND));
        wiki_link->Bind(wxEVT_LEFT_DOWN, [](wxMouseEvent &) { wxLaunchDefaultBrowser("http//:example.com"); });
        wiki_sizer->Add(wiki_link, 0, wxALIGN_CENTER | wxALL, FromDIP(3));

        button_sizer->Add(wiki_sizer, 0, wxLEFT, horizontal_margin);
        button_sizer->AddStretchSpacer();

        top_sizer->Add(button_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, horizontal_margin);
    }

    top_sizer->AddSpacer(vertical_margin);
    SetSizerAndFit(top_sizer);

    m_mode  = get_prefered_map_mode();
    m_timer = new wxTimer(this);

    GUI::wxGetApp().UpdateDarkUIWin(this);

    Bind(wxEVT_PAINT, &FilamentGroupPopup::OnPaint, this);
    Bind(wxEVT_TIMER, &FilamentGroupPopup::OnTimer, this);
    Bind(wxEVT_ENTER_WINDOW, &FilamentGroupPopup::OnEnterWindow, this);
    Bind(wxEVT_LEAVE_WINDOW, &FilamentGroupPopup::OnLeaveWindow, this);
}

void FilamentGroupPopup::DrawRoundedCorner(int radius)
{
#ifdef __WIN32__
    HWND hwnd = GetHWND();
    if (hwnd) {
        HRGN hrgn = CreateRoundRectRgn(0, 0, GetRect().GetWidth(), GetRect().GetHeight(), radius, radius);
        SetWindowRgn(hwnd, hrgn, TRUE);

        SetWindowLong(hwnd, GWL_EXSTYLE, GetWindowLong(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
        SetLayeredWindowAttributes(hwnd, 0, 0, LWA_COLORKEY);
    }
#else
    wxClientDC         dc(this);
    wxGraphicsContext *gc = wxGraphicsContext::Create(dc);
    if (gc) {
        gc->SetBrush(*wxWHITE_BRUSH);
        gc->SetPen(*wxTRANSPARENT_PEN);
        wxRect         rect(0, 0, GetSize().GetWidth(), GetSize().GetHeight());
        wxGraphicsPath path = wxGraphicsRenderer::GetDefaultRenderer()->CreatePath();
        path.AddRoundedRectangle(0, 0, rect.width, rect.height, radius);
        gc->DrawPath(path);
        delete gc;
    }
#endif
}

void FilamentGroupPopup::Init()
{
    const wxString AutoForMatchDesp = _L("(Arrange before slicing)");
    const wxString MachineSyncTip = _L("(Please sync printer)");

    auto disabled_bmp = create_scaled_bitmap("map_mode_disabled", nullptr, 16);
    auto unchecked_bmp = create_scaled_bitmap("map_mode_off", nullptr, 16);
    radio_btns[ButtonType::btForMatch]->Enable(m_connected);
    if (m_connected) {
        button_labels[ButtonType::btForMatch]->SetForegroundColour(LabelEnableColor);
        button_desps[ButtonType::btForMatch]->SetForegroundColour(LabelEnableColor);
        detail_infos[ButtonType::btForMatch]->SetForegroundColour(GreyColor);
        radio_btns[ButtonType::btForMatch]->SetBitmap(unchecked_bmp);
        button_desps[ButtonType::btForMatch]->SetLabel(AutoForMatchDesp);
    }
    else {
        button_labels[ButtonType::btForMatch]->SetForegroundColour(LabelDisableColor);
        button_desps[ButtonType::btForMatch]->SetForegroundColour(LabelDisableColor);
        detail_infos[ButtonType::btForMatch]->SetForegroundColour(LabelDisableColor);
        radio_btns[ButtonType::btForMatch]->SetBitmap(disabled_bmp);
        button_desps[ButtonType::btForMatch]->SetLabel(MachineSyncTip);
    }

    m_mode = GetFilamentMapMode();
    if (m_mode == fmmAutoForMatch && !m_connected) {
        SetFilamentMapMode(fmmAutoForFlush);
        m_mode = fmmAutoForFlush;
    }

    UpdateButtonStatus();
}

void FilamentGroupPopup::tryPopup(Plater* plater,PartPlate* partplate,bool skip_plate_sync)
{
    if (should_pop_up()) {
        bool connect_status = plater->get_machine_sync_status();
        this->partplate_ref = partplate;
        this->plater_ref = plater;
        this->m_sync_plate = !skip_plate_sync && partplate->get_filament_map_mode() != fmmDefault;
        if (m_active) {
            if (m_connected != connect_status) { Init(); }
            m_connected = connect_status;
            ResetTimer();
        }
        else {
            m_connected = connect_status;
            m_active = true;
            Init();
            ResetTimer();
            PopupWindow::Popup();
        }
    }
}

FilamentMapMode FilamentGroupPopup::GetFilamentMapMode() const
{
    const auto& proj_config = wxGetApp().preset_bundle->project_config;
    if (m_sync_plate)
        return partplate_ref->get_real_filament_map_mode(proj_config);

    return plater_ref->get_global_filament_map_mode();
}

void FilamentGroupPopup::SetFilamentMapMode(const FilamentMapMode mode)
{
    if (m_sync_plate) {
        partplate_ref->set_filament_map_mode(mode);
        return;
    }
    plater_ref->set_global_filament_map_mode(mode);
}


void FilamentGroupPopup::tryClose() { StartTimer(); }

void FilamentGroupPopup::OnPaint(wxPaintEvent&)
{
    DrawRoundedCorner(16);
}

void FilamentGroupPopup::StartTimer() { m_timer->StartOnce(300); }

void FilamentGroupPopup::ResetTimer()
{
    if (m_timer->IsRunning()) { m_timer->Stop(); }
}

void FilamentGroupPopup::OnRadioBtn(int idx)
{
    if (mode_list.at(idx) == FilamentMapMode::fmmAutoForMatch && !m_connected)
        return;
    m_mode = mode_list.at(idx);

    SetFilamentMapMode(m_mode);

    UpdateButtonStatus(m_mode);
}

void FilamentGroupPopup::OnTimer(wxTimerEvent &event) { Dismiss(); }

void FilamentGroupPopup::Dismiss() {
    m_active = false;
    PopupWindow::Dismiss();
    m_timer->Stop();
}

void FilamentGroupPopup::OnLeaveWindow(wxMouseEvent &)
{
    wxPoint pos = this->ScreenToClient(wxGetMousePosition());
    if (this->GetClientRect().Contains(pos)) return;
    StartTimer();
}

void FilamentGroupPopup::OnEnterWindow(wxMouseEvent &) { ResetTimer(); }

void FilamentGroupPopup::UpdateButtonStatus(int hover_idx)
{
    auto checked_bmp         = create_scaled_bitmap("map_mode_on", nullptr, 16);
    auto unchecked_bmp       = create_scaled_bitmap("map_mode_off", nullptr, 16);
    auto checked_hover_bmp = create_scaled_bitmap("map_mode_on_hovered", nullptr, 16);
    auto unchecked_hover_bmp = create_scaled_bitmap("map_mode_off_hovered", nullptr, 16);


    for (int i = 0; i < ButtonType::btCount; ++i) {
        if (ButtonType::btForMatch == i && !m_connected) {
            button_labels[i]->SetFont(Label::Body_14);
            continue;
        }
        // process checked and unchecked status
        if (mode_list.at(i) == m_mode) {
            if (i == hover_idx)
                radio_btns[i]->SetBitmap(checked_hover_bmp);
            else
                radio_btns[i]->SetBitmap(checked_bmp);
            button_labels[i]->SetFont(Label::Head_14);
        } else {
            if (i == hover_idx)
                radio_btns[i]->SetBitmap(unchecked_hover_bmp);
            else
                radio_btns[i]->SetBitmap(unchecked_bmp);
            button_labels[i]->SetFont(Label::Body_14);
        }
    }

    Layout();
    Fit();
}

}} // namespace Slic3r::GUI