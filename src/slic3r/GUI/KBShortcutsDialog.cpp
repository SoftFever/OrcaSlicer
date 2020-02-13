#include "KBShortcutsDialog.hpp"
#include "I18N.hpp"
#include "libslic3r/Utils.hpp"
#include "GUI.hpp"
#include <wx/scrolwin.h>
#include <wx/display.h>
#include "GUI_App.hpp"
#include "wxExtensions.hpp"

namespace Slic3r {
namespace GUI {

KBShortcutsDialog::KBShortcutsDialog()
    : DPIDialog(NULL, wxID_ANY, wxString(SLIC3R_APP_NAME) + " - " + _(L("Keyboard Shortcuts")),
     wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    auto main_sizer = new wxBoxSizer(wxVERTICAL);

    // logo
    m_logo_bmp = ScalableBitmap(this, "PrusaSlicer_32px.png", 32);

    // fonts
    const wxFont& font = wxGetApp().normal_font();
    const wxFont& bold_font = wxGetApp().bold_font();
    SetFont(font);

    wxFont head_font = bold_font;
#ifdef __WXOSX__
    head_font.SetPointSize(14);
#else
    head_font.SetPointSize(bold_font.GetPointSize() + 2);
#endif // __WXOSX__

    fill_shortcuts();

    panel = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, get_size());
    panel->SetScrollbars(1, 20, 1, 2);

    auto main_grid_sizer = new wxFlexGridSizer(2, 10, 10);
    panel->SetSizer(main_grid_sizer);
    main_sizer->Add(panel, 1, wxEXPAND | wxALL, 0);

    wxBoxSizer* l_sizer = new wxBoxSizer(wxVERTICAL);
    main_grid_sizer->Add(l_sizer, 0);

    wxBoxSizer* r_sizer = new wxBoxSizer(wxVERTICAL);
    main_grid_sizer->Add(r_sizer, 0);

    m_head_bitmaps.reserve(m_full_shortcuts.size());

    for (auto& shortcut : m_full_shortcuts)
    {
        auto sizer = shortcut.second.second == szLeft ? l_sizer : r_sizer;
        wxBoxSizer* hsizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(hsizer, 0, wxEXPAND | wxTOP | wxBOTTOM, 10);

        // logo
        m_head_bitmaps.push_back(new wxStaticBitmap(panel, wxID_ANY, m_logo_bmp.bmp()));
        hsizer->Add(m_head_bitmaps.back(), 0, wxEXPAND | wxLEFT | wxRIGHT, 15);

        // head
        wxStaticText* head = new wxStaticText(panel, wxID_ANY, shortcut.first);
        head->SetFont(head_font);
        hsizer->Add(head, 0, wxALIGN_CENTER_VERTICAL);


        // Shortcuts list
        auto grid_sizer = new wxFlexGridSizer(2, 5, 15);
        sizer->Add(grid_sizer, 0, wxEXPAND | wxLEFT| wxRIGHT, 15);

        for (auto pair : shortcut.second.first)
        {
            auto shortcut = new wxStaticText(panel, wxID_ANY, _(pair.first));
            shortcut->SetFont(bold_font);
            grid_sizer->Add(shortcut, -1, wxALIGN_CENTRE_VERTICAL);

            auto description = new wxStaticText(panel, wxID_ANY, _(pair.second));
            description->SetFont(font);
            grid_sizer->Add(description, -1, wxALIGN_CENTRE_VERTICAL);
        }
    }

    wxStdDialogButtonSizer* buttons = this->CreateStdDialogButtonSizer(wxOK);

    this->SetEscapeId(wxID_OK);
    this->Bind(wxEVT_BUTTON, &KBShortcutsDialog::onCloseDialog, this, wxID_OK);
    main_sizer->Add(buttons, 0, wxEXPAND | wxRIGHT | wxBOTTOM, 15);

    this->Bind(wxEVT_LEFT_DOWN, &KBShortcutsDialog::onCloseDialog, this);

    SetSizer(main_sizer);
    main_sizer->SetSizeHints(this);
}

void KBShortcutsDialog::fill_shortcuts()
{
    const std::string &ctrl = GUI::shortkey_ctrl_prefix();
    const std::string &alt  = GUI::shortkey_alt_prefix();

    m_full_shortcuts.reserve(4);

    Shortcuts main_shortcuts;
    main_shortcuts.reserve(25);

    main_shortcuts.push_back(Shortcut(ctrl+"O"          ,L("Open project STL/OBJ/AMF/3MF with config, delete bed")));
    main_shortcuts.push_back(Shortcut(ctrl+"I"          ,L("Import STL/OBJ/AMF/3MF without config, keep bed")));
    main_shortcuts.push_back(Shortcut(ctrl+"L"          ,L("Load Config from .ini/amf/3mf/gcode")));
    main_shortcuts.push_back(Shortcut(ctrl+"G"          ,L("Export G-code")));
    main_shortcuts.push_back(Shortcut(ctrl+"S"          ,L("Save project (3MF)")));
    main_shortcuts.push_back(Shortcut(ctrl+alt+"L"      ,L("Load Config from .ini/amf/3mf/gcode and merge")));
    main_shortcuts.push_back(Shortcut(ctrl+"R"          ,L("(Re)slice")));
//    main_shortcuts.push_back(Shortcut(ctrl+"U"          ,L("Quick slice")));
//    main_shortcuts.push_back(Shortcut(ctrl+"Shift+U"    ,L("Repeat last quick slice")));
    main_shortcuts.push_back(Shortcut(ctrl+"1"          ,L("Select Plater Tab")));
//    main_shortcuts.push_back(Shortcut(ctrl+alt+"U"      ,L("Quick slice and Save as")));
    main_shortcuts.push_back(Shortcut(ctrl+"2"          ,L("Select Print Settings Tab")));
    main_shortcuts.push_back(Shortcut(ctrl+"3"          ,L("Select Filament Settings Tab")));
    main_shortcuts.push_back(Shortcut(ctrl+"4"          ,L("Select Printer Settings Tab")));
    main_shortcuts.push_back(Shortcut(ctrl+"5"          ,L("Switch to 3D")));
    main_shortcuts.push_back(Shortcut(ctrl+"6"          ,L("Switch to Preview")));
    main_shortcuts.push_back(Shortcut(ctrl+"P"          ,L("Preferences")));
    main_shortcuts.push_back(Shortcut(ctrl+"J"          ,L("Print host upload queue")));
    main_shortcuts.push_back(Shortcut("0-6"             ,L("Camera view")));
    main_shortcuts.push_back(Shortcut("+"               ,L("Add Instance of the selected object")));
    main_shortcuts.push_back(Shortcut("-"               ,L("Remove Instance of the selected object")));
    main_shortcuts.push_back(Shortcut("?"               ,L("Show keyboard shortcuts list")));
    main_shortcuts.push_back(Shortcut(ctrl/*+"LeftMouse"*/,L("Press to select multiple object or move multiple object with mouse")));

    m_full_shortcuts.push_back(std::make_pair(_(L("Main Shortcuts")), std::make_pair(main_shortcuts, szLeft)));


    Shortcuts plater_shortcuts;
    plater_shortcuts.reserve(20);

    plater_shortcuts.push_back(Shortcut("A",        L("Arrange")));
    plater_shortcuts.push_back(Shortcut("Shift+A",  L("Arrange selection")));
    plater_shortcuts.push_back(Shortcut(ctrl+"A",   L("Select All objects")));
    plater_shortcuts.push_back(Shortcut("Del",      L("Delete selected")));
    plater_shortcuts.push_back(Shortcut(ctrl+"Del", L("Delete All")));
    plater_shortcuts.push_back(Shortcut(ctrl+"C",   L("Copy to clipboard")));
    plater_shortcuts.push_back(Shortcut(ctrl+"V",   L("Paste from clipboard")));
    plater_shortcuts.push_back(Shortcut("M",        L("Gizmo move")));
    plater_shortcuts.push_back(Shortcut("S",        L("Gizmo scale")));
    plater_shortcuts.push_back(Shortcut("R",        L("Gizmo rotate")));
    plater_shortcuts.push_back(Shortcut("C",        L("Gizmo cut")));
    plater_shortcuts.push_back(Shortcut("F",        L("Gizmo Place face on bed")));
    plater_shortcuts.push_back(Shortcut("L",        L("Gizmo SLA support points")));
    plater_shortcuts.push_back(Shortcut("Shift+",   L("Press to activate selection rectangle\nor to snap by 5% in Gizmo scale\nor to snap by 1mm in Gizmo move")));
    plater_shortcuts.push_back(Shortcut("F",        L("Press to scale selection to fit print volume\nin Gizmo scale")));
    plater_shortcuts.push_back(Shortcut(alt,        L("Press to activate deselection rectangle\nor to scale or rotate selected objects\naround their own center")));
    plater_shortcuts.push_back(Shortcut(ctrl,       L("Press to activate one direction scaling in Gizmo scale")));
    plater_shortcuts.push_back(Shortcut("K",        L("Change camera type (perspective, orthographic)")));
    plater_shortcuts.push_back(Shortcut("B",        L("Zoom to Bed")));
    plater_shortcuts.push_back(Shortcut("Z",        L("Zoom to all objects in scene, if none selected")));
    plater_shortcuts.push_back(Shortcut("Z",        L("Zoom to selected object")));
    plater_shortcuts.push_back(Shortcut("I",        L("Zoom in")));
    plater_shortcuts.push_back(Shortcut("O",        L("Zoom out")));
    plater_shortcuts.push_back(Shortcut("E",        L("Show/Hide object/instance labels")));
    plater_shortcuts.push_back(Shortcut(ctrl+"M",   L("Show/Hide 3Dconnexion devices settings dialog")));
    plater_shortcuts.push_back(Shortcut("ESC",      L("Unselect gizmo / Clear selection")));
#if ENABLE_RENDER_PICKING_PASS
    // Don't localize debugging texts.
    plater_shortcuts.push_back(Shortcut("T",        "Toggle picking pass texture rendering on/off"));
#endif // ENABLE_RENDER_PICKING_PASS

    m_full_shortcuts.push_back(std::make_pair(_(L("Plater Shortcuts")), std::make_pair(plater_shortcuts, szRight)));


//     Shortcuts gizmo_shortcuts;
//     gizmo_shortcuts.reserve(2);
//
//     gizmo_shortcuts.push_back(Shortcut("Shift+",    L("Press to snap by 5% in Gizmo Scale\n or by 1mm in Gizmo Move")));
//     gizmo_shortcuts.push_back(Shortcut(alt,         L("Press to scale or rotate selected objects around their own center")));
//
//     m_full_shortcuts.push_back(std::make_pair(_(L("Gizmo Shortcuts")), std::make_pair(gizmo_shortcuts, 1)));


    Shortcuts preview_shortcuts;
    preview_shortcuts.reserve(4);

    preview_shortcuts.push_back(Shortcut(L("Arrow Up"),     L("Upper Layer")));
    preview_shortcuts.push_back(Shortcut(L("Arrow Down"),   L("Lower Layer")));
    preview_shortcuts.push_back(Shortcut("U",               L("Upper Layer")));
    preview_shortcuts.push_back(Shortcut("D",               L("Lower Layer")));
    preview_shortcuts.push_back(Shortcut("L",               L("Show/Hide (L)egend")));

    m_full_shortcuts.push_back(std::make_pair(_(L("Preview Shortcuts")), std::make_pair(preview_shortcuts, szLeft)));


    Shortcuts layers_slider_shortcuts;
    layers_slider_shortcuts.reserve(6);

    layers_slider_shortcuts.push_back(Shortcut(L("Arrow Up"),   L("Move current slider thumb Up")));
    layers_slider_shortcuts.push_back(Shortcut(L("Arrow Down"), L("Move current slider thumb Down")));
    layers_slider_shortcuts.push_back(Shortcut(L("Arrow Left"), L("Set upper thumb to current slider thumb")));
    layers_slider_shortcuts.push_back(Shortcut(L("Arrow Right"),L("Set lower thumb to current slider thumb")));
    layers_slider_shortcuts.push_back(Shortcut("+",             L("Add color change marker for current layer")));
    layers_slider_shortcuts.push_back(Shortcut("-",             L("Delete color change marker for current layer")));

    m_full_shortcuts.push_back(std::make_pair(_(L("Layers Slider Shortcuts")), std::make_pair(layers_slider_shortcuts, szRight)));
}

void KBShortcutsDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    m_logo_bmp.msw_rescale();

    for (wxStaticBitmap* bmp : m_head_bitmaps)
        bmp->SetBitmap(m_logo_bmp.bmp());

    const int em = em_unit();

    msw_buttons_rescale(this, em, { wxID_OK });

    wxSize size = get_size();

    panel->SetMinSize(size);

    SetMinSize(size);
    Fit();

    Refresh();
}

void KBShortcutsDialog::onCloseDialog(wxEvent &)
{
    this->EndModal(wxID_CLOSE);
}

wxSize KBShortcutsDialog::get_size()
{
    wxTopLevelWindow* window = Slic3r::GUI::find_toplevel_parent(this);
    const int display_idx = wxDisplay::GetFromWindow(window);
    wxRect display;
    if (display_idx == wxNOT_FOUND) {
        display = wxDisplay(0u).GetClientArea();
        window->Move(display.GetTopLeft());
    }
    else {
        display = wxDisplay(display_idx).GetClientArea();
    }

    const int em = em_unit();
    wxSize dialog_size = wxSize(90 * em, 85 * em);

    const int margin = 10 * em;
    if (dialog_size.x > display.GetWidth())
        dialog_size.x = display.GetWidth() - margin;
    if (dialog_size.y > display.GetHeight())
        dialog_size.y = display.GetHeight() - margin;

    return dialog_size;
}

} // namespace GUI
} // namespace Slic3r
