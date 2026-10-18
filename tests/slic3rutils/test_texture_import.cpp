#include <glad/gl.h>
#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>

#include "libslic3r/Format/AssimpImport.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/TexturePainting.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/TextureImportDialog.hpp"
#include "test_utils.hpp"

#include <chrono>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <wx/app.h>
#include <wx/defs.h>
#include <wx/event.h>
#include <wx/evtloop.h>
#include <wx/frame.h>
#include <wx/glcanvas.h>
#include <wx/image.h>
#include <wx/init.h>
#include <wx/timer.h>

#if defined(__WXGTK__) && defined(wxHAS_EGL)
#include "slic3r/GUI/LinuxDisplayBackend.hpp"
#include <EGL/egl.h>
#endif

// A separate executable keeps wxWidgets initialization and a native fast-fail
// in the dialog from interfering with the other slic3rutils tests.
TEST_CASE("A JPEG textured GLB reaches filament matching without interaction", "[TextureImport][Integration][RequiresApp]")
{
#if defined(__linux__)
    if (!std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY"))
        SKIP("This integration test needs a display (for example, xvfb-run).");
#endif

    ScopedTemporaryDir profile;
    Slic3r::set_data_dir(profile.string());
    Slic3r::set_resources_dir(ORCA_TEST_RESOURCES_DIR);
    Slic3r::set_var_dir(std::string(ORCA_TEST_RESOURCES_DIR) + "/images");

    auto* app = new Slic3r::GUI::GUI_App();
    wxApp::SetInstance(app);
    char program[] = "texture_import_integration_tests";
    char* argv[] = {program, nullptr};
    int argc = 1;
    REQUIRE(wxEntryStart(argc, argv));
    const std::unique_ptr<wxApp, void (*)(wxApp*)> cleanup(app, [](wxApp*) { wxEntryCleanup(); });
    // Do not call GUI_App::OnInit: no main window, printer connection, wizard,
    // user profiles or updater is needed to exercise the real import dialog.
    wxInitAllImageHandlers();
    app->preset_bundle = new Slic3r::PresetBundle();
    app->init_fonts();

    wxGUIEventLoop event_loop;
    wxEventLoopActivator activate(&event_loop);
    Slic3r::TexturedMesh mesh;
    std::string error;
    const char* supplied_model = std::getenv("ORCA_TEXTURE_IMPORT_TEST_MODEL");
    const std::string filename = supplied_model ? supplied_model :
        std::string(TEST_DATA_DIR) + "/texture_import/two_color_jpeg.glb";
    INFO(filename);
    INFO(error);
    REQUIRE(Slic3r::load_assimp_textured_model(filename, mesh, &error));
    REQUIRE_FALSE(mesh.textures.empty());

    std::vector<unsigned char> pixels;
    int width = 0, height = 0;
    REQUIRE(Slic3r::decode_texture_to_pixels(mesh.textures.front(), pixels, width, height));
    REQUIRE(width > 0);
    REQUIRE(height > 0);

    auto parent = std::make_unique<wxFrame>(nullptr, wxID_ANY, "Texture import integration test");
    // The full app loads GL entry points before constructing the preview. Give
    // this isolated test the same prerequisite without starting the main frame.
    {
        wxGLAttributes attributes;
        attributes.PlatformDefaults().RGBA().DoubleBuffer().Depth(24).EndList();
        wxGLCanvas canvas(parent.get(), attributes);
        parent->Show();
        const auto shown_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            event_loop.YieldFor(wxEVT_CATEGORY_UI);
            if (canvas.IsShownOnScreen())
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } while (std::chrono::steady_clock::now() < shown_deadline);
        REQUIRE(canvas.IsShownOnScreen());
        wxGLContext context(&canvas);
        REQUIRE(context.IsOK());
        REQUIRE(canvas.SetCurrent(context));
        int version = 0;
#if defined(__WXGTK__) && defined(wxHAS_EGL)
        // Match OpenGLManager's loader selection without starting the renderer.
        if (Slic3r::GUI::is_running_on_wayland())
            version = gladLoadGL(reinterpret_cast<GLADloadfunc>(eglGetProcAddress));
        else
#endif
            version = gladLoaderLoadGL();
        REQUIRE(version != 0);
    }
    parent->Hide();
    Slic3r::GUI::TextureFilamentEntry filament;
    filament.dialog_index = 0;
    filament.project_config_index = 0;
    filament.color_hex = "#FF0000";
    filament.name = "Generic PLA";
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(supplied_model ? 600 : 30);
    bool timed_out = false;
    {
        // Choose the existing no-repair option so a supplied local model with
        // open boundaries does not require answering an unrelated modal prompt.
        Slic3r::GUI::TextureImportDialog dialog(parent.get(), mesh, {filament}, [&] {
            timed_out = std::chrono::steady_clock::now() >= deadline;
            return timed_out;
        }, [](int) { return true; },
            Slic3r::TexturePaintingSettings::MeshRepairDecision::ImportWithoutRepair);

        wxEvtHandler observer;
        wxTimer timer(&observer);
        observer.Bind(wxEVT_TIMER, [&](wxTimerEvent&) {
            // ShowModal only enters the modal loop after the worker result has
            // been consumed, the preview updated and mapping rows constructed.
            if (dialog.IsModal())
                dialog.EndModal(wxID_OK);
        });
        timer.Start(10);
        const int answer = dialog.ShowModal();
        timer.Stop();

        REQUIRE_FALSE(timed_out);
        REQUIRE(answer == wxID_OK);
        REQUIRE_FALSE(dialog.fallback_to_geometry_only());
        const auto painted = dialog.get_painted_mesh();
        REQUIRE_FALSE(painted.indices.empty());
        REQUIRE(painted.face_colors.size() == painted.indices.size());
        REQUIRE_FALSE(painted.cluster_colors.empty());
        const auto matches = dialog.get_matches();
        REQUIRE(matches.size() == painted.cluster_colors.size());
        for (const auto& match : matches)
            REQUIRE(match.filament_index >= 0);
    }
    parent.reset();
}
