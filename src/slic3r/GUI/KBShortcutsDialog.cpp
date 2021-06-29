#include "libslic3r/libslic3r.h"
#include "KBShortcutsDialog.hpp"
#include "I18N.hpp"
#include "libslic3r/Utils.hpp"
#include "GUI.hpp"
#include "Notebook.hpp"
#include <wx/scrolwin.h>
#include <wx/display.h>
#include "GUI_App.hpp"
#include "wxExtensions.hpp"
#include "MainFrame.hpp"
#include <wx/notebook.h>

namespace Slic3r {
namespace GUI {

KBShortcutsDialog::KBShortcutsDialog()
    : DPIDialog(static_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY, wxString(wxGetApp().is_editor() ? SLIC3R_APP_NAME : GCODEVIEWER_APP_NAME) + " - " + _L("Keyboard Shortcuts"),
    wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    // fonts
    const wxFont& font = wxGetApp().normal_font();
    const wxFont& bold_font = wxGetApp().bold_font();
    SetFont(font);

    auto main_sizer = new wxBoxSizer(wxVERTICAL);

    main_sizer->Add(create_header(this, bold_font), 0, wxEXPAND | wxALL, 10);

#ifdef _MSW_DARK_MODE
    wxBookCtrlBase* book;
//    if (wxGetApp().dark_mode()) 
        book = new Notebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP);
/*    else
        book = new wxNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP);*/
#else
    wxNotebook* book = new wxNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP);
#endif
    main_sizer->Add(book, 1, wxEXPAND | wxALL, 10);

    fill_shortcuts();
    for (size_t i = 0; i < m_full_shortcuts.size(); ++i) {
        wxPanel* page = create_page(book, m_full_shortcuts[i], font, bold_font);
        m_pages.push_back(page);
        book->AddPage(page, m_full_shortcuts[i].first.first, i == 0);
    }

    wxStdDialogButtonSizer* buttons = this->CreateStdDialogButtonSizer(wxOK);
    wxGetApp().UpdateDarkUI(static_cast<wxButton*>(this->FindWindowById(wxID_OK, this)));
    this->SetEscapeId(wxID_OK);
    main_sizer->Add(buttons, 0, wxEXPAND | wxALL, 5);

    SetSizer(main_sizer);
    main_sizer->SetSizeHints(this);
    this->CenterOnParent();
}

void KBShortcutsDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    m_logo_bmp.msw_rescale();
    m_header_bitmap->SetBitmap(m_logo_bmp.bmp());
    msw_buttons_rescale(this, em_unit(), { wxID_OK });

    Layout();
    Fit();
    Refresh();
}

void KBShortcutsDialog::fill_shortcuts()
{
    const std::string& ctrl = GUI::shortkey_ctrl_prefix();
    const std::string& alt = GUI::shortkey_alt_prefix();

    if (wxGetApp().is_editor()) {
        Shortcuts commands_shortcuts = {
            // File
            { ctrl + "N", L("New project, clear plater") },
            { ctrl + "O", L("Open project STL/OBJ/AMF/3MF with config, clear plater") },
            { ctrl + "S", L("Save project (3mf)") },
            { ctrl + alt + "S", L("Save project as (3mf)") },
            { ctrl + "R", L("(Re)slice") },
            // File>Import
            { ctrl + "I", L("Import STL/OBJ/AMF/3MF without config, keep plater") },
            { ctrl + "L", L("Import Config from ini/amf/3mf/gcode") },
            { ctrl + alt + "L", L("Load Config from ini/amf/3mf/gcode and merge") },
            // File>Export
            { ctrl + "G", L("Export G-code") },
            { ctrl + "Shift+" + "G", L("Send G-code") },
            { ctrl + "E", L("Export config") },
            { ctrl + "U", L("Export to SD card / Flash drive") },
            { ctrl + "T", L("Eject SD card / Flash drive") },
            // Edit
            { ctrl + "A", L("Select all objects") },
            { "Esc", L("Deselect all") },
            { "Del", L("Delete selected") },
            { ctrl + "Del", L("Delete all") },
            { ctrl + "Z", L("Undo") },
            { ctrl + "Y", L("Redo") },
            { ctrl + "C", L("Copy to clipboard") },
            { ctrl + "V", L("Paste from clipboard") },
#ifdef __APPLE__
            { ctrl + "Shift+" + "R", L("Reload plater from disk") },
#else
            { "F5", L("Reload plater from disk") },
#endif // __APPLE__
            { ctrl + "F", L("Search") },
            // Window
            { ctrl + "1", L("Select Plater Tab") },
            { ctrl + "2", L("Select Print Settings Tab") },
            { ctrl + "3", L("Select Filament Settings Tab") },
            { ctrl + "4", L("Select Printer Settings Tab") },
            { ctrl + "5", L("Switch to 3D") },
            { ctrl + "6", L("Switch to Preview") },
            { ctrl + "J", L("Print host upload queue") },
            { ctrl + "Shift+" + "I", L("Open new instance") },
            // View
            { "0-6", L("Camera view") },
            { "E", L("Show/Hide object/instance labels") },
            // Configuration
#ifdef __APPLE__
            { ctrl + ",", L("Preferences") },
#else
            { ctrl + "P", L("Preferences") },
#endif
            // Help
            { "?", L("Show keyboard shortcuts list") }
        };

        m_full_shortcuts.push_back({ { _L("Commands"), "" }, commands_shortcuts });

        Shortcuts plater_shortcuts = {
            { "A", L("Arrange") },
            { "Shift+A", L("Arrange selection") },
            { "+", L("Add Instance of the selected object") },
            { "-", L("Remove Instance of the selected object") },
            { ctrl, L("Press to select multiple objects\nor move multiple objects with mouse") },
            { "Shift+", L("Press to activate selection rectangle") },
            { alt, L("Press to activate deselection rectangle") },
            { L("Arrow Up"), L("Move selection 10 mm in positive Y direction") },
            { L("Arrow Down"), L("Move selection 10 mm in negative Y direction") },
            { L("Arrow Left"), L("Move selection 10 mm in negative X direction") },
            { L("Arrow Right"), L("Move selection 10 mm in positive X direction") },
            { std::string("Shift+") + L("Any arrow"), L("Movement step set to 1 mm") },
            { ctrl + L("Any arrow"), L("Movement in camera space") },
            { L("Page Up"), L("Rotate selection 45 degrees CCW") },
            { L("Page Down"), L("Rotate selection 45 degrees CW") },
            { "M", L("Gizmo move") },
            { "S", L("Gizmo scale") },
            { "R", L("Gizmo rotate") },
            { "C", L("Gizmo cut") },
            { "F", L("Gizmo Place face on bed") },
            { "H", L("Gizmo SLA hollow") },
            { "L", L("Gizmo SLA support points") },
            { "Esc", L("Unselect gizmo or clear selection") },
            { "K", L("Change camera type (perspective, orthographic)") },
            { "B", L("Zoom to Bed") },
            { "Z", L("Zoom to selected object\nor all objects in scene, if none selected") },
            { "I", L("Zoom in") },
            { "O", L("Zoom out") },
            { "Tab", L("Switch between Editor/Preview") },
            { "Shift+Tab", L("Collapse/Expand the sidebar") },
#ifdef _WIN32
            { ctrl + "M", L("Show/Hide 3Dconnexion devices settings dialog, if enabled") },
#else
#ifdef __APPLE__
            { ctrl + "Shift+M", L("Show/Hide 3Dconnexion devices settings dialog") },
            { ctrl + "M", L("Minimize application") },
#else
            { ctrl + "M", L("Show/Hide 3Dconnexion devices settings dialog") },
#endif // __APPLE__
#endif // _WIN32
#if ENABLE_RENDER_PICKING_PASS
            // Don't localize debugging texts.
            { "P", "Toggle picking pass texture rendering on/off" },
#endif // ENABLE_RENDER_PICKING_PASS
        };

        m_full_shortcuts.push_back({ { _L("Plater"), "" }, plater_shortcuts });

        Shortcuts gizmos_shortcuts = {
            { ctrl, L("All gizmos: Rotate - left mouse button; Pan - right mouse button") },
            { "Shift+", L("Gizmo move: Press to snap by 1mm") },
            { "Shift+", L("Gizmo scale: Press to snap by 5%") },
            { "F", L("Gizmo scale: Scale selection to fit print volume") },
            { ctrl, L("Gizmo scale: Press to activate one direction scaling") },
            { alt, L("Gizmo scale: Press to scale selected objects around their own center") },
            { alt, L("Gizmo rotate: Press to rotate selected objects around their own center") },
        };

        m_full_shortcuts.push_back({ { _L("Gizmos"), _L("The following shortcuts are applicable when the specified gizmo is active") }, gizmos_shortcuts });

        Shortcuts object_list_shortcuts = {
            { "P", L("Set selected items as Ptrintable/Unprintable") },
            { "0", L("Set default extruder for the selected items") },
            { "1-9", L("Set extruder number for the selected items") },
        };

        m_full_shortcuts.push_back({ { _L("Objects List"), "" }, object_list_shortcuts });
    }
    else {
        Shortcuts commands_shortcuts = {
            { ctrl + "O", L("Open a G-code file") },
#ifdef __APPLE__
            { ctrl + "Shift+" + "R", L("Reload the plater from disk") },
#else
            { "F5", L("Reload plater from disk") },
#endif // __APPLE__
        };

        m_full_shortcuts.push_back({ { _L("Commands"), "" }, commands_shortcuts });
    }

    Shortcuts preview_shortcuts = {
        { L("Arrow Up"),    L("Vertical slider - Move active thumb Up") },
        { L("Arrow Down"),  L("Vertical slider - Move active thumb Down") },
        { L("Arrow Left"),  L("Horizontal slider - Move active thumb Left") },
        { L("Arrow Right"), L("Horizontal slider - Move active thumb Right") },
        { "W", L("Vertical slider - Move active thumb Up") },
        { "S", L("Vertical slider - Move active thumb Down") },
        { "A", L("Horizontal slider - Move active thumb Left") },
        { "D", L("Horizontal slider - Move active thumb Right") },
        { "X", L("On/Off one layer mode of the vertical slider") },
        { "L", L("Show/Hide Legend and Estimated printing time") },
#if ENABLE_GCODE_WINDOW
        { "C", L("Show/Hide G-code window") },
#endif // ENABLE_GCODE_WINDOW
    };

    m_full_shortcuts.push_back({ { _L("Preview"), "" }, preview_shortcuts });

    Shortcuts layers_slider_shortcuts = {
        { L("Arrow Up"),    L("Move active thumb Up") },
        { L("Arrow Down"),  L("Move active thumb Down") },
        { L("Arrow Left"),  L("Set upper thumb as active") },
        { L("Arrow Right"), L("Set lower thumb as active") },
        { "+", L("Add color change marker for current layer") },
        { "-", L("Delete color change marker for current layer") },
        { "Shift+", L("Press to speed up 5 times while moving thumb\nwith arrow keys or mouse wheel") },
        { ctrl, L("Press to speed up 5 times while moving thumb\nwith arrow keys or mouse wheel") },
    };

    m_full_shortcuts.push_back({ { _L("Vertical Slider"), _L("The following shortcuts are applicable in G-code preview when the vertical slider is active") }, layers_slider_shortcuts });

    Shortcuts sequential_slider_shortcuts = {
        { L("Arrow Left"),  L("Move active thumb Left") },
        { L("Arrow Right"), L("Move active thumb Right") },
        { L("Arrow Up"),    L("Set left thumb as active") },
        { L("Arrow Down"),  L("Set right thumb as active") },
        { "Shift+", L("Press to speed up 5 times while moving thumb\nwith arrow keys or mouse wheel") },
        { ctrl, L("Press to speed up 5 times while moving thumb\nwith arrow keys or mouse wheel") },
    };

    m_full_shortcuts.push_back({ { _L("Horizontal Slider"), _L("The following shortcuts are applicable in G-code preview when the horizontal slider is active") }, sequential_slider_shortcuts });
}

wxPanel* KBShortcutsDialog::create_header(wxWindow* parent, const wxFont& bold_font)
{
    wxPanel* panel = new wxPanel(parent);
    wxGetApp().UpdateDarkUI(panel);
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);

    wxFont header_font = bold_font;
#ifdef __WXOSX__
    header_font.SetPointSize(14);
#else
    header_font.SetPointSize(bold_font.GetPointSize() + 2);
#endif // __WXOSX__

    sizer->AddStretchSpacer();

    // logo
    m_logo_bmp = ScalableBitmap(this, wxGetApp().is_editor() ? "PrusaSlicer_32px.png" : "PrusaSlicer-gcodeviewer_32px.png", 32);
    m_header_bitmap = new wxStaticBitmap(panel, wxID_ANY, m_logo_bmp.bmp());
    sizer->Add(m_header_bitmap, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

    // text
    wxStaticText* text = new wxStaticText(panel, wxID_ANY, _L("Keyboard shortcuts"));
    text->SetFont(header_font);
    sizer->Add(text, 0, wxALIGN_CENTER_VERTICAL);

    sizer->AddStretchSpacer();

    panel->SetSizer(sizer);
    return panel;
}

wxPanel* KBShortcutsDialog::create_page(wxWindow* parent, const ShortcutsItem& shortcuts, const wxFont& font, const wxFont& bold_font)
{
    wxPanel* main_page = new wxPanel(parent);
    wxGetApp().UpdateDarkUI(main_page);
    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

    if (!shortcuts.first.second.empty()) {
        main_sizer->AddSpacer(10);
        wxBoxSizer* info_sizer = new wxBoxSizer(wxHORIZONTAL);
        info_sizer->AddStretchSpacer();
        info_sizer->Add(new wxStaticText(main_page, wxID_ANY, shortcuts.first.second), 0);
        info_sizer->AddStretchSpacer();
        main_sizer->Add(info_sizer, 0, wxEXPAND);
        main_sizer->AddSpacer(10);
    }

    static const int max_items_per_column = 20;
    int columns_count = 1 + static_cast<int>(shortcuts.second.size()) / max_items_per_column;

    wxScrolledWindow* scrollable_panel = new wxScrolledWindow(main_page);
    wxGetApp().UpdateDarkUI(scrollable_panel);
    scrollable_panel->SetScrollbars(20, 20, 50, 50);
    scrollable_panel->SetInitialSize(wxSize(850, 450));

    wxBoxSizer* scrollable_panel_sizer = new wxBoxSizer(wxVERTICAL);
    wxFlexGridSizer* grid_sizer = new wxFlexGridSizer(2 * columns_count, 5, 15);

    int items_count = (int)shortcuts.second.size();
    for (int i = 0; i < max_items_per_column; ++i) {
        for (int j = 0; j < columns_count; ++j) {
            int id = j * max_items_per_column + i;
            if (id < items_count) {
                const auto& [shortcut, description] = shortcuts.second[id];
                auto key = new wxStaticText(scrollable_panel, wxID_ANY, _(shortcut));
                key->SetFont(bold_font);
                grid_sizer->Add(key, 0, wxALIGN_CENTRE_VERTICAL);

                auto desc = new wxStaticText(scrollable_panel, wxID_ANY, _(description));
                desc->SetFont(font);
                grid_sizer->Add(desc, 0, wxALIGN_CENTRE_VERTICAL);
            }
            else {
                if (columns_count > 1) {
                    grid_sizer->Add(new wxStaticText(scrollable_panel, wxID_ANY, ""), 0, wxALIGN_CENTRE_VERTICAL);
                    grid_sizer->Add(new wxStaticText(scrollable_panel, wxID_ANY, ""), 0, wxALIGN_CENTRE_VERTICAL);
                }
            }
        }
    }

    scrollable_panel_sizer->Add(grid_sizer, 1, wxEXPAND | wxALL, 10);
    scrollable_panel->SetSizer(scrollable_panel_sizer);

    main_sizer->Add(scrollable_panel, 1, wxEXPAND);
    main_page->SetSizer(main_sizer);

    return main_page;
}

} // namespace GUI
} // namespace Slic3r
