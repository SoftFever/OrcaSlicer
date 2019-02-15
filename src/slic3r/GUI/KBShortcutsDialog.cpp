#include "KBShortcutsDialog.hpp"
#include "I18N.hpp"
#include "libslic3r/Utils.hpp"
#include "GUI.hpp"
#include <wx/scrolwin.h>
#include "GUI_App.hpp"

namespace Slic3r { 
namespace GUI {

KBShortcutsDialog::KBShortcutsDialog()
    : wxDialog(NULL, wxID_ANY, _(L("Slic3r Prusa Edition - Keyboard Shortcuts")), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER)
{
	SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));    

	auto main_sizer = new wxBoxSizer(wxVERTICAL);

    // logo
	wxBitmap logo_bmp = wxBitmap(from_u8(Slic3r::var("Slic3r_32px.png")), wxBITMAP_TYPE_PNG);

    // fonts
    wxFont head_font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT).Bold();
#ifdef __WXOSX__
    head_font.SetPointSize(14);
#else
    head_font.SetPointSize(12);
#endif // __WXOSX__

    const wxFont& font = wxGetApp().small_font();
    const wxFont& bold_font = wxGetApp().bold_font();

    fill_shortcuts();

    auto panel = new wxPanel(this);
    auto main_grid_sizer = new wxFlexGridSizer(2, 10, 10);
    panel->SetSizer(main_grid_sizer);
    main_sizer->Add(panel, 1, wxEXPAND | wxALL, 0);

    wxBoxSizer* l_sizer = new wxBoxSizer(wxVERTICAL);
    main_grid_sizer->Add(l_sizer, 0);

    wxBoxSizer* r_sizer = new wxBoxSizer(wxVERTICAL);
    main_grid_sizer->Add(r_sizer, 0);

    for (auto& sc : m_full_shortcuts)
    {
//         auto sizer = sc.first == _(L("Main Shortcuts")) ? l_sizer : r_sizer;
        auto sizer = sc.second.second == 0 ? l_sizer : r_sizer;
        wxBoxSizer* hsizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(hsizer, 0, wxEXPAND | wxTOP | wxBOTTOM, 10);

        // logo
        auto *logo = new wxStaticBitmap(panel, wxID_ANY, logo_bmp);
        hsizer->Add(logo, 0, wxEXPAND | wxLEFT | wxRIGHT, 15);

        // head
        wxStaticText* head = new wxStaticText(panel, wxID_ANY, sc.first, wxDefaultPosition, wxSize(200,-1));
        head->SetFont(head_font);
        hsizer->Add(head, 0, wxALIGN_CENTER_VERTICAL);

        // Shortcuts list
        auto grid_sizer = new wxFlexGridSizer(2, 5, 15);
        sizer->Add(grid_sizer, 0, wxEXPAND | wxLEFT| wxRIGHT, 15);

        for (auto pair : sc.second.first)
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
    main_shortcuts.push_back(Shortcut(ctrl+"U"          ,L("Quick slice")));
    main_shortcuts.push_back(Shortcut(ctrl+"Shift+U"    ,L("Repeat last quick slice")));
    main_shortcuts.push_back(Shortcut(ctrl+"1"          ,L("Select Plater Tab")));
    main_shortcuts.push_back(Shortcut(ctrl+alt+"U"      ,L("Quick slice and Save as")));
    main_shortcuts.push_back(Shortcut(ctrl+"2"          ,L("Select Print Settings Tab")));
    main_shortcuts.push_back(Shortcut(ctrl+"3"          ,L("Select Filament Settings Tab")));
    main_shortcuts.push_back(Shortcut(ctrl+"4"          ,L("Select Printer Settings Tab")));
    main_shortcuts.push_back(Shortcut(ctrl+"5"          ,L("Switch to 3D")));
    main_shortcuts.push_back(Shortcut(ctrl+"6"          ,L("Switch to Preview")));
    main_shortcuts.push_back(Shortcut(ctrl+"P"          ,L("Preferences")));
    main_shortcuts.push_back(Shortcut(ctrl+"J"          ,L("Print host upload queue")));
    main_shortcuts.push_back(Shortcut("0-6"             ,L("Camera view ")));
    main_shortcuts.push_back(Shortcut("+"               ,L("Add Instance to selected object ")));
    main_shortcuts.push_back(Shortcut("-"               ,L("Remove Instance from selected object")));
    main_shortcuts.push_back(Shortcut("?"               ,L("Show keyboard shortcuts list")));
    main_shortcuts.push_back(Shortcut("Shift+LeftMouse", L("Select multiple object/Move multiple object")));

    m_full_shortcuts.push_back(std::make_pair( _(L("Main Shortcuts")), std::make_pair(main_shortcuts, 0) ));


    Shortcuts plater_shortcuts;
    plater_shortcuts.reserve(20);

    plater_shortcuts.push_back(Shortcut("A",        L("Arrange")));
    plater_shortcuts.push_back(Shortcut(ctrl+"A",   L("Select All objects")));
    plater_shortcuts.push_back(Shortcut("Del",      L("Delete selected")));
    plater_shortcuts.push_back(Shortcut(ctrl+"Del", L("Delete All")));
    plater_shortcuts.push_back(Shortcut("M",        L("Gizmo move")));
    plater_shortcuts.push_back(Shortcut("S",        L("Gizmo scale")));
    plater_shortcuts.push_back(Shortcut("R",        L("Gizmo rotate")));
    plater_shortcuts.push_back(Shortcut("C",        L("Gizmo cut")));
    plater_shortcuts.push_back(Shortcut("F",        L("Gizmo Place face on bed")));
    plater_shortcuts.push_back(Shortcut("L",        L("Gizmo SLA support points")));
    plater_shortcuts.push_back(Shortcut("Shift+",   L("Press to snap by 5% in Gizmo scale\nor by 1mm in Gizmo move")));
    plater_shortcuts.push_back(Shortcut(alt,        L("Press to scale or rotate selected objects\naround their own center")));
    plater_shortcuts.push_back(Shortcut("B",        L("Zoom to Bed")));
    plater_shortcuts.push_back(Shortcut("Z",        L("Zoom to all objects in scene, if none selected")));
    plater_shortcuts.push_back(Shortcut("Z",        L("Zoom to selected object")));
    plater_shortcuts.push_back(Shortcut("I",        L("Zoom in")));
    plater_shortcuts.push_back(Shortcut("O",        L("Zoom out")));
    plater_shortcuts.push_back(Shortcut("ESC",      L("Unselect gizmo, keep object selection")));

    m_full_shortcuts.push_back(std::make_pair(_(L("Plater Shortcuts")), std::make_pair(plater_shortcuts, 1)));


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

    m_full_shortcuts.push_back(std::make_pair( _(L("Preview Shortcuts")), std::make_pair(preview_shortcuts, 0) ));


    Shortcuts layers_slider_shortcuts;
    layers_slider_shortcuts.reserve(6);

    layers_slider_shortcuts.push_back(Shortcut(L("Arrow Up"),   L("Move current slider thump Up")));
    layers_slider_shortcuts.push_back(Shortcut(L("Arrow Down"), L("Move current slider thump Down")));
    layers_slider_shortcuts.push_back(Shortcut(L("Arrow Left"), L("Set upper thumb to current slider thumb")));
    layers_slider_shortcuts.push_back(Shortcut(L("Arrow Right"),L("Set lower thumb to current slider thumb")));
    layers_slider_shortcuts.push_back(Shortcut("+",             L("Add color change marker for current layer")));
    layers_slider_shortcuts.push_back(Shortcut("-",             L("Delete color change marker for current layer")));

    m_full_shortcuts.push_back(std::make_pair( _(L("Layers Slider Shortcuts")), std::make_pair(layers_slider_shortcuts, 1) ));
}

void KBShortcutsDialog::onCloseDialog(wxEvent &)
{
    this->EndModal(wxID_CLOSE);
    this->Close();
}

} // namespace GUI
} // namespace Slic3r
