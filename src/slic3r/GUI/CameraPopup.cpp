#include "CameraPopup.hpp"

#include "I18N.hpp"
#include "Widgets/Label.hpp"
#include "libslic3r/Utils.hpp"
#include "BitmapCache.hpp"
#include <wx/progdlg.h>
#include <wx/clipbrd.h>
#include <wx/dcgraph.h>

namespace Slic3r {
namespace GUI {

wxIMPLEMENT_CLASS(CameraPopup, wxPopupTransientWindow);

wxBEGIN_EVENT_TABLE(CameraPopup, wxPopupTransientWindow)
    EVT_MOUSE_EVENTS(CameraPopup::OnMouse )
    EVT_SIZE(CameraPopup::OnSize)
    EVT_SET_FOCUS(CameraPopup::OnSetFocus )
    EVT_KILL_FOCUS(CameraPopup::OnKillFocus )
wxEND_EVENT_TABLE()

wxDEFINE_EVENT(EVT_VCAMERA_SWITCH, wxMouseEvent);
wxDEFINE_EVENT(EVT_SDCARD_ABSENT_HINT, wxCommandEvent);

const wxColour TEXT_COL = wxColour(43, 52, 54);

CameraPopup::CameraPopup(wxWindow *parent, MachineObject* obj)
   : wxPopupTransientWindow(parent, wxBORDER_NONE | wxPU_CONTAINS_CONTROLS),
    m_obj(obj)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif
    m_panel = new wxScrolledWindow(this, wxID_ANY);
    m_panel->SetBackgroundColour(*wxWHITE);
    m_panel->SetMinSize(wxSize(FromDIP(180),-1));
    m_panel->Bind(wxEVT_MOTION, &CameraPopup::OnMouse, this);

    main_sizer = new wxBoxSizer(wxVERTICAL);
    wxFlexGridSizer* top_sizer = new wxFlexGridSizer(0, 2, 0, FromDIP(50));
    top_sizer->AddGrowableCol(0);
    top_sizer->SetFlexibleDirection(wxBOTH);
    top_sizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);
    //timelapse
    m_text_timelapse = new wxStaticText(m_panel, wxID_ANY, _L("Timelapse"));
    m_text_timelapse->Wrap(-1);
    m_text_timelapse->SetFont(Label::Head_14);
    m_text_timelapse->SetForegroundColour(TEXT_COL);
    m_switch_timelapse = new SwitchButton(m_panel);
    if (obj)
        m_switch_timelapse->SetValue(obj->camera_timelapse);

    //recording
    m_text_recording = new wxStaticText(m_panel, wxID_ANY, _L("Monitoring Recording"));
    m_text_recording->Wrap(-1);
    m_text_recording->SetFont(Label::Head_14);
    m_text_recording->SetForegroundColour(TEXT_COL);
    m_switch_recording = new SwitchButton(m_panel);
    if (obj)
        m_switch_recording->SetValue(obj->camera_recording_when_printing);

    //vcamera
    m_text_vcamera = new wxStaticText(m_panel, wxID_ANY, _L("Virtual Camera"));
    m_text_vcamera->Wrap(-1);
    m_text_vcamera->SetFont(Label::Head_14);
    m_text_vcamera->SetForegroundColour(TEXT_COL);
    m_switch_vcamera = new SwitchButton(m_panel);

    top_sizer->Add(m_text_timelapse, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT | wxALL, FromDIP(5));
    top_sizer->Add(m_switch_timelapse, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxALL, FromDIP(5));
    top_sizer->Add(m_text_recording, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT | wxALL, FromDIP(5));
    top_sizer->Add(m_switch_recording, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxALL, FromDIP(5));
    top_sizer->Add(m_text_vcamera, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT | wxALL, FromDIP(5));
    top_sizer->Add(m_switch_vcamera, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxALL, FromDIP(5));

    //resolution
    m_text_resolution = new wxStaticText(m_panel, wxID_ANY, _L("Resolution"));
    m_text_resolution->Wrap(-1);
    m_text_resolution->SetFont(Label::Head_14);
    m_text_resolution->SetForegroundColour(TEXT_COL);
    top_sizer->Add(m_text_resolution, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT | wxALL, FromDIP(5));
    top_sizer->Add(0, 0, wxALL, 0);
    for (int i = 0; i < (int)RESOLUTION_OPTIONS_NUM; ++i)
    {
        m_resolution_options[i] = create_item_radiobox(to_resolution_label_string(CameraResolution(i)), m_panel, wxEmptyString, FromDIP(10));
        top_sizer->Add(m_resolution_options[i], 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT | wxALL, FromDIP(5));
        top_sizer->Add(0, 0, wxALL, 0);
    }
    if (obj)
        sync_resolution_setting(obj->camera_resolution);

    main_sizer->Add(top_sizer, 0, wxALL, FromDIP(10));

    auto url = wxString::Format(L"https://wiki.bambulab.com/%s/software/bambu-studio/virtual-camera", L"en");
    vcamera_guide_link = new wxHyperlinkCtrl(m_panel, wxID_ANY, _L("Show 'Streaming Video' guide page."),
        url, wxDefaultPosition, wxDefaultSize, wxHL_DEFAULT_STYLE);
    vcamera_guide_link->Hide();
    main_sizer->Add(vcamera_guide_link, 0, wxALL, FromDIP(15));

    m_panel->SetSizer(main_sizer);
    m_panel->Layout();

    main_sizer->Fit(m_panel);

    SetClientSize(m_panel->GetSize());
    m_switch_timelapse->Connect(wxEVT_LEFT_DOWN, wxCommandEventHandler(CameraPopup::on_switch_timelapse), NULL, this);
    m_switch_recording->Connect(wxEVT_LEFT_DOWN, wxCommandEventHandler(CameraPopup::on_switch_recording), NULL, this);
    m_switch_vcamera->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
        wxMouseEvent evt(EVT_VCAMERA_SWITCH);
        evt.SetEventObject(this);
        GetEventHandler()->ProcessEvent(evt);
        });
    #ifdef __APPLE__
    m_panel->Bind(wxEVT_LEFT_UP, &CameraPopup::OnLeftUp, this);
    #endif //APPLE

    check_func_supported();
}

void CameraPopup::sdcard_absent_hint()
{
    wxCommandEvent evt(EVT_SDCARD_ABSENT_HINT);
    evt.SetEventObject(this);
    GetEventHandler()->ProcessEvent(evt);
}

void CameraPopup::on_switch_timelapse(wxCommandEvent& event)
{
    if (!m_obj) return;
    if (m_obj->sdcard_state != MachineObject::SdcardState::HAS_SDCARD_NORMAL) {
        sdcard_absent_hint();
        return;
    }
    bool value = m_switch_timelapse->GetValue();
    m_obj->command_ipcam_timelapse(!value);
}

void CameraPopup::on_switch_recording(wxCommandEvent& event)
{
    if (!m_obj) return;
    if (m_obj->sdcard_state != MachineObject::SdcardState::HAS_SDCARD_NORMAL) {
        sdcard_absent_hint();
        return;
    }
    bool value = m_switch_recording->GetValue();
    m_obj->command_ipcam_record(!value);
}

void CameraPopup::on_set_resolution()
{
    if (!m_obj) return;

    m_obj->command_ipcam_resolution_set(to_resolution_msg_string(curr_sel_resolution));
}

void CameraPopup::Popup(wxWindow *WXUNUSED(focus))
{
    wxPoint curr_position = this->GetPosition();
    wxSize win_size = this->GetSize();
    curr_position.x -= win_size.x;
    this->SetPosition(curr_position);
    wxPopupTransientWindow::Popup();
}

wxWindow* CameraPopup::create_item_radiobox(wxString title, wxWindow* parent, wxString tooltip, int padding_left)
{
    wxWindow *item = new wxWindow(parent, wxID_ANY, wxDefaultPosition, wxSize(-1, FromDIP(20)));
    item->SetBackgroundColour(*wxWHITE);

    RadioBox *radiobox = new RadioBox(item);
    radiobox->SetPosition(wxPoint(padding_left, (item->GetSize().GetHeight() - radiobox->GetSize().GetHeight()) / 2));
    resolution_rbtns.push_back(radiobox);
    int btn_idx = resolution_rbtns.size() - 1;
    radiobox->Bind(wxEVT_LEFT_DOWN, [this, btn_idx](wxMouseEvent &e) {
        if (m_obj && allow_alter_resolution) {
            select_curr_radiobox(btn_idx, false);
            on_set_resolution();
        }
        });

    wxStaticText *text = new wxStaticText(item, wxID_ANY, title, wxDefaultPosition, wxDefaultSize);
    resolution_texts.push_back(text);
    text->SetPosition(wxPoint(padding_left + radiobox->GetSize().GetWidth() + 10, (item->GetSize().GetHeight() - text->GetSize().GetHeight()) / 2));
    text->SetFont(Label::Body_13);
    text->SetForegroundColour(0x6B6B6B);
    text->Bind(wxEVT_LEFT_DOWN, [this, btn_idx](wxMouseEvent &e) {
        if (m_obj && allow_alter_resolution) {
            select_curr_radiobox(btn_idx, false);
            on_set_resolution();
        }
        });

    radiobox->SetToolTip(tooltip);
    text->SetToolTip(tooltip);
    return item;
}

void CameraPopup::select_curr_radiobox(int btn_idx, bool ui_change)
{
    int len = resolution_rbtns.size();
    for (int i = 0; i < len; ++i) {
        if (i == btn_idx) {
            curr_sel_resolution = CameraResolution(i);
            if (ui_change)
                resolution_rbtns[i]->SetValue(true);
        }
        else {
            if (ui_change)
                resolution_rbtns[i]->SetValue(false);
        }
    }
}

void CameraPopup::sync_resolution_setting(std::string resolution)
{
    if (resolution == "") {
        reset_resolution_setting();
        return;
    }
    int res = 0;
    for (CameraResolution i = RESOLUTION_720P; i < RESOLUTION_OPTIONS_NUM; i = CameraResolution(i+1)){
        if (resolution == to_resolution_msg_string(i)) {
            res = int(i);
            break;
        }
    }
    select_curr_radiobox(res, true);
}

void CameraPopup::reset_resolution_setting()
{
    int len = resolution_rbtns.size();
    for (int i = 0; i < len; ++i) {
         resolution_rbtns[i]->SetValue(false);
    }
    curr_sel_resolution = RESOLUTION_OPTIONS_NUM;
}

void CameraPopup::sync_vcamera_state(bool show_vcamera)
{
    is_vcamera_show = show_vcamera;
    if (is_vcamera_show) {
        m_switch_vcamera->SetValue(true);
        vcamera_guide_link->Show();
    }
    else {
        m_switch_vcamera->SetValue(false);
        vcamera_guide_link->Hide();
    }

    rescale();
}

void CameraPopup::check_func_supported()
{
    // function supported
    if (m_obj->is_function_supported(PrinterFunction::FUNC_TIMELAPSE) && m_obj->has_ipcam) {
        m_text_timelapse->Show();
        m_switch_timelapse->Show();
    } else {
        m_text_timelapse->Hide();
        m_switch_timelapse->Hide();
    }

    if (m_obj->is_function_supported(PrinterFunction::FUNC_RECORDING) && m_obj->has_ipcam) {
        m_text_recording->Show();
        m_switch_recording->Show();
    } else {
        m_text_recording->Hide();
        m_switch_recording->Hide();
    }

    if (m_obj->is_function_supported(PrinterFunction::FUNC_VIRTUAL_CAMERA) && m_obj->has_ipcam) {
        m_text_vcamera->Show();
        m_switch_vcamera->Show();
        if (is_vcamera_show)
            vcamera_guide_link->Show();
    } else {
        m_text_vcamera->Hide();
        m_switch_vcamera->Hide();
        vcamera_guide_link->Hide();
    }

    allow_alter_resolution = (m_obj->is_function_supported(PrinterFunction::FUNC_ALTER_RESOLUTION) && m_obj->has_ipcam);

    //resolution supported
    std::vector<std::string> resolution_supported = m_obj->get_resolution_supported();
    for (int i = 0; i < (int)RESOLUTION_OPTIONS_NUM; ++i){
        auto curr_res = to_resolution_msg_string(CameraResolution(i));
        std::vector <std::string> ::iterator it = std::find(resolution_supported.begin(), resolution_supported.end(), curr_res);
        if (it!= resolution_supported.end())
            m_resolution_options[i] -> Show();
        else
            m_resolution_options[i] -> Hide();
    }
}

void CameraPopup::update()
{
    if (!m_obj) return;
    m_switch_timelapse->SetValue(m_obj->camera_timelapse);
    m_switch_recording->SetValue(m_obj->camera_recording_when_printing);
    sync_resolution_setting(m_obj->camera_resolution);

    rescale();
}

wxString CameraPopup::to_resolution_label_string(CameraResolution resolution) {
    switch (resolution) {
    case RESOLUTION_720P:
        return _L("720p");
    case RESOLUTION_1080P:
        return _L("1080p");
    default:
        return _L("");
    }
    return "";
}

std::string CameraPopup::to_resolution_msg_string(CameraResolution resolution) {
    switch (resolution) {
    case RESOLUTION_720P:
        return std::string("720p");
    case RESOLUTION_1080P:
        return std::string("1080p");
    default:
        return "";
    }
    return "";
}

void CameraPopup::rescale()
{
    m_panel->Layout();
    main_sizer->Fit(m_panel);
    SetClientSize(m_panel->GetSize());
    wxPopupTransientWindow::Update();
}

void CameraPopup::OnLeftUp(wxMouseEvent &event)
{
    auto mouse_pos = ClientToScreen(event.GetPosition());
    auto wxscroll_win_pos = m_panel->ClientToScreen(wxPoint(0, 0));

    if (mouse_pos.x > wxscroll_win_pos.x && mouse_pos.y > wxscroll_win_pos.y && mouse_pos.x < (wxscroll_win_pos.x + m_panel->GetSize().x) && mouse_pos.y < (wxscroll_win_pos.y + m_panel->GetSize().y)) {
        //timelapse
        auto timelapse_rect = m_switch_timelapse->ClientToScreen(wxPoint(0, 0));
        if (mouse_pos.x > timelapse_rect.x && mouse_pos.y > timelapse_rect.y && mouse_pos.x < (timelapse_rect.x + m_switch_timelapse->GetSize().x) && mouse_pos.y < (timelapse_rect.y + m_switch_timelapse->GetSize().y)) {
            wxMouseEvent timelapse_evt(wxEVT_LEFT_DOWN);
            m_switch_timelapse->GetEventHandler()->ProcessEvent(timelapse_evt);
            return;
        }
        //recording
        auto recording_rect = m_switch_recording->ClientToScreen(wxPoint(0, 0));
        if (mouse_pos.x > recording_rect.x && mouse_pos.y > recording_rect.y && mouse_pos.x < (recording_rect.x + m_switch_recording->GetSize().x) && mouse_pos.y < (recording_rect.y + m_switch_recording->GetSize().y)) {
            wxMouseEvent recording_evt(wxEVT_LEFT_DOWN);
            m_switch_recording->GetEventHandler()->ProcessEvent(recording_evt);
            return;
        }
        //vcamera
        auto vcamera_rect = m_switch_vcamera->ClientToScreen(wxPoint(0, 0));
        if (mouse_pos.x > vcamera_rect.x && mouse_pos.y > vcamera_rect.y && mouse_pos.x < (vcamera_rect.x + m_switch_vcamera->GetSize().x) && mouse_pos.y < (vcamera_rect.y + m_switch_vcamera->GetSize().y)) {
            wxMouseEvent vcamera_evt(wxEVT_LEFT_DOWN);
            m_switch_vcamera->GetEventHandler()->ProcessEvent(vcamera_evt);
            return;
        }
        //resolution
        for (int i = 0; i < (int)RESOLUTION_OPTIONS_NUM; ++i){
            auto resolution_rbtn = resolution_rbtns[i];
            auto rbtn_rect = resolution_rbtn->ClientToScreen(wxPoint(0, 0));
            if (mouse_pos.x > rbtn_rect.x && mouse_pos.y > rbtn_rect.y && mouse_pos.x < (rbtn_rect.x + resolution_rbtn->GetSize().x) && mouse_pos.y < (rbtn_rect.y + resolution_rbtn->GetSize().y)) {
                wxMouseEvent resolution_evt(wxEVT_LEFT_DOWN);
                resolution_rbtn->GetEventHandler()->ProcessEvent(resolution_evt);
                return;
            }
            auto resolution_txt = resolution_texts[i];
            auto txt_rect = resolution_txt->ClientToScreen(wxPoint(0, 0));
            if (mouse_pos.x > txt_rect.x && mouse_pos.y > txt_rect.y && mouse_pos.x < (txt_rect.x + resolution_txt->GetSize().x) && mouse_pos.y < (txt_rect.y + resolution_txt->GetSize().y)) {
                wxMouseEvent resolution_evt(wxEVT_LEFT_DOWN);
                resolution_txt->GetEventHandler()->ProcessEvent(resolution_evt);
                return;
            }
        }
        //hyper link
        auto h_rect = vcamera_guide_link->ClientToScreen(wxPoint(0, 0));
        if (mouse_pos.x > h_rect.x && mouse_pos.y > h_rect.y && mouse_pos.x < (h_rect.x + vcamera_guide_link->GetSize().x) && mouse_pos.y < (h_rect.y + vcamera_guide_link->GetSize().y)) {
            auto url = wxString::Format(L"https://wiki.bambulab.com/%s/software/bambu-studio/virtual-camera", L"en");
            wxLaunchDefaultBrowser(url);
        }
    }
}

void CameraPopup::OnDismiss() {
    wxPopupTransientWindow::OnDismiss();
}

bool CameraPopup::ProcessLeftDown(wxMouseEvent &event)
{
    return wxPopupTransientWindow::ProcessLeftDown(event);
}

bool CameraPopup::Show(bool show)
{
    return wxPopupTransientWindow::Show(show);
}

void CameraPopup::OnSize(wxSizeEvent &event)
{
    event.Skip();
}

void CameraPopup::OnSetFocus(wxFocusEvent &event)
{
    event.Skip();
}

void CameraPopup::OnKillFocus(wxFocusEvent &event)
{
    event.Skip();
}

void CameraPopup::OnMouse(wxMouseEvent &event)
{
    event.Skip();
}

CameraItem::CameraItem(wxWindow *parent, std::string normal, std::string hover)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    m_bitmap_normal  = ScalableBitmap(this, normal, 20);
    m_bitmap_hover   = ScalableBitmap(this, hover, 20);

    SetSize(wxSize(FromDIP(20), FromDIP(20)));
    SetMinSize(wxSize(FromDIP(20), FromDIP(20)));
    SetMaxSize(wxSize(FromDIP(20), FromDIP(20)));
    Bind(wxEVT_PAINT, &CameraItem::paintEvent, this);
    Bind(wxEVT_ENTER_WINDOW, &CameraItem::on_enter_win, this);
    Bind(wxEVT_LEAVE_WINDOW, &CameraItem::on_level_win, this);
}

CameraItem::~CameraItem() {}

void CameraItem::msw_rescale() {}

void CameraItem::on_enter_win(wxMouseEvent &evt)
{
    m_hover = true;
    Refresh();
}

void CameraItem::on_level_win(wxMouseEvent &evt)
{
    m_hover = false;
    Refresh();
}

void CameraItem::paintEvent(wxPaintEvent &evt)
{
    wxPaintDC dc(this);
    render(dc);

    // PrepareDC(buffdc);
    // PrepareDC(dc);
}

void CameraItem::render(wxDC &dc)
{
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({0, 0}, size, &dc, {0, 0});

    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif
}

void CameraItem::doRender(wxDC &dc)
{
    if (m_hover) {
        dc.DrawBitmap(m_bitmap_hover.bmp(), wxPoint((GetSize().x - m_bitmap_hover.GetBmpSize().x) / 2, (GetSize().y - m_bitmap_hover.GetBmpSize().y) / 2));
    } else {
        dc.DrawBitmap(m_bitmap_normal.bmp(), wxPoint((GetSize().x - m_bitmap_normal.GetBmpSize().x) / 2, (GetSize().y - m_bitmap_normal.GetBmpSize().y) / 2));
    }
}

}
}