#include "KBShortcutsDialog.hpp"
#include "I18N.hpp"
#include "libslic3r/Utils.hpp"
#include "GUI.hpp"
#include <wx/scrolwin.h>
#include <wx/display.h>
#include "GUI_App.hpp"
#include "wxExtensions.hpp"

#define NOTEBOOK_TOP 1
#define NOTEBOOK_LEFT 2
#define LISTBOOK_TOP 3
#define LISTBOOK_LEFT 4
#define TOOLBOOK 5
#define CHOICEBOOK 6
#define BOOK_TYPE NOTEBOOK_TOP

#if (BOOK_TYPE == NOTEBOOK_TOP) || (BOOK_TYPE == NOTEBOOK_LEFT)
#include <wx/notebook.h>
#elif (BOOK_TYPE == LISTBOOK_TOP) || (BOOK_TYPE == LISTBOOK_LEFT)
#include <wx/listbook.h>
#elif BOOK_TYPE == TOOLBOOK
#include <wx/toolbook.h>
#elif BOOK_TYPE == CHOICEBOOK
#include <wx/choicebk.h>
#endif // BOOK_TYPE 

namespace Slic3r {
namespace GUI {

KBShortcutsDialog::KBShortcutsDialog()
    : DPIDialog(NULL, wxID_ANY, wxString(SLIC3R_APP_NAME) + " - " + _(L("Keyboard Shortcuts")),
      wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE) // | wxRESIZE_BORDER)
{
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    // fonts
    const wxFont& font = wxGetApp().normal_font();
    const wxFont& bold_font = wxGetApp().bold_font();
    SetFont(font);

    auto main_sizer = new wxBoxSizer(wxVERTICAL);

    main_sizer->Add(create_header(this, bold_font), 0, wxEXPAND | wxALL, 10);

#if BOOK_TYPE == NOTEBOOK_TOP
    wxNotebook* book = new wxNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP);
#elif BOOK_TYPE == NOTEBOOK_LEFT
    wxNotebook* book = new wxNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_LEFT);
#elif BOOK_TYPE == LISTBOOK_TOP
    wxListbook* book = new wxListbook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLB_TOP);
#elif BOOK_TYPE == LISTBOOK_LEFT
    wxListbook* book = new wxListbook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLB_LEFT);
#elif BOOK_TYPE == TOOLBOOK
    wxToolbook* book = new wxToolbook(this, wxID_ANY);
#elif BOOK_TYPE == CHOICEBOOK
    wxChoicebook* book = new wxChoicebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxCHB_TOP);
#endif // BOOK_TYPE 
main_sizer->Add(book, 1, wxEXPAND | wxALL, 10);

    fill_shortcuts();
    for (size_t i = 0; i < m_full_shortcuts.size(); ++i)
    {
        book->AddPage(create_page(book, m_full_shortcuts[i], font, bold_font), m_full_shortcuts[i].first, i == 0);
    }

    wxStdDialogButtonSizer* buttons = this->CreateStdDialogButtonSizer(wxOK);
    this->SetEscapeId(wxID_OK);
    main_sizer->Add(buttons, 0, wxEXPAND | wxALL, 10);

    SetSizer(main_sizer);
    main_sizer->SetSizeHints(this);
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
        // Edit
        { ctrl + "A", L("Select all objects") },
        { "Esc", L("Deselect all") },
        { "Del", L("Delete selected") },
        { ctrl + "Del", L("Delete all") },
        { ctrl + "Z", L("Undo") },
        { ctrl + "Y", L("Redo") },
        { ctrl + "C", L("Copy to clipboard") },
        { ctrl + "V", L("Paste from clipboard") },
        { "F5", L("Reload plater from disk") },
        // Window
        { ctrl + "1", L("Select Plater Tab") },
        { ctrl + "2", L("Select Print Settings Tab") },
        { ctrl + "3", L("Select Filament Settings Tab") },
        { ctrl + "4", L("Select Printer Settings Tab") },
        { ctrl + "5", L("Switch to 3D") },
        { ctrl + "6", L("Switch to Preview") },
        { ctrl + "J", L("Print host upload queue") },
        // View
        { "0-6", L("Camera view") },
#if ENABLE_SHOW_SCENE_LABELS
        { "E", L("Show/Hide object/instance labels") },
#endif // ENABLE_SHOW_SCENE_LABELS
        // Configuration
        { ctrl + "P", L("Preferences") },
        // Help
        { "?", L("Show keyboard shortcuts list") }
    };

    m_full_shortcuts.push_back(std::make_pair(_(L("Commands")), commands_shortcuts));

    Shortcuts plater_shortcuts = {
        { "A", L("Arrange") },
        { "Shift+A", L("Arrange selection") },
        { "+", L("Add Instance of the selected object") },
        { "-", L("Remove Instance of the selected object") },
        { ctrl, L("Press to select multiple object\nor move multiple object with mouse") },
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
        { ctrl + "M", L("Show/Hide 3Dconnexion devices settings dialog") }
#if ENABLE_RENDER_PICKING_PASS
        // Don't localize debugging texts.
        , { "T", "Toggle picking pass texture rendering on/off" }
#endif // ENABLE_RENDER_PICKING_PASS
    };

    m_full_shortcuts.push_back(std::make_pair(_(L("Plater")), plater_shortcuts));

    Shortcuts gizmos_shortcuts = {
        { "Shift+", L("Press to to snap by 5% in Gizmo scale\nor to snap by 1mm in Gizmo move") },
        { "F", L("Scale selection to fit print volume\nin Gizmo scale") },
        { ctrl, L("Press to activate one direction scaling in Gizmo scale") },
        { alt, L("Press to scale (in Gizmo scale) or rotate (in Gizmo rotate)\nselected objects around their own center") },
    };

    m_full_shortcuts.push_back(std::make_pair(_(L("Gizmos")), gizmos_shortcuts));

    Shortcuts preview_shortcuts = {
        { L("Arrow Up"), L("Upper Layer") },
        { L("Arrow Down"), L("Lower Layer") },
        { "U", L("Upper Layer") },
        { "D", L("Lower Layer") },
        { "L", L("Show/Hide Legend") }
    };

    m_full_shortcuts.push_back(std::make_pair(_(L("Preview")), preview_shortcuts));

    Shortcuts layers_slider_shortcuts = {
        { L("Arrow Up"), L("Move current slider thumb Up") },
        { L("Arrow Down"), L("Move current slider thumb Down") },
        { L("Arrow Left"), L("Set upper thumb to current slider thumb") },
        { L("Arrow Right"), L("Set lower thumb to current slider thumb") },
        { "+", L("Add color change marker for current layer") },
        { "-", L("Delete color change marker for current layer") }
    };

    m_full_shortcuts.push_back(std::make_pair(_(L("Layers Slider")), layers_slider_shortcuts));
}

wxPanel* KBShortcutsDialog::create_header(wxWindow* parent, const wxFont& bold_font)
{
    wxPanel* panel = new wxPanel(parent);
    wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);

    wxFont header_font = bold_font;
#ifdef __WXOSX__
    header_font.SetPointSize(14);
#else
    header_font.SetPointSize(bold_font.GetPointSize() + 2);
#endif // __WXOSX__

    sizer->AddStretchSpacer();

    // logo
    m_logo_bmp = ScalableBitmap(this, "PrusaSlicer_32px.png", 32);
    m_header_bitmap = new wxStaticBitmap(panel, wxID_ANY, m_logo_bmp.bmp());
    sizer->Add(m_header_bitmap, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);

    // text
    wxStaticText* text = new wxStaticText(panel, wxID_ANY, _(L("Keyboard shortcuts")));
    text->SetFont(header_font);
    sizer->Add(text, 0, wxALIGN_CENTER_VERTICAL);

    sizer->AddStretchSpacer();

    panel->SetSizer(sizer);
    return panel;
}

wxPanel* KBShortcutsDialog::create_page(wxWindow* parent, const std::pair<wxString, Shortcuts>& shortcuts, const wxFont& font, const wxFont& bold_font)
{
    static const int max_items_per_column = 20;
    int columns_count = 1 + (int)shortcuts.second.size() / max_items_per_column;

    wxPanel* page = new wxPanel(parent);
#if (BOOK_TYPE == LISTBOOK_TOP) || (BOOK_TYPE == LISTBOOK_LEFT)
    wxStaticBoxSizer* sizer = new wxStaticBoxSizer(wxVERTICAL, page, " " + shortcuts.first + " ");
    sizer->GetStaticBox()->SetFont(bold_font);
#else
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
#endif // BOOK_TYPE

    wxFlexGridSizer* grid_sizer = new wxFlexGridSizer(2 * columns_count, 5, 15);

    int items_count = (int)shortcuts.second.size();
    for (int i = 0; i < max_items_per_column; ++i)
    {
        for (int j = 0; j < columns_count; ++j)
        {
            int id = j * max_items_per_column + i;
            if (id >= items_count)
                break;

            const auto& [shortcut, description] = shortcuts.second[id];
            auto key = new wxStaticText(page, wxID_ANY, _(shortcut));
            key->SetFont(bold_font);
            grid_sizer->Add(key, 0, wxALIGN_CENTRE_VERTICAL);

            auto desc = new wxStaticText(page, wxID_ANY, _(description));
            desc->SetFont(font);
            grid_sizer->Add(desc, 0, wxALIGN_CENTRE_VERTICAL);
        }
    }

    sizer->Add(grid_sizer, 1, wxEXPAND | wxALL, 10);
    page->SetSizer(sizer);
    return page;
}

} // namespace GUI
} // namespace Slic3r
