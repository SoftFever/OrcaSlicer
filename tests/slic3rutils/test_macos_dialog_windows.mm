// Opt-in GUI regression: requires a logged-in macOS desktop, not a headless CI worker.
#import <Cocoa/Cocoa.h>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>
#include <unistd.h>
#include <wx/app.h>
#include <wx/dialog.h>
#include <wx/defs.h>
#include <wx/event.h>
#include <wx/frame.h>
#include <wx/gdicmn.h>
#include <wx/popupwin.h>
#include <wx/utils.h>
#include <wx/window.h>

namespace {

class TestApp : public wxApp
{
public:
    bool OnInit() override
    {
        SetExitOnFrameDelete(false);
        auto* frame = new wxFrame(nullptr, wxID_ANY, "Dialog regression parent");
        SetTopWindow(frame);
        frame->Show();
        // Like wxWidgets' own GUI tests, run from idle inside the real Cocoa main loop.
        Bind(wxEVT_IDLE, [this](wxIdleEvent& event) {
            event.Skip();
            if (m_started)
                return;
            m_started = true;
            m_result = Catch::Session().run();
            ExitMainLoop();
        });
        return true;
    }

    int OnRun() override
    {
        wxApp::OnRun();
        return m_result;
    }

private:
    bool m_started = false;
    int m_result = 1;
};

NSWindow* native_window(wxWindow& window)
{
    return [(NSView*)window.GetHandle() window];
}

bool in_window_menu(NSWindow* window)
{
    for (NSMenuItem* item in [[NSApp windowsMenu] itemArray]) {
        if ([item target] == window)
            return true;
    }
    return false;
}

} // namespace

wxIMPLEMENT_APP_NO_MAIN(TestApp);

int main(int argc, char** argv)
{
    // Fail rather than hang indefinitely when no usable desktop/WindowServer is available.
    alarm(30);
    return wxEntry(argc, argv);
}

TEST_CASE("macOS dialogs remain selectable while retaining modality", "[MacDialogWindows][RequiresGUI]")
{
    auto* frame = wxTheApp->GetTopWindow();
    NSMenu* window_menu = [[NSMenu alloc] initWithTitle:@"Window"];
    [NSApp setWindowsMenu:window_menu];
    [window_menu release];
    REQUIRE([NSApp windowsMenu] != nil);

    // Non-resizable dialogs must be listed too, and not before the first Show().
    wxDialog dialog(frame, wxID_ANY, "Dialog regression", wxDefaultPosition,
                    wxSize(320, 200), wxCAPTION | wxCLOSE_BOX);
    NSWindow* native = native_window(dialog);
    CHECK_FALSE([native isKindOfClass:[NSPanel class]]);
    CHECK([native canBecomeMainWindow]);
    CHECK([native isExcludedFromWindowsMenu]);
    CHECK_FALSE(in_window_menu(native));
    CHECK(([native collectionBehavior] & NSWindowCollectionBehaviorManaged) != 0);
    CHECK(([native collectionBehavior] & NSWindowCollectionBehaviorParticipatesInCycle) != 0);
    CHECK(([native collectionBehavior] & (NSWindowCollectionBehaviorTransient |
                                         NSWindowCollectionBehaviorStationary |
                                         NSWindowCollectionBehaviorIgnoresCycle)) == 0);

    dialog.Show();
    CHECK_FALSE([native isExcludedFromWindowsMenu]);
    REQUIRE(in_window_menu(native));
    dialog.SetTitle("Renamed dialog regression");
    CHECK([[native title] isEqualToString:@"Renamed dialog regression"]);

    {
        wxWindowDisabler disabler(&dialog);
        CHECK_FALSE(frame->IsEnabled());
        CHECK_FALSE(dialog.IsModal());
        CHECK([NSApp modalWindow] == native);

        wxDialog child(&dialog, wxID_ANY, "Nested confirmation");
        child.CallAfter([&child] {
            CHECK(child.IsModal());
            CHECK([NSApp modalWindow] == native_window(child));
            child.EndModal(wxID_OK);
        });
        CHECK(child.ShowModal() == wxID_OK);
        CHECK([NSApp modalWindow] == native);
        CHECK_FALSE(frame->IsEnabled());
    }
    CHECK(frame->IsEnabled());
    CHECK([NSApp modalWindow] == nil);

    dialog.Hide();
    CHECK([native isExcludedFromWindowsMenu]);
    CHECK_FALSE(in_window_menu(native));
    dialog.SetTitle("Hidden dialog regression");
    CHECK_FALSE(in_window_menu(native));
    dialog.Show();
    CHECK(in_window_menu(native));
    dialog.Hide();

    wxDialog frameless(frame, wxID_ANY, "Frameless", wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    CHECK([native_window(frameless) isKindOfClass:[NSPanel class]]);
    wxDialog transient;
    transient.SetExtraStyle(wxWS_EX_TRANSIENT);
    transient.Create(frame, wxID_ANY, "Transient");
    CHECK([native_window(transient) isKindOfClass:[NSPanel class]]);
    wxPopupWindow popup(frame);
    CHECK([native_window(popup) isKindOfClass:[NSPanel class]]);
    auto* tool = new wxFrame(frame, wxID_ANY, "Tool", wxDefaultPosition, wxDefaultSize,
                             wxCAPTION | wxFRAME_TOOL_WINDOW);
    CHECK([native_window(*tool) isKindOfClass:[NSPanel class]]);
    tool->Destroy();
    frame->Destroy();
    // Stack dialogs die before app cleanup processes the deferred frame destruction.
}
