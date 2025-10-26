#include "ImageDPIFrame.hpp"

#include <wx/event.h>
#include <wx/sizer.h>
#include <wx/slider.h>
#include <wx/dcmemory.h>
#include "GUI_App.hpp"
#include "Tab.hpp"
#include "PartPlate.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include <chrono>
#include "wxExtensions.hpp"

using namespace Slic3r;
using namespace Slic3r::GUI;

namespace Slic3r { namespace GUI {
#define ANIMATION_REFRESH_INTERVAL 20
ImageDPIFrame::ImageDPIFrame()
    : DPIFrame(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, "", wxDefaultPosition, wxDefaultSize, !wxCAPTION | !wxCLOSE_BOX | wxBORDER_NONE)
{
    m_image_px = 280;
    int width = 270;
    //SetTransparent(0);
    SetMinSize(wxSize(FromDIP(width), -1));
    SetMaxSize(wxSize(FromDIP(width), -1));
    SetBackgroundColour(wxColour(242, 242, 242, 255));
#ifdef __APPLE__
    SetWindowStyleFlag(GetWindowStyleFlag() | wxSTAY_ON_TOP);
#endif

    m_sizer_main           = new wxBoxSizer(wxVERTICAL);
    auto image_sizer  = new wxBoxSizer(wxVERTICAL);
    auto imgsize           = FromDIP(width);
    m_bitmap = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("printer_preview_C13", this, m_image_px), wxDefaultPosition, wxSize(imgsize, imgsize * 0.94), 0);
    image_sizer->Add(m_bitmap, 0, wxALIGN_CENTER  | wxALL, FromDIP(0));
    m_sizer_main->Add(image_sizer, FromDIP(0), wxALIGN_CENTER, FromDIP(0));

    Bind(wxEVT_CLOSE_WINDOW, [this](auto &e) {
        on_hide();
    });
    SetSizer(m_sizer_main);
    Layout();
    Fit();
    init_timer();
}

ImageDPIFrame::~ImageDPIFrame() {

}

bool ImageDPIFrame::Show(bool show)
{
    Layout();
    return DPIFrame::Show(show);
}

void ImageDPIFrame::set_bitmap(const wxBitmap &bit_map) {
    m_bitmap->SetBitmap(bit_map);
}

void ImageDPIFrame::on_dpi_changed(const wxRect &suggested_rect)
{
   // m_image->Rescale();
    //m_bitmap->Rescale();
}

void ImageDPIFrame::init_timer()
{
    m_refresh_timer = new wxTimer();
    m_refresh_timer->SetOwner(this);
    Bind(wxEVT_TIMER, &ImageDPIFrame::on_timer, this);
}

void ImageDPIFrame::on_timer(wxTimerEvent &event)
{
    if (!IsShown()) {//after 1s  to show Frame
        if (m_timer_count >= 50) {
            Show();
            Raise();
        }
        m_timer_count++;
    }else{
        wxPoint mouse_pos = wxGetMousePosition();
        wxRect window_rect = GetScreenRect();

        wxPoint center(window_rect.x + window_rect.width / 2, window_rect.y + window_rect.height / 2);
        int half_width = window_rect.width / 2;
        int half_height = window_rect.height / 2;
        wxRect expanded_rect(
            center.x - half_width * 2,
            center.y - half_height * 2,
            window_rect.width * 2,
            window_rect.height * 2
        );

        if (!expanded_rect.Contains(mouse_pos)) {
            on_hide();
        }
    }
}

void ImageDPIFrame::on_show() {
    if (IsShown()) {
        on_hide();
    }
    if (m_refresh_timer) {
        m_timer_count = 0;
        m_refresh_timer->Start(ANIMATION_REFRESH_INTERVAL);
    }
}

void ImageDPIFrame::on_hide()
{
    if (m_refresh_timer) {
        m_refresh_timer->Stop();
    }
    if (IsShown()) {
        Hide();
        if (wxGetApp().mainframe != nullptr) {
            wxGetApp().mainframe->Show();
            wxGetApp().mainframe->Raise();
        }
    }
}

} // namespace GUI
} // namespace Slic3r