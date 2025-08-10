#include "JusPrinView3D.hpp"

#include <wx/statbmp.h>
#include <wx/bitmap.h>
#include <wx/animate.h>
#include <wx/glcanvas.h>
#include <wx/dcbuffer.h>
#include <wx/graphics.h>

#include "../GLCanvas3D.hpp"
#include "../GUI_Preview.hpp"
#include "../Event.hpp"
#include "../GUI_App.hpp"

namespace Slic3r {
namespace GUI {

JustPrinButton::JustPrinButton(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size)
    : wxPanel(parent, id, pos, size, wxTAB_TRAVERSAL | wxBORDER_NONE)
{   
#ifdef __APPLE__
    SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
    SetBackgroundColour(wxColour(0, 0, 0, 0));
#endif

    Bind(wxEVT_PAINT, &JustPrinButton::OnPaint, this);
    m_animationCtrl = new wxAnimationCtrl(this, wxID_ANY);
    wxAnimation animation;
    wxString    gif_url = from_u8((boost::filesystem::path(resources_dir()) /"images/prin_login.gif").make_preferred().string());
    if (animation.LoadFile(gif_url, wxANIMATION_TYPE_GIF)) {
        m_animationCtrl->SetAnimation(animation);
        m_animationCtrl->Play();
    }
    Bind(wxEVT_ENTER_WINDOW, &JustPrinButton::OnMouseEnter, this);
    Bind(wxEVT_LEAVE_WINDOW, &JustPrinButton::OnMouseLeave, this);
    Bind(wxEVT_MOTION, &JustPrinButton::OnMouseMove, this);
    m_animationCtrl->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
        if (m_do) {
            m_do(event);
        }
        event.Skip();
    });
    //m_animationCtrl->Hide();
}

void JustPrinButton::OnPaint(wxPaintEvent& event) {
    wxAutoBufferedPaintDC dc(this);
    dc.Clear();

    wxSize size = GetClientSize();
    int width = size.GetWidth();
    int height = size.GetHeight();
    int radius = 12;

    wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
    if (gc) {
        // Clear background
        wxColour transparentColour(255, 255, 255, 0);
        gc->SetBrush(wxBrush(transparentColour));
        gc->DrawRectangle(0, 0, width, height);

#ifdef __APPLE__
        // Draw drop shadows with offset
        // First shadow (larger, more diffuse)
        gc->SetBrush(wxBrush(wxColour(10, 10, 10, 8)));
        gc->DrawRoundedRectangle(1, 1, width - 2, height - 2, radius);

        // Second shadow (smaller, more intense)
        gc->SetBrush(wxBrush(wxColour(33, 33, 33, 15)));
        gc->DrawRoundedRectangle(2, 2, width - 4, height - 4, radius);

        // Main button
        gc->SetBrush(wxBrush(*wxWHITE));
        wxColour borderColor = !m_isHovered ? wxColour(125, 125, 125) : *wxBLUE;
        gc->SetPen(wxPen(borderColor, 1));
        gc->DrawRoundedRectangle(3, 3, width-6, height-6, radius);
#else
   // Draw drop shadows with offset
        // First shadow (larger, more diffuse)
        gc->SetBrush(wxBrush(wxColour(10, 10, 10, 8)));
        gc->DrawRectangle(1, 1, width - 2, height - 2);

        // Second shadow (smaller, more intense)
        gc->SetBrush(wxBrush(wxColour(33, 33, 33, 15)));
        gc->DrawRectangle(2, 2, width - 2, height - 2, radius);

        // Main button
        gc->SetBrush(wxBrush(*wxWHITE));
        wxColour borderColor = !m_isHovered ? wxColour(125, 125, 125) : *wxBLUE;
        gc->SetPen(wxPen(borderColor, 1));
        gc->DrawRectangle(3, 3, width-6, height-6);

#endif
        delete gc;
    }
}

void JustPrinButton::OnMouseEnter(wxMouseEvent& event){
    m_isHovered = true;
    Refresh();
}

void JustPrinButton::OnMouseLeave(wxMouseEvent& event)  {
    wxPoint mousePos   = ScreenToClient(wxGetMousePosition());
    wxRect  clientRect = GetClientRect();
    if (!clientRect.Contains(mousePos)) {
        m_isHovered = false;
        Refresh();
    }
}

void JustPrinButton::OnMouseMove(wxMouseEvent& event) {
    wxPoint mousePos   = event.GetPosition();
    wxRect  clientRect = GetClientRect();
    if (!clientRect.Contains(mousePos)) {
        if (m_isHovered) {
            m_isHovered = false;
            Refresh();
        }
    } else {
        if (!m_isHovered) {
            m_isHovered = true;
            Refresh();
        }
    }
}

 void JustPrinButton::DoSetSize(int x, int y, int width, int height, int sizeFlags){
     m_animationCtrl->SetSize((width-227)/2,
                              (height-28)/2, // 10px from top
                              227, 28, sizeFlags);
     wxPanel::DoSetSize(x, y, width, height, sizeFlags);
 }

JusPrinView3D::JusPrinView3D(wxWindow* parent, Bed3D& bed, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process)
    : View3D(parent, bed, model, config, process)
{
    init_overlay();
}

JusPrinView3D::~JusPrinView3D()
{
    delete m_chat_panel;
    delete m_overlay_btn;
}

void JusPrinView3D::init_overlay()
{
    // Create chat panel overlay
    m_chat_panel = new JusPrinChatPanel(this);

    // Position the chat panel
    wxSize client_size = GetClientSize();
    int chat_height = client_size.GetHeight() * 0.95;  // 95% of window height
    int chat_width = client_size.GetWidth() * 0.7;    // 70% of window width

    m_chat_panel->SetSize(
        (client_size.GetWidth() - chat_width) / 2,  // Center horizontally
        10,  // 10px from top
        chat_width,
        chat_height
    );
    m_chat_panel->Hide();

    // Create image overlay using resources directory
    m_overlay_btn = new JustPrinButton(this, wxID_ANY,
        wxPoint((client_size.GetWidth() - 200) / 2, chat_height - 40),
        wxSize(200, 100));

    // Bind click event to show chat panel
    auto open_chat = [this](wxMouseEvent& evt) {
        if (m_chat_panel) {
            m_chat_panel->Show();
            m_chat_panel->SetFocus();
        }
        evt.Skip();
    };
    m_overlay_btn->Bind(wxEVT_LEFT_DOWN, open_chat);
    m_overlay_btn->AddJoin(open_chat);

    wxString    two_circle_path = from_u8((boost::filesystem::path(resources_dir()) /"images/two_circle.png").make_preferred().string());
    wxImage icon_img(two_circle_path);
    m_icon_image = new wxStaticBitmap(this, wxID_ANY, icon_img);
    m_icon_text_left = new wxStaticText(this, wxID_ANY, "1");
    m_icon_text_right = new wxStaticText(this, wxID_ANY, "1");
    wxFont font = m_icon_text_left->GetFont();
    font.SetPointSize(12); // 设置字体大小为24
    m_icon_text_left->SetFont(font);
    m_icon_text_right->SetFont(font);
    m_icon_text_left->SetMaxSize(wxSize(9, 12));
    m_icon_text_right->SetMaxSize(wxSize(9, 12));
    m_icon_text_left->SetForegroundColour(wxColour(*wxBLACK));
    m_icon_text_right->SetForegroundColour(wxColour(*wxBLACK));
    m_icon_text_left->SetBackgroundColour(wxColour("#F7C645"));
    m_icon_text_right->SetBackgroundColour(wxColour("#EA3426"));

    this->get_canvas3d()->get_wxglcanvas()->Bind(EVT_GLCANVAS_MOUSE_DOWN, &JusPrinView3D::OnCanvasMouseDown, this);

    Bind(wxEVT_SIZE, &JusPrinView3D::OnSize, this);
}

void JusPrinView3D::OnSize(wxSizeEvent& evt)
{
    evt.Skip();
    if (m_chat_panel && m_overlay_btn) {
        wxSize size = GetClientSize();

        // Resize chat panel
        int chat_height = size.GetHeight() * 0.95;
        int chat_width = size.GetWidth() * 0.7;
        m_chat_panel->SetSize(
            (size.GetWidth() - chat_width) / 2,
            10,
            chat_width,
            chat_height
        );
        m_chat_panel->Raise();

        // Resize and reposition image
        int image_height = 38+8;
        int image_width = 238+8;
        m_overlay_btn->SetSize(
            (size.GetWidth() - image_width) / 2,
            chat_height - 50,
            image_width,
            image_height
        );
        m_overlay_btn->Raise();
        int icon_x = (size.GetWidth() - image_width) / 2 +210;
        int icon_y = chat_height - 50 - 5;
        m_icon_image->SetPosition({icon_x, icon_y}	);
        m_icon_image->Raise();
        m_icon_text_left->SetPosition({icon_x+3, icon_y+2});
        m_icon_text_right->SetPosition({icon_x+18, icon_y+2});
        m_icon_text_left->Raise();
        m_icon_text_right->Raise();
        
    }
}

void JusPrinView3D::OnCanvasMouseDown(SimpleEvent& evt)
{
    if (m_chat_panel && m_chat_panel->IsShown()) {
        // wxPoint click_pt = evt.GetPosition();

        // wxRect btn_rect = m_overlay_image->GetScreenRect();
        // wxRect img_rect(this->ScreenToClient(btn_rect.GetTopLeft()),
        //                this->ScreenToClient(btn_rect.GetBottomRight()));

        // if (!img_rect.Contains(click_pt)) {
            m_chat_panel->Hide();
        // }
    }
    evt.Skip();
}

} // namespace GUI
} // namespace Slic3r
