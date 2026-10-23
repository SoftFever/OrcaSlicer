#ifndef slic3r_GUI_AccessibilityMac_h_
#define slic3r_GUI_AccessibilityMac_h_

#include <functional>
#include <wx/string.h>

#ifdef __WXOSX__

class wxWindow;

namespace Slic3r {
namespace GUI {

// macOS accessibility (VoiceOver / Voice Control) for custom-drawn and native widgets.
//
// OrcaSlicer draws many controls with a plain NSView (backed by wxWindow), which
// assistive technologies neither name nor identify. This module configures the
// NSAccessibility metadata those views need so VoiceOver can focus, name and activate
// them and Voice Control can target them. The C++ API below is the only thing the rest
// of the codebase touches; every Cocoa and NSAccessibility detail stays in
// AccessibilityMac.mm.
namespace Accessibility {

// The NSAccessibility role an element advertises. Add values here as more custom
// widgets are exposed. Keep in sync with role_string() in AccessibilityMac.mm.
enum class Role {
    Button,   // NSAccessibilityButtonRole
    Checkbox, // NSAccessibilityCheckBoxRole
};

// How a wxWindow should be exposed to assistive technologies.
struct Descriptor {
    Role role{ Role::Button };
    // Accessible name. When empty, the window's tooltip is used so icon-only controls
    // still get a name.
    wxString name;
    // Future: let a caller override the auto-derived Voice Control identifier. Currently
    // apply() always derives it from the window id (see default_identifier()), so no
    // caller sets this.
    // wxString identifier;
    // Future: expose a custom-drawn (non-native) toggle's checked/mixed state as
    // accessibilityValue (0 = off, 1 = on, 2 = mixed). No custom toggle is exposed yet,
    // and the native checkbox reports its own value.
    // int value = -1;
    // Default action invoked when the assistive technology activates the element; empty
    // for non-interactive elements. It may capture the widget's `this` and is stored on
    // the window's NSView, which wxWidgets releases (in ~wxWidgetCocoaImpl) before the
    // widget is freed; only valid for window-backed elements. Native controls already
    // have a press action and ignore this.
    std::function<void()> press;
};

// Expose `win` to assistive technologies as described. Idempotent; call again whenever
// the name or enabled state changes (for example after a button's label is updated).
//
// The call is a no-op for a native control (an NSControl-backed wxWindow, e.g. a
// wxBitmapToggleButton): AppKit already exposes its role, value, enabled state and press
// action, so apply() only fills in the name and Voice Control identifier. Custom-drawn
// views are configured in full.
void apply(wxWindow* win, const Descriptor& desc);

} // namespace Accessibility
} // namespace GUI
} // namespace Slic3r

#endif // __WXOSX__

#endif // !slic3r_GUI_AccessibilityMac_h_
