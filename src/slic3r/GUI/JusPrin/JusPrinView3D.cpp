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

namespace {
    // Button constants
    constexpr int BUTTON_RADIUS = 12;
    constexpr int BUTTON_SHADOW_OFFSET = 3;

    // Chat panel constants
    constexpr int MIN_CHAT_HEIGHT = 180;
    constexpr int MIN_CHAT_WIDTH = 326;
    constexpr double CHAT_HEIGHT_RATIO = 0.25;
    constexpr double CHAT_WIDTH_RATIO = 0.5;
    constexpr int CHAT_BOTTOM_MARGIN = 10;

    // Animation/Image constants
    constexpr int ANIMATION_WIDTH = 227;
    constexpr int ANIMATION_HEIGHT = 28;

    // Badge constants
    constexpr int BADGE_SIZE = 20;
    constexpr int BADGE_OFFSET_X = 210;
#ifdef __APPLE__
    constexpr int BADGE_OFFSET_Y = 5;
#else
    constexpr int BADGE_OFFSET_Y = 10;
#endif

    // Overlay button constants
    constexpr int OVERLAY_IMAGE_HEIGHT = 38;
    constexpr int OVERLAY_IMAGE_WIDTH = 238;
    constexpr int OVERLAY_PADDING = 8;
}

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
        wxColour borderColor = !m_isHovered ? wxColour(0, 0, 0, 0) : *wxBLUE;
        gc->SetPen(wxPen(borderColor, 1));
        gc->DrawRoundedRectangle(3, 3, width-6, height-6, radius);
#else
   // Draw drop shadows with offset
        // First shadow (larger, more diffuse)
        gc->SetBrush(wxBrush(wxColour(10, 10, 10, 8)));
        gc->DrawRectangle(1, 1, width - 2, height - 2);

        // Second shadow (smaller, more intense)
        gc->SetBrush(wxBrush(wxColour(33, 33, 33, 15)));
        gc->DrawRectangle(2, 2, width - 2, height - 2);

        // Main button
        gc->SetBrush(wxBrush(*wxWHITE));
        wxColour borderColor = !m_isHovered ? wxColour(0, 0, 0, 0) : *wxBLUE;
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
     m_animationCtrl->SetSize(
         (width - ANIMATION_WIDTH) / 2,
         (height - ANIMATION_HEIGHT) / 2,
         ANIMATION_WIDTH,
         ANIMATION_HEIGHT,
         sizeFlags
     );
     wxPanel::DoSetSize(x, y, width, height, sizeFlags);
 }

// Implement the OnPaint method
void Slic3r::GUI::CircularBadge::OnPaint(wxPaintEvent&) {
    wxPaintDC dc(this);
    dc.SetBackgroundMode(wxTRANSPARENT);

    wxSize size = GetClientSize();

    wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
    if (gc) {
#ifdef __APPLE_
        // Draw with solid color, no border
        gc->SetBrush(wxBrush(m_bgColor));
        gc->SetPen(wxPen(m_bgColor)); // Changed to use background color for no visible border

        // Draw slightly smaller than the full size to ensure margins
        double margin = 1.0;
        gc->DrawEllipse(margin, margin,
                       size.GetWidth() - 2*margin,
                       size.GetHeight() - 2*margin);

        // Draw text in black for maximum contrast
        gc->SetFont(GetFont(), *wxBLACK);
        double textWidth, textHeight;
        gc->GetTextExtent(m_text, &textWidth, &textHeight);

        double x = (size.GetWidth() - textWidth) / 2;
        double y = (size.GetHeight() - textHeight) / 2;
        gc->DrawText(m_text, x, y);
 #else
        int width  = size.GetWidth();
        int height = size.GetHeight();
        gc->SetBrush(wxBrush(wxColour(m_bgColor)));
        gc->DrawRectangle(1, 1, width-2, height-2);
        gc->SetBrush(wxBrush(wxColour(*wxWHITE)));
        gc->DrawRectangle(2, 2, width - 4, height - 4);

        gc->SetFont(GetFont(), m_bgColor);
        double textWidth, textHeight;
        gc->GetTextExtent(m_text, &textWidth, &textHeight);

        double x = (size.GetWidth() - textWidth) / 2;
        double y = (size.GetHeight() - textHeight) / 2;
        gc->DrawText(m_text, x, y);
 #endif

        delete gc;
    }
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
    int chat_height = std::max(180, (int)(client_size.GetHeight() * 0.25));  // minimum 180px
    int chat_width = std::max(326, (int)(client_size.GetWidth() * 0.5));    // minimum 326px
    int chat_y = client_size.GetHeight() - chat_height - 10;  // 10px from bottom

    m_chat_panel->SetSize(
        (client_size.GetWidth() - chat_width) / 2,
        chat_y,
        chat_width,
        chat_height
    );
    m_chat_panel->Hide();

    // Create image overlay using resources directory
    m_overlay_btn = new JustPrinButton(this, wxID_ANY,
        wxPoint((client_size.GetWidth() - 200) / 2, chat_height - 40),
        wxSize(200, 100));
    m_overlay_btn->Raise();

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
    // Just create the left badge for now
    m_icon_text_right = new CircularBadge(this, "1", wxColour("#EA3426"));
    m_icon_text_right->Raise();
    m_icon_text_left = new CircularBadge(this, "1", wxColour("#F7C645"));
    m_icon_text_left->Raise();

    // Debug: Make the badge bigger initially to see it better
    m_icon_text_left->SetMinSize(wxSize(20, 20));

    this->get_canvas3d()->get_wxglcanvas()->Bind(EVT_GLCANVAS_MOUSE_DOWN, &JusPrinView3D::OnCanvasMouseDown, this);
    Bind(wxEVT_SIZE, &JusPrinView3D::OnSize, this);
}

void JusPrinView3D::OnSize(wxSizeEvent& evt)
{
    evt.Skip();
    if (!m_chat_panel || !m_overlay_btn) return;

    wxSize size = GetClientSize();

    // Resize chat panel
    int chat_height = std::max(MIN_CHAT_HEIGHT, (int)(size.GetHeight() * CHAT_HEIGHT_RATIO));
    int chat_width = std::max(MIN_CHAT_WIDTH, (int)(size.GetWidth() * CHAT_WIDTH_RATIO));
    int chat_y = size.GetHeight() - chat_height - CHAT_BOTTOM_MARGIN;

    m_chat_panel->SetSize(
        (size.GetWidth() - chat_width) / 2,
        chat_y,
        chat_width,
        chat_height
    );
    m_chat_panel->Raise();

    // Resize and reposition overlay button
    int image_height = OVERLAY_IMAGE_HEIGHT + OVERLAY_PADDING;
    int image_width = OVERLAY_IMAGE_WIDTH + OVERLAY_PADDING;
    int button_y = size.GetHeight() - image_height - CHAT_BOTTOM_MARGIN;

    m_overlay_btn->SetSize(
        (size.GetWidth() - image_width) / 2,
        button_y,
        image_width,
        image_height
    );

    // Position badges
    int icon_x = (size.GetWidth() - image_width) / 2 + BADGE_OFFSET_X;
    m_icon_text_left->SetPosition({icon_x + 3, button_y - BADGE_OFFSET_Y});
    m_icon_text_right->SetPosition({icon_x + 15, button_y - BADGE_OFFSET_Y});

    // Ensure proper z-order
    m_overlay_btn->Raise();
    m_icon_text_left->Raise();
    m_icon_text_right->Raise();
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
