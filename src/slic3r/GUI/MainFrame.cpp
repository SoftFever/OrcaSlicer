#include "MainFrame.hpp"

#include <wx/panel.h>
#include <wx/notebook.h>
#include <wx/icon.h>
#include <wx/sizer.h>
#include <wx/menu.h>
#include <wx/progdlg.h>
#include <wx/tooltip.h>
#include <wx/debug.h>

#include "Tab.hpp"
#include "PresetBundle.hpp"
#include "../AppController.hpp"
#include "ProgressStatusBar.hpp"
#include "3DScene.hpp"
#include "Print.hpp"
#include "Polygon.hpp"
#include "AppConfig.hpp"

#include <fstream>
#include "GUI_App.hpp"

namespace Slic3r {
namespace GUI {

MainFrame::MainFrame(const bool no_plater, const bool loaded) :
wxFrame(NULL, wxID_ANY, SLIC3R_BUILD, wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE),
        m_no_plater(no_plater),
        m_loaded(loaded)
{
    m_appController = new Slic3r::AppController();

    // Load the icon either from the exe, or from the ico file.
#if _WIN32
    {
        TCHAR szExeFileName[MAX_PATH];
        GetModuleFileName(nullptr, szExeFileName, MAX_PATH);
        SetIcon(wxIcon(szExeFileName, wxBITMAP_TYPE_ICO));
    }
#else
    SetIcon(wxIcon(Slic3r::var("Slic3r_128px.png"), wxBITMAP_TYPE_PNG));
#endif // _WIN32

    // initialize tabpanel and menubar
    init_tabpanel();
    init_menubar();

    // set default tooltip timer in msec
    // SetAutoPop supposedly accepts long integers but some bug doesn't allow for larger values
    // (SetAutoPop is not available on GTK.)
    wxToolTip::SetAutoPop(32767);

    // initialize status bar
    m_statusbar = new ProgressStatusBar(this);
    m_statusbar->embed(this);
    m_statusbar->set_status_text(_(L("Version ")) +
                                 SLIC3R_VERSION +
                                 _(L(" - Remember to check for updates at http://github.com/prusa3d/slic3r/releases")));

	//     m_appController->set_model(m_plater->model);
	//     m_appController->set_print(m_plater->print);
	//     m_plater->appController = m_appController;
	GUI::set_gui_appctl();

	// Make the global status bar and its progress indicator available in C++
    m_appController->set_global_progress_indicator(m_statusbar);

    m_loaded = true;

    // initialize layout
    auto sizer = new wxBoxSizer(wxVERTICAL);
    if (m_tabpanel)
        sizer->Add(m_tabpanel, 1, wxEXPAND);
    sizer->SetSizeHints(this);
    SetSizer(sizer);
    Fit();
    SetMinSize(wxSize(760, 490));
    SetSize(GetMinSize());
    wxGetApp().restore_window_pos(this, "main_frame");
    Show();
    Layout();

    // declare events
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event){
        if (event.CanVeto() && !wxGetApp().check_unsaved_changes()) {
            event.Veto();
            return;
        }
        // save window size
        wxGetApp().save_window_pos(this, "main_frame");
        // Save the slic3r.ini.Usually the ini file is saved from "on idle" callback,
        // but in rare cases it may not have been called yet.
        wxGetApp().app_config->save();
//         if (m_plater)
//             m_plater->print = undef;
        _3DScene::remove_all_canvases();
//         Slic3r::GUI::deregister_on_request_update_callback();
        // propagate event
        event.Skip();
    });

    update_ui_from_settings();
    return;
}

void MainFrame::init_tabpanel()
{
    m_tabpanel = new wxNotebook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP | wxTAB_TRAVERSAL);

    m_tabpanel->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, [this](wxEvent&){
        auto panel = m_tabpanel->GetCurrentPage();
//             panel->OnActivate(); if panel->can('OnActivate');

        if (panel == nullptr)
            return;

        for (auto& tab_name : { "print", "filament", "printer" }) {
            if (tab_name == panel->GetName()) {
                // On GTK, the wxEVT_NOTEBOOK_PAGE_CHANGED event is triggered
                // before the MainFrame is fully set up.
                auto it = m_options_tabs.find(tab_name);
                assert(it != m_options_tabs.end());
                if (it != m_options_tabs.end())
                    it->second->OnActivate();
            }
        }
    });

    if (!m_no_plater) {
        m_plater = new Slic3r::GUI::Plater(m_tabpanel, this);
        m_tabpanel->AddPage(m_plater, _(L("Plater")));
    }

    // The following event is emited by Tab implementation on config value change.
    Bind(EVT_TAB_VALUE_CHANGED, &MainFrame::on_value_changed, this);
//     EVT_COMMAND($self, -1, $VALUE_CHANGE_EVENT, sub {
//         my($self, $event) = @_;
//         auto str = event->GetString;
//         my($opt_key, $name) = ($str = ~/ (.*) (.*) / );
//         auto tab = Slic3r::GUI::get_preset_tab(name);
//         auto config = tab->get_config();
//         if (m_plater) {
//             m_plater->on_config_change(config); // propagate config change events to the plater
//                 if (opt_key == "extruders_count"){
//                     auto value = event->GetInt();
//                     m_plater->on_extruders_change(value);
//                 }
//         }
//         // don't save while loading for the first time
//         if (Slic3r::GUI::autosave && m_loaded)
//             m_config->save(Slic3r::GUI::autosave) ;
//     });

    // The following event is emited by Tab on preset selection,
    // or when the preset's "modified" status changes.
    Bind(EVT_TAB_PRESETS_CHANGED, &MainFrame::on_presets_changed, this);


    // The following event is emited by the C++ Tab implementation on object selection change.
//     EVT_COMMAND($self, -1, $OBJECT_SELECTION_CHANGED_EVENT, sub {
//         auto obj_idx = event->GetId();
//         // my $child = $event->GetInt == 1 ? 1 : undef;
//         // $self->{plater}->select_object($obj_idx < 0 ? undef : $obj_idx, $child);
//         // $self->{plater}->item_changed_selection($obj_idx);
// 
//         auto vol_idx = event->GetInt();
//         m_plater->select_object_from_cpp(obj_idx < 0 ? undef : obj_idx, vol_idx < 0 ? -1 : vol_idx);
//     });

    create_preset_tabs();
    std::vector<std::string> tab_names = { "print", "filament", "sla_material", "printer" };    
    for (auto tab_name : tab_names)
        m_options_tabs[tab_name] = get_preset_tab(tab_name.c_str()); 

    if (m_plater) {
//         m_plater->on_select_preset(sub{
//             my($group, $name) = @_;
//             $self->{options_tabs}{$group}->select_preset($name);
//         });
        // load initial config
        auto full_config = wxGetApp().preset_bundle->full_config();
//         m_plater->on_config_change(full_config);

        // Show a correct number of filament fields.
//         if (defined full_config->nozzle_diameter){
//             // nozzle_diameter is undefined when SLA printer is selected
//             m_plater->on_extruders_change(int(@{$full_config->nozzle_diameter}));
//         }

        // Show correct preset comboboxes according to the printer_technology
//         m_plater->show_preset_comboboxes(full_config.printer_technology() == "FFF" ? 0 : 1);
    }
}

std::vector<PresetTab> preset_tabs = {
    { "print", nullptr, ptFFF },
    { "filament", nullptr, ptFFF },
    { "sla_material", nullptr, ptSLA }
};

std::vector<PresetTab>& MainFrame::get_preset_tabs() {
    return preset_tabs;
}

Tab* MainFrame::get_tab(const std::string& name)
{
    std::vector<PresetTab>::iterator it = std::find_if(preset_tabs.begin(), preset_tabs.end(),
        [name](PresetTab& tab){ return name == tab.name; });
    return it != preset_tabs.end() ? it->panel : nullptr;
}

Tab* MainFrame::get_preset_tab(const std::string& name)
{
    Tab* tab = get_tab(name);
    if (tab) return tab;

    for (size_t i = 0; i < m_tabpanel->GetPageCount(); ++i) {
        tab = dynamic_cast<Tab*>(m_tabpanel->GetPage(i));
        if (!tab)
            continue;
        if (tab->name() == name) {
            return tab;
        }
    }
    return nullptr;
}

void MainFrame::create_preset_tabs()
{
    wxGetApp().update_label_colours_from_appconfig();
    add_created_tab(new TabPrint(m_tabpanel));
    add_created_tab(new TabFilament(m_tabpanel));
    add_created_tab(new TabSLAMaterial(m_tabpanel));
    add_created_tab(new TabPrinter(m_tabpanel));
}

void MainFrame::add_created_tab(Tab* panel)
{
    panel->create_preset_tab();

    // Load the currently selected preset into the GUI, update the preset selection box.
    panel->load_current_preset();

    const wxString& tab_name = panel->GetName();
    bool add_panel = true;

    auto it = std::find_if(preset_tabs.begin(), preset_tabs.end(),
        [tab_name](PresetTab& tab){return tab.name == tab_name; });
    if (it != preset_tabs.end()) {
        it->panel = panel;
        add_panel = it->technology == wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology();
    }

    if (add_panel)
        m_tabpanel->AddPage(panel, panel->title());
}

void MainFrame::init_menubar()
{
    // File menu
    wxMenu* fileMenu = new wxMenu;
    {
        wxGetApp().append_menu_item(fileMenu, wxID_ANY, _(L("Open STL/OBJ/AMF/3MF…\tCtrl+O")), _(L("Open a model")), 
            "", [](wxCommandEvent&){
//             if (m_plater) m_plater->add();
        }); //'brick_add.png');
        append_menu_item(fileMenu, wxID_ANY, _(L("&Load Config…\tCtrl+L")), _(L("Load exported configuration file")), 
                        [this](wxCommandEvent&){ load_config_file(); }, "plugin_add.png");
        append_menu_item(fileMenu, wxID_ANY, _(L("&Export Config…\tCtrl+E")), _(L("Export current configuration to file")), 
                        [this](wxCommandEvent&){ export_config(); }, "plugin_go.png");
        append_menu_item(fileMenu, wxID_ANY, _(L("&Load Config Bundle…")), _(L("Load presets from a bundle")), 
                        [this](wxCommandEvent&){ load_configbundle(); }, "lorry_add.png");
        append_menu_item(fileMenu, wxID_ANY, _(L("&Export Config Bundle…")), _(L("Export all presets to file")), 
                        [this](wxCommandEvent&){ export_configbundle(); }, "lorry_go.png");
        fileMenu->AppendSeparator();
        wxMenuItem* repeat = nullptr;
        append_menu_item(fileMenu, wxID_ANY, _(L("Q&uick Slice…\tCtrl+U")), _(L("Slice a file into a G-code")), 
            [this, repeat](wxCommandEvent&){
                wxTheApp->CallAfter([this, repeat](){
                    quick_slice();
                    repeat->Enable(is_last_input_file());
            }); }, "cog_go.png");
        append_menu_item(fileMenu, wxID_ANY, _(L("Quick Slice and Save &As…\tCtrl+Alt+U")), _(L("Slice a file into a G-code, save as")), 
            [this, repeat](wxCommandEvent&){
                wxTheApp->CallAfter([this, repeat](){
                    quick_slice(qsSaveAs);
                    repeat->Enable(is_last_input_file());
            }); }, "cog_go.png");
        repeat = append_menu_item(fileMenu, wxID_ANY, _(L("&Repeat Last Quick Slice\tCtrl+Shift+U")), _(L("Repeat last quick slice")), 
            [this](wxCommandEvent&){
            wxTheApp->CallAfter([this](){
                quick_slice(qsReslice);
            }); }, "cog_go.png");
        repeat->Enable(0);
        fileMenu->AppendSeparator();
        append_menu_item(fileMenu, wxID_ANY, _(L("Slice to SV&G…\tCtrl+G")), _(L("Slice file to a multi-layer SVG")),
            [this](wxCommandEvent&){ quick_slice(qsSaveAs | qsExportSVG); }, "shape_handles.png");
        append_menu_item(fileMenu, wxID_ANY, _(L("Slice to PNG…")), _(L("Slice file to a set of PNG files")),
            [this](wxCommandEvent&){ slice_to_png(); /*$self->quick_slice(save_as = > 0, export_png = > 1);*/ }, "shape_handles.png");
        m_menu_item_reslice_now = append_menu_item(fileMenu, wxID_ANY, _(L("(&Re)Slice Now\tCtrl+S")), _(L("Start new slicing process")),
            [this](wxCommandEvent&){ reslice_now(); }, "shape_handles.png");
        fileMenu->AppendSeparator();
        append_menu_item(fileMenu, wxID_ANY, _(L("Repair STL file…")), _(L("Automatically repair an STL file")),
            [this](wxCommandEvent&){ repair_stl(); }, "wrench.png");
        fileMenu->AppendSeparator();
        append_menu_item(fileMenu, wxID_EXIT, _(L("&Quit")), _(L("Quit Slic3r")),
            [this](wxCommandEvent&){ Close(false); } );
    }

    // Plater menu
    if(m_plater) {
        auto plater_menu = new wxMenu();
        append_menu_item(plater_menu, wxID_ANY, L("Export G-code..."), L("Export current plate as G-code"), 
            [this](wxCommandEvent&){ /*m_plater->export_gcode(); */}, "cog_go.png");
        append_menu_item(plater_menu, wxID_ANY, L("Export plate as STL..."), L("Export current plate as STL"), 
            [this](wxCommandEvent&){ /*m_plater->export_stl(); */}, "brick_go.png");
        append_menu_item(plater_menu, wxID_ANY, L("Export plate as AMF..."), L("Export current plate as AMF"), 
            [this](wxCommandEvent&){ /*m_plater->export_amf();*/ }, "brick_go.png");
        append_menu_item(plater_menu, wxID_ANY, L("Export plate as 3MF..."), L("Export current plate as 3MF"), 
            [this](wxCommandEvent&){ /*m_plater->export_3mf(); */}, "brick_go.png");

//         m_object_menu = m_plater->object_menu;
        on_plater_selection_changed(false);
    }

    // Window menu
    auto windowMenu = new wxMenu();
    {
        size_t tab_offset = 0;
        if (m_plater) {
            append_menu_item(windowMenu, wxID_ANY, L("Select &Plater Tab\tCtrl+1"), L("Show the plater"), 
                [this](wxCommandEvent&){ select_tab(0); }, "application_view_tile.png");
            tab_offset += 1;
        }
        if (tab_offset > 0) {
            windowMenu->AppendSeparator();
        }
        append_menu_item(windowMenu, wxID_ANY, L("Select P&rint Settings Tab\tCtrl+2"), L("Show the print settings"), 
            [this, tab_offset](wxCommandEvent&){ select_tab(tab_offset + 0); }, "cog.png");
        append_menu_item(windowMenu, wxID_ANY, L("Select &Filament Settings Tab\tCtrl+3"), L("Show the filament settings"), 
            [this, tab_offset](wxCommandEvent&){ select_tab(tab_offset + 1); }, "spool.png");
        append_menu_item(windowMenu, wxID_ANY, L("Select Print&er Settings Tab\tCtrl+4"), L("Show the printer settings"), 
            [this, tab_offset](wxCommandEvent&){ select_tab(tab_offset + 2); }, "printer_empty.png");
    }

    // View menu
    if (m_plater) {
        m_viewMenu = new wxMenu();
// \xA0 is a non-breaing space. It is entered here to spoil the automatic accelerators,
        // as the simple numeric accelerators spoil all numeric data entry.
        // The camera control accelerators are captured by 3DScene Perl module instead.
        auto accel = [](const wxString& st1, const wxString& st2) {
//             if ($^O eq "MSWin32")
//                 return st1 + "\t\xA0" + st2;
//             else
                return st1; 
        };

        append_menu_item(m_viewMenu, wxID_ANY, accel(_(L("Iso")), "0"),     L("Iso View"),   [this](wxCommandEvent&){ select_view("iso"); });
        append_menu_item(m_viewMenu, wxID_ANY, accel(_(L("Top")), "1"),     L("Top View"),   [this](wxCommandEvent&){ select_view("top"); });
        append_menu_item(m_viewMenu, wxID_ANY, accel(_(L("Bottom")), "2"),  L("Bottom View"),[this](wxCommandEvent&){ select_view("bottom"); });
        append_menu_item(m_viewMenu, wxID_ANY, accel(_(L("Front")), "3"),   L("Front View"), [this](wxCommandEvent&){ select_view("front"); });
        append_menu_item(m_viewMenu, wxID_ANY, accel(_(L("Rear")), "4"),    L("Rear View"),  [this](wxCommandEvent&){ select_view("rear"); });
        append_menu_item(m_viewMenu, wxID_ANY, accel(_(L("Left")), "5"),    L("Left View"),  [this](wxCommandEvent&){ select_view("left"); });
        append_menu_item(m_viewMenu, wxID_ANY, accel(_(L("Right")), "6"),   L("Right View"), [this](wxCommandEvent&){ select_view("right"); });
    }

    // Help menu
    auto helpMenu = new wxMenu();
    {
        append_menu_item(helpMenu, wxID_ANY, _(L("Prusa 3D Drivers")), _(L("Open the Prusa3D drivers download page in your browser")), 
            [this](wxCommandEvent&){ wxLaunchDefaultBrowser("http://www.prusa3d.com/drivers/"); });
        append_menu_item(helpMenu, wxID_ANY, _(L("Prusa Edition Releases")), _(L("Open the Prusa Edition releases page in your browser")), 
            [this](wxCommandEvent&){ wxLaunchDefaultBrowser("http://github.com/prusa3d/slic3r/releases"); });
//#        my $versioncheck = $self->_append_menu_item($helpMenu, "Check for &Updates...", "Check for new Slic3r versions", sub{
//#            wxTheApp->check_version(1);
//#        });
//#        $versioncheck->Enable(wxTheApp->have_version_check);
        append_menu_item(helpMenu, wxID_ANY, _(L("Slic3r &Website")), _(L("Open the Slic3r website in your browser")), 
            [this](wxCommandEvent&){ wxLaunchDefaultBrowser("http://slic3r.org/"); });
        append_menu_item(helpMenu, wxID_ANY, _(L("Slic3r &Manual")), _(L("Open the Slic3r manual in your browser")), 
            [this](wxCommandEvent&){ wxLaunchDefaultBrowser("http://manual.slic3r.org/"); });
        helpMenu->AppendSeparator();
        append_menu_item(helpMenu, wxID_ANY, _(L("System Info")), _(L("Show system information")), 
            [this](wxCommandEvent&){ wxGetApp().system_info(); });
        append_menu_item(helpMenu, wxID_ANY, _(L("Show &Configuration Folder")), _(L("Show user configuration folder (datadir)")), 
            [this](wxCommandEvent&){ Slic3r::GUI::desktop_open_datadir_folder(); });
        append_menu_item(helpMenu, wxID_ANY, _(L("Report an Issue")), _(L("Report an issue on the Slic3r Prusa Edition")), 
            [this](wxCommandEvent&){ wxLaunchDefaultBrowser("http://github.com/prusa3d/slic3r/issues/new"); });
        append_menu_item(helpMenu, wxID_ANY, _(L("&About Slic3r")), _(L("Show about dialog")), 
            [this](wxCommandEvent&){ Slic3r::GUI::about(); });
    }

    // menubar
    // assign menubar to frame after appending items, otherwise special items
    // will not be handled correctly
    {
        auto menubar = new wxMenuBar();
        menubar->Append(fileMenu, L("&File"));
        if (m_plater_menu) menubar->Append(m_plater_menu, L("&Plater")) ;
        if (m_object_menu) menubar->Append(m_object_menu, L("&Object")) ;
        menubar->Append(windowMenu, L("&Window"));
        if (m_viewMenu) menubar->Append(m_viewMenu, L("&View"));
        // Add additional menus from C++
        wxGetApp().add_config_menu(menubar);
        menubar->Append(helpMenu, L("&Help"));
        SetMenuBar(menubar);
    }
}

// Selection of a 3D object changed on the platter.
void MainFrame::on_plater_selection_changed(const bool have_selection)
{
    if (!m_object_menu) return;
    
    for (auto item : m_object_menu->GetMenuItems())
        m_object_menu->Enable(item->GetId(), have_selection);
}

void MainFrame::slice_to_png(){
//     m_plater->stop_background_process();
//     m_plater->async_apply_config();
    m_appController->print_ctl()->slice_to_png();
}

// To perform the "Quck Slice", "Quick Slice and Save As", "Repeat last Quick Slice" and "Slice to SVG".
void MainFrame::quick_slice(const int qs){
//     my $progress_dialog;
    wxString input_file;
//  eval
//     {
    // validate configuration
    auto config = wxGetApp().preset_bundle->full_config();
    config.validate();

    // select input file
    if (!(qs & qsReslice)) {
        auto dlg = new wxFileDialog(this, _(L("Choose a file to slice (STL/OBJ/AMF/3MF/PRUSA):")),
            wxGetApp().app_config->get_last_dir(), "",
            file_wildcards[FT_MODEL], wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg->ShowModal() != wxID_OK) {
            dlg->Destroy();
            return;
        }
        input_file = dlg->GetPath();
        dlg->Destroy();
        if (!(qs & qsExportSVG))
            m_qs_last_input_file = input_file;
    }
    else {
        if (m_qs_last_input_file.IsEmpty()) {
            auto dlg = new wxMessageDialog(this, _(L("No previously sliced file.")),
                _(L("Error")), wxICON_ERROR | wxOK);
            dlg->ShowModal();
            return;
        }
        if (std::ifstream(m_qs_last_input_file.char_str())) {
            auto dlg = new wxMessageDialog(this, _(L("Previously sliced file ("))+m_qs_last_input_file+_(L(") not found.")),
                _(L("File Not Found")), wxICON_ERROR | wxOK);
            dlg->ShowModal();
            return;
        }
        input_file = m_qs_last_input_file;
    }
    auto input_file_basename = get_base_name(input_file);
    wxGetApp().app_config->update_skein_dir(get_dir_name(input_file));

    auto bed_shape = Slic3r::Polygon::new_scale(config.option<ConfigOptionPoints>("bed_shape")->values);
//     auto print_center = Slic3r::Pointf->new_unscale(bed_shape.bounding_box().center());
// 
//     auto sprint = new Slic3r::Print::Simple(
//         print_center = > print_center,
//         status_cb = > [](int percent, const wxString& msg){
//         m_progress_dialog->Update(percent, msg+"…");
//     });

    // keep model around
    auto model = Slic3r::Model::read_from_file(input_file.ToStdString());

//     sprint->apply_config(config);
//     sprint->set_model(model);

    // Copy the names of active presets into the placeholder parser.
//     wxGetApp().preset_bundle->export_selections(sprint->placeholder_parser);

    // select output file
    wxString output_file;
    if (qs & qsReslice) {
        if (!m_qs_last_output_file.IsEmpty())
            output_file = m_qs_last_output_file;
    } 
    else if (qs & qsSaveAs) {
        // The following line may die if the output_filename_format template substitution fails.
//         output_file = sprint->output_filepath;
//         if (export_svg)
//         output_file = ~s / \.[gG][cC][oO][dD][eE]$ / .svg /;
        auto dlg = new wxFileDialog(this, _(L("Save ")) + (qs & qsExportSVG ? _(L("SVG")) : _(L("G-code"))) + _(L(" file as:")),
            wxGetApp().app_config->get_last_output_dir(get_dir_name(output_file)), get_base_name(input_file), 
            qs & qsExportSVG ? file_wildcards[FT_SVG] : file_wildcards[FT_GCODE],
            wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dlg->ShowModal() != wxID_OK) {
            dlg->Destroy();
            return;
        }
        output_file = dlg->GetPath();
        dlg->Destroy();
        if (!(qs & qsExportSVG))
            m_qs_last_output_file = output_file;
        wxGetApp().app_config->update_last_output_dir(get_dir_name(output_file));
    } 
    else if (qs & qsExportPNG) {
//         output_file = sprint->output_filepath;
//         output_file = ~s / \.[gG][cC][oO][dD][eE]$ / .zip / ;
        auto dlg = new wxFileDialog(this, _(L("Save zip file as:")),
            wxGetApp().app_config->get_last_output_dir(get_dir_name(output_file)),
            get_base_name(output_file), "*.zip", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dlg->ShowModal() != wxID_OK) {
            dlg->Destroy();
            return;
        }
        output_file = dlg->GetPath();
        dlg->Destroy();
    }

    // show processbar dialog
    m_progress_dialog = new wxProgressDialog(_(L("Slicing…")), _(L("Processing ")) + input_file_basename + "…",
        100, this, 4);
    m_progress_dialog->Pulse();
    {
//         my @warnings = ();
//         local $SIG{ __WARN__ } = sub{ push @warnings, $_[0] };

//         sprint->output_file(output_file);
//         if (export_svg) {
//             sprint->export_svg();
//         }
//         else if(export_png) {
//             sprint->export_png();
//         }
//         else {
//             sprint->export_gcode();
//         }
//         sprint->status_cb(undef);
//         Slic3r::GUI::warning_catcher($self)->($_) for @warnings;
    }
    m_progress_dialog->Destroy();
    m_progress_dialog = nullptr;

    auto message = input_file_basename + _(L(" was successfully sliced."));
//     wxTheApp->notify(message);
    wxMessageDialog(this, message, _(L("Slicing Done!")), wxOK | wxICON_INFORMATION).ShowModal();
//     };
//     Slic3r::GUI::catch_error(this, [](){ if (m_progress_dialog) m_progress_dialog->Destroy(); });
}

void MainFrame::reslice_now(){
//     if (m_plater)
//         m_plater->reslice();
}

void MainFrame::repair_stl()
{
    wxString input_file;
    {
        auto dlg = new wxFileDialog(this, _(L("Select the STL file to repair:")),
            wxGetApp().app_config->get_last_dir(), "",
            file_wildcards[FT_STL], wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg->ShowModal() != wxID_OK) {
            dlg->Destroy();
            return;
        }
        input_file = dlg->GetPath();
        dlg->Destroy();
    }

    auto output_file = input_file;
    {
//         output_file = ~s / \.[sS][tT][lL]$ / _fixed.obj / ;
        auto dlg = new wxFileDialog( this, L("Save OBJ file (less prone to coordinate errors than STL) as:"), 
                                        get_dir_name(output_file), get_base_name(output_file), 
                                        file_wildcards[FT_OBJ], wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
        if (dlg->ShowModal() != wxID_OK) {
            dlg->Destroy();
            return /*undef*/;
        }
        output_file = dlg->GetPath();
        dlg->Destroy();
    }

    auto tmesh = new Slic3r::TriangleMesh();
    tmesh->ReadSTLFile(input_file.char_str());
    tmesh->repair();
    tmesh->WriteOBJFile(output_file.char_str());
    Slic3r::GUI::show_info(this, L("Your file was repaired."), L("Repair"));
}

void MainFrame::export_config()
{
    // Generate a cummulative configuration for the selected print, filaments and printer.
    auto config = wxGetApp().preset_bundle->full_config();
    // Validate the cummulative configuration.
    auto valid = config.validate();
    if (!valid.empty()) {
//         Slic3r::GUI::catch_error(this);
        return;
    }
    // Ask user for the file name for the config file.
    auto dlg = new wxFileDialog(this, _(L("Save configuration as:")),
        !m_last_config.IsEmpty() ? get_dir_name(m_last_config) : wxGetApp().app_config->get_last_dir(),
        !m_last_config.IsEmpty() ? get_base_name(m_last_config) : "config.ini",
        file_wildcards[FT_INI], wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    wxString file;
    if (dlg->ShowModal() == wxID_OK)
        file = dlg->GetPath();
    dlg->Destroy();
    if (!file.IsEmpty()) {
        wxGetApp().app_config->update_config_dir(get_dir_name(file));
        m_last_config = file;
        config.save(file.ToStdString());
    }
}

// Load a config file containing a Print, Filament & Printer preset.
void MainFrame::load_config_file(wxString file/* = wxEmptyString*/)
{
    if (file.IsEmpty()) {
        if (!wxGetApp().check_unsaved_changes())
            return;
        auto dlg = new wxFileDialog(this, _(L("Select configuration to load:")),
            !m_last_config.IsEmpty() ? get_dir_name(m_last_config) : wxGetApp().app_config->get_last_dir(),
            "config.ini", "INI files (*.ini, *.gcode)|*.ini;*.INI;*.gcode;*.g", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg->ShowModal() != wxID_OK)
            return;
        file = dlg->GetPath();
        dlg->Destroy();
    }
//     eval{ 
        wxGetApp().preset_bundle->load_config_file(file.ToStdString()); 
//     };
    // Dont proceed further if the config file cannot be loaded.
//     if (Slic3r::GUI::catch_error(this))
//         return;
    for (auto tab : m_options_tabs )
        tab.second->load_current_preset();
    wxGetApp().app_config->update_config_dir(get_dir_name(file));
    m_last_config = file;
}

void MainFrame::export_configbundle()
{
    if (!wxGetApp().check_unsaved_changes())
        return;
    // validate current configuration in case it's dirty
    auto valid = wxGetApp().preset_bundle->full_config().validate();
    if (!valid.empty()) {
//         Slic3r::GUI::catch_error(this);
        return;
    }
    // Ask user for a file name.
    auto dlg = new wxFileDialog(this, _(L("Save presets bundle as:")),
        !m_last_config.IsEmpty() ? get_dir_name(m_last_config) : wxGetApp().app_config->get_last_dir(),
        "Slic3r_config_bundle.ini",
        file_wildcards[FT_INI], wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    wxString file;
    if (dlg->ShowModal() == wxID_OK)
        file = dlg->GetPath();
    dlg->Destroy();
    if (!file.IsEmpty()) {
        // Export the config bundle.
       wxGetApp().app_config->update_config_dir(get_dir_name(file));
//         eval{ 
            wxGetApp().preset_bundle->export_configbundle(file.ToStdString()); 
//         };
//         Slic3r::GUI::catch_error(this);
    }
}

// Loading a config bundle with an external file name used to be used
// to auto - install a config bundle on a fresh user account,
// but that behavior was not documented and likely buggy.
void MainFrame::load_configbundle(wxString file/* = wxEmptyString, const bool reset_user_profile*/){
    if (!wxGetApp().check_unsaved_changes())
        return;
    if (file.IsEmpty()) {
        auto dlg = new wxFileDialog(this, _(L("Select configuration to load:")),
            !m_last_config.IsEmpty() ? get_dir_name(m_last_config) : wxGetApp().app_config->get_last_dir(),
            "config.ini", file_wildcards[FT_INI], wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dlg->ShowModal() != wxID_OK)
            return;
        file = dlg->GetPath();
        dlg->Destroy();
    }

    wxGetApp().app_config->update_config_dir(get_dir_name(file));

    auto presets_imported = 0;
//     eval{ 
    presets_imported = wxGetApp().preset_bundle->load_configbundle(file.ToStdString());
//     };
//     Slic3r::GUI::catch_error(this) and return;

    // Load the currently selected preset into the GUI, update the preset selection box.
    for (auto tab : m_options_tabs)
        tab.second->load_current_preset();

    const auto message = wxString::Format(_(L("%d presets successfully imported.")), presets_imported);
    Slic3r::GUI::show_info(this, message, "Info");
}

// Load a provied DynamicConfig into the Print / Filament / Printer tabs, thus modifying the active preset.
// Also update the platter with the new presets.
void MainFrame::load_config(const DynamicPrintConfig& config){
    for (auto tab : m_options_tabs)
        tab.second->load_config(config);
//     if (m_plater) m_plater->on_config_change(config);
}

void MainFrame::select_tab(size_t tab) const{
    m_tabpanel->SetSelection(tab);
}

// Set a camera direction, zoom to all objects.
void MainFrame::select_view(const std::string& direction){
//     if (m_plater)
//         m_plater->select_view(direction);
}

wxMenuItem* MainFrame::append_menu_item(wxMenu* menu, 
                                        int id, 
                                        const wxString& string,
                                        const wxString& description,
                                        std::function<void(wxCommandEvent& event)> cb, 
                                        const std::string& icon /*= ""*/)
{
    if (id == wxID_ANY)
        id = wxNewId();
    auto item = menu->Append(id, string, description);
    if (!icon.empty())
        item->SetBitmap(wxBitmap(Slic3r::var(icon), wxBITMAP_TYPE_PNG));
    menu->Bind(wxEVT_MENU, /*[cb](wxCommandEvent& event){cb; }*/cb);
    return item;
}

void MainFrame::on_presets_changed(SimpleEvent &event)
{
    auto *tab = dynamic_cast<Tab*>(event.GetEventObject());
    wxASSERT(tab != nullptr);
    if (tab == nullptr) {
        return;
    }

    // Update preset combo boxes(Print settings, Filament, Material, Printer) from their respective tabs.
    auto presets = tab->get_presets();
    if (m_plater != nullptr && presets != nullptr) {
        auto reload_dependent_tabs = tab->get_dependent_tabs();

        // FIXME: The preset type really should be a property of Tab instead
        Slic3r::Preset::Type preset_type = tab->type();
        if (preset_type == Slic3r::Preset::TYPE_INVALID){
            wxASSERT(false);
            return;
        }

        m_plater->sidebar().update_presets(preset_type);

        if (preset_type == Slic3r::Preset::TYPE_PRINTER) {
            // Printer selected at the Printer tab, update "compatible" marks at the print and filament selectors.
            // XXX: Do this in a more C++ way
            std::vector<std::string> tab_names_other = { "print", "filament", "sla_material" };
            for (const auto tab_name_other : tab_names_other) {
                Tab* cur_tab = m_options_tabs[tab_name_other];
                // If the printer tells us that the print or filament preset has been switched or invalidated,
                // refresh the print or filament tab page.Otherwise just refresh the combo box.
                if (reload_dependent_tabs.empty() ||
                    find(reload_dependent_tabs.begin(), reload_dependent_tabs.end(), tab_name_other) ==
                    reload_dependent_tabs.end() )
                    cur_tab->update_tab_ui();
                else
                    cur_tab->load_current_preset();
            }
            m_plater->sidebar().show_preset_comboboxes(static_cast<TabPrinter*>(tab)->m_printer_technology == ptSLA);
        }
        // XXX: ?
        // m_plater->on_config_change(tab->get_config());
    }
}

void MainFrame::on_value_changed(wxCommandEvent&)
{
    ;
}

// Called after the Preferences dialog is closed and the program settings are saved.
// Update the UI based on the current preferences.
void MainFrame::update_ui_from_settings()
{
    m_menu_item_reslice_now->Enable(wxGetApp().app_config->get("background_processing") == "1");
//     if (m_plater) m_plater->update_ui_from_settings();
    std::vector<std::string> tab_names = { "print", "filament", "printer" };
    for (auto tab_name: tab_names)
        m_options_tabs[tab_name]->update_ui_from_settings();
}


std::string MainFrame::get_base_name(const wxString full_name) const 
{
    return boost::filesystem::path(full_name).filename().string();
}

std::string MainFrame::get_dir_name(const wxString full_name) const 
{
    return boost::filesystem::path(full_name).parent_path().string();
}


} // GUI
} // Slic3r
