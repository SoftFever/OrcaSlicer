#ifndef slic3r_GUI_JusPrinView3D_hpp_
#define slic3r_GUI_JusPrinView3D_hpp_

#include "../../GUI/GUI_Preview.hpp"
#include "JusPrinChatPanel.hpp"

// Chat panel size constants
constexpr int MIN_CHAT_HEIGHT_SMALL = 220;
constexpr int MIN_CHAT_WIDTH_SMALL = 420;
constexpr double CHAT_HEIGHT_RATIO_SMALL = 0.25;
constexpr double CHAT_WIDTH_RATIO_SMALL = 0.5;

constexpr int MIN_CHAT_HEIGHT_LARGE = 520;
constexpr int MIN_CHAT_WIDTH_LARGE = 520;
constexpr double CHAT_HEIGHT_RATIO_LARGE = 0.75;
constexpr double CHAT_WIDTH_RATIO_LARGE = 0.85;

// Forward declarations
class wxStaticBitmap;
class wxWindow;
class wxSizeEvent;

namespace Slic3r {
namespace GUI {

// Move CircularBadge class definition to header
class CircularBadge : public wxPanel {
public:
    CircularBadge(wxWindow* parent, const wxString& text, const wxColour& bgColor)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(16, 16))
    {
        m_text = text;
        m_bgColor = bgColor;
#ifdef __APPLE__
        // Set transparent background
        SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
        SetBackgroundColour(wxTransparentColour);
#else
        SetBackgroundColour(*wxWHITE);
#endif

        Bind(wxEVT_PAINT, &CircularBadge::OnPaint, this);
    }

    // Add method to update text
    void SetText(const wxString& text) {
        m_text = text;
        Refresh();
    }

private:
    void OnPaint(wxPaintEvent&);
    wxString m_text;
    wxColour m_bgColor;
};

class JustPrinButton : public wxPanel
{
public:
    JustPrinButton(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize);
    void DoSetSize(int x, int y, int width, int height, int sizeFlags = wxSIZE_AUTO) override;
    void AddJoin(std::function<void(wxMouseEvent&)> do_some) { m_do = do_some; }

private:
    void OnPaint(wxPaintEvent& event);
    void OnMouseEnter(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnMouseMove(wxMouseEvent& event);

private:
    bool                 m_isHovered{false};
    wxAnimationCtrlBase* m_animationCtrl{nullptr};
    std::function<void(wxMouseEvent&)> m_do{nullptr};
};

class JusPrinView3D : public View3D {
public:
    JusPrinView3D(wxWindow* parent, Bed3D& bed, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process);
    virtual ~JusPrinView3D();

    void changeChatPanelDisplay(const std::string& display);

    JusPrinChatPanel* jusprinChatPanel() const { return m_chat_panel; }

protected:
    void OnSize(wxSizeEvent& evt);
    void OnCanvasMouseDown(SimpleEvent& evt);

private:
    void initOverlay();
    void showChatPanel();
    void hideChatPanel();
    void updateChatPanelSize();

    // Chat panel size configuration
    int m_min_chat_height{MIN_CHAT_HEIGHT_LARGE};
    int m_min_chat_width{MIN_CHAT_WIDTH_LARGE};
    double m_chat_height_ratio{CHAT_HEIGHT_RATIO_LARGE};
    double m_chat_width_ratio{CHAT_WIDTH_RATIO_LARGE};

    JusPrinChatPanel* m_chat_panel{nullptr};
    JustPrinButton*   m_overlay_btn{nullptr};
    CircularBadge*    m_icon_text_left{nullptr};
    CircularBadge*    m_icon_text_right{nullptr};
};

} // namespace GUI
} // namespace Slic3r

#endif
