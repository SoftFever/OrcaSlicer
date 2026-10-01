#ifndef slic3r_GUI_AccessibilityMac_h_
#define slic3r_GUI_AccessibilityMac_h_

#include <functional>
#include <wx/string.h>

#ifdef __WXOSX__

class wxWindow;

namespace Slic3r {
namespace GUI {

// macOS VoiceOver accessibility for custom-drawn (non-native) widgets.
//
// OrcaSlicer draws many controls with a plain NSView (backed by wxWindow), which
// VoiceOver neither names nor identifies. This module attaches the NSAccessibility
// metadata those views need so VoiceOver can focus, name, and activate them. The C++
// API below is the only thing the rest of the codebase touches; every Cocoa and
// NSAccessibility detail stays in AccessibilityMac.mm.
namespace Accessibility {

// The NSAccessibility role an element advertises to VoiceOver. Add values here as more
// custom widgets are exposed. Keep in sync with role_string() in AccessibilityMac.mm.
enum class Role {
    Button,   // NSAccessibilityButtonRole
    Checkbox, // NSAccessibilityCheckBoxRole
};

// How a wxWindow should be exposed to VoiceOver.
struct Descriptor {
    Role role{ Role::Button };
    // Accessible name. When empty, the window's tooltip is used so icon-only controls
    // still get a name.
    wxString name;
    // Default action invoked when VoiceOver activates the element; empty for
    // non-interactive elements. It may capture the widget's `this` and is stored on the
    // window's NSView, which wxWidgets releases (in ~wxWidgetCocoaImpl) before the widget
    // is freed; only valid for window-backed elements.
    std::function<void()> press;
};

// Expose `win` to VoiceOver as described. Idempotent; call again whenever the name or
// role changes (for example after a button's label is updated).
void apply(wxWindow* win, const Descriptor& desc);

} // namespace Accessibility
} // namespace GUI
} // namespace Slic3r

#endif // __WXOSX__

#endif // !slic3r_GUI_AccessibilityMac_h_
