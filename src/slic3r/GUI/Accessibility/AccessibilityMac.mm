#include "AccessibilityMac.h"

#include <objc/runtime.h>
#include <wx/window.h>
#include "wx/osx/core/cfstring.h"
#import <Cocoa/Cocoa.h>

// ---------------------------------------------------------------------------
// VoiceOver accessibility for custom-drawn controls.
//
// OrcaSlicer renders its dialog controls with a plain NSView (backed by wxWindow),
// which VoiceOver neither names nor identifies. We attach the accessibility metadata to
// the control's NSView using associated objects and answer the NSAccessibility informal
// protocol for views that opt in. Only views marked with orcaSetAccessibilityElement:
// are affected; all others keep the default NSView behaviour (not an element, unknown
// role).
// ---------------------------------------------------------------------------

static const void* kOrcaAccessibilityFlagKey   = &kOrcaAccessibilityFlagKey;
static const void* kOrcaAccessibilityRoleKey   = &kOrcaAccessibilityRoleKey;
static const void* kOrcaAccessibilityTitleKey  = &kOrcaAccessibilityTitleKey;
static const void* kOrcaAccessibilityWindowKey = &kOrcaAccessibilityWindowKey;
static const void* kOrcaAccessibilityPressKey  = &kOrcaAccessibilityPressKey;

@interface NSView (OrcaSlicerAccessibility)
- (BOOL)orcaIsAccessibilityElement;
- (void)orcaSetAccessibilityElement:(BOOL)flag;
- (wxWindow*)orcaAccessibilityWindow;
- (void)orcaSetAccessibilityWindow:(wxWindow*)win;
- (void)orcaSetAccessibilityRole:(NSString*)role;
- (void)orcaSetAccessibilityTitle:(NSString*)title;
- (void)orcaSetAccessibilityPressHandler:(void(^)(void))handler;
@end

@implementation NSView (OrcaSlicerAccessibility)

- (BOOL)orcaIsAccessibilityElement
{
    return [objc_getAssociatedObject(self, kOrcaAccessibilityFlagKey) boolValue];
}

- (void)orcaSetAccessibilityElement:(BOOL)flag
{
    objc_setAssociatedObject(self, kOrcaAccessibilityFlagKey, @(flag), OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

- (wxWindow*)orcaAccessibilityWindow
{
    return (wxWindow*)[(NSValue*)objc_getAssociatedObject(self, kOrcaAccessibilityWindowKey) pointerValue];
}

- (void)orcaSetAccessibilityWindow:(wxWindow*)win
{
    objc_setAssociatedObject(self, kOrcaAccessibilityWindowKey, [NSValue valueWithPointer:win], OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

- (void)orcaSetAccessibilityRole:(NSString*)role
{
    objc_setAssociatedObject(self, kOrcaAccessibilityRoleKey, role, OBJC_ASSOCIATION_COPY_NONATOMIC);
}

- (void)orcaSetAccessibilityTitle:(NSString*)title
{
    objc_setAssociatedObject(self, kOrcaAccessibilityTitleKey, title, OBJC_ASSOCIATION_COPY_NONATOMIC);
}

- (void)orcaSetAccessibilityPressHandler:(void(^)(void))handler
{
    objc_setAssociatedObject(self, kOrcaAccessibilityPressKey, handler, OBJC_ASSOCIATION_COPY_NONATOMIC);
}

// NSAccessibility informal protocol. These only alter behaviour for views that have
// opted in above; for everything else they fall back to the plain-NSView defaults.

- (BOOL)isAccessibilityElement
{
    return [self orcaIsAccessibilityElement];
}

- (NSString*)accessibilityRole
{
    if (![self orcaIsAccessibilityElement])
        return NSAccessibilityUnknownRole;
    return objc_getAssociatedObject(self, kOrcaAccessibilityRoleKey);
}

// The element's accessible *name* is its title (NSAccessibilityTitleAttribute). Do not
// use accessibilityLabel here: on macOS that maps to the *description* attribute, which
// VoiceOver does not announce as the element's name.
- (NSString*)accessibilityTitle
{
    if (![self orcaIsAccessibilityElement])
        return nil;
    return objc_getAssociatedObject(self, kOrcaAccessibilityTitleKey);
}

- (BOOL)isAccessibilityEnabled
{
    wxWindow* win = [self orcaAccessibilityWindow];
    return win ? win->IsEnabled() : YES;
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wobjc-protocol-method-implementation"
- (BOOL)accessibilityPerformPress
{
    if (![self orcaIsAccessibilityElement])
        return NO;

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

} // namespace

void apply(wxWindow* win, const Descriptor& desc)
{
    if (!win)
        return;

    NSView* view = (NSView*)win->GetHandle();
    if (!view)
        return;

    [view orcaSetAccessibilityWindow:win];
    [view orcaSetAccessibilityRole:role_string(desc.role)];

    wxString name = desc.name;
    if (name.empty())
        name = win->GetToolTipText(); // fall back to the window's tooltip
    [view orcaSetAccessibilityTitle:name.empty() ? nil : wxCFStringRef(name).AsNSString()];

    if (desc.press) {
        auto press = desc.press; // copy the std::function into the stored block
        [view orcaSetAccessibilityPressHandler:^{ press(); }];
    } else {
        [view orcaSetAccessibilityPressHandler:nil];
    }

    [view orcaSetAccessibilityElement:YES];
}

} // namespace Accessibility
} // namespace GUI
} // namespace Slic3r
