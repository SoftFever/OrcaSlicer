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

    // Chat panel size constants
    constexpr int MIN_CHAT_HEIGHT = 340;
    constexpr int MIN_CHAT_WIDTH = 420;

    // Button constants
    constexpr int BUTTON_RADIUS = 12;
    constexpr int BUTTON_SHADOW_OFFSET = 3;

    constexpr int CHAT_BOTTOM_MARGIN = 10;

    // Animation/Image constants
    constexpr int ANIMATION_WIDTH = 227;
    constexpr int ANIMATION_HEIGHT = 28;

    // Badge constants
    constexpr int BADGE_SIZE = 22;

    // Overlay button constants
    constexpr int OVERLAY_IMAGE_HEIGHT = 38;
    constexpr int OVERLAY_IMAGE_WIDTH = 238;
    constexpr int OVERLAY_PADDING = 8;

    // Move these constants to namespace scope since they're used in static config
    constexpr double CHAT_HEIGHT_RATIO_SMALL = 0.25;
    constexpr double CHAT_WIDTH_RATIO_SMALL = 0.5;
    constexpr double CHAT_HEIGHT_RATIO_LARGE = 0.75;
    constexpr double CHAT_WIDTH_RATIO_LARGE = 0.85;

    // Define static configs
    const Slic3r::GUI::ChatPanelConfig SMALL_CONFIG {
        CHAT_HEIGHT_RATIO_SMALL,
        CHAT_WIDTH_RATIO_SMALL
    };

    const Slic3r::GUI::ChatPanelConfig LARGE_CONFIG {
        CHAT_HEIGHT_RATIO_LARGE,
        CHAT_WIDTH_RATIO_LARGE
    };
}

namespace Slic3r {
namespace GUI {

ChatActivationButton::ChatActivationButton(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size)
    : wxPanel(parent, id, pos, size, wxTAB_TRAVERSAL | wxBORDER_NONE)
{
#ifdef __APPLE__
    SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
    SetBackgroundColour(wxColour(0, 0, 0, 0));
#endif

    Bind(wxEVT_PAINT, &ChatActivationButton::OnPaint, this);
    m_animationCtrl = new wxAnimationCtrl(this, wxID_ANY);
    wxAnimation animation;
    wxString    gif_url = from_u8((boost::filesystem::path(resources_dir()) /"images/prin_login.gif").make_preferred().string());
    if (animation.LoadFile(gif_url, wxANIMATION_TYPE_GIF)) {
        m_animationCtrl->SetAnimation(animation);
        m_animationCtrl->Play();
    }
    Bind(wxEVT_ENTER_WINDOW, &ChatActivationButton::OnMouseEnter, this);
    Bind(wxEVT_LEAVE_WINDOW, &ChatActivationButton::OnMouseLeave, this);
    Bind(wxEVT_MOTION, &ChatActivationButton::OnMouseMove, this);
    m_animationCtrl->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& event) {
        if (m_do) {
            m_do(event);
        }
        event.Skip();
    });
}

void ChatActivationButton::OnPaint(wxPaintEvent& event) {
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
        gc->DrawRoundedRectangle(4, 6, width - 4, height - 4, radius);

        // Second shadow (smaller, more intense)
        gc->SetBrush(wxBrush(wxColour(33, 33, 33, 15)));
        gc->DrawRoundedRectangle(4, 5, width - 6, height - 5, radius);

        // Main button
        gc->SetBrush(wxBrush(*wxWHITE));
        wxColour borderColor = !m_isHovered ? wxColour(0, 0, 0, 0) : *wxBLUE;
        gc->SetPen(wxPen(borderColor, 1));
        gc->DrawRoundedRectangle(3, 3, width-6, height-6, radius);

#else
        // Main button
        gc->SetBrush(wxBrush(*wxWHITE));
        wxColour borderColor = !m_isHovered ? wxColour(0, 0, 0, 0) : *wxBLUE;
        gc->SetPen(wxPen(borderColor, 1));
        gc->DrawRectangle(0, 0, width-2, height-2);
#endif
        delete gc;
    }
}

void ChatActivationButton::OnMouseEnter(wxMouseEvent& event){
    m_isHovered = true;
    Refresh();
}

void ChatActivationButton::OnMouseLeave(wxMouseEvent& event)  {
    wxPoint mousePos   = ScreenToClient(wxGetMousePosition());
    wxRect  clientRect = GetClientRect();
    if (!clientRect.Contains(mousePos)) {
        m_isHovered = false;
        Refresh();
    }
}

void ChatActivationButton::OnMouseMove(wxMouseEvent& event) {
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

 void ChatActivationButton::DoSetSize(int x, int y, int width, int height, int sizeFlags){
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
void Slic3r::GUI::ActivationButtonNotificationBadge::OnPaint(wxPaintEvent&) {
    wxPaintDC dc(this);
    dc.SetBackgroundMode(wxTRANSPARENT);

    wxSize size = GetClientSize();

    wxGraphicsContext* gc = wxGraphicsContext::Create(dc);
    if (gc) {
#ifdef __APPLE__
        // Draw with solid color, no border
        gc->SetBrush(wxBrush(m_bgColor));
        gc->SetPen(wxPen(m_bgColor)); // Changed to use background color for no visible border

        // Draw slightly smaller than the full size to ensure margins
        double margin = 1.0;
        gc->DrawEllipse(margin, margin,
                       size.GetWidth() - 2*margin,
                       size.GetHeight() - 2*margin);

        // Draw text in black for maximum contrast
        auto font = GetFont().Scale(0.8);
        gc->SetFont(font, *wxBLACK);
        double textWidth, textHeight;
        gc->GetTextExtent(m_text, &textWidth, &textHeight);

        double x = (size.GetWidth() - textWidth) / 2;
        double y = (size.GetHeight() - textHeight) / 2;
        gc->DrawText(m_text, x, y);
 #else
        int width  = size.GetWidth();
        int height = size.GetHeight();
        gc->SetPen(wxPen(m_bgColor, 1));
        gc->SetBrush(wxBrush(wxColour(*wxWHITE)));
        gc->DrawRectangle(0, 0, width-1, height-1);
        auto font = GetFont().Scale(0.8);
        gc->SetFont(font, m_bgColor);
        double textWidth, textHeight;
        gc->GetTextExtent(m_text, &textWidth, &textHeight);

        double x = (size.GetWidth() - textWidth) / 2;
        double y = (size.GetHeight() - textHeight) / 2;
        gc->DrawText(m_text, x, y);
 #endif

        delete gc;
    }
}

// Define the static configurations
static const ChatPanelConfig SMALL_CONFIG = {
    CHAT_HEIGHT_RATIO_SMALL,
    CHAT_WIDTH_RATIO_SMALL
};

static const ChatPanelConfig LARGE_CONFIG = {
    CHAT_HEIGHT_RATIO_LARGE,
    CHAT_WIDTH_RATIO_LARGE
};

JusPrinView3D::JusPrinView3D(wxWindow* parent, Bed3D& bed, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process)
    : View3D(parent, bed, model, config, process)
    , m_chatpanel_view_mode("large") // Initialize with large view
{
    initOverlay();
}

JusPrinView3D::~JusPrinView3D()
{
    delete m_chat_panel;
    delete m_overlay_btn;
    delete m_red_badge;
    delete m_orange_badge;
    delete m_green_badge;
}

void JusPrinView3D::updateChatPanelRect() {
    if (!m_chat_panel) return;

    const auto& config = m_chatpanel_view_mode == "large" ? LARGE_CONFIG : SMALL_CONFIG;

    wxSize size = GetClientSize();
    int chat_width = std::max(MIN_CHAT_WIDTH, (int)(size.GetWidth() * config.width_ratio));
    int chat_height = std::max(MIN_CHAT_HEIGHT, (int)(size.GetHeight() * config.height_ratio));

    m_chat_panel->SetSize(
        (size.GetWidth() - chat_width) / 2,
        size.GetHeight() - chat_height - CHAT_BOTTOM_MARGIN,
        chat_width,
        chat_height
    );
}

void JusPrinView3D::updateActivationButtonRect() {
    // Resize and reposition overlay button
    int image_height = OVERLAY_IMAGE_HEIGHT + OVERLAY_PADDING;
    int image_width = OVERLAY_IMAGE_WIDTH + OVERLAY_PADDING;
    int button_y = GetClientSize().GetHeight() - image_height - CHAT_BOTTOM_MARGIN;

    m_overlay_btn->SetSize(
        (GetClientSize().GetWidth() - image_width) / 2,
        button_y,
        image_width,
        image_height
    );
}

void JusPrinView3D::showBadgesIfNecessary() {
    if (!m_red_badge || !m_orange_badge || !m_green_badge) return;

    auto formatBadgeText = [](int count) {
        return count > 9 ? "9+" : std::to_string(count);
    };

    m_red_badge->SetText(formatBadgeText(m_red_badge_count));
    m_orange_badge->SetText(formatBadgeText(m_orange_badge_count));
    m_green_badge->SetText(formatBadgeText(m_green_badge_count));

    int image_width = OVERLAY_IMAGE_WIDTH + OVERLAY_PADDING;
    wxRect btn_rect = m_overlay_btn->GetRect();
    int button_y = btn_rect.GetY();

    const int num_visible_badges = (m_red_badge_count > 0) +
           (m_orange_badge_count > 0) +
           (m_green_badge_count > 0);

#ifdef __APPLE__
    constexpr int BADGE_OFFSET_Y = 8;
    constexpr int RIGHT_MARGIN = 10;
    constexpr double BADGE_OVERLAP = 0.75;
#else
    constexpr int BADGE_OFFSET_Y = BADGE_SIZE;
    constexpr int RIGHT_MARGIN = 0;
    constexpr double BADGE_OVERLAP = 1.0;
#endif

    int icon_x = (GetClientSize().GetWidth() + image_width) / 2;
    if (num_visible_badges == 1) {
        icon_x -= BADGE_SIZE + RIGHT_MARGIN;
    } else if (num_visible_badges > 1) {
        icon_x -= BADGE_SIZE + BADGE_SIZE * (num_visible_badges - 1) * BADGE_OVERLAP + RIGHT_MARGIN;
    }

    if (m_green_badge_count > 0) {
        m_green_badge->SetPosition({icon_x, button_y - BADGE_OFFSET_Y});
        icon_x += BADGE_SIZE*BADGE_OVERLAP;
    }

    if (m_orange_badge_count > 0) {
        m_orange_badge->SetPosition({icon_x, button_y - BADGE_OFFSET_Y});
        icon_x += BADGE_SIZE*BADGE_OVERLAP;
    }

    if (m_red_badge_count > 0) {
        m_red_badge->SetPosition({icon_x, button_y - BADGE_OFFSET_Y});
    }

    m_red_badge->Refresh();
    m_orange_badge->Refresh();
    m_green_badge->Refresh();

    bool show_badges = m_overlay_btn->IsShown();
    // Handle all badge visibility
    if (show_badges) {
        // Show badges with positive counts
        if (m_green_badge_count > 0) m_green_badge->Show();
        if (m_orange_badge_count > 0) m_orange_badge->Show();
        if (m_red_badge_count > 0) m_red_badge->Show();
    }
    // Hide badges with zero counts
    if (m_green_badge_count <= 0) m_green_badge->Hide();
    if (m_orange_badge_count <= 0) m_orange_badge->Hide();
    if (m_red_badge_count <= 0) m_red_badge->Hide();

}

void JusPrinView3D::initOverlay()
{
    m_chat_panel = new JusPrinChatPanel(this);
    m_chat_panel->Hide();

    // Create image overlay using resources directory
    m_overlay_btn = new ChatActivationButton(this, wxID_ANY,
        wxPoint((GetClientSize().GetWidth() - 200) / 2, GetClientSize().GetHeight() - 40),
        wxSize(200, 100));

    // Bind click event to show chat panel
    auto open_chat = [this](wxMouseEvent& evt) {
        wxGetApp().plater()->jusprinChatPanel()->SendChatPanelFocusEvent("in_focus");
        evt.Skip();
    };
    m_overlay_btn->Bind(wxEVT_LEFT_DOWN, open_chat);
    m_overlay_btn->AddJoin(open_chat);

    m_red_badge = new ActivationButtonNotificationBadge(this, "", wxColour("#E65C5C"));
    m_orange_badge = new ActivationButtonNotificationBadge(this, "", wxColour("#FDB074"));
    m_green_badge = new ActivationButtonNotificationBadge(this, "", wxColour("#009685"));
    m_red_badge->SetSize(BADGE_SIZE, BADGE_SIZE);
    m_orange_badge->SetSize(BADGE_SIZE, BADGE_SIZE);
    m_green_badge->SetSize(BADGE_SIZE, BADGE_SIZE);

    // Ensure proper z-order
    m_overlay_btn->Raise();
    m_green_badge->Raise();
    m_orange_badge->Raise();
    m_red_badge->Raise();

    m_overlay_btn->Hide();
    m_red_badge->Hide();
    m_orange_badge->Hide();
    m_green_badge->Hide();

    if (wxGetApp().app_config->get_bool("developer_mode")) {    // Make sure chat can display in dev mode so that we can bring out javascript console
        changeChatPanelView("large");
        showChatPanel();
    }

    this->get_canvas3d()->get_wxglcanvas()->Bind(EVT_GLCANVAS_MOUSE_DOWN, &JusPrinView3D::OnCanvasMouseDown, this);
    Bind(wxEVT_SIZE, &JusPrinView3D::OnSize, this);

}

void JusPrinView3D::OnSize(wxSizeEvent& evt)
{
    evt.Skip();
    if (!m_chat_panel || !m_overlay_btn) return;

    updateChatPanelRect();
    updateActivationButtonRect();
    showBadgesIfNecessary();
}

void JusPrinView3D::showChatPanel() {
    if (!m_chat_panel) return;

    m_chat_panel->Show();
    m_overlay_btn->Hide();
    showBadgesIfNecessary();
}

void JusPrinView3D::hideChatPanel() {
    if (!m_chat_panel) return;

    m_chat_panel->Hide();
    m_overlay_btn->Show();
    showBadgesIfNecessary();
}


void JusPrinView3D::changeChatPanelView(const std::string& viewMode) {
    if (!m_chat_panel) return;

    m_chatpanel_view_mode = viewMode;
    updateChatPanelRect();
}

void JusPrinView3D::setChatPanelVisibility(bool is_visible) {
    if (is_visible) {
        showChatPanel();
    } else {
        hideChatPanel();
    }
}

void JusPrinView3D::setChatPanelNotificationBadges(int red_badge, int orange_badge, int green_badge) {
    m_red_badge_count = red_badge;
    m_orange_badge_count = orange_badge;
    m_green_badge_count = green_badge;
    showBadgesIfNecessary();
}

void JusPrinView3D::OnCanvasMouseDown(SimpleEvent& evt) {
    if (m_chat_panel && m_chat_panel->IsShown()) {
        wxGetApp().plater()->jusprinChatPanel()->SendChatPanelFocusEvent("out_of_focus");
    }
    evt.Skip();
}

} // namespace GUI
} // namespace Slic3r
