#include "AccessibilityMac.h"

#include <objc/runtime.h>
#include <wx/window.h>
#include "wx/osx/core/cfstring.h"
#import <Cocoa/Cocoa.h>

// ---------------------------------------------------------------------------
// VoiceOver / Voice Control accessibility for custom-drawn controls.
//
// OrcaSlicer renders its dialog controls with a plain wxNSView (backed by wxWindow),
// which assistive technologies neither name nor identify. We configure the
// NSAccessibility attributes of those views using AppKit's instance-scoped setters
// (setAccessibilityRole:, setAccessibilityTitle:, setAccessibilityValue:,
// setAccessibilityIdentifier:, setAccessibilityElement:). Using the setters — rather
// than a category overriding the getters on every NSView — confines the change to the
// specific views we opt in.
//
// Two attributes cannot be expressed as static setters and are provided through a small
// NSView category that only affects plain NSView instances (native controls such as
// NSButton implement these methods themselves, shadowing the category):
//   - accessibilityPerformPress, the activation action, which is a method with no setter;
//   - isAccessibilityEnabled, which must reflect the widget's live wxWindow state.
// ---------------------------------------------------------------------------

static const void* kOrcaAccessibilityPressKey  = &kOrcaAccessibilityPressKey;
static const void* kOrcaAccessibilityWindowKey = &kOrcaAccessibilityWindowKey;

@interface NSView (OrcaSlicerAccessibility)
- (void)orcaSetAccessibilityPressHandler:(void(^)(void))handler;
- (void)orcaSetAccessibilityWindow:(wxWindow*)win;
- (wxWindow*)orcaAccessibilityWindow;
@end

@implementation NSView (OrcaSlicerAccessibility)

- (void)orcaSetAccessibilityPressHandler:(void(^)(void))handler
{
    objc_setAssociatedObject(self, kOrcaAccessibilityPressKey, handler, OBJC_ASSOCIATION_COPY_NONATOMIC);
}

- (void)orcaSetAccessibilityWindow:(wxWindow*)win
{
    objc_setAssociatedObject(self, kOrcaAccessibilityWindowKey, [NSValue valueWithPointer:win], OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

- (wxWindow*)orcaAccessibilityWindow
{
    return (wxWindow*)[(NSValue*)objc_getAssociatedObject(self, kOrcaAccessibilityWindowKey) pointerValue];
}

// NSAccessibilityEnabledAttribute. Reflects the live widget state, which includes a
// disabled ancestor, so it cannot be captured as a one-shot setter value.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wobjc-protocol-method-implementation"
- (BOOL)isAccessibilityEnabled
{
    wxWindow* win = [self orcaAccessibilityWindow];
    return win ? win->IsEnabled() : YES;
}

// NSAccessibilityPressAction. A method, not a settable property, so it is implemented
// here for custom-drawn views. Views that were never configured have no handler and
// simply report the press as unhandled, matching the default NSView behaviour.
- (BOOL)accessibilityPerformPress
{
    void(^handler)(void) = objc_getAssociatedObject(self, kOrcaAccessibilityPressKey);
    if (handler) {
        handler();
        return YES;
    }
    return NO;
}
#pragma clang diagnostic pop

@end

namespace Slic3r {
namespace GUI {
namespace Accessibility {

namespace {

// Maps a Role to its NSAccessibility role string. Keep in sync with Role in
// AccessibilityMac.h: every enumerator needs a case here, otherwise it falls through to
// NSAccessibilityUnknownRole.
NSString* role_string(Role role)
{
    switch (role) {
    case Role::Button:   return NSAccessibilityButtonRole;
    case Role::Checkbox: return NSAccessibilityCheckBoxRole;
    }
    return NSAccessibilityUnknownRole;
}

// A stable, unique identifier for Voice Control. wxWindow::GetId() is unique within the
// window for the lifetime of the process, which is all Voice Control's number overlay
// needs to keep its spoken-number mapping stable.
wxString default_identifier(const wxWindow* win)
{
    return wxString::Format("wxID_%d", (int)win->GetId());
}

} // namespace

void apply(wxWindow* win, const Descriptor& desc)
{
    if (!win)
        return;

    NSView* view = (NSView*)win->GetHandle();
    if (!view)
        return;

    // A native control (an NSControl-backed wxWindow such as a wxBitmapToggleButton)
    // already exposes its role, value, enabled state and press action through AppKit, so
    // we only fill in the two pieces AppKit cannot derive for our controls: the name and
    // the Voice Control identifier. A custom-drawn view (a plain wxNSView) exposes none
    // of this, so it is configured in full below.
    const bool native = [view isKindOfClass:[NSControl class]];

    // NSAccessibilityTitleAttribute — the element's name. Fall back to the tooltip so
    // icon-only controls still get a name.
    wxString name = desc.name;
    if (name.empty())
        name = win->GetToolTipText();
    [view setAccessibilityTitle:name.empty() ? nil : wxCFStringRef(name).AsNSString()];

    // NSAccessibilityIdentifierAttribute — a stable id Voice Control uses to map spoken
    // commands back to the element. Derived from the window id (see default_identifier()).
    wxString identifier = default_identifier(win);
    [view setAccessibilityIdentifier:wxCFStringRef(identifier).AsNSString()];

    if (native)
        return;

    [view orcaSetAccessibilityWindow:win];

    // NSAccessibilityRoleAttribute — the element's role ("button", "checkbox", ...).
    [view setAccessibilityRole:role_string(desc.role)];

    // Future: NSAccessibilityValueAttribute — a custom-drawn toggle's checked/mixed
    // state (0 = off, 1 = on, 2 = mixed). No custom toggle is exposed yet; the native
    // checkbox reports its own value.
    // if (desc.value >= 0)
    //     [view setAccessibilityValue:@(desc.value)];

    if (desc.press) {
        auto press = desc.press; // copy the std::function into the stored block
        [view orcaSetAccessibilityPressHandler:^{ press(); }];
    } else {
        [view orcaSetAccessibilityPressHandler:nil];
    }

    // isAccessibilityElement — opt this view into the accessibility tree.
    [view setAccessibilityElement:YES];
}

} // namespace Accessibility
} // namespace GUI
} // namespace Slic3r
