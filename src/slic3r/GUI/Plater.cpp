#include "Plater.hpp"
#include <cstddef>
#include <algorithm>
#include <numeric>
#include <vector>
#include <string>
#include <regex>
#include <future>
#include <boost/algorithm/string.hpp>
#include <boost/optional.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/bmpcbox.h>
#include <wx/statbox.h>
#include <wx/statbmp.h>
#include <wx/filedlg.h>
#include <wx/dnd.h>
#include <wx/progdlg.h>
#include <wx/wupdlock.h>
#include <wx/numdlg.h>
#include <wx/debug.h>
#include <wx/busyinfo.h>
#include <wx/event.h>
#include <wx/wrapsizer.h>
#ifdef _WIN32
#include <wx/richtooltip.h>
#include <wx/custombgwin.h>
#include <wx/popupwin.h>
#endif
#include <wx/clrpicker.h>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Format/STEP.hpp"
#include "libslic3r/Format/AMF.hpp"
//#include "libslic3r/Format/3mf.hpp"
#include "libslic3r/Format/bbs_3mf.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/SLA/Hollowing.hpp"
#include "libslic3r/SLA/SupportPoint.hpp"
#include "libslic3r/SLA/ReprojectPointsOnMesh.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/ClipperUtils.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "GUI_Utils.hpp"
#include "GUI_Factories.hpp"
#include "wxExtensions.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "3DScene.hpp"
#include "GLCanvas3D.hpp"
#include "Selection.hpp"
#include "GLToolbar.hpp"
#include "GUI_Preview.hpp"
#include "3DBed.hpp"
#include "PartPlate.hpp"
#include "Camera.hpp"
#include "Mouse3DController.hpp"
#include "Tab.hpp"
#include "Jobs/OrientJob.hpp"
#include "Jobs/ArrangeJob.hpp"
#include "Jobs/FillBedJob.hpp"
#include "Jobs/RotoptimizeJob.hpp"
#include "Jobs/SLAImportJob.hpp"
#include "Jobs/PrintJob.hpp"
#include "Jobs/NotificationProgressIndicator.hpp"
#include "BackgroundSlicingProcess.hpp"
#include "SelectMachine.hpp"
#include "SendToPrinter.hpp"
#include "PublishDialog.hpp"
#include "ModelMall.hpp"
#include "ConfigWizard.hpp"
#include "../Utils/ASCIIFolding.hpp"
#include "../Utils/FixModelByWin10.hpp"
#include "../Utils/UndoRedo.hpp"
#include "../Utils/PresetUpdater.hpp"
#include "../Utils/Process.hpp"
#include "RemovableDriveManager.hpp"
#include "InstanceCheck.hpp"
#include "NotificationManager.hpp"
#include "PresetComboBoxes.hpp"
#include "MsgDialog.hpp"
#include "ProjectDirtyStateManager.hpp"
#include "Gizmos/GLGizmoSimplify.hpp" // create suggestion notification

// BBS
#include "Widgets/ProgressDialog.hpp"
#include "BBLStatusBar.hpp"
#include "BitmapCache.hpp"
#include "ParamsDialog.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Widgets/RadioBox.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/Button.hpp"

#include "GUI_ObjectTable.hpp"
#include "libslic3r/Thread.hpp"

#ifdef __APPLE__
#include "Gizmos/GLGizmosManager.hpp"
#endif // __APPLE__

#include <wx/glcanvas.h>    // Needs to be last because reasons :-/
#include "WipeTowerDialog.hpp"

#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/Platform.hpp"
#include "nlohmann/json.hpp"

#include "PhysicalPrinterDialog.hpp"
#include "PrintHostDialogs.hpp"
#include "PlateSettingsDialog.hpp"

using boost::optional;
namespace fs = boost::filesystem;
using Slic3r::_3DScene;
using Slic3r::Preset;
using Slic3r::GUI::format_wxstr;
using namespace nlohmann;

static const std::pair<unsigned int, unsigned int> THUMBNAIL_SIZE_3MF = { 512, 512 };

namespace Slic3r {
namespace GUI {

wxDEFINE_EVENT(EVT_SCHEDULE_BACKGROUND_PROCESS,     SimpleEvent);
wxDEFINE_EVENT(EVT_SLICING_UPDATE,                  SlicingStatusEvent);
wxDEFINE_EVENT(EVT_SLICING_COMPLETED,               wxCommandEvent);
wxDEFINE_EVENT(EVT_PROCESS_COMPLETED,               SlicingProcessCompletedEvent);
wxDEFINE_EVENT(EVT_EXPORT_BEGAN,                    wxCommandEvent);
wxDEFINE_EVENT(EVT_EXPORT_FINISHED,                 wxCommandEvent);
wxDEFINE_EVENT(EVT_IMPORT_MODEL_ID,                 wxCommandEvent);
wxDEFINE_EVENT(EVT_DOWNLOAD_PROJECT,                wxCommandEvent);
wxDEFINE_EVENT(EVT_PUBLISH,                         wxCommandEvent);
// BBS: backup & restore
wxDEFINE_EVENT(EVT_RESTORE_PROJECT,                 wxCommandEvent);
wxDEFINE_EVENT(EVT_PRINT_FINISHED,                  wxCommandEvent);
wxDEFINE_EVENT(EVT_SEND_FINISHED,                   wxCommandEvent);
wxDEFINE_EVENT(EVT_PUBLISH_FINISHED,                wxCommandEvent);
//BBS: repair model
wxDEFINE_EVENT(EVT_REPAIR_MODEL,                    wxCommandEvent);
wxDEFINE_EVENT(EVT_FILAMENT_COLOR_CHANGED,          wxCommandEvent);
wxDEFINE_EVENT(EVT_INSTALL_PLUGIN_NETWORKING,       wxCommandEvent);
wxDEFINE_EVENT(EVT_UPDATE_PLUGINS_WHEN_LAUNCH,       wxCommandEvent);
wxDEFINE_EVENT(EVT_INSTALL_PLUGIN_HINT,             wxCommandEvent);
wxDEFINE_EVENT(EVT_PREVIEW_ONLY_MODE_HINT,          wxCommandEvent);
//BBS: change light/dark mode
wxDEFINE_EVENT(EVT_GLCANVAS_COLOR_MODE_CHANGED,     SimpleEvent);


bool Plater::has_illegal_filename_characters(const wxString& wxs_name)
{
    std::string name = into_u8(wxs_name);
    return has_illegal_filename_characters(name);
}

bool Plater::has_illegal_filename_characters(const std::string& name)
{
    const char* illegal_characters = "<>:/\\|?*\"";
    for (size_t i = 0; i < std::strlen(illegal_characters); i++)
        if (name.find_first_of(illegal_characters[i]) != std::string::npos)
            return true;

    return false;
}

void Plater::show_illegal_characters_warning(wxWindow* parent)
{
    show_error(parent, _L("Invalid name, the following characters are not allowed:") + " <>:/\\|?*\"");
}

enum SlicedInfoIdx
{
    siFilament_m,
    siFilament_mm3,
    siFilament_g,
    siMateril_unit,
    siCost,
    siEstimatedTime,
    siWTNumbetOfToolchanges,
    siCount
};

enum class LoadFilesType {
    NoFile,
    Single3MF,
    SingleOther,
    Multiple3MF,
    MultipleOther,
    Multiple3MFOther,
};

enum class LoadType : unsigned char
{
    Unknown,
    OpenProject,
    LoadGeometry,
    LoadConfig
};

class SlicedInfo : public wxStaticBoxSizer
{
public:
    SlicedInfo(wxWindow *parent);
    void SetTextAndShow(SlicedInfoIdx idx, const wxString& text, const wxString& new_label="");

private:
    std::vector<std::pair<wxStaticText*, wxStaticText*>> info_vec;
};

SlicedInfo::SlicedInfo(wxWindow *parent) :
    wxStaticBoxSizer(new wxStaticBox(parent, wxID_ANY, _L("Sliced Info")), wxVERTICAL)
{
    GetStaticBox()->SetFont(wxGetApp().bold_font());
    wxGetApp().UpdateDarkUI(GetStaticBox());

    auto *grid_sizer = new wxFlexGridSizer(2, 5, 15);
    grid_sizer->SetFlexibleDirection(wxVERTICAL);

    info_vec.reserve(siCount);

    auto init_info_label = [this, parent, grid_sizer](wxString text_label) {
        auto *text = new wxStaticText(parent, wxID_ANY, text_label);
        text->SetForegroundColour(*wxBLACK);
        text->SetFont(wxGetApp().small_font());
        auto info_label = new wxStaticText(parent, wxID_ANY, "N/A");
        info_label->SetForegroundColour(*wxBLACK);
        info_label->SetFont(wxGetApp().small_font());
        grid_sizer->Add(text, 0);
        grid_sizer->Add(info_label, 0);
        info_vec.push_back(std::pair<wxStaticText*, wxStaticText*>(text, info_label));
    };

    init_info_label(_L("Used Filament (m)"));
    init_info_label(_L("Used Filament (mm³)"));
    init_info_label(_L("Used Filament (g)"));
    init_info_label(_L("Used Materials"));
    init_info_label(_L("Cost"));
    init_info_label(_L("Estimated time"));
    init_info_label(_L("Filament changes"));

    Add(grid_sizer, 0, wxEXPAND);
    this->Show(false);
}

void SlicedInfo::SetTextAndShow(SlicedInfoIdx idx, const wxString& text, const wxString& new_label/*=""*/)
{
    const bool show = text != "N/A";
    if (show)
        info_vec[idx].second->SetLabelText(text);
    if (!new_label.IsEmpty())
        info_vec[idx].first->SetLabelText(new_label);
    info_vec[idx].first->Show(show);
    info_vec[idx].second->Show(show);
}

static wxString temp_dir;

// Sidebar / private

enum class ActionButtonType : int {
    abReslice,
    abExport,
    abSendGCode
};

struct Sidebar::priv
{
    Plater *plater;

    wxPanel *scrolled;
    PlaterPresetComboBox *combo_print;
    std::vector<PlaterPresetComboBox*> combos_filament;
    int editing_filament = -1;
    wxBoxSizer *sizer_filaments;
    PlaterPresetComboBox *combo_sla_print;
    PlaterPresetComboBox *combo_sla_material;
    PlaterPresetComboBox* combo_printer = nullptr;
    wxBoxSizer *sizer_params;

    //BBS Sidebar widgets
    wxPanel* m_panel_print_title;
    wxStaticText* m_staticText_print_title;
    wxPanel* m_panel_print_content;
    wxComboBox* m_comboBox_print_preset;
    wxStaticLine* m_staticline1;
    StaticBox* m_panel_filament_title;
    wxStaticText* m_staticText_filament_settings;
    ScalableButton *  m_bpButton_add_filament;
    ScalableButton *  m_bpButton_del_filament;
    ScalableButton *  m_bpButton_ams_filament;
    ScalableButton *  m_bpButton_set_filament;
    wxPanel* m_panel_filament_content;
    wxScrolledWindow* m_scrolledWindow_filament_content;
    wxStaticLine* m_staticline2;
    wxPanel* m_panel_project_title;
    ScalableButton* m_filament_icon = nullptr;
    Button * m_flushing_volume_btn = nullptr;

    // BBS printer config
    StaticBox* m_panel_printer_title = nullptr;
    ScalableButton* m_printer_icon = nullptr;
    ScalableButton* m_printer_setting = nullptr;
    wxStaticText* m_text_printer_settings = nullptr;
    wxPanel* m_panel_printer_content = nullptr;

    ObjectList          *m_object_list{ nullptr };
    ObjectSettings      *object_settings{ nullptr };

    wxButton *btn_export_gcode;
    wxButton *btn_reslice;
    ScalableButton *btn_send_gcode;
    //ScalableButton *btn_eject_device;
    ScalableButton* btn_export_gcode_removable; //exports to removable drives (appears only if removable drive is connected)

    bool                is_collapsed {false};
    Search::OptionsSearcher     searcher;
    std::string ams_list_device;

    priv(Plater *plater) : plater(plater) {}
    ~priv();

    void show_preset_comboboxes();

#ifdef _WIN32
    wxString btn_reslice_tip;
    void show_rich_tip(const wxString& tooltip, wxButton* btn);
    void hide_rich_tip(wxButton* btn);
#endif
};

Sidebar::priv::~priv()
{
    // BBS
    //delete object_manipulation;
    delete object_settings;
    // BBS
#if 0
    delete frequently_changed_parameters;
#endif
}

void Sidebar::priv::show_preset_comboboxes()
{
    const bool showSLA = wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA;

//BBS
#if 0
    for (size_t i = 0; i < 4; ++i)
        sizer_presets->Show(i, !showSLA);

    for (size_t i = 4; i < 8; ++i) {
        if (sizer_presets->IsShown(i) != showSLA)
            sizer_presets->Show(i, showSLA);
    }

    frequently_changed_parameters->Show(!showSLA);
#endif

    scrolled->GetParent()->Layout();
    scrolled->Refresh();
}

#ifdef _WIN32
using wxRichToolTipPopup = wxCustomBackgroundWindow<wxPopupTransientWindow>;
static wxRichToolTipPopup* get_rtt_popup(wxButton* btn)
{
    auto children = btn->GetChildren();
    for (auto child : children)
        if (child->IsShown())
            return dynamic_cast<wxRichToolTipPopup*>(child);
    return nullptr;
}

void Sidebar::priv::show_rich_tip(const wxString& tooltip, wxButton* btn)
{
    if (tooltip.IsEmpty())
        return;
    wxRichToolTip tip(tooltip, "");
    tip.SetIcon(wxICON_NONE);
    tip.SetTipKind(wxTipKind_BottomRight);
    tip.SetTitleFont(wxGetApp().normal_font());
    tip.SetBackgroundColour(wxGetApp().get_window_default_clr());

    tip.ShowFor(btn);
    // Every call of the ShowFor() creates new RichToolTip and show it.
    // Every one else are hidden.
    // So, set a text color just for the shown rich tooltip
    if (wxRichToolTipPopup* popup = get_rtt_popup(btn)) {
        auto children = popup->GetChildren();
        for (auto child : children) {
            child->SetForegroundColour(wxGetApp().get_label_clr_default());
            // we neen just first text line for out rich tooltip
            return;
        }
    }
}

void Sidebar::priv::hide_rich_tip(wxButton* btn)
{
    if (wxRichToolTipPopup* popup = get_rtt_popup(btn))
        popup->Dismiss();
}
#endif

// Sidebar / public

static struct DynamicFilamentList : DynamicList
{
    std::vector<std::pair<wxString, wxBitmap *>> items;

    void apply_on(Choice *c) override
    {
        if (items.empty())
            update(true);
        auto cb = dynamic_cast<ComboBox *>(c->window);
        auto n  = cb->GetSelection();
        cb->Clear();
        cb->Append(_L("Default"));
        for (auto i : items) {
            cb->Append(i.first, *i.second);
        }
        if (n < cb->GetCount())
            cb->SetSelection(n);
    }
    wxString get_value(int index) override
    {
        wxString str;
        str << index;
        return str;
    }
    int index_of(wxString value) override
    {
        long n = 0;
        return (value.ToLong(&n) && n <= items.size()) ? int(n) : -1;
    }
    void update(bool force = false)
    {
        items.clear();
        if (!force && m_choices.empty())
            return;
        auto icons = get_extruder_color_icons(true);
        auto presets = wxGetApp().preset_bundle->filament_presets;
        for (int i = 0; i < presets.size(); ++i) {
            wxString str;
            std::string type;
            wxGetApp().preset_bundle->filaments.find_preset(presets[i])->get_filament_type(type);
            str << (i + 1) << " - " << type;
            items.push_back({str, icons[i]});
        }
        DynamicList::update();
    }
} dynamic_filament_list;

Sidebar::Sidebar(Plater *parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(42 * wxGetApp().em_unit(), -1)), p(new priv(parent))
{
    Choice::register_dynamic_list("support_filament", &dynamic_filament_list);
    Choice::register_dynamic_list("support_interface_filament", &dynamic_filament_list);

    p->scrolled = new wxPanel(this);
    //    p->scrolled->SetScrollbars(0, 100, 1, 2); // ys_DELETE_after_testing. pixelsPerUnitY = 100
    // but this cause the bad layout of the sidebar, when all infoboxes appear.
    // As a result we can see the empty block at the bottom of the sidebar
    // But if we set this value to 5, layout will be better
    //p->scrolled->SetScrollRate(0, 5);
    p->scrolled->SetBackgroundColour(*wxWHITE);


    SetFont(wxGetApp().normal_font());
#ifndef __APPLE__
#ifdef _WIN32
    wxGetApp().UpdateDarkUI(this);
    wxGetApp().UpdateDarkUI(p->scrolled);
#else
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif
#endif

    int em = wxGetApp().em_unit();
    //BBS refine layout and styles
    // Sizer in the scrolled area
    auto* scrolled_sizer = m_scrolled_sizer = new wxBoxSizer(wxVERTICAL);
    p->scrolled->SetSizer(scrolled_sizer);

    wxColour title_bg = wxColour(248, 248, 248);
    wxColour inactive_text = wxColour(86, 86, 86);
    wxColour active_text = wxColour(0, 0, 0);
    wxColour static_line_col = wxColour(166, 169, 170);

#ifdef __WINDOWS__
    p->scrolled->SetDoubleBuffered(true);
#endif //__WINDOWS__

    // add printer
    {
        /***************** 1. create printer title bar    **************/
        // 1.1 create title bar resources
        p->m_panel_printer_title = new StaticBox(p->scrolled, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxBORDER_NONE);
        p->m_panel_printer_title->SetBackgroundColor(title_bg);
        p->m_panel_printer_title->SetBackgroundColor2(0xF1F1F1);

        p->m_printer_icon = new ScalableButton(p->m_panel_printer_title, wxID_ANY, "printer");
        p->m_text_printer_settings = new Label(p->m_panel_printer_title, _L("Printer"), LB_PROPAGATE_MOUSE_EVENT);

        p->m_printer_icon->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
            auto wizard_t = new ConfigWizard(wxGetApp().mainframe);
            wizard_t->run(ConfigWizard::RR_USER, ConfigWizard::SP_CUSTOM);
            });


        p->m_printer_setting = new ScalableButton(p->m_panel_printer_title, wxID_ANY, "settings");
        p->m_printer_setting->Bind(wxEVT_BUTTON, [this](wxCommandEvent &e) {
            // p->editing_filament = -1;
            // wxGetApp().params_dialog()->Popup();
            // wxGetApp().get_tab(Preset::TYPE_FILAMENT)->restore_last_select_item();
            wxGetApp().run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_PRINTERS);
            });

        wxBoxSizer* h_sizer_title = new wxBoxSizer(wxHORIZONTAL);
        h_sizer_title->Add(p->m_printer_icon, 0, wxALIGN_CENTRE | wxLEFT | wxRIGHT, em);
        h_sizer_title->Add(p->m_text_printer_settings, 0, wxALIGN_CENTER);
        h_sizer_title->AddStretchSpacer();
        h_sizer_title->Add(p->m_printer_setting, 0, wxALIGN_CENTER);
        h_sizer_title->Add(15 * em / 10, 0, 0, 0, 0);
        h_sizer_title->SetMinSize(-1, 3 * em);

        p->m_panel_printer_title->SetSizer(h_sizer_title);
        p->m_panel_printer_title->Layout();

        // 1.2 Add spliters around title bar
        // add spliter 1
        //auto spliter_1 = new ::StaticLine(p->scrolled);
        //spliter_1->SetBackgroundColour("#A6A9AA");
        //scrolled_sizer->Add(spliter_1, 0, wxEXPAND);

        // add printer title
        scrolled_sizer->Add(p->m_panel_printer_title, 0, wxEXPAND | wxALL, 0);
        p->m_panel_printer_title->Bind(wxEVT_LEFT_UP, [this] (auto & e) {
            if (p->m_panel_printer_content->GetMaxHeight() == 0)
                p->m_panel_printer_content->SetMaxSize({-1, -1});
            else
                p->m_panel_printer_content->SetMaxSize({-1, 0});
            m_scrolled_sizer->Layout();
        });

        // add spliter 2
        auto spliter_2 = new ::StaticLine(p->scrolled);
        spliter_2->SetLineColour("#CECECE");
        scrolled_sizer->Add(spliter_2, 0, wxEXPAND);


        /*************************** 2. add printer content ************************/
        p->m_panel_printer_content = new wxPanel(p->scrolled, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
        p->m_panel_printer_content->SetBackgroundColour(wxColour(255, 255, 255));

        PlaterPresetComboBox* combo_printer = new PlaterPresetComboBox(p->m_panel_printer_content, Preset::TYPE_PRINTER);
        ScalableButton* edit_btn = new ScalableButton(p->m_panel_printer_content, wxID_ANY, "edit");
        edit_btn->SetToolTip(_L("Click to edit preset"));
        edit_btn->Bind(wxEVT_BUTTON, [this, combo_printer](wxCommandEvent)
            {
                p->editing_filament = -1;
                if (combo_printer->switch_to_tab())
                    p->editing_filament = 0;
            });
        combo_printer->edit_btn = edit_btn;
        p->combo_printer = combo_printer;

        connection_btn = new ScalableButton(p->m_panel_printer_content, wxID_ANY, "monitor_signal_strong");
        connection_btn->SetBackgroundColour(wxColour(255, 255, 255));
        connection_btn->SetToolTip(_L("Connection"));
        connection_btn->Bind(wxEVT_BUTTON, [this, combo_printer](wxCommandEvent)
            {
                PhysicalPrinterDialog dlg(this->GetParent());
                dlg.ShowModal();
            });

        wxBoxSizer* vsizer_printer = new wxBoxSizer(wxVERTICAL);
        wxBoxSizer* hsizer_printer = new wxBoxSizer(wxHORIZONTAL);

        vsizer_printer->AddSpacer(FromDIP(16));
        hsizer_printer->Add(combo_printer, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(3));
        hsizer_printer->Add(edit_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(3));
        hsizer_printer->Add(FromDIP(8), 0, 0, 0, 0);
        hsizer_printer->Add(connection_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(3));
        hsizer_printer->Add(FromDIP(8), 0, 0, 0, 0);
        vsizer_printer->Add(hsizer_printer, 0, wxEXPAND, 0);

        // Bed type selection
        wxBoxSizer* bed_type_sizer = new wxBoxSizer(wxHORIZONTAL);
        wxStaticText* bed_type_title = new wxStaticText(p->m_panel_printer_content, wxID_ANY, _L("Bed type"));
        //bed_type_title->SetBackgroundColour();
        bed_type_title->Wrap(-1);
        bed_type_title->SetFont(Label::Body_14);
        m_bed_type_list = new ComboBox(p->m_panel_printer_content, wxID_ANY, wxString(""), wxDefaultPosition, {-1, FromDIP(30)}, 0, nullptr, wxCB_READONLY);
        const ConfigOptionDef* bed_type_def = print_config_def.get("curr_bed_type");
        if (bed_type_def && bed_type_def->enum_keys_map) {
            for (auto item : *bed_type_def->enum_keys_map) {
                if (item.first == "Default Plate")
                    continue;

                m_bed_type_list->AppendString(_L(item.first));
            }
        }

        bed_type_title->Bind(wxEVT_ENTER_WINDOW, [bed_type_title, this](wxMouseEvent &e) {
            e.Skip();
            auto font = bed_type_title->GetFont();
            font.SetUnderlined(true);
            bed_type_title->SetFont(font);
            SetCursor(wxCURSOR_HAND);
        });
        bed_type_title->Bind(wxEVT_LEAVE_WINDOW, [bed_type_title, this](wxMouseEvent &e) {
            e.Skip();
            auto font = bed_type_title->GetFont();
            font.SetUnderlined(false);
            bed_type_title->SetFont(font);
            SetCursor(wxCURSOR_ARROW);
        });
        bed_type_title->Bind(wxEVT_LEFT_UP, [bed_type_title, this](wxMouseEvent &e) {
            wxLaunchDefaultBrowser("https://wiki.bambulab.com/en/x1/manual/compatibility-and-parameter-settings-of-filaments");
        });

        AppConfig *app_config = wxGetApp().app_config;
        std::string str_bed_type = app_config->get("curr_bed_type");
        int bed_type_value = atoi(str_bed_type.c_str());
        // hotfix: btDefault is added as the first one in BedType, and app_config should not be btDefault
        if (bed_type_value == 0) {
            app_config->set("curr_bed_type", "1");
            bed_type_value = 1;
        }

        int bed_type_idx = bed_type_value - 1;
        m_bed_type_list->Select(bed_type_idx);
        bed_type_sizer->Add(bed_type_title, 0, wxLEFT | wxRIGHT | wxALIGN_CENTER_VERTICAL, FromDIP(10));
        bed_type_sizer->Add(m_bed_type_list, 1, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(10));
        vsizer_printer->Add(bed_type_sizer, 0, wxEXPAND | wxTOP, FromDIP(5));
        vsizer_printer->AddSpacer(FromDIP(16));

        auto& project_config = wxGetApp().preset_bundle->project_config;
        /*const t_config_enum_values* keys_map = print_config_def.get("curr_bed_type")->enum_keys_map;
        BedType bed_type = btCount;
        for (auto item : *keys_map) {
            if (item.first == str_bed_type)
                bed_type = (BedType)item.second;
        }*/
        BedType bed_type = (BedType)bed_type_value;
        project_config.set_key_value("curr_bed_type", new ConfigOptionEnum<BedType>(bed_type));

        p->m_panel_printer_content->SetSizer(vsizer_printer);
        p->m_panel_printer_content->Layout();
        scrolled_sizer->Add(p->m_panel_printer_content, 0, wxEXPAND, 0);
    }

    {
    // add filament title
    p->m_panel_filament_title = new StaticBox(p->scrolled, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxBORDER_NONE);
    p->m_panel_filament_title->SetBackgroundColor(title_bg);
    p->m_panel_filament_title->SetBackgroundColor2(0xF1F1F1);
    p->m_panel_filament_title->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &e) {
        if (e.GetPosition().x > (p->m_flushing_volume_btn->IsShown()
                ? p->m_flushing_volume_btn->GetPosition().x : p->m_bpButton_add_filament->GetPosition().x))
            return;
        if (p->m_panel_filament_content->GetMaxHeight() == 0)
            p->m_panel_filament_content->SetMaxSize({-1, -1});
        else
            p->m_panel_filament_content->SetMaxSize({-1, 0});
        m_scrolled_sizer->Layout();
    });

    wxBoxSizer* bSizer39;
    bSizer39 = new wxBoxSizer( wxHORIZONTAL );
    p->m_filament_icon = new ScalableButton(p->m_panel_filament_title, wxID_ANY, "filament");
    p->m_staticText_filament_settings = new Label(p->m_panel_filament_title, _L("Filament"), LB_PROPAGATE_MOUSE_EVENT);
    bSizer39->Add(p->m_filament_icon, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(10));
    bSizer39->Add( p->m_staticText_filament_settings, 0, wxALIGN_CENTER );
    bSizer39->Add(FromDIP(10), 0, 0, 0, 0);
    bSizer39->SetMinSize(-1, FromDIP(30));

    p->m_panel_filament_title->SetSizer( bSizer39 );
    p->m_panel_filament_title->Layout();
    auto spliter_1 = new ::StaticLine(p->scrolled);
    spliter_1->SetLineColour("#A6A9AA");
    scrolled_sizer->Add(spliter_1, 0, wxEXPAND);
    scrolled_sizer->Add(p->m_panel_filament_title, 0, wxEXPAND | wxALL, 0);
    auto spliter_2 = new ::StaticLine(p->scrolled);
    spliter_2->SetLineColour("#CECECE");
    scrolled_sizer->Add(spliter_2, 0, wxEXPAND);

    bSizer39->AddStretchSpacer(1);

    // BBS
    // add wiping dialog
    //wiping_dialog_button->SetFont(wxGetApp().normal_font());
    p->m_flushing_volume_btn = new Button(p->m_panel_filament_title, _L("Flushing volumes"));
    p->m_flushing_volume_btn->SetFont(Label::Body_10);
    p->m_flushing_volume_btn->SetPaddingSize(wxSize(FromDIP(8),FromDIP(3)));
    p->m_flushing_volume_btn->SetCornerRadius(FromDIP(8));

    StateColor flush_bg_col(std::pair<wxColour, int>(wxColour(219, 253, 231), StateColor::Pressed),
                            std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Normal));

    StateColor flush_fg_col(std::pair<wxColour, int>(wxColour(107, 107, 106), StateColor::Pressed),
                            std::pair<wxColour, int>(wxColour(107, 107, 106), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(107, 107, 106), StateColor::Normal));

    StateColor flush_bd_col(std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Pressed),
                            std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(172, 172, 172), StateColor::Normal));

    p->m_flushing_volume_btn->SetBackgroundColor(flush_bg_col);
    p->m_flushing_volume_btn->SetBorderColor(flush_bd_col);
    p->m_flushing_volume_btn->SetTextColor(flush_fg_col);
    p->m_flushing_volume_btn->SetFocus();
    p->m_flushing_volume_btn->SetId(wxID_RESET);
    p->m_flushing_volume_btn->Rescale();

    p->m_flushing_volume_btn->Bind(wxEVT_BUTTON, ([parent](wxCommandEvent &e)
        {
            auto& project_config = wxGetApp().preset_bundle->project_config;
            auto& printer_config = wxGetApp().preset_bundle->printers.get_edited_preset().config;
            const std::vector<double>& init_matrix = (project_config.option<ConfigOptionFloats>("flush_volumes_matrix"))->values;
            const std::vector<double>& init_extruders = (project_config.option<ConfigOptionFloats>("flush_volumes_vector"))->values;
            ConfigOption* extra_flush_volume_opt = printer_config.option("nozzle_volume");
            int extra_flush_volume = extra_flush_volume_opt ? (int)extra_flush_volume_opt->getFloat() : 0;
            ConfigOptionFloat* flush_multi_opt = project_config.option<ConfigOptionFloat>("flush_multiplier");
            float flush_multiplier = flush_multi_opt ? flush_multi_opt->getFloat() : 1.f;

            const std::vector<std::string> extruder_colours = wxGetApp().plater()->get_extruder_colors_from_plater_config();

            WipingDialog dlg(parent, cast<float>(init_matrix), cast<float>(init_extruders), extruder_colours, extra_flush_volume, flush_multiplier);

            if (dlg.ShowModal() == wxID_OK) {
                std::vector<float> matrix = dlg.get_matrix();
                std::vector<float> extruders = dlg.get_extruders();
                (project_config.option<ConfigOptionFloats>("flush_volumes_matrix"))->values = std::vector<double>(matrix.begin(), matrix.end());
                (project_config.option<ConfigOptionFloats>("flush_volumes_vector"))->values = std::vector<double>(extruders.begin(), extruders.end());
                (project_config.option<ConfigOptionFloat>("flush_multiplier"))->set(new ConfigOptionFloat(dlg.get_flush_multiplier()));

                wxGetApp().app_config->set("flush_multiplier", std::to_string(dlg.get_flush_multiplier()));
                wxGetApp().preset_bundle->export_selections(*wxGetApp().app_config);

                wxGetApp().plater()->update_project_dirty_from_presets();
                wxPostEvent(parent, SimpleEvent(EVT_SCHEDULE_BACKGROUND_PROCESS, parent));
            }
        }));
    bSizer39->Add(p->m_flushing_volume_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(5));
    bSizer39->Hide(p->m_flushing_volume_btn);
    bSizer39->Add(FromDIP(10), 0, 0, 0, 0 );

    ScalableButton* add_btn = new ScalableButton(p->m_panel_filament_title, wxID_ANY, "add_filament");
    add_btn->SetToolTip(_L("Add one filament"));
    add_btn->Bind(wxEVT_BUTTON, [this, scrolled_sizer](wxCommandEvent& e){
        // BBS: limit filament choices to 16
        if (p->combos_filament.size() >= 16)
            return;

        int filament_count = p->combos_filament.size() + 1;
        wxColour new_col = Plater::get_next_color_for_filament();
        std::string new_color = new_col.GetAsString(wxC2S_HTML_SYNTAX).ToStdString();
        wxGetApp().preset_bundle->set_num_filaments(filament_count, new_color);
        wxGetApp().plater()->on_filaments_change(filament_count);
        wxGetApp().get_tab(Preset::TYPE_PRINT)->update();
        wxGetApp().preset_bundle->export_selections(*wxGetApp().app_config);
    });
    p->m_bpButton_add_filament = add_btn;

    bSizer39->Add(add_btn, 0, wxALIGN_CENTER|wxALL, FromDIP(5));
    bSizer39->Add(FromDIP(10), 0, 0, 0, 0 );

    ScalableButton* del_btn = new ScalableButton(p->m_panel_filament_title, wxID_ANY, "delete_filament");
    del_btn->SetToolTip(_L("Remove last filament"));
    del_btn->Bind(wxEVT_BUTTON, [this, scrolled_sizer](wxCommandEvent &e) {
        if (p->combos_filament.size() <= 1)
            return;

        size_t filament_count = p->combos_filament.size() - 1;
        if (wxGetApp().preset_bundle->is_the_only_edited_filament(filament_count) || (filament_count == 1)) {
            wxGetApp().get_tab(Preset::TYPE_FILAMENT)->select_preset(wxGetApp().preset_bundle->filament_presets[0], false, "", true);
        }

        if (p->editing_filament >= filament_count) {
            p->editing_filament = -1;
        }

        wxGetApp().preset_bundle->set_num_filaments(filament_count);
        wxGetApp().plater()->on_filaments_change(filament_count);
        wxGetApp().get_tab(Preset::TYPE_PRINT)->update();
        wxGetApp().preset_bundle->export_selections(*wxGetApp().app_config);
    });
    p->m_bpButton_del_filament = del_btn;

    bSizer39->Add(del_btn, 0, wxALIGN_CENTER_VERTICAL, FromDIP(5));
    bSizer39->Add(FromDIP(20), 0, 0, 0, 0);

    ams_btn = new ScalableButton(p->m_panel_filament_title, wxID_ANY, "ams_fila_sync", wxEmptyString, wxDefaultSize, wxDefaultPosition,
                                                 wxBU_EXACTFIT | wxNO_BORDER, false, 18);
    ams_btn->SetToolTip(_L("Synchronize filament list from AMS"));
    ams_btn->Bind(wxEVT_BUTTON, [this, scrolled_sizer](wxCommandEvent &e) {
        sync_ams_list();
    });
    p->m_bpButton_ams_filament = ams_btn;

    bSizer39->Add(ams_btn, 0, wxALIGN_CENTER|wxALL, FromDIP(5));
    bSizer39->Add(FromDIP(10), 0, 0, 0, 0 );

    ScalableButton* set_btn = new ScalableButton(p->m_panel_filament_title, wxID_ANY, "settings");
    set_btn->SetToolTip(_L("Set filaments to use"));
    set_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent &e) {
        p->editing_filament = -1;
        // wxGetApp().params_dialog()->Popup();
        // wxGetApp().get_tab(Preset::TYPE_FILAMENT)->restore_last_select_item();
        wxGetApp().run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_FILAMENTS);
        });
    p->m_bpButton_set_filament = set_btn;

    bSizer39->Add(set_btn, 0, wxALIGN_CENTER);
    bSizer39->Add(FromDIP(15), 0, 0, 0, 0);

    // add filament content
    p->m_panel_filament_content = new wxPanel( p->scrolled, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL );
    p->m_panel_filament_content->SetBackgroundColour( wxColour( 255, 255, 255 ) );

    //wxBoxSizer* bSizer_filament_content;
    //bSizer_filament_content = new wxBoxSizer( wxHORIZONTAL );

    // BBS:  filament double columns
    p->sizer_filaments = new wxBoxSizer(wxHORIZONTAL);
    p->sizer_filaments->Add(new wxBoxSizer(wxVERTICAL), 1, wxEXPAND);
    p->sizer_filaments->Add(new wxBoxSizer(wxVERTICAL), 1, wxEXPAND);

    p->combos_filament.push_back(nullptr);

    /* first filament item */
    p->combos_filament[0] = new PlaterPresetComboBox(p->m_panel_filament_content, Preset::TYPE_FILAMENT);
    auto combo_and_btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    // BBS:  filament double columns
    combo_and_btn_sizer->Add(FromDIP(8), 0, 0, 0, 0);
    if (p->combos_filament[0]->clr_picker) {
        p->combos_filament[0]->clr_picker->SetLabel("1");
        combo_and_btn_sizer->Add(p->combos_filament[0]->clr_picker, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(3));
    }
    combo_and_btn_sizer->Add(p->combos_filament[0], 1, wxALL | wxEXPAND, FromDIP(2))->SetMinSize({-1, FromDIP(30) });

    ScalableButton* edit_btn = new ScalableButton(p->m_panel_filament_content, wxID_ANY, "edit");
    edit_btn->SetBackgroundColour(wxColour(255, 255, 255));
    edit_btn->SetToolTip(_L("Click to edit preset"));

    PlaterPresetComboBox* combobox = p->combos_filament[0];
    edit_btn->Bind(wxEVT_BUTTON, [this, combobox](wxCommandEvent)
        {
            p->editing_filament = 0;
            combobox->switch_to_tab();
        });
    combobox->edit_btn = edit_btn;

    combo_and_btn_sizer->Add(edit_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(3));
    combo_and_btn_sizer->Add(FromDIP(8), 0, 0, 0, 0);

    p->combos_filament[0]->set_filament_idx(0);
    p->sizer_filaments->GetItem((size_t)0)->GetSizer()->Add(combo_and_btn_sizer, 1, wxEXPAND);

    //bSizer_filament_content->Add(p->sizer_filaments, 1, wxALIGN_CENTER | wxALL);
    wxSizer *sizer_filaments2 = new wxBoxSizer(wxVERTICAL);
    sizer_filaments2->AddSpacer(FromDIP(16));
    sizer_filaments2->Add(p->sizer_filaments, 0, wxEXPAND, 0);
    sizer_filaments2->AddSpacer(FromDIP(16));
    p->m_panel_filament_content->SetSizer(sizer_filaments2);
    p->m_panel_filament_content->Layout();
    scrolled_sizer->Add(p->m_panel_filament_content, 0, wxEXPAND, 0);
    }

    {
    //add project title
    auto params_panel = ((MainFrame*)parent->GetParent())->m_param_panel;
    if (params_panel) {
        params_panel->get_top_panel()->Reparent(p->scrolled);
        auto spliter_1 = new ::StaticLine(p->scrolled);
        spliter_1->SetLineColour("#A6A9AA");
        scrolled_sizer->Add(spliter_1, 0, wxEXPAND);
        scrolled_sizer->Add(params_panel->get_top_panel(), 0, wxEXPAND);
        auto spliter_2 = new ::StaticLine(p->scrolled);
        spliter_2->SetLineColour("#CECECE");
        scrolled_sizer->Add(spliter_2, 0, wxEXPAND);
    }

    //add project content
    p->sizer_params = new wxBoxSizer(wxVERTICAL);
    p->m_object_list = new ObjectList(p->scrolled);
    p->sizer_params->Add(p->m_object_list, 1, wxEXPAND | wxTOP, 0);
    scrolled_sizer->Add(p->sizer_params, 2, wxEXPAND | wxLEFT, 0);
    p->m_object_list->Hide();

    // Frequently Object Settings
    p->object_settings = new ObjectSettings(p->scrolled);
#if !NEW_OBJECT_SETTING
    p->object_settings->Hide();
    p->sizer_params->Add(p->object_settings->get_sizer(), 0, wxEXPAND | wxTOP, 5 * em / 10);
#else
    if (params_panel) {
        params_panel->Reparent(p->scrolled);
        scrolled_sizer->Add(params_panel, 3, wxEXPAND);
    }
#endif
    }

    auto *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(p->scrolled, 1, wxEXPAND);
    SetSizer(sizer);
}

Sidebar::~Sidebar() {}

void Sidebar::init_filament_combo(PlaterPresetComboBox **combo, const int filament_idx) {
    *combo = new PlaterPresetComboBox(p->m_panel_filament_content, Slic3r::Preset::TYPE_FILAMENT);
    (*combo)->set_filament_idx(filament_idx);

    auto combo_and_btn_sizer = new wxBoxSizer(wxHORIZONTAL);

    // BBS:  filament double columns
    int em = wxGetApp().em_unit();
    combo_and_btn_sizer->Add(FromDIP(8), 0, 0, 0, 0 );
    (*combo)->clr_picker->SetLabel(wxString::Format("%d", filament_idx + 1));
    combo_and_btn_sizer->Add((*combo)->clr_picker, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(3));
    combo_and_btn_sizer->Add(*combo, 1, wxALL | wxEXPAND, FromDIP(2))->SetMinSize({-1, FromDIP(30)});

    /* BBS hide del_btn
    ScalableButton* del_btn = new ScalableButton(p->m_panel_filament_content, wxID_ANY, "delete_filament");
    del_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e){
        int extruder_count = std::max(1, (int)p->combos_filament.size() - 1);

        update_objects_list_filament_column(std::max(1, extruder_count - 1));
        on_filaments_change(extruder_count);
        wxGetApp().preset_bundle->printers.get_edited_preset().set_num_extruders(extruder_count);
        wxGetApp().preset_bundle->update_multi_material_filament_presets();
    });

    combo_and_btn_sizer->Add(32 * em / 10, 0, 0, 0, 0);
    combo_and_btn_sizer->Add(del_btn, 0, wxALIGN_CENTER_VERTICAL, 5 * em / 10);
    */
    ScalableButton* edit_btn = new ScalableButton(p->m_panel_filament_content, wxID_ANY, "edit");
    edit_btn->SetToolTip(_L("Click to edit preset"));

    PlaterPresetComboBox* combobox = (*combo);
    edit_btn->Bind(wxEVT_BUTTON, [this, combobox, filament_idx](wxCommandEvent)
        {
            p->editing_filament = -1;
            if (combobox->switch_to_tab())
                p->editing_filament = filament_idx; // sync with TabPresetComboxBox's m_filament_idx
        });
    combobox->edit_btn = edit_btn;

    combo_and_btn_sizer->Add(edit_btn, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(3));

    combo_and_btn_sizer->Add(FromDIP(8), 0, 0, 0, 0);

    // BBS:  filament double columns
    auto side = filament_idx % 2;
    auto /***/sizer_filaments = this->p->sizer_filaments->GetItem(side)->GetSizer();
    if (side == 1 && filament_idx > 1) sizer_filaments->Remove(filament_idx / 2);
    sizer_filaments->Add(combo_and_btn_sizer, 1, wxEXPAND);
    if (side == 0) {
        sizer_filaments = this->p->sizer_filaments->GetItem(1)->GetSizer();
        sizer_filaments->AddStretchSpacer(1);
    }
}

void Sidebar::remove_unused_filament_combos(const size_t current_extruder_count)
{
    if (current_extruder_count >= p->combos_filament.size())
        return;
    while (p->combos_filament.size() > current_extruder_count) {
        const int last = p->combos_filament.size() - 1;
        auto sizer_filaments = this->p->sizer_filaments->GetItem(last % 2)->GetSizer();
        sizer_filaments->Remove(last / 2);
        (*p->combos_filament[last]).Destroy();
        p->combos_filament.pop_back();
    }
    // BBS:  filament double columns
    auto sizer_filaments0 = this->p->sizer_filaments->GetItem((size_t)0)->GetSizer();
    auto sizer_filaments1 = this->p->sizer_filaments->GetItem(1)->GetSizer();
    if (current_extruder_count < 2) {
        sizer_filaments1->Clear();
    } else {
        size_t c0 = sizer_filaments0->GetChildren().GetCount();
        size_t c1 = sizer_filaments1->GetChildren().GetCount();
        if (c0 < c1)
            sizer_filaments1->Remove(c1 - 1);
        else if (c0 > c1)
            sizer_filaments1->AddStretchSpacer(1);
    }
}

void Sidebar::update_all_preset_comboboxes()
{
    PresetBundle &preset_bundle = *wxGetApp().preset_bundle;
    const auto print_tech = preset_bundle.printers.get_edited_preset().printer_technology();

    bool is_bbl_preset = preset_bundle.printers.get_edited_preset().is_bbl_vendor_preset(&preset_bundle);

    auto p_mainframe = wxGetApp().mainframe;

    p_mainframe->show_device(is_bbl_preset);
    if (is_bbl_preset) {
        //only show connection button for not-BBL printer
        connection_btn->Hide();
        //only show sync-ams button for BBL printer
        ams_btn->Show();
        //update print button default value for bbl or third-party printer
        p_mainframe->set_print_button_to_default(MainFrame::PrintSelectType::ePrintPlate);
        m_bed_type_list->Enable();


    } else {
        connection_btn->Show();
        ams_btn->Hide();
        p_mainframe->set_print_button_to_default(MainFrame::PrintSelectType::eSendGcode);
        auto cfg = preset_bundle.printers.get_edited_preset().config;
        wxString url = cfg.opt_string("print_host_webui").empty() ? cfg.opt_string("print_host") : cfg.opt_string("print_host_webui");
        if(!url.empty()) 
        {
            if(!url.Lower().starts_with("http"))
                url = wxString::Format("http://%s",url);

            p_mainframe->load_printer_url(url);
        }

        m_bed_type_list->SelectAndNotify(btPEI-1);
        m_bed_type_list->Disable();
    }

    // Update the print choosers to only contain the compatible presets, update the dirty flags.
    //BBS

    // Update the printer choosers, update the dirty flags.
    //p->combo_printer->update();
    // Update the filament choosers to only contain the compatible presets, update the color preview,
    // update the dirty flags.
    if (print_tech == ptFFF) {
        for (PlaterPresetComboBox* cb : p->combos_filament)
            cb->update();
    }

    if (p->combo_printer)
        p->combo_printer->update();

    p_mainframe->m_tabpanel->SetSelection(p_mainframe->m_tabpanel->GetSelection());
}

void Sidebar::update_presets(Preset::Type preset_type)
{
    PresetBundle &preset_bundle = *wxGetApp().preset_bundle;
    const auto print_tech = preset_bundle.printers.get_edited_preset().printer_technology();

    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": enter, preset_type %1%")%preset_type;
    switch (preset_type) {
    case Preset::TYPE_FILAMENT:
    {
        // BBS
#if 0
        const size_t extruder_cnt = print_tech != ptFFF ? 1 :
                                dynamic_cast<ConfigOptionFloats*>(preset_bundle.printers.get_edited_preset().config.option("nozzle_diameter"))->values.size();
        const size_t filament_cnt = p->combos_filament.size() > extruder_cnt ? extruder_cnt : p->combos_filament.size();
#else
        const size_t filament_cnt = p->combos_filament.size();
#endif
        const std::string &name = preset_bundle.filaments.get_selected_preset_name();
        if (p->editing_filament >= 0) {
            preset_bundle.set_filament_preset(p->editing_filament, name);
        } else if (filament_cnt == 1) {
            // Single filament printer, synchronize the filament presets.
            preset_bundle.set_filament_preset(0, name);
        }

        for (size_t i = 0; i < filament_cnt; i++)
            p->combos_filament[i]->update();

        dynamic_filament_list.update();
        break;
    }

    case Preset::TYPE_PRINT:
        //wxGetApp().mainframe->m_param_panel;
        //p->combo_print->update();
        {
        Tab* print_tab = wxGetApp().get_tab(Preset::TYPE_PRINT);
        if (print_tab) {
            print_tab->get_combo_box()->update();
        }
        break;
        }
    case Preset::TYPE_SLA_PRINT:
        ;// p->combo_sla_print->update();
        break;

    case Preset::TYPE_SLA_MATERIAL:
        ;// p->combo_sla_material->update();
        break;

    case Preset::TYPE_PRINTER:
    {
        update_all_preset_comboboxes();
        p->show_preset_comboboxes();

        /* update bed shape */
        Tab* printer_tab = wxGetApp().get_tab(Preset::TYPE_PRINTER);
        if (printer_tab) {
            printer_tab->update();
        }
        break;
    }

    default: break;
    }

    // Synchronize config.ini with the current selections.
    wxGetApp().preset_bundle->export_selections(*wxGetApp().app_config);

    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": exit.");
}

//BBS
void Sidebar::update_presets_from_to(Slic3r::Preset::Type preset_type, std::string from, std::string to)
{
    PresetBundle &preset_bundle = *wxGetApp().preset_bundle;

    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": enter, preset_type %1%, from %2% to %3%")%preset_type %from %to;

    switch (preset_type) {
    case Preset::TYPE_FILAMENT:
    {
        const size_t filament_cnt = p->combos_filament.size();
        for (auto it = preset_bundle.filament_presets.begin(); it != preset_bundle.filament_presets.end(); it++)
        {
            if ((*it).compare(from) == 0) {
                (*it) = to;
            }
        }
        for (size_t i = 0; i < filament_cnt; i++)
            p->combos_filament[i]->update();
        break;
    }

    default: break;
    }

    // Synchronize config.ini with the current selections.
    wxGetApp().preset_bundle->export_selections(*wxGetApp().app_config);

    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": exit!");
}

void Sidebar::change_top_border_for_mode_sizer(bool increase_border)
{
    // BBS
#if 0
    if (p->mode_sizer) {
        p->mode_sizer->set_items_flag(increase_border ? wxTOP : 0);
        p->mode_sizer->set_items_border(increase_border ? int(0.5 * wxGetApp().em_unit()) : 0);
    }
#endif
}

void Sidebar::msw_rescale()
{
    SetMinSize(wxSize(42 * wxGetApp().em_unit(), -1));
    p->m_panel_printer_title->GetSizer()->SetMinSize(-1, 3 * wxGetApp().em_unit());
    p->m_panel_filament_title->GetSizer()
        ->SetMinSize(-1, 3 * wxGetApp().em_unit());
    p->m_printer_icon->msw_rescale();
    p->m_printer_setting->msw_rescale();
    p->m_filament_icon->msw_rescale();
    p->m_bpButton_add_filament->msw_rescale();
    p->m_bpButton_del_filament->msw_rescale();
    p->m_bpButton_ams_filament->msw_rescale();
    p->m_bpButton_set_filament->msw_rescale();
    p->m_flushing_volume_btn->Rescale();
    //BBS
    m_bed_type_list->Rescale();
    m_bed_type_list->SetMinSize({-1, 3 * wxGetApp().em_unit()});
#if 0
    if (p->mode_sizer)
        p->mode_sizer->msw_rescale();
#endif

    //for (PlaterPresetComboBox* combo : std::vector<PlaterPresetComboBox*> { p->combo_print,
    //                                                            //p->combo_sla_print,
    //                                                            //p->combo_sla_material,
    //                                                            //p->combo_printer
    //                                                            } )
    //    combo->msw_rescale();
    p->combo_printer->msw_rescale();
    for (PlaterPresetComboBox* combo : p->combos_filament)
        combo->msw_rescale();

    // BBS
    //p->frequently_changed_parameters->msw_rescale();
    //obj_list()->msw_rescale();
    // BBS TODO: add msw_rescale for newly added windows
    // BBS
    //p->object_manipulation->msw_rescale();
    p->object_settings->msw_rescale();

    // BBS
#if 0
    p->object_info->msw_rescale();

    p->btn_send_gcode->msw_rescale();
//    p->btn_eject_device->msw_rescale();
    p->btn_export_gcode_removable->msw_rescale();
#ifdef _WIN32
    const int scaled_height = p->btn_export_gcode_removable->GetBitmapHeight();
#else
    const int scaled_height = p->btn_export_gcode_removable->GetBitmapHeight() + 4;
#endif
    p->btn_export_gcode->SetMinSize(wxSize(-1, scaled_height));
    p->btn_reslice     ->SetMinSize(wxSize(-1, scaled_height));
#endif
    p->scrolled->Layout();

    p->searcher.dlg_msw_rescale();
}

void Sidebar::sys_color_changed()
{
    wxWindowUpdateLocker noUpdates(this);

#if 0
    for (wxWindow* win : std::vector<wxWindow*>{ this, p->sliced_info->GetStaticBox(), p->object_info->GetStaticBox(), p->btn_reslice, p->btn_export_gcode })
        wxGetApp().UpdateDarkUI(win);
    p->object_info->msw_rescale();

    for (wxWindow* win : std::vector<wxWindow*>{ p->scrolled, p->presets_panel })
        wxGetApp().UpdateAllStaticTextDarkUI(win);
#endif
    //for (wxWindow* btn : std::vector<wxWindow*>{ p->btn_reslice, p->btn_export_gcode })
    //    wxGetApp().UpdateDarkUI(btn, true);
    p->m_printer_icon->msw_rescale();
    p->m_printer_setting->msw_rescale();
    p->m_filament_icon->msw_rescale();
    p->m_bpButton_add_filament->msw_rescale();
    p->m_bpButton_del_filament->msw_rescale();
    p->m_bpButton_ams_filament->msw_rescale();
    p->m_bpButton_set_filament->msw_rescale();
    p->m_flushing_volume_btn->Rescale();

    // BBS
#if 0
    if (p->mode_sizer)
        p->mode_sizer->msw_rescale();
    p->frequently_changed_parameters->sys_color_changed();
#endif
    p->object_settings->sys_color_changed();

    //BBS: remove print related combos
#if 0
    for (PlaterPresetComboBox* combo : std::vector<PlaterPresetComboBox*>{  p->combo_print,
                                                                p->combo_sla_print,
                                                                p->combo_sla_material,
                                                                p->combo_printer })
        combo->sys_color_changed();
#endif
    for (PlaterPresetComboBox* combo : p->combos_filament)
        combo->sys_color_changed();

    // BBS
    obj_list()->sys_color_changed();
    // BBS
    //p->object_manipulation->sys_color_changed();

    // btn...->msw_rescale() updates icon on button, so use it
    //p->btn_send_gcode->msw_rescale();
//    p->btn_eject_device->msw_rescale();
    //p->btn_export_gcode_removable->msw_rescale();

    p->scrolled->Layout();

    p->searcher.dlg_sys_color_changed();
}

void Sidebar::search()
{
    p->searcher.search();
}

void Sidebar::jump_to_option(const std::string& opt_key, Preset::Type type, const std::wstring& category)
{
    //const Search::Option& opt = p->searcher.get_option(opt_key, type);
    if (type == Preset::TYPE_PRINT) {
        auto tab = dynamic_cast<TabPrintModel*>(wxGetApp().params_panel()->get_current_tab());
        if (tab && tab->has_key(opt_key)) {
            tab->activate_option(opt_key, category);
            return;
        }
        wxGetApp().params_panel()->switch_to_global();
    }
    wxGetApp().get_tab(type)->activate_option(opt_key, category);
}

void Sidebar::jump_to_option(size_t selected)
{
    const Search::Option& opt = p->searcher.get_option(selected);
    jump_to_option(opt.opt_key(), opt.type, opt.category);

    // Switch to the Settings NotePad
//    wxGetApp().mainframe->select_tab();
}

// BBS. Move logic from Plater::on_extruders_change() to Sidebar::on_filaments_change().
void Sidebar::on_filaments_change(size_t num_filaments)
{
    auto& choices = combos_filament();

    if (num_filaments == choices.size())
        return;

    if (choices.size() == 1 || num_filaments == 1)
        choices[0]->GetDropDown().Invalidate();

    wxWindowUpdateLocker noUpdates_scrolled_panel(this);

    size_t i = choices.size();
    while (i < num_filaments)
    {
        PlaterPresetComboBox* choice/*{ nullptr }*/;
        init_filament_combo(&choice, i);
        choices.push_back(choice);

        // initialize selection
        choice->update();
        ++i;
    }

    // remove unused choices if any
    remove_unused_filament_combos(num_filaments);

    auto sizer = p->m_panel_filament_title->GetSizer();
    if (p->m_flushing_volume_btn != nullptr && sizer != nullptr) {
        if (num_filaments > 1)
            sizer->Show(p->m_flushing_volume_btn);
        else
            sizer->Hide(p->m_flushing_volume_btn);
    }

    Layout();
    p->m_panel_filament_title->Refresh();
    update_ui_from_settings();
    dynamic_filament_list.update();
}

void Sidebar::on_bed_type_change(BedType bed_type)
{
    // btDefault option is not included in global bed type setting
    int sel_idx = (int)bed_type - 1;
    if (m_bed_type_list != nullptr)
        m_bed_type_list->SetSelection(sel_idx);
}

void Sidebar::load_ams_list(std::string const &device, std::map<std::string, Ams *> const &list)
{
    std::vector<DynamicPrintConfig> filament_ams_list;
    for (auto ams : list) {
        char n = ams.first.front() - '0' + 'A';
        for (auto tray : ams.second->trayList) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                                    << boost::format(": ams %1% tray %2% id %3% color %4%") % ams.first % tray.first % tray.second->setting_id % tray.second->color;
            char t = tray.first.front() - '0' + '1';
            DynamicPrintConfig ams;
            ams.set_key_value("filament_id", new ConfigOptionStrings{tray.second->setting_id});
            ams.set_key_value("filament_type", new ConfigOptionStrings{tray.second->type});
            ams.set_key_value("tray_name", new ConfigOptionStrings{std::string(1, n) + std::string(1, t)});
            ams.set_key_value("filament_colour", new ConfigOptionStrings{"#" + tray.second->color.substr(0, 6)});
            filament_ams_list.emplace_back(std::move(ams));
        }
    }
    p->ams_list_device = device;
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": %1% items") % filament_ams_list.size();
    wxGetApp().preset_bundle->filament_ams_list = filament_ams_list;
    for (auto c : p->combos_filament)
        c->update();
}

void Sidebar::sync_ams_list()
{
    auto & list = wxGetApp().preset_bundle->filament_ams_list;
    if (list.empty()) {
        MessageDialog dlg(this,
            _L("No AMS filaments. Please select a printer in 'Device' page to load AMS info."),
            _L("Sync filaments with AMS"), wxOK);
        dlg.ShowModal();
        return;
    }
    std::string ams_filament_ids = wxGetApp().app_config->get("ams_filament_ids", p->ams_list_device);
    std::vector<std::string> list2;
    if (!ams_filament_ids.empty())
        boost::algorithm::split(list2, ams_filament_ids, boost::algorithm::is_any_of(","));
    struct SyncAmsDialog : MessageDialog {
        SyncAmsDialog(wxWindow * parent, bool first): MessageDialog(parent,
            first
                ? _L("Sync filaments with AMS will drop all current selected filament presets and colors. Do you want to continue?")
                : _L("Already did a synchronization, do you want to sync only changes or resync all?"),
            _L("Sync filaments with AMS"), 0)
        {
            if (first) {
                add_button(wxID_YES, true, _L("Yes"));
            } else {
                add_button(wxID_OK, true, _L("Sync"));
                add_button(wxID_YES, false, _L("Resync"));
            }
            add_button(wxID_CANCEL, false, _L("Cancel"));
        }
    } dlg(this, ams_filament_ids.empty());
    auto res = dlg.ShowModal();
    if (res == wxID_CANCEL) return;
    list2.resize(list.size());
    for (int i = 0; i < list.size(); ++i) {
        auto filament_id = list[i].opt_string("filament_id", 0u);
        list[i].set_key_value("filament_changed", new ConfigOptionBool{res == wxID_YES || list2[i] != filament_id});
        list2[i] = filament_id;
    }
    unsigned int unknowns = 0;
    auto n = wxGetApp().preset_bundle->sync_ams_list(unknowns);
    if (n == 0) {
        MessageDialog dlg(this,
            _L("There are no compatible filaments, and sync is not performed."),
            _L("Sync filaments with AMS"), wxOK);
        dlg.ShowModal();
        return;
    }
    ams_filament_ids = boost::algorithm::join(list2, ",");
    wxGetApp().app_config ->set("ams_filament_ids", p->ams_list_device, ams_filament_ids);
    if (unknowns > 0) {
        MessageDialog dlg(this,
            _L("There are some unknown filaments mapped to generic preset. Please update Orca Slicer or restart Orca Slicer to check if there is an update to system presets."),
            _L("Sync filaments with AMS"), wxOK);
        dlg.ShowModal();
    }
    wxGetApp().plater()->on_filaments_change(n);
    for (auto &c : p->combos_filament)
        c->update();
    wxGetApp().get_tab(Preset::TYPE_FILAMENT)->select_preset(wxGetApp().preset_bundle->filament_presets[0]);
    wxGetApp().preset_bundle->export_selections(*wxGetApp().app_config);
    dynamic_filament_list.update();
}

ObjectList* Sidebar::obj_list()
{
    // BBS
    //return obj_list();
    return p->m_object_list;
}

ObjectSettings* Sidebar::obj_settings()
{
    return p->object_settings;
}

wxPanel* Sidebar::scrolled_panel()
{
    return p->scrolled;
}

wxPanel* Sidebar::print_panel()
{
    return p->m_panel_print_content;
}

wxPanel* Sidebar::filament_panel()
{
    return p->m_panel_filament_content;
}

ConfigOptionsGroup* Sidebar::og_freq_chng_params(const bool is_fff)
{
    // BBS
#if 0
    return p->frequently_changed_parameters->get_og(is_fff);
#endif
    return NULL;
}

wxButton* Sidebar::get_wiping_dialog_button()
{
#if 0
    return p->frequently_changed_parameters->get_wiping_dialog_button();
#endif
    return NULL;
}

void Sidebar::enable_buttons(bool enable)
{
#if 0
    p->btn_reslice->Enable(enable);
    p->btn_export_gcode->Enable(enable);
    p->btn_send_gcode->Enable(enable);
//    p->btn_eject_device->Enable(enable);
    p->btn_export_gcode_removable->Enable(enable);
#endif
}

bool Sidebar::show_reslice(bool show)          const { return p->btn_reslice->Show(show); }
bool Sidebar::show_export(bool show)           const { return p->btn_export_gcode->Show(show); }
bool Sidebar::show_send(bool show)             const { return p->btn_send_gcode->Show(show); }
bool Sidebar::show_export_removable(bool show) const { return p->btn_export_gcode_removable->Show(show); }
//bool Sidebar::show_eject(bool show)            const { return p->btn_eject_device->Show(show); }
//bool Sidebar::get_eject_shown()                const { return p->btn_eject_device->IsShown(); }

bool Sidebar::is_multifilament()
{
    return p->combos_filament.size() > 1;
}

static std::vector<Search::InputInfo> get_search_inputs(ConfigOptionMode mode)
{
    std::vector<Search::InputInfo> ret {};

    auto& tabs_list = wxGetApp().tabs_list;
    auto print_tech = wxGetApp().preset_bundle->printers.get_selected_preset().printer_technology();
    for (auto tab : tabs_list)
        if (tab->supports_printer_technology(print_tech))
            ret.emplace_back(Search::InputInfo {tab->get_config(), tab->type(), mode});

    return ret;
}

void Sidebar::update_searcher()
{
    p->searcher.init(get_search_inputs(m_mode));
}

void Sidebar::update_mode()
{
    m_mode = wxGetApp().get_mode();

    //BBS: remove print related combos
    update_searcher();

    wxWindowUpdateLocker noUpdates(this);

    // BBS
    //obj_list()->get_sizer()->Show(m_mode > comSimple);

    obj_list()->unselect_objects();
    obj_list()->update_selections();
//    obj_list()->update_object_menu();

    Layout();
}

bool Sidebar::is_collapsed() { return p->is_collapsed; }

void Sidebar::collapse(bool collapse)
{
    p->is_collapsed = collapse;

    this->Show(!collapse);
    p->plater->Layout();

    // save collapsing state to the AppConfig
    //if (wxGetApp().is_editor())
    //    wxGetApp().app_config->set_bool("collapsed_sidebar", collapse);
}

#ifdef _MSW_DARK_MODE
void Sidebar::show_mode_sizer(bool show)
{
    //p->mode_sizer->Show(show);
}
#endif

void Sidebar::update_ui_from_settings()
{
    // BBS
    //p->object_manipulation->update_ui_from_settings();
    // update Cut gizmo, if it's open
    p->plater->canvas3D()->update_gizmos_on_off_state();
    p->plater->set_current_canvas_as_dirty();
    p->plater->get_current_canvas3D()->request_extra_frame();
#if 0
    p->object_list->apply_volumes_order();
#endif
}

bool Sidebar::show_object_list(bool show) const
{
    if (!p->m_object_list->Show(show))
        return false;
    p->scrolled->Layout();
    return true;
}

void Sidebar::finish_param_edit() { p->editing_filament = -1; }

std::vector<PlaterPresetComboBox*>& Sidebar::combos_filament()
{
    return p->combos_filament;
}

Search::OptionsSearcher& Sidebar::get_searcher()
{
    return p->searcher;
}

std::string& Sidebar::get_search_line()
{
    return p->searcher.search_string();
}

// Plater::DropTarget

class PlaterDropTarget : public wxFileDropTarget
{
public:
    PlaterDropTarget(Plater* plater) : m_plater(plater) { this->SetDefaultAction(wxDragCopy); }

    virtual bool OnDropFiles(wxCoord x, wxCoord y, const wxArrayString &filenames);

    void handleOnIdle(wxIdleEvent & event);

private:
    Plater* m_plater;
    wxArrayString m_filenames;
};

bool PlaterDropTarget::OnDropFiles(wxCoord x, wxCoord y, const wxArrayString &filenames)
{
#ifdef WIN32
    // hides the system icon
    this->MSWUpdateDragImageOnLeave();
#endif // WIN32

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": drag %1% files into app")%filenames.size();
    m_filenames = filenames;
    wxGetApp().Bind(wxEVT_IDLE, &PlaterDropTarget::handleOnIdle, this);
    return true;
}

void PlaterDropTarget::handleOnIdle(wxIdleEvent &event)
{
    wxGetApp().mainframe->Raise();
    wxGetApp().Unbind(wxEVT_IDLE, &PlaterDropTarget::handleOnIdle, this);
    if (m_plater != nullptr) {
        m_plater->load_files(m_filenames);
        wxGetApp().mainframe->update_title();
    }
    //m_filenames.clear();
}

// State to manage showing after export notifications and device ejecting
enum ExportingStatus{
    NOT_EXPORTING,
    EXPORTING_TO_REMOVABLE,
    EXPORTING_TO_LOCAL
};

// Plater / private
struct Plater::priv
{
    // PIMPL back pointer ("Q-Pointer")
    Plater *q;
    MainFrame *main_frame;

    MenuFactory menus;

    SelectMachineDialog* m_select_machine_dlg = nullptr;
    SendToPrinterDialog* m_send_to_sdcard_dlg = nullptr;
    PublishDialog *m_publish_dlg = nullptr;

    // Data
    Slic3r::DynamicPrintConfig *config;        // FIXME: leak?
    Slic3r::Print               fff_print;
    Slic3r::SLAPrint            sla_print;
    Slic3r::Model               model;
    PrinterTechnology           printer_technology = ptFFF;
    Slic3r::GCodeProcessorResult gcode_result;

    // GUI elements
    wxSizer* panel_sizer{ nullptr };
    wxPanel* current_panel{ nullptr };
    std::vector<wxPanel*> panels;
    Sidebar *sidebar;
    Bed3D bed;
    Camera camera;
    //BBS: partplate related structure
    PartPlateList partplate_list;
    //BBS: add a flag to ignore cancel event
    bool m_ignore_event{false};
    bool m_slice_all{false};
    bool m_is_slicing {false};
    bool m_is_publishing {false};
    int m_cur_slice_plate;
    //BBS: m_slice_all in .gcode.3mf file case, set true when slice all
    bool m_slice_all_only_has_gcode{ false };

    bool m_need_update{false};
    //BBS: add popup object table logic
    //ObjectTableDialog* m_popup_table{ nullptr };

#if ENABLE_ENVIRONMENT_MAP
    GLTexture environment_texture;
#endif // ENABLE_ENVIRONMENT_MAP
    Mouse3DController mouse3d_controller;
    View3D* view3D;
    // BBS
    //GLToolbar view_toolbar;
    GLToolbar collapse_toolbar;
    Preview *preview;
    AssembleView* assemble_view { nullptr };
    bool first_enter_assemble{ true };
    std::unique_ptr<NotificationManager> notification_manager;

    ProjectDirtyStateManager dirty_state;

    BackgroundSlicingProcess    background_process;
    bool suppressed_backround_processing_update { false };

    // Jobs defined inside the group class will be managed so that only one can
    // run at a time. Also, the background process will be stopped if a job is
    // started. It is up the the plater to ensure that the background slicing
    // can't be restarted while a ui job is still running.
    class Jobs: public ExclusiveJobGroup
    {
        priv *m;
        size_t m_arrange_id, m_fill_bed_id, m_rotoptimize_id, m_sla_import_id, m_orient_id;
        std::shared_ptr<NotificationProgressIndicator> m_pri;
        //BBS
        size_t m_print_id;

        void before_start() override { m->background_process.stop(); }

    public:
        Jobs(priv *_m) :
            m(_m),
            m_pri{std::make_shared<NotificationProgressIndicator>(m->notification_manager.get())}
        {
            m_arrange_id = add_job(std::make_unique<ArrangeJob>(m_pri, m->q));
            m_orient_id = add_job(std::make_unique<OrientJob>(m_pri, m->q));
            m_fill_bed_id = add_job(std::make_unique<FillBedJob>(m_pri, m->q));
            m_rotoptimize_id = add_job(std::make_unique<RotoptimizeJob>(m_pri, m->q));
            m_sla_import_id = add_job(std::make_unique<SLAImportJob>(m_pri, m->q));
            //BBS add print id
            m_print_id = add_job(std::make_unique<PrintJob>(m_pri, m->q));
        }

        void arrange()
        {
            m->take_snapshot("Arrange");
            start(m_arrange_id);
        }

        void orient()
        {
            m->take_snapshot("Orient");
            start(m_orient_id);
        }

        void fill_bed()
        {
            m->take_snapshot("Fill bed");
            start(m_fill_bed_id);
        }

        void optimize_rotation()
        {
            m->take_snapshot("Optimize Rotation");
            start(m_rotoptimize_id);
        }

        void import_sla_arch()
        {
            m->take_snapshot("Import SLA archive");
            start(m_sla_import_id);
        }

        //BBS bbl printing job
        void print()
        {
            start(m_print_id);
        }
    } m_ui_jobs;

    int                         m_job_prepare_state;

    bool                        delayed_scene_refresh;
    std::string                 delayed_error_message;

    wxTimer                     background_process_timer;

    std::string                 label_btn_export;
    std::string                 label_btn_send;

    bool                        show_render_statistic_dialog{ false };
    bool                        show_wireframe{ false };
    bool                        wireframe_enabled{ true };

    static const std::regex pattern_bundle;
    static const std::regex pattern_3mf;
    static const std::regex pattern_zip_amf;
    static const std::regex pattern_any_amf;
    static const std::regex pattern_prusa;

    bool m_is_dark = false;

    priv(Plater *q, MainFrame *main_frame);
    ~priv();


    bool need_update() const { return m_need_update; }
    void set_need_update(bool need_update) { m_need_update = need_update; }

    void set_plater_dirty(bool is_dirty) { dirty_state.set_plater_dirty(is_dirty); }
    bool is_project_dirty() const { return dirty_state.is_dirty(); }
    bool is_presets_dirty() const { return dirty_state.is_presets_dirty(); }
    void update_project_dirty_from_presets()
    {
        // BBS: backup
        Slic3r::put_other_changes();
        dirty_state.update_from_presets();
    }
    int save_project_if_dirty(const wxString& reason) {
        int res = wxID_NO;
        if (dirty_state.is_dirty()) {
            MainFrame* mainframe = wxGetApp().mainframe;
            if (mainframe->can_save_as()) {
                wxString suggested_project_name;
                wxString project_name = suggested_project_name = get_project_filename(".3mf");
                if (suggested_project_name.IsEmpty()) {
                    fs::path output_file = get_export_file_path(FT_3MF);
                    suggested_project_name = output_file.empty() ? _L("Untitled") : from_u8(output_file.stem().string());
                }
                res = MessageDialog(mainframe, reason + "\n" + format_wxstr(_L("Do you want to save changes to \"%1%\"?"), suggested_project_name),
                                    wxString(SLIC3R_APP_FULL_NAME), wxYES_NO | wxCANCEL).ShowModal();
                if (res == wxID_YES)
                    if (!mainframe->save_project_as(project_name))
                        res = wxID_CANCEL;
            }
        }
        return res;
    }
    void reset_project_dirty_after_save() { m_undo_redo_stack_main.mark_current_as_saved(); dirty_state.reset_after_save(); }
    void reset_project_dirty_initial_presets() { dirty_state.reset_initial_presets(); }

#if ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW
    void render_project_state_debug_window() const { dirty_state.render_debug_window(); }
#endif // ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW

    enum class UpdateParams {
        FORCE_FULL_SCREEN_REFRESH          = 1,
        FORCE_BACKGROUND_PROCESSING_UPDATE = 2,
        POSTPONE_VALIDATION_ERROR_MESSAGE  = 4,
    };
    void update(unsigned int flags = 0);
    void select_view(const std::string& direction);
    //BBS: add no_slice option
    void select_view_3D(const std::string& name, bool no_slice = true);
    void select_next_view_3D();

    bool is_preview_shown() const { return current_panel == preview; }
    bool is_preview_loaded() const { return preview->is_loaded(); }
    bool is_view3D_shown() const { return current_panel == view3D; }
    bool is_assemble_view_show() const { return current_panel == assemble_view; }

    bool are_view3D_labels_shown() const { return (current_panel == view3D) && view3D->get_canvas3d()->are_labels_shown(); }
    void show_view3D_labels(bool show) { if (current_panel == view3D) view3D->get_canvas3d()->show_labels(show); }

    bool is_sidebar_collapsed() const   { return sidebar->is_collapsed(); }
    void collapse_sidebar(bool collapse);

    bool is_view3D_layers_editing_enabled() const { return (current_panel == view3D) && view3D->get_canvas3d()->is_layers_editing_enabled(); }

    void set_current_canvas_as_dirty();
    GLCanvas3D* get_current_canvas3D(bool exclude_preview = false);
    void unbind_canvas_event_handlers();
    void reset_canvas_volumes();

    // BBS
    bool init_collapse_toolbar();

    // BBS
    void hide_select_machine_dlg() { m_select_machine_dlg->EndModal(wxID_OK); }
    void hide_send_to_printer_dlg() { m_send_to_sdcard_dlg->EndModal(wxID_OK); }

    void update_preview_bottom_toolbar();

    void reset_gcode_toolpaths();

    void reset_all_gizmos();
    void apply_free_camera_correction(bool apply = true);
    void update_ui_from_settings();
    // BBS
    std::shared_ptr<BBLStatusBar> statusbar();
    std::string get_config(const std::string &key) const;
    BoundingBoxf bed_shape_bb() const;
    BoundingBox scaled_bed_shape_bb() const;

    // BBS: backup & restore
    std::vector<size_t> load_files(const std::vector<fs::path>& input_files, LoadStrategy strategy, bool ask_multi = false);
    std::vector<size_t> load_model_objects(const ModelObjectPtrs& model_objects, bool allow_negative_z = false, bool split_object = false);

    fs::path get_export_file_path(GUI::FileType file_type);
    wxString get_export_file(GUI::FileType file_type);

    // BBS
    void load_auxiliary_files();

    const Selection& get_selection() const;
    Selection& get_selection();
    Selection& get_curr_selection();

    int get_selected_object_idx() const;
    int get_selected_volume_idx() const;
    void selection_changed();
    void object_list_changed();

    // BBS
    void select_curr_plate_all();
    void remove_curr_plate_all();

    void select_all();
    void deselect_all();
    void remove(size_t obj_idx);
    void delete_object_from_model(size_t obj_idx, bool refresh_immediately = true); //BBS
    void delete_all_objects_from_model();
    void reset(bool apply_presets_change = false);
    void center_selection();
    void mirror(Axis axis);
    void split_object();
    void split_volume();
    void scale_selection_to_fit_print_volume();

    // Return the active Undo/Redo stack. It may be either the main stack or the Gimzo stack.
    Slic3r::UndoRedo::Stack& undo_redo_stack() { assert(m_undo_redo_stack_active != nullptr); return *m_undo_redo_stack_active; }
    Slic3r::UndoRedo::Stack& undo_redo_stack_main() { return m_undo_redo_stack_main; }
    void enter_gizmos_stack();
    bool leave_gizmos_stack();

    void take_snapshot(const std::string& snapshot_name, UndoRedo::SnapshotType snapshot_type = UndoRedo::SnapshotType::Action);
    /*void take_snapshot(const wxString& snapshot_name, UndoRedo::SnapshotType snapshot_type = UndoRedo::SnapshotType::Action)
        { this->take_snapshot(std::string(snapshot_name.ToUTF8().data()), snapshot_type); }*/
    int  get_active_snapshot_index();

    void undo();
    void redo();
    void undo_redo_to(size_t time_to_load);

    // BBS: backup
    bool up_to_date(bool saved, bool backup);

    void suppress_snapshots()   { m_prevent_snapshots++; }
    void allow_snapshots()      { m_prevent_snapshots--; }
    // BBS: single snapshot
    void single_snapshots_enter(SingleSnapshot *single)
    {
        if (m_single == nullptr) m_single = single;
    }
    void single_snapshots_leave(SingleSnapshot *single)
    {
        if (m_single == single) m_single = nullptr;
    }

    void process_validation_warning(StringObjectException const &warning) const;

    bool background_processing_enabled() const {
#ifdef SUPPORT_BACKGROUND_PROCESSING
        return this->get_config("background_processing") == "1";
#else
        return false;
#endif
    }
    void update_print_volume_state();
    void schedule_background_process();
    // Update background processing thread from the current config and Model.
    enum UpdateBackgroundProcessReturnState {
        // update_background_process() reports, that the Print / SLAPrint was updated in a way,
        // that the background process was invalidated and it needs to be re-run.
        UPDATE_BACKGROUND_PROCESS_RESTART = 1,
        // update_background_process() reports, that the Print / SLAPrint was updated in a way,
        // that a scene needs to be refreshed (you should call _3DScene::reload_scene(canvas3Dwidget, false))
        UPDATE_BACKGROUND_PROCESS_REFRESH_SCENE = 2,
        // update_background_process() reports, that the Print / SLAPrint is invalid, and the error message
        // was sent to the status line.
        UPDATE_BACKGROUND_PROCESS_INVALID = 4,
        // Restart even if the background processing is disabled.
        UPDATE_BACKGROUND_PROCESS_FORCE_RESTART = 8,
        // Restart for G-code (or SLA zip) export or upload.
        UPDATE_BACKGROUND_PROCESS_FORCE_EXPORT = 16,
    };
    // returns bit mask of UpdateBackgroundProcessReturnState
    unsigned int update_background_process(bool force_validation = false, bool postpone_error_messages = false, bool switch_print = true);
    // Restart background processing thread based on a bitmask of UpdateBackgroundProcessReturnState.
    bool restart_background_process(unsigned int state);
    // returns bit mask of UpdateBackgroundProcessReturnState
    unsigned int update_restart_background_process(bool force_scene_update, bool force_preview_update);
    void show_delayed_error_message() {
        if (!this->delayed_error_message.empty()) {
            std::string msg = std::move(this->delayed_error_message);
            this->delayed_error_message.clear();
            GUI::show_error(this->q, msg);
        }
    }
    void export_gcode(fs::path output_path, bool output_path_on_removable_media);
    void export_gcode(fs::path output_path, bool output_path_on_removable_media, PrintHostJob upload_job);

    void reload_from_disk();
    bool replace_volume_with_stl(int object_idx, int volume_idx, const fs::path& new_path, const std::string& snapshot = "");
    void replace_with_stl();
    void reload_all_from_disk();

    //BBS: add no_slice option
    void set_current_panel(wxPanel* panel, bool no_slice = true);

    void on_combobox_select(wxCommandEvent&);
    void on_select_bed_type(wxCommandEvent&);
    void on_select_preset(wxCommandEvent&);
    void on_slicing_update(SlicingStatusEvent&);
    void on_slicing_completed(wxCommandEvent&);
    void on_process_completed(SlicingProcessCompletedEvent&);
    void on_export_began(wxCommandEvent&);
    void on_export_finished(wxCommandEvent&);
    void on_slicing_began();

    void clear_warnings();
    void add_warning(const Slic3r::PrintStateBase::Warning &warning, size_t oid);
    // Update notification manager with the current state of warnings produced by the background process (slicing).
    void actualize_slicing_warnings(const PrintBase &print);
    void actualize_object_warnings(const PrintBase& print);
    // Displays dialog window with list of warnings.
    // Returns true if user clicks OK.
    // Returns true if current_warnings vector is empty without showning the dialog
    bool warnings_dialog();

    void on_action_add(SimpleEvent&);
    void on_action_add_plate(SimpleEvent&);
    void on_action_del_plate(SimpleEvent&);
    void on_action_split_objects(SimpleEvent&);
    void on_action_split_volumes(SimpleEvent&);
    void on_action_layersediting(SimpleEvent&);

    void on_object_select(SimpleEvent&);
    void on_right_click(RBtnEvent&);
    //BBS: add model repair
    void on_repair_model(wxCommandEvent &event);
    void on_filament_color_changed(wxCommandEvent &event);
    void show_install_plugin_hint(wxCommandEvent &event);
    void install_network_plugin(wxCommandEvent &event);
    void show_preview_only_hint(wxCommandEvent &event);
    //BBS: add part plate related logic
    void on_plate_right_click(RBtnPlateEvent&);
    void on_plate_selected(SimpleEvent&);
    void on_action_request_model_id(wxCommandEvent& evt);
    void on_action_download_project(wxCommandEvent& evt);
    void on_slice_button_status(bool enable);
    //BBS: GUI refactor: GLToolbar
    void on_action_open_project(SimpleEvent&);
    void on_action_slice_plate(SimpleEvent&);
    void on_action_slice_all(SimpleEvent&);
    void on_action_publish(wxCommandEvent &evt);
    void on_action_print_plate(SimpleEvent&);
    void on_action_print_all(SimpleEvent&);
    void on_action_export_gcode(SimpleEvent&);
    void on_action_send_gcode(SimpleEvent&);
    void on_action_export_sliced_file(SimpleEvent&);
    void on_action_export_all_sliced_file(SimpleEvent&);
    void on_action_select_sliced_plate(wxCommandEvent& evt);
    //BBS: change dark/light mode
    void on_change_color_mode(SimpleEvent& evt);
    void on_apple_change_color_mode(wxSysColourChangedEvent& evt);
    void on_update_geometry(Vec3dsEvent<2>&);
    void on_3dcanvas_mouse_dragging_started(SimpleEvent&);
    void on_3dcanvas_mouse_dragging_finished(SimpleEvent&);

    //void show_action_buttons(const bool is_ready_to_slice) const;
    bool show_publish_dlg(bool show = true);
    void update_publish_dialog_status(wxString &msg, int percent = -1);

    // Set the bed shape to a single closed 2D polygon(array of two element arrays),
    // triangulate the bed and store the triangles into m_bed.m_triangles,
    // fills the m_bed.m_grid_lines and sets m_bed.m_origin.
    // Sets m_bed.m_polygon to limit the object placement.
    //BBS: add bed exclude area
    void set_bed_shape(const Pointfs& shape, const Pointfs& exclude_areas, const double printable_height, const std::string& custom_texture, const std::string& custom_model, bool force_as_custom = false);

    bool can_delete() const;
    bool can_delete_all() const;
    bool can_edit_text() const;
    bool can_add_plate() const;
    bool can_delete_plate() const;
    bool can_increase_instances() const;
    bool can_decrease_instances() const;
    bool can_split_to_objects() const;
    bool can_split_to_volumes() const;
    bool can_arrange() const;
    bool can_layers_editing() const;
    bool can_fix_through_netfabb() const;
    bool can_simplify() const;
    bool can_set_instance_to_object() const;
    bool can_mirror() const;
    bool can_reload_from_disk() const;
    //BBS:
    bool can_fillcolor() const;
    bool has_assemble_view() const;
    bool can_replace_with_stl() const;
    bool can_split(bool to_objects) const;
#if ENABLE_ENHANCED_PRINT_VOLUME_FIT
    bool can_scale_to_print_volume() const;
#endif // ENABLE_ENHANCED_PRINT_VOLUME_FIT

    //BBS: add plate_id for thumbnail
    void generate_thumbnail(ThumbnailData& data, unsigned int w, unsigned int h, const ThumbnailsParams& thumbnail_params, Camera::EType camera_type);
    ThumbnailsList generate_thumbnails(const ThumbnailsParams& params, Camera::EType camera_type);
    //BBS
    void generate_calibration_thumbnail(ThumbnailData& data, unsigned int w, unsigned int h, const ThumbnailsParams& thumbnail_params);
    PlateBBoxData generate_first_layer_bbox();

    void bring_instance_forward() const;

    // returns the path to project file with the given extension (none if extension == wxEmptyString)
    // extension should contain the leading dot, i.e.: ".3mf"
    wxString get_project_filename(const wxString& extension = wxEmptyString) const;
    wxString get_export_gcode_filename(const wxString& extension = wxEmptyString, bool only_filename = false, bool export_all = false) const;
    void set_project_filename(const wxString& filename);

    //BBS store bbs project name
    wxString get_project_name();
    void set_project_name(const wxString& project_name);

    // Call after plater and Canvas#D is initialized
    void init_notification_manager();

    // Caching last value of show_action_buttons parameter for show_action_buttons(), so that a callback which does not know this state will not override it.
    //mutable bool    			ready_to_slice = { false };
    // Flag indicating that the G-code export targets a removable device, therefore the show_action_buttons() needs to be called at any case when the background processing finishes.
    ExportingStatus             exporting_status { NOT_EXPORTING };
    std::string                 last_output_path;
    std::string                 last_output_dir_path;
    //BBS store machine_sn and 3mf_path for PrintJob
    PrintPrepareData            m_print_job_data;
    bool                        inside_snapshot_capture() { return m_prevent_snapshots != 0; }
    int                         process_completed_with_error { -1 }; //-1 means no error

    //BBS: project
    BBLProject                  project;

    //BBS: add print project related logic
    void update_fff_scene_only_shells(bool only_shells = true);
    //BBS: add popup object table logic
    bool PopupObjectTable(int object_id, int volume_id, const wxPoint& position);
    void on_action_send_to_printer(bool isall = false);
private:
    bool layers_height_allowed() const;

    void update_fff_scene();
    void update_sla_scene();

    void undo_redo_to(std::vector<UndoRedo::Snapshot>::const_iterator it_snapshot);
    void update_after_undo_redo(const UndoRedo::Snapshot& snapshot, bool temp_snapshot_was_taken = false);
    void on_action_export_to_sdcard(SimpleEvent&);
    void on_action_export_to_sdcard_all(SimpleEvent&);
    void update_plugin_when_launch(wxCommandEvent& event);
    // path to project folder stored with no extension
    boost::filesystem::path     m_project_folder;

    /* display project name */
    wxString                    m_project_name;

    Slic3r::UndoRedo::Stack 	m_undo_redo_stack_main;
    Slic3r::UndoRedo::Stack 	m_undo_redo_stack_gizmos;
    Slic3r::UndoRedo::Stack    *m_undo_redo_stack_active = &m_undo_redo_stack_main;
    int                         m_prevent_snapshots = 0;     /* Used for avoid of excess "snapshoting".
                                                              * Like for "delete selected" or "set numbers of copies"
                                                              * we should call tack_snapshot just ones
                                                              * instead of calls for each action separately
                                                              * */
    // BBS: single snapshot
    Plater::SingleSnapshot     *m_single = nullptr;
    // BBS: backup
    size_t m_saved_timestamp = 0;
    size_t m_backup_timestamp = 0;
    std::string 				m_last_fff_printer_profile_name;
    std::string 				m_last_sla_printer_profile_name;

    // vector of all warnings generated by last slicing
    std::vector<std::pair<Slic3r::PrintStateBase::Warning, size_t>> current_warnings;
    bool show_warning_dialog { false };

};

const std::regex Plater::priv::pattern_bundle(".*[.](amf|amf[.]xml|zip[.]amf|3mf)", std::regex::icase);
const std::regex Plater::priv::pattern_3mf(".*3mf", std::regex::icase);
const std::regex Plater::priv::pattern_zip_amf(".*[.]zip[.]amf", std::regex::icase);
const std::regex Plater::priv::pattern_any_amf(".*[.](amf|amf[.]xml|zip[.]amf)", std::regex::icase);
const std::regex Plater::priv::pattern_prusa(".*bbl", std::regex::icase);

Plater::priv::priv(Plater *q, MainFrame *main_frame)
    : q(q)
    , main_frame(main_frame)
    //BBS: add bed_exclude_area
    , config(Slic3r::DynamicPrintConfig::new_from_defaults_keys({
        "printable_area", "bed_exclude_area", "bed_custom_texture", "bed_custom_model", "print_sequence",
        "extruder_clearance_radius", "extruder_clearance_max_radius",
        "extruder_clearance_height_to_lid", "extruder_clearance_height_to_rod", "skirt_loops", "skirt_distance",
        "brim_width", "brim_object_gap", "brim_type", "nozzle_diameter", "single_extruder_multi_material",
        "enable_prime_tower", "wipe_tower_x", "wipe_tower_y", "prime_tower_width", "prime_tower_brim_width", "prime_volume",
        "extruder_colour", "filament_colour", "material_colour", "printable_height", "printer_model", "printer_technology",
        // These values are necessary to construct SlicingParameters by the Canvas3D variable layer height editor.
        "layer_height", "initial_layer_print_height", "min_layer_height", "max_layer_height",
        "brim_width", "wall_loops", "wall_filament", "sparse_infill_density", "sparse_infill_filament", "top_shell_layers",
        "enable_support", "support_filament", "support_interface_filament",
        "support_top_z_distance", "support_bottom_z_distance", "raft_layers"
        }))
    , sidebar(new Sidebar(q))
    , notification_manager(std::make_unique<NotificationManager>(q))
    , m_ui_jobs(this)
    , m_job_prepare_state(Job::JobPrepareState::PREPARE_STATE_DEFAULT)
    , delayed_scene_refresh(false)
    , collapse_toolbar(GLToolbar::Normal, "Collapse")
    //BBS :partplatelist construction
    , partplate_list(this->q, &model)
{
    this->q->SetFont(Slic3r::GUI::wxGetApp().normal_font());

    //BBS: use the first partplate's print for background process
    partplate_list.update_slice_context_to_current_plate(background_process);
    /*
    background_process.set_fff_print(&fff_print);
    background_process.set_sla_print(&sla_print);
    background_process.set_gcode_result(&gcode_result);
    background_process.set_thumbnail_cb([this](const ThumbnailsParams& params) { return this->generate_thumbnails(params, Camera::EType::Ortho); });
    background_process.set_slicing_completed_event(EVT_SLICING_COMPLETED);
    background_process.set_finished_event(EVT_PROCESS_COMPLETED);
    background_process.set_export_began_event(EVT_EXPORT_BEGAN);
    // Default printer technology for default config.
    background_process.select_technology(this->printer_technology);
    // Register progress callback from the Print class to the Plater.

    auto statuscb = [this](const Slic3r::PrintBase::SlicingStatus &status) {
        wxQueueEvent(this->q, new Slic3r::SlicingStatusEvent(EVT_SLICING_UPDATE, 0, status));
    };
    fff_print.set_status_callback(statuscb);
    sla_print.set_status_callback(statuscb); */

    // BBS: to be checked. Not follow patch.
    background_process.set_thumbnail_cb([this](const ThumbnailsParams& params) { return this->generate_thumbnails(params, Camera::EType::Ortho); });
    background_process.set_slicing_completed_event(EVT_SLICING_COMPLETED);
    background_process.set_finished_event(EVT_PROCESS_COMPLETED);
    background_process.set_export_began_event(EVT_EXPORT_BEGAN);
    background_process.set_export_finished_event(EVT_EXPORT_FINISHED);
    this->q->Bind(EVT_SLICING_UPDATE, &priv::on_slicing_update, this);
    this->q->Bind(EVT_PUBLISH, &priv::on_action_publish, this);
    this->q->Bind(EVT_REPAIR_MODEL, &priv::on_repair_model, this);
    this->q->Bind(EVT_FILAMENT_COLOR_CHANGED, &priv::on_filament_color_changed, this);
    this->q->Bind(EVT_INSTALL_PLUGIN_NETWORKING, &priv::install_network_plugin, this);
    this->q->Bind(EVT_INSTALL_PLUGIN_HINT, &priv::show_install_plugin_hint, this);
    this->q->Bind(EVT_UPDATE_PLUGINS_WHEN_LAUNCH, &priv::update_plugin_when_launch, this);
    this->q->Bind(EVT_PREVIEW_ONLY_MODE_HINT, &priv::show_preview_only_hint, this);
    this->q->Bind(EVT_GLCANVAS_COLOR_MODE_CHANGED, &priv::on_change_color_mode, this);
    this->q->Bind(wxEVT_SYS_COLOUR_CHANGED, &priv::on_apple_change_color_mode, this);

    view3D = new View3D(q, bed, &model, config, &background_process);
    //BBS: use partplater's gcode
    preview = new Preview(q, bed, &model, config, &background_process, partplate_list.get_current_slice_result(), [this]() { schedule_background_process(); });

    assemble_view = new AssembleView(q, bed, &model, config, &background_process);

#ifdef __APPLE__
    // BBS
    // set default view_toolbar icons size equal to GLGizmosManager::Default_Icons_Size
    //view_toolbar.set_icons_size(GLGizmosManager::Default_Icons_Size);
#endif // __APPLE__

    panels.push_back(view3D);
    panels.push_back(preview);
    panels.push_back(assemble_view);

    this->background_process_timer.SetOwner(this->q, 0);
    this->q->Bind(wxEVT_TIMER, [this](wxTimerEvent &evt)
    {
        if (!this->suppressed_backround_processing_update)
            this->update_restart_background_process(false, false);
    });

    update();

    auto* hsizer = new wxBoxSizer(wxHORIZONTAL);
    auto* vsizer = new wxBoxSizer(wxVERTICAL);

    // BBS: move sidebar to left side
    hsizer->Add(sidebar, 0, wxEXPAND | wxLEFT | wxRIGHT, 0);
    auto spliter_1 = new ::StaticLine(q, true);
    spliter_1->SetLineColour("#A6A9AA");
    hsizer->Add(spliter_1, 0, wxEXPAND);

    panel_sizer = new wxBoxSizer(wxHORIZONTAL);
    panel_sizer->Add(view3D, 1, wxEXPAND | wxALL, 0);
    panel_sizer->Add(preview, 1, wxEXPAND | wxALL, 0);
    panel_sizer->Add(assemble_view, 1, wxEXPAND | wxALL, 0);
    vsizer->Add(panel_sizer, 1, wxEXPAND | wxALL, 0);
    hsizer->Add(vsizer, 1, wxEXPAND | wxALL, 0);

    q->SetSizer(hsizer);

    menus.init(q);


    // Events:

    if (wxGetApp().is_editor()) {
        // Preset change event
        sidebar->Bind(wxEVT_COMBOBOX, &priv::on_combobox_select, this);
        sidebar->Bind(EVT_OBJ_LIST_OBJECT_SELECT, [this](wxEvent&) { priv::selection_changed(); });
        // BBS: should bind BACKGROUND_PROCESS event to plater
        q->Bind(EVT_SCHEDULE_BACKGROUND_PROCESS, [this](SimpleEvent&) { this->schedule_background_process(); });
        // jump to found option from SearchDialog
        q->Bind(wxCUSTOMEVT_JUMP_TO_OPTION, [this](wxCommandEvent& evt) { sidebar->jump_to_option(evt.GetInt()); });
    }

    wxGLCanvas* view3D_canvas = view3D->get_wxglcanvas();
    //BBS: GUI refactor
    wxGLCanvas* preview_canvas = preview->get_wxglcanvas();

    if (wxGetApp().is_editor()) {
        // 3DScene events:
        view3D_canvas->Bind(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS, [this](SimpleEvent&) {
            delayed_error_message.clear();
            this->background_process_timer.Start(500, wxTIMER_ONE_SHOT);
            });
        view3D_canvas->Bind(EVT_GLCANVAS_OBJECT_SELECT, &priv::on_object_select, this);
        view3D_canvas->Bind(EVT_GLCANVAS_RIGHT_CLICK, &priv::on_right_click, this);
        //BBS: add part plate related logic
        view3D_canvas->Bind(EVT_GLCANVAS_PLATE_RIGHT_CLICK, &priv::on_plate_right_click, this);
        view3D_canvas->Bind(EVT_GLCANVAS_REMOVE_OBJECT, [q](SimpleEvent&) { q->remove_selected(); });
        view3D_canvas->Bind(EVT_GLCANVAS_ARRANGE, [this](SimpleEvent& evt) {
            //BBS arrage from EVT set default state.
            this->q->set_prepare_state(Job::PREPARE_STATE_DEFAULT);
            this->q->arrange(); });
        view3D_canvas->Bind(EVT_GLCANVAS_ARRANGE_PARTPLATE, [this](SimpleEvent& evt) {
            //BBS arrage from EVT set default state.
            this->q->set_prepare_state(Job::PREPARE_STATE_MENU);
            this->q->arrange(); });
        view3D_canvas->Bind(EVT_GLCANVAS_ORIENT, [this](SimpleEvent& evt) {
            //BBS oriant from EVT set default state.
            this->q->set_prepare_state(Job::PREPARE_STATE_DEFAULT);
            this->q->orient(); });
        view3D_canvas->Bind(EVT_GLCANVAS_ORIENT_PARTPLATE, [this](SimpleEvent& evt) {
            //BBS oriant from EVT set default state.
            this->q->set_prepare_state(Job::PREPARE_STATE_MENU);
            this->q->orient(); });
        //BBS
        view3D_canvas->Bind(EVT_GLCANVAS_SELECT_CURR_PLATE_ALL, [this](SimpleEvent&) {this->q->select_curr_plate_all(); });

        view3D_canvas->Bind(EVT_GLCANVAS_SELECT_ALL, [this](SimpleEvent&) { this->q->select_all(); });
        view3D_canvas->Bind(EVT_GLCANVAS_QUESTION_MARK, [](SimpleEvent&) { wxGetApp().keyboard_shortcuts(); });
        view3D_canvas->Bind(EVT_GLCANVAS_INCREASE_INSTANCES, [this](Event<int>& evt)
            { if (evt.data == 1) this->q->increase_instances(); else if (this->can_decrease_instances()) this->q->decrease_instances(); });
        view3D_canvas->Bind(EVT_GLCANVAS_INSTANCE_MOVED, [this](SimpleEvent&) { update(); });
        view3D_canvas->Bind(EVT_GLCANVAS_FORCE_UPDATE, [this](SimpleEvent&) { update(); });
        view3D_canvas->Bind(EVT_GLCANVAS_INSTANCE_ROTATED, [this](SimpleEvent&) { update(); });
        view3D_canvas->Bind(EVT_GLCANVAS_INSTANCE_SCALED, [this](SimpleEvent&) { update(); });
        // BBS
        //view3D_canvas->Bind(EVT_GLCANVAS_ENABLE_ACTION_BUTTONS, [this](Event<bool>& evt) { this->sidebar->enable_buttons(evt.data); });
        view3D_canvas->Bind(EVT_GLCANVAS_ENABLE_ACTION_BUTTONS, [this](Event<bool>& evt) { on_slice_button_status(evt.data); });
        view3D_canvas->Bind(EVT_GLCANVAS_UPDATE_GEOMETRY, &priv::on_update_geometry, this);
        view3D_canvas->Bind(EVT_GLCANVAS_MOUSE_DRAGGING_STARTED, &priv::on_3dcanvas_mouse_dragging_started, this);
        view3D_canvas->Bind(EVT_GLCANVAS_MOUSE_DRAGGING_FINISHED, &priv::on_3dcanvas_mouse_dragging_finished, this);
        view3D_canvas->Bind(EVT_GLCANVAS_TAB, [this](SimpleEvent&) { select_next_view_3D(); });
        view3D_canvas->Bind(EVT_GLCANVAS_RESETGIZMOS, [this](SimpleEvent&) { reset_all_gizmos(); });
        view3D_canvas->Bind(EVT_GLCANVAS_UNDO, [this](SimpleEvent&) { this->undo(); });
        view3D_canvas->Bind(EVT_GLCANVAS_REDO, [this](SimpleEvent&) { this->redo(); });
        view3D_canvas->Bind(EVT_GLCANVAS_COLLAPSE_SIDEBAR, [this](SimpleEvent&) { this->q->collapse_sidebar(!this->q->is_sidebar_collapsed());  });
        view3D_canvas->Bind(EVT_GLCANVAS_RESET_LAYER_HEIGHT_PROFILE, [this](SimpleEvent&) { this->view3D->get_canvas3d()->reset_layer_height_profile(); });
        view3D_canvas->Bind(EVT_GLCANVAS_ADAPTIVE_LAYER_HEIGHT_PROFILE, [this](Event<float>& evt) { this->view3D->get_canvas3d()->adaptive_layer_height_profile(evt.data); });
        view3D_canvas->Bind(EVT_GLCANVAS_SMOOTH_LAYER_HEIGHT_PROFILE, [this](HeightProfileSmoothEvent& evt) { this->view3D->get_canvas3d()->smooth_layer_height_profile(evt.data); });
        view3D_canvas->Bind(EVT_GLCANVAS_RELOAD_FROM_DISK, [this](SimpleEvent&) { this->reload_all_from_disk(); });

        // 3DScene/Toolbar:
        view3D_canvas->Bind(EVT_GLTOOLBAR_ADD, &priv::on_action_add, this);
        view3D_canvas->Bind(EVT_GLTOOLBAR_DELETE, [q](SimpleEvent&) { q->remove_selected(); });
        view3D_canvas->Bind(EVT_GLTOOLBAR_DELETE_ALL, [this](SimpleEvent&) { delete_all_objects_from_model(); });
//        view3D_canvas->Bind(EVT_GLTOOLBAR_DELETE_ALL, [q](SimpleEvent&) { q->reset_with_confirm(); });

        view3D_canvas->Bind(EVT_GLTOOLBAR_ADD_PLATE, &priv::on_action_add_plate, this);
        view3D_canvas->Bind(EVT_GLTOOLBAR_DEL_PLATE, &priv::on_action_del_plate, this);
        view3D_canvas->Bind(EVT_GLTOOLBAR_ORIENT, [this](SimpleEvent&) {
            //BBS arrage from EVT set default state.
            this->q->set_prepare_state(Job::PREPARE_STATE_DEFAULT);
            this->q->orient(); });
        view3D_canvas->Bind(EVT_GLTOOLBAR_ARRANGE, [this](SimpleEvent&) {
            //BBS arrage from EVT set default state.
            this->q->set_prepare_state(Job::PREPARE_STATE_DEFAULT);
            this->q->arrange();
            });
        view3D_canvas->Bind(EVT_GLTOOLBAR_CUT, [q](SimpleEvent&) { q->cut_selection_to_clipboard(); });
        view3D_canvas->Bind(EVT_GLTOOLBAR_COPY, [q](SimpleEvent&) { q->copy_selection_to_clipboard(); });
        view3D_canvas->Bind(EVT_GLTOOLBAR_PASTE, [q](SimpleEvent&) { q->paste_from_clipboard(); });
        view3D_canvas->Bind(EVT_GLTOOLBAR_LAYERSEDITING, &priv::on_action_layersediting, this);
        //BBS: add clone
        view3D_canvas->Bind(EVT_GLTOOLBAR_CLONE, [q](SimpleEvent&) { q->clone_selection(); });
        view3D_canvas->Bind(EVT_GLTOOLBAR_MORE, [q](SimpleEvent&) { q->increase_instances(); });
        view3D_canvas->Bind(EVT_GLTOOLBAR_FEWER, [q](SimpleEvent&) { q->decrease_instances(); });
        view3D_canvas->Bind(EVT_GLTOOLBAR_SPLIT_OBJECTS, &priv::on_action_split_objects, this);
        view3D_canvas->Bind(EVT_GLTOOLBAR_SPLIT_VOLUMES, &priv::on_action_split_volumes, this);
        //BBS: GUI refactor: GLToolbar
        view3D_canvas->Bind(EVT_GLTOOLBAR_OPEN_PROJECT, &priv::on_action_open_project, this);
        //view3D_canvas->Bind(EVT_GLTOOLBAR_SLICE_PLATE, &priv::on_action_slice_plate, this);
        //view3D_canvas->Bind(EVT_GLTOOLBAR_SLICE_ALL, &priv::on_action_slice_all, this);
        //view3D_canvas->Bind(EVT_GLTOOLBAR_PRINT_PLATE, &priv::on_action_print_plate, this);
        //view3D_canvas->Bind(EVT_GLTOOLBAR_PRINT_ALL, &priv::on_action_print_all, this);
        //view3D_canvas->Bind(EVT_GLTOOLBAR_EXPORT_GCODE, &priv::on_action_export_gcode, this);
        view3D_canvas->Bind(EVT_GLVIEWTOOLBAR_ASSEMBLE, [q](SimpleEvent&) { q->select_view_3D("Assemble"); });
        //preview also send these events
        //preview_canvas->Bind(EVT_GLTOOLBAR_SLICE_PLATE, &priv::on_action_slice_plate, this);
        //preview_canvas->Bind(EVT_GLTOOLBAR_PRINT_PLATE, &priv::on_action_print_plate, this);
        //preview_canvas->Bind(EVT_GLTOOLBAR_PRINT_ALL, &priv::on_action_print_all, this);
        //review_canvas->Bind(EVT_GLTOOLBAR_EXPORT_GCODE, &priv::on_action_export_gcode, this);
    }
    view3D_canvas->Bind(EVT_GLCANVAS_UPDATE_BED_SHAPE, [q](SimpleEvent&) { q->set_bed_shape(); });

    // Preview events:
    preview->get_wxglcanvas()->Bind(EVT_GLCANVAS_QUESTION_MARK, [](SimpleEvent&) { wxGetApp().keyboard_shortcuts(); });
    preview->get_wxglcanvas()->Bind(EVT_GLCANVAS_UPDATE_BED_SHAPE, [q](SimpleEvent&) { q->set_bed_shape(); });
    preview->get_wxglcanvas()->Bind(EVT_GLCANVAS_UPDATE, [this](SimpleEvent &) {
            preview->get_canvas3d()->set_as_dirty();
        });
    if (wxGetApp().is_editor()) {
        preview->get_wxglcanvas()->Bind(EVT_GLCANVAS_TAB, [this](SimpleEvent&) { select_next_view_3D(); });
        preview->get_wxglcanvas()->Bind(EVT_GLCANVAS_COLLAPSE_SIDEBAR, [this](SimpleEvent&) { this->q->collapse_sidebar(!this->q->is_sidebar_collapsed());  });
        preview->get_wxglcanvas()->Bind(EVT_CUSTOMEVT_TICKSCHANGED, [this](wxCommandEvent& event) {
            Type tick_event_type = (Type)event.GetInt();
            Model& model = wxGetApp().plater()->model();
            //BBS: replace model custom gcode with current plate custom gcode
            model.plates_custom_gcodes[model.curr_plate_index] = preview->get_canvas3d()->get_gcode_viewer().get_layers_slider()->GetTicksValues();

            // BBS set to invalid state only
            if (tick_event_type == Type::ToolChange || tick_event_type == Type::Custom || tick_event_type == Type::Template || tick_event_type == Type::PausePrint) {
                PartPlate *plate = this->q->get_partplate_list().get_curr_plate();
                if (plate) {
                    plate->update_slice_result_valid_state(false);
                }
            }

            preview->on_tick_changed(tick_event_type);

            // update slice and print button
            wxGetApp().mainframe->update_slice_print_status(MainFrame::SlicePrintEventType::eEventSliceUpdate, true, false);
            set_need_update(true);
        });
    }
    if (wxGetApp().is_gcode_viewer())
        preview->Bind(EVT_GLCANVAS_RELOAD_FROM_DISK, [this](SimpleEvent&) { this->q->reload_gcode_from_disk(); });

    //BBS
    wxGLCanvas* assemble_canvas = assemble_view->get_wxglcanvas();
    if (wxGetApp().is_editor()) {
        assemble_canvas->Bind(EVT_GLTOOLBAR_FILLCOLOR, [q](IntEvent& evt) { q->fill_color(evt.get_data()); });
        assemble_canvas->Bind(EVT_GLCANVAS_OBJECT_SELECT, &priv::on_object_select, this);
        assemble_canvas->Bind(EVT_GLVIEWTOOLBAR_3D, [q](SimpleEvent&) { q->select_view_3D("3D"); });
        assemble_canvas->Bind(EVT_GLCANVAS_RIGHT_CLICK, &priv::on_right_click, this);
    }

    if (wxGetApp().is_editor()) {
        q->Bind(EVT_SLICING_COMPLETED, &priv::on_slicing_completed, this);
        q->Bind(EVT_PROCESS_COMPLETED, &priv::on_process_completed, this);
        q->Bind(EVT_EXPORT_BEGAN, &priv::on_export_began, this);
        q->Bind(EVT_EXPORT_FINISHED, &priv::on_export_finished, this);
        q->Bind(EVT_GLVIEWTOOLBAR_3D, [q](SimpleEvent&) { q->select_view_3D("3D"); });
        //BBS: set on_slice to false
        q->Bind(EVT_GLVIEWTOOLBAR_PREVIEW, [q](SimpleEvent&) { q->select_view_3D("Preview", false); });
        q->Bind(EVT_GLTOOLBAR_SLICE_PLATE, &priv::on_action_slice_plate, this);
        q->Bind(EVT_GLTOOLBAR_SLICE_ALL, &priv::on_action_slice_all, this);
        q->Bind(EVT_GLTOOLBAR_PRINT_PLATE, &priv::on_action_print_plate, this);
        q->Bind(EVT_GLTOOLBAR_SELECT_SLICED_PLATE, &priv::on_action_select_sliced_plate, this);
        q->Bind(EVT_GLTOOLBAR_PRINT_ALL, &priv::on_action_print_all, this);
        q->Bind(EVT_GLTOOLBAR_EXPORT_GCODE, &priv::on_action_export_gcode, this);
        q->Bind(EVT_GLTOOLBAR_SEND_GCODE, &priv::on_action_send_gcode, this);
        q->Bind(EVT_GLTOOLBAR_EXPORT_SLICED_FILE, &priv::on_action_export_sliced_file, this);
        q->Bind(EVT_GLTOOLBAR_EXPORT_ALL_SLICED_FILE, &priv::on_action_export_all_sliced_file, this);
        q->Bind(EVT_GLTOOLBAR_SEND_TO_PRINTER, &priv::on_action_export_to_sdcard, this);
        q->Bind(EVT_GLTOOLBAR_SEND_TO_PRINTER_ALL, &priv::on_action_export_to_sdcard_all, this);
        q->Bind(EVT_GLCANVAS_PLATE_SELECT, &priv::on_plate_selected, this);
        q->Bind(EVT_DOWNLOAD_PROJECT, &priv::on_action_download_project, this);
        q->Bind(EVT_IMPORT_MODEL_ID, &priv::on_action_request_model_id, this);
        q->Bind(EVT_PRINT_FINISHED, [q](wxCommandEvent &evt) { q->print_job_finished(evt); });
        q->Bind(EVT_SEND_FINISHED, [q](wxCommandEvent &evt) { q->send_job_finished(evt); });
        q->Bind(EVT_PUBLISH_FINISHED, [q](wxCommandEvent &evt) { q->publish_job_finished(evt);});
        //q->Bind(EVT_GLVIEWTOOLBAR_ASSEMBLE, [q](SimpleEvent&) { q->select_view_3D("Assemble"); });
    }

    // Drop target:
    q->SetDropTarget(new PlaterDropTarget(q));   // if my understanding is right, wxWindow takes the owenership
    q->Layout();

    set_current_panel(wxGetApp().is_editor() ? static_cast<wxPanel*>(view3D) : static_cast<wxPanel*>(preview));

    // updates camera type from .ini file
    camera.enable_update_config_on_type_change(true);
    // BBS set config
    bool use_perspective_camera = get_config("use_perspective_camera").compare("true") == 0;
    if (use_perspective_camera) {
        camera.set_type(Camera::EType::Perspective);
    } else {
        camera.set_type(Camera::EType::Ortho);
    }

    // Load the 3DConnexion device database.
    mouse3d_controller.load_config(*wxGetApp().app_config);
    // Start the background thread to detect and connect to a HID device (Windows and Linux).
    // Connect to a 3DConnextion driver (OSX).
    mouse3d_controller.init();
#ifdef _WIN32
    // Register an USB HID (Human Interface Device) attach event. evt contains Win32 path to the USB device containing VID, PID and other info.
    // This event wakes up the Mouse3DController's background thread to enumerate HID devices, if the VID of the callback event
    // is one of the 3D Mouse vendors (3DConnexion or Logitech).
    this->q->Bind(EVT_HID_DEVICE_ATTACHED, [this](HIDDeviceAttachedEvent &evt) {
        mouse3d_controller.device_attached(evt.data);
        });
    this->q->Bind(EVT_HID_DEVICE_DETACHED, [this](HIDDeviceAttachedEvent& evt) {
        mouse3d_controller.device_detached(evt.data);
        });
#endif /* _WIN32 */
    //notification_manager = new NotificationManager(this->q);

    if (wxGetApp().is_editor()) {
        this->q->Bind(EVT_EJECT_DRIVE_NOTIFICAION_CLICKED, [this](EjectDriveNotificationClickedEvent&) { this->q->eject_drive(); });
        this->q->Bind(EVT_EXPORT_GCODE_NOTIFICAION_CLICKED, [this](ExportGcodeNotificationClickedEvent&) { this->q->export_gcode(true); });
        this->q->Bind(EVT_PRESET_UPDATE_AVAILABLE_CLICKED, [](PresetUpdateAvailableClickedEvent&) {  wxGetApp().get_preset_updater()->on_update_notification_confirm(); });

        /* BBS do not handle removeable driver event */
        this->q->Bind(EVT_REMOVABLE_DRIVE_EJECTED, [this](RemovableDriveEjectEvent &evt) {
            if (evt.data.second) {
                // BBS
                //this->show_action_buttons(this->ready_to_slice);
                notification_manager->close_notification_of_type(NotificationType::ExportFinished);
                notification_manager->push_notification(NotificationType::CustomNotification,
                                                        NotificationManager::NotificationLevel::RegularNotificationLevel,
                                                        format(_L("Successfully unmounted. The device %s(%s) can now be safely removed from the computer."), evt.data.first.name, evt.data.first.path)
                    );
            } else {
                notification_manager->push_notification(NotificationType::CustomNotification,
                                                        NotificationManager::NotificationLevel::ErrorNotificationLevel,
                                                        format(_L("Ejecting of device %s(%s) has failed."), evt.data.first.name, evt.data.first.path)
                    );
            }
        });
        this->q->Bind(EVT_REMOVABLE_DRIVES_CHANGED, [this](RemovableDrivesChangedEvent &) {
            // BBS
            //this->show_action_buttons(this->ready_to_slice);
            // Close notification ExportingFinished but only if last export was to removable
            notification_manager->device_ejected();
        });
        // Start the background thread and register this window as a target for update events.
        wxGetApp().removable_drive_manager()->init(this->q);
#ifdef _WIN32
        //Trigger enumeration of removable media on Win32 notification.
        this->q->Bind(EVT_VOLUME_ATTACHED, [this](VolumeAttachedEvent &evt) { wxGetApp().removable_drive_manager()->volumes_changed(); });
        this->q->Bind(EVT_VOLUME_DETACHED, [this](VolumeDetachedEvent &evt) { wxGetApp().removable_drive_manager()->volumes_changed(); });
#endif /* _WIN32 */
    }

    // Initialize the Undo / Redo stack with a first snapshot.
    //this->take_snapshot("New Project", UndoRedo::SnapshotType::ProjectSeparator);
    // Reset the "dirty project" flag.
    m_undo_redo_stack_main.mark_current_as_saved();
    dirty_state.update_from_undo_redo_stack(false);
    //this->take_snapshot("New Project");
    // BBS: save project confirm
    up_to_date(true, false);
    up_to_date(true, true);
    model.set_need_backup();

    // BBS: restore project
    if (wxGetApp().is_editor()) {
        auto last_backup = wxGetApp().app_config->get_last_backup_dir();
        this->q->Bind(EVT_RESTORE_PROJECT, [this, last = last_backup](wxCommandEvent& e) {
            std::string last_backup = last;
            std::string originfile;
            if (Slic3r::has_restore_data(last_backup, originfile)) {
                auto result = MessageDialog(this->q, _L("Previous unsaved project detected, do you want to restore it?"), wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Restore"), wxYES_NO | wxYES_DEFAULT | wxCENTRE).ShowModal();
                if (result == wxID_YES) {
                    this->q->load_project(from_path(last_backup), from_path(originfile));
                    Slic3r::backup_soon();
                    return;
                }
            }
            try {
                if (originfile != "<lock>") // see bbs_3mf.cpp for lock detail
                    boost::filesystem::remove_all(last);
            }
            catch (...) {}
            int skip_confirm = e.GetInt();
            this->q->new_project(skip_confirm, true);
            });
        //wxPostEvent(this->q, wxCommandEvent{EVT_RESTORE_PROJECT});
    }

    /*this->q->Bind(EVT_LOAD_MODEL_OTHER_INSTANCE, [this](LoadFromOtherInstanceEvent& evt) {
        BOOST_LOG_TRIVIAL(trace) << "Received load from other instance event.";
        wxArrayString input_files;
        for (size_t i = 0; i < evt.data.size(); ++i) {
            input_files.push_back(from_u8(evt.data[i].string()));
        }
        wxGetApp().mainframe->Raise();
        this->q->load_files(input_files);
    });
    this->q->Bind(EVT_INSTANCE_GO_TO_FRONT, [this](InstanceGoToFrontEvent &) {
        bring_instance_forward();
    });*/
    //wxGetApp().other_instance_message_handler()->init(this->q);

    // collapse sidebar according to saved value
    //if (wxGetApp().is_editor()) {
    //    bool is_collapsed = wxGetApp().app_config->get("collapsed_sidebar") == "1";
    //    sidebar->collapse(is_collapsed);
    //}
}

Plater::priv::~priv()
{
    if (config != nullptr)
        delete config;
    // Saves the database of visited (already shown) hints into hints.ini.
    notification_manager->deactivate_loaded_hints();
}

void Plater::priv::update(unsigned int flags)
{
    // the following line, when enabled, causes flickering on NVIDIA graphics cards
//    wxWindowUpdateLocker freeze_guard(q);
#ifdef SUPPORT_AUTOCENTER
    if (get_config("autocenter") == "true")
        model.center_instances_around_point(this->bed.build_volume().bed_center());
#endif

    unsigned int update_status = 0;
    const bool force_background_processing_restart = this->printer_technology == ptSLA || (flags & (unsigned int)UpdateParams::FORCE_BACKGROUND_PROCESSING_UPDATE);
    if (force_background_processing_restart)
        // Update the SLAPrint from the current Model, so that the reload_scene()
        // pulls the correct data.
        update_status = this->update_background_process(false, flags & (unsigned int)UpdateParams::POSTPONE_VALIDATION_ERROR_MESSAGE);
    //BBS TODO reload_scene
    this->view3D->reload_scene(false, flags & (unsigned int)UpdateParams::FORCE_FULL_SCREEN_REFRESH);
    this->preview->reload_print();
    //BBS assemble view
    this->assemble_view->reload_scene(false, flags);

    if (current_panel && q->is_preview_shown()) {
        q->force_update_all_plate_thumbnails();
        //update_fff_scene_only_shells(true);
    }

    if (force_background_processing_restart)
        this->restart_background_process(update_status);
    else
        this->schedule_background_process();

    // BBS
#if 0
    if (get_config("autocenter") == "true" && this->sidebar->obj_manipul()->IsShown())
        this->sidebar->obj_manipul()->UpdateAndShow(true);
#endif
}

void Plater::priv::select_view(const std::string& direction)
{
    if (current_panel == view3D) {
        BOOST_LOG_TRIVIAL(info) << "select view3D";
        view3D->select_view(direction);
        wxGetApp().update_ui_from_settings();
    }
    else if (current_panel == preview) {
        BOOST_LOG_TRIVIAL(info) << "select preview";
        preview->select_view(direction);
        wxGetApp().update_ui_from_settings();
    }
    else if (current_panel == assemble_view) {
        BOOST_LOG_TRIVIAL(info) << "select assemble view";
        assemble_view->select_view(direction);
    }
}
// BBS set print speed table and find maximum speed
void Plater::setPrintSpeedTable(GlobalSpeedMap &printSpeedMap) {
    Slic3r::DynamicPrintConfig config = wxGetApp().preset_bundle->full_config();
    printSpeedMap.maxSpeed = 0;
    if (config.has("inner_wall_speed")) {
        printSpeedMap.perimeterSpeed = config.opt_float("inner_wall_speed");
        if (printSpeedMap.perimeterSpeed > printSpeedMap.maxSpeed)
            printSpeedMap.maxSpeed = printSpeedMap.perimeterSpeed;
    }
    if (config.has("outer_wall_speed")) {
        printSpeedMap.externalPerimeterSpeed = config.opt_float("outer_wall_speed");
        printSpeedMap.maxSpeed = std::max(printSpeedMap.maxSpeed, printSpeedMap.externalPerimeterSpeed);
    }
    if (config.has("sparse_infill_speed")) {
        printSpeedMap.infillSpeed = config.opt_float("sparse_infill_speed");
        if (printSpeedMap.infillSpeed > printSpeedMap.maxSpeed)
            printSpeedMap.maxSpeed = printSpeedMap.infillSpeed;
    }
    if (config.has("internal_solid_infill_speed")) {
        printSpeedMap.solidInfillSpeed = config.opt_float("internal_solid_infill_speed");
        if (printSpeedMap.solidInfillSpeed > printSpeedMap.maxSpeed)
            printSpeedMap.maxSpeed = printSpeedMap.solidInfillSpeed;
    }
    if (config.has("top_surface_speed")) {
        printSpeedMap.topSolidInfillSpeed = config.opt_float("top_surface_speed");
        if (printSpeedMap.topSolidInfillSpeed > printSpeedMap.maxSpeed)
            printSpeedMap.maxSpeed = printSpeedMap.topSolidInfillSpeed;
    }
    if (config.has("support_speed")) {
        printSpeedMap.supportSpeed = config.opt_float("support_speed");

        if (printSpeedMap.supportSpeed > printSpeedMap.maxSpeed)
            printSpeedMap.maxSpeed = printSpeedMap.supportSpeed;
    }
    if (config.has("small_perimeter_speed")) {
        printSpeedMap.smallPerimeterSpeed = config.get_abs_value("small_perimeter_speed");

        if (printSpeedMap.smallPerimeterSpeed > printSpeedMap.maxSpeed)
            printSpeedMap.maxSpeed = printSpeedMap.smallPerimeterSpeed;
    }
    /*        "inner_wall_speed", "outer_wall_speed", "sparse_infill_speed", "internal_solid_infill_speed",
        "top_surface_speed", "support_speed", "support_object_xy_distance", "support_interface_speed",
        "bridge_speed", "gap_infill_speed", "travel_speed", "initial_layer_speed"*/

    auto& print = wxGetApp().plater()->get_partplate_list().get_current_fff_print();
    auto print_config = print.config();
    printSpeedMap.bed_poly.points = get_bed_shape(*(wxGetApp().plater()->config()));
    Pointfs excluse_area_points = print_config.bed_exclude_area.values;
    Polygons exclude_polys;
    Polygon exclude_poly;
    for (int i = 0; i < excluse_area_points.size(); i++) {
        auto pt = excluse_area_points[i];
        exclude_poly.points.emplace_back(scale_(pt.x()), scale_(pt.y()));
        if (i % 4 == 3) {  // exclude areas are always rectangle
            exclude_polys.push_back(exclude_poly);
            exclude_poly.points.clear();
        }
    }
    printSpeedMap.bed_poly = diff({ printSpeedMap.bed_poly }, exclude_polys)[0];
}

// find temperature of heatend and bed and matierial of an given extruder
void Plater::setExtruderParams(std::map<size_t, Slic3r::ExtruderParams>& extParas) {
    extParas.clear();
    Slic3r::DynamicPrintConfig config = wxGetApp().preset_bundle->full_config();
    // BBS
    int numExtruders = wxGetApp().preset_bundle->filament_presets.size();
    for (unsigned int i = 0; i != numExtruders; ++i) {
        std::string matName = "";
        // BBS
        int bedTemp = 35;
        double endTemp = 0.f;
        if (config.has("filament_type")) {
            matName = config.opt_string("filament_type", i);
        }
        if (config.has("nozzle_temperature")) {
            endTemp = config.opt_int("nozzle_temperature", i);
        }

        // FIXME: curr_bed_type is now a plate config rather than a global config.
        // Currently bed temp is not used for brim generation, so just comment it for now.
#if 0
        if (config.has("curr_bed_type")) {
            BedType curr_bed_type = config.opt_enum<BedType>("curr_bed_type");
            bedTemp = config.opt_int(get_bed_temp_key(curr_bed_type), i);
        }
#endif
        if (i == 0) extParas.insert({ i,{matName, bedTemp, endTemp} });
        extParas.insert({ i + 1,{matName, bedTemp, endTemp} });
    }
}

wxColour Plater::get_next_color_for_filament()
{
    static int curr_color_filamenet = 0;
    // refs to https://www.ebaomonthly.com/window/photo/lesson/colorList.htm
    wxColour colors[FILAMENT_SYSTEM_COLORS_NUM] = {
        *wxYELLOW,
        * wxRED,
        *wxBLUE,
        *wxCYAN,
        *wxLIGHT_GREY,
        *wxWHITE,
        *wxBLACK,
        wxColour(0,127,255),
        wxColour(139,0,255),
        wxColour(102,255,0),
        wxColour(255,215,0),
        wxColour(0,35,100),
        wxColour(255,0,255),
        wxColour(8,37,103),
        wxColour(127,255,212),
        wxColour(255,191,0)
    };
    return colors[curr_color_filamenet++ % FILAMENT_SYSTEM_COLORS_NUM];
}

wxString Plater::get_slice_warning_string(GCodeProcessorResult::SliceWarning& warning)
{
    if (warning.msg == BED_TEMP_TOO_HIGH_THAN_FILAMENT) {
        return _L("The bed temperature exceeds filament's vitrification temperature. Please open the front door of printer before printing to avoid nozzle clog.");
    } else if (warning.msg == NOZZLE_HRC_CHECKER) {
        return _L("The nozzle hardness required by the filament is higher than the default nozzle hardness of the printer. Please replace the hardened nozzle or filament, otherwise, the nozzle will be attrited or damaged.");
    } else {
        return wxString(warning.msg);
    }
}

void Plater::priv::apply_free_camera_correction(bool apply/* = true*/)
{
    bool use_perspective_camera = get_config("use_perspective_camera").compare("true") == 0;
    if (use_perspective_camera)
        camera.set_type(Camera::EType::Perspective);
    else
        camera.set_type(Camera::EType::Ortho);
    if (apply
#ifdef SUPPORT_FREE_CAMERA
        && wxGetApp().app_config->get("use_free_camera") != "1"
#endif
        )
        camera.recover_from_free_camera();
}

//BBS: add no slice option
void Plater::priv::select_view_3D(const std::string& name, bool no_slice)
{
    if (name == "3D") {
        BOOST_LOG_TRIVIAL(info) << "select view3D";
        if (q->only_gcode_mode() || q->using_exported_file()) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format("goto preview page when loading gcode/exported_3mf");
        }
        set_current_panel(view3D, no_slice);
    }
    else if (name == "Preview") {
        BOOST_LOG_TRIVIAL(info) << "select preview";
        //BBS update extruder params and speed table before slicing
        Plater::setExtruderParams(Slic3r::Model::extruderParamsMap);
        Plater::setPrintSpeedTable(Slic3r::Model::printSpeedMap);
        set_current_panel(preview, no_slice);
    }
    else if (name == "Assemble") {
        BOOST_LOG_TRIVIAL(info) << "select assemble view";
        set_current_panel(assemble_view, no_slice);
    }

    //BBS update selection
    wxGetApp().obj_list()->update_selections();
    selection_changed();

    apply_free_camera_correction(false);
}

void Plater::priv::select_next_view_3D()
{
    
    if (current_panel == view3D)
        wxGetApp().mainframe->select_tab(size_t(MainFrame::tpPreview));
    else if (current_panel == preview)
        wxGetApp().mainframe->select_tab(size_t(MainFrame::tp3DEditor));
//    else if (current_panel == assemble_view)
//        set_current_panel(view3D);
}

void Plater::priv::collapse_sidebar(bool collapse)
{
    if (q->m_only_gcode && !collapse)
        return;
    sidebar->collapse(collapse);
    notification_manager->set_sidebar_collapsed(collapse);
}


void Plater::priv::reset_all_gizmos()
{
    view3D->get_canvas3d()->reset_all_gizmos();
}

// Called after the Preferences dialog is closed and the program settings are saved.
// Update the UI based on the current preferences.
void Plater::priv::update_ui_from_settings()
{
    apply_free_camera_correction();

    view3D->get_canvas3d()->update_ui_from_settings();
    preview->get_canvas3d()->update_ui_from_settings();

    sidebar->update_ui_from_settings();
}

// BBS
std::shared_ptr<BBLStatusBar> Plater::priv::statusbar()
{
    return nullptr;
}

std::string Plater::priv::get_config(const std::string &key) const
{
    return wxGetApp().app_config->get(key);
}

BoundingBoxf Plater::priv::bed_shape_bb() const
{
    BoundingBox bb = scaled_bed_shape_bb();
    return BoundingBoxf(unscale(bb.min), unscale(bb.max));
}

BoundingBox Plater::priv::scaled_bed_shape_bb() const
{
    const auto *bed_shape_opt = config->opt<ConfigOptionPoints>("printable_area");
    const auto printable_area = Slic3r::Polygon::new_scale(bed_shape_opt->values);
    return printable_area.bounding_box();
}

// BBS: backup & restore
std::vector<size_t> Plater::priv::load_files(const std::vector<fs::path>& input_files, LoadStrategy strategy, bool ask_multi)
{
    std::vector<size_t> empty_result;
    bool dlg_cont = true;
    bool is_user_cancel = false;
    bool translate_old = false;
    int current_width, current_depth, current_height;

    if (input_files.empty()) { return std::vector<size_t>(); }
    
    // SoftFever: ugly fix so we can exist pa calib mode
    background_process.fff_print()->calib_mode() = CalibMode::Calib_None;


    // BBS
    int filaments_cnt = config->opt<ConfigOptionStrings>("filament_colour")->values.size();
    bool one_by_one = input_files.size() == 1 || printer_technology == ptSLA/* || filaments_cnt <= 1*/;
    if (! one_by_one) {
        for (const auto &path : input_files) {
            if (std::regex_match(path.string(), pattern_bundle)) {
                one_by_one = true;
                break;
            }
        }
    }

    bool load_model = strategy & LoadStrategy::LoadModel;
    bool load_config = strategy & LoadStrategy::LoadConfig;
    bool imperial_units = strategy & LoadStrategy::ImperialUnits;
    bool silence = strategy & LoadStrategy::Silence;

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": load_model %1%, load_config %2%, input_files size %3%")%load_model %load_config %input_files.size();

    const auto loading = _L("Loading") + dots;
    ProgressDialog dlg(loading, "", 100, find_toplevel_parent(q), wxPD_AUTO_HIDE | wxPD_CAN_ABORT | wxPD_APP_MODAL);
    wxBusyCursor busy;

    auto *new_model = (!load_model || one_by_one) ? nullptr : new Slic3r::Model();
    std::vector<size_t> obj_idxs;

    int answer_convert_from_meters          = wxOK_DEFAULT;
    int answer_convert_from_imperial_units  = wxOK_DEFAULT;
    int tolal_model_count                   = 0;

    int progress_percent = 0;
    int total_files = input_files.size();
    const int stage_percent[IMPORT_STAGE_MAX+1] = {
            5,      // IMPORT_STAGE_RESTORE
            10,     // IMPORT_STAGE_OPEN
            30,     // IMPORT_STAGE_READ_FILES
            50,     // IMPORT_STAGE_EXTRACT
            60,     // IMPORT_STAGE_LOADING_OBJECTS
            70,     // IMPORT_STAGE_LOADING_PLATES
            80,     // IMPORT_STAGE_FINISH
            85,     // IMPORT_STAGE_ADD_INSTANCE
            90,      // IMPORT_STAGE_UPDATE_GCODE
            92,     // IMPORT_STAGE_CHECK_MODE_GCODE
            95,     // UPDATE_GCODE_RESULT
            98,     // IMPORT_LOAD_CONFIG
            99,     // IMPORT_LOAD_MODEL_OBJECTS
            100
     };
    const int step_percent[LOAD_STEP_STAGE_NUM+1] = {
            5,     // LOAD_STEP_STAGE_READ_FILE
            30,     // LOAD_STEP_STAGE_GET_SOLID
            60,     // LOAD_STEP_STAGE_GET_MESH
            100
     };

    const float INPUT_FILES_RATIO            = 0.7;
    const float INIT_MODEL_RATIO             = 0.75;
    const float CENTER_AROUND_ORIGIN_RATIO   = 0.8;
    const float LOAD_MODEL_RATIO             = 0.9;

    for (size_t i = 0; i < input_files.size(); ++i) {
        int file_percent = 0;

#ifdef _WIN32
        auto path = input_files[i];
        // On Windows, we swap slashes to back slashes, see GH #6803 as read_from_file() does not understand slashes on Windows thus it assignes full path to names of loaded objects.
        path.make_preferred();
#else  // _WIN32
       // Don't make a copy on Posix. Slash is a path separator, back slashes are not accepted as a substitute.
        const auto &path = input_files[i];
#endif // _WIN32
        const auto filename         = path.filename();
        int  progress_percent = static_cast<int>(100.0f * static_cast<float>(i) / static_cast<float>(input_files.size()));
        const auto real_filename    = (strategy & LoadStrategy::Restore) ? input_files[++i].filename() : filename;
        const auto dlg_info         = _L("Loading file") + ": " + from_path(real_filename);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": load file %1%") % filename;
        dlg_cont = dlg.Update(progress_percent, dlg_info);
        if (!dlg_cont) return empty_result;

        const bool type_3mf = std::regex_match(path.string(), pattern_3mf);
        // const bool type_zip_amf = !type_3mf && std::regex_match(path.string(), pattern_zip_amf);
        const bool type_any_amf = !type_3mf && std::regex_match(path.string(), pattern_any_amf);
        // const bool type_prusa   = std::regex_match(path.string(), pattern_prusa);

        Slic3r::Model model;
        // BBS: add auxiliary files related logic
        bool load_aux = strategy & LoadStrategy::LoadAuxiliary, load_old_project = false;
        if (load_model && load_config && type_3mf) {
            load_aux = true;
            strategy = strategy | LoadStrategy::LoadAuxiliary;
        }
        if (load_config) strategy = strategy | LoadStrategy::CheckVersion;
        bool is_project_file = false;
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": is_project_file %1%, type_3mf %2%") % is_project_file % type_3mf;
        try {
            if (type_3mf) {
                DynamicPrintConfig config;
                Semver             file_version;
                {
                    DynamicPrintConfig config_loaded;

                    // BBS: add part plate related logic
                    PlateDataPtrs             plate_data;
                    En3mfType                 en_3mf_file_type = En3mfType::From_BBS;
                    ConfigSubstitutionContext config_substitutions{ForwardCompatibilitySubstitutionRule::Enable};
                    std::vector<Preset *>     project_presets;
                    // BBS: backup & restore
                    q->skip_thumbnail_invalid = true;
                    model = Slic3r::Model::read_from_archive(path.string(), &config_loaded, &config_substitutions, en_3mf_file_type, strategy, &plate_data, &project_presets,
                                                             &file_version,
                                                             [this, &dlg, real_filename, &progress_percent, &file_percent, stage_percent, INPUT_FILES_RATIO, total_files, i,
                                                              &is_user_cancel](int import_stage, int current, int total, bool &cancel) {
                                                                 bool     cont = true;
                                                                 float percent_float = (100.0f * (float)i / (float)total_files) + INPUT_FILES_RATIO * ((float)stage_percent[import_stage] + (float)current * (float)(stage_percent[import_stage + 1] - stage_percent[import_stage]) /(float) total) / (float)total_files;
                                                                 BOOST_LOG_TRIVIAL(trace) << "load_3mf_file: percent(float)=" << percent_float << ", stage = " << import_stage << ", curr = " << current << ", total = " << total;
                                                                 progress_percent = (int)percent_float;
                                                                 wxString msg  = wxString::Format(_L("Loading file: %s"), from_path(real_filename));
                                                                 cont          = dlg.Update(progress_percent, msg);
                                                                 cancel        = !cont;
                                                                 if (cancel)
                                                                     is_user_cancel = cancel;
                                                             });
                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" << __LINE__
                                            << boost::format(", plate_data.size %1%, project_preset.size %2%, is_bbs_3mf %3%, file_version %4% \n") % plate_data.size() %
                                                   project_presets.size() % (en_3mf_file_type == En3mfType::From_BBS) % file_version.to_string();

                    // 1. add extruder for prusa model if the number of existing extruders is not enough
                    // 2. add extruder for BBS or Other model if only import geometry
                    if (en_3mf_file_type == En3mfType::From_Prusa || (load_model && !load_config)) {
                        std::set<int> extruderIds;
                        for (ModelObject *o : model.objects) {
                            if (o->config.option("extruder")) extruderIds.insert(o->config.extruder());
                            for (auto volume : o->volumes) {
                                if (volume->config.option("extruder")) extruderIds.insert(volume->config.extruder());
                                for (int extruder : volume->get_extruders()) { extruderIds.insert(extruder); }
                            }
                        }
                        int size = extruderIds.size() == 0 ? 0 : *(extruderIds.rbegin());

                        int filament_size = sidebar->combos_filament().size();
                        while (filament_size < 16 && filament_size < size) {
                            int         filament_count = filament_size + 1;
                            wxColour    new_col        = Plater::get_next_color_for_filament();
                            std::string new_color      = new_col.GetAsString(wxC2S_HTML_SYNTAX).ToStdString();
                            wxGetApp().preset_bundle->set_num_filaments(filament_count, new_color);
                            wxGetApp().plater()->on_filaments_change(filament_count);
                            ++filament_size;
                        }
                        wxGetApp().get_tab(Preset::TYPE_PRINT)->update();
                    }

                    std::string import_project_action = wxGetApp().app_config->get("import_project_action");
                    LoadType load_type;
                    if (import_project_action.empty())
                        load_type = LoadType::Unknown;
                    else
                        load_type  = static_cast<LoadType>(std::stoi(import_project_action));

                    // BBS: version check
                    Semver app_version = *(Semver::parse(SLIC3R_VERSION));
                    if (en_3mf_file_type == En3mfType::From_Prusa) {
                        // do not reset the model config
                        load_config = false;
                        if(load_type != LoadType::LoadGeometry)
                            show_info(q, _L("The 3mf is not from Bambu Lab, load geometry data only."), _L("Load 3mf"));
                    }
                    else if (load_config && (file_version.maj() != app_version.maj())) {
                        // version mismatch, only load geometries
                        load_config = false;
                        if (!load_model) {
                            // only load config case, return directly
                            show_info(q, _L("The Config can not be loaded."), _L("Load 3mf"));
                            q->skip_thumbnail_invalid = false;
                            return empty_result;
                        }
                        load_old_project = true;
                        // select view to 3D
                        q->select_view_3D("3D");
                        // select plate 0 as default
                        q->select_plate(0);
                        if (load_type != LoadType::LoadGeometry) {
                            if (en_3mf_file_type == En3mfType::From_BBS)
                                show_info(q, _L("The 3mf is generated by old Orca Slicer, load geometry data only."), _L("Load 3mf"));
                            else
                                show_info(q, _L("The 3mf is not from Bambu Lab, load geometry data only."), _L("Load 3mf"));
                        }
                        for (ModelObject *model_object : model.objects) {
                            model_object->config.reset();
                            // Is there any modifier or advanced config data?
                            for (ModelVolume *model_volume : model_object->volumes) model_volume->config.reset();
                        }
                    } else if (load_config && (file_version > app_version)) {
                        if (config_substitutions.unrecogized_keys.size() > 0) {
                            wxString text  = wxString::Format(_L("The 3mf's version %s is newer than %s's version %s, Found following keys unrecognized:"),
                                                             file_version.to_string(), std::string(SLIC3R_APP_FULL_NAME), app_version.to_string());
                            text += "\n";
                            bool     first = true;
                            // std::string context = into_u8(text);
                            wxString context = text;
                            for (auto &key : config_substitutions.unrecogized_keys) {
                                context += "  -";
                                context += key;
                                context += ";\n";
                                first = false;
                            }
                            wxString append = _L("You'd better upgrade your software.\n");
                            context += "\n\n";
                            // context += into_u8(append);
                            context += append;
                            show_info(q, context, _L("Newer 3mf version"));
                        }
                        else {
                            //if the minor version is not matched
                            if (file_version.min() != app_version.min()) {
                                wxString text  = wxString::Format(_L("The 3mf's version %s is newer than %s's version %s, Suggest to upgrade your software."),
                                                 file_version.to_string(), std::string(SLIC3R_APP_FULL_NAME), app_version.to_string());
                                text += "\n";
                                show_info(q, text, _L("Newer 3mf version"));
                            }
                        }
                    } else if (!load_config) {
                        // reset config except color
                        for (ModelObject *model_object : model.objects) {
                            bool has_extruder = model_object->config.has("extruder");
                            int  extruder_id  = -1;
                            // save the extruder information before reset
                            if (has_extruder) { extruder_id = model_object->config.extruder(); }

                            model_object->config.reset();

                            // restore the extruder after reset
                            if (has_extruder) { model_object->config.set("extruder", extruder_id); }

                            // Is there any modifier or advanced config data?
                            for (ModelVolume *model_volume : model_object->volumes) {
                                has_extruder = model_volume->config.has("extruder");
                                if (has_extruder) { extruder_id = model_volume->config.extruder(); }

                                model_volume->config.reset();

                                if (has_extruder) { model_volume->config.set("extruder", extruder_id); }
                            }
                        }
                    }

                    // plate data
                    if (plate_data.size() > 0) {
                        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" << __LINE__ << boost::format(", import 3mf UPDATE_GCODE_RESULT \n");
                        wxString msg = wxString::Format(_L("Loading file: %s"), from_path(real_filename));
                        dlg_cont     = dlg.Update(progress_percent, msg);
                        if (!dlg_cont) {
                            q->skip_thumbnail_invalid = false;
                            return empty_result;
                        }

                        Semver old_version(1, 5, 9);
                        if ((en_3mf_file_type == En3mfType::From_BBS) && (file_version < old_version) && load_model && load_config && !config_loaded.empty()) {
                            translate_old = true;
                            partplate_list.get_plate_size(current_width, current_depth, current_height);
                        }

                        if (load_config) {
                            if (translate_old) {
                                //set the size back
                                partplate_list.reset_size(current_width + Bed3D::Axes::DefaultTipRadius, current_depth + Bed3D::Axes::DefaultTipRadius, current_height, false);
                            }
                            partplate_list.load_from_3mf_structure(plate_data);
                            partplate_list.update_slice_context_to_current_plate(background_process);
                            this->preview->update_gcode_result(partplate_list.get_current_slice_result());
                            release_PlateData_list(plate_data);
                            sidebar->obj_list()->reload_all_plates();
                        } else {
                            partplate_list.reload_all_objects();
                        }
                    }

                    // BBS:: project embedded presets
                    if ((project_presets.size() > 0) && load_config) {
                        // load project embedded presets
                        PresetsConfigSubstitutions preset_substitutions;
                        PresetBundle &             preset_bundle = *wxGetApp().preset_bundle;
                        preset_substitutions                     = preset_bundle.load_project_embedded_presets(project_presets, ForwardCompatibilitySubstitutionRule::Enable);
                        if (!preset_substitutions.empty()) show_substitutions_info(preset_substitutions);
                    }
                    if (project_presets.size() > 0) {
                        for (unsigned int i = 0; i < project_presets.size(); i++) { delete project_presets[i]; }
                        project_presets.clear();
                    }

                    if (load_config && !config_loaded.empty()) {
                        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" << __LINE__ << boost::format(", import 3mf IMPORT_LOAD_CONFIG \n");
                        wxString msg = wxString::Format(_L("Loading file: %s"), from_path(real_filename));
                        dlg_cont     = dlg.Update(progress_percent, msg);
                        if (!dlg_cont) {
                            q->skip_thumbnail_invalid = false;
                            return empty_result;
                        }

                        // Based on the printer technology field found in the loaded config, select the base for the config,
                        PrinterTechnology printer_technology = Preset::printer_technology(config_loaded);

                        config.apply(static_cast<const ConfigBase &>(FullPrintConfig::defaults()));
                        // and place the loaded config over the base.
                        config += std::move(config_loaded);
                        std::map<std::string, std::string> validity = config.validate();
                        if (!validity.empty()) {
                            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" << __LINE__ << boost::format("Param values in 3mf error: ");
                            for (std::map<std::string, std::string>::iterator it=validity.begin(); it!=validity.end(); ++it)
                                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" << __LINE__ << boost::format("%1%: %2%")%it->first %it->second;
                            //
                            NotificationManager *notify_manager = q->get_notification_manager();
                            std::string error_message = L("Invalid values found in the 3mf:");
                            error_message += "\n";
                            for (std::map<std::string, std::string>::iterator it=validity.begin(); it!=validity.end(); ++it)
                                error_message += "-" + it->first + ": " + it->second + "\n";
                            error_message += "\n";
                            error_message += L("Please correct them in the param tabs");
                            notify_manager->bbl_show_3mf_warn_notification(error_message);
                        }
                    }
                    if (!config_substitutions.empty()) show_substitutions_info(config_substitutions.substitutions, filename.string());

                    // BBS
                    this->model.plates_custom_gcodes = model.plates_custom_gcodes;
                    this->model.design_info = model.design_info;
                    this->model.model_info  = model.model_info;
                }

                if (load_config) {
                    if (!config.empty()) {
                        Preset::normalize(config);
                        PresetBundle *preset_bundle = wxGetApp().preset_bundle;
                        // BBS: first validate the printer
                        // TODO: remove it after released""
                        bool validated = preset_bundle->validate_printers(filename.string(), config);
                        if (!validated) {
                            load_config      = false;
                            load_old_project = true;
                            // select view to 3D
                            q->select_view_3D("3D");
                            // select plate 0 as default
                            q->select_plate(0);
                            show_info(q, _L("The 3mf is not compatible, load geometry data only!"), _L("Incompatible 3mf"));
                            for (ModelObject *model_object : model.objects) {
                                model_object->config.reset();
                                // Is there any modifier or advanced config data?
                                for (ModelVolume *model_volume : model_object->volumes) model_volume->config.reset();
                            }
                        } else {
                            preset_bundle->load_config_model(filename.string(), std::move(config), file_version);
                            // BBS: moved this logic to presetcollection
                            //{
                            //    // After loading of the presets from project, check if they are visible.
                            //    // Set them to visible if they are not.

                            //    auto update_selected_preset_visibility = [](PresetCollection& presets, std::vector<std::string>& names) {
                            //        if (!presets.get_selected_preset().is_visible) {
                            //            assert(presets.get_selected_preset().name == presets.get_edited_preset().name);
                            //            presets.get_selected_preset().is_visible = true;
                            //            presets.get_edited_preset().is_visible = true;
                            //            names.emplace_back(presets.get_selected_preset().name);
                            //        }
                            //    };

                            //    std::vector<std::string> names;
                            //    if (printer_technology == ptFFF) {
                            //        update_selected_preset_visibility(preset_bundle->prints, names);
                            //        for (const std::string& filament : preset_bundle->filament_presets) {
                            //            Preset* preset = preset_bundle->filaments.find_preset(filament);
                            //            if (preset && !preset->is_visible) {
                            //                preset->is_visible = true;
                            //                names.emplace_back(preset->name);
                            //                if (preset->name == preset_bundle->filaments.get_edited_preset().name)
                            //                    preset_bundle->filaments.get_selected_preset().is_visible = true;
                            //            }
                            //        }
                            //    }
                            //    else {
                            //        update_selected_preset_visibility(preset_bundle->sla_prints, names);
                            //        update_selected_preset_visibility(preset_bundle->sla_materials, names);
                            //    }
                            //    update_selected_preset_visibility(preset_bundle->printers, names);

                            //    preset_bundle->update_compatible(PresetSelectCompatibleType::Never);

                            //    // show notification about temporarily installed presets
                            //    if (!names.empty()) {
                            //        std::string notif_text = into_u8(_L_PLURAL("The preset below was temporarily installed on the active instance of PrusaSlicer",
                            //                                                   "The presets below were temporarily installed on the active instance of PrusaSlicer",
                            //                                                   names.size())) + ":";
                            //        for (std::string& name : names)
                            //            notif_text += "\n - " + name;
                            //        notification_manager->push_notification(NotificationType::CustomNotification,
                            //            NotificationManager::NotificationLevel::PrintInfoNotificationLevel, notif_text);
                            //    }
                            //}

                            // BBS
                            // if (printer_technology == ptFFF)
                            //    CustomGCode::update_custom_gcode_per_print_z_from_config(model.custom_gcode_per_print_z, &preset_bundle->project_config);

                            // For exporting from the amf/3mf we shouldn't check printer_presets for the containing information about "Print Host upload"
                            // BBS: add preset combo box re-active logic
                            // currently found only needs re-active here
                            wxGetApp().load_current_presets(false, false);
                            // Update filament colors for the MM-printer profile in the full config
                            // to avoid black (default) colors for Extruders in the ObjectList,
                            // when for extruder colors are used filament colors
                            q->on_filaments_change(preset_bundle->filament_presets.size());

                            ConfigOption *bed_type_opt = preset_bundle->project_config.option("curr_bed_type");
                            if (bed_type_opt != nullptr) q->on_bed_type_change((BedType) bed_type_opt->getInt());
                            is_project_file = true;
                        }
                    }
                    if (!silence) wxGetApp().app_config->update_config_dir(path.parent_path().string());
                }
            } else {
                // BBS: add plate data related logic
                PlateDataPtrs plate_data;
                // BBS: project embedded settings
                std::vector<Preset *> project_presets;
                bool                  is_xxx;
                Semver                file_version;
                model = Slic3r::Model::read_from_file(
                    path.string(), nullptr, nullptr, strategy, &plate_data, &project_presets, &is_xxx, &file_version, nullptr,
                    [this, &dlg, real_filename, &progress_percent, &file_percent, INPUT_FILES_RATIO, total_files, i](int current, int total, bool &cancel)
                    {
                            bool     cont = true;
                            float percent_float = (100.0f * (float)i / (float)total_files) + INPUT_FILES_RATIO * 100.0f * ((float)current / (float)total) / (float)total_files;
                            BOOST_LOG_TRIVIAL(trace) << "load_stl_file: percent(float)=" << percent_float << ", curr = " << current << ", total = " << total;
                            progress_percent = (int)percent_float;
                            wxString msg  = wxString::Format(_L("Loading file: %s"), from_path(real_filename));
                            cont          = dlg.Update(progress_percent, msg);
                            cancel        = !cont;
                     },
                    [this, &dlg, real_filename, &progress_percent, &file_percent, step_percent, INPUT_FILES_RATIO, total_files, i](int load_stage, int current, int total, bool &cancel)
                    {
                            bool     cont = true;
                            float percent_float = (100.0f * (float)i / (float)total_files) + INPUT_FILES_RATIO * ((float)step_percent[load_stage] + (float)current * (float)(step_percent[load_stage + 1] - step_percent[load_stage]) / (float)total) / (float)total_files;
                            BOOST_LOG_TRIVIAL(trace) << "load_step_file: percent(float)=" << percent_float << ", stage = " << load_stage << ", curr = " << current << ", total = " << total;
                            progress_percent = (int)percent_float;
                            wxString msg  = wxString::Format(_L("Loading file: %s"), from_path(real_filename));
                            cont          = dlg.Update(progress_percent, msg);
                            cancel        = !cont;
                    },
                    [](int isUtf8StepFile) {
                        if (!isUtf8StepFile)
                            Slic3r::GUI::show_info(nullptr, _L("Name of components inside step file is not UTF8 format!") + "\n\n" + _L("The name may show garbage characters!"),
                                                   _L("Attention!"));
                    });

                if (type_any_amf && is_xxx) imperial_units = true;

                for (auto obj : model.objects)
                    if (obj->name.empty()) obj->name = fs::path(obj->input_file).filename().string();

                if (plate_data.size() > 0) {
                    partplate_list.load_from_3mf_structure(plate_data);
                    partplate_list.update_slice_context_to_current_plate(background_process);
                    this->preview->update_gcode_result(partplate_list.get_current_slice_result());
                    release_PlateData_list(plate_data);
                    sidebar->obj_list()->reload_all_plates();
                }

                // BBS:: project embedded presets
                if (project_presets.size() > 0) {
                    // load project embedded presets
                    PresetsConfigSubstitutions preset_substitutions;
                    PresetBundle &             preset_bundle = *wxGetApp().preset_bundle;
                    preset_substitutions                     = preset_bundle.load_project_embedded_presets(project_presets, ForwardCompatibilitySubstitutionRule::Enable);
                    if (!preset_substitutions.empty()) show_substitutions_info(preset_substitutions);

                    for (unsigned int i = 0; i < project_presets.size(); i++) { delete project_presets[i]; }
                    project_presets.clear();
                }
            }
        } catch (const ConfigurationError &e) {
            std::string message = GUI::format(_L("Failed loading file \"%1%\". An invalid configuration was found."), filename.string()) + "\n\n" + e.what();
            GUI::show_error(q, message);
            continue;
        } catch (const std::exception &e) {
            if (!is_user_cancel)
                GUI::show_error(q, e.what());
            continue;
        }

        progress_percent = 100.0f * (float)i / (float)total_files + INIT_MODEL_RATIO * 100.0f / (float)total_files;
        dlg_cont = dlg.Update(progress_percent);
        if (!dlg_cont) {
            q->skip_thumbnail_invalid = false;
            return empty_result;
        }

        if (load_model) {
            // The model should now be initialized
            auto convert_from_imperial_units = [](Model &model, bool only_small_volumes) { model.convert_from_imperial_units(only_small_volumes); };

            // BBS: add load_old_project logic
            if ((!is_project_file) && (!load_old_project)) {
                // if (!is_project_file) {
                if (int deleted_objects = model.removed_objects_with_zero_volume(); deleted_objects > 0) {
                    MessageDialog(q, _L("Objects with zero volume removed"), _L("The volume of the object is zero"), wxICON_INFORMATION | wxOK).ShowModal();
                }
                if (imperial_units)
                    // Convert even if the object is big.
                    convert_from_imperial_units(model, false);
                else if (model.looks_like_saved_in_meters()) {
                    // BBS do not handle look like in meters
                    MessageDialog dlg(q,
                                      format_wxstr(_L("The object from file %s is too small, and maybe in meters or inches.\n Do you want to scale to millimeters?"),
                                                   from_path(filename)),
                                      _L("Object too small"), wxICON_QUESTION | wxYES_NO);
                    int           answer = dlg.ShowModal();
                    if (answer == wxID_YES) model.convert_from_meters(true);
                } else if (model.looks_like_imperial_units()) {
                    // BBS do not handle look like in meters
                    MessageDialog dlg(q,
                                      format_wxstr(_L("The object from file %s is too small, and maybe in meters or inches.\n Do you want to scale to millimeters?"),
                                                   from_path(filename)),
                                      _L("Object too small"), wxICON_QUESTION | wxYES_NO);
                    int           answer = dlg.ShowModal();
                    if (answer == wxID_YES) convert_from_imperial_units(model, true);
                }
                // else if (model.looks_like_imperial_units()) {
                // BBS do not handle look like in imperial
                // auto convert_model_if = [convert_from_imperial_units](Model& model, bool condition) {
                //    if (condition)
                //        //FIXME up-scale only the small parts?
                //        convert_from_imperial_units(model, true);
                //};
                // if (answer_convert_from_imperial_units == wxOK_DEFAULT) {
                //    RichMessageDialog dlg(q, format_wxstr(_L_PLURAL(
                //        "The dimensions of the object from file %s seem to be defined in inches.\n"
                //        "The internal unit of PrusaSlicer is a millimeter. Do you want to recalculate the dimensions of the object?",
                //        "The dimensions of some objects from file %s seem to be defined in inches.\n"
                //        "The internal unit of PrusaSlicer is a millimeter. Do you want to recalculate the dimensions of these objects?", model.objects.size()), from_path(filename))
                //        + "\n", _L("The object is too small"), wxICON_QUESTION | wxYES_NO);
                //    dlg.ShowCheckBox(_L("Apply to all the remaining small objects being loaded."));
                //    int answer = dlg.ShowModal();
                //    if (dlg.IsCheckBoxChecked())
                //        answer_convert_from_imperial_units = answer;
                //    else
                //        convert_model_if(model, answer == wxID_YES);
                //}
                // convert_model_if(model, answer_convert_from_imperial_units == wxID_YES);
            }

             if (model.looks_like_multipart_object()) {
               MessageDialog msg_dlg(q, _L(
                    "This file contains several objects positioned at multiple heights.\n"
                    "Instead of considering them as multiple objects, should \n"
                    "the file be loaded as a single object having multiple parts?") + "\n",
                    _L("Multi-part object detected"), wxICON_WARNING | wxYES | wxNO);
                if (msg_dlg.ShowModal() == wxID_YES) {
                    model.convert_multipart_object(filaments_cnt);
                }
            }
        }
        // else if ((wxGetApp().get_mode() == comSimple) && (type_3mf || type_any_amf) && model_has_advanced_features(model)) {
        //    MessageDialog msg_dlg(q, _L("This file cannot be loaded in a simple mode. Do you want to switch to an advanced mode?")+"\n",
        //        _L("Detected advanced data"), wxICON_WARNING | wxYES | wxNO);
        //    if (msg_dlg.ShowModal() == wxID_YES) {
        //        Slic3r::GUI::wxGetApp().save_mode(comAdvanced);
        //        view3D->set_as_dirty();
        //    }
        //    else
        //        return obj_idxs;
        //}

        progress_percent = 100.0f * (float)i / (float)total_files + CENTER_AROUND_ORIGIN_RATIO * 100.0f / (float)total_files;
        dlg_cont = dlg.Update(progress_percent);
        if (!dlg_cont) {
            q->skip_thumbnail_invalid = false;
            return empty_result;
        }

        int model_idx = 0;
        for (ModelObject *model_object : model.objects) {
            if (!type_3mf && !type_any_amf) model_object->center_around_origin(false);

            // BBS
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" << __LINE__ << boost::format("import 3mf IMPORT_LOAD_MODEL_OBJECTS \n");
            wxString msg = wxString::Format("Loading file: %s", from_path(real_filename));
            model_idx++;
            dlg_cont = dlg.Update(progress_percent, msg);
            if (!dlg_cont) {
                q->skip_thumbnail_invalid = false;
                return empty_result;
            }

            model_object->ensure_on_bed(is_project_file);
        }

        tolal_model_count += model_idx;

        progress_percent = 100.0f * (float)i / (float)total_files + LOAD_MODEL_RATIO * 100.0f / (float)total_files;
        dlg_cont = dlg.Update(progress_percent);
        if (!dlg_cont) {
            q->skip_thumbnail_invalid = false;
            return empty_result;
        }

        if (one_by_one) {
            // BBS: add load_old_project logic
            if (type_3mf && !is_project_file && !load_old_project)
                // if (type_3mf && !is_project_file)
                model.center_instances_around_point(this->bed.build_volume().bed_center());
            // BBS: add auxiliary files logic
            // BBS: backup & restore
            if (load_aux) {
                q->model().load_from(model);
                load_auxiliary_files();
            }
            // BBS: don't allow negative_z when load model objects
            // auto loaded_idxs = load_model_objects(model.objects, is_project_file);
            auto loaded_idxs = load_model_objects(model.objects);
            obj_idxs.insert(obj_idxs.end(), loaded_idxs.begin(), loaded_idxs.end());

            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" << __LINE__ << boost::format("import 3mf IMPORT_LOAD_MODEL_OBJECTS \n");
            wxString msg = wxString::Format(_L("Loading file: %s"), from_path(real_filename));
            dlg_cont     = dlg.Update(progress_percent, msg);
            if (!dlg_cont) {
                q->skip_thumbnail_invalid = false;
                return empty_result;
            }
        } else {
            // This must be an .stl or .obj file, which may contain a maximum of one volume.
            for (const ModelObject *model_object : model.objects) {
                new_model->add_object(*model_object);

                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ":" << __LINE__ << boost::format("import 3mf IMPORT_ADD_MODEL_OBJECTS \n");
                wxString msg = wxString::Format(_L("Loading file: %s"), from_path(real_filename));
                dlg_cont     = dlg.Update(progress_percent, msg);
                if (!dlg_cont) {
                    q->skip_thumbnail_invalid = false;
                    return empty_result;
                }
            }
        }
    }

    if (new_model != nullptr && new_model->objects.size() > 1) {
        //BBS do not popup this dialog

        if (ask_multi) {
            MessageDialog msg_dlg(q, _L("Load these files as a single object with multiple parts?\n"), _L("Object with multiple parts was detected"),
                                  wxICON_WARNING | wxYES | wxNO);
            if (msg_dlg.ShowModal() == wxID_YES) { new_model->convert_multipart_object(filaments_cnt); }
        }

        auto loaded_idxs = load_model_objects(new_model->objects);
        obj_idxs.insert(obj_idxs.end(), loaded_idxs.begin(), loaded_idxs.end());
    }

    if (new_model) delete new_model;

    //BBS: translate old 3mf to correct positions
    if (translate_old) {
        //translate the objects
        int plate_count = partplate_list.get_plate_count();
        for (int index = 1; index < plate_count; index ++) {
            PartPlate* cur_plate = (PartPlate *)partplate_list.get_plate(index);

            Vec3d cur_origin = cur_plate->get_origin();
            Vec3d new_origin = partplate_list.compute_origin_using_new_size(index, current_width, current_depth);

            cur_plate->translate_all_instance(new_origin - cur_origin);
        }
        partplate_list.reset_size(current_width, current_depth, current_height, true, true);
    }

    //BBS: add gcode loading logic in the end
    q->m_exported_file = false;
    q->skip_thumbnail_invalid = false;
    if (load_model && load_config) {
        if (model.objects.empty()) {
            partplate_list.load_gcode_files();
            PartPlate * first_plate = nullptr, *cur_plate = nullptr;
            int plate_cnt = partplate_list.get_plate_count();
            int index = 0, first_plate_index = 0;
            q->m_valid_plates_count = 0;
            for (index = 0; index < plate_cnt; index ++)
            {
                cur_plate = partplate_list.get_plate(index);
                if (!first_plate && cur_plate->is_slice_result_valid()) {
                    first_plate = cur_plate;
                    first_plate_index = index;
                }
                if (cur_plate->is_slice_result_valid())
                    q->m_valid_plates_count ++;
            }
            if (first_plate&&first_plate->is_slice_result_valid()) {
                q->m_exported_file = true;
                //select plate 0 as default
                q->select_plate(first_plate_index);
                //set to 3d tab
                q->select_view_3D("Preview");
                wxGetApp().mainframe->select_tab(MainFrame::tpPreview);
            }
            else {
                //set to 3d tab
                q->select_view_3D("3D");
                //select plate 0 as default
                q->select_plate(0);
            }
        }
        else {
            //set to 3d tab
            q->select_view_3D("3D");
            //select plate 0 as default
            q->select_plate(0);
        }
    }
    else {
        //always set to 3D after loading files
        q->select_view_3D("3D");
        wxGetApp().mainframe->select_tab(MainFrame::tp3DEditor);
    }

    if (load_model) {
        if (!silence) wxGetApp().app_config->update_skein_dir(input_files[input_files.size() - 1].parent_path().make_preferred().string());
        // XXX: Plater.pm had @loaded_files, but didn't seem to fill them with the filenames...
    }

    // automatic selection of added objects
    if (!obj_idxs.empty() && view3D != nullptr) {
        // update printable state for new volumes on canvas3D
        wxGetApp().plater()->canvas3D()->update_instance_printable_state_for_objects(obj_idxs);

        Selection& selection = view3D->get_canvas3d()->get_selection();
        selection.clear();
        for (size_t idx : obj_idxs) {
            selection.add_object((unsigned int)idx, false);
        }

        // BBS: update object list selection
        this->sidebar->obj_list()->update_selections();

        if (view3D->get_canvas3d()->get_gizmos_manager().is_enabled())
            // this is required because the selected object changed and the flatten on face an sla support gizmos need to be updated accordingly
            view3D->get_canvas3d()->update_gizmos_on_off_state();
    }

    GLGizmoSimplify::add_simplify_suggestion_notification(
        obj_idxs, model.objects, *notification_manager);


    if (tolal_model_count <= 0 && !q->m_exported_file) {
        dlg.Hide();
        if (!is_user_cancel) {
            MessageDialog msg(wxGetApp().mainframe, _L("The file does not contain any geometry data."), _L("Warning"), wxYES | wxICON_WARNING);
            if (msg.ShowModal() == wxID_YES) {}
        }
    }
    return obj_idxs;
}

 #define AUTOPLACEMENT_ON_LOAD

std::vector<size_t> Plater::priv::load_model_objects(const ModelObjectPtrs& model_objects, bool allow_negative_z, bool split_object)
{
    const Vec3d bed_size = Slic3r::to_3d(this->bed.build_volume().bounding_volume2d().size(), 1.0) - 2.0 * Vec3d::Ones();

#ifndef AUTOPLACEMENT_ON_LOAD
    // bool need_arrange = false;
#endif /* AUTOPLACEMENT_ON_LOAD */
    bool scaled_down = false;
    std::vector<size_t> obj_idxs;
    unsigned int obj_count = model.objects.size();

#ifdef AUTOPLACEMENT_ON_LOAD
    ModelInstancePtrs new_instances;
#endif /* AUTOPLACEMENT_ON_LOAD */
    for (ModelObject *model_object : model_objects) {
        auto *object = model.add_object(*model_object);
        object->sort_volumes(true);
        std::string object_name = object->name.empty() ? fs::path(object->input_file).filename().string() : object->name;
        obj_idxs.push_back(obj_count++);

        if (model_object->instances.empty()) {
#ifdef AUTOPLACEMENT_ON_LOAD
            object->center_around_origin();
            new_instances.emplace_back(object->add_instance());
#else /* AUTOPLACEMENT_ON_LOAD */
            // if object has no defined position(s) we need to rearrange everything after loading
            // need_arrange = true;
             // add a default instance and center object around origin
            object->center_around_origin();  // also aligns object to Z = 0
            ModelInstance* instance = object->add_instance();

            //BBS calc transformation
            Geometry::Transformation t = instance->get_transformation();
            instance->set_offset(Slic3r::to_3d(this->bed.build_volume().bed_center(), -object->origin_translation(2)));
#endif /* AUTOPLACEMENT_ON_LOAD */
        }

        //BBS: when the object is too large, let the user choose whether to scale it down
        for (size_t i = 0; i < object->instances.size(); ++i) {
            ModelInstance* instance = object->instances[i];
            const Vec3d size = object->instance_bounding_box(i).size();
            const Vec3d ratio = size.cwiseQuotient(bed_size);
            const double max_ratio = std::max(ratio(0), ratio(1));
            if (max_ratio > 10000) {
                MessageDialog dlg(q, _L("Your object appears to be too large, Do you want to scale it down to fit the heat bed automatically?"), _L("Object too large"),
                                  wxICON_QUESTION | wxYES_NO);
                int           answer = dlg.ShowModal();
                if (answer == wxID_YES) {
                    // the size of the object is too big -> this could lead to overflow when moving to clipper coordinates,
                    // so scale down the mesh
                    object->scale_mesh_after_creation(1. / max_ratio);
                    object->origin_translation = Vec3d::Zero();
                    object->center_around_origin();
                    scaled_down = true;
                    break;
                }
            }
            else if (max_ratio > 10) {
                MessageDialog dlg(q, _L("Your object appears to be too large, Do you want to scale it down to fit the heat bed automatically?"), _L("Object too large"),
                                  wxICON_QUESTION | wxYES_NO);
                int           answer = dlg.ShowModal();
                if (answer == wxID_YES) {
                    instance->set_scaling_factor(instance->get_scaling_factor() / max_ratio);
                    scaled_down = true;
                }
            }
        }

        object->ensure_on_bed(allow_negative_z);
        if (!split_object) {
            //BBS initial assemble transformation
            for (ModelObject* model_object : model.objects) {
                //BBS initialize assemble transformation
                for (int i = 0; i < model_object->instances.size(); i++) {
                    if (!model_object->instances[i]->is_assemble_initialized()) {
                        model_object->instances[i]->set_assemble_transformation(model_object->instances[i]->get_transformation());
                    }
                }
            }
        }
    }

#ifdef AUTOPLACEMENT_ON_LOAD
#if 0
    // FIXME distance should be a config value /////////////////////////////////
    auto min_obj_distance = static_cast<coord_t>(6/SCALING_FACTOR);
    const auto *bed_shape_opt = config->opt<ConfigOptionPoints>("printable_area");
    assert(bed_shape_opt);
    auto& bedpoints = bed_shape_opt->values;
    Polyline bed; bed.points.reserve(bedpoints.size());
    for(auto& v : bedpoints) bed.append(Point::new_scale(v(0), v(1)));

    // BBS: get wipe tower of current plate
    int cur_plate_idx = partplate_list.get_curr_plate_index();
    std::pair<bool, GLCanvas3D::WipeTowerInfo> wti = view3D->get_canvas3d()->get_wipe_tower_info(cur_plate_idx);

    arr::find_new_position(model, new_instances, min_obj_distance, bed, wti);

    // it remains to move the wipe tower:
    view3D->get_canvas3d()->arrange_wipe_tower(wti);
#else
    // BBS: find an empty cell to put the copied object
    for (auto& instance : new_instances) {
        auto offset = instance->get_offset();
        auto start_point = this->bed.build_volume().bounding_volume2d().center();
        auto empty_cell = wxGetApp().plater()->canvas3D()->get_nearest_empty_cell({ start_point(0),start_point(1) });
        Vec3d displacement = { empty_cell.x(),empty_cell.y(),offset(2)};
        instance->set_offset(displacement);
    }
#endif

#endif /* AUTOPLACEMENT_ON_LOAD */

    //BBS: remove the auto scaled_down logic when load models
    //if (scaled_down) {
    //    GUI::show_info(q,
    //        _L("Your object appears to be too large, so it was automatically scaled down to fit your print bed."),
    //        _L("Object too large?"));
    //}

    notification_manager->close_notification_of_type(NotificationType::UpdatedItemsInfo);
    for (const size_t idx : obj_idxs) {
        wxGetApp().obj_list()->add_object_to_list(idx);
    }

    update();
    // Update InfoItems in ObjectList after update() to use of a correct value of the GLCanvas3D::is_sinking(),
    // which is updated after a view3D->reload_scene(false, flags & (unsigned int)UpdateParams::FORCE_FULL_SCREEN_REFRESH) call
    for (const size_t idx : obj_idxs)
        wxGetApp().obj_list()->update_info_items(idx);

    object_list_changed();

    this->schedule_background_process();

    return obj_idxs;
}

// BBS
void Plater::priv::load_auxiliary_files()
{
    std::string auxiliary_path = encode_path(q->model().get_auxiliary_file_temp_path().c_str());
    wxGetApp().mainframe->m_auxiliary->Reload(auxiliary_path);
}

fs::path Plater::priv::get_export_file_path(GUI::FileType file_type)
{
    // Update printbility state of each of the ModelInstances.
    this->update_print_volume_state();

    const Selection& selection = get_selection();
    int obj_idx = selection.get_object_idx();

    fs::path output_file;
    if (file_type == FT_3MF)
        // for 3mf take the path from the project filename, if any
        output_file = into_path(get_project_filename(".3mf"));

    //bbs  name the project using the part name
    if (output_file.empty()) {
        if (get_project_name() != _L("Untitled")) {
            output_file = into_path(get_project_name() + ".3mf");
        }
    }

    if (output_file.empty())
    {
        // first try to get the file name from the current selection
        if ((0 <= obj_idx) && (obj_idx < (int)this->model.objects.size()))
            output_file = this->model.objects[obj_idx]->get_export_filename();

        if (output_file.empty())
            // Find the file name of the first printable object.
            output_file = this->model.propose_export_file_name_and_path();

        if (output_file.empty() && !model.objects.empty())
            // Find the file name of the first object.
            output_file = this->model.objects[0]->get_export_filename();

        if (output_file.empty())
            // Use _L("Untitled") name
            output_file = into_path(_L("Untitled"));
    }
    return output_file;
}

wxString Plater::priv::get_export_file(GUI::FileType file_type)
{
    wxString wildcard;
    switch (file_type) {
        case FT_STL:
        case FT_AMF:
        case FT_3MF:
        case FT_GCODE:
        case FT_OBJ:
            wildcard = file_wildcards(file_type);
        break;
        default:
            wildcard = file_wildcards(FT_MODEL);
        break;
    }

    fs::path output_file = get_export_file_path(file_type);

    wxString dlg_title;
    switch (file_type) {
        case FT_STL:
        {
            output_file.replace_extension("stl");
            dlg_title = _L("Export STL file:");
            break;
        }
        case FT_AMF:
        {
            // XXX: Problem on OS X with double extension?
            output_file.replace_extension("zip.amf");
            dlg_title = _devL("Export AMF file:");
            break;
        }
        case FT_3MF:
        {
            output_file.replace_extension("3mf");
            dlg_title = _L("Save file as:");
            break;
        }
        case FT_OBJ:
        {
            output_file.replace_extension("obj");
            dlg_title = _devL("Export OBJ file:");
            break;
        }
        default: break;
    }

    std::string out_dir = (boost::filesystem::path(output_file).parent_path()).string();

    wxFileDialog dlg(q, dlg_title,
        is_shapes_dir(out_dir) ? from_u8(wxGetApp().app_config->get_last_dir()) : from_path(output_file.parent_path()), from_path(output_file.filename()),
        wildcard, wxFD_SAVE | wxFD_OVERWRITE_PROMPT | wxPD_APP_MODAL);

    int result = dlg.ShowModal();
    if (result == wxID_CANCEL)
        return "<cancel>";
    if (result != wxID_OK)
        return wxEmptyString;

    wxString out_path = dlg.GetPath();
    fs::path path(into_path(out_path));
    wxGetApp().app_config->update_last_output_dir(path.parent_path().string());

    return out_path;
}

const Selection& Plater::priv::get_selection() const
{
    return view3D->get_canvas3d()->get_selection();
}

Selection& Plater::priv::get_selection()
{
    return view3D->get_canvas3d()->get_selection();
}

Selection& Plater::priv::get_curr_selection()
{
    return get_current_canvas3D()->get_selection();
}

int Plater::priv::get_selected_object_idx() const
{
    int idx = get_selection().get_object_idx();
    return ((0 <= idx) && (idx < 1000)) ? idx : -1;
}

int Plater::priv::get_selected_volume_idx() const
{
    auto& selection = get_selection();
    int idx = selection.get_object_idx();
    if ((0 > idx) || (idx > 1000))
        return-1;
    const GLVolume* v = selection.get_volume(*selection.get_volume_idxs().begin());
    if (model.objects[idx]->volumes.size() > 1)
        return v->volume_idx();
    return -1;
}

void Plater::priv::selection_changed()
{
    // if the selection is not valid to allow for layer editing, we need to turn off the tool if it is running
    if (!layers_height_allowed() && view3D->is_layers_editing_enabled()) {
        SimpleEvent evt(EVT_GLTOOLBAR_LAYERSEDITING);
        on_action_layersediting(evt);
    }

    // forces a frame render to update the view (to avoid a missed update if, for example, the context menu appears)
    view3D->render();
}

void Plater::priv::object_list_changed()
{
    const bool export_in_progress = this->background_process.is_export_scheduled(); // || ! send_gcode_file.empty());
    // XXX: is this right?
    //const bool model_fits = view3D->get_canvas3d()->check_volumes_outside_state() == ModelInstancePVS_Inside;
    const bool model_fits = view3D->get_canvas3d()->check_volumes_outside_state() != ModelInstancePVS_Partly_Outside;

    PartPlate* part_plate = partplate_list.get_curr_plate();

    // BBS
    //sidebar->enable_buttons(!model.objects.empty() && !export_in_progress && model_fits && part_plate->has_printable_instances());
    bool can_slice = !model.objects.empty() && !export_in_progress && model_fits && part_plate->has_printable_instances();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": can_slice %1%, model_fits= %2%, export_in_progress %3%, has_printable_instances %4% ")%can_slice %model_fits %export_in_progress %part_plate->has_printable_instances();
    main_frame->update_slice_print_status(MainFrame::eEventObjectUpdate, can_slice);

    wxGetApp().params_panel()->notify_object_config_changed();
}

void Plater::priv::select_curr_plate_all()
{
    view3D->select_curr_plate_all();
    this->sidebar->obj_list()->update_selections();
}

void Plater::priv::remove_curr_plate_all()
{
    SingleSnapshot ss(q);
    view3D->remove_curr_plate_all();
    this->sidebar->obj_list()->update_selections();
}

void Plater::priv::select_all()
{
    view3D->select_all();
    this->sidebar->obj_list()->update_selections();
}

void Plater::priv::deselect_all()
{
    view3D->deselect_all();
}

void Plater::priv::remove(size_t obj_idx)
{
    if (view3D->is_layers_editing_enabled())
        view3D->enable_layers_editing(false);

    m_ui_jobs.cancel_all();
    model.delete_object(obj_idx);
    //BBS: notify partplate the instance removed
    partplate_list.notify_instance_removed(obj_idx, -1);
    update();
    // Delete object from Sidebar list. Do it after update, so that the GLScene selection is updated with the modified model.
    sidebar->obj_list()->delete_object_from_list(obj_idx);
    object_list_changed();
}


void Plater::priv::delete_object_from_model(size_t obj_idx, bool refresh_immediately)
{
    std::string snapshot_label = "Delete Object";
    if (! model.objects[obj_idx]->name.empty())
        snapshot_label += ": " + model.objects[obj_idx]->name;
    Plater::TakeSnapshot snapshot(q, snapshot_label);
    m_ui_jobs.cancel_all();
    model.delete_object(obj_idx);
    //BBS: notify partplate the instance removed
    partplate_list.notify_instance_removed(obj_idx, -1);

    //BBS
    if (refresh_immediately) {
        update();
        object_list_changed();
    }
}

void Plater::priv::delete_all_objects_from_model()
{
    Plater::TakeSnapshot snapshot(q, "Delete All Objects");

    if (view3D->is_layers_editing_enabled())
        view3D->enable_layers_editing(false);

    reset_gcode_toolpaths();
    gcode_result.reset();

    view3D->get_canvas3d()->reset_sequential_print_clearance();

    m_ui_jobs.cancel_all();

    // Stop and reset the Print content.
    background_process.reset();

    //BBS: update partplate
    partplate_list.clear();

    model.clear_objects();
    update();
    // Delete object from Sidebar list. Do it after update, so that the GLScene selection is updated with the modified model.
    sidebar->obj_list()->delete_all_objects_from_list();
    object_list_changed();

    //BBS
    model.plates_custom_gcodes.clear();
}

void Plater::priv::reset(bool apply_presets_change)
{
    Plater::TakeSnapshot snapshot(q, "Reset Project", UndoRedo::SnapshotType::ProjectSeparator);

    clear_warnings();

    set_project_filename("");

    if (view3D->is_layers_editing_enabled())
        view3D->get_canvas3d()->force_main_toolbar_left_action(view3D->get_canvas3d()->get_main_toolbar_item_id("layersediting"));

    reset_gcode_toolpaths();
    //BBS: update gcode to current partplate's
    //GCodeProcessorResult* current_result = this->background_process.get_current_plate()->get_slice_result();
    //current_result->reset();
    //gcode_result.reset();

    view3D->get_canvas3d()->reset_sequential_print_clearance();

    m_ui_jobs.cancel_all();

    //BBS: clear the partplate list's object before object cleared
    partplate_list.reinit();
    partplate_list.update_slice_context_to_current_plate(background_process);
    preview->update_gcode_result(partplate_list.get_current_slice_result());

    // Stop and reset the Print content.
    this->background_process.reset();
    model.clear_objects();
    update();

    //BBS
    if (wxGetApp().is_editor()) {
        // Delete object from Sidebar list. Do it after update, so that the GLScene selection is updated with the modified model.
        sidebar->obj_list()->delete_all_objects_from_list();
        object_list_changed();
    }

    project.reset();

    //BBS: reset all project embedded presets
    wxGetApp().preset_bundle->reset_project_embedded_presets();
    if (apply_presets_change)
        wxGetApp().apply_keeped_preset_modifications();
    else
        wxGetApp().load_current_presets(false, false);

    //BBS
    model.plates_custom_gcodes.clear();

    // BBS
    m_saved_timestamp = m_backup_timestamp = size_t(-1);
}

void Plater::priv::center_selection()
{
    view3D->center_selected();
}

void Plater::priv::mirror(Axis axis)
{
    view3D->mirror_selection(axis);
}

void Plater::find_new_position(const ModelInstancePtrs &instances)
{
    arrangement::ArrangePolygons movable, fixed;
    arrangement::ArrangeParams arr_params = get_arrange_params(this);

    for (const ModelObject *mo : p->model.objects)
        for (ModelInstance *inst : mo->instances) {
            auto it = std::find(instances.begin(), instances.end(), inst);
            arrangement::ArrangePolygon arrpoly;
            inst->get_arrange_polygon(&arrpoly);

            if (it == instances.end())
                fixed.emplace_back(std::move(arrpoly));
            else {
                arrpoly.setter = [it](const arrangement::ArrangePolygon &p) {
                    if (p.is_arranged() && p.bed_idx == 0) {
                        Vec2d t = p.translation.cast<double>();
                        (*it)->apply_arrange_result(t, p.rotation);
                    }
                };
                movable.emplace_back(std::move(arrpoly));
            }
        }

    if (auto wt = get_wipe_tower_arrangepoly(*this))
        fixed.emplace_back(*wt);

    arrangement::arrange(movable, fixed, this->build_volume().polygon(), arr_params);

    for (auto & m : movable)
        m.apply();
}

void Plater::priv::split_object()
{
    int obj_idx = get_selected_object_idx();
    if (obj_idx == -1)
        return;

    // we clone model object because split_object() adds the split volumes
    // into the same model object, thus causing duplicates when we call load_model_objects()
    Model new_model = model;
    ModelObject* current_model_object = new_model.objects[obj_idx];

    wxBusyCursor wait;
    ModelObjectPtrs new_objects;
    current_model_object->split(&new_objects);
    if (new_objects.size() == 1)
        // #ysFIXME use notification
        Slic3r::GUI::warning_catcher(q, _L("The selected object couldn't be split."));
    else
    {
        // BBS no solid parts removed
        // If we splited object which is contain some parts/modifiers then all non-solid parts (modifiers) were deleted
        //if (current_model_object->volumes.size() > 1 && current_model_object->volumes.size() != new_objects.size())
        //    notification_manager->push_notification(NotificationType::CustomNotification,
        //        NotificationManager::NotificationLevel::PrintInfoNotificationLevel,
        //        _u8L("All non-solid parts (modifiers) were deleted"));

        Plater::TakeSnapshot snapshot(q, "Split to Objects");

        remove(obj_idx);

        // load all model objects at once, otherwise the plate would be rearranged after each one
        // causing original positions not to be kept
        //BBS: set split_object to true to avoid re-compute assemble matrix
        std::vector<size_t> idxs = load_model_objects(new_objects, false, true);

        // select newly added objects
        for (size_t idx : idxs)
        {
            get_selection().add_object((unsigned int)idx, false);
        }
    }
}

void Plater::priv::split_volume()
{
    wxGetApp().obj_list()->split();
}

void Plater::priv::scale_selection_to_fit_print_volume()
{
#if ENABLE_ENHANCED_PRINT_VOLUME_FIT
    this->view3D->get_canvas3d()->get_selection().scale_to_fit_print_volume(this->bed.build_volume());
#else
    this->view3D->get_canvas3d()->get_selection().scale_to_fit_print_volume(*config);
#endif // ENABLE_ENHANCED_PRINT_VOLUME_FIT
}

void Plater::priv::schedule_background_process()
{
    delayed_error_message.clear();
    // Trigger the timer event after 0.5s
    this->background_process_timer.Start(500, wxTIMER_ONE_SHOT);
    // Notify the Canvas3D that something has changed, so it may invalidate some of the layer editing stuff.
    this->view3D->get_canvas3d()->set_config(this->config);
}

void Plater::priv::update_print_volume_state()
{
    //BBS: use the plate's bounding box instead of the bed's
    PartPlate* pp = partplate_list.get_curr_plate();
    BuildVolume build_volume(pp->get_shape(), this->bed.build_volume().printable_height());
    this->model.update_print_volume_state(build_volume);
}

void Plater::priv::process_validation_warning(StringObjectException const &warning) const
{
    if (warning.string.empty())
        notification_manager->close_notification_of_type(NotificationType::ValidateWarning);
    else {
        std::string text = warning.string;
        auto po = dynamic_cast<PrintObjectBase const *>(warning.object);
        auto mo = po ? po->model_object() : dynamic_cast<ModelObject const *>(warning.object);
        auto action_fn = (mo || !warning.opt_key.empty()) ? [id = mo ? mo->id() : 0, opt = warning.opt_key](wxEvtHandler *) {
		    auto & objects = wxGetApp().model().objects;
		    auto iter = id.id ? std::find_if(objects.begin(), objects.end(), [id](auto o) { return o->id() == id; }) : objects.end();
            if (iter != objects.end()) {
                wxGetApp().mainframe->select_tab(MainFrame::tp3DEditor);
			    wxGetApp().obj_list()->select_items({{*iter, nullptr}});
            }
            if (!opt.empty()) {
                if (iter != objects.end())
				    wxGetApp().params_panel()->switch_to_object();
                wxGetApp().sidebar().jump_to_option(opt, Preset::TYPE_PRINT, L"");
		    }
		    return false;
	    } : std::function<bool(wxEvtHandler *)>();
        auto hypertext = (mo || !warning.opt_key.empty()) ? _u8L("Jump to") : "";
        if (mo) hypertext += std::string(" [") + mo->name + "]";
        if (!warning.opt_key.empty()) hypertext += std::string(" (") + warning.opt_key + ")";

        // BBS disable support enforcer
        //if (text == "_SUPPORTS_OFF") {
        //    text = _u8L("An object has custom support enforcers which will not be used "
        //                "because supports are disabled.")+"\n";
        //    hypertext = _u8L("Enable supports for enforcers only");

        //    action_fn = [](wxEvtHandler*) {
        //        Tab* print_tab = wxGetApp().get_tab(Preset::TYPE_PRINT);
        //        assert(print_tab);
        //        DynamicPrintConfig& config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
        //        config.set_key_value("enable_support", new ConfigOptionBool(true));
        //        config.set_key_value("auto_support_type", new ConfigOptionEnum<SupportType>(stNormalAuto));
        //        print_tab->on_value_change("enable_support", config.opt_bool("enable_support"));
        //        print_tab->on_value_change("support_material_auto", config.opt_bool("support_material_auto"));
        //        return true;
        //    };
        //}

        notification_manager->push_notification(
            NotificationType::ValidateWarning,
            NotificationManager::NotificationLevel::WarningNotificationLevel,
            _u8L("WARNING:") + "\n" + text, hypertext, action_fn
        );
    }
}


// Update background processing thread from the current config and Model.
// Returns a bitmask of UpdateBackgroundProcessReturnState.
unsigned int Plater::priv::update_background_process(bool force_validation, bool postpone_error_messages, bool switch_print)
{
    // bitmap of enum UpdateBackgroundProcessReturnState
    unsigned int return_state = 0;

    // If the update_background_process() was not called by the timer, kill the timer,
    // so the update_restart_background_process() will not be called again in vain.
    background_process_timer.Stop();
    // Update the "out of print bed" state of ModelInstances.
    update_print_volume_state();
    // Apply new config to the possibly running background task.
    bool               was_running = background_process.running();
    //BBS: add the switch print logic before Print::Apply
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": enter, force_validation=%1% postpone_error_messages=%2%, switch_print=%3%, was_running=%4%")%force_validation %postpone_error_messages %switch_print %was_running;
    if (switch_print)
    {
        // Update the "out of print bed" state of ModelInstances.
        this->update_print_volume_state();
        //BBS: update the current print to the current plate
        this->partplate_list.update_slice_context_to_current_plate(background_process);
        this->preview->update_gcode_result(partplate_list.get_current_slice_result());
    }
    Print::ApplyStatus invalidated = background_process.apply(this->model, wxGetApp().preset_bundle->full_config());

    if ((invalidated == Print::APPLY_STATUS_CHANGED) || (invalidated == Print::APPLY_STATUS_INVALIDATED))
        // BBS: add only gcode mode
        q->set_only_gcode(false);

    //BBS: add slicing related logs
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": background process apply result=%1%")%invalidated;
    if (background_process.empty())
        view3D->get_canvas3d()->reset_sequential_print_clearance();

    if (invalidated == Print::APPLY_STATUS_INVALIDATED) {
        //BBS: update current plater's slicer result to invalid
        this->background_process.get_current_plate()->update_slice_result_valid_state(false);

        //no need, should be done in background_process.apply
        //this->background_process.get_current_gcode_result()->reset();
        // Reset preview canvases. If the print has been invalidated, the preview canvases will be cleared.
        // Otherwise they will be just refreshed.
        if (preview != nullptr) {
            // If the preview is not visible, the following line just invalidates the preview,
            // but the G-code paths or SLA preview are calculated first once the preview is made visible.
            reset_gcode_toolpaths();
            preview->reload_print();
        }
        // In FDM mode, we need to reload the 3D scene because of the wipe tower preview box.
        // In SLA mode, we need to reload the 3D scene every time to show the support structures.
        if (printer_technology == ptSLA || (printer_technology == ptFFF && config->opt_bool("enable_prime_tower")))
            return_state |= UPDATE_BACKGROUND_PROCESS_REFRESH_SCENE;

        notification_manager->set_slicing_progress_hidden();
    }

    if ((invalidated != Print::APPLY_STATUS_UNCHANGED || force_validation) && ! background_process.empty()) {
        // The delayed error message is no more valid.
        delayed_error_message.clear();
        // The state of the Print changed, and it is non-zero. Let's validate it and give the user feedback on errors.

        //BBS: add is_warning logic
        StringObjectException warning;
        //BBS: refine seq-print logic
        Polygons polygons;
        std::vector<std::pair<Polygon, float>> height_polygons;
        StringObjectException err = background_process.validate(&warning, &polygons, &height_polygons);
        // update string by type
        q->post_process_string_object_exception(err);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": validate err=%1%, warning=%2%")%err.string%warning.string;

        if (err.string.empty()) {
            this->partplate_list.get_curr_plate()->update_apply_result_invalid(false);
            notification_manager->set_all_slicing_errors_gray(true);
            notification_manager->close_notification_of_type(NotificationType::ValidateError);
            if (invalidated != Print::APPLY_STATUS_UNCHANGED && background_processing_enabled())
                return_state |= UPDATE_BACKGROUND_PROCESS_RESTART;

            // Pass a warning from validation and either show a notification,
            // or hide the old one.
            process_validation_warning(warning);
            if (printer_technology == ptFFF) {
                view3D->get_canvas3d()->reset_sequential_print_clearance();
                view3D->get_canvas3d()->set_as_dirty();
                view3D->get_canvas3d()->request_extra_frame();
            }
        }
        else {
            this->partplate_list.get_curr_plate()->update_apply_result_invalid(true);
            // The print is not valid.
            // Show error as notification.
            notification_manager->push_validate_error_notification(err);
            //also update the warnings
            process_validation_warning(warning);
            return_state |= UPDATE_BACKGROUND_PROCESS_INVALID;
            if (printer_technology == ptFFF) {
                const Print* print = background_process.fff_print();
                //Polygons polygons;
                //if (print->config().print_sequence == PrintSequence::ByObject)
                //    Print::sequential_print_clearance_valid(*print, &polygons);
                view3D->get_canvas3d()->set_sequential_print_clearance_visible(true);
                view3D->get_canvas3d()->set_sequential_print_clearance_render_fill(true);
                view3D->get_canvas3d()->set_sequential_print_clearance_polygons(polygons, height_polygons);
            }
        }
    }
    else if (! this->delayed_error_message.empty()) {
        // Reusing the old state.
        return_state |= UPDATE_BACKGROUND_PROCESS_INVALID;
    }

    //actualizate warnings
    if (invalidated != Print::APPLY_STATUS_UNCHANGED || background_process.empty()) {
        if (background_process.empty())
            process_validation_warning({});
        actualize_slicing_warnings(*this->background_process.current_print());
        actualize_object_warnings(*this->background_process.current_print());
        show_warning_dialog = false;
        process_completed_with_error = -1;
    }

    if (invalidated != Print::APPLY_STATUS_UNCHANGED && was_running && ! this->background_process.running() &&
        (return_state & UPDATE_BACKGROUND_PROCESS_RESTART) == 0) {
        // The background processing was killed and it will not be restarted.
        // Post the "canceled" callback message, so that it will be processed after any possible pending status bar update messages.
        SlicingProcessCompletedEvent evt(EVT_PROCESS_COMPLETED, 0,
            SlicingProcessCompletedEvent::Cancelled, nullptr);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" %1%, post an EVT_PROCESS_COMPLETED to main, status %2%")%__LINE__ %evt.status();
        wxQueueEvent(q, evt.Clone());
    }

    if ((return_state & UPDATE_BACKGROUND_PROCESS_INVALID) != 0)
    {
        // Validation of the background data failed.
        //BBS: add slice&&print status update logic
        this->main_frame->update_slice_print_status(MainFrame::eEventSliceUpdate, false);

        process_completed_with_error = partplate_list.get_curr_plate_index();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", Line %1%: set to process_completed_with_error, return_state=%2%")%__LINE__%return_state;
    }
    else
    {
        // Background data is valid.
        if ((return_state & UPDATE_BACKGROUND_PROCESS_RESTART) != 0 ||
            (return_state & UPDATE_BACKGROUND_PROCESS_REFRESH_SCENE) != 0 )
            notification_manager->set_slicing_progress_hidden();

        //BBS: add slice&&print status update logic
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", Line %1%: background data valid, return_state=%2%")%__LINE__%return_state;
        PartPlate* cur_plate = background_process.get_current_plate();
        if (background_process.finished() && cur_plate && cur_plate->is_slice_result_valid())
        {
            //ready_to_slice = false;
            this->main_frame->update_slice_print_status(MainFrame::eEventSliceUpdate, false);
        }
        else if (!background_process.empty() &&
                 !background_process.running()) /* Do not update buttons if background process is running
                                                 * This condition is important for SLA mode especially,
                                                 * when this function is called several times during calculations
                                                 * */
        {
            if (cur_plate->can_slice()) {
                //ready_to_slice = true;
                this->main_frame->update_slice_print_status(MainFrame::eEventSliceUpdate, true);
                process_completed_with_error = -1;
            }
            else {
                //ready_to_slice = false;
                this->main_frame->update_slice_print_status(MainFrame::eEventSliceUpdate, false);
                process_completed_with_error = partplate_list.get_curr_plate_index();
            }
        }
#if 0
        //sidebar->set_btn_label(ActionButtonType::abExport, _(label_btn_export));
        //sidebar->set_btn_label(ActionButtonType::abSendGCode, _(label_btn_send));

        //const wxString slice_string = background_process.running() && wxGetApp().get_mode() == comSimple ?
        //                              _L("Slicing") + dots : _L("Slice now");
        //sidebar->set_btn_label(ActionButtonType::abReslice, slice_string);

        //if (background_process.finished())
        //    show_action_buttons(false);
        //else if (!background_process.empty() &&
        //         !background_process.running()) /* Do not update buttons if background process is running
        //                                         * This condition is important for SLA mode especially,
        //                                         * when this function is called several times during calculations
        //                                         * */
        //    show_action_buttons(true);
#endif
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", Line %1%: exit, return_state=%2%")%__LINE__%return_state;
    return return_state;
}

// Restart background processing thread based on a bitmask of UpdateBackgroundProcessReturnState.
bool Plater::priv::restart_background_process(unsigned int state)
{
    if (m_ui_jobs.is_any_running()) {
        // Avoid a race condition
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: ui jobs running, return false")%__LINE__;
        return false;
    }

    if ( ! this->background_process.empty() &&
         (state & priv::UPDATE_BACKGROUND_PROCESS_INVALID) == 0 &&
         ( ((state & UPDATE_BACKGROUND_PROCESS_FORCE_RESTART) != 0 && ! this->background_process.finished()) ||
           (state & UPDATE_BACKGROUND_PROCESS_FORCE_EXPORT) != 0 ||
           (state & UPDATE_BACKGROUND_PROCESS_RESTART) != 0 ) ) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", Line %1%: print is valid, try to start it now")%__LINE__;
        // The print is valid and it can be started.
        if (this->background_process.start()) {
            if (!show_warning_dialog)
                on_slicing_began();
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", Line %1%: start successfully")%__LINE__;
            return true;
        }
    }
    else if (this->background_process.empty()) {
        PartPlate* cur_plate = background_process.get_current_plate();
        if (cur_plate->is_slice_result_valid() && ((state & UPDATE_BACKGROUND_PROCESS_FORCE_RESTART) != 0)) {
            if (this->background_process.start()) {
                if (!show_warning_dialog)
                    on_slicing_began();
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", Line %1%: start successfully")%__LINE__;
                return true;
            }
        }
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", Line %1%: not started")%__LINE__;
    return false;
}

void Plater::priv::export_gcode(fs::path output_path, bool output_path_on_removable_media)
{
    wxCHECK_RET(!(output_path.empty()), "export_gcode: output_path and upload_job empty");

    BOOST_LOG_TRIVIAL(info) << boost::format("export_gcode: output_path %1%")%output_path.string();
    if (model.objects.empty())
        return;

    if (background_process.is_export_scheduled()) {
        GUI::show_error(q, _L("Another export job is running."));
        return;
    }

    // bitmask of UpdateBackgroundProcessReturnState
    unsigned int state = update_background_process(true);
    if (state & priv::UPDATE_BACKGROUND_PROCESS_REFRESH_SCENE)
        view3D->reload_scene(false);

    if ((state & priv::UPDATE_BACKGROUND_PROCESS_INVALID) != 0)
        return;

    show_warning_dialog = false;
    if (! output_path.empty()) {
        background_process.schedule_export(output_path.string(), output_path_on_removable_media);
        notification_manager->push_delayed_notification(NotificationType::ExportOngoing, []() {return true; }, 1000, 0);
    } else {
        BOOST_LOG_TRIVIAL(info) << "output_path  is empty";
    }

    // If the SLA processing of just a single object's supports is running, restart slicing for the whole object.
    this->background_process.set_task(PrintBase::TaskParams());
    this->restart_background_process(priv::UPDATE_BACKGROUND_PROCESS_FORCE_EXPORT);
}
void Plater::priv::export_gcode(fs::path output_path, bool output_path_on_removable_media, PrintHostJob upload_job)
{
    wxCHECK_RET(!(output_path.empty() && upload_job.empty()), "export_gcode: output_path and upload_job empty");

    if (model.objects.empty())
        return;

    if (background_process.is_export_scheduled()) {
        GUI::show_error(q, _L("Another export job is running."));
        return;
    }

    // bitmask of UpdateBackgroundProcessReturnState
    unsigned int state = update_background_process(true);
    if (state & priv::UPDATE_BACKGROUND_PROCESS_REFRESH_SCENE)
        view3D->reload_scene(false);

    if ((state & priv::UPDATE_BACKGROUND_PROCESS_INVALID) != 0)
        return;

    show_warning_dialog = false;
    if (! output_path.empty()) {
        background_process.schedule_export(output_path.string(), output_path_on_removable_media);
        notification_manager->push_delayed_notification(NotificationType::ExportOngoing, []() {return true; }, 1000, 0);
    } else {
        background_process.schedule_upload(std::move(upload_job));
    }

    // If the SLA processing of just a single object's supports is running, restart slicing for the whole object.
    this->background_process.set_task(PrintBase::TaskParams());
    this->restart_background_process(priv::UPDATE_BACKGROUND_PROCESS_FORCE_EXPORT);
}
unsigned int Plater::priv::update_restart_background_process(bool force_update_scene, bool force_update_preview)
{
    bool switch_print = true;
    //BBS: judge whether can switch print or not
    if ((partplate_list.get_plate_count() > 1) && !this->background_process.can_switch_print())
    {
        //can not switch print currently
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(": plate count %1%, can not switch") % partplate_list.get_plate_count();
        switch_print = false;
    }
    // bitmask of UpdateBackgroundProcessReturnState
    unsigned int state = this->update_background_process(false, false, switch_print);
    if (force_update_scene || (state & UPDATE_BACKGROUND_PROCESS_REFRESH_SCENE) != 0)
        view3D->reload_scene(false);

    if (force_update_preview)
        this->preview->reload_print();
    this->restart_background_process(state);
    return state;
}

void Plater::priv::update_fff_scene()
{
    if (this->preview != nullptr)
        this->preview->reload_print();
    // In case this was MM print, wipe tower bounding box on 3D tab might need redrawing with exact depth:
    view3D->reload_scene(true);
    //BBS: add assemble view related logic
    assemble_view->reload_scene(true);
}

//BBS: add print project related logic
void Plater::priv::update_fff_scene_only_shells(bool only_shells)
{
    if (this->preview != nullptr)
    {
        const Print* current_print = this->background_process.fff_print();
        if (current_print)
        {
            //this->preview->reset_shells();
            this->preview->load_shells(*current_print);
        }
    }

    if (!only_shells) {
        view3D->reload_scene(true);
        assemble_view->reload_scene(true);
    }
}

void Plater::priv::update_sla_scene()
{
    // Update the SLAPrint from the current Model, so that the reload_scene()
    // pulls the correct data.
    delayed_scene_refresh = false;
    this->update_restart_background_process(true, true);
}

bool Plater::priv::replace_volume_with_stl(int object_idx, int volume_idx, const fs::path& new_path, const std::string& snapshot)
{
    const std::string path = new_path.string();
    wxBusyCursor wait;
    wxBusyInfo info(_devL("Replace from:") + " " + from_u8(path), q->get_current_canvas3D()->get_wxglcanvas());

    Model new_model;
    try {
        new_model = Model::read_from_file(path, nullptr, nullptr, LoadStrategy::AddDefaultInstances);
        for (ModelObject* model_object : new_model.objects) {
            model_object->center_around_origin();
            model_object->ensure_on_bed();
        }
    }
    catch (std::exception&) {
        // error while loading
        return false;
    }

    if (new_model.objects.size() > 1 || new_model.objects.front()->volumes.size() > 1) {
        MessageDialog dlg(q, _devL("Unable to replace with more than one volume"), _devL("Error during replace"), wxOK | wxOK_DEFAULT | wxICON_WARNING);
        dlg.ShowModal();
        return false;
    }

    if (!snapshot.empty())
        q->take_snapshot(snapshot);

    ModelObject* old_model_object = model.objects[object_idx];
    ModelVolume* old_volume = old_model_object->volumes[volume_idx];

    bool sinking = old_model_object->bounding_box().min.z() < SINKING_Z_THRESHOLD;

    ModelObject* new_model_object = new_model.objects.front();
    old_model_object->add_volume(*new_model_object->volumes.front());
    ModelVolume* new_volume = old_model_object->volumes.back();
    new_volume->set_new_unique_id();
    new_volume->config.apply(old_volume->config);
    new_volume->set_type(old_volume->type());
    new_volume->set_material_id(old_volume->material_id());
    new_volume->set_transformation(old_volume->get_transformation());
    new_volume->translate(new_volume->get_transformation().get_matrix(true) * (new_volume->source.mesh_offset - old_volume->source.mesh_offset));
    assert(!old_volume->source.is_converted_from_inches || !old_volume->source.is_converted_from_meters);
    if (old_volume->source.is_converted_from_inches)
        new_volume->convert_from_imperial_units();
    else if (old_volume->source.is_converted_from_meters)
        new_volume->convert_from_meters();
    new_volume->supported_facets.assign(old_volume->supported_facets);
    new_volume->seam_facets.assign(old_volume->seam_facets);
    new_volume->mmu_segmentation_facets.assign(old_volume->mmu_segmentation_facets);
    std::swap(old_model_object->volumes[volume_idx], old_model_object->volumes.back());
    old_model_object->delete_volume(old_model_object->volumes.size() - 1);
    if (!sinking)
        old_model_object->ensure_on_bed();
    old_model_object->sort_volumes(true);

    // if object has just one volume, rename object too
    if (old_model_object->volumes.size() == 1)
        old_model_object->name = old_model_object->volumes.front()->name;

    // update new name in ObjectList
    sidebar->obj_list()->update_name_in_list(object_idx, volume_idx);

    sla::reproject_points_and_holes(old_model_object);

    return true;
}

void Plater::priv::replace_with_stl()
{
    if (! q->get_view3D_canvas3D()->get_gizmos_manager().check_gizmos_closed_except(GLGizmosManager::EType::Undefined))
        return;

    const Selection& selection = get_selection();

    if (selection.is_wipe_tower() || get_selection().get_volume_idxs().size() != 1)
        return;

    const GLVolume* v = selection.get_volume(*selection.get_volume_idxs().begin());
    int object_idx = v->object_idx();
    int volume_idx = v->volume_idx();

    // collects paths of files to load

    const ModelObject* object = model.objects[object_idx];
    const ModelVolume* volume = object->volumes[volume_idx];

    fs::path input_path;
    if (!volume->source.input_file.empty() && fs::exists(volume->source.input_file))
        input_path = volume->source.input_file;

    wxString title = _L("Select a new file");
    title += ":";
    wxFileDialog dialog(q, title, "", from_u8(input_path.filename().string()), file_wildcards(FT_MODEL), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK)
        return;

    fs::path out_path = dialog.GetPath().ToUTF8().data();
    if (out_path.empty()) {
        MessageDialog dlg(q, _L("File for the replace wasn't selected"), _L("Error during replace"), wxOK | wxOK_DEFAULT | wxICON_WARNING);
        dlg.ShowModal();
        return;
    }

    if (!replace_volume_with_stl(object_idx, volume_idx, out_path, "Replace with STL"))
        return;

    // update 3D scene
    update();

    // new GLVolumes have been created at this point, so update their printable state
    for (size_t i = 0; i < model.objects.size(); ++i) {
        view3D->get_canvas3d()->update_instance_printable_state_for_object(i);
    }
}

#if ENABLE_RELOAD_FROM_DISK_REWORK
static std::vector<std::pair<int, int>> reloadable_volumes(const Model &model, const Selection &selection)
{
    std::vector<std::pair<int, int>> ret;
    const std::set<unsigned int> &   selected_volumes_idxs = selection.get_volume_idxs();
    for (unsigned int idx : selected_volumes_idxs) {
        const GLVolume &v     = *selection.get_volume(idx);
        const int       o_idx = v.object_idx();
        if (0 <= o_idx && o_idx < int(model.objects.size())) {
            const ModelObject *obj   = model.objects[o_idx];
            const int          v_idx = v.volume_idx();
            if (0 <= v_idx && v_idx < int(obj->volumes.size())) {
                const ModelVolume *vol = obj->volumes[v_idx];
                if (!vol->source.is_from_builtin_objects && !vol->source.input_file.empty() && !fs::path(vol->source.input_file).extension().string().empty())
                    ret.push_back({o_idx, v_idx});
            }
        }
    }
    return ret;
}
#endif // ENABLE_RELOAD_FROM_DISK_REWORK

void Plater::priv::reload_from_disk()
{
#if ENABLE_RELOAD_FROM_DISK_REWORK
    // collect selected reloadable ModelVolumes
    std::vector<std::pair<int, int>> selected_volumes = reloadable_volumes(model, get_selection());

    // nothing to reload, return
    if (selected_volumes.empty())
        return;

    std::sort(selected_volumes.begin(), selected_volumes.end(), [](const std::pair<int, int> &v1, const std::pair<int, int> &v2) {
        return (v1.first < v2.first) || (v1.first == v2.first && v1.second < v2.second);
        });
    selected_volumes.erase(std::unique(selected_volumes.begin(), selected_volumes.end(), [](const std::pair<int, int> &v1, const std::pair<int, int> &v2) {
        return (v1.first == v2.first) && (v1.second == v2.second);
        }), selected_volumes.end());
#else
    Plater::TakeSnapshot snapshot(q, "Reload from disk");

    const Selection& selection = get_selection();

    if (selection.is_wipe_tower())
        return;

    // struct to hold selected ModelVolumes by their indices
    struct SelectedVolume
    {
        int object_idx;
        int volume_idx;

        // operators needed by std::algorithms
        bool operator < (const SelectedVolume& other) const { return object_idx < other.object_idx || (object_idx == other.object_idx && volume_idx < other.volume_idx); }
        bool operator == (const SelectedVolume& other) const { return object_idx == other.object_idx && volume_idx == other.volume_idx; }
    };
    std::vector<SelectedVolume> selected_volumes;

    // collects selected ModelVolumes
    const std::set<unsigned int>& selected_volumes_idxs = selection.get_volume_idxs();
    for (unsigned int idx : selected_volumes_idxs) {
        const GLVolume* v = selection.get_volume(idx);
        int v_idx = v->volume_idx();
        if (v_idx >= 0) {
            int o_idx = v->object_idx();
            if (0 <= o_idx && o_idx < (int)model.objects.size())
                selected_volumes.push_back({ o_idx, v_idx });
        }
    }
    std::sort(selected_volumes.begin(), selected_volumes.end());
    selected_volumes.erase(std::unique(selected_volumes.begin(), selected_volumes.end()), selected_volumes.end());
#endif // ENABLE_RELOAD_FROM_DISK_REWORK

    // collects paths of files to load
    std::vector<fs::path> input_paths;
    std::vector<fs::path> missing_input_paths;
#if ENABLE_RELOAD_FROM_DISK_REWORK
    std::vector<std::pair<fs::path, fs::path>> replace_paths;
    for (auto [obj_idx, vol_idx] : selected_volumes) {
        const ModelObject *object = model.objects[obj_idx];
        const ModelVolume *volume = object->volumes[vol_idx];
        if (fs::exists(volume->source.input_file))
            input_paths.push_back(volume->source.input_file);
        else {
            // searches the source in the same folder containing the object
            bool found = false;
            if (!object->input_file.empty()) {
                fs::path object_path = fs::path(object->input_file).remove_filename();
                if (!object_path.empty()) {
                    object_path /= fs::path(volume->source.input_file).filename();
                    if (fs::exists(object_path)) {
                        input_paths.push_back(object_path);
                        found = true;
                    }
                }
            }
            if (!found)
                missing_input_paths.push_back(volume->source.input_file);
        }
    }
#else
    std::vector<fs::path> replace_paths;
    for (const SelectedVolume& v : selected_volumes) {
        const ModelObject* object = model.objects[v.object_idx];
        const ModelVolume* volume = object->volumes[v.volume_idx];

        if (!volume->source.input_file.empty()) {
            if (fs::exists(volume->source.input_file))
                input_paths.push_back(volume->source.input_file);
            else {
                // searches the source in the same folder containing the object
                bool found = false;
                if (!object->input_file.empty()) {
                    fs::path object_path = fs::path(object->input_file).remove_filename();
                    if (!object_path.empty()) {
                        object_path /= fs::path(volume->source.input_file).filename();
                        const std::string source_input_file = object_path.string();
                        if (fs::exists(source_input_file)) {
                            input_paths.push_back(source_input_file);
                            found = true;
                        }
                    }
                }
                if (!found)
                    missing_input_paths.push_back(volume->source.input_file);
            }
        }
        else if (!object->input_file.empty() && volume->is_model_part() && !volume->name.empty() && !volume->source.is_from_builtin_objects)
            missing_input_paths.push_back(volume->name);
    }
#endif // ENABLE_RELOAD_FROM_DISK_REWORK

    std::sort(missing_input_paths.begin(), missing_input_paths.end());
    missing_input_paths.erase(std::unique(missing_input_paths.begin(), missing_input_paths.end()), missing_input_paths.end());

    while (!missing_input_paths.empty()) {
        // ask user to select the missing file
        fs::path search = missing_input_paths.back();
        wxString title = _L("Please select a file");
#if defined(__APPLE__)
        title += " (" + from_u8(search.filename().string()) + ")";
#endif // __APPLE__
        title += ":";
        wxFileDialog dialog(q, title, "", from_u8(search.filename().string()), file_wildcards(FT_MODEL), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dialog.ShowModal() != wxID_OK)
            return;

        std::string sel_filename_path = dialog.GetPath().ToUTF8().data();
        std::string sel_filename = fs::path(sel_filename_path).filename().string();
        if (boost::algorithm::iequals(search.filename().string(), sel_filename)) {
            input_paths.push_back(sel_filename_path);
            missing_input_paths.pop_back();

            fs::path sel_path = fs::path(sel_filename_path).remove_filename().string();

            std::vector<fs::path>::iterator it = missing_input_paths.begin();
            while (it != missing_input_paths.end()) {
                // try to use the path of the selected file with all remaining missing files
                fs::path repathed_filename = sel_path;
                repathed_filename /= it->filename();
                if (fs::exists(repathed_filename)) {
                    input_paths.push_back(repathed_filename.string());
                    it = missing_input_paths.erase(it);
                }
                else
                    ++it;
            }
        }
        else {
            wxString      message = _devL("Do you want to replace it") + " ?";
            MessageDialog dlg(q, message, wxMessageBoxCaptionStr, wxYES_NO | wxYES_DEFAULT | wxICON_QUESTION);
            if (dlg.ShowModal() == wxID_YES)
#if ENABLE_RELOAD_FROM_DISK_REWORK
                replace_paths.emplace_back(search, sel_filename_path);
#else
                replace_paths.emplace_back(sel_filename_path);
#endif // ENABLE_RELOAD_FROM_DISK_REWORK
            missing_input_paths.pop_back();
        }
    }

    std::sort(input_paths.begin(), input_paths.end());
    input_paths.erase(std::unique(input_paths.begin(), input_paths.end()), input_paths.end());

    std::sort(replace_paths.begin(), replace_paths.end());
    replace_paths.erase(std::unique(replace_paths.begin(), replace_paths.end()), replace_paths.end());

#if ENABLE_RELOAD_FROM_DISK_REWORK
    Plater::TakeSnapshot snapshot(q, "Reload from disk");
#endif // ENABLE_RELOAD_FROM_DISK_REWORK

    std::vector<wxString> fail_list;

    // load one file at a time
    for (size_t i = 0; i < input_paths.size(); ++i) {
        const auto& path = input_paths[i].string();

        wxBusyCursor wait;
        wxBusyInfo info(_devL("Reload from:") + " " + from_u8(path), q->get_current_canvas3D()->get_wxglcanvas());

        Model new_model;
        try
        {
            //BBS: add plate data related logic
            PlateDataPtrs plate_data;
            //BBS: project embedded settings
            std::vector<Preset*> project_presets;

            // BBS: backup
            new_model = Model::read_from_file(path, nullptr, nullptr, LoadStrategy::AddDefaultInstances, &plate_data, &project_presets);
            for (ModelObject* model_object : new_model.objects)
            {
                model_object->center_around_origin();
                model_object->ensure_on_bed();
            }

            if (plate_data.size() > 0)
            {
                partplate_list.load_from_3mf_structure(plate_data);
                partplate_list.update_slice_context_to_current_plate(background_process);
                this->preview->update_gcode_result(partplate_list.get_current_slice_result());
                release_PlateData_list(plate_data);
                sidebar->obj_list()->reload_all_plates();
            }
        }
        catch (std::exception&)
        {
            // error while loading
            return;
        }

#if ENABLE_RELOAD_FROM_DISK_REWORK
        for (auto [obj_idx, vol_idx] : selected_volumes) {
            ModelObject *old_model_object = model.objects[obj_idx];
            ModelVolume *old_volume       = old_model_object->volumes[vol_idx];

            bool sinking = old_model_object->bounding_box().min.z() < SINKING_Z_THRESHOLD;

            bool has_source = !old_volume->source.input_file.empty() &&
                              boost::algorithm::iequals(fs::path(old_volume->source.input_file).filename().string(), fs::path(path).filename().string());
            bool has_name = !old_volume->name.empty() && boost::algorithm::iequals(old_volume->name, fs::path(path).filename().string());
            if (has_source || has_name) {
                int  new_volume_idx = -1;
                int  new_object_idx = -1;
                bool match_found    = false;
                // take idxs from the matching volume
                if (has_source && old_volume->source.object_idx < int(new_model.objects.size())) {
                    const ModelObject *obj = new_model.objects[old_volume->source.object_idx];
                    if (old_volume->source.volume_idx < int(obj->volumes.size())) {
                        if (obj->volumes[old_volume->source.volume_idx]->name == old_volume->name) {
                            new_volume_idx = old_volume->source.volume_idx;
                            new_object_idx = old_volume->source.object_idx;
                            match_found    = true;
                        }
                    }
                }

                if (!match_found && has_name) {
                    // take idxs from the 1st matching volume
                    for (size_t o = 0; o < new_model.objects.size(); ++o) {
                        ModelObject *obj   = new_model.objects[o];
                        bool         found = false;
                        for (size_t v = 0; v < obj->volumes.size(); ++v) {
                            if (obj->volumes[v]->name == old_volume->name) {
                                new_volume_idx = (int) v;
                                new_object_idx = (int) o;
                                found          = true;
                                break;
                            }
                        }
                        if (found) break;
                    }
                }

                if (new_object_idx < 0 || int(new_model.objects.size()) <= new_object_idx) {
                    fail_list.push_back(from_u8(has_source ? old_volume->source.input_file : old_volume->name));
                    continue;
                }
                ModelObject *new_model_object = new_model.objects[new_object_idx];
                if (new_volume_idx < 0 || int(new_model_object->volumes.size()) <= new_volume_idx) {
                    fail_list.push_back(from_u8(has_source ? old_volume->source.input_file : old_volume->name));
                    continue;
                }

                old_model_object->add_volume(*new_model_object->volumes[new_volume_idx]);
                ModelVolume *new_volume = old_model_object->volumes.back();
                new_volume->set_new_unique_id();
                new_volume->config.apply(old_volume->config);
                new_volume->set_type(old_volume->type());
                new_volume->set_material_id(old_volume->material_id());

                Transform3d transform = Transform3d::Identity();
                transform.translate(new_volume->source.mesh_offset - old_volume->source.mesh_offset);
                new_volume->set_transformation(old_volume->get_transformation().get_matrix() * old_volume->source.transform.get_matrix(true) *
                                               transform * new_volume->source.transform.get_matrix(true).inverse());

                new_volume->source.object_idx = old_volume->source.object_idx;
                new_volume->source.volume_idx = old_volume->source.volume_idx;
                assert(!old_volume->source.is_converted_from_inches || !old_volume->source.is_converted_from_meters);
                if (old_volume->source.is_converted_from_inches)
                    new_volume->convert_from_imperial_units();
                else if (old_volume->source.is_converted_from_meters)
                    new_volume->convert_from_meters();
                std::swap(old_model_object->volumes[vol_idx], old_model_object->volumes.back());
                old_model_object->delete_volume(old_model_object->volumes.size() - 1);
                if (!sinking) old_model_object->ensure_on_bed();
                old_model_object->sort_volumes(wxGetApp().app_config->get("order_volumes") == "1");

                sla::reproject_points_and_holes(old_model_object);

                // Fix warning icon in object list
                wxGetApp().obj_list()->update_item_error_icon(obj_idx, vol_idx);
            }
        }
#else
        // update the selected volumes whose source is the current file
        for (const SelectedVolume& sel_v : selected_volumes) {
            ModelObject* old_model_object = model.objects[sel_v.object_idx];
            ModelVolume* old_volume = old_model_object->volumes[sel_v.volume_idx];

            bool sinking = old_model_object->bounding_box().min.z() < SINKING_Z_THRESHOLD;

            bool has_source = !old_volume->source.input_file.empty() && boost::algorithm::iequals(fs::path(old_volume->source.input_file).filename().string(), fs::path(path).filename().string());
            bool has_name = !old_volume->name.empty() && boost::algorithm::iequals(old_volume->name, fs::path(path).filename().string());
            if (has_source || has_name) {
                int new_volume_idx = -1;
                int new_object_idx = -1;
//                if (has_source) {
//                    // take idxs from source
//                    new_volume_idx = old_volume->source.volume_idx;
//                    new_object_idx = old_volume->source.object_idx;
//                }
//                else {
                    // take idxs from the 1st matching volume
                    for (size_t o = 0; o < new_model.objects.size(); ++o) {
                        ModelObject* obj = new_model.objects[o];
                        bool found = false;
                        for (size_t v = 0; v < obj->volumes.size(); ++v) {
                            if (obj->volumes[v]->name == old_volume->name) {
                                new_volume_idx = (int)v;
                                new_object_idx = (int)o;
                                found = true;
                                break;
                            }
                        }
                        if (found)
                            break;
                    }
//                }

                if (new_object_idx < 0 || int(new_model.objects.size()) <= new_object_idx) {
                    fail_list.push_back(from_u8(has_source ? old_volume->source.input_file : old_volume->name));
                    continue;
                }
                ModelObject* new_model_object = new_model.objects[new_object_idx];
                if (new_volume_idx < 0 || int(new_model_object->volumes.size()) <= new_volume_idx) {
                    fail_list.push_back(from_u8(has_source ? old_volume->source.input_file : old_volume->name));
                    continue;
                }

                old_model_object->add_volume(*new_model_object->volumes[new_volume_idx]);
                ModelVolume* new_volume = old_model_object->volumes.back();
                new_volume->set_new_unique_id();
                new_volume->config.apply(old_volume->config);
                new_volume->set_type(old_volume->type());
                new_volume->set_material_id(old_volume->material_id());
                new_volume->set_transformation(old_volume->get_transformation());
                new_volume->translate(new_volume->get_transformation().get_matrix(true) * (new_volume->source.mesh_offset - old_volume->source.mesh_offset));
                new_volume->source.object_idx = old_volume->source.object_idx;
                new_volume->source.volume_idx = old_volume->source.volume_idx;
                assert(! old_volume->source.is_converted_from_inches || ! old_volume->source.is_converted_from_meters);
                if (old_volume->source.is_converted_from_inches)
                    new_volume->convert_from_imperial_units();
                else if (old_volume->source.is_converted_from_meters)
                    new_volume->convert_from_meters();
                std::swap(old_model_object->volumes[sel_v.volume_idx], old_model_object->volumes.back());
                old_model_object->delete_volume(old_model_object->volumes.size() - 1);
                if (!sinking)
                    old_model_object->ensure_on_bed();
                old_model_object->sort_volumes(true);

                sla::reproject_points_and_holes(old_model_object);
            }
        }
#endif // ENABLE_RELOAD_FROM_DISK_REWORK
    }

#if ENABLE_RELOAD_FROM_DISK_REWORK
    for (auto [src, dest] : replace_paths) {
        for (auto [obj_idx, vol_idx] : selected_volumes) {
            if (boost::algorithm::iequals(model.objects[obj_idx]->volumes[vol_idx]->source.input_file, src.string()))
                replace_volume_with_stl(obj_idx, vol_idx, dest, "");
        }
    }
#else
    for (size_t i = 0; i < replace_paths.size(); ++i) {
        const auto& path = replace_paths[i].string();
        for (const SelectedVolume& sel_v : selected_volumes) {
            ModelObject* old_model_object = model.objects[sel_v.object_idx];
            ModelVolume* old_volume = old_model_object->volumes[sel_v.volume_idx];
            bool has_source = !old_volume->source.input_file.empty() && boost::algorithm::iequals(fs::path(old_volume->source.input_file).filename().string(), fs::path(path).filename().string());
            if (!replace_volume_with_stl(sel_v.object_idx, sel_v.volume_idx, path, "")) {
                fail_list.push_back(from_u8(has_source ? old_volume->source.input_file : old_volume->name));
            }
        }
    }
#endif // ENABLE_RELOAD_FROM_DISK_REWORK

    if (!fail_list.empty()) {
        wxString message = _devL("Unable to reload:") + "\n";
        for (const wxString& s : fail_list) {
            message += s + "\n";
        }
        MessageDialog dlg(q, message, _devL("Error during reload"), wxOK | wxOK_DEFAULT | wxICON_WARNING);
        dlg.ShowModal();
    }

    // update 3D scene
    update();

    // new GLVolumes have been created at this point, so update their printable state
    for (size_t i = 0; i < model.objects.size(); ++i) {
        view3D->get_canvas3d()->update_instance_printable_state_for_object(i);
    }
}

void Plater::priv::reload_all_from_disk()
{
    if (model.objects.empty())
        return;

    Plater::TakeSnapshot snapshot(q, "Reload all");
    Plater::SuppressSnapshots suppress(q);

    Selection& selection = get_selection();
    Selection::IndicesList curr_idxs = selection.get_volume_idxs();
    // reload from disk uses selection
    select_all();
    reload_from_disk();
    // restore previous selection
    selection.clear();
    for (unsigned int idx : curr_idxs) {
        selection.add(idx, false);
    }
}

//BBS: add no_slice logic
void Plater::priv::set_current_panel(wxPanel* panel, bool no_slice)
{
    if (std::find(panels.begin(), panels.end(), panel) == panels.end())
        return;

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": current_panel %1%, new_panel %2%")%current_panel%panel;
#ifdef __WXMAC__
    bool force_render = (current_panel != nullptr);
#endif // __WXMAC__

    //BBS: add slice logic when switch to preview page
    auto do_reslice = [this, no_slice]() {
            // see: Plater::priv::object_list_changed()
            // FIXME: it may be better to have a single function making this check and let it be called wherever needed
            bool export_in_progress = this->background_process.is_export_scheduled();
            bool model_fits = this->view3D->get_canvas3d()->check_volumes_outside_state() != ModelInstancePVS_Partly_Outside;
            //BBS: add partplate logic
            PartPlate * current_plate = this->partplate_list.get_curr_plate();
            bool only_has_gcode_need_preview = false;
            bool current_has_print_instances = current_plate->has_printable_instances();
            if (current_plate->is_slice_result_valid() && this->model.objects.empty() && !current_has_print_instances)
                only_has_gcode_need_preview = true;

            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": from set_current_panel, no_slice %1%, export_in_progress %2%, model_fits %3%, m_is_slicing %4%")%no_slice%export_in_progress%model_fits%m_is_slicing;

            if (!no_slice && !this->model.objects.empty() && !export_in_progress && model_fits && current_has_print_instances)
            {
                //if already running in background, not relice here
                //BBS: add more judge for slicing
                if (!this->background_process.running() && !this->m_is_slicing)
                {
                    this->m_slice_all = false;
                    this->q->reslice();
                }
                else {
                    //reset current plate to the slicing plate
                    int plate_index = this->background_process.get_current_plate()->get_index();
                    this->partplate_list.select_plate(plate_index);
                }
            }
            else if (only_has_gcode_need_preview)
            {
                this->m_slice_all = false;
                this->q->reslice();
            }
            //BBS: process empty plate, reset previous toolpath
            else
            {
                //if (!this->m_slice_all)
                if (!current_has_print_instances)
                    reset_gcode_toolpaths();
                //this->q->refresh_print();
                if (!preview->get_canvas3d()->is_initialized())
                {
                    preview->get_canvas3d()->render(true);
                }
            }
            //TODO: turn off this switch currently
            /*auto canvas_w = float(preview->get_canvas3d()->get_canvas_size().get_width());
            auto canvas_h = float(preview->get_canvas3d()->get_canvas_size().get_height());
            Point screen_center(canvas_w/2, canvas_h/2);
            auto center_point = preview->get_canvas3d()->_mouse_to_3d(screen_center);
            center_point(2) = 0.f;
            if (!current_plate->contains(center_point))
                this->partplate_list.select_plate_view();*/

            // keeps current gcode preview, if any
            if (this->m_slice_all) {
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": slicing all, just reload shells");
                this->update_fff_scene_only_shells();
            }
            else {
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": single slice, reload print");
                if (model_fits)
                    this->preview->reload_print(true);
                else
                    this->update_fff_scene_only_shells();
            }

            preview->set_as_dirty();
        };

    if (current_panel == panel)
    {
        //BBS: add slice logic when switch to preview page
        //BBS: add only gcode mode
        if (!q->only_gcode_mode() && (current_panel == preview) && (wxGetApp().is_editor())) {
            do_reslice();
        }
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": the same panel, exit");
        return;
    }

    //BBS: wish to reset all plates stats item selected state when back to View3D Tab
    preview->get_canvas3d()->reset_select_plate_toolbar_selection();

    wxPanel* old_panel = current_panel;
//#if BBL_HAS_FIRST_PAGE
    if (!old_panel) {
        //BBS: only switch to the first panel when visible
        panel->Show();
        //dynamic_cast<View3D *>(panel)->get_canvas3d()->render();
        if (!panel->IsShownOnScreen())
            return;
    }
//#endif
    current_panel = panel;
    //BBS: add the collapse logic
    if (current_panel == preview && q->only_gcode_mode()) {
        this->sidebar->collapse(true);
        preview->get_canvas3d()->enable_select_plate_toolbar(false);
    }
    else if (current_panel == preview && q->using_exported_file() && (q->m_valid_plates_count <= 1)) {
        preview->get_canvas3d()->enable_select_plate_toolbar(false);
    }
    else {
        preview->get_canvas3d()->enable_select_plate_toolbar(true);
    }

    // to reduce flickering when changing view, first set as visible the new current panel
    for (wxPanel* p : panels) {
        if (p == current_panel) {
#ifdef __WXMAC__
            // On Mac we need also to force a render to avoid flickering when changing view
            if (force_render) {
                if (p == view3D)
                    dynamic_cast<View3D*>(p)->get_canvas3d()->render();
                else if (p == preview)
                    dynamic_cast<Preview*>(p)->get_canvas3d()->render();
            }
#endif // __WXMAC__
            p->Show();
        }
    }
    // then set to invisible the other
    for (wxPanel* p : panels) {
        if (p != current_panel)
            p->Hide();
    }

    panel_sizer->Layout();

    if (wxGetApp().plater()) {
        Camera& cam = wxGetApp().plater()->get_camera();
        if (old_panel == preview || old_panel == view3D) {
            view3D->get_canvas3d()->get_camera().load_camera_view(cam);
        } else if (old_panel == assemble_view) {
            assemble_view->get_canvas3d()->get_camera().load_camera_view(cam);
        }
        if (current_panel == view3D || current_panel == preview) {
            cam.load_camera_view(view3D->get_canvas3d()->get_camera());
        }
        else if (current_panel == assemble_view) {
            cam.load_camera_view(assemble_view->get_canvas3d()->get_camera());
        }
    }

    if (current_panel == view3D) {
        if (old_panel == preview)
            preview->get_canvas3d()->unbind_event_handlers();
        else if (old_panel == assemble_view)
            assemble_view->get_canvas3d()->unbind_event_handlers();

        view3D->get_canvas3d()->bind_event_handlers();

        if (view3D->is_reload_delayed()) {
            // Delayed loading of the 3D scene.
            if (printer_technology == ptSLA) {
                // Update the SLAPrint from the current Model, so that the reload_scene()
                // pulls the correct data.
                update_restart_background_process(true, false);
            } else
                view3D->reload_scene(true);
        }

        // sets the canvas as dirty to force a render at the 1st idle event (wxWidgets IsShownOnScreen() is buggy and cannot be used reliably)
        view3D->set_as_dirty();
        // reset cached size to force a resize on next call to render() to keep imgui in synch with canvas size
        view3D->get_canvas3d()->reset_old_size();
        // BBS
        //view_toolbar.select_item("3D");
        if (notification_manager != nullptr)
            notification_manager->set_in_preview(false);
    }
    else if (current_panel == preview) {
        q->invalid_all_plate_thumbnails();
        if (old_panel == view3D)
            view3D->get_canvas3d()->unbind_event_handlers();
        else if (old_panel == assemble_view)
            assemble_view->get_canvas3d()->unbind_event_handlers();

        preview->get_canvas3d()->bind_event_handlers();

        GLGizmosManager& gizmos = view3D->get_canvas3d()->get_gizmos_manager();
        if (gizmos.is_running()) {
            gizmos.reset_all_states();
            gizmos.update_data();
        }

        if (wxGetApp().is_editor()) {
            // see: Plater::priv::object_list_changed()
            // FIXME: it may be better to have a single function making this check and let it be called wherever needed
            /*bool export_in_progress = this->background_process.is_export_scheduled();
            bool model_fits = view3D->get_canvas3d()->check_volumes_outside_state() != ModelInstancePVS_Partly_Outside;
            //BBS: add partplate logic
            PartPlate* current_plate = partplate_list.get_curr_plate();
            if (!no_slice && !model.objects.empty() && !export_in_progress && model_fits && current_plate->has_printable_instances()) {
                preview->get_canvas3d()->init_gcode_viewer();
                // BBS
                //if already running in background, not relice here
                if (!this->background_process.running())
                {
                    m_slice_all = false;
                    this->q->reslice();
                }
            }
            // keeps current gcode preview, if any
            preview->reload_print(true);

            preview->set_as_dirty();*/
            if (wxGetApp().is_editor() && !q->only_gcode_mode())
                do_reslice();
        }

        // reset cached size to force a resize on next call to render() to keep imgui in synch with canvas size
        preview->get_canvas3d()->reset_old_size();
        // BBS
        //view_toolbar.select_item("Preview");
        if (notification_manager != nullptr)
            notification_manager->set_in_preview(true);
    }
    else if (current_panel == assemble_view) {
        if (old_panel == view3D) {
            view3D->get_canvas3d()->unbind_event_handlers();
        }
        else if (old_panel == preview)
            preview->get_canvas3d()->unbind_event_handlers();

        assemble_view->get_canvas3d()->bind_event_handlers();
        assemble_view->reload_scene(true);

        // BBS set default view and zoom
        if (first_enter_assemble) {
            wxGetApp().plater()->get_camera().requires_zoom_to_volumes = true;
            first_enter_assemble = false;
        }

        assemble_view->set_as_dirty();
        // BBS
        //view_toolbar.select_item("Assemble");
    }

    current_panel->SetFocusFromKbd();

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": successfully, exit");
}

// BBS
void Plater::priv::on_combobox_select(wxCommandEvent &evt)
{
    PlaterPresetComboBox* preset_combo_box = dynamic_cast<PlaterPresetComboBox*>(evt.GetEventObject());
    if (preset_combo_box) {
        this->on_select_preset(evt);
    }
    else {
        this->on_select_bed_type(evt);
    }
}

void Plater::priv::on_select_bed_type(wxCommandEvent &evt)
{
    ComboBox* combo = static_cast<ComboBox*>(evt.GetEventObject());
    int selection = combo->GetSelection();
    wxString bed_type_name = combo->GetString(selection);

    DynamicPrintConfig& proj_config = wxGetApp().preset_bundle->project_config;
    const t_config_enum_values* keys_map = print_config_def.get("curr_bed_type")->enum_keys_map;

    if (keys_map) {
        BedType new_bed_type = btCount;
        for (auto item : *keys_map) {
            if (_L(item.first) == bed_type_name) {
                new_bed_type = (BedType)item.second;
                break;
            }
        }

        if (new_bed_type != btCount) {
            BedType old_bed_type = proj_config.opt_enum<BedType>("curr_bed_type");
            if (old_bed_type != new_bed_type) {
                proj_config.set_key_value("curr_bed_type", new ConfigOptionEnum<BedType>(new_bed_type));

                wxGetApp().plater()->update_project_dirty_from_presets();

                // update plater with new config
                q->on_config_change(wxGetApp().preset_bundle->full_config());

                // update app_config
                AppConfig* app_config = wxGetApp().app_config;
                app_config->set("curr_bed_type", std::to_string(int(new_bed_type)));

                //update slice status
                auto plate_list = partplate_list.get_plate_list();
                for (auto plate : plate_list) {
                    if (plate->get_bed_type() == btDefault) {
                        plate->update_slice_result_valid_state(false);
                    }
                }

                // update render
                view3D->get_canvas3d()->render();
                preview->msw_rescale();
            }
        }
    }
}

void Plater::priv::on_select_preset(wxCommandEvent &evt)
{
    PlaterPresetComboBox* combo = static_cast<PlaterPresetComboBox*>(evt.GetEventObject());
    Preset::Type preset_type    = combo->get_type();

    // Under OSX: in case of use of a same names written in different case (like "ENDER" and "Ender"),
    // m_presets_choice->GetSelection() will return first item, because search in PopupListCtrl is case-insensitive.
    // So, use GetSelection() from event parameter
    int selection = evt.GetSelection();

    auto idx = combo->get_filament_idx();

    //! Because of The MSW and GTK version of wxBitmapComboBox derived from wxComboBox,
    //! but the OSX version derived from wxOwnerDrawnCombo.
    //! So, to get selected string we do
    //!     combo->GetString(combo->GetSelection())
    //! instead of
    //!     combo->GetStringSelection().ToUTF8().data());

    std::string preset_name = wxGetApp().preset_bundle->get_preset_name_by_alias(preset_type,
        Preset::remove_suffix_modified(combo->GetString(selection).ToUTF8().data()));

    if (preset_type == Preset::TYPE_FILAMENT) {
        wxGetApp().preset_bundle->set_filament_preset(idx, preset_name);
        wxGetApp().plater()->update_project_dirty_from_presets();
        wxGetApp().preset_bundle->export_selections(*wxGetApp().app_config);
        dynamic_filament_list.update();
    }

    bool select_preset = !combo->selection_is_changed_according_to_physical_printers();
    // TODO: ?
    if (preset_type == Preset::TYPE_FILAMENT && sidebar->is_multifilament()) {
        // Only update the plater UI for the 2nd and other filaments.
        combo->update();
    }
    else if (select_preset) {
        if (preset_type == Preset::TYPE_PRINTER) {
            PhysicalPrinterCollection& physical_printers = wxGetApp().preset_bundle->physical_printers;
            if(combo->is_selected_physical_printer())
                preset_name = physical_printers.get_selected_printer_preset_name();
            else
                physical_printers.unselect_printer();
        }
        //BBS
        //wxWindowUpdateLocker noUpdates1(sidebar->print_panel());
        wxWindowUpdateLocker noUpdates2(sidebar->filament_panel());
        wxGetApp().get_tab(preset_type)->select_preset(preset_name);
    }

    // update plater with new config
    q->on_config_change(wxGetApp().preset_bundle->full_config());
    if (preset_type == Preset::TYPE_PRINTER) {
    /* Settings list can be changed after printer preset changing, so
     * update all settings items for all item had it.
     * Furthermore, Layers editing is implemented only for FFF printers
     * and for SLA presets they should be deleted
     */
        wxGetApp().obj_list()->update_object_list_by_printer_technology();
    }

#ifdef __WXMSW__
    // From the Win 2004 preset combobox lose a focus after change the preset selection
    // and that is why the up/down arrow doesn't work properly
    // So, set the focus to the combobox explicitly
    combo->SetFocus();
#endif
    // BBS: log modify of filament selection
    Slic3r::put_other_changes();

    // update slice state and set bedtype default for 3rd-party printer
    auto plate_list = partplate_list.get_plate_list();
    for (auto plate : plate_list) {
         plate->update_slice_result_valid_state(false);
    }
}

void Plater::priv::on_slicing_update(SlicingStatusEvent &evt)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": event_type %1%, percent %2%, text %3%") % evt.GetEventType() % evt.status.percent % evt.status.text;
    //BBS: add slice project logic
    std::string title_text = _u8L("Slicing");
    evt.status.text = title_text + evt.status.text;
    if (evt.status.percent >= 0) {
        if (m_ui_jobs.is_any_running()) {
            // Avoid a race condition
            return;
        }

        notification_manager->set_slicing_progress_percentage(evt.status.text, (float)evt.status.percent / 100.0f);

        // update slicing percent
        PartPlateList& plate_list = wxGetApp().plater()->get_partplate_list();
        //slicing parallel, only update if percent is greater than before
        if (evt.status.percent > plate_list.get_curr_plate()->get_slicing_percent())
            plate_list.get_curr_plate()->update_slicing_percent(evt.status.percent);
    }

    if (evt.status.flags & (PrintBase::SlicingStatus::RELOAD_SCENE | PrintBase::SlicingStatus::RELOAD_SLA_SUPPORT_POINTS)) {
        switch (this->printer_technology) {
        case ptFFF:
            //BBS: add slice project logic, only display shells at the beginning
            if (!m_slice_all || (m_cur_slice_plate == (partplate_list.get_plate_count() - 1)))
                //this->update_fff_scene();
                this->update_fff_scene_only_shells();
            break;
        case ptSLA:
            // If RELOAD_SLA_SUPPORT_POINTS, then the SLA gizmo is updated (reload_scene calls update_gizmos_data)
            if (view3D->is_dragging())
                delayed_scene_refresh = true;
            else
                this->update_sla_scene();
            break;
        default: break;
        }
    } else if (evt.status.flags & PrintBase::SlicingStatus::RELOAD_SLA_PREVIEW) {
        // Update the SLA preview. Only called if not RELOAD_SLA_SUPPORT_POINTS, as the block above will refresh the preview anyways.
        this->preview->reload_print();
    }

    if (evt.status.flags & (PrintBase::SlicingStatus::UPDATE_PRINT_STEP_WARNINGS | PrintBase::SlicingStatus::UPDATE_PRINT_OBJECT_STEP_WARNINGS)) {
        // Update notification center with warnings of object_id and its warning_step.
        ObjectID object_id = evt.status.warning_object_id;
        int warning_step = evt.status.warning_step;
        PrintStateBase::StateWithWarnings state;
        ModelObject const * model_object = nullptr;

        //BBS: add partplate related logic, use the print in background process
        if (evt.status.flags & PrintBase::SlicingStatus::UPDATE_PRINT_STEP_WARNINGS) {
            state = this->printer_technology == ptFFF ?
                this->background_process.m_fff_print->step_state_with_warnings(static_cast<PrintStep>(warning_step)) :
                this->background_process.m_sla_print->step_state_with_warnings(static_cast<SLAPrintStep>(warning_step));
        } else if (this->printer_technology == ptFFF) {
            const PrintObject *print_object = this->background_process.m_fff_print->get_object(object_id);
            if (print_object) {
                state = print_object->step_state_with_warnings(static_cast<PrintObjectStep>(warning_step));
                model_object = print_object->model_object();
            }
        } else {
            const SLAPrintObject *print_object = this->background_process.m_sla_print->get_object(object_id);
            if (print_object) {
                state = print_object->step_state_with_warnings(static_cast<SLAPrintObjectStep>(warning_step));
                model_object = print_object->model_object();
            }
        }
        // Now process state.warnings.
        for (auto const& warning : state.warnings) {
            if (warning.current) {
                notification_manager->push_slicing_warning_notification(warning.message, false, model_object, object_id, warning_step, warning.message_id);
                add_warning(warning, object_id.id);
            }
        }
    }
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format("exit.");
}

void Plater::priv::on_slicing_completed(wxCommandEvent & evt)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": event_type %1%, string %2%") % evt.GetEventType() % evt.GetString();
    //BBS: add slice project logic
    if (m_slice_all && (m_cur_slice_plate < (partplate_list.get_plate_count() - 1))) {
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format("slicing all, finished plate %1%, will continue next.")%m_cur_slice_plate;
        return;
    }

    if (view3D->is_dragging()) // updating scene now would interfere with the gizmo dragging
        delayed_scene_refresh = true;
    else {
        if (this->printer_technology == ptFFF) {
            //BBS: only reload shells
            this->update_fff_scene_only_shells(false);
            //this->update_fff_scene();
        }
        else
            this->update_sla_scene();
    }
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format("exit.");
}

void Plater::priv::on_export_began(wxCommandEvent& evt)
{
    if (show_warning_dialog)
        warnings_dialog();
}

void Plater::priv::on_export_finished(wxCommandEvent& evt)
{
#if 0
    //BBS: also export 3mf to the same directory for debugging
    std::string gcode_path_str(evt.GetString().ToUTF8().data());
    fs::path gcode_path(gcode_path_str);

    if (q) {
        q->export_3mf(gcode_path.replace_extension(".3mf"), SaveStrategy::Silence); // BBS: silence
    }
#endif
}

void Plater::priv::on_slicing_began()
{
    clear_warnings();
    notification_manager->close_notification_of_type(NotificationType::SignDetected);
    notification_manager->close_notification_of_type(NotificationType::ExportFinished);
    notification_manager->set_slicing_progress_began();
}
void Plater::priv::add_warning(const Slic3r::PrintStateBase::Warning& warning, size_t oid)
{
    for (auto& it : current_warnings) {
        if (warning.message_id == it.first.message_id) {
            if (warning.message_id != 0 || (warning.message_id == 0 && warning.message == it.first.message))
            {
                if (warning.message_id != 0)
                    it.first.message = warning.message;
                return;
            }
        }
    }
    current_warnings.emplace_back(std::pair<Slic3r::PrintStateBase::Warning, size_t>(warning, oid));
}
void Plater::priv::actualize_slicing_warnings(const PrintBase &print)
{
    std::vector<ObjectID> ids = print.print_object_ids();
    if (ids.empty()) {
        clear_warnings();
        return;
    }
    ids.emplace_back(print.id());
    std::sort(ids.begin(), ids.end());
    notification_manager->remove_slicing_warnings_of_released_objects(ids);
    notification_manager->set_all_slicing_warnings_gray(true);
}
void Plater::priv::actualize_object_warnings(const PrintBase& print)
{
    std::vector<ObjectID> ids;
    for (const ModelObject* object : print.model().objects )
    {
        ids.push_back(object->id());
    }
    std::sort(ids.begin(), ids.end());
    notification_manager->remove_simplify_suggestion_of_released_objects(ids);
}
void Plater::priv::clear_warnings()
{
    notification_manager->close_slicing_errors_and_warnings();
    this->current_warnings.clear();
}
bool Plater::priv::warnings_dialog()
{
    if (current_warnings.empty())
        return true;
    std::string text = _u8L("There are warnings after slicing models:") + "\n";
    for (auto const& it : current_warnings) {
        size_t next_n = it.first.message.find_first_of('\n', 0);
        text += "\n";
        if (next_n != std::string::npos)
            text += it.first.message.substr(0, next_n);
        else
            text += it.first.message;
    }
    //text += "\n\nDo you still wish to export?";
    MessageDialog msg_window(this->q, from_u8(text), _L("warnings"), wxOK);
    const auto    res = msg_window.ShowModal();
    return res == wxID_OK;

}

//BBS: add project slice logic
void Plater::priv::on_process_completed(SlicingProcessCompletedEvent &evt)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": enter, m_ignore_event %1%, status %2%")%m_ignore_event %evt.status();
    //BBS:ignore cancel event for some special case
    if (m_ignore_event)
    {
        m_ignore_event = false;
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": ignore this event %1%") % evt.status();
        return;
    }
    //BBS: add project slice logic
    bool is_finished = !m_slice_all || (m_cur_slice_plate == (partplate_list.get_plate_count() - 1));

    //BBS: slice .gcode.3mf file related logic, assign is_finished again
    bool only_has_gcode_need_preview = false;
    auto plate_list = this->partplate_list.get_plate_list();
    bool has_print_instances = false;
    for (auto plate : plate_list)
        has_print_instances = has_print_instances || plate->has_printable_instances();
    if (this->model.objects.empty() && !has_print_instances)
        only_has_gcode_need_preview = true;
    if (only_has_gcode_need_preview && m_slice_all_only_has_gcode) {
        is_finished = (m_cur_slice_plate == (partplate_list.get_plate_count() - 1));
        if (is_finished)
            m_slice_all_only_has_gcode = false;
    }

    // Stop the background task, wait until the thread goes into the "Idle" state.
    // At this point of time the thread should be either finished or canceled,
    // so the following call just confirms, that the produced data were consumed.
    this->background_process.stop();
    notification_manager->set_slicing_progress_export_possible();

    // Reset the "export G-code path" name, so that the automatic background processing will be enabled again.
    this->background_process.reset_export();
    // This bool stops showing export finished notification even when process_completed_with_error is false
    bool has_error = false;
    if (evt.error()) {
        std::pair<std::string, size_t> message = evt.format_error_message();
        if (evt.critical_error()) {
            if (q->m_tracking_popup_menu) {
                // We don't want to pop-up a message box when tracking a pop-up menu.
                // We postpone the error message instead.
                q->m_tracking_popup_menu_error_message = message.first;
            } else {
                show_error(q, message.first, message.second != 0);
                notification_manager->set_slicing_progress_hidden();
            }
        } else {
            ModelObject const *model_object = nullptr;
            const PrintObject *print_object = this->background_process.m_fff_print->get_object(ObjectID(message.second));
            if (print_object) model_object = print_object->model_object();
            notification_manager->push_slicing_error_notification(message.first, model_object);
        }
        if (evt.invalidate_plater())
        {
            // BBS
#if 0
            const wxString invalid_str = _L("Invalid data");
            for (auto btn : { ActionButtonType::abReslice, ActionButtonType::abSendGCode, ActionButtonType::abExport })
                sidebar->set_btn_label(btn, invalid_str);
#endif
            process_completed_with_error = partplate_list.get_curr_plate_index();;
        }
        has_error = true;
        is_finished = true;
    }
    if (evt.cancelled()) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", cancel event, status: %1%") % evt.status();
        this->notification_manager->set_slicing_progress_canceled(_u8L("Slicing Canceled"));
        is_finished = true;
    }

    //BBS: set the current plater's slice result to valid
    if (!this->background_process.empty())
        this->background_process.get_current_plate()->update_slice_result_valid_state(evt.success());

    //BBS: update the action button according to the current plate's status
    bool ready_to_slice = !this->partplate_list.get_curr_plate()->is_slice_result_valid();

    // BBS
#if 0
    this->sidebar->show_sliced_info_sizer(evt.success());
#endif

    // This updates the "Slice now", "Export G-code", "Arrange" buttons status.
    // Namely, it refreshes the "Out of print bed" property of all the ModelObjects, and it enables
    // the "Slice now" and "Export G-code" buttons based on their "out of bed" status.
    //BBS: remove this update here, will be updated in update_fff_scene later
    //this->object_list_changed();

    // refresh preview
    if (view3D->is_dragging()) // updating scene now would interfere with the gizmo dragging
        delayed_scene_refresh = true;
    else {
        if (this->printer_technology == ptFFF) {
            if (is_finished)
                this->update_fff_scene();
        }
        else
            this->update_sla_scene();
    }

    //BBS: add slice&&print status update logic
    if (evt.cancelled()) {
        /*if (wxGetApp().get_mode() == comSimple)
            sidebar->set_btn_label(ActionButtonType::abReslice, "Slice now");
        show_action_buttons(true);*/
        ready_to_slice = true;
        //this->main_frame->update_slice_print_status(MainFrame::eEventSliceUpdate, true, true);

        //BBS
        if (m_is_publishing) {
            m_publish_dlg->cancel();
        }
    } else {
        if((ready_to_slice) || (wxGetApp().get_mode() == comSimple)) {
            //this means the current plate is not the slicing plate
            //show_action_buttons(ready_to_slice);
            //this->main_frame->update_slice_print_status(MainFrame::eEventSliceUpdate, ready_to_slice, true);
        }
        if (exporting_status != ExportingStatus::NOT_EXPORTING && !has_error) {
            notification_manager->stop_delayed_notifications_of_type(NotificationType::ExportOngoing);
            notification_manager->close_notification_of_type(NotificationType::ExportOngoing);
        }
        // If writing to removable drive was scheduled, show notification with eject button
        if (exporting_status == ExportingStatus::EXPORTING_TO_REMOVABLE && !has_error) {
            //show_action_buttons(ready_to_slice);
            this->main_frame->update_slice_print_status(MainFrame::eEventSliceUpdate, ready_to_slice, true);
            notification_manager->push_exporting_finished_notification(last_output_path, last_output_dir_path,
                // Don't offer the "Eject" button on ChromeOS, the Linux side has no control over it.
                platform_flavor() != PlatformFlavor::LinuxOnChromium);
            wxGetApp().removable_drive_manager()->set_exporting_finished(true);
        }else
        if (exporting_status == ExportingStatus::EXPORTING_TO_LOCAL && !has_error)
            notification_manager->push_exporting_finished_notification(last_output_path, last_output_dir_path, false);

        // BBS, Generate calibration thumbnail for current plate
        if (!has_error && preview) {
            // generate calibration data
            /* BBS generate calibration data by printer
            preview->reload_print();
            ThumbnailData* calibration_data = &partplate_list.get_curr_plate()->cali_thumbnail_data;
            const ThumbnailsParams calibration_params = { {}, false, true, true, true, partplate_list.get_curr_plate_index() };
            generate_calibration_thumbnail(*calibration_data, PartPlate::cali_thumbnail_width, PartPlate::cali_thumbnail_height, calibration_params);
            preview->get_canvas3d()->reset_gcode_toolpaths();*/

            // generate bbox data
            PlateBBoxData* plate_bbox_data = &partplate_list.get_curr_plate()->cali_bboxes_data;
            *plate_bbox_data = generate_first_layer_bbox();
        }
    }

    exporting_status = ExportingStatus::NOT_EXPORTING;


    // BBS stop publishing if error occur
    //if (m_is_publishing) {
    //    GCodeProcessorResult *gcode_result = background_process.get_current_gcode_result();
    //    m_publish_dlg->UpdateStatus(_L("Error occurred during slicing"), -1, false);
    //    // if toolpath is outside
    //    if (!gcode_result || gcode_result->toolpath_outside) {
    //        m_is_publishing = false;
    //    }
    //}


    if (is_finished)
    {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":finished, reload print soon");
        m_is_slicing = false;
        this->preview->reload_print(false);
        /* BBS if in publishing progress */
        if (m_is_publishing) {
            if (m_publish_dlg && !m_publish_dlg->was_cancelled()) {
                if (m_publish_dlg->IsShown()) {
                    q->publish_project();
                } else {
                    m_is_publishing = false;
                }
            }
        }
        q->SetDropTarget(new PlaterDropTarget(q));
    }
    else
    {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":slicing all, plate %1% finished, start next slice...")%m_cur_slice_plate;
        m_cur_slice_plate++;

        q->Freeze();
        q->select_plate(m_cur_slice_plate);
        partplate_list.select_plate_view();
        int ret = q->start_next_slice();
        if (ret) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":slicing all, plate %1% can not be sliced, will stop")%m_cur_slice_plate;
            m_is_slicing = false;
        }
        //not the last plate
        update_fff_scene_only_shells();
        q->Thaw();
        if (m_is_publishing) {
            if (m_publish_dlg && !m_publish_dlg->was_cancelled()) {
                wxString msg = wxString::Format(_L("Slicing Plate %d"), m_cur_slice_plate + 1);
                int percent  = 70 * m_cur_slice_plate / partplate_list.get_plate_count();
                m_publish_dlg->UpdateStatus(msg, percent, false);
            }
        }
    }
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(", exit.");
}

void Plater::priv::on_action_add(SimpleEvent&)
{
    if (q != nullptr) {
        //q->add_model();
        //BBS open file in toolbar add
        q->add_file();
    }
}

//BBS: add plate from toolbar
void Plater::priv::on_action_add_plate(SimpleEvent&)
{
    if (q != nullptr) {
        take_snapshot("add partplate");
        this->partplate_list.create_plate();
        int new_plate = this->partplate_list.get_plate_count() - 1;
        this->partplate_list.select_plate(new_plate);
        update();

        // BBS set default view
        //q->get_camera().select_view("topfront");
        q->get_camera().requires_zoom_to_plate = REQUIRES_ZOOM_TO_ALL_PLATE;
    }
}

//BBS: remove plate from toolbar
void Plater::priv::on_action_del_plate(SimpleEvent&)
{
    if (q != nullptr) {
        q->delete_plate();
        //q->get_camera().select_view("topfront");
        //q->get_camera().requires_zoom_to_plate = REQUIRES_ZOOM_TO_ALL_PLATE;
    }
}

//BBS: GUI refactor: GLToolbar
void Plater::priv::on_action_open_project(SimpleEvent&)
{
    if (q != nullptr) {
        q->load_project();
    }
}

//BBS: GUI refactor: slice plate
void Plater::priv::on_action_slice_plate(SimpleEvent&)
{
    if (q != nullptr) {
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ":received slice plate event\n" ;
        //BBS update extruder params and speed table before slicing
        Plater::setExtruderParams(Slic3r::Model::extruderParamsMap);
        Plater::setPrintSpeedTable(Slic3r::Model::printSpeedMap);
        m_slice_all = false;
        q->reslice();
        q->select_view_3D("Preview");
    }
}

//BBS: GUI refactor: slice all
void Plater::priv::on_action_slice_all(SimpleEvent&)
{
    if (q != nullptr) {
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ":received slice project event\n" ;
        //BBS update extruder params and speed table before slicing
        Plater::setExtruderParams(Slic3r::Model::extruderParamsMap);
        Plater::setPrintSpeedTable(Slic3r::Model::printSpeedMap);
        m_slice_all = true;
        m_slice_all_only_has_gcode = true;
        m_cur_slice_plate = 0;
        //select plate
        q->select_plate(m_cur_slice_plate);
        q->reslice();
        if (!m_is_publishing)
            q->select_view_3D("Preview");
        //BBS: wish to select all plates stats item
        preview->get_canvas3d()->_update_select_plate_toolbar_stats_item(true);
    }
}

void Plater::priv::on_action_publish(wxCommandEvent &event)
{
    if (q != nullptr) {
        if (event.GetInt() == EVT_PUBLISHING_START) {
            // update by background slicing process
            if (process_completed_with_error >= 0) {
                wxString msg = _L("Please resolve the slicing errors and publish again.");
                this->m_publish_dlg->UpdateStatus(msg, false);
                return;
            }

            m_is_publishing = true;
            // if slicing is ready publish project, else slicing first
            if (partplate_list.is_all_slice_results_valid()) {
                q->publish_project();
            } else {
                BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ":received slice project in background event\n";
                SimpleEvent evt = SimpleEvent(EVT_GLTOOLBAR_SLICE_ALL);
                this->on_action_slice_all(evt);
            }
        } else {
            m_is_publishing = false;
            show_publish_dlg(false);
        }
    }
}

void Plater::priv::on_action_print_plate(SimpleEvent&)
{
    if (q != nullptr) {
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ":received print plate event\n" ;
    }

    //do not check login for lan printing
    ////BBS check login status
    //if (!wxGetApp().check_login()) return;


    //BBS
    if (!m_select_machine_dlg) m_select_machine_dlg = new SelectMachineDialog(q);
    m_select_machine_dlg->prepare(partplate_list.get_curr_plate_index());
    m_select_machine_dlg->ShowModal();
}


void Plater::priv::on_action_send_to_printer(bool isall)
{
	if (!m_send_to_sdcard_dlg) m_send_to_sdcard_dlg = new SendToPrinterDialog(q);
    if (isall) {
        m_send_to_sdcard_dlg->prepare(PLATE_ALL_IDX);
    }
    else {
        m_send_to_sdcard_dlg->prepare(partplate_list.get_curr_plate_index());
    }

	m_send_to_sdcard_dlg->ShowModal();
}

void Plater::priv::on_action_select_sliced_plate(wxCommandEvent &evt)
{
    if (q != nullptr) {
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ":received select sliced plate event\n" ;
    }
    q->select_sliced_plate(evt.GetInt());
}

void Plater::priv::on_action_print_all(SimpleEvent&)
{
    if (q != nullptr) {
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ":received print all event\n" ;
    }

    //BBS
    if (!m_select_machine_dlg) m_select_machine_dlg = new SelectMachineDialog(q);
    m_select_machine_dlg->prepare(PLATE_ALL_IDX);
    m_select_machine_dlg->ShowModal();
}

void Plater::priv::on_action_export_gcode(SimpleEvent&)
{
    if (q != nullptr) {
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ":received export gcode event\n" ;
        q->export_gcode(false);
    }
}

void Plater::priv::on_action_send_gcode(SimpleEvent&)
{
    if (q != nullptr) {
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ":received export gcode event\n" ;
        q->send_gcode_legacy();
    }
}

void Plater::priv::on_action_export_sliced_file(SimpleEvent&)
{
    if (q != nullptr) {
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ":received export sliced file event\n" ;
        q->export_gcode_3mf();
    }
}

void Plater::priv::on_action_export_all_sliced_file(SimpleEvent &)
{
    if (q != nullptr) {
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ":received export all sliced file event\n";
        q->export_gcode_3mf(true);
    }
}

void Plater::priv::on_action_export_to_sdcard(SimpleEvent&)
{
	if (q != nullptr) {
		BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ":received export sliced file event\n";
		q->send_to_printer();
	}
}

void Plater::priv::on_action_export_to_sdcard_all(SimpleEvent&)
{
    if (q != nullptr) {
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ":received export sliced file event\n";
        q->send_to_printer(true);
    }
}


//BBS: add plate select logic
void Plater::priv::on_plate_selected(SimpleEvent&)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ":received plate selected event\n" ;
    sidebar->obj_list()->on_plate_selected(partplate_list.get_curr_plate_index());
}

void Plater::priv::on_action_request_model_id(wxCommandEvent& evt)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ":received import model id event\n" ;
    if (q != nullptr) {
        q->import_model_id(evt.GetString().ToStdString());
    }
}

void Plater::priv::on_action_download_project(wxCommandEvent& evt)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ":received download project event\n" ;
    if (q != nullptr) {
        q->download_project(evt.GetString());
    }
}

//BBS: add slice button status update logic
void Plater::priv::on_slice_button_status(bool enable)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ": enable = "<<enable<<"\n";
    if (!background_process.running())
        main_frame->update_slice_print_status(MainFrame::eEventObjectUpdate, enable);
}

void Plater::priv::on_action_split_objects(SimpleEvent&)
{
    split_object();
}

void Plater::priv::on_action_split_volumes(SimpleEvent&)
{
    split_volume();
}

void Plater::priv::on_object_select(SimpleEvent& evt)
{
    wxGetApp().obj_list()->update_selections();
    selection_changed();
}

//BBS: repair model through netfabb
void Plater::priv::on_repair_model(wxCommandEvent &event)
{
    wxGetApp().obj_list()->fix_through_netfabb();
}

void Plater::priv::on_filament_color_changed(wxCommandEvent &event)
{
    //q->update_all_plate_thumbnails(true);
    //q->get_preview_canvas3D()->update_plate_thumbnails();
}

void Plater::priv::install_network_plugin(wxCommandEvent &event)
{
    wxGetApp().ShowDownNetPluginDlg();
    return;
}

void Plater::priv::update_plugin_when_launch(wxCommandEvent &event)
{
    std::string data_dir_str = data_dir();
    boost::filesystem::path data_dir_path(data_dir_str);
    auto cache_folder = data_dir_path / "ota";
    std::string changelog_file = cache_folder.string() + "/network_plugins.json";

    UpdatePluginDialog dlg(wxGetApp().mainframe);
    dlg.update_info(changelog_file);
    auto result = dlg.ShowModal();

    auto app_config = wxGetApp().app_config;
    if (!app_config) return;

    if (result == wxID_OK) {
        app_config->set("update_network_plugin", "true");
    }
    else if (result == wxID_NO) {
        app_config->set("update_network_plugin", "false");
    }
    app_config->save();
}

void Plater::priv::show_install_plugin_hint(wxCommandEvent &event)
{
    notification_manager->bbl_show_plugin_install_notification(into_u8(_L("Network Plug-in is not detected. Network related features are unavailable.")));
}

void Plater::priv::show_preview_only_hint(wxCommandEvent &event)
{
    notification_manager->bbl_show_preview_only_notification(into_u8(_L("Preview only mode:\nThe loaded file contains gcode only, Can not enter the Prepare page")));
}

void Plater::priv::on_apple_change_color_mode(wxSysColourChangedEvent& evt) {
    m_is_dark = wxSystemSettings::GetAppearance().IsDark();
    if (view3D->get_canvas3d() && view3D->get_canvas3d()->is_initialized()) {
        view3D->get_canvas3d()->on_change_color_mode(m_is_dark);
        preview->get_canvas3d()->on_change_color_mode(m_is_dark);
        assemble_view->get_canvas3d()->on_change_color_mode(m_is_dark);
    }
}

void Plater::priv::on_change_color_mode(SimpleEvent& evt) {
    m_is_dark = wxGetApp().app_config->get("dark_color_mode") == "1";
    view3D->get_canvas3d()->on_change_color_mode(m_is_dark);
    preview->get_canvas3d()->on_change_color_mode(m_is_dark);
    assemble_view->get_canvas3d()->on_change_color_mode(m_is_dark);
}

void Plater::priv::on_right_click(RBtnEvent& evt)
{
    int obj_idx = get_selected_object_idx();

    wxMenu* menu = nullptr;

    if (obj_idx == -1) { // no one or several object are selected
        if (evt.data.second) { // right button was clicked on empty space
            if (!get_selection().is_empty()) // several objects are selected in 3DScene
                return;
            menu = menus.default_menu();
        }
        else {
            if (current_panel == assemble_view) {
                menu = menus.assemble_multi_selection_menu();
            }
            else {
                menu = menus.multi_selection_menu();
            }
        }
    }
    else {
        // If in 3DScene is(are) selected volume(s), but right button was clicked on empty space
        if (evt.data.second)
            return;

        // Each context menu respects to the selected item in ObjectList,
        // so this selection should be updated before menu agyuicreation
        wxGetApp().obj_list()->update_selections();

        if (printer_technology == ptSLA)
            menu = menus.sla_object_menu();
        else {
            const Selection& selection = get_selection();
            // show "Object menu" for each one or several FullInstance instead of FullObject
            const bool is_some_full_instances = selection.is_single_full_instance() ||
                                                selection.is_single_full_object() ||
                                                selection.is_multiple_full_instance();
            const bool is_part = selection.is_single_volume() || selection.is_single_modifier();

            //BBS get assemble view menu
            if (current_panel == assemble_view) {
                menu = is_some_full_instances   ? menus.assemble_object_menu() :
                   is_part                  ? menus.assemble_part_menu()   : menus.assemble_multi_selection_menu();
            } else {
                menu = is_some_full_instances   ? menus.object_menu() :
                       is_part                  ? menus.part_menu()   : menus.multi_selection_menu();
            }
        }
    }

    if (q != nullptr && menu) {
#ifdef __linux__
        // For some reason on Linux the menu isn't displayed if position is specified
        // (even though the position is sane).
        q->PopupMenu(menu);
#else
        //BBS: GUI refactor: move sidebar to the left
        int x, y;
        current_panel->GetPosition(&x, &y);
        q->PopupMenu(menu, (int)evt.data.first.x() + x, (int)evt.data.first.y());
        //q->PopupMenu(menu);
#endif
    }
}

//BBS: add part plate related logic
void Plater::priv::on_plate_right_click(RBtnPlateEvent& evt)
{
    wxMenu* menu = menus.plate_menu();

#ifdef __linux__
    q->PopupMenu(menu);
#else
    //BBS: GUI refactor: move sidebar to the left
    int x, y;
    current_panel->GetPosition(&x, &y);
    q->PopupMenu(menu, (int)evt.data.first.x() + x, (int)evt.data.first.y());
    //q->PopupMenu(menu);
#endif
}

void Plater::priv::on_update_geometry(Vec3dsEvent<2>&)
{
    // TODO
}

void Plater::priv::on_3dcanvas_mouse_dragging_started(SimpleEvent&)
{
    view3D->get_canvas3d()->reset_sequential_print_clearance();
}

// Update the scene from the background processing,
// if the update message was received during mouse manipulation.
void Plater::priv::on_3dcanvas_mouse_dragging_finished(SimpleEvent&)
{
    if (delayed_scene_refresh) {
        delayed_scene_refresh = false;
        update_sla_scene();
    }

    //partplate_list.reload_all_objects();
}

//BBS: add plate id for thumbnail generate param
void Plater::priv::generate_thumbnail(ThumbnailData& data, unsigned int w, unsigned int h, const ThumbnailsParams& thumbnail_params, Camera::EType camera_type)
{
    view3D->get_canvas3d()->render_thumbnail(data, w, h, thumbnail_params, camera_type);
}

//BBS: add plate id for thumbnail generate param
ThumbnailsList Plater::priv::generate_thumbnails(const ThumbnailsParams& params, Camera::EType camera_type)
{
    ThumbnailsList thumbnails;
    for (const Vec2d& size : params.sizes) {
        thumbnails.push_back(ThumbnailData());
        Point isize(size); // round to ints
        generate_thumbnail(thumbnails.back(), isize.x(), isize.y(), params, camera_type);
        if (!thumbnails.back().is_valid())
            thumbnails.pop_back();
    }
    return thumbnails;
}

void Plater::priv::generate_calibration_thumbnail(ThumbnailData& data, unsigned int w, unsigned int h, const ThumbnailsParams& thumbnail_params)
{
    preview->get_canvas3d()->render_calibration_thumbnail(data, w, h, thumbnail_params);
}

PlateBBoxData Plater::priv::generate_first_layer_bbox()
{
    PlateBBoxData bboxdata;
    std::vector<BBoxData>& id_bboxes = bboxdata.bbox_objs;
    BoundingBoxf bbox_all;
    auto                   print = this->background_process.m_fff_print;
    auto curr_plate = this->partplate_list.get_curr_plate();
    auto curr_plate_seq = curr_plate->get_real_print_seq();
    bboxdata.is_seq_print = (curr_plate_seq == PrintSequence::ByObject);
    bboxdata.first_extruder = print->get_tool_ordering().first_extruder();
    bboxdata.bed_type       = bed_type_to_gcode_string(print->config().curr_bed_type.value);
    // get nozzle diameter
    auto opt_nozzle_diameters = print->config().option<ConfigOptionFloats>("nozzle_diameter");
    if (opt_nozzle_diameters != nullptr)
        bboxdata.nozzle_diameter = float(opt_nozzle_diameters->get_at(bboxdata.first_extruder));
    //PrintObjectPtrs objects;
    //if (this->printer_technology == ptFFF) {
    //    objects = this->background_process.m_fff_print->objects().vector();
    //}
    //else {
    //    objects = this->background_process.m_sla_print->objects();
    //}
    auto objects = print->objects();
    auto orig = this->partplate_list.get_curr_plate()->get_origin();
    Vec2d orig2d = { orig[0], orig[1] };

    BBoxData data;
    for (auto obj : objects)
    {
        auto bb_scaled = obj->get_first_layer_bbox(data.area, data.layer_height, data.name);
        auto bb = unscaled(bb_scaled);
        bb.min -= orig2d;
        bb.max -= orig2d;
        bbox_all.merge(bb);
        data.area *= (SCALING_FACTOR * SCALING_FACTOR); // unscale area
        data.id = obj->id().id;
        data.bbox = { bb.min.x(),bb.min.y(),bb.max.x(),bb.max.y() };
        id_bboxes.emplace_back(data);
    }

    // add wipe tower bounding box
    if (print->has_wipe_tower()) {
        auto   wt_corners = print->first_layer_wipe_tower_corners();
        // when loading gcode.3mf, wipe tower info may not be correct
        if (!wt_corners.empty()) {
            BoundingBox bb_scaled = {wt_corners[0], wt_corners[2]};
            auto        bb        = unscaled(bb_scaled);
            bb.min -= orig2d;
            bb.max -= orig2d;
            bbox_all.merge(bb);
            data.name = "wipe_tower";
            data.id   = partplate_list.get_curr_plate()->get_index() + 1000;
            data.bbox = {bb.min.x(), bb.min.y(), bb.max.x(), bb.max.y()};
            id_bboxes.emplace_back(data);
        }
    }

    bboxdata.bbox_all = { bbox_all.min.x(),bbox_all.min.y(),bbox_all.max.x(),bbox_all.max.y() };
    return bboxdata;
}

wxString Plater::priv::get_project_filename(const wxString& extension) const
{
    if (m_project_name.empty())
        return "";
    else {
        auto full_filename = m_project_folder / std::string((m_project_name + extension).mb_str(wxConvUTF8));
        return m_project_folder.empty() ? "" : from_path(full_filename);
    }
}

wxString Plater::priv::get_export_gcode_filename(const wxString& extension, bool only_filename, bool export_all) const
{
    std::string plate_index_str;
    if (partplate_list.get_plate_count() > 1)
        plate_index_str = (boost::format("_plate_%1%") % std::to_string(partplate_list.get_curr_plate_index() + 1)).str();
    if (!m_project_folder.empty()) {
        if (!only_filename) {
            if (export_all) {
                auto full_filename = m_project_folder / std::string((m_project_name + extension).mb_str(wxConvUTF8));
                return from_path(full_filename);
            } else {
                auto full_filename = m_project_folder / std::string((m_project_name + plate_index_str + extension).mb_str(wxConvUTF8));
                return from_path(full_filename);
            }
        } else {
            if (export_all)
                return m_project_name + wxString(plate_index_str) + extension;
            else
                return m_project_name + extension;
        }
    } else {
        if (only_filename) {
            if(m_project_name == L"Untitled")
                return fs::path(model.objects.front()->name).replace_extension().c_str() + wxString(plate_index_str) + extension;

            if (export_all)
                return m_project_name + extension;
            else
                return m_project_name + wxString(plate_index_str) + extension;
        }
        else
            return "";
    }
}

wxString Plater::priv::get_project_name()
{
    return m_project_name;
}

//BBS
void Plater::priv::set_project_name(const wxString& project_name)
{
    m_project_name = project_name;
    //update topbar title
    wxGetApp().mainframe->SetTitle(m_project_name);
#ifdef __WINDOWS__
    wxGetApp().mainframe->topbar()->SetTitle(m_project_name);
#else
    if (!m_project_name.IsEmpty())
        wxGetApp().mainframe->update_title_colour_after_set_title();
#endif
}

void Plater::priv::set_project_filename(const wxString& filename)
{
    boost::filesystem::path full_path = into_path(filename);
    boost::filesystem::path ext = full_path.extension();
    //if (boost::iequals(ext.string(), ".amf")) {
    //    // Remove the first extension.
    //    full_path.replace_extension("");
    //    // It may be ".zip.amf".
    //    if (boost::iequals(full_path.extension().string(), ".zip"))
    //        // Remove the 2nd extension.
    //        full_path.replace_extension("");
    //} else {
    //    // Remove just one extension.
    //    full_path.replace_extension("");
    //}
    full_path.replace_extension("");

    m_project_folder = full_path.parent_path();

    //BBS
    wxString project_name = from_u8(full_path.filename().string());
    set_project_name(project_name);
    // record filename for hint when open exported file/.gcode
    if (q->m_only_gcode)
        q->m_preview_only_filename = std::string((project_name + ".gcode").mb_str());
    if (q->m_exported_file)
        q->m_preview_only_filename = std::string((project_name + ".3mf").mb_str());

    wxGetApp().mainframe->update_title();

    if (!m_project_folder.empty() && !q->m_only_gcode)
        wxGetApp().mainframe->add_to_recent_projects(filename);
}

void Plater::priv::init_notification_manager()
{
    if (!notification_manager)
        return;
    notification_manager->init();

    auto cancel_callback = [this]() {
        if (this->background_process.idle())
            return false;
        this->background_process.stop();
        return true;
    };
    notification_manager->init_slicing_progress_notification(cancel_callback);
    notification_manager->set_fff(printer_technology == ptFFF);
    notification_manager->init_progress_indicator();
}

void Plater::orient()
{
    p->m_ui_jobs.orient();
}

//BBS: add job state related functions
void Plater::set_prepare_state(int state)
{
    p->m_job_prepare_state = state;
}

int Plater::get_prepare_state()
{
    return p->m_job_prepare_state;
}

void Plater::get_print_job_data(PrintPrepareData* data)
{
    if (data) {
        data->plate_idx = p->m_print_job_data.plate_idx;
        data->_3mf_path = p->m_print_job_data._3mf_path;
        data->_3mf_config_path = p->m_print_job_data._3mf_config_path;
    }
}

int Plater::get_print_finished_event()
{
    return EVT_PRINT_FINISHED;
}

int Plater::get_send_finished_event()
{
    return EVT_SEND_FINISHED;
}

int Plater::get_publish_finished_event()
{
    return EVT_PUBLISH_FINISHED;
}

void Plater::priv::set_current_canvas_as_dirty()
{
    if (current_panel == view3D)
        view3D->set_as_dirty();
    else if (current_panel == preview)
        preview->set_as_dirty();
    else if (current_panel == assemble_view)
        assemble_view->set_as_dirty();
}

GLCanvas3D* Plater::priv::get_current_canvas3D(bool exclude_preview)
{
    if (current_panel == view3D)
        return view3D->get_canvas3d();
    else if (!exclude_preview && (current_panel == preview))
        return preview->get_canvas3d();
    else if (current_panel == assemble_view)
        return assemble_view->get_canvas3d();
    else //BBS default set to view3D
        return view3D->get_canvas3d();

    //return (current_panel == view3D) ? view3D->get_canvas3d() : ((current_panel == preview) ? preview->get_canvas3d() : nullptr);
}

void Plater::priv::unbind_canvas_event_handlers()
{
    if (view3D != nullptr)
        view3D->get_canvas3d()->unbind_event_handlers();

    if (preview != nullptr)
        preview->get_canvas3d()->unbind_event_handlers();

    if (assemble_view != nullptr)
        assemble_view->get_canvas3d()->unbind_event_handlers();
}

void Plater::priv::reset_canvas_volumes()
{
    if (view3D != nullptr)
        view3D->get_canvas3d()->reset_volumes();

    if (preview != nullptr)
        preview->get_canvas3d()->reset_volumes();
}

bool Plater::priv::init_collapse_toolbar()
{
    if (wxGetApp().is_gcode_viewer())
        return true;

    if (collapse_toolbar.get_items_count() > 0)
        // already initialized
        return true;

    BackgroundTexture::Metadata background_data;
    background_data.filename = m_is_dark ? "toolbar_background_dark.png" : "toolbar_background.png";
    background_data.left = 16;
    background_data.top = 16;
    background_data.right = 16;
    background_data.bottom = 16;

    if (!collapse_toolbar.init(background_data))
        return false;

    collapse_toolbar.set_layout_type(GLToolbar::Layout::Vertical);
    collapse_toolbar.set_horizontal_orientation(GLToolbar::Layout::HO_Right);
    collapse_toolbar.set_vertical_orientation(GLToolbar::Layout::VO_Top);
    collapse_toolbar.set_border(5.0f);
    collapse_toolbar.set_separator_size(5);
    collapse_toolbar.set_gap_size(2);

    collapse_toolbar.del_all_item();

    GLToolbarItem::Data item;

    item.name = "collapse_sidebar";
    // set collapse svg name
    item.icon_filename = "*.svg";
    item.sprite_id = 0;
    item.left.action_callback = []() {
        wxGetApp().plater()->collapse_sidebar(!wxGetApp().plater()->is_sidebar_collapsed());
    };

    if (!collapse_toolbar.add_item(item))
        return false;

    // Now "collapse" sidebar to current state. This is done so the tooltip
    // is updated before the toolbar is first used.
    wxGetApp().plater()->collapse_sidebar(wxGetApp().plater()->is_sidebar_collapsed());
    return true;
}

void Plater::priv::update_preview_bottom_toolbar()
{
    ;
}

#if 0
void Plater::update_partplate()
{
    sidebar().update_partplate(p->partplate_list);
}
#endif

void Plater::priv::reset_gcode_toolpaths()
{
    preview->get_canvas3d()->reset_gcode_toolpaths();
}

bool Plater::priv::can_set_instance_to_object() const
{
    const int obj_idx = get_selected_object_idx();
    return 0 <= obj_idx && obj_idx < (int)model.objects.size() && model.objects[obj_idx]->instances.size() > 1;
}

bool Plater::priv::can_split(bool to_objects) const
{
    return sidebar->obj_list()->is_splittable(to_objects);
}

bool Plater::priv::can_fillcolor() const
{
    //BBS TODO
    return true;
}

bool Plater::priv::has_assemble_view() const
{
    for (auto object: model.objects)
    {
        for (auto instance : object->instances)
            if (instance->is_assemble_initialized())
                return true;

        int part_cnt = 0;
        for (auto volume : object->volumes) {
            if (volume->is_model_part())
                part_cnt++;
        }

        if (part_cnt > 1)
            return true;
    }
    return false;
}

#if ENABLE_ENHANCED_PRINT_VOLUME_FIT
bool Plater::priv::can_scale_to_print_volume() const
{
    const BuildVolume::Type type = this->bed.build_volume().type();
    return !view3D->get_canvas3d()->get_selection().is_empty() && (type == BuildVolume::Type::Rectangle || type == BuildVolume::Type::Circle);
}
#endif // ENABLE_ENHANCED_PRINT_VOLUME_FIT

bool Plater::priv::can_mirror() const
{
    return get_selection().is_from_single_instance();
}

bool Plater::priv::can_replace_with_stl() const
{
    return get_selection().get_volume_idxs().size() == 1;
}

bool Plater::priv::can_reload_from_disk() const
{
#if ENABLE_RELOAD_FROM_DISK_REWORK
    // collect selected reloadable ModelVolumes
    std::vector<std::pair<int, int>> selected_volumes = reloadable_volumes(model, get_selection());
    // nothing to reload, return
    if (selected_volumes.empty())
        return false;
#else
    // struct to hold selected ModelVolumes by their indices
    struct SelectedVolume
    {
        int object_idx;
        int volume_idx;

        // operators needed by std::algorithms
        bool operator < (const SelectedVolume& other) const { return (object_idx < other.object_idx) || ((object_idx == other.object_idx) && (volume_idx < other.volume_idx)); }
        bool operator == (const SelectedVolume& other) const { return (object_idx == other.object_idx) && (volume_idx == other.volume_idx); }
    };
    std::vector<SelectedVolume> selected_volumes;

    const Selection& selection = get_selection();

    // collects selected ModelVolumes
    const std::set<unsigned int>& selected_volumes_idxs = selection.get_volume_idxs();
    for (unsigned int idx : selected_volumes_idxs) {
        const GLVolume* v = selection.get_volume(idx);
        int v_idx = v->volume_idx();
        if (v_idx >= 0) {
            int o_idx = v->object_idx();
            if (0 <= o_idx && o_idx < (int)model.objects.size())
                selected_volumes.push_back({ o_idx, v_idx });
        }
    }
#endif // ENABLE_RELOAD_FROM_DISK_REWORK

#if ENABLE_RELOAD_FROM_DISK_REWORK
    std::sort(selected_volumes.begin(), selected_volumes.end(), [](const std::pair<int, int> &v1, const std::pair<int, int> &v2) {
        return (v1.first < v2.first) || (v1.first == v2.first && v1.second < v2.second);
        });
    selected_volumes.erase(std::unique(selected_volumes.begin(), selected_volumes.end(), [](const std::pair<int, int> &v1, const std::pair<int, int> &v2) {
        return (v1.first == v2.first) && (v1.second == v2.second);
        }), selected_volumes.end());

    // collects paths of files to load
    std::vector<fs::path> paths;
    for (auto [obj_idx, vol_idx] : selected_volumes) {
        paths.push_back(model.objects[obj_idx]->volumes[vol_idx]->source.input_file);
    }
#else
    std::sort(selected_volumes.begin(), selected_volumes.end());
    selected_volumes.erase(std::unique(selected_volumes.begin(), selected_volumes.end()), selected_volumes.end());

    // collects paths of files to load
    std::vector<fs::path> paths;
    for (const SelectedVolume& v : selected_volumes) {
        const ModelObject* object = model.objects[v.object_idx];
        const ModelVolume* volume = object->volumes[v.volume_idx];
        if (!volume->source.input_file.empty())
            paths.push_back(volume->source.input_file);
        else if (!object->input_file.empty() && !volume->name.empty() && !volume->source.is_from_builtin_objects)
            paths.push_back(volume->name);
    }
#endif // ENABLE_RELOAD_FROM_DISK_REWORK
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());

    return !paths.empty();
}

void Plater::priv::update_publish_dialog_status(wxString &msg, int percent)
{
    if (m_publish_dlg)
        m_publish_dlg->UpdateStatus(msg, percent);
}

bool Plater::priv::show_publish_dlg(bool show)
{
    if (q != nullptr) { BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ":recevied publish event\n"; }

    if (!m_publish_dlg) m_publish_dlg = new PublishDialog(q);
    if (show) {
        m_publish_dlg->reset();
        m_publish_dlg->start_slicing();
        //m_publish_dlg->Show();
        m_publish_dlg->ShowModal();
    } else {
        m_publish_dlg->EndModal(wxID_OK);
        //cancel the slicing
        if (this->background_process.running())
            this->background_process.stop();
    }
    return true;
}

//BBS: add bed exclude area
void Plater::priv::set_bed_shape(const Pointfs& shape, const Pointfs& exclude_areas, const double printable_height, const std::string& custom_texture, const std::string& custom_model, bool force_as_custom)
{
    //BBS: add shape position
    Vec2d shape_position = partplate_list.get_current_shape_position();
    bool new_shape = bed.set_shape(shape, printable_height, custom_model, force_as_custom, shape_position);

    float prev_height_lid, prev_height_rod;
    partplate_list.get_height_limits(prev_height_lid, prev_height_rod);
    double height_to_lid = config->opt_float("extruder_clearance_height_to_lid");
    double height_to_rod = config->opt_float("extruder_clearance_height_to_rod");

    Pointfs prev_exclude_areas = partplate_list.get_exclude_area();
    new_shape |= (height_to_lid != prev_height_lid) || (height_to_rod != prev_height_rod) || (prev_exclude_areas != exclude_areas);
    if (new_shape) {
        if (view3D) view3D->bed_shape_changed();
        if (preview) preview->bed_shape_changed();

        //BBS: update part plate's size
        // BBS: to be checked
        Vec3d max = bed.extended_bounding_box().max;
        Vec3d min = bed.extended_bounding_box().min;
        double z = config->opt_float("printable_height");

        //Pointfs& exclude_areas = config->option<ConfigOptionPoints>("bed_exclude_area")->values;
        partplate_list.reset_size(max.x() - min.x() - Bed3D::Axes::DefaultTipRadius, max.y() - min.y() - Bed3D::Axes::DefaultTipRadius, z);
        partplate_list.set_shapes(shape, exclude_areas, custom_texture, height_to_lid, height_to_rod);

        Vec2d new_shape_position = partplate_list.get_current_shape_position();
        if (shape_position != new_shape_position)
            bed.set_shape(shape, printable_height, custom_model, force_as_custom, new_shape_position);
    }
}

bool Plater::priv::can_delete() const
{
    return !get_selection().is_empty() && !get_selection().is_wipe_tower();
}

bool Plater::priv::can_delete_all() const
{
    return !model.objects.empty();
}

bool Plater::priv::can_edit_text() const
{
    const Selection &selection = view3D->get_canvas3d()->get_selection();
    if (selection.is_single_full_instance())
        return true;

    if (selection.is_single_volume()) {
        const GLVolume *gl_volume      = selection.get_volume(*selection.get_volume_idxs().begin());
        int             out_object_idx = gl_volume->object_idx();
        ModelObject *   model_object   = selection.get_model()->objects[out_object_idx];
        int             out_volume_idx = gl_volume->volume_idx();
        ModelVolume *   model_volume   = model_object->volumes[out_volume_idx];
        if (model_volume)
            return !model_volume->get_text_info().m_text.empty();
    }
    return false;
}

bool Plater::priv::can_add_plate() const
{
    return q->get_partplate_list().get_plate_count() < PartPlateList::MAX_PLATES_COUNT;
}

bool Plater::priv::can_delete_plate() const
{
    return q->get_partplate_list().get_plate_count() > 1;
}

bool Plater::priv::can_fix_through_netfabb() const
{
    std::vector<int> obj_idxs, vol_idxs;
    sidebar->obj_list()->get_selection_indexes(obj_idxs, vol_idxs);

#if FIX_THROUGH_NETFABB_ALWAYS
    // Fixing always.
    return ! obj_idxs.empty() || ! vol_idxs.empty();
#else // FIX_THROUGH_NETFABB_ALWAYS
    // Fixing only if the model is not manifold.
    if (vol_idxs.empty()) {
        for (auto obj_idx : obj_idxs)
            if (model.objects[obj_idx]->get_repaired_errors_count() > 0)
                return true;
        return false;
    }

    int obj_idx = obj_idxs.front();
    for (auto vol_idx : vol_idxs)
        if (model.objects[obj_idx]->get_repaired_errors_count(vol_idx) > 0)
            return true;
    return false;
#endif // FIX_THROUGH_NETFABB_ALWAYS
}

bool Plater::priv::can_simplify() const
{
    // is object for simplification selected
    if (get_selected_object_idx() < 0) return false;
    // is already opened?
    if (q->get_view3D_canvas3D()->get_gizmos_manager().get_current_type() ==
        GLGizmosManager::EType::Simplify)
        return false;
    return true;
}

bool Plater::priv::can_increase_instances() const
{
    if (m_ui_jobs.is_any_running()
     || q->get_view3D_canvas3D()->get_gizmos_manager().is_in_editing_mode())
            return false;

    int obj_idx = get_selected_object_idx();
    return (0 <= obj_idx) && (obj_idx < (int)model.objects.size());
}

bool Plater::priv::can_decrease_instances() const
{
    if (m_ui_jobs.is_any_running()
     || q->get_view3D_canvas3D()->get_gizmos_manager().is_in_editing_mode())
            return false;

    int obj_idx = get_selected_object_idx();
    return (0 <= obj_idx) && (obj_idx < (int)model.objects.size()) && (model.objects[obj_idx]->instances.size() > 1);
}

bool Plater::priv::can_split_to_objects() const
{
    return q->can_split(true);
}

bool Plater::priv::can_split_to_volumes() const
{
    return (printer_technology != ptSLA) && q->can_split(false);
}

bool Plater::priv::can_arrange() const
{
    return !model.objects.empty() && !m_ui_jobs.is_any_running();
}

bool Plater::priv::layers_height_allowed() const
{
    if (printer_technology != ptFFF)
        return false;

    int obj_idx = get_selected_object_idx();
    return 0 <= obj_idx && obj_idx < (int)model.objects.size() && model.objects[obj_idx]->bounding_box().max.z() > SINKING_Z_THRESHOLD && view3D->is_layers_editing_allowed();
}

bool Plater::priv::can_layers_editing() const
{
    return layers_height_allowed();
}

void Plater::priv::on_action_layersediting(SimpleEvent&)
{
    view3D->enable_layers_editing(!view3D->is_layers_editing_enabled());
    notification_manager->set_move_from_overlay(view3D->is_layers_editing_enabled());
}

void Plater::priv::enter_gizmos_stack()
{
    assert(m_undo_redo_stack_active == &m_undo_redo_stack_main);
    if (m_undo_redo_stack_active == &m_undo_redo_stack_main) {
        m_undo_redo_stack_active = &m_undo_redo_stack_gizmos;
        assert(m_undo_redo_stack_active->empty());
        // Take the initial snapshot of the gizmos.
        // Not localized on purpose, the text will never be shown to the user.
        this->take_snapshot(std::string("Gizmos-Initial"));
    }
}

bool Plater::priv::leave_gizmos_stack()
{
    bool changed = false;
    assert(m_undo_redo_stack_active == &m_undo_redo_stack_gizmos);
    if (m_undo_redo_stack_active == &m_undo_redo_stack_gizmos) {
        assert(! m_undo_redo_stack_active->empty());
        changed = m_undo_redo_stack_gizmos.has_undo_snapshot();
        m_undo_redo_stack_active->clear();
        m_undo_redo_stack_active = &m_undo_redo_stack_main;
    }
    return changed;
}

int Plater::priv::get_active_snapshot_index()
{
    const size_t active_snapshot_time = this->undo_redo_stack().active_snapshot_time();
    const std::vector<UndoRedo::Snapshot>& ss_stack = this->undo_redo_stack().snapshots();
    const auto it = std::lower_bound(ss_stack.begin(), ss_stack.end(), UndoRedo::Snapshot(active_snapshot_time));
    return it - ss_stack.begin();
}

void Plater::priv::take_snapshot(const std::string& snapshot_name, const UndoRedo::SnapshotType snapshot_type)
{
    if (m_prevent_snapshots > 0)
        return;
    assert(m_prevent_snapshots >= 0);
    // BBS: single snapshot
    if (m_single && !m_single->check(snapshot_modifies_project(snapshot_type) && (snapshot_name.empty() || snapshot_name.back() != '!')))
        return;
    UndoRedo::SnapshotData snapshot_data;
    snapshot_data.snapshot_type      = snapshot_type;
    snapshot_data.printer_technology = this->printer_technology;
    if (this->view3D->is_layers_editing_enabled())
        snapshot_data.flags |= UndoRedo::SnapshotData::VARIABLE_LAYER_EDITING_ACTIVE;
    if (this->sidebar->obj_list()->is_selected(itSettings)) {
        snapshot_data.flags |= UndoRedo::SnapshotData::SELECTED_SETTINGS_ON_SIDEBAR;
        snapshot_data.layer_range_idx = this->sidebar->obj_list()->get_selected_layers_range_idx();
    }
    else if (this->sidebar->obj_list()->is_selected(itLayer)) {
        snapshot_data.flags |= UndoRedo::SnapshotData::SELECTED_LAYER_ON_SIDEBAR;
        snapshot_data.layer_range_idx = this->sidebar->obj_list()->get_selected_layers_range_idx();
    }
    else if (this->sidebar->obj_list()->is_selected(itLayerRoot))
        snapshot_data.flags |= UndoRedo::SnapshotData::SELECTED_LAYERROOT_ON_SIDEBAR;

    // If SLA gizmo is active, ask it if it wants to trigger support generation
    // on loading this snapshot.
    if (view3D->get_canvas3d()->get_gizmos_manager().wants_reslice_supports_on_undo())
        snapshot_data.flags |= UndoRedo::SnapshotData::RECALCULATE_SLA_SUPPORTS;

    //FIXME updating the Wipe tower config values at the ModelWipeTower from the Print config.
    // This is a workaround until we refactor the Wipe Tower position / orientation to live solely inside the Model, not in the Print config.
    // BBS: add partplate logic
    if (this->printer_technology == ptFFF) {
        const DynamicPrintConfig& config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
        const DynamicPrintConfig& proj_cfg = wxGetApp().preset_bundle->project_config;
        const ConfigOptionFloats* tower_x_opt = proj_cfg.option<ConfigOptionFloats>("wipe_tower_x");
        const ConfigOptionFloats* tower_y_opt = proj_cfg.option<ConfigOptionFloats>("wipe_tower_y");
        assert(tower_x_opt->values.size() == tower_y_opt->values.size());
        model.wipe_tower.positions.clear();
        model.wipe_tower.positions.resize(tower_x_opt->values.size());
        for (int plate_idx = 0; plate_idx < tower_x_opt->values.size(); plate_idx++) {
            ModelWipeTower& tower = model.wipe_tower;

            tower.positions[plate_idx] = Vec2d(tower_x_opt->get_at(plate_idx), tower_y_opt->get_at(plate_idx));
            tower.rotation = proj_cfg.opt_float("wipe_tower_rotation_angle");
        }
    }
    const GLGizmosManager& gizmos = view3D->get_canvas3d()->get_gizmos_manager();

    if (snapshot_type == UndoRedo::SnapshotType::ProjectSeparator)
        this->undo_redo_stack().clear();
    this->undo_redo_stack().take_snapshot(snapshot_name, model, view3D->get_canvas3d()->get_selection(), view3D->get_canvas3d()->get_gizmos_manager(), partplate_list, snapshot_data);
    if (snapshot_type == UndoRedo::SnapshotType::LeavingGizmoWithAction) {
        // Filter all but the last UndoRedo::SnapshotType::GizmoAction in a row between the last UndoRedo::SnapshotType::EnteringGizmo and UndoRedo::SnapshotType::LeavingGizmoWithAction.
        // The remaining snapshot will be renamed to a more generic name,
        // depending on what gizmo is being left.
        assert(gizmos.get_current() != nullptr);
        std::string new_name = gizmos.get_current()->get_action_snapshot_name();
        this->undo_redo_stack().reduce_noisy_snapshots(new_name);
    } else if (snapshot_type == UndoRedo::SnapshotType::ProjectSeparator) {
        // Reset the "dirty project" flag.
        m_undo_redo_stack_main.mark_current_as_saved();
    }
    //BBS: add PartPlateList as the paremeter for take_snapshot
    this->undo_redo_stack().release_least_recently_used();

    dirty_state.update_from_undo_redo_stack(m_undo_redo_stack_main.project_modified());

    // Save the last active preset name of a particular printer technology.
    ((this->printer_technology == ptFFF) ? m_last_fff_printer_profile_name : m_last_sla_printer_profile_name) = wxGetApp().preset_bundle->printers.get_selected_preset_name();
    BOOST_LOG_TRIVIAL(info) << "Undo / Redo snapshot taken: " << snapshot_name << ", Undo / Redo stack memory: " << Slic3r::format_memsize_MB(this->undo_redo_stack().memsize()) << log_memory_info();
}

void Plater::priv::undo()
{
    const std::vector<UndoRedo::Snapshot> &snapshots = this->undo_redo_stack().snapshots();
    auto it_current = std::lower_bound(snapshots.begin(), snapshots.end(), UndoRedo::Snapshot(this->undo_redo_stack().active_snapshot_time()));
    // BBS: undo-redo until modify record
    while (--it_current != snapshots.begin() && !snapshot_modifies_project(*it_current));
    if (it_current == snapshots.begin()) return;
    this->undo_redo_to(it_current);
}

void Plater::priv::redo()
{
    const std::vector<UndoRedo::Snapshot> &snapshots = this->undo_redo_stack().snapshots();
    auto it_current = std::lower_bound(snapshots.begin(), snapshots.end(), UndoRedo::Snapshot(this->undo_redo_stack().active_snapshot_time()));
    // BBS: undo-redo until modify record
    while (it_current != snapshots.end() && !snapshot_modifies_project(*it_current++));
    if (it_current != snapshots.end()) {
        while (it_current != snapshots.end() && !snapshot_modifies_project(*it_current++));
        this->undo_redo_to(--it_current);
    }
}

void Plater::priv::undo_redo_to(size_t time_to_load)
{
    const std::vector<UndoRedo::Snapshot> &snapshots = this->undo_redo_stack().snapshots();
    auto it_current = std::lower_bound(snapshots.begin(), snapshots.end(), UndoRedo::Snapshot(time_to_load));
    assert(it_current != snapshots.end());
    this->undo_redo_to(it_current);
}

// BBS: check need save or backup
bool Plater::priv::up_to_date(bool saved, bool backup)
{
    size_t& last_time = backup ? m_backup_timestamp : m_saved_timestamp;
    if (saved) {
        last_time = undo_redo_stack_main().active_snapshot_time();
        if (!backup)
            undo_redo_stack_main().mark_current_as_saved();
        return true;
    }
    else {
        return !undo_redo_stack_main().has_real_change_from(last_time);
    }
}

void Plater::priv::undo_redo_to(std::vector<UndoRedo::Snapshot>::const_iterator it_snapshot)
{
    // Make sure that no updating function calls take_snapshot until we are done.
    SuppressSnapshots snapshot_supressor(q);

    bool 				temp_snapshot_was_taken 	= this->undo_redo_stack().temp_snapshot_active();
    PrinterTechnology 	new_printer_technology 		= it_snapshot->snapshot_data.printer_technology;
    bool 				printer_technology_changed 	= this->printer_technology != new_printer_technology;
    if (printer_technology_changed) {
        //BBS do not support SLA
    }
    // Save the last active preset name of a particular printer technology.
    ((this->printer_technology == ptFFF) ? m_last_fff_printer_profile_name : m_last_sla_printer_profile_name) = wxGetApp().preset_bundle->printers.get_selected_preset_name();
    //FIXME updating the Wipe tower config values at the ModelWipeTower from the Print config.
    // This is a workaround until we refactor the Wipe Tower position / orientation to live solely inside the Model, not in the Print config.
    // BBS: add partplate logic
    if (this->printer_technology == ptFFF) {
        const DynamicPrintConfig& config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
        const DynamicPrintConfig& proj_cfg = wxGetApp().preset_bundle->project_config;
        const ConfigOptionFloats* tower_x_opt = proj_cfg.option<ConfigOptionFloats>("wipe_tower_x");
        const ConfigOptionFloats* tower_y_opt = proj_cfg.option<ConfigOptionFloats>("wipe_tower_y");
        assert(tower_x_opt->values.size() == tower_y_opt->values.size());
        model.wipe_tower.positions.clear();
        model.wipe_tower.positions.resize(tower_x_opt->values.size());
        for (int plate_idx = 0; plate_idx < tower_x_opt->values.size(); plate_idx++) {
            ModelWipeTower& tower = model.wipe_tower;

            tower.positions[plate_idx] = Vec2d(tower_x_opt->get_at(plate_idx), tower_y_opt->get_at(plate_idx));
            tower.rotation = proj_cfg.opt_float("wipe_tower_rotation_angle");
        }
    }
    const int layer_range_idx = it_snapshot->snapshot_data.layer_range_idx;
    // Flags made of Snapshot::Flags enum values.
    unsigned int new_flags = it_snapshot->snapshot_data.flags;
    UndoRedo::SnapshotData top_snapshot_data;
    top_snapshot_data.printer_technology = this->printer_technology;
    if (this->view3D->is_layers_editing_enabled())
        top_snapshot_data.flags |= UndoRedo::SnapshotData::VARIABLE_LAYER_EDITING_ACTIVE;
    if (this->sidebar->obj_list()->is_selected(itSettings)) {
        top_snapshot_data.flags |= UndoRedo::SnapshotData::SELECTED_SETTINGS_ON_SIDEBAR;
        top_snapshot_data.layer_range_idx = this->sidebar->obj_list()->get_selected_layers_range_idx();
    }
    else if (this->sidebar->obj_list()->is_selected(itLayer)) {
        top_snapshot_data.flags |= UndoRedo::SnapshotData::SELECTED_LAYER_ON_SIDEBAR;
        top_snapshot_data.layer_range_idx = this->sidebar->obj_list()->get_selected_layers_range_idx();
    }
    else if (this->sidebar->obj_list()->is_selected(itLayerRoot))
        top_snapshot_data.flags |= UndoRedo::SnapshotData::SELECTED_LAYERROOT_ON_SIDEBAR;
    bool   		 new_variable_layer_editing_active = (new_flags & UndoRedo::SnapshotData::VARIABLE_LAYER_EDITING_ACTIVE) != 0;
    bool         new_selected_settings_on_sidebar  = (new_flags & UndoRedo::SnapshotData::SELECTED_SETTINGS_ON_SIDEBAR) != 0;
    bool         new_selected_layer_on_sidebar     = (new_flags & UndoRedo::SnapshotData::SELECTED_LAYER_ON_SIDEBAR) != 0;
    bool         new_selected_layerroot_on_sidebar = (new_flags & UndoRedo::SnapshotData::SELECTED_LAYERROOT_ON_SIDEBAR) != 0;

    if (this->view3D->get_canvas3d()->get_gizmos_manager().wants_reslice_supports_on_undo())
        top_snapshot_data.flags |= UndoRedo::SnapshotData::RECALCULATE_SLA_SUPPORTS;

    // Disable layer editing before the Undo / Redo jump.
    if (!new_variable_layer_editing_active && view3D->is_layers_editing_enabled())
        view3D->get_canvas3d()->force_main_toolbar_left_action(view3D->get_canvas3d()->get_main_toolbar_item_id("layersediting"));

    // Make a copy of the snapshot, undo/redo could invalidate the iterator
    const UndoRedo::Snapshot snapshot_copy = *it_snapshot;
    // Do the jump in time.
    if (it_snapshot->timestamp < this->undo_redo_stack().active_snapshot_time() ?
        this->undo_redo_stack().undo(model, this->view3D->get_canvas3d()->get_selection(), this->view3D->get_canvas3d()->get_gizmos_manager(), this->partplate_list, top_snapshot_data, it_snapshot->timestamp) :
        this->undo_redo_stack().redo(model, this->view3D->get_canvas3d()->get_gizmos_manager(), this->partplate_list, it_snapshot->timestamp)) {
        if (printer_technology_changed) {
            // Switch to the other printer technology. Switch to the last printer active for that particular technology.
            AppConfig *app_config = wxGetApp().app_config;
            app_config->set("presets", PRESET_PRINTER_NAME, (new_printer_technology == ptFFF) ? m_last_fff_printer_profile_name : m_last_sla_printer_profile_name);
            //FIXME Why are we reloading the whole preset bundle here? Please document. This is fishy and it is unnecessarily expensive.
            // Anyways, don't report any config value substitutions, they have been already reported to the user at application start up.
            wxGetApp().preset_bundle->load_presets(*app_config, ForwardCompatibilitySubstitutionRule::EnableSilent);
            // load_current_presets() calls Tab::load_current_preset() -> TabPrint::update() -> Object_list::update_and_show_object_settings_item(),
            // but the Object list still keeps pointer to the old Model. Avoid a crash by removing selection first.
            this->sidebar->obj_list()->unselect_objects();
            // Load the currently selected preset into the GUI, update the preset selection box.
            // This also switches the printer technology based on the printer technology of the active printer profile.
            wxGetApp().load_current_presets();
        }
        //FIXME updating the Print config from the Wipe tower config values at the ModelWipeTower.
        // This is a workaround until we refactor the Wipe Tower position / orientation to live solely inside the Model, not in the Print config.
        // BBS: add partplate logic
        if (this->printer_technology == ptFFF) {
            const DynamicPrintConfig& config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
            const DynamicPrintConfig& proj_cfg = wxGetApp().preset_bundle->project_config;
            ConfigOptionFloats* tower_x_opt = const_cast<ConfigOptionFloats*>(proj_cfg.option<ConfigOptionFloats>("wipe_tower_x"));
            ConfigOptionFloats* tower_y_opt = const_cast<ConfigOptionFloats*>(proj_cfg.option<ConfigOptionFloats>("wipe_tower_y"));
            // BBS: don't support wipe tower rotation
            //double current_rotation = proj_cfg.opt_float("wipe_tower_rotation_angle");
            bool need_update = false;
            if (tower_x_opt->values.size() != model.wipe_tower.positions.size()) {
                tower_x_opt->clear();
                ConfigOptionFloat default_tower_x(40.f);
                tower_x_opt->resize(model.wipe_tower.positions.size(), &default_tower_x);
                need_update = true;
            }

            if (tower_y_opt->values.size() != model.wipe_tower.positions.size()) {
                tower_y_opt->clear();
                ConfigOptionFloat default_tower_y(200.f);
                tower_y_opt->resize(model.wipe_tower.positions.size(), &default_tower_y);
                need_update = true;
            }

            for (int plate_idx = 0; plate_idx < model.wipe_tower.positions.size(); plate_idx++) {
                if (Vec2d(tower_x_opt->get_at(plate_idx), tower_y_opt->get_at(plate_idx)) != model.wipe_tower.positions[plate_idx]) {
                    ConfigOptionFloat tower_x_new(model.wipe_tower.positions[plate_idx].x());
                    ConfigOptionFloat tower_y_new(model.wipe_tower.positions[plate_idx].y());
                    tower_x_opt->set_at(&tower_x_new, plate_idx, 0);
                    tower_y_opt->set_at(&tower_y_new, plate_idx, 0);
                    need_update = true;
                    break;
                }
            }

            if (need_update) {
                // update print to current plate (preview->m_process)
                this->partplate_list.update_slice_context_to_current_plate(this->background_process);
                this->preview->update_gcode_result(this->partplate_list.get_current_slice_result());
                this->update();
            }
        }
        // set selection mode for ObjectList on sidebar
        this->sidebar->obj_list()->set_selection_mode(new_selected_settings_on_sidebar  ? ObjectList::SELECTION_MODE::smSettings :
                                                      new_selected_layer_on_sidebar     ? ObjectList::SELECTION_MODE::smLayer :
                                                      new_selected_layerroot_on_sidebar ? ObjectList::SELECTION_MODE::smLayerRoot :
                                                                                          ObjectList::SELECTION_MODE::smUndef);
        if (new_selected_settings_on_sidebar || new_selected_layer_on_sidebar)
            this->sidebar->obj_list()->set_selected_layers_range_idx(layer_range_idx);

        this->update_after_undo_redo(snapshot_copy, temp_snapshot_was_taken);
        // Enable layer editing after the Undo / Redo jump.
        if (!view3D->is_layers_editing_enabled() && this->layers_height_allowed() && new_variable_layer_editing_active)
            view3D->get_canvas3d()->force_main_toolbar_left_action(view3D->get_canvas3d()->get_main_toolbar_item_id("layersediting"));
    }

    dirty_state.update_from_undo_redo_stack(m_undo_redo_stack_main.project_modified());
}

void Plater::priv::update_after_undo_redo(const UndoRedo::Snapshot& snapshot, bool /* temp_snapshot_was_taken */)
{
    this->view3D->get_canvas3d()->get_selection().clear();
    // Update volumes from the deserializd model, always stop / update the background processing (for both the SLA and FFF technologies).
    this->update((unsigned int)UpdateParams::FORCE_BACKGROUND_PROCESSING_UPDATE | (unsigned int)UpdateParams::POSTPONE_VALIDATION_ERROR_MESSAGE);
    // Release old snapshots if the memory allocated is excessive. This may remove the top most snapshot if jumping to the very first snapshot.
    //if (temp_snapshot_was_taken)
    // Release the old snapshots always, as it may have happened, that some of the triangle meshes got deserialized from the snapshot, while some
    // triangle meshes may have gotten released from the scene or the background processing, therefore now being calculated into the Undo / Redo stack size.
        this->undo_redo_stack().release_least_recently_used();
    //YS_FIXME update obj_list from the deserialized model (maybe store ObjectIDs into the tree?) (no selections at this point of time)
    this->view3D->get_canvas3d()->get_selection().set_deserialized(GUI::Selection::EMode(this->undo_redo_stack().selection_deserialized().mode), this->undo_redo_stack().selection_deserialized().volumes_and_instances);
    this->view3D->get_canvas3d()->get_gizmos_manager().update_after_undo_redo(snapshot);

    wxGetApp().obj_list()->update_after_undo_redo();

    if (wxGetApp().get_mode() == comSimple && model_has_advanced_features(this->model)) {
        // If the user jumped to a snapshot that require user interface with advanced features, switch to the advanced mode without asking.
        // There is a little risk of surprising the user, as he already must have had the advanced or advanced mode active for such a snapshot to be taken.
        Slic3r::GUI::wxGetApp().save_mode(comAdvanced);
        view3D->set_as_dirty();
    }

    // this->update() above was called with POSTPONE_VALIDATION_ERROR_MESSAGE, so that if an error message was generated when updating the back end, it would not open immediately,
    // but it would be saved to be show later. Let's do it now. We do not want to display the message box earlier, because on Windows & OSX the message box takes over the message
    // queue pump, which in turn executes the rendering function before a full update after the Undo / Redo jump.
    this->show_delayed_error_message();

    //FIXME what about the state of the manipulators?
    //FIXME what about the focus? Cursor in the side panel?

    BOOST_LOG_TRIVIAL(info) << "Undo / Redo snapshot reloaded. Undo / Redo stack memory: " << Slic3r::format_memsize_MB(this->undo_redo_stack().memsize()) << log_memory_info();
}

void Plater::priv::bring_instance_forward() const
{
/*#ifdef __APPLE__
    wxGetApp().other_instance_message_handler()->bring_instance_forward();
    return;
#endif //__APPLE__*/
    if (main_frame == nullptr) {
        BOOST_LOG_TRIVIAL(debug) << "Couldnt bring instance forward - mainframe is null";
        return;
    }
    BOOST_LOG_TRIVIAL(debug) << "Orca Slicer window going forward";
    //this code maximize app window on Fedora
    {
        main_frame->Iconize(false);
        if (main_frame->IsMaximized())
            main_frame->Maximize(true);
        else
            main_frame->Maximize(false);
    }
    //this code maximize window on Ubuntu
    {
        main_frame->Restore();
        wxGetApp().GetTopWindow()->SetFocus();  // focus on my window
        wxGetApp().GetTopWindow()->Raise();  // bring window to front
        wxGetApp().GetTopWindow()->Show(true); // show the window
    }
}

//BBS: popup object table
bool Plater::priv::PopupObjectTable(int object_id, int volume_id, const wxPoint& position)
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" enter, create ObjectTableDialog");
    int max_width{1920}, max_height{1080};

    max_width = q->GetMaxWidth();
    max_height = q->GetMaxHeight();
    ObjectTableDialog table_dialog(q, q, &model, wxSize(max_width, max_height));
    //m_popup_table = new ObjectTableDialog(q, q,  &model);

    wxRect rect = sidebar->GetRect();
    wxPoint pos = sidebar->ClientToScreen(wxPoint(rect.x, rect.y));

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": show ObjectTableDialog");
    table_dialog.Popup(object_id, volume_id, pos);

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" finished, will destroy ObjectTableDialog");
    return true;
}

void Sidebar::set_btn_label(const ActionButtonType btn_type, const wxString& label) const
{
    switch (btn_type)
    {
        case ActionButtonType::abReslice:   p->btn_reslice->SetLabelText(label);        break;
        case ActionButtonType::abExport:    p->btn_export_gcode->SetLabelText(label);   break;
        case ActionButtonType::abSendGCode: /*p->btn_send_gcode->SetLabelText(label);*/     break;
    }
}

// Plater / Public

Plater::Plater(wxWindow *parent, MainFrame *main_frame)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxGetApp().get_min_size())
    , p(new priv(this, main_frame))
{
    // Initialization performed in the private c-tor
    enable_wireframe(false);
}

bool Plater::Show(bool show)
{
    if (wxGetApp().mainframe)
        wxGetApp().mainframe->show_option(show);
    return wxPanel::Show(show);
}

bool Plater::is_project_dirty() const { return p->is_project_dirty(); }
bool Plater::is_presets_dirty() const { return p->is_presets_dirty(); }
void Plater::set_plater_dirty(bool is_dirty) { p->set_plater_dirty(is_dirty); }
void Plater::update_project_dirty_from_presets() { p->update_project_dirty_from_presets(); }
int  Plater::save_project_if_dirty(const wxString& reason) { return p->save_project_if_dirty(reason); }
void Plater::reset_project_dirty_after_save() { p->reset_project_dirty_after_save(); }
void Plater::reset_project_dirty_initial_presets() { p->reset_project_dirty_initial_presets(); }
#if ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW
void Plater::render_project_state_debug_window() const { p->render_project_state_debug_window(); }
#endif // ENABLE_PROJECT_DIRTY_STATE_DEBUG_WINDOW

Sidebar&        Plater::sidebar()           { return *p->sidebar; }
const Model&    Plater::model() const       { return p->model; }
Model&          Plater::model()             { return p->model; }
const Print&    Plater::fff_print() const   { return p->fff_print; }
Print&          Plater::fff_print()         { return p->fff_print; }
const SLAPrint& Plater::sla_print() const   { return p->sla_print; }
SLAPrint&       Plater::sla_print()         { return p->sla_print; }

int Plater::new_project(bool skip_confirm, bool silent, const wxString& project_name)
{
    bool transfer_preset_changes = false;
    // BBS: save confirm
    auto check = [&transfer_preset_changes](bool yes_or_no) {
        wxString header = _L("Some presets are modified.") + "\n" +
            (yes_or_no ? _L("You can keep the modified presets to the new project or discard them") :
                _L("You can keep the modifield presets to the new project, discard or save changes as new presets."));
        using ab = UnsavedChangesDialog::ActionButtons;
        int act_buttons = ab::KEEP;
        if (!yes_or_no)
            act_buttons |= ab::SAVE;
        return wxGetApp().check_and_keep_current_preset_changes(_L("Creating a new project"), header, act_buttons, &transfer_preset_changes);
    };
    int result;
    if (!skip_confirm && (result = close_with_confirm(check)) == wxID_CANCEL)
        return wxID_CANCEL;

    //BBS: add only gcode mode
    bool previous_gcode = m_only_gcode;

    m_only_gcode = false;
    m_exported_file = false;
    m_loading_project = false;
    get_notification_manager()->bbl_close_plateinfo_notification();
    get_notification_manager()->bbl_close_preview_only_notification();
    get_notification_manager()->bbl_close_3mf_warn_notification();

    if (!silent)
        wxGetApp().mainframe->select_tab(MainFrame::tp3DEditor);

    //get_partplate_list().reinit();
    //get_partplate_list().update_slice_context_to_current_plate(p->background_process);
    //p->preview->update_gcode_result(p->partplate_list.get_current_slice_result());
    reset(transfer_preset_changes);
    reset_project_dirty_initial_presets();
    wxGetApp().update_saved_preset_from_current_preset();
    update_project_dirty_from_presets();

    //reset project
    p->project.reset();
    //set project name
    if (project_name.empty())
        p->set_project_name(_L("Untitled"));
    else
        p->set_project_name(project_name);

    Plater::TakeSnapshot snapshot(this, "New Project", UndoRedo::SnapshotType::ProjectSeparator);

    Model m;
    model().load_from(m); // new id avoid same path name

    //select first plate
    get_partplate_list().select_plate(0);
    SimpleEvent event(EVT_GLCANVAS_PLATE_SELECT);
    p->on_plate_selected(event);

    p->load_auxiliary_files();
    wxGetApp().app_config->update_last_backup_dir(model().get_backup_path());

    // BBS set default view and zoom
    p->select_view_3D("3D");
    p->select_view("topfront");
    p->camera.requires_zoom_to_bed = true;
    if (previous_gcode)
        collapse_sidebar(false);

    up_to_date(true, false);
    up_to_date(true, true);
    return wxID_YES;
}



// BBS: FIXME, missing resotre logic
void Plater::load_project(wxString const& filename2,
    wxString const& originfile)
{
    auto filename = filename2;
    auto check = [&filename, this] (bool yes_or_no) {
        if (!yes_or_no && !wxGetApp().check_and_save_current_preset_changes(_L("Load project"),
                _L("Some presets are modified.")))
            return false;
        if (filename.empty()) {
            // Ask user for a project file name.
            wxGetApp().load_project(this, filename);
        }
        return !filename.empty();
    };

    // BSS: save project, force close
    int result;
    if ((result = close_with_confirm(check)) == wxID_CANCEL) {
        return;
    }

    //BBS: add only gcode mode
    bool previous_gcode = m_only_gcode;

    // BBS
    if (m_loading_project) {
        //some error cases happens
        //return directly
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(": current loading other project, return directly");
        return;
    }
    else
        m_loading_project = true;

    m_only_gcode = false;
    m_exported_file = false;
    get_notification_manager()->bbl_close_plateinfo_notification();
    get_notification_manager()->bbl_close_preview_only_notification();
    get_notification_manager()->bbl_close_3mf_warn_notification();

    auto path     = into_path(filename);

    auto strategy = LoadStrategy::LoadModel | LoadStrategy::LoadConfig;
    if (originfile == "<silence>") {
        strategy = strategy | LoadStrategy::Silence;
    } else if (originfile != "-") {
        strategy = strategy | LoadStrategy::Restore;
    }
    bool load_restore = strategy & LoadStrategy::Restore;

    // Take the Undo / Redo snapshot.
    reset();

    Plater::TakeSnapshot snapshot(this, "Load Project", UndoRedo::SnapshotType::ProjectSeparator);

    std::vector<fs::path> input_paths;
    input_paths.push_back(path);
    if (strategy & LoadStrategy::Restore)
        input_paths.push_back(into_u8(originfile));

    std::vector<size_t> res = load_files(input_paths, strategy);

    reset_project_dirty_initial_presets();
    update_project_dirty_from_presets();
    wxGetApp().preset_bundle->export_selections(*wxGetApp().app_config);

    // if res is empty no data has been loaded
    if (!res.empty() && (load_restore || !(strategy & LoadStrategy::Silence))) {
        p->set_project_filename(load_restore ? originfile : filename);
    } else {
        if (using_exported_file())
            p->set_project_filename(filename);
    }


    // BBS set default 3D view and direction after loading project
    //p->select_view_3D("3D");
    if (!m_exported_file) {
        p->select_view("topfront");
        p->camera.requires_zoom_to_plate = REQUIRES_ZOOM_TO_ALL_PLATE;
        wxGetApp().mainframe->select_tab(MainFrame::tp3DEditor);
    }

    if (previous_gcode)
        collapse_sidebar(false);

    wxGetApp().app_config->update_last_backup_dir(model().get_backup_path());
    if (load_restore && !originfile.empty()) {
        wxGetApp().app_config->update_skein_dir(into_path(originfile).parent_path().string());
        wxGetApp().app_config->update_config_dir(into_path(originfile).parent_path().string());
    }

    if (!load_restore)
        up_to_date(true, false);
    else
        p->dirty_state.update_from_undo_redo_stack(true);
    up_to_date(true, true);

    wxGetApp().params_panel()->switch_to_object_if_has_object_configs();

    m_loading_project = false;
}

// BBS: save logic
int Plater::save_project(bool saveAs)
{
    //if (up_to_date(false, false)) // should we always save
    //    return;
    auto filename = get_project_filename(".3mf");
    if (!saveAs && filename.IsEmpty())
        saveAs = true;
    if (saveAs)
        filename = p->get_export_file(FT_3MF);
    if (filename.empty())
        return wxID_NO;
    if (filename == "<cancel>")
        return wxID_CANCEL;

    //BBS export 3mf without gcode
    if (export_3mf(into_path(filename), SaveStrategy::SplitModel) < 0) {
        MessageDialog(this, _L("Failed to save the project.\nPlease check whether the folder exists online or if other programs open the project file."),
            _L("Save project"), wxOK | wxICON_WARNING).ShowModal();
        return wxID_CANCEL;
    }

    Slic3r::remove_backup(model(), false);

    p->set_project_filename(filename);

    up_to_date(true, false);
    up_to_date(true, true);

    wxGetApp().update_saved_preset_from_current_preset();
    p->dirty_state.reset_after_save();
    return wxID_YES;
}

//BBS import model by model id
void Plater::import_model_id(const std::string& download_info)
{
    std::string download_url = wxGetApp().get_download_model_url();
    std::string filename = wxGetApp().get_download_model_name();

    /* auto selection_data_arr = wxSplit(download_info, '|');

     if (selection_data_arr.size() == 2) {
         download_url = selection_data_arr[0].ToStdString();
         filename = selection_data_arr[1].ToStdString();
     }*/


    bool download_ok = false;
    /* save to a file */

    /* jump to 3D eidtor */
    wxGetApp().mainframe->select_tab((size_t)MainFrame::TabPosition::tp3DEditor);

    /* prepare progress dialog */
    bool cont = true;
    bool cont_dlg = true;
    bool cancel = false;
    wxString msg;
    wxString dlg_title = _L("Importing Model");

    int percent = 1;
    ProgressDialog dlg(dlg_title,
        wxString(' ', 100) + "\n\n\n\n",
        100,    // range
        this,   // parent
        wxPD_CAN_ABORT |
        wxPD_APP_MODAL |
        wxPD_AUTO_HIDE |
        wxPD_SMOOTH);

    boost::filesystem::path target_path;

    //reset params
    p->project.reset();

    /* prepare project and profile */
    boost::thread import_thread = Slic3r::create_thread([&percent, &cont, &cancel, &msg, &target_path, &download_ok, download_url, &filename] {

        NetworkAgent* m_agent = Slic3r::GUI::wxGetApp().getAgent();
        if (!m_agent) return;

        int res = 0;
        unsigned int http_code;
        std::string http_body;

        msg = _L("prepare 3mf file...");

        //gets the number of files with the same name
        std::vector<wxString>   vecFiles;
        bool                    is_already_exist = false;


        target_path = fs::path(wxGetApp().app_config->get("download_path"));

        try
        {
            vecFiles.clear();
            wxString extension = fs::path(filename).extension().c_str();
            auto name = filename.substr(0, filename.length() - extension.length() - 1);

            for (const auto& iter : boost::filesystem::directory_iterator(target_path))
            {
                if (boost::filesystem::is_directory(iter.path()))
                    continue;

                wxString sFile = iter.path().filename().string().c_str();
                if (strstr(sFile.c_str(), name.c_str()) != NULL) {
                    vecFiles.push_back(sFile);
                }

                if (sFile == filename) is_already_exist = true;
            }
        }
        catch (const std::exception& error)
        {
            //wxString sError = error.what();
        }

        //update filename
        if (is_already_exist && vecFiles.size() >= 1) {
            wxString extension = fs::path(filename).extension().c_str();
            wxString name = filename.substr(0, filename.length() - extension.length());
            filename = wxString::Format("%s(%d)%s", name, vecFiles.size() + 1, extension).ToStdString();
        }


        msg = _L("downloading project ...");

        //target_path = wxStandardPaths::Get().GetTempDir().utf8_str().data();


        //target_path = wxGetApp().get_local_models_path().c_str();
        boost::uuids::uuid uuid = boost::uuids::random_generator()();
        std::string unique = to_string(uuid).substr(0, 6);

        if (filename.empty()) {
            filename = "untitled.3mf";
        }

        //target_path /= (boost::format("%1%_%2%.3mf") % filename % unique).str();
        target_path /= filename.c_str();
        fs::path tmp_path = target_path;
        tmp_path += format(".%1%", ".download");

        auto url = download_url;
        auto http = Http::get(url);
        http.on_progress([&percent, &cont, &msg](Http::Progress progress, bool& cancel) {
            if (!cont) cancel = true;
            if (progress.dltotal != 0) {
                percent = progress.dlnow * 100 / progress.dltotal;
            }
            msg = wxString::Format(_L("Project downloaded %d%%"), percent);
            })
            .on_error([&msg, &cont](std::string body, std::string error, unsigned http_status) {
                (void)body;
                BOOST_LOG_TRIVIAL(error) << format("Error getting: `%1%`: HTTP %2%, %3%",
                    body,
                    http_status,
                    error);
                msg = wxString::Format("Download Failed! body=%s, error=%s, status=%d", body, error, http_status);
                cont = false;
                return;
                })
                .on_complete([&cont, &download_ok, tmp_path, target_path](std::string body, unsigned /* http_status */) {
                    fs::fstream file(tmp_path, std::ios::out | std::ios::binary | std::ios::trunc);
                    file.write(body.c_str(), body.size());
                    file.close();
                    fs::rename(tmp_path, target_path);
                    cont = false;
                    download_ok = true;
                    })
                    .perform_sync();

                    // for break while
                    cont = false;
        });

    while (cont && cont_dlg) {
        wxMilliSleep(50);
        cont_dlg = dlg.Update(percent, msg);
        if (!cont_dlg) {
            cont = cont_dlg;
            cancel = true;
        }

        if (download_ok)
            break;
    }

    if (import_thread.joinable())
        import_thread.join();

    dlg.Close();
    if (download_ok) {
        BOOST_LOG_TRIVIAL(trace) << "import_model_id: target_path = " << target_path.string();
        /* load project */
        this->load_project(encode_path(target_path.string().c_str()), "<silence>");

        /*BBS set project info after load project, project info is reset in load project */
        //p->project.project_model_id = model_id;
        //p->project.project_design_id = design_id;
        AppConfig* config = wxGetApp().app_config;
        if (config) {
            p->project.project_country_code = config->get_country_code();
        }

        // show save new project
        p->set_project_filename(wxString::FromUTF8(filename));
    }
    else {
        wxMessageBox(msg);
        return;
    }
}

//BBS download project by project id
void Plater::download_project(const wxString& project_id)
{
    return;
}

void Plater::request_model_download()
{
    wxCommandEvent* event = new wxCommandEvent(EVT_IMPORT_MODEL_ID);
    wxQueueEvent(this, event);
}

void Plater::request_download_project(std::string project_id)
{
    wxCommandEvent* event = new wxCommandEvent(EVT_DOWNLOAD_PROJECT);
    event->SetString(project_id);
    wxQueueEvent(this, event);
}

// BBS: save logic
bool Plater::up_to_date(bool saved, bool backup)
{
    if (saved) {
        Slic3r::clear_other_changes(backup);
        return p->up_to_date(saved, backup);
    }
    return p->model.objects.empty() || (p->up_to_date(saved, backup) &&
                                        !Slic3r::has_other_changes(backup));
}

void Plater::add_model(bool imperial_units/* = false*/,  std::string fname/* = ""*/)
{
    std::vector<fs::path> paths;

    if(fname.empty()){
        wxArrayString input_files;
        wxGetApp().import_model(this, input_files);
        if (input_files.empty())
            return;
        
        for (const auto &file : input_files)
            paths.emplace_back(into_path(file));
    }
    else{
        paths.emplace_back(fname);
    }

    std::string snapshot_label;
    assert(! paths.empty());
    if (paths.size() == 1) {
        snapshot_label = "Import Object";
        snapshot_label += ": ";
        snapshot_label += encode_path(paths.front().filename().string().c_str());
    } else {
        snapshot_label = "Import Objects";
        snapshot_label += ": ";
        snapshot_label += paths.front().filename().string().c_str();
        for (size_t i = 1; i < paths.size(); ++ i) {
            snapshot_label += ", ";
            snapshot_label += encode_path(paths[i].filename().string().c_str());
        }
    }

    Plater::TakeSnapshot snapshot(this, snapshot_label);

    // BBS: check file types
    auto loadfiles_type  = LoadFilesType::NoFile;
    auto amf_files_count = get_3mf_file_count(paths);

    if (paths.size() > 1 && amf_files_count < paths.size()) { loadfiles_type = LoadFilesType::Multiple3MFOther; }
    if (paths.size() > 1 && amf_files_count == paths.size()) { loadfiles_type = LoadFilesType::Multiple3MF; }
    if (paths.size() > 1 && amf_files_count == 0) { loadfiles_type = LoadFilesType::MultipleOther; }
    if (paths.size() == 1 && amf_files_count == 1) { loadfiles_type = LoadFilesType::Single3MF; };
    if (paths.size() == 1 && amf_files_count == 0) { loadfiles_type = LoadFilesType::SingleOther; };

    bool ask_multi = false;

    if (loadfiles_type == LoadFilesType::MultipleOther)
        ask_multi = true;

    auto strategy = LoadStrategy::LoadModel;
    if (imperial_units) strategy = strategy | LoadStrategy::ImperialUnits;
    if (!load_files(paths, strategy, ask_multi).empty()) {

        if (get_project_name() == _L("Untitled") && paths.size() > 0) {
            p->set_project_filename(wxString::FromUTF8(paths[0].string()));
        }

        wxGetApp().mainframe->update_title();
    }
}

void Plater::calib_pa(const Calib_Params& params) {
    
    const auto calib_pa_name = wxString::Format(L"Pressure Advance Test");
    new_project(false, false, calib_pa_name);
    wxGetApp().mainframe->select_tab(size_t(MainFrame::tp3DEditor));
    if (params.mode == CalibMode::Calib_PA_Line) {
        add_model(false, Slic3r::resources_dir() + "/calib/PressureAdvance/pressure_advance_test.stl");
    }
    else {
        add_model(false, Slic3r::resources_dir() + "/calib/PressureAdvance/tower_with_seam.stl");
        auto print_config = &wxGetApp().preset_bundle->prints.get_edited_preset().config;
        auto printer_config = &wxGetApp().preset_bundle->printers.get_edited_preset().config;
        auto filament_config = &wxGetApp().preset_bundle->filaments.get_edited_preset().config;
        filament_config->set_key_value("slow_down_layer_time", new ConfigOptionFloats{ 1.0f });
        print_config->set_key_value("default_jerk", new ConfigOptionFloat(1.0f));
        print_config->set_key_value("outer_wall_jerk", new ConfigOptionFloat(1.0f));
        print_config->set_key_value("inner_wall_jerk", new ConfigOptionFloat(1.0f));
        if(print_config->option<ConfigOptionEnum<PerimeterGeneratorType>>("wall_generator")->value == PerimeterGeneratorType::Arachne)
            print_config->set_key_value("wall_transition_angle", new ConfigOptionFloat(25));
        model().objects[0]->config.set_key_value("seam_position", new ConfigOptionEnum<SeamPosition>(spRear));

        changed_objects({ 0 });
        wxGetApp().get_tab(Preset::TYPE_PRINT)->update_dirty();
        wxGetApp().get_tab(Preset::TYPE_FILAMENT)->update_dirty();
        wxGetApp().get_tab(Preset::TYPE_PRINTER)->update_dirty();
        wxGetApp().get_tab(Preset::TYPE_PRINT)->reload_config();
        wxGetApp().get_tab(Preset::TYPE_FILAMENT)->reload_config();
        wxGetApp().get_tab(Preset::TYPE_PRINTER)->reload_config();

        auto new_height = std::ceil((params.end - params.start) / params.step) + 1;
        auto obj_bb = model().objects[0]->bounding_box();
        if (new_height < obj_bb.size().z()) {
            std::array<Vec3d, 4> plane_pts;
            plane_pts[0] = Vec3d(obj_bb.min(0), obj_bb.min(1), new_height);
            plane_pts[1] = Vec3d(obj_bb.min(0), obj_bb.max(1), new_height);
            plane_pts[2] = Vec3d(obj_bb.max(0), obj_bb.max(1), new_height);
            plane_pts[3] = Vec3d(obj_bb.max(0), obj_bb.min(1), new_height);
            cut(0, 0, plane_pts, ModelObjectCutAttribute::KeepLower);
        }

        // automatic selection of added objects
        // update printable state for new volumes on canvas3D
        wxGetApp().plater()->canvas3D()->update_instance_printable_state_for_objects({0});

        Selection& selection = p->view3D->get_canvas3d()->get_selection();
        selection.clear();
        selection.add_object(0, false);

        // BBS: update object list selection
        p->sidebar->obj_list()->update_selections();
        selection.notify_instance_update(-1, -1);
        if (p->view3D->get_canvas3d()->get_gizmos_manager().is_enabled())
            // this is required because the selected object changed and the flatten on face an sla support gizmos need to be updated accordingly
            p->view3D->get_canvas3d()->update_gizmos_on_off_state();
    }
    p->background_process.fff_print()->set_calib_params(params);


}

void Plater::calib_flowrate(int pass) {
    if (pass != 1 && pass != 2)
        return;
    const auto calib_name = wxString::Format(L"Flowrate Test - Pass%d", pass);
    new_project(false, false, calib_name);

    wxGetApp().mainframe->select_tab(size_t(MainFrame::tp3DEditor));
    
    if(pass == 1)
        add_model(false, (boost::filesystem::path(Slic3r::resources_dir()) / "calib" / "filament_flow" / "flowrate-test-pass1.3mf").string());
    else
        add_model(false, (boost::filesystem::path(Slic3r::resources_dir()) / "calib" / "filament_flow" / "flowrate-test-pass2.3mf").string());

    auto print_config = &wxGetApp().preset_bundle->prints.get_edited_preset().config;
    auto printerConfig = &wxGetApp().preset_bundle->printers.get_edited_preset().config;
    auto filament_config = &wxGetApp().preset_bundle->filaments.get_edited_preset().config;

    /// --- scale ---
    // model is created for a 0.4 nozzle, scale z with nozzle size.
    const ConfigOptionFloats* nozzle_diameter_config = printerConfig->option<ConfigOptionFloats>("nozzle_diameter");
    assert(nozzle_diameter_config->values.size() > 0);
    float nozzle_diameter = nozzle_diameter_config->values[0];
    float xyScale = nozzle_diameter / 0.6;
    //scale z to have 7 layers
    double first_layer_height = print_config->option<ConfigOptionFloat>("initial_layer_print_height")->value;
    double layer_height = nozzle_diameter / 2.0; // prefer 0.2 layer height for 0.4 nozzle
    first_layer_height = std::max(first_layer_height, layer_height);

    float zscale = (first_layer_height + 6 * layer_height) / 1.4;
    // only enlarge
    if (xyScale > 1.2) {
        for (auto _obj : model().objects)
            _obj->scale(xyScale, xyScale, zscale); 
    }
    else {
        for (auto _obj : model().objects)
            _obj->scale(1, 1, zscale);
    }

    Flow infill_flow = Flow(nozzle_diameter * 1.2f, layer_height, nozzle_diameter);
    double filament_max_volumetric_speed = filament_config->option<ConfigOptionFloats>("filament_max_volumetric_speed")->get_at(0);
    double max_infill_speed = filament_max_volumetric_speed / (infill_flow.mm3_per_mm() * (pass == 1 ? 1.2 : 1));
    double internal_solid_speed = std::floor(std::min(print_config->opt_float("internal_solid_infill_speed"), max_infill_speed));
    double top_surface_speed = std::floor(std::min(print_config->opt_float("top_surface_speed"), max_infill_speed));

    // adjust parameters
    for (auto _obj : model().objects) {
        _obj->ensure_on_bed();
        _obj->config.set_key_value("wall_loops", new ConfigOptionInt(3));
        _obj->config.set_key_value("only_one_wall_top", new ConfigOptionBool(true));
        _obj->config.set_key_value("sparse_infill_density", new ConfigOptionPercent(35));
        _obj->config.set_key_value("bottom_shell_layers", new ConfigOptionInt(1));
        _obj->config.set_key_value("top_shell_layers", new ConfigOptionInt(5));
        _obj->config.set_key_value("detect_thin_wall", new ConfigOptionBool(true));
        _obj->config.set_key_value("filter_out_gap_fill", new ConfigOptionFloat(0));
        _obj->config.set_key_value("sparse_infill_pattern", new ConfigOptionEnum<InfillPattern>(ipRectilinear));
        _obj->config.set_key_value("top_surface_line_width", new ConfigOptionFloat(nozzle_diameter * 1.2f));
        _obj->config.set_key_value("internal_solid_infill_line_width", new ConfigOptionFloat(nozzle_diameter * 1.2f));
        _obj->config.set_key_value("top_surface_pattern", new ConfigOptionEnum<InfillPattern>(ipMonotonic));
        _obj->config.set_key_value("top_solid_infill_flow_ratio", new ConfigOptionFloat(1.0f));
        _obj->config.set_key_value("top_surface_pattern", new ConfigOptionEnum<InfillPattern>(ipMonotonic));
        _obj->config.set_key_value("infill_direction", new ConfigOptionFloat(45));
        _obj->config.set_key_value("ironing_type", new ConfigOptionEnum<IroningType>(IroningType::NoIroning));
        _obj->config.set_key_value("internal_solid_infill_speed", new ConfigOptionFloat(internal_solid_speed));
        _obj->config.set_key_value("top_surface_speed", new ConfigOptionFloat(top_surface_speed));

        // extract flowrate from name, filename format: flowrate_xxx
        std::string obj_name = _obj->name;
        assert(obj_name.length() > 9);
        obj_name = obj_name.substr(9);
        if (obj_name[0] == 'm')
            obj_name[0] = '-';
        auto modifier = stof(obj_name);
        _obj->config.set_key_value("print_flow_ratio", new ConfigOptionFloat(1.0f + modifier/100.f));
    }

    print_config->set_key_value("layer_height", new ConfigOptionFloat(layer_height));
    print_config->set_key_value("initial_layer_print_height", new ConfigOptionFloat(first_layer_height));
    print_config->set_key_value("reduce_crossing_wall", new ConfigOptionBool(true));
    //filament_config->set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats{ 9. });

    wxGetApp().get_tab(Preset::TYPE_PRINT)->update_dirty();
    wxGetApp().get_tab(Preset::TYPE_FILAMENT)->update_dirty();
    wxGetApp().get_tab(Preset::TYPE_PRINTER)->update_dirty();
    wxGetApp().get_tab(Preset::TYPE_PRINT)->reload_config();
    wxGetApp().get_tab(Preset::TYPE_FILAMENT)->reload_config();
    wxGetApp().get_tab(Preset::TYPE_PRINTER)->reload_config();

}

void Plater::calib_temp(const Calib_Params& params) {
    const auto calib_temp_name = wxString::Format(L"Nozzle temperature test");
    new_project(false, false, calib_temp_name);
    wxGetApp().mainframe->select_tab(size_t(MainFrame::tp3DEditor));
    if (params.mode != CalibMode::Calib_Temp_Tower)
        return;
    
    add_model(false, Slic3r::resources_dir() + "/calib/temperature_tower/temperature_tower.stl");
    auto filament_config = &wxGetApp().preset_bundle->filaments.get_edited_preset().config;
    auto start_temp = lround(params.start);
    filament_config->set_key_value("nozzle_temperature_initial_layer", new ConfigOptionInts(1,(int)start_temp));
    filament_config->set_key_value("nozzle_temperature", new ConfigOptionInts(1,(int)start_temp));
    model().objects[0]->config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btOuterOnly));
    model().objects[0]->config.set_key_value("brim_width", new ConfigOptionFloat(5.0));
    model().objects[0]->config.set_key_value("brim_object_gap", new ConfigOptionFloat(0.0));

    changed_objects({ 0 });
    wxGetApp().get_tab(Preset::TYPE_PRINT)->update_dirty();
    wxGetApp().get_tab(Preset::TYPE_FILAMENT)->update_dirty();
    wxGetApp().get_tab(Preset::TYPE_PRINT)->reload_config();
    wxGetApp().get_tab(Preset::TYPE_FILAMENT)->reload_config();

    // cut upper
    auto obj_bb = model().objects[0]->bounding_box();
    auto block_count = lround((350 - params.end) / 5 + 1);
    if(block_count > 0){
        // add EPSILON offset to avoid cutting at the exact location where the flat surface is
        auto new_height = block_count * 10.0 + EPSILON;
        if (new_height < obj_bb.size().z()) {
            std::array<Vec3d, 4> plane_pts;
            plane_pts[0] = Vec3d(obj_bb.min(0), obj_bb.min(1), new_height);
            plane_pts[1] = Vec3d(obj_bb.min(0), obj_bb.max(1), new_height);
            plane_pts[2] = Vec3d(obj_bb.max(0), obj_bb.max(1), new_height);
            plane_pts[3] = Vec3d(obj_bb.max(0), obj_bb.min(1), new_height);
            cut(0, 0, plane_pts, ModelObjectCutAttribute::KeepLower);
        }
    }
    
    // cut bottom
    obj_bb = model().objects[0]->bounding_box();
    block_count = lround((350 - params.start) / 5);
    if(block_count > 0){
        auto new_height = block_count * 10.0 + EPSILON;
        if (new_height < obj_bb.size().z()) {
            std::array<Vec3d, 4> plane_pts;
            plane_pts[0] = Vec3d(obj_bb.min(0), obj_bb.min(1), new_height);
            plane_pts[1] = Vec3d(obj_bb.min(0), obj_bb.max(1), new_height);
            plane_pts[2] = Vec3d(obj_bb.max(0), obj_bb.max(1), new_height);
            plane_pts[3] = Vec3d(obj_bb.max(0), obj_bb.min(1), new_height);
            cut(0, 0, plane_pts, ModelObjectCutAttribute::KeepUpper);
        }
    }
    
    p->background_process.fff_print()->set_calib_params(params);
}

void Plater::calib_max_vol_speed(const Calib_Params& params)
{
    const auto calib_vol_speed_name = wxString::Format(L"Max volumetric speed test");
    new_project(false, false, calib_vol_speed_name);
    wxGetApp().mainframe->select_tab(size_t(MainFrame::tp3DEditor));
    if (params.mode != CalibMode::Calib_Vol_speed_Tower)
        return;

    add_model(false, Slic3r::resources_dir() + "/calib/volumetric_speed/SpeedTestStructure.step");

    auto print_config = &wxGetApp().preset_bundle->prints.get_edited_preset().config;
    auto filament_config = &wxGetApp().preset_bundle->filaments.get_edited_preset().config;
    auto printer_config = &wxGetApp().preset_bundle->printers.get_edited_preset().config;
    auto obj = model().objects[0];

    auto bed_shape = printer_config->option<ConfigOptionPoints>("printable_area")->values;
    BoundingBoxf bed_ext = get_extents(bed_shape);
    auto scale_obj = (bed_ext.size().x() - 10) / obj->bounding_box().size().x();
    if (scale_obj < 1.0)
        obj->scale(scale_obj, 1, 1);

    const ConfigOptionFloats* nozzle_diameter_config = printer_config->option<ConfigOptionFloats>("nozzle_diameter");
    assert(nozzle_diameter_config->values.size() > 0);
    double nozzle_diameter = nozzle_diameter_config->values[0];
    double line_width = nozzle_diameter * 1.75;
    double layer_height = nozzle_diameter * 0.8;

    auto max_lh = printer_config->option<ConfigOptionFloats>("max_layer_height");
    if (max_lh->values[0] < layer_height)
        max_lh->values[0] = { layer_height };

    filament_config->set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats { 200 });
    filament_config->set_key_value("slow_down_layer_time", new ConfigOptionFloats{0.0});
    
    print_config->set_key_value("enable_overhang_speed", new ConfigOptionBool { false });
    print_config->set_key_value("timelapse_type", new ConfigOptionEnum<TimelapseType>(tlTraditional));
    print_config->set_key_value("wall_loops", new ConfigOptionInt(1));
    print_config->set_key_value("top_shell_layers", new ConfigOptionInt(0));
    print_config->set_key_value("bottom_shell_layers", new ConfigOptionInt(1));
    print_config->set_key_value("sparse_infill_density", new ConfigOptionPercent(0));
    print_config->set_key_value("spiral_mode", new ConfigOptionBool(true));
    print_config->set_key_value("outer_wall_line_width", new ConfigOptionFloat(line_width));
    print_config->set_key_value("initial_layer_print_height", new ConfigOptionFloat(layer_height));
    print_config->set_key_value("layer_height", new ConfigOptionFloat(layer_height));
    obj->config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btOuterAndInner));
    obj->config.set_key_value("brim_width", new ConfigOptionFloat(3.0));
    obj->config.set_key_value("brim_object_gap", new ConfigOptionFloat(0.0));

    changed_objects({ 0 });
    wxGetApp().get_tab(Preset::TYPE_PRINT)->update_dirty();
    wxGetApp().get_tab(Preset::TYPE_FILAMENT)->update_dirty();
    wxGetApp().get_tab(Preset::TYPE_PRINTER)->update_dirty();
    wxGetApp().get_tab(Preset::TYPE_PRINT)->reload_config();
    wxGetApp().get_tab(Preset::TYPE_FILAMENT)->reload_config();
    wxGetApp().get_tab(Preset::TYPE_PRINTER)->reload_config();

    //  cut upper
    auto obj_bb = obj->bounding_box();
    auto height = (params.end - params.start + 1) / params.step;
    if (height < obj_bb.size().z()) {
        std::array<Vec3d, 4> plane_pts;
        plane_pts[0] = Vec3d(obj_bb.min(0), obj_bb.min(1), height);
        plane_pts[1] = Vec3d(obj_bb.min(0), obj_bb.max(1), height);
        plane_pts[2] = Vec3d(obj_bb.max(0), obj_bb.max(1), height);
        plane_pts[3] = Vec3d(obj_bb.max(0), obj_bb.min(1), height);
        cut(0, 0, plane_pts, ModelObjectCutAttribute::KeepLower);
    }

    auto new_params = params;
    auto mm3_per_mm = Flow(line_width, layer_height, nozzle_diameter).mm3_per_mm() *
                      filament_config->option<ConfigOptionFloats>("filament_flow_ratio")->get_at(0);
    new_params.end = params.end / mm3_per_mm;
    new_params.start = params.start / mm3_per_mm;
    new_params.step = params.step / mm3_per_mm;


    p->background_process.fff_print()->set_calib_params(new_params);
}
void Plater::calib_VFA(const Calib_Params& params)
{
    const auto calib_vfa_name = wxString::Format(L"VFA test");
    new_project(false, false, calib_vfa_name);
    wxGetApp().mainframe->select_tab(size_t(MainFrame::tp3DEditor));
    if (params.mode != CalibMode::Calib_VFA_Tower)
        return;

    add_model(false, Slic3r::resources_dir() + "/calib/vfa/VFA.stl");
    auto print_config = &wxGetApp().preset_bundle->prints.get_edited_preset().config;
    auto filament_config = &wxGetApp().preset_bundle->filaments.get_edited_preset().config;
    filament_config->set_key_value("slow_down_layer_time", new ConfigOptionFloats { 0.0 });
    filament_config->set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats { 200 });
    print_config->set_key_value("enable_overhang_speed", new ConfigOptionBool { false });
    print_config->set_key_value("timelapse_type", new ConfigOptionEnum<TimelapseType>(tlTraditional));
    print_config->set_key_value("wall_loops", new ConfigOptionInt(1));
    print_config->set_key_value("top_shell_layers", new ConfigOptionInt(0));
    print_config->set_key_value("bottom_shell_layers", new ConfigOptionInt(1));
    print_config->set_key_value("sparse_infill_density", new ConfigOptionPercent(0));
    print_config->set_key_value("spiral_mode", new ConfigOptionBool(true));
    model().objects[0]->config.set_key_value("brim_type", new ConfigOptionEnum<BrimType>(btOuterOnly));
    model().objects[0]->config.set_key_value("brim_width", new ConfigOptionFloat(3.0));
    model().objects[0]->config.set_key_value("brim_object_gap", new ConfigOptionFloat(0.0));

    changed_objects({ 0 });
    wxGetApp().get_tab(Preset::TYPE_PRINT)->update_dirty();
    wxGetApp().get_tab(Preset::TYPE_FILAMENT)->update_dirty();
    wxGetApp().get_tab(Preset::TYPE_PRINT)->update_ui_from_settings();
    wxGetApp().get_tab(Preset::TYPE_FILAMENT)->update_ui_from_settings();

    // cut upper
    auto obj_bb = model().objects[0]->bounding_box();
    auto height = 5 * ((params.end - params.start) / params.step + 1);
    if (height < obj_bb.size().z()) {
        std::array<Vec3d, 4> plane_pts;
        plane_pts[0] = Vec3d(obj_bb.min(0), obj_bb.min(1), height);
        plane_pts[1] = Vec3d(obj_bb.min(0), obj_bb.max(1), height);
        plane_pts[2] = Vec3d(obj_bb.max(0), obj_bb.max(1), height);
        plane_pts[3] = Vec3d(obj_bb.max(0), obj_bb.min(1), height);
        cut(0, 0, plane_pts, ModelObjectCutAttribute::KeepLower);
    }

    p->background_process.fff_print()->set_calib_params(params);
}
void Plater::import_sl1_archive()
{
    if (!p->m_ui_jobs.is_any_running())
        p->m_ui_jobs.import_sla_arch();
}

void Plater::extract_config_from_project()
{
    wxString input_file;
    wxGetApp().load_project(this, input_file);

    if (! input_file.empty())
        load_files({ into_path(input_file) }, LoadStrategy::LoadConfig);
}

void Plater::load_gcode()
{
    // Ask user for a gcode file name.
    wxString input_file;
    wxGetApp().load_gcode(this, input_file);
    // And finally load the gcode file.
    load_gcode(input_file);
}

//BBS: remove GCodeViewer as seperate APP logic
void Plater::load_gcode(const wxString& filename)
{
    if (! is_gcode_file(into_u8(filename))
        || (m_last_loaded_gcode == filename && m_only_gcode)
        )
        return;

    m_last_loaded_gcode = filename;

    // BSS: create a new project when load_gcode, force close previous one
    if (new_project(false, true) != wxID_YES)
        return;

    m_only_gcode = true;

    // cleanup view before to start loading/processing
    //BBS: update gcode to current partplate's
    GCodeProcessorResult* current_result = p->partplate_list.get_current_slice_result();
    Print& current_print = p->partplate_list.get_current_fff_print();
    //BBS:already reset in new_project
    //current_result->reset();
    //p->gcode_result.reset();
    //reset_gcode_toolpaths();
    p->preview->reload_print(false, m_only_gcode);
    wxGetApp().mainframe->select_tab(MainFrame::tpPreview);
    p->set_current_panel(p->preview, true);
    p->get_current_canvas3D()->render();
    //p->notification_manager->bbl_show_plateinfo_notification(into_u8(_L("Preview only mode for gcode file.")));

    current_print.apply(this->model(), wxGetApp().preset_bundle->full_config());

    wxBusyCursor wait;

    // process gcode
    GCodeProcessor processor;
    try
    {
        processor.process_file(filename.ToUTF8().data());
    }
    catch (const std::exception& ex)
    {
        show_error(this, ex.what());
        return;
    }
    *current_result = std::move(processor.extract_result());
    //current_result->filename = filename;
    current_print.set_gcode_file_ready();

    // show results
    p->preview->reload_print(false, m_only_gcode);
    //BBS: zoom to bed 0 for gcode preview
    //p->preview->get_canvas3d()->zoom_to_gcode();
    p->preview->get_canvas3d()->zoom_to_plate(0);

    if (p->preview->get_canvas3d()->get_gcode_layers_zs().empty()) {
        MessageDialog(this, _L("The selected file") + ":\n" + filename + "\n" + _L("does not contain valid gcode."),
            wxString(GCODEVIEWER_APP_NAME) + " - " + _L("Error occurs while loading G-code file"), wxCLOSE | wxICON_WARNING | wxCENTRE).ShowModal();
        set_project_filename(DEFAULT_PROJECT_NAME);
    }
    else
        set_project_filename(filename);
}

void Plater::reload_gcode_from_disk()
{
    wxString filename(m_last_loaded_gcode);
    m_last_loaded_gcode.clear();
    load_gcode(filename);
}

void Plater::refresh_print()
{
    p->preview->refresh_print();
}

// BBS
wxString Plater::get_project_name()
{
    return p->get_project_name();
}

void Plater::update_all_plate_thumbnails(bool force_update)
{
    for (int i = 0; i < get_partplate_list().get_plate_count(); i++) {
        PartPlate* plate = get_partplate_list().get_plate(i);
        ThumbnailsParams thumbnail_params = { {}, false, true, true, true, i};
        if (force_update || !plate->thumbnail_data.is_valid()) {
            get_view3D_canvas3D()->render_thumbnail(plate->thumbnail_data, plate->plate_thumbnail_width, plate->plate_thumbnail_height, thumbnail_params, Camera::EType::Ortho);
        }
    }
}

//invalid all plate's thumbnails
void Plater::invalid_all_plate_thumbnails()
{
    if (using_exported_file() || skip_thumbnail_invalid)
        return;
    BOOST_LOG_TRIVIAL(info) << "thumb: invalid all";
    for (int i = 0; i < get_partplate_list().get_plate_count(); i++) {
        PartPlate* plate = get_partplate_list().get_plate(i);
        plate->thumbnail_data.reset();
    }
}

void Plater::force_update_all_plate_thumbnails()
{
    if (using_exported_file() || skip_thumbnail_invalid) {
    }
    else {
        invalid_all_plate_thumbnails();
        update_all_plate_thumbnails(true);
    }
    get_preview_canvas3D()->update_plate_thumbnails();
}

// BBS: backup
std::vector<size_t> Plater::load_files(const std::vector<fs::path>& input_files, LoadStrategy strategy, bool ask_multi) {
    //BBS: wish to reset state when load a new file
    p->m_slice_all_only_has_gcode = false;
    //BBS: wish to reset all plates stats item selected state when load a new file
    p->preview->get_canvas3d()->reset_select_plate_toolbar_selection();
    return p->load_files(input_files, strategy, ask_multi);
}

// To be called when providing a list of files to the GUI slic3r on command line.
std::vector<size_t> Plater::load_files(const std::vector<std::string>& input_files, LoadStrategy strategy,  bool ask_multi)
{
    std::vector<fs::path> paths;
    paths.reserve(input_files.size());
    for (const std::string& path : input_files)
        paths.emplace_back(path);
    return p->load_files(paths, strategy, ask_multi);
}

class RadioBox;
class RadioSelector
{
public:
    int       m_select_id;
    int       m_groupid;
    RadioBox *m_radiobox;
};
WX_DECLARE_LIST(RadioSelector, RadioSelectorList);

#define PROJECT_DROP_DIALOG_SELECT_PLANE_SIZE wxSize(FromDIP(350), FromDIP(120))
#define PROJECT_DROP_DIALOG_BUTTON_SIZE wxSize(FromDIP(60), FromDIP(24))

class ProjectDropDialog : public DPIDialog
{
private:
    wxColour          m_def_color = wxColour(255, 255, 255);
    RadioSelectorList m_radio_group;
    int               m_action{1};
    bool              m_show_again;

public:
    ProjectDropDialog(const std::string &filename);

    wxPanel *     m_top_line;
    wxStaticText *m_fname_title;
    wxStaticText *m_fname_f;
    wxStaticText *m_fname_s;
    StaticBox * m_panel_select;
    Button *    m_confirm;
    Button *    m_cancel;


    void      select_radio(int index);
    void      on_select_radio(wxMouseEvent &event);
    void      on_select_ok(wxMouseEvent &event);
    void      on_select_cancel(wxMouseEvent &event);

    int       get_select_radio(int groupid);
    int       get_action() const { return m_action; }
    void      set_action(int index) { m_action = index; }

    wxBoxSizer *create_item_checkbox(wxString title, wxWindow *parent, wxString tooltip, std::string param);
    wxBoxSizer *create_item_radiobox(wxString title, wxWindow *parent, int select_id, int groupid);

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;
};

ProjectDropDialog::ProjectDropDialog(const std::string &filename)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe),
                wxID_ANY,
                from_u8((boost::format(_utf8(L("Drop project file")))).str()),
                wxDefaultPosition,
                wxDefaultSize,
                wxCAPTION | wxCLOSE_BOX)
{
    // def setting
    SetBackgroundColour(m_def_color);

    // icon
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);

    m_top_line = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_top_line->SetBackgroundColour(wxColour(166, 169, 170));

    m_sizer_main->Add(m_top_line, 0, wxEXPAND, 0);

    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, 20);

    wxBoxSizer *m_sizer_name = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *m_sizer_fline = new wxBoxSizer(wxHORIZONTAL);

    m_fname_title = new wxStaticText(this, wxID_ANY, _L("Please select an action"), wxDefaultPosition, wxDefaultSize, 0);
    m_fname_title->Wrap(-1);
    m_fname_title->SetFont(::Label::Body_13);
    m_fname_title->SetForegroundColour(wxColour(107, 107, 107));
    m_fname_title->SetBackgroundColour(wxColour(255, 255, 255));

    m_sizer_fline->Add(m_fname_title, 0, wxALL, 0);
    m_sizer_fline->Add(0, 0, 0, wxEXPAND | wxLEFT, 5);

    m_fname_f = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
    m_fname_f->SetFont(::Label::Head_13);
    m_fname_f->Wrap(-1);
    m_fname_f->SetForegroundColour(wxColour(38, 46, 48));

    m_sizer_fline->Add(m_fname_f, 1, wxALL, 0);

    m_sizer_name->Add(m_sizer_fline, 1, wxEXPAND, 0);

    m_fname_s = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
    m_fname_s->SetFont(::Label::Head_13);
    m_fname_s->Wrap(-1);
    m_fname_s->SetForegroundColour(wxColour(38, 46, 48));

    m_sizer_name->Add(m_fname_s, 1, wxALL, 0);

    m_sizer_main->Add(m_sizer_name, 1, wxEXPAND | wxLEFT | wxRIGHT, 40);

    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, 5);

    m_panel_select = new StaticBox(this, wxID_ANY, wxDefaultPosition, PROJECT_DROP_DIALOG_SELECT_PLANE_SIZE);
    StateColor box_colour(std::pair<wxColour, int>(wxColour("#F8F8F8"), StateColor::Normal));
    StateColor box_border_colour(std::pair<wxColour, int>(wxColour(*wxWHITE), StateColor::Normal));

    m_panel_select->SetBackgroundColor(box_colour);
    m_panel_select->SetBorderColor(box_border_colour);
    m_panel_select->SetCornerRadius(5);

    wxBoxSizer *m_sizer_select_h = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer *m_sizer_select_v = new wxBoxSizer(wxVERTICAL);


    auto select_f = create_item_radiobox(_L("Open as project"), m_panel_select, 1, 0);
    auto select_s = create_item_radiobox(_L("Import geometry only"), m_panel_select, 2, 0);
    //auto select_t = create_item_radiobox(_L("Import presets only"), m_panel_select,3, 0);

    m_sizer_select_v->Add(select_f, 0, wxEXPAND, 5);
    m_sizer_select_v->Add(select_s, 0, wxEXPAND, 5);
    //m_sizer_select_v->Add(select_t, 0, wxEXPAND, 5);
    select_radio(2);

    m_sizer_select_h->Add(m_sizer_select_v, 0, wxALIGN_CENTER | wxLEFT, 22);

    m_panel_select->SetSizer(m_sizer_select_h);
    m_panel_select->Layout();
    m_sizer_main->Add(m_panel_select, 0, wxEXPAND | wxLEFT | wxRIGHT, 40);

    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, 10);

    wxBoxSizer *m_sizer_bottom = new wxBoxSizer(wxHORIZONTAL);
    // hide the "Don't show again" checkbox
    //wxBoxSizer *m_sizer_left = new wxBoxSizer(wxHORIZONTAL);

    //auto dont_show_again = create_item_checkbox(_L("Don't show again"), this, _L("Don't show again"), "show_drop_project_dialog");
    //m_sizer_left->Add(dont_show_again, 0, wxALL, 5);

    //m_sizer_bottom->Add(m_sizer_left, 0, wxEXPAND, 5);

    m_sizer_bottom->Add(0, 0, 1, wxEXPAND, 5);

    wxBoxSizer *m_sizer_right  = new wxBoxSizer(wxHORIZONTAL);

    m_confirm = new Button(this, _L("OK"));
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed), std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal));

    m_confirm->SetBackgroundColor(btn_bg_green);
    m_confirm->SetBorderColor(wxColour(0, 150, 136));
    m_confirm->SetTextColor(wxColour("#FFFFFE"));
    m_confirm->SetSize(PROJECT_DROP_DIALOG_BUTTON_SIZE);
    m_confirm->SetMinSize(PROJECT_DROP_DIALOG_BUTTON_SIZE);
    m_confirm->SetCornerRadius(FromDIP(12));
    m_confirm->Bind(wxEVT_LEFT_DOWN, &ProjectDropDialog::on_select_ok, this);
    m_sizer_right->Add(m_confirm, 0, wxALL, 5);

    m_cancel = new Button(this, _L("Cancel"));
    m_cancel->SetTextColor(wxColour(107, 107, 107));
    m_cancel->SetSize(PROJECT_DROP_DIALOG_BUTTON_SIZE);
    m_cancel->SetMinSize(PROJECT_DROP_DIALOG_BUTTON_SIZE);
    m_cancel->SetCornerRadius(FromDIP(12));
    m_cancel->Bind(wxEVT_LEFT_DOWN, &ProjectDropDialog::on_select_cancel, this);
    m_sizer_right->Add(m_cancel, 0, wxALL, 5);

    m_sizer_bottom->Add( m_sizer_right, 0, wxEXPAND, 5 );
    m_sizer_main->Add(m_sizer_bottom, 0, wxEXPAND | wxLEFT | wxRIGHT, 40);
    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, 20);

    SetSizer(m_sizer_main);
    Layout();
    Fit();
    Centre(wxBOTH);


    auto limit_width   = m_fname_f->GetSize().GetWidth() - 2;
    auto current_width = 0;
    auto cut_index     = 0;
    auto fstring       = wxString("");
    auto bstring       = wxString("");

    //auto file_name = from_u8(filename.c_str());
    auto file_name = wxString(filename);
    for (int x = 0; x < file_name.length(); x++) {
        current_width += m_fname_s->GetTextExtent(file_name[x]).GetWidth();
        cut_index = x;

        if (current_width > limit_width) {
            bstring += file_name[x];
        } else {
            fstring += file_name[x];
        }
    }

    m_fname_f->SetLabel(fstring);
    m_fname_s->SetLabel(bstring);

    wxGetApp().UpdateDlgDarkUI(this);
}

wxBoxSizer *ProjectDropDialog ::create_item_radiobox(wxString title, wxWindow *parent, int select_id, int groupid)
{
    wxBoxSizer *sizer = new wxBoxSizer(wxHORIZONTAL);
    auto radiobox =  new RadioBox(parent);

    radiobox->SetBackgroundColour(wxColour(248,248,248));
    sizer->Add(radiobox, 0, wxALL, 5);
    sizer->Add(0, 0, 0, wxEXPAND | wxLEFT, 5);
    auto text = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, 0);
    text->Wrap(-1);
    text->SetForegroundColour(wxColour(107, 107, 107));
    text->SetBackgroundColour(wxColour(248,248,248));
    sizer->Add(text, 0, wxALL, 5);

    radiobox->Bind(wxEVT_LEFT_DOWN, &ProjectDropDialog::on_select_radio, this);
    text->Bind(wxEVT_LEFT_DOWN, [this, radiobox](auto &e) {
        e.SetId(radiobox->GetId());
        on_select_radio(e);
    });

    RadioSelector *rs = new RadioSelector;
    rs->m_groupid     = groupid;
    rs->m_radiobox    = radiobox;
    rs->m_select_id   = select_id;
    m_radio_group.Append(rs);

    return sizer;
}
wxBoxSizer *ProjectDropDialog::create_item_checkbox(wxString title, wxWindow *parent, wxString tooltip, std::string param)
{
    wxBoxSizer *m_sizer_checkbox = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_checkbox->Add(0, 0, 0, wxEXPAND | wxLEFT, 5);

    auto checkbox = new ::CheckBox(parent);
    m_sizer_checkbox->Add(checkbox, 0, wxALIGN_CENTER, 0);
    m_sizer_checkbox->Add(0, 0, 0, wxEXPAND | wxLEFT, 8);

    auto checkbox_title = new wxStaticText(parent, wxID_ANY, title, wxDefaultPosition, wxSize(-1, -1), 0);
    checkbox_title->SetForegroundColour(wxColour(144,144,144));
    checkbox_title->SetFont(::Label::Body_13);
    checkbox_title->Wrap(-1);
    m_sizer_checkbox->Add(checkbox_title, 0, wxALIGN_CENTER | wxALL, 3);

     m_show_again = wxGetApp().app_config->get(param) == "true" ? true : false;
    checkbox->SetValue(m_show_again);

    checkbox->Bind(wxEVT_TOGGLEBUTTON, [this, checkbox, param](wxCommandEvent &e) {
        m_show_again = m_show_again ? false : true;
        e.Skip();
    });

    return m_sizer_checkbox;
}

void ProjectDropDialog::select_radio(int index)
{
    m_action                         = index;
    RadioSelectorList::Node *node    = m_radio_group.GetFirst();
    auto                     groupid = 0;

    while (node) {
        RadioSelector *rs = node->GetData();
        if (rs->m_select_id == index) groupid = rs->m_groupid;
        node = node->GetNext();
    }

    node = m_radio_group.GetFirst();
    while (node) {
        RadioSelector *rs = node->GetData();
        if (rs->m_groupid == groupid && rs->m_select_id == index) rs->m_radiobox->SetValue(true);
        if (rs->m_groupid == groupid && rs->m_select_id != index) rs->m_radiobox->SetValue(false);
        node = node->GetNext();
    }
}

int ProjectDropDialog::get_select_radio(int groupid)
{
    RadioSelectorList::Node *node = m_radio_group.GetFirst();
    while (node) {
        RadioSelector *rs = node->GetData();
        if (rs->m_groupid == groupid && rs->m_radiobox->GetValue()) { return rs->m_select_id; }
        node = node->GetNext();
    }

    return 0;
}
void ProjectDropDialog::on_select_radio(wxMouseEvent &event)
{
    RadioSelectorList::Node *node    = m_radio_group.GetFirst();
    auto                     groupid = 0;

    while (node) {
        RadioSelector *rs = node->GetData();
        if (rs->m_radiobox->GetId() == event.GetId()) groupid = rs->m_groupid;
        node = node->GetNext();
    }

    node = m_radio_group.GetFirst();
    while (node) {
        RadioSelector *rs = node->GetData();
        if (rs->m_groupid == groupid && rs->m_radiobox->GetId() == event.GetId()) {
            set_action(rs->m_select_id);
            rs->m_radiobox->SetValue(true);
        }


        if (rs->m_groupid == groupid && rs->m_radiobox->GetId() != event.GetId()) rs->m_radiobox->SetValue(false);
        node = node->GetNext();
    }
}

void ProjectDropDialog::on_select_ok(wxMouseEvent &event)
{
    wxGetApp().app_config->set_bool("show_drop_project_dialog", m_show_again);
    EndModal(wxID_OK);
}

void ProjectDropDialog::on_select_cancel(wxMouseEvent &event)
{
    EndModal(wxID_CANCEL);
}

void ProjectDropDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    m_confirm->SetMinSize(PROJECT_DROP_DIALOG_BUTTON_SIZE);
    m_cancel->SetMinSize(PROJECT_DROP_DIALOG_BUTTON_SIZE);
    Fit();
    Refresh();
}

//BBS: remove GCodeViewer as seperate APP logic
bool Plater::load_files(const wxArrayString& filenames)
{
    const std::regex pattern_drop(".*[.](stp|step|stl|obj|amf|3mf|svg)", std::regex::icase);
    const std::regex pattern_gcode_drop(".*[.](gcode|g)", std::regex::icase);

    std::vector<fs::path> normal_paths;
    std::vector<fs::path> gcode_paths;

    for (const auto& filename : filenames) {
        fs::path path(into_path(filename));
        if (std::regex_match(path.string(), pattern_drop))
            normal_paths.push_back(std::move(path));
        else if (std::regex_match(path.string(), pattern_gcode_drop))
            gcode_paths.push_back(std::move(path));
        else
            continue;
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": normal_paths %1%, gcode_paths %2%")%normal_paths.size() %gcode_paths.size();
    if (normal_paths.empty() && gcode_paths.empty()) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": can not find valid path, return directly");
        // Likely no supported files
        return false;
    }
    else if (normal_paths.empty()){
        //only gcode files
        if (gcode_paths.size() > 1) {
            show_info(this, _L("Only one G-code file can be opened at the same time."), _L("G-code loading"));
            return false;
        }
        load_gcode(from_path(gcode_paths.front()));
        return true;
    }

    if (!gcode_paths.empty()) {
        show_info(this, _L("G-code files can not be loaded with models together!"), _L("G-code loading"));
        return false;
    }

    //// searches for project files
    //for (std::vector<fs::path>::const_reverse_iterator it = normal_paths.rbegin(); it != normal_paths.rend(); ++it) {
    //    std::string filename = (*it).filename().string();
    //    ////BBS: only 3mf will be treated as project file
    //    if (open_3mf_file((*it)))
    //        return true;
    //}

    //// other files
    std::string snapshot_label;
    assert(!normal_paths.empty());
    if (normal_paths.size() == 1) {
        snapshot_label = "Load File";
        snapshot_label += ": ";
        snapshot_label += encode_path(normal_paths.front().filename().string().c_str());
    } else {
        snapshot_label = "Load Files";
        snapshot_label += ": ";
        snapshot_label += encode_path(normal_paths.front().filename().string().c_str());
        for (size_t i = 1; i < normal_paths.size(); ++i) {
            snapshot_label += ", ";
            snapshot_label += encode_path(normal_paths[i].filename().string().c_str());
        }
    }

    //Plater::TakeSnapshot snapshot(this, snapshot_label);
    //load_files(normal_paths, LoadStrategy::LoadModel);

    // BBS: check file types
    std::sort(normal_paths.begin(), normal_paths.end(), [](fs::path obj1, fs::path obj2) { return obj1.filename().string() < obj2.filename().string(); });

    auto loadfiles_type  = LoadFilesType::NoFile;
    auto amf_files_count = get_3mf_file_count(normal_paths);

    if (normal_paths.size() > 1 && amf_files_count < normal_paths.size()) { loadfiles_type = LoadFilesType::Multiple3MFOther; }
    if (normal_paths.size() > 1 && amf_files_count == normal_paths.size()) { loadfiles_type = LoadFilesType::Multiple3MF; }
    if (normal_paths.size() > 1 && amf_files_count == 0) { loadfiles_type = LoadFilesType::MultipleOther; }
    if (normal_paths.size() == 1 && amf_files_count == 1) { loadfiles_type = LoadFilesType::Single3MF; };
    if (normal_paths.size() == 1 && amf_files_count == 0) { loadfiles_type = LoadFilesType::SingleOther; };

    auto first_file = std::vector<fs::path>{};
    auto tmf_file   = std::vector<fs::path>{};
    auto other_file = std::vector<fs::path>{};
    auto res        = true;

    if (this->m_only_gcode || this->m_exported_file) {
        if ((loadfiles_type == LoadFilesType::SingleOther)
            || (loadfiles_type == LoadFilesType::MultipleOther)) {
            show_info(this, _L("Can not add models when in preview mode!"), _L("Add Models"));
            return false;
        }
    }

    switch (loadfiles_type) {
    case LoadFilesType::Single3MF:
        open_3mf_file(normal_paths[0]);
        break;

    case LoadFilesType::SingleOther: {
        Plater::TakeSnapshot snapshot(this, snapshot_label);
        if (load_files(normal_paths, LoadStrategy::LoadModel, false).empty()) { res = false; }
        break;
    }
    case LoadFilesType::Multiple3MF:
        first_file = std::vector<fs::path>{normal_paths[0]};
        for (auto i = 0; i < normal_paths.size(); i++) {
            if (i > 0) { other_file.push_back(normal_paths[i]); }
        };

        open_3mf_file(first_file[0]);
        if (load_files(other_file, LoadStrategy::LoadModel).empty()) {  res = false;  }
        break;

    case LoadFilesType::MultipleOther: {
        Plater::TakeSnapshot snapshot(this, snapshot_label);
        if (load_files(normal_paths, LoadStrategy::LoadModel, true).empty()) { res = false; }
        break;
    }

    case LoadFilesType::Multiple3MFOther:
        for (const auto &path : normal_paths) {
            if (wxString(encode_path(path.filename().string().c_str())).EndsWith("3mf")) {
                if (first_file.size() <= 0)
                    first_file.push_back(path);
                else
                    tmf_file.push_back(path);
            } else {
                other_file.push_back(path);
            }
        }

        open_3mf_file(first_file[0]);
        if (load_files(tmf_file, LoadStrategy::LoadModel).empty()) {  res = false;  }
        if (load_files(other_file, LoadStrategy::LoadModel, false).empty()) {  res = false;  }
        break;
    default: break;
    }

    return res;
}


bool Plater::open_3mf_file(const fs::path &file_path)
{
    std::string filename = encode_path(file_path.filename().string().c_str());
    if (!boost::algorithm::iends_with(filename, ".3mf")) {
        return false;
    }

    LoadType load_type = LoadType::Unknown;
    if (!model().objects.empty()) {
        bool show_drop_project_dialog = true;
        if (show_drop_project_dialog) {
            ProjectDropDialog dlg(filename);
            if (dlg.ShowModal() == wxID_OK) {
                int choice = dlg.get_action();
                load_type  = static_cast<LoadType>(choice);
                wxGetApp().app_config->set("import_project_action", std::to_string(choice));

                // BBS: jump to plater panel
                wxGetApp().mainframe->select_tab(MainFrame::tp3DEditor);
            }
        } else
            load_type = static_cast<LoadType>(
                std::clamp(std::stoi(wxGetApp().app_config->get("import_project_action")), static_cast<int>(LoadType::OpenProject), static_cast<int>(LoadType::LoadConfig)));
    } else
        load_type = LoadType::OpenProject;

    if (load_type == LoadType::Unknown) return false;

    switch (load_type) {
        case LoadType::OpenProject: {
            if (wxGetApp().can_load_project())
                load_project(from_path(file_path));
            break;
        }
        case LoadType::LoadGeometry: {
            Plater::TakeSnapshot snapshot(this, "Import Object");
            load_files({file_path}, LoadStrategy::LoadModel);
            break;
        }
        case LoadType::LoadConfig: {
            load_files({file_path}, LoadStrategy::LoadConfig);
            break;
        }
        case LoadType::Unknown: {
            assert(false);
            break;
        }
    }

    return true;
}

int Plater::get_3mf_file_count(std::vector<fs::path> paths)
{
    auto count = 0;
    for (const auto &path : paths) {
        if (wxString(encode_path(path.filename().string().c_str())).EndsWith("3mf")) count++;
    }
    return count;
}

void Plater::add_file()
{
    wxArrayString input_files;
    wxGetApp().import_model(this, input_files);
    if (input_files.empty()) return;

    std::vector<fs::path> paths;
    for (const auto &file : input_files) paths.emplace_back(into_path(file));

    std::string snapshot_label;
    assert(!paths.empty());

    snapshot_label = "Import Objects";
    snapshot_label += ": ";
    snapshot_label += encode_path(paths.front().filename().string().c_str());
    for (size_t i = 1; i < paths.size(); ++i) {
        snapshot_label += ", ";
        snapshot_label += encode_path(paths[i].filename().string().c_str());
    }

    // BBS: check file types
    auto loadfiles_type  = LoadFilesType::NoFile;
    auto amf_files_count = get_3mf_file_count(paths);

    if (paths.size() > 1 && amf_files_count < paths.size()) { loadfiles_type = LoadFilesType::Multiple3MFOther; }
    if (paths.size() > 1 && amf_files_count == paths.size()) { loadfiles_type = LoadFilesType::Multiple3MF; }
    if (paths.size() > 1 && amf_files_count == 0) { loadfiles_type = LoadFilesType::MultipleOther; }
    if (paths.size() == 1 && amf_files_count == 1) { loadfiles_type = LoadFilesType::Single3MF; };
    if (paths.size() == 1 && amf_files_count == 0) { loadfiles_type = LoadFilesType::SingleOther; };

    auto first_file = std::vector<fs::path>{};
    auto tmf_file   = std::vector<fs::path>{};
    auto other_file = std::vector<fs::path>{};

    switch (loadfiles_type)
    {
    case LoadFilesType::Single3MF:
        open_3mf_file(paths[0]);
    	break;

    case LoadFilesType::SingleOther: {
        Plater::TakeSnapshot snapshot(this, snapshot_label);
        if (!load_files(paths, LoadStrategy::LoadModel, false).empty()) { wxGetApp().mainframe->update_title(); }
        break;
    }
    case LoadFilesType::Multiple3MF:
        first_file = std::vector<fs::path>{paths[0]};
        for (auto i = 0; i < paths.size(); i++) {
            if (i > 0) { other_file.push_back(paths[i]); }
        };

        open_3mf_file(first_file[0]);
        if (!load_files(other_file, LoadStrategy::LoadModel).empty()) { wxGetApp().mainframe->update_title(); }
        break;

    case LoadFilesType::MultipleOther: {
        Plater::TakeSnapshot snapshot(this, snapshot_label);
        if (!load_files(paths, LoadStrategy::LoadModel, true).empty()) {
            wxGetApp().mainframe->update_title();
        }
        break;
    }
    case LoadFilesType::Multiple3MFOther:
        for (const auto &path : paths) {
            if (wxString(encode_path(path.filename().string().c_str())).EndsWith("3mf")) {
                if (first_file.size() <= 0)
                    first_file.push_back(path);
                else
                    tmf_file.push_back(path);
            } else {
                other_file.push_back(path);
            }
        }

        open_3mf_file(first_file[0]);
        load_files(tmf_file, LoadStrategy::LoadModel);
        if (!load_files(other_file, LoadStrategy::LoadModel, false).empty()) { wxGetApp().mainframe->update_title(); }
        break;
    default:break;
    }
}

void Plater::update(bool conside_update_flag)
{
    if (conside_update_flag) {
        if (need_update()) {
            p->update();
            p->set_need_update(false);
        }
    }
    else
        p->update();
}

void Plater::object_list_changed() { p->object_list_changed(); }

void Plater::stop_jobs() { p->m_ui_jobs.stop_all(); }

bool Plater::is_any_job_running() const
{
    return p->m_ui_jobs.is_any_running();
}

void Plater::update_ui_from_settings() { p->update_ui_from_settings(); }

void Plater::select_view(const std::string& direction) { p->select_view(direction); }

//BBS: add no_slice logic
void Plater::select_view_3D(const std::string& name, bool no_slice) { p->select_view_3D(name, no_slice); }

bool Plater::is_preview_shown() const { return p->is_preview_shown(); }
bool Plater::is_preview_loaded() const { return p->is_preview_loaded(); }
bool Plater::is_view3D_shown() const { return p->is_view3D_shown(); }

bool Plater::are_view3D_labels_shown() const { return p->are_view3D_labels_shown(); }
void Plater::show_view3D_labels(bool show) { p->show_view3D_labels(show); }

bool Plater::is_sidebar_collapsed() const { return p->is_sidebar_collapsed(); }
void Plater::collapse_sidebar(bool show) { p->collapse_sidebar(show); }

//BBS
void Plater::select_curr_plate_all() { p->select_curr_plate_all(); }
void Plater::remove_curr_plate_all() { p->remove_curr_plate_all(); }

void Plater::select_all() { p->select_all(); }
void Plater::deselect_all() { p->deselect_all(); }

void Plater::remove(size_t obj_idx) { p->remove(obj_idx); }
void Plater::reset(bool apply_presets_change) { p->reset(apply_presets_change); }
void Plater::reset_with_confirm()
{
    if (p->model.objects.empty() || MessageDialog(static_cast<wxWindow *>(this), _L("All objects will be removed, continue?"),
                                                  wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Delete all"), wxYES_NO | wxCANCEL | wxYES_DEFAULT | wxCENTRE)
                                            .ShowModal() == wxID_YES) {
        reset();
        // BBS: jump to plater panel
        wxGetApp().mainframe->select_tab(size_t(0));
    }
}

// BBS: save logic
int GUI::Plater::close_with_confirm(std::function<bool(bool)> second_check)
{
    if (up_to_date(false, false)) {
        if (second_check && !second_check(false)) return wxID_CANCEL;
        model().set_backup_path("");
        return wxID_NO;
    }

    auto result = MessageDialog(static_cast<wxWindow*>(this), _L("The current project has unsaved changes, save it before continue?"),
        wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Save"), wxYES_NO | wxCANCEL | wxYES_DEFAULT | wxCENTRE).ShowModal();
    if (result == wxID_CANCEL)
        return result;
    else if (result == wxID_YES) {
        result = save_project();
        if (result == wxID_CANCEL)
            return result;
    }

    if (second_check && !second_check(result == wxID_YES)) return wxID_CANCEL;

    model().set_backup_path("");
    up_to_date(true, false);
    up_to_date(true, true);

    return result;
}

//BBS: trigger a restore project event
void Plater::trigger_restore_project(int skip_confirm)
{
    auto evt = new wxCommandEvent(EVT_RESTORE_PROJECT, this->GetId());
    evt->SetInt(skip_confirm);
    wxQueueEvent(this, evt);
    //wxPostEvent(this, *evt);
}

//BBS
void Plater::delete_object_from_model(size_t obj_idx, bool refresh_immediately) { p->delete_object_from_model(obj_idx, refresh_immediately); }

//BBS: delete all from model
void Plater::delete_all_objects_from_model()
{
    p->delete_all_objects_from_model();
}

void Plater::set_selected_visible(bool visible)
{
    if (p->get_curr_selection().is_empty())
        return;

    Plater::TakeSnapshot snapshot(this, "Set Selected Objects Visible in AssembleView");
    p->m_ui_jobs.cancel_all();

    p->get_current_canvas3D()->set_selected_visible(visible);
}


void Plater::remove_selected()
{
    /*if (p->get_selection().is_empty())
        return;*/
    if (p->get_curr_selection().is_empty())
        return;

    // BBS: check before deleting object
    if (!p->can_delete())
        return;

    Plater::TakeSnapshot snapshot(this, "Delete Selected Objects");
    p->m_ui_jobs.cancel_all();

    //BBS delete current selected
    // p->view3D->delete_selected();
    p->get_current_canvas3D()->delete_selected();
}

void Plater::increase_instances(size_t num)
{
    // BBS
#if 0
    if (! can_increase_instances()) { return; }

    Plater::TakeSnapshot snapshot(this, "Increase Instances");

    int obj_idx = p->get_selected_object_idx();

    ModelObject* model_object = p->model.objects[obj_idx];
    ModelInstance* model_instance = model_object->instances.back();

    bool was_one_instance = model_object->instances.size()==1;

    double offset_base = canvas3D()->get_size_proportional_to_max_bed_size(0.05);
    double offset = offset_base;
    for (size_t i = 0; i < num; i++, offset += offset_base) {
        Vec3d offset_vec = model_instance->get_offset() + Vec3d(offset, offset, 0.0);
        model_object->add_instance(offset_vec, model_instance->get_scaling_factor(), model_instance->get_rotation(), model_instance->get_mirror());
//        p->print.get_object(obj_idx)->add_copy(Slic3r::to_2d(offset_vec));
    }

#ifdef SUPPORT_AUTO_CENTER
    if (p->get_config("autocenter") == "true")
        arrange();
#endif

    p->update();

    p->get_selection().add_instance(obj_idx, (int)model_object->instances.size() - 1);

    sidebar().obj_list()->increase_object_instances(obj_idx, was_one_instance ? num + 1 : num);

    p->selection_changed();
    this->p->schedule_background_process();
#endif
}

void Plater::decrease_instances(size_t num)
{
    // BBS
#if 0
    if (! can_decrease_instances()) { return; }

    Plater::TakeSnapshot snapshot(this, "Decrease Instances");

    int obj_idx = p->get_selected_object_idx();

    ModelObject* model_object = p->model.objects[obj_idx];
    if (model_object->instances.size() > num) {
        for (size_t i = 0; i < num; ++ i)
            model_object->delete_last_instance();
        p->update();
        // Delete object from Sidebar list. Do it after update, so that the GLScene selection is updated with the modified model.
        sidebar().obj_list()->decrease_object_instances(obj_idx, num);
    }
    else {
        remove(obj_idx);
    }

    if (!model_object->instances.empty())
        p->get_selection().add_instance(obj_idx, (int)model_object->instances.size() - 1);

    p->selection_changed();
    this->p->schedule_background_process();
#endif
}

static long GetNumberFromUser(  const wxString& msg,
                                const wxString& prompt,
                                const wxString& title,
                                long value,
                                long min,
                                long max,
                                wxWindow* parent)
{
#ifdef _WIN32
    wxNumberEntryDialog dialog(parent, msg, prompt, title, value, min, max, wxDefaultPosition);
    wxGetApp().UpdateDlgDarkUI(&dialog);
    if (dialog.ShowModal() == wxID_OK)
        return dialog.GetValue();

    return -1;
#else
    return wxGetNumberFromUser(msg, prompt, title, value, min, max, parent);
#endif
}

void Plater::set_number_of_copies(/*size_t num*/)
{
    int obj_idx = p->get_selected_object_idx();
    if (obj_idx == -1)
        return;

    ModelObject* model_object = p->model.objects[obj_idx];

    const int num = GetNumberFromUser( " ", _L("Number of copies:"),
                                    _L("Copies of the selected object"), model_object->instances.size(), 0, 1000, this );
    if (num < 0)
        return;

    Plater::TakeSnapshot snapshot(this, (boost::format("Set numbers of copies to %1%")%num).str());

    int diff = num - (int)model_object->instances.size();
    if (diff > 0)
        increase_instances(diff);
    else if (diff < 0)
        decrease_instances(-diff);
}

void Plater::fill_bed_with_instances()
{
    if (!p->m_ui_jobs.is_any_running())
        p->m_ui_jobs.fill_bed();
}

bool Plater::is_selection_empty() const
{
    return p->get_selection().is_empty() || p->get_selection().is_wipe_tower();
}

void Plater::scale_selection_to_fit_print_volume()
{
    p->scale_selection_to_fit_print_volume();
}

void Plater::convert_unit(ConversionType conv_type)
{
    std::vector<int> obj_idxs, volume_idxs;
    wxGetApp().obj_list()->get_selection_indexes(obj_idxs, volume_idxs);
    if (obj_idxs.empty() && volume_idxs.empty())
        return;

    TakeSnapshot snapshot(this, conv_type == ConversionType::CONV_FROM_INCH  ? "Convert from imperial units" :
                                conv_type == ConversionType::CONV_TO_INCH    ? "Revert conversion from imperial units" :
                                conv_type == ConversionType::CONV_FROM_METER ? "Convert from meters" : "Revert conversion from meters");
    wxBusyCursor wait;

    ModelObjectPtrs objects;
    std::reverse(obj_idxs.begin(), obj_idxs.end());
    for (int obj_idx : obj_idxs) {
        ModelObject *object = p->model.objects[obj_idx];
        object->convert_units(objects, conv_type, volume_idxs);
        remove(obj_idx);
    }
    std::reverse(objects.begin(), objects.end());
    p->load_model_objects(objects);

    Selection& selection = p->view3D->get_canvas3d()->get_selection();
    size_t last_obj_idx = p->model.objects.size() - 1;

    if (volume_idxs.empty()) {
        for (size_t i = 0; i < objects.size(); ++i)
            selection.add_object((unsigned int)(last_obj_idx - i), i == 0);
    }
    else {
        for (int vol_idx : volume_idxs)
            selection.add_volume(last_obj_idx, vol_idx, 0, false);
    }
}

// BBS: replace z with plane_points
void Plater::cut(size_t obj_idx, size_t instance_idx, std::array<Vec3d, 4> plane_points, ModelObjectCutAttributes attributes)
{
    wxCHECK_RET(obj_idx < p->model.objects.size(), "obj_idx out of bounds");
    auto *object = p->model.objects[obj_idx];

    wxCHECK_RET(instance_idx < object->instances.size(), "instance_idx out of bounds");

    if (! attributes.has(ModelObjectCutAttribute::KeepUpper) && ! attributes.has(ModelObjectCutAttribute::KeepLower))
        return;

    Plater::TakeSnapshot snapshot(this, "Cut by Plane");

    wxBusyCursor wait;
    // BBS: replace z with plane_points
    const auto new_objects = object->cut(instance_idx, plane_points, attributes);

    remove(obj_idx);
    p->load_model_objects(new_objects);

    Selection& selection = p->get_selection();
    size_t last_id = p->model.objects.size() - 1;
    for (size_t i = 0; i < new_objects.size(); ++i)
        selection.add_object((unsigned int)(last_id - i), i == 0);
}

// BBS
void Plater::segment(size_t obj_idx, size_t instance_idx, double smoothing_alpha, int segment_number)
{
    wxCHECK_RET(obj_idx < p->model.objects.size(), "obj_idx out of bounds");
    auto* object = p->model.objects[obj_idx];

    wxCHECK_RET(instance_idx < object->instances.size(), "instance_idx out of bounds");

    Plater::TakeSnapshot snapshot(this, "Segment");

    wxBusyCursor wait;
    // real process
    PresetBundle& preset_bundle = *wxGetApp().preset_bundle;
    const auto print_tech = preset_bundle.printers.get_edited_preset().printer_technology();
    const size_t filament_cnt = print_tech != ptFFF ? 1 : preset_bundle.filament_presets.size();
    const auto new_objects = object->segment(instance_idx, filament_cnt, smoothing_alpha, segment_number);

    remove(obj_idx);
    p->load_model_objects(new_objects);

    Selection& selection = p->get_selection();
    size_t last_id = p->model.objects.size() - 1;
    for (size_t i = 0; i < new_objects.size(); ++i)
    {
        selection.add_object((unsigned int)(last_id - i), i == 0);
    }
}

// BBS
void Plater::merge(size_t obj_idx, std::vector<int>& vol_indeces)
{
    wxCHECK_RET(obj_idx < p->model.objects.size(), "obj_idx out of bounds");
    auto* object = p->model.objects[obj_idx];

    Plater::TakeSnapshot snapshot(this, "Merge");

    wxBusyCursor wait;
    // real process
    PresetBundle& preset_bundle = *wxGetApp().preset_bundle;
    const auto print_tech = preset_bundle.printers.get_edited_preset().printer_technology();
    // BBS
    const size_t filament_cnt = print_tech != ptFFF ? 1 : preset_bundle.filament_presets.size();

    const auto new_objects = object->merge_volumes(vol_indeces);

    remove(obj_idx);
    p->load_model_objects(new_objects);

    Selection& selection = p->get_selection();
    size_t last_id = p->model.objects.size() - 1;
    for (size_t i = 0; i < new_objects.size(); ++i)
    {
        selection.add_object((unsigned int)(last_id - i), i == 0);
    }
}

void Plater::export_gcode(bool prefer_removable)
{
    if (p->model.objects.empty())
        return;

    //if (get_view3D_canvas3D()->get_gizmos_manager().is_in_editing_mode(true))
    //    return;

    if (p->process_completed_with_error == p->partplate_list.get_curr_plate_index())
        return;

    // If possible, remove accents from accented latin characters.
    // This function is useful for generating file names to be processed by legacy firmwares.
    fs::path default_output_file;
    try {
        // Update the background processing, so that the placeholder parser will get the correct values for the ouput file template.
        // Also if there is something wrong with the current configuration, a pop-up dialog will be shown and the export will not be performed.
        unsigned int state = this->p->update_restart_background_process(false, false);
        if (state & priv::UPDATE_BACKGROUND_PROCESS_INVALID)
            return;
        default_output_file = this->p->background_process.output_filepath_for_project("");
    } catch (const Slic3r::PlaceholderParserError &ex) {
        // Show the error with monospaced font.
        show_error(this, ex.what(), true);
        return;
    } catch (const std::exception &ex) {
        show_error(this, ex.what(), false);
        return;
    }
    default_output_file = fs::path(Slic3r::fold_utf8_to_ascii(default_output_file.string()));
    AppConfig 				&appconfig 				 = *wxGetApp().app_config;
    RemovableDriveManager 	&removable_drive_manager = *wxGetApp().removable_drive_manager();
    // Get a last save path, either to removable media or to an internal media.
    std::string      		 start_dir 				 = appconfig.get_last_output_dir(default_output_file.parent_path().string(), prefer_removable);
    if (prefer_removable) {
        // Returns a path to a removable media if it exists, prefering start_dir. Update the internal removable drives database.
        start_dir = removable_drive_manager.get_removable_drive_path(start_dir);
        if (start_dir.empty())
            // Direct user to the last internal media.
            start_dir = appconfig.get_last_output_dir(default_output_file.parent_path().string(), false);
    }

    fs::path output_path;
    {
        std::string ext = default_output_file.extension().string();
        wxFileDialog dlg(this, (printer_technology() == ptFFF) ? _L("Save G-code file as:") : _devL("Save SLA file as:"),
            start_dir,
            from_path(default_output_file.filename()),
            GUI::file_wildcards((printer_technology() == ptFFF) ? FT_GCODE : FT_SL1, ext),
            wxFD_SAVE | wxFD_OVERWRITE_PROMPT
        );
        if (dlg.ShowModal() == wxID_OK) {
            output_path = into_path(dlg.GetPath());
            while (has_illegal_filename_characters(output_path.filename().string())) {
                show_error(this, _devL("The provided file name is not valid.") + "\n" +
                    _devL("The following characters are not allowed by a FAT file system:") + " <>:/\\|?*\"");
                dlg.SetFilename(from_path(output_path.filename()));
                if (dlg.ShowModal() == wxID_OK)
                    output_path = into_path(dlg.GetPath());
                else {
                    output_path.clear();
                    break;
                }
            }
        }
    }

    if (! output_path.empty()) {
        bool path_on_removable_media = removable_drive_manager.set_and_verify_last_save_path(output_path.string());
        //bool path_on_removable_media = false;
        p->notification_manager->new_export_began(path_on_removable_media);
        p->exporting_status = path_on_removable_media ? ExportingStatus::EXPORTING_TO_REMOVABLE : ExportingStatus::EXPORTING_TO_LOCAL;
        p->last_output_path = output_path.string();
        p->last_output_dir_path = output_path.parent_path().string();
        p->export_gcode(output_path, path_on_removable_media);
        // Storing a path to AppConfig either as path to removable media or a path to internal media.
        // is_path_on_removable_drive() is called with the "true" parameter to update its internal database as the user may have shuffled the external drives
        // while the dialog was open.
        appconfig.update_last_output_dir(output_path.parent_path().string(), path_on_removable_media);
    }
}

void Plater::send_to_printer(bool isall)
{
    p->on_action_send_to_printer(isall);
}

//BBS export gcode 3mf to file
void Plater::export_gcode_3mf(bool export_all)
{
    if (p->model.objects.empty())
        return;

    if (p->process_completed_with_error == p->partplate_list.get_curr_plate_index())
        return;

    //calc default_output_file, get default output file from background process
    fs::path default_output_file;
    AppConfig& appconfig = *wxGetApp().app_config;
    std::string start_dir;
    try {
        // Update the background processing, so that the placeholder parser will get the correct values for the ouput file template.
        // Also if there is something wrong with the current configuration, a pop-up dialog will be shown and the export will not be performed.
        unsigned int state = this->p->update_restart_background_process(false, false);
        if (state & priv::UPDATE_BACKGROUND_PROCESS_INVALID)
            return;
        default_output_file = this->p->background_process.output_filepath_for_project("");
    }
    catch (const Slic3r::PlaceholderParserError& ex) {
        // Show the error with monospaced font.
        show_error(this, ex.what(), true);
        return;
    }
    catch (const std::exception& ex) {
        show_error(this, ex.what(), false);
        return;
    }
    default_output_file.replace_extension(".3mf");
    default_output_file = fs::path(Slic3r::fold_utf8_to_ascii(default_output_file.string()));

    //Get a last save path
    start_dir = appconfig.get_last_output_dir(default_output_file.parent_path().string(), false);

    fs::path output_path;
    {
        std::string ext = default_output_file.extension().string();
        wxFileDialog dlg(this, _L("Save Sliced file as:"),
            start_dir,
            from_path(default_output_file.filename()),
            GUI::file_wildcards(FT_3MF, ext),
            wxFD_SAVE | wxFD_OVERWRITE_PROMPT
        );
        if (dlg.ShowModal() == wxID_OK) {
            output_path = into_path(dlg.GetPath());
            ext = output_path.extension().string();
            if (ext != ".3mf")
                output_path = output_path.string() + ".3mf";
        }
    }

    if (!output_path.empty()) {
        //BBS do not set to removable media path
        bool path_on_removable_media = false;
        p->notification_manager->new_export_began(path_on_removable_media);
        p->exporting_status = path_on_removable_media ? ExportingStatus::EXPORTING_TO_REMOVABLE : ExportingStatus::EXPORTING_TO_LOCAL;
        //BBS do not save last output path
        p->last_output_path = output_path.string();
        p->last_output_dir_path = output_path.parent_path().string();
        int plate_idx = get_partplate_list().get_curr_plate_index();
        if (export_all)
            plate_idx = PLATE_ALL_IDX;
        export_3mf(output_path, SaveStrategy::Silence | SaveStrategy::SplitModel | SaveStrategy::WithGcode | SaveStrategy::SkipModel, plate_idx); // BBS: silence

        RemovableDriveManager& removable_drive_manager = *wxGetApp().removable_drive_manager();


        bool on_removable = removable_drive_manager.is_path_on_removable_drive(p->last_output_dir_path);


        // update last output dir
        appconfig.update_last_output_dir(output_path.parent_path().string(), false);
        p->notification_manager->push_exporting_finished_notification(output_path.string(), p->last_output_dir_path, on_removable);
    }
}

void Plater::send_gcode_finish(wxString name)
{
    auto out_str = GUI::format(_L("The file %s has been sent to the printer's storage space and can be viewed on the printer."), name);
    p->notification_manager->push_exporting_finished_notification(out_str, "", false);
}

void Plater::export_core_3mf()
{
    wxString path = p->get_export_file(FT_3MF);
    if (path.empty()) { return; }
    const std::string path_u8 = into_u8(path);
    export_3mf(path_u8, SaveStrategy::Silence);
}

void Plater::export_stl(bool extended, bool selection_only)
{
    if (p->model.objects.empty()) { return; }

    wxBusyCursor wait;

    const auto &selection = p->get_selection();

    // BBS support mulity objects
    // const auto obj_idx = selection.get_object_idx();
    // if (selection_only && (obj_idx == -1 || selection.is_wipe_tower()))
    //    return;

    if (selection_only && selection.is_wipe_tower())
        return;

    //BBS
    if (selection_only) {
        // only support selection single full object and mulitiple full object
        if (!selection.is_single_full_object() && !selection.is_multiple_full_object())
            return;
    }

    wxString path = p->get_export_file(FT_STL);
    if (path.empty()) { return; }
    const std::string path_u8 = into_u8(path);


    // Following lambda generates a combined mesh for export with normals pointing outwards.
    auto mesh_to_export = [](const ModelObject& mo, int instance_id) {
        TriangleMesh mesh;
        for (const ModelVolume* v : mo.volumes)
            if (v->is_model_part()) {
                TriangleMesh vol_mesh(v->mesh());
                vol_mesh.transform(v->get_matrix(), true);
                mesh.merge(vol_mesh);
            }
        if (instance_id == -1) {
            TriangleMesh vols_mesh(mesh);
            mesh = TriangleMesh();
            for (const ModelInstance* i : mo.instances) {
                TriangleMesh m = vols_mesh;
                m.transform(i->get_matrix(), true);
                mesh.merge(m);
            }
        }
        else if (0 <= instance_id && instance_id < int(mo.instances.size()))
            mesh.transform(mo.instances[instance_id]->get_matrix(), true);
        return mesh;
    };

    TriangleMesh mesh;
    if (p->printer_technology == ptFFF) {
        if (selection_only) {
            if (selection.is_single_full_object()) {
                const auto obj_idx = selection.get_object_idx();
                const ModelObject* model_object = p->model.objects[obj_idx];
                if (selection.get_mode() == Selection::Instance)
                {
                    mesh = std::move(mesh_to_export(*model_object, ( model_object->instances.size() > 1) ? -1 : selection.get_instance_idx()));
                }
                else
                {
                    const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
                    mesh = model_object->volumes[volume->volume_idx()]->mesh();
                    mesh.transform(volume->get_volume_transformation().get_matrix(), true);
                }

                if (model_object->instances.size() == 1)
                    mesh.translate(-model_object->origin_translation.cast<float>());
            }
            else if (selection.is_multiple_full_object()) {
                const std::set<std::pair<int, int>>& instances_idxs = p->get_selection().get_selected_object_instances();
                for (const std::pair<int, int>& i : instances_idxs)
                {
                    ModelObject* object = p->model.objects[i.first];
                    mesh.merge(mesh_to_export(*object, i.second));
                }
            }
        }
        else {
            for (const ModelObject *o : p->model.objects)
                mesh.merge(mesh_to_export(*o, -1));
        }
    }
    else
    {
        // This is SLA mode, all objects have only one volume.
        // However, we must have a look at the backend to load
        // hollowed mesh and/or supports
        const auto obj_idx = selection.get_object_idx();
        const PrintObjects& objects = p->sla_print.objects();
        for (const SLAPrintObject* object : objects)
        {
            const ModelObject* model_object = object->model_object();
            if (selection_only) {
                if (model_object->id() != p->model.objects[obj_idx]->id())
                    continue;
            }
            Transform3d mesh_trafo_inv = object->trafo().inverse();
            bool is_left_handed = object->is_left_handed();

            TriangleMesh pad_mesh;
            bool has_pad_mesh = extended && object->has_mesh(slaposPad);
            if (has_pad_mesh)
            {
                pad_mesh = object->get_mesh(slaposPad);
                pad_mesh.transform(mesh_trafo_inv);
            }

            TriangleMesh supports_mesh;
            bool has_supports_mesh = extended && object->has_mesh(slaposSupportTree);
            if (has_supports_mesh)
            {
                supports_mesh = object->get_mesh(slaposSupportTree);
                supports_mesh.transform(mesh_trafo_inv);
            }
            const std::vector<SLAPrintObject::Instance>& obj_instances = object->instances();
            for (const SLAPrintObject::Instance& obj_instance : obj_instances)
            {
                auto it = std::find_if(model_object->instances.begin(), model_object->instances.end(),
                    [&obj_instance](const ModelInstance *mi) { return mi->id() == obj_instance.instance_id; });
                assert(it != model_object->instances.end());

                if (it != model_object->instances.end())
                {
                    bool one_inst_only = selection_only && ! selection.is_single_full_object();

                    int instance_idx = it - model_object->instances.begin();
                    const Transform3d& inst_transform = one_inst_only
                            ? Transform3d::Identity()
                            : object->model_object()->instances[instance_idx]->get_transformation().get_matrix();

                    TriangleMesh inst_mesh;

                    if (has_pad_mesh)
                    {
                        TriangleMesh inst_pad_mesh = pad_mesh;
                        inst_pad_mesh.transform(inst_transform, is_left_handed);
                        inst_mesh.merge(inst_pad_mesh);
                    }

                    if (has_supports_mesh)
                    {
                        TriangleMesh inst_supports_mesh = supports_mesh;
                        inst_supports_mesh.transform(inst_transform, is_left_handed);
                        inst_mesh.merge(inst_supports_mesh);
                    }

                    TriangleMesh inst_object_mesh = object->get_mesh_to_slice();
                    inst_object_mesh.transform(mesh_trafo_inv);
                    inst_object_mesh.transform(inst_transform, is_left_handed);

                    inst_mesh.merge(inst_object_mesh);

                    // ensure that the instance lays on the bed
                    inst_mesh.translate(0.0f, 0.0f, -inst_mesh.bounding_box().min[2]);

                    // merge instance with global mesh
                    mesh.merge(inst_mesh);

                    if (one_inst_only)
                        break;
                }
            }
        }
    }

    Slic3r::store_stl(path_u8.c_str(), &mesh, true);
}

//BBS: remove amf export
/*void Plater::export_amf()
{
    if (p->model.objects.empty()) { return; }

    wxString path = p->get_export_file(FT_AMF);
    if (path.empty()) { return; }
    const std::string path_u8 = into_u8(path);

    wxBusyCursor wait;
    bool export_config = true;
    DynamicPrintConfig cfg = wxGetApp().preset_bundle->full_config_secure();
    bool full_pathnames = false;
    if (Slic3r::store_amf(path_u8.c_str(), &p->model, export_config ? &cfg : nullptr, full_pathnames)) {
        ; //store success
    } else {
        ; // store failed
    }
}*/

// BBS: backup
int Plater::export_3mf(const boost::filesystem::path& output_path, SaveStrategy strategy, int export_plate_idx, Export3mfProgressFn proFn)
{
    //if (p->model.objects.empty()) {
    //    MessageDialog dialog(nullptr, _L("No objects to export."), _L("Save project"), wxYES);
    //    if (dialog.ShowModal() == wxYES)
    //        return -1;
    //}

    if (output_path.empty())
        return -1;

    bool export_config = true;
    wxString path = from_path(output_path);

    if (!path.Lower().EndsWith(".3mf"))
        return -1;

    DynamicPrintConfig cfg = wxGetApp().preset_bundle->full_config_secure();
    const std::string path_u8 = into_u8(path);
    wxBusyCursor wait;

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": path=%1%, backup=%2%, export_plate_idx=%3%, SaveStrategy=%4%")
        %output_path.string()%(strategy & SaveStrategy::Backup)%export_plate_idx %(unsigned int)strategy;

    //BBS: add plate logic for thumbnail generate
    std::vector<ThumbnailData*> thumbnails;
    std::vector<ThumbnailData*> calibration_thumbnails;
    std::vector<PlateBBoxData*> plate_bboxes;
    // BBS: backup
    if (!(strategy & SaveStrategy::Backup)) {
        for (int i = 0; i < p->partplate_list.get_plate_count(); i++) {
            ThumbnailData* thumbnail_data = &p->partplate_list.get_plate(i)->thumbnail_data;
            if (p->partplate_list.get_plate(i)->thumbnail_data.is_valid() &&  using_exported_file()) {
                //no need to generate thumbnail
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": non need to re-generate thumbnail for gcode/exported mode of plate %1%")%i;
            }
            else {
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": re-generate thumbnail for plate %1%") % i;
                const ThumbnailsParams thumbnail_params = { {}, false, true, true, true, i };
                p->generate_thumbnail(p->partplate_list.get_plate(i)->thumbnail_data, THUMBNAIL_SIZE_3MF.first, THUMBNAIL_SIZE_3MF.second, thumbnail_params, Camera::EType::Ortho);
            }
            thumbnails.push_back(thumbnail_data);

            ThumbnailData* calibration_data = &p->partplate_list.get_plate(i)->cali_thumbnail_data;
            calibration_thumbnails.push_back(calibration_data);
            PlateBBoxData* plate_bbox_data = &p->partplate_list.get_plate(i)->cali_bboxes_data;
            plate_bboxes.push_back(plate_bbox_data);
        }

        if (p->partplate_list.get_curr_plate()->is_slice_result_valid()) {
            //BBS generate BBS calibration thumbnails
            int index = p->partplate_list.get_curr_plate_index();
            ThumbnailData* calibration_data = calibration_thumbnails[index];
            const ThumbnailsParams calibration_params = { {}, false, true, true, true, p->partplate_list.get_curr_plate_index() };
            p->generate_calibration_thumbnail(*calibration_data, PartPlate::cali_thumbnail_width, PartPlate::cali_thumbnail_height, calibration_params);
            if (using_exported_file()) {
                //do nothing
            }
            else
                *plate_bboxes[index] = p->generate_first_layer_bbox();
        }
    }

    //BBS: add bbs 3mf logic
    PlateDataPtrs plate_data_list;
    p->partplate_list.store_to_3mf_structure(plate_data_list, (strategy & SaveStrategy::WithGcode || strategy & SaveStrategy::WithSliceInfo), export_plate_idx);

    // BBS: backup
    PresetBundle& preset_bundle = *wxGetApp().preset_bundle;
    std::vector<Preset*> project_presets = preset_bundle.get_current_project_embedded_presets();

    StoreParams store_params;
    store_params.path  = path_u8.c_str();
    store_params.model = &p->model;
    store_params.plate_data_list = plate_data_list;
    store_params.export_plate_idx = export_plate_idx;
    store_params.project_presets = project_presets;
    store_params.config = export_config ? &cfg : nullptr;
    store_params.thumbnail_data = thumbnails;
    store_params.calibration_thumbnail_data = calibration_thumbnails;
    store_params.proFn = proFn;
    store_params.id_bboxes = plate_bboxes;//BBS
    store_params.project = &p->project;
    store_params.strategy = strategy | SaveStrategy::Zip64;


    // get type and color for platedata
    auto* filament_color = dynamic_cast<const ConfigOptionStrings*>(cfg.option("filament_colour"));

    for (int i = 0; i < plate_data_list.size(); i++) {
        PlateData *plate_data = plate_data_list[i];
        for (auto it = plate_data->slice_filaments_info.begin(); it != plate_data->slice_filaments_info.end(); it++) {
            std::string display_filament_type;
            it->type  = cfg.get_filament_type(display_filament_type, it->id);
            it->color = filament_color ? filament_color->get_at(it->id) : "#FFFFFF";
            // save filament info used in curr plate
            int index = p->partplate_list.get_curr_plate_index();
            if (store_params.id_bboxes.size() > index) {
                store_params.id_bboxes[index]->filament_ids.push_back(it->id);
                store_params.id_bboxes[index]->filament_colors.push_back(it->color);
            }
        }
    }

    // handle Design Info
    bool has_design_info = false;
    ModelDesignInfo designInfo;
    if (p->model.design_info != nullptr) {
        if (!p->model.design_info->Designer.empty()) {
            BOOST_LOG_TRIVIAL(trace) << "design_info, found designer = " << p->model.design_info->Designer;
            has_design_info = true;
        }
    }
    if (!has_design_info) {
        // add Designed Info
        if (p->model.design_info == nullptr) {
            // set designInfo before export and reset after export
            if (wxGetApp().is_user_login()) {
                p->model.design_info                 = std::make_shared<ModelDesignInfo>();
                //p->model.design_info->Designer       = wxGetApp().getAgent()->get_user_nickanme();
                p->model.design_info->Designer       = "";
                p->model.design_info->DesignerUserId = wxGetApp().getAgent()->get_user_id();
                BOOST_LOG_TRIVIAL(trace) << "design_info prepare, designer = "<< "";
                BOOST_LOG_TRIVIAL(trace) << "design_info prepare, designer_user_id = " << p->model.design_info->DesignerUserId;
            }
        }
    }

    bool store_result = Slic3r::store_bbs_3mf(store_params);
    // reset designed info
    if (!has_design_info)
        p->model.design_info = nullptr;

    if (store_result) {
        if (!(store_params.strategy & SaveStrategy::Silence)) {
            // Success
            p->set_project_filename(path);
        }
    }
    else {
        return -1;
    }

    if (project_presets.size() > 0)
    {
        for (unsigned int i = 0; i < project_presets.size(); i++)
        {
            delete project_presets[i];
        }
        project_presets.clear();
    }

    release_PlateData_list(plate_data_list);

    for (unsigned int i = 0; i < calibration_thumbnails.size(); i++)
    {
        //release the data here, as it will always be generated when export
        calibration_thumbnails[i]->reset();
    }

    return 0;
}

void Plater::publish_project()
{
    return;
}


void Plater::reload_from_disk()
{
    p->reload_from_disk();
}

void Plater::replace_with_stl()
{
    p->replace_with_stl();
}

void Plater::reload_all_from_disk()
{
    p->reload_all_from_disk();
}

bool Plater::has_toolpaths_to_export() const
{
    return  p->preview->get_canvas3d()->has_toolpaths_to_export();
}

void Plater::export_toolpaths_to_obj() const
{
    if ((printer_technology() != ptFFF) || !is_preview_loaded())
        return;

    wxString path = p->get_export_file(FT_OBJ);
    if (path.empty())
        return;

    wxBusyCursor wait;
    p->preview->get_canvas3d()->export_toolpaths_to_obj(into_u8(path).c_str());
}

//BBS: add multiple plate reslice logic
void Plater::reslice()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", Line %1%: enter, process_completed_with_error=%2%")%__LINE__ %p->process_completed_with_error;
    // There is "invalid data" button instead "slice now"
    if (p->process_completed_with_error == p->partplate_list.get_curr_plate_index())
    {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(": process_completed_with_error, return directly");
        reset_gcode_toolpaths();
        return;
    }

    // In case SLA gizmo is in editing mode, refuse to continue
    // and notify user that he should leave it first.
    if (get_view3D_canvas3D()->get_gizmos_manager().is_in_editing_mode(true))
        return;

    // Stop arrange and (or) optimize rotation tasks.
    this->stop_jobs();

    if (printer_technology() == ptSLA) {
        for (auto& object : model().objects)
            if (object->sla_points_status == sla::PointsStatus::NoPoints)
                object->sla_points_status = sla::PointsStatus::Generating;
    }

    //FIXME Don't reslice if export of G-code or sending to OctoPrint is running.
    // bitmask of UpdateBackgroundProcessReturnState
    unsigned int state = this->p->update_background_process(true);
    if (state & priv::UPDATE_BACKGROUND_PROCESS_REFRESH_SCENE)
        this->p->view3D->reload_scene(false);
    // If the SLA processing of just a single object's supports is running, restart slicing for the whole object.
    this->p->background_process.set_task(PrintBase::TaskParams());
    // Only restarts if the state is valid.
    //BBS: jusdge the result
    bool result = this->p->restart_background_process(state | priv::UPDATE_BACKGROUND_PROCESS_FORCE_RESTART);
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", Line %1%: restart background,state=%2%, result=%3%")%__LINE__%state %result;
    if ((state & priv::UPDATE_BACKGROUND_PROCESS_INVALID) != 0)
    {
        //BBS: add logs
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(": state %1% is UPDATE_BACKGROUND_PROCESS_INVALID, can not slice") % state;
        p->update_fff_scene_only_shells();
        return;
    }

    if ((!result) && p->m_slice_all && (p->m_cur_slice_plate < (p->partplate_list.get_plate_count() - 1)))
    {
        //slice next
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(": in slicing all, current plate %1% already sliced, skip to next") % p->m_cur_slice_plate ;
        SlicingProcessCompletedEvent evt(EVT_PROCESS_COMPLETED, 0,
            SlicingProcessCompletedEvent::Finished, nullptr);
        // Post the "complete" callback message, so that it will slice the next plate soon
        wxQueueEvent(this, evt.Clone());
        p->m_is_slicing = true;
        this->SetDropTarget(nullptr);
        if (p->m_cur_slice_plate == 0)
            reset_gcode_toolpaths();
        return;
    }

    if (result) {
        p->m_is_slicing = true;
        this->SetDropTarget(nullptr);
    }

    bool clean_gcode_toolpaths = true;
    // BBS
    if (p->background_process.running())
    {
        //p->ready_to_slice = false;
        p->main_frame->update_slice_print_status(MainFrame::eEventSliceUpdate, false);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": background process is running, m_is_slicing is true");
    }
    else if (!p->background_process.empty() && !p->background_process.idle()) {
        //p->show_action_buttons(true);
        //p->ready_to_slice = true;
        p->main_frame->update_slice_print_status(MainFrame::eEventSliceUpdate, true);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": background process changes to not_idle, set ready_to_slice back to true");
    }
    else {
        //BBS: add reset logic for empty plate
        PartPlate * current_plate = p->background_process.get_current_plate();

        if (!current_plate->has_printable_instances()) {
            clean_gcode_toolpaths = true;
            current_plate->update_slice_result_valid_state(false);
        }
        else {
            clean_gcode_toolpaths = false;
            current_plate->update_slice_result_valid_state(true);
        }
        p->main_frame->update_slice_print_status(MainFrame::eEventSliceUpdate, false);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": background process in idle state, use previous result, clean_gcode_toolpaths=%1%")%clean_gcode_toolpaths;
    }

    if (clean_gcode_toolpaths)
        reset_gcode_toolpaths();

    p->preview->reload_print(!clean_gcode_toolpaths);

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": finished, started slicing for plate %1%") % p->partplate_list.get_curr_plate_index();
}

//BBS: add project slicing related logic
int Plater::start_next_slice()
{
    // Stop arrange and (or) optimize rotation tasks.
    //this->stop_jobs();

    //FIXME Don't reslice if export of G-code or sending to OctoPrint is running.
    // bitmask of UpdateBackgroundProcessReturnState
    unsigned int state = this->p->update_background_process(true, false, false);
    if (state & priv::UPDATE_BACKGROUND_PROCESS_REFRESH_SCENE)
        this->p->view3D->reload_scene(false);

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": update_background_process returns %1%")%state;
    if (p->partplate_list.get_curr_plate()->is_apply_result_invalid()) {
        p->process_completed_with_error = p->partplate_list.get_curr_plate_index();
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(": found invalidated apply in update_background_process.");
        return -1;
    }

    // Only restarts if the state is valid.
    bool result = this->p->restart_background_process(state | priv::UPDATE_BACKGROUND_PROCESS_FORCE_RESTART);
    if (!result)
    {
        //slice next
        SlicingProcessCompletedEvent evt(EVT_PROCESS_COMPLETED, 0,
                SlicingProcessCompletedEvent::Finished, nullptr);
        // Post the "complete" callback message, so that it will slice the next plate soon
        wxQueueEvent(this, evt.Clone());
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": restart_background_process returns %1%")%result;

    return 0;
}


void Plater::reslice_SLA_supports(const ModelObject &object, bool postpone_error_messages)
{
    reslice_SLA_until_step(slaposPad, object, postpone_error_messages);
}

void Plater::reslice_SLA_hollowing(const ModelObject &object, bool postpone_error_messages)
{
    reslice_SLA_until_step(slaposDrillHoles, object, postpone_error_messages);
}

void Plater::reslice_SLA_until_step(SLAPrintObjectStep step, const ModelObject &object, bool postpone_error_messages)
{
    //FIXME Don't reslice if export of G-code or sending to OctoPrint is running.
    // bitmask of UpdateBackgroundProcessReturnState
    unsigned int state = this->p->update_background_process(true, postpone_error_messages);
    if (state & priv::UPDATE_BACKGROUND_PROCESS_REFRESH_SCENE)
        this->p->view3D->reload_scene(false);

    if (this->p->background_process.empty() || (state & priv::UPDATE_BACKGROUND_PROCESS_INVALID))
        // Nothing to do on empty input or invalid configuration.
        return;

    // Limit calculation to the single object only.
    PrintBase::TaskParams task;
    task.single_model_object = object.id();
    // If the background processing is not enabled, calculate supports just for the single instance.
    // Otherwise calculate everything, but start with the provided object.
    if (!this->p->background_processing_enabled()) {
        task.single_model_instance_only = true;
        task.to_object_step = step;
    }
    this->p->background_process.set_task(task);
    // and let the background processing start.
    this->p->restart_background_process(state | priv::UPDATE_BACKGROUND_PROCESS_FORCE_RESTART);
}
void Plater::send_gcode_legacy(int plate_idx, Export3mfProgressFn proFn)
{
    // if physical_printer is selected, send gcode for this printer
    // DynamicPrintConfig* physical_printer_config = wxGetApp().preset_bundle->physical_printers.get_selected_printer_config();
    DynamicPrintConfig* physical_printer_config = &Slic3r::GUI::wxGetApp().preset_bundle->printers.get_edited_preset().config;
    if (! physical_printer_config || p->model.objects.empty())
        return;

    PrintHostJob upload_job(physical_printer_config);
    if (upload_job.empty())
        return;

    // Obtain default output path
    fs::path default_output_file;
    try {
        // Update the background processing, so that the placeholder parser will get the correct values for the ouput file template.
        // Also if there is something wrong with the current configuration, a pop-up dialog will be shown and the export will not be performed.
        unsigned int state = this->p->update_restart_background_process(false, false);
        if (state & priv::UPDATE_BACKGROUND_PROCESS_INVALID)
            return;
        default_output_file = this->p->background_process.output_filepath_for_project("");
    } catch (const Slic3r::PlaceholderParserError& ex) {
        // Show the error with monospaced font.
        show_error(this, ex.what(), true);
        return;
    } catch (const std::exception& ex) {
        show_error(this, ex.what(), false);
        return;
    }
    default_output_file = fs::path(Slic3r::fold_utf8_to_ascii(default_output_file.string()));

    // Repetier specific: Query the server for the list of file groups.
    wxArrayString groups;
    {
        wxBusyCursor wait;
        upload_job.printhost->get_groups(groups);
    }

    PrintHostSendDialog dlg(default_output_file, upload_job.printhost->get_post_upload_actions(), groups);
    if (dlg.ShowModal() == wxID_OK) {
        upload_job.upload_data.upload_path = dlg.filename();
        upload_job.upload_data.post_action = dlg.post_action();
        upload_job.upload_data.group       = dlg.group();

        p->export_gcode(fs::path(), false, std::move(upload_job));
    }
}
int Plater::send_gcode(int plate_idx, Export3mfProgressFn proFn)
{
    int result = 0;
    /* generate 3mf */
    if (plate_idx == PLATE_CURRENT_IDX) {
        p->m_print_job_data.plate_idx = get_partplate_list().get_curr_plate_index();
    }
    else if (plate_idx == PLATE_ALL_IDX) {
        p->m_print_job_data.plate_idx = get_partplate_list().get_curr_plate_index();
    } else {
        p->m_print_job_data.plate_idx = plate_idx;
    }

    PartPlate* plate = get_partplate_list().get_curr_plate();
    try {
        p->m_print_job_data._3mf_path = fs::path(plate->get_tmp_gcode_path());
        p->m_print_job_data._3mf_path.replace_extension("3mf");
    }
    catch (std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "generate 3mf path failed";
        return -1;
    }

    SaveStrategy strategy = SaveStrategy::Silence | SaveStrategy::SkipModel | SaveStrategy::WithGcode | SaveStrategy::SkipAuxiliary;
#if !BBL_RELEASE_TO_PUBLIC
    //only save model in QA environment
    std::string sel = get_app_config()->get("iot_environment");
    if (sel == ENV_PRE_HOST)
        strategy = SaveStrategy::Silence | SaveStrategy::SplitModel | SaveStrategy::WithGcode;
#endif

    result = export_3mf(p->m_print_job_data._3mf_path, strategy, plate_idx, proFn);

    return result;
}

int Plater::export_config_3mf(int plate_idx, Export3mfProgressFn proFn)
{
    int result = 0;
    /* generate 3mf */
    if (plate_idx == PLATE_CURRENT_IDX) {
        p->m_print_job_data.plate_idx = get_partplate_list().get_curr_plate_index();
    }
    else {
        p->m_print_job_data.plate_idx = plate_idx;
    }

    PartPlate* plate = get_partplate_list().get_curr_plate();
    try {
        p->m_print_job_data._3mf_config_path = fs::path(plate->get_temp_config_3mf_path());
    }
    catch (std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "generate 3mf path failed";
        return -1;
    }

    SaveStrategy strategy = SaveStrategy::Silence | SaveStrategy::SkipModel | SaveStrategy::WithSliceInfo | SaveStrategy::SkipAuxiliary;
    result = export_3mf(p->m_print_job_data._3mf_config_path, strategy, plate_idx, proFn);

    return result;
}

//BBS
void Plater::print_job_finished(wxCommandEvent &evt)
{
    Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;
    dev->set_selected_machine(evt.GetString().ToStdString());

    p->hide_select_machine_dlg();
    p->main_frame->request_select_tab(MainFrame::TabPosition::tpMonitor);
    //jump to monitor and select device status panel
    MonitorPanel* curr_monitor = p->main_frame->m_monitor;
    if(curr_monitor)
       curr_monitor->get_tabpanel()->ChangeSelection(MonitorPanel::PrinterTab::PT_STATUS);
}

void Plater::send_job_finished(wxCommandEvent& evt)
{
    Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;
    //dev->set_selected_machine(evt.GetString().ToStdString());

    send_gcode_finish(evt.GetString());
    p->hide_send_to_printer_dlg();
    //p->main_frame->request_select_tab(MainFrame::TabPosition::tpMonitor);
    ////jump to monitor and select device status panel
    //MonitorPanel* curr_monitor = p->main_frame->m_monitor;
    //if (curr_monitor)
    //    curr_monitor->get_tabpanel()->ChangeSelection(MonitorPanel::PrinterTab::PT_STATUS);
}

void Plater::publish_job_finished(wxCommandEvent &evt)
{
    p->m_publish_dlg->EndModal(wxID_OK);
   // GUI::wxGetApp().load_url(evt.GetString());
   //GUI::wxGetApp().open_publish_page_dialog(evt.GetString());
}

// Called when the Eject button is pressed.
void Plater::eject_drive()
{
	wxBusyCursor wait;
    wxGetApp().removable_drive_manager()->set_and_verify_last_save_path(p->last_output_dir_path);
	wxGetApp().removable_drive_manager()->eject_drive();
}

void Plater::take_snapshot(const std::string &snapshot_name) { p->take_snapshot(snapshot_name); }
//void Plater::take_snapshot(const wxString &snapshot_name) { p->take_snapshot(snapshot_name); }
void Plater::take_snapshot(const std::string &snapshot_name, UndoRedo::SnapshotType snapshot_type) { p->take_snapshot(snapshot_name, snapshot_type); }
//void Plater::take_snapshot(const wxString &snapshot_name, UndoRedo::SnapshotType snapshot_type) { p->take_snapshot(snapshot_name, snapshot_type); }
void Plater::suppress_snapshots() { p->suppress_snapshots(); }
void Plater::allow_snapshots() { p->allow_snapshots(); }
// BBS: single snapshot
void Plater::single_snapshots_enter(SingleSnapshot *single)
{
    p->single_snapshots_enter(single);
}
void Plater::single_snapshots_leave(SingleSnapshot *single)
{
    p->single_snapshots_leave(single);
}
void Plater::undo() { p->undo(); }
void Plater::redo() { p->redo(); }
void Plater::undo_to(int selection)
{
    if (selection == 0) {
        p->undo();
        return;
    }

    const int idx = p->get_active_snapshot_index() - selection - 1;
    p->undo_redo_to(p->undo_redo_stack().snapshots()[idx].timestamp);
}
void Plater::redo_to(int selection)
{
    if (selection == 0) {
        p->redo();
        return;
    }

    const int idx = p->get_active_snapshot_index() + selection + 1;
    p->undo_redo_to(p->undo_redo_stack().snapshots()[idx].timestamp);
}
bool Plater::undo_redo_string_getter(const bool is_undo, int idx, const char** out_text)
{
    const std::vector<UndoRedo::Snapshot>& ss_stack = p->undo_redo_stack().snapshots();
    const int idx_in_ss_stack = p->get_active_snapshot_index() + (is_undo ? -(++idx) : idx);

    if (0 < idx_in_ss_stack && (size_t)idx_in_ss_stack < ss_stack.size() - 1) {
        *out_text = ss_stack[idx_in_ss_stack].name.c_str();
        return true;
    }

    return false;
}

void Plater::undo_redo_topmost_string_getter(const bool is_undo, std::string& out_text)
{
    const std::vector<UndoRedo::Snapshot>& ss_stack = p->undo_redo_stack().snapshots();
    const int idx_in_ss_stack = p->get_active_snapshot_index() + (is_undo ? -1 : 0);

    if (0 < idx_in_ss_stack && (size_t)idx_in_ss_stack < ss_stack.size() - 1) {
        out_text = ss_stack[idx_in_ss_stack].name;
        return;
    }

    out_text = "";
}

bool Plater::search_string_getter(int idx, const char** label, const char** tooltip)
{
    const Search::OptionsSearcher& search_list = p->sidebar->get_searcher();

    if (0 <= idx && (size_t)idx < search_list.size()) {
        search_list[idx].get_marked_label_and_tooltip(label, tooltip);
        return true;
    }

    return false;
}

// BBS.
void Plater::on_filaments_change(size_t num_filaments)
{
    // only update elements in plater
    update_filament_colors_in_full_config();
    sidebar().on_filaments_change(num_filaments);
    sidebar().obj_list()->update_objects_list_filament_column(num_filaments);

    for (ModelObject* mo : wxGetApp().model().objects) {
        for (ModelVolume* mv : mo->volumes) {
            mv->update_extruder_count(num_filaments);
        }
    }
}

void Plater::on_bed_type_change(BedType bed_type)
{
    sidebar().on_bed_type_change(bed_type);
}

bool Plater::update_filament_colors_in_full_config()
{
    DynamicPrintConfig& project_config = wxGetApp().preset_bundle->project_config;
    ConfigOptionStrings* color_opt = project_config.option<ConfigOptionStrings>("filament_colour");

    p->config->option<ConfigOptionStrings>("filament_colour")->values = color_opt->values;
    return true;
}

void Plater::config_change_notification(const DynamicPrintConfig &config, const std::string& key)
{
    GLCanvas3D* view3d_canvas = get_view3D_canvas3D();
    if (key == std::string("print_sequence")) {
        auto seq_print = config.option<ConfigOptionEnum<PrintSequence>>("print_sequence");
        if (seq_print && view3d_canvas && view3d_canvas->is_initialized() && view3d_canvas->is_rendering_enabled()) {
            NotificationManager* notify_manager = get_notification_manager();
            if (seq_print->value == PrintSequence::ByObject) {
                std::string info_text = _u8L("Print By Object: \nSuggest to use auto-arrange to avoid collisions when printing.");
                notify_manager->bbl_show_seqprintinfo_notification(info_text);
            }
            else
                notify_manager->bbl_close_seqprintinfo_notification();
        }
    }
    // notification for more options
}

void Plater::on_config_change(const DynamicPrintConfig &config)
{
    bool update_scheduled = false;
    bool bed_shape_changed = false;
    //bool print_sequence_changed = false;
    t_config_option_keys diff_keys = p->config->diff(config);
    for (auto opt_key : diff_keys) {
        if (opt_key == "filament_colour") {
            update_scheduled = true; // update should be scheduled (for update 3DScene) #2738

            if (update_filament_colors_in_full_config()) {
                p->sidebar->obj_list()->update_filament_colors();
                continue;
            }
        }
        if (opt_key == "material_colour") {
            update_scheduled = true; // update should be scheduled (for update 3DScene)
        }

        p->config->set_key_value(opt_key, config.option(opt_key)->clone());
        if (opt_key == "printer_technology") {
            this->set_printer_technology(config.opt_enum<PrinterTechnology>(opt_key));
            // print technology is changed, so we should to update a search list
            p->sidebar->update_searcher();
            p->reset_gcode_toolpaths();
            p->view3D->get_canvas3d()->reset_sequential_print_clearance();
            //BBS: invalid all the slice results
            p->partplate_list.invalid_all_slice_result();
        }
        //BBS: add bed_exclude_area
        else if (opt_key == "printable_area" || opt_key == "bed_exclude_area"
            || opt_key == "extruder_clearance_height_to_lid"
            || opt_key == "extruder_clearance_height_to_rod") {
            bed_shape_changed = true;
            update_scheduled = true;
        }
        else if (opt_key == "bed_shape" || opt_key == "bed_custom_texture" || opt_key == "bed_custom_model") {
            bed_shape_changed = true;
            update_scheduled = true;
        }
        else if (boost::starts_with(opt_key, "enable_prime_tower") ||
            boost::starts_with(opt_key, "prime_tower") ||
            boost::starts_with(opt_key, "wipe_tower") ||
            // opt_key == "filament_minimal_purge_on_wipe_tower" // ? #ys_FIXME
            opt_key == "single_extruder_multi_material" ||
            // BBS
            opt_key == "prime_volume") {
            update_scheduled = true;
        }
        else if(opt_key == "extruder_colour") {
            update_scheduled = true;
            //p->sidebar->obj_list()->update_extruder_colors();
        }
        else if (opt_key == "printable_height") {
            bed_shape_changed = true;
            update_scheduled = true;
        }
        else if (opt_key == "print_sequence") {
            update_scheduled = true;
            //print_sequence_changed = true;
        }
        else if (opt_key == "printer_model") {
            p->reset_gcode_toolpaths();
            // update to force bed selection(for texturing)
            bed_shape_changed = true;
            update_scheduled = true;
        }
        // BBS
        else if (opt_key == "support_interface_filament" ||
            opt_key == "support_filament") {
            update_scheduled = true;
        }
    }

    if (bed_shape_changed)
        set_bed_shape();

    config_change_notification(config, std::string("print_sequence"));

    if (update_scheduled)
        update();

    if (p->main_frame->is_loaded())
        this->p->schedule_background_process();
}

void Plater::set_bed_shape() const
{
    std::string texture_filename;
    auto bundle = wxGetApp().preset_bundle;
    if (bundle != nullptr) {
        const Preset* curr = &bundle->printers.get_selected_preset();
        if (curr->is_system)
            texture_filename = PresetUtils::system_printer_bed_texture(*curr);
        else {
            auto *printer_model = curr->config.opt<ConfigOptionString>("printer_model");
            if (printer_model != nullptr && ! printer_model->value.empty()) {
                texture_filename = bundle->get_texture_for_printer_model(printer_model->value);
            }
        }
    }
    set_bed_shape(p->config->option<ConfigOptionPoints>("printable_area")->values,
        //BBS: add bed exclude areas
        p->config->option<ConfigOptionPoints>("bed_exclude_area")->values,
        p->config->option<ConfigOptionFloat>("printable_height")->value,
        p->config->option<ConfigOptionString>("bed_custom_texture")->value.empty() ? texture_filename : p->config->option<ConfigOptionString>("bed_custom_texture")->value,
        p->config->option<ConfigOptionString>("bed_custom_model")->value);
}

//BBS: add bed exclude area
void Plater::set_bed_shape(const Pointfs& shape, const Pointfs& exclude_area, const double printable_height, const std::string& custom_texture, const std::string& custom_model, bool force_as_custom) const
{
    p->set_bed_shape(shape, exclude_area, printable_height, custom_texture, custom_model, force_as_custom);
}

void Plater::force_filament_colors_update()
{
//BBS: filament_color logic has been moved out of filament setting
#if 0
    bool update_scheduled = false;
    DynamicPrintConfig* config = p->config;
    const std::vector<std::string> filament_presets = wxGetApp().preset_bundle->filament_presets;
    if (filament_presets.size() > 1 &&
        p->config->option<ConfigOptionStrings>("filament_colour")->values.size() == filament_presets.size())
    {
        const PresetCollection& filaments = wxGetApp().preset_bundle->filaments;
        std::vector<std::string> filament_colors;
        filament_colors.reserve(filament_presets.size());

        for (const std::string& filament_preset : filament_presets)
            filament_colors.push_back(filaments.find_preset(filament_preset, true)->config.opt_string("filament_colour", (unsigned)0));

        if (config->option<ConfigOptionStrings>("filament_colour")->values != filament_colors) {
            config->option<ConfigOptionStrings>("filament_colour")->values = filament_colors;
            update_scheduled = true;
        }
    }

    if (update_scheduled) {
        update();
        p->sidebar->obj_list()->update_filament_colors();
    }

    if (p->main_frame->is_loaded())
        this->p->schedule_background_process();
#endif
}

void Plater::force_print_bed_update()
{
    // Fill in the printer model key with something which cannot possibly be valid, so that Plater::on_config_change() will update the print bed
    // once a new Printer profile config is loaded.
    p->config->opt_string("printer_model", true) = "bbl_empty";
}

void Plater::on_activate()
{
    this->p->show_delayed_error_message();
}

// Get vector of extruder colors considering filament color, if extruder color is undefined.
std::vector<std::string> Plater::get_extruder_colors_from_plater_config(const GCodeProcessorResult* const result) const
{
    if (wxGetApp().is_gcode_viewer() && result != nullptr)
        return result->extruder_colors;
    else {
        const Slic3r::DynamicPrintConfig* config = &wxGetApp().preset_bundle->project_config;
        std::vector<std::string> filament_colors;
        if (!config->has("filament_colour")) // in case of a SLA print
            return filament_colors;

        filament_colors = (config->option<ConfigOptionStrings>("filament_colour"))->values;
        return filament_colors;
    }
}

/* Get vector of colors used for rendering of a Preview scene in "Color print" mode
 * It consists of extruder colors and colors, saved in model.custom_gcode_per_print_z
 */
std::vector<std::string> Plater::get_colors_for_color_print(const GCodeProcessorResult* const result) const
{
    std::vector<std::string> colors = get_extruder_colors_from_plater_config(result);

    if (wxGetApp().is_gcode_viewer() && result != nullptr) {
        for (const CustomGCode::Item& code : result->custom_gcode_per_print_z) {
            if (code.type == CustomGCode::ColorChange)
                colors.emplace_back(code.color);
        }
    }
    else {
        //BBS
        colors.reserve(colors.size() + p->model.get_curr_plate_custom_gcodes().gcodes.size());
        for (const CustomGCode::Item& code : p->model.get_curr_plate_custom_gcodes().gcodes) {
            if (code.type == CustomGCode::ColorChange)
                colors.emplace_back(code.color);
        }
    }

    return colors;
}

wxString Plater::get_project_filename(const wxString& extension) const
{
    return p->get_project_filename(extension);
}

wxString Plater::get_export_gcode_filename(const wxString & extension, bool only_filename, bool export_all) const
{
    return p->get_export_gcode_filename(extension, only_filename, export_all);
}

void Plater::set_project_filename(const wxString& filename)
{
    p->set_project_filename(filename);
}

bool Plater::is_export_gcode_scheduled() const
{
    return p->background_process.is_export_scheduled();
}

const Selection &Plater::get_selection() const
{
    return p->get_selection();
}

int Plater::get_selected_object_idx()
{
    return p->get_selected_object_idx();
}

bool Plater::is_single_full_object_selection() const
{
    return p->get_selection().is_single_full_object();
}

GLCanvas3D* Plater::canvas3D()
{
    // BBS modify view3D->get_canvas3d() to current canvas
    return p->get_current_canvas3D();
}

const GLCanvas3D* Plater::canvas3D() const
{
    // BBS modify view3D->get_canvas3d() to current canvas
    return p->get_current_canvas3D();
}

GLCanvas3D* Plater::get_view3D_canvas3D()
{
    return p->view3D->get_canvas3d();
}

GLCanvas3D* Plater::get_preview_canvas3D()
{
    return p->preview->get_canvas3d();
}

GLCanvas3D* Plater::get_assmeble_canvas3D()
{
    if (p->assemble_view)
        return p->assemble_view->get_canvas3d();
    return nullptr;
}

GLCanvas3D* Plater::get_current_canvas3D(bool exclude_preview)
{
    return p->get_current_canvas3D(exclude_preview);
}

void Plater::arrange()
{
    if (!p->m_ui_jobs.is_any_running()) {
        p->m_ui_jobs.arrange();
    }
}

void Plater::set_current_canvas_as_dirty()
{
    p->set_current_canvas_as_dirty();
}

void Plater::unbind_canvas_event_handlers()
{
    p->unbind_canvas_event_handlers();
}

void Plater::reset_canvas_volumes()
{
    p->reset_canvas_volumes();
}

PrinterTechnology Plater::printer_technology() const
{
    return p->printer_technology;
}

const DynamicPrintConfig * Plater::config() const { return p->config; }

bool Plater::set_printer_technology(PrinterTechnology printer_technology)
{
    p->printer_technology = printer_technology;
    bool ret = p->background_process.select_technology(printer_technology);
    if (ret) {
        // Update the active presets.
    }
    //FIXME for SLA synchronize
    //p->background_process.apply(Model)!

    if (printer_technology == ptSLA) {
        for (ModelObject* model_object : p->model.objects) {
            model_object->ensure_on_bed();
        }
    }

    p->label_btn_export = printer_technology == ptFFF ? L("Export G-code") : L("Export");
    p->label_btn_send   = printer_technology == ptFFF ? L("Send G-code")   : L("Send to printer");

    if (wxGetApp().mainframe != nullptr)
        wxGetApp().mainframe->update_menubar();

    p->sidebar->get_searcher().set_printer_technology(printer_technology);

    p->notification_manager->set_fff(printer_technology == ptFFF);
    p->notification_manager->set_slicing_progress_hidden();

    return ret;
}

void Plater::clear_before_change_mesh(int obj_idx)
{
    ModelObject* mo = model().objects[obj_idx];

    // If there are custom supports/seams/mmu segmentation, remove them. Fixed mesh
    // may be different and they would make no sense.
    bool paint_removed = false;
    for (ModelVolume* mv : mo->volumes) {
        paint_removed |= ! mv->supported_facets.empty() || ! mv->seam_facets.empty() || ! mv->mmu_segmentation_facets.empty();
        mv->supported_facets.reset();
        mv->seam_facets.reset();
        mv->mmu_segmentation_facets.reset();
    }
    if (paint_removed) {
        // snapshot_time is captured by copy so the lambda knows where to undo/redo to.
        get_notification_manager()->push_notification(
                    NotificationType::CustomSupportsAndSeamRemovedAfterRepair,
                    NotificationManager::NotificationLevel::PrintInfoNotificationLevel,
                    _u8L("Custom supports and color painting were removed before repairing."));
    }
}

void Plater::changed_mesh(int obj_idx)
{
    ModelObject* mo = model().objects[obj_idx];
    sla::reproject_points_and_holes(mo);
    update();
    p->object_list_changed();
    p->schedule_background_process();
}

void Plater::changed_object(int obj_idx)
{
    if (obj_idx < 0)
        return;
    // recenter and re - align to Z = 0
    p->model.objects[obj_idx]->ensure_on_bed(p->printer_technology != ptSLA);
    if (this->p->printer_technology == ptSLA) {
        // Update the SLAPrint from the current Model, so that the reload_scene()
        // pulls the correct data, update the 3D scene.
        this->p->update_restart_background_process(true, false);
    }
    else
        p->view3D->reload_scene(false);

    // update print
    this->p->schedule_background_process();
}

void Plater::changed_objects(const std::vector<size_t>& object_idxs)
{
    if (object_idxs.empty())
        return;

    for (size_t obj_idx : object_idxs) {
        if (obj_idx < p->model.objects.size()) {
            if (p->model.objects[obj_idx]->bounding_box().min.z() >= SINKING_Z_THRESHOLD)
                // re - align to Z = 0
                p->model.objects[obj_idx]->ensure_on_bed();
        }
    }
    if (this->p->printer_technology == ptSLA) {
        // Update the SLAPrint from the current Model, so that the reload_scene()
        // pulls the correct data, update the 3D scene.
        this->p->update_restart_background_process(true, false);
    }
    else {
        p->view3D->reload_scene(false);
        p->view3D->get_canvas3d()->update_instance_printable_state_for_objects(object_idxs);
    }

    // update print
    this->p->schedule_background_process();
}

void Plater::schedule_background_process(bool schedule/* = true*/)
{
    if (schedule)
        this->p->schedule_background_process();

    this->p->suppressed_backround_processing_update = false;
}

bool Plater::is_background_process_update_scheduled() const
{
    return this->p->background_process_timer.IsRunning();
}

void Plater::suppress_background_process(const bool stop_background_process)
{
    if (stop_background_process)
        this->p->background_process_timer.Stop();

    this->p->suppressed_backround_processing_update = true;
}

void Plater::center_selection()     { p->center_selection(); }
void Plater::mirror(Axis axis)      { p->mirror(axis); }
void Plater::split_object()         { p->split_object(); }
void Plater::split_volume()         { p->split_volume(); }
void Plater::optimize_rotation()    { if (!p->m_ui_jobs.is_any_running()) p->m_ui_jobs.optimize_rotation(); }
void Plater::update_menus()         { p->menus.update(); }
// BBS
//void Plater::show_action_buttons(const bool ready_to_slice) const   { p->show_action_buttons(ready_to_slice); }

void Plater::fill_color(int extruder_id)
{
    if (can_fillcolor()) {
        p->assemble_view->get_canvas3d()->get_selection().fill_color(extruder_id);
    }
}

//BBS
void Plater::cut_selection_to_clipboard()
{
    Plater::TakeSnapshot snapshot(this, "Cut Selected Objects");
    if (can_cut_to_clipboard() && !p->sidebar->obj_list()->cut_to_clipboard()) {
        p->view3D->get_canvas3d()->get_selection().cut_to_clipboard();
    }
}

void Plater::copy_selection_to_clipboard()
{
    // At first try to copy selected values to the ObjectList's clipboard
    // to check if Settings or Layers are selected in the list
    // and then copy to 3DCanvas's clipboard if not
    if (can_copy_to_clipboard() && !p->sidebar->obj_list()->copy_to_clipboard())
        p->view3D->get_canvas3d()->get_selection().copy_to_clipboard();
}

void Plater::paste_from_clipboard()
{
    if (!can_paste_from_clipboard())
        return;

    Plater::TakeSnapshot snapshot(this, "Paste From Clipboard");

    // At first try to paste values from the ObjectList's clipboard
    // to check if Settings or Layers were copied
    // and then paste from the 3DCanvas's clipboard if not
    if (!p->sidebar->obj_list()->paste_from_clipboard())
        p->view3D->get_canvas3d()->get_selection().paste_from_clipboard();
}

//BBS: add clone
void Plater::clone_selection()
{
    if (is_selection_empty())
        return;
    long res = wxGetNumberFromUser("",
        _L("Clone"),
        _L("Number of copies:"),
        1, 0, 100, this);
    wxString msg;
    if (res == -1) {
        msg = _L("Invalid number");
        return;
    }
    Selection& selection = p->get_selection();
    selection.clone(res);
}

void Plater::search(bool plater_is_active, Preset::Type type, wxWindow *tag, TextInput *etag, wxWindow *stag)
{
    if (plater_is_active) {
        if (is_preview_shown())
            return;
        // plater should be focused for correct navigation inside search window
        this->SetFocus();

        wxKeyEvent evt;
#ifdef __APPLE__
        evt.m_keyCode = 'f';
#else /* __APPLE__ */
        evt.m_keyCode = WXK_CONTROL_F;
#endif /* __APPLE__ */
        evt.SetControlDown(true);
        canvas3D()->on_char(evt);
    }
    else
        p->sidebar->get_searcher().show_dialog(type, tag, etag, stag);
}

void Plater::msw_rescale()
{
    p->preview->msw_rescale();

    p->view3D->get_canvas3d()->msw_rescale();

    p->sidebar->msw_rescale();

    p->menus.msw_rescale();

    Layout();
    GetParent()->Layout();
}

void Plater::sys_color_changed()
{
    p->preview->sys_color_changed();
    p->sidebar->sys_color_changed();
    p->menus.sys_color_changed();

    Layout();
    GetParent()->Layout();
}

// BBS
#if 0
bool Plater::init_view_toolbar()
{
    return p->init_view_toolbar();
}

void Plater::enable_view_toolbar(bool enable)
{
    p->view_toolbar.set_enabled(enable);
}
#endif

bool Plater::init_collapse_toolbar()
{
    return p->init_collapse_toolbar();
}

void Plater::enable_collapse_toolbar(bool enable)
{
    p->collapse_toolbar.set_enabled(enable);
}

const Camera& Plater::get_camera() const
{
    return p->camera;
}

Camera& Plater::get_camera()
{
    return p->camera;
}

//BBS: partplate list related functions
PartPlateList& Plater::get_partplate_list()
{
    return p->partplate_list;
}

void Plater::apply_background_progress()
{
    PartPlate* part_plate = p->partplate_list.get_curr_plate();
    int plate_index = p->partplate_list.get_curr_plate_index();
    bool result_valid = part_plate->is_slice_result_valid();
    //always apply the current plate's print
    Print::ApplyStatus invalidated = p->background_process.apply(this->model(), wxGetApp().preset_bundle->full_config());

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" %1%: plate %2%, after apply, invalidated= %3%, previous result_valid %4% ") % __LINE__ % plate_index % invalidated % result_valid;
    if (invalidated & PrintBase::APPLY_STATUS_INVALIDATED)
    {
        part_plate->update_slice_result_valid_state(false);
        //p->ready_to_slice = true;
        p->main_frame->update_slice_print_status(MainFrame::eEventPlateUpdate, true);
    }
}

//BBS: select Plate
int Plater::select_plate(int plate_index, bool need_slice)
{
    int ret;
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" %1%: plate %2%, need_slice %3% ")%__LINE__ %plate_index  %need_slice;
    take_snapshot("select partplate!");
    ret = p->partplate_list.select_plate(plate_index);
    if (!ret) {
        if (is_view3D_shown())
            wxGetApp().plater()->canvas3D()->render();
    }

    if ((!ret) && (p->background_process.can_switch_print()))
    {
        //select successfully
        p->partplate_list.update_slice_context_to_current_plate(p->background_process);
        p->preview->update_gcode_result(p->partplate_list.get_current_slice_result());
        p->update_print_volume_state();

        PartPlate* part_plate = p->partplate_list.get_curr_plate();
        bool result_valid = part_plate->is_slice_result_valid();
        PrintBase* print = nullptr;
        GCodeResult* gcode_result = nullptr;
        Print::ApplyStatus invalidated;

        part_plate->get_print(&print, &gcode_result, NULL);

        //always apply the current plate's print
        invalidated = p->background_process.apply(this->model(), wxGetApp().preset_bundle->full_config());
        bool model_fits, validate_err;

        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" %1%: plate %2%, after apply, invalidated= %3%, previous result_valid %4% ")%__LINE__ %plate_index  %invalidated %result_valid;
        if (result_valid)
        {
            if (is_preview_shown())
            {
                if (need_slice) { //from preview's thumbnail
                    if ((invalidated & PrintBase::APPLY_STATUS_INVALIDATED) || (gcode_result->moves.empty())){
                        //part_plate->update_slice_result_valid_state(false);
                        p->process_completed_with_error = -1;
                        p->m_slice_all = false;
                        reset_gcode_toolpaths();
                        reslice();
                    }
                    else {
                        validate_current_plate(model_fits, validate_err);
                        //just refresh_print
                        refresh_print();
                        p->main_frame->update_slice_print_status(MainFrame::eEventPlateUpdate, false, true);
                    }
                }
                else {// from multiple slice's next
                    //do nothing
                }
            }
            else
            {
                validate_current_plate(model_fits, validate_err);
                if (invalidated & PrintBase::APPLY_STATUS_INVALIDATED)
                {
                    part_plate->update_slice_result_valid_state(false);
                    // BBS
                    //p->show_action_buttons(true);
                    //p->ready_to_slice = true;
                    p->main_frame->update_slice_print_status(MainFrame::eEventPlateUpdate, true);
                }
                else
                {
                    // BBS
                    //p->show_action_buttons(false);
                    //p->ready_to_slice = false;
                    p->main_frame->update_slice_print_status(MainFrame::eEventPlateUpdate, false);

                    refresh_print();
                }
            }
        }
        else
        {
            //check inside status
            //model_fits = p->view3D->get_canvas3d()->check_volumes_outside_state() != ModelInstancePVS_Partly_Outside;
            //bool validate_err = false;
            validate_current_plate(model_fits, validate_err);
            if (model_fits && !validate_err) {
                p->process_completed_with_error = -1;
            }
            else {
                p->process_completed_with_error = p->partplate_list.get_curr_plate_index();
            }
            if (is_preview_shown())
            {
                if (need_slice)
                {
                    //p->process_completed_with_error = -1;
                    p->m_slice_all = false;
                    reset_gcode_toolpaths();
                    if (model_fits && !validate_err)
                        reslice();
                    else {
                        p->main_frame->update_slice_print_status(MainFrame::eEventPlateUpdate, false);
                        //sometimes the previous print's sliced result is still valid, but the newly added object is laid over the boundary
                        //then the print toolpath will be shown, so we should not refresh print here, only onload shell
                        //refresh_print();
                        p->update_fff_scene_only_shells();
                    }
                }
                else {
                    //p->ready_to_slice = false;
                    p->main_frame->update_slice_print_status(MainFrame::eEventPlateUpdate, false);
                    refresh_print();
                }
            }
            else
            {
                //validate_current_plate(model_fits, validate_err);
                //check inside status
                /*if (model_fits && !validate_err){
                    p->process_completed_with_error = -1;
                }
                else {
                    p->process_completed_with_error = p->partplate_list.get_curr_plate_index();
                }*/

                // BBS: don't show action buttons
                //p->show_action_buttons(true);
                //p->ready_to_slice = true;
                if (model_fits && part_plate->has_printable_instances())
                {
                    //p->view3D->get_canvas3d()->post_event(Event<bool>(EVT_GLCANVAS_ENABLE_ACTION_BUTTONS, true));
                    p->main_frame->update_slice_print_status(MainFrame::eEventPlateUpdate, true);
                }
                else
                {
                    //p->view3D->get_canvas3d()->post_event(Event<bool>(EVT_GLCANVAS_ENABLE_ACTION_BUTTONS, false));
                    p->main_frame->update_slice_print_status(MainFrame::eEventPlateUpdate, false);
                }
            }
        }
    }

    SimpleEvent event(EVT_GLCANVAS_PLATE_SELECT);
    p->on_plate_selected(event);

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" %1%: plate %2%, return %3%")%__LINE__ %plate_index %ret;
    return ret;
}

int Plater::select_sliced_plate(int plate_index)
{
    int ret = 0;
    BOOST_LOG_TRIVIAL(info) << "select_sliced_plate plate_idx=" << plate_index;

    Freeze();
    ret = select_plate(plate_index, true);
    if (ret)
    {
        BOOST_LOG_TRIVIAL(error) << "select_plate error for plate_idx=" << plate_index;
        Thaw();
        return -1;
    }
    p->partplate_list.select_plate_view();
    Thaw();

    return ret;
}

void Plater::validate_current_plate(bool& model_fits, bool& validate_error)
{
    model_fits = p->view3D->get_canvas3d()->check_volumes_outside_state() != ModelInstancePVS_Partly_Outside;
    validate_error = false;
    if (p->printer_technology == ptFFF) {
        std::string plater_text = _u8L("An object is laid over the boundary of plate or exceeds the height limit.\n"
                    "Please solve the problem by moving it totally on or off the plate, and confirming that the height is within the build volume.");;
        StringObjectException warning;
        Polygons polygons;
        std::vector<std::pair<Polygon, float>> height_polygons;
        StringObjectException err = p->background_process.validate(&warning, &polygons, &height_polygons);
        // update string by type
        post_process_string_object_exception(err);
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": validate err=%1%, warning=%2%, model_fits %3%")%err.string%warning.string %model_fits;

        if (err.string.empty()) {
            p->partplate_list.get_curr_plate()->update_apply_result_invalid(false);
            p->notification_manager->set_all_slicing_errors_gray(true);
            p->notification_manager->close_notification_of_type(NotificationType::ValidateError);

            // Pass a warning from validation and either show a notification,
            // or hide the old one.
            p->process_validation_warning(warning);
            p->view3D->get_canvas3d()->reset_sequential_print_clearance();
            p->view3D->get_canvas3d()->set_as_dirty();
            p->view3D->get_canvas3d()->request_extra_frame();
        }
        else {
            // The print is not valid.
            p->partplate_list.get_curr_plate()->update_apply_result_invalid(true);
            // Show error as notification.
            p->notification_manager->push_validate_error_notification(err);
            p->process_validation_warning(warning);
            //model_fits = false;
            validate_error = true;
            p->view3D->get_canvas3d()->set_sequential_print_clearance_visible(true);
            p->view3D->get_canvas3d()->set_sequential_print_clearance_render_fill(true);
            p->view3D->get_canvas3d()->set_sequential_print_clearance_polygons(polygons, height_polygons);
        }

        if (!model_fits) {
            p->notification_manager->push_plater_error_notification(plater_text);
        }
        else {
            p->notification_manager->close_plater_error_notification(plater_text);
        }
    }

    PartPlate* part_plate = p->partplate_list.get_curr_plate();
    part_plate->update_slice_ready_status(model_fits);

    return;
}


//BBS: select Plate by hover_id
int Plater::select_plate_by_hover_id(int hover_id, bool right_click)
{
    int ret;
    int action, plate_index;

    plate_index = hover_id / PartPlate::GRABBER_COUNT;
    action = hover_id % PartPlate::GRABBER_COUNT;

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": enter, hover_id %1%, plate_index %2%, action %3%")%hover_id % plate_index %action;
    if (action == 0)
    {
        //select plate
        ret = p->partplate_list.select_plate(plate_index);
        if (!ret) {
            SimpleEvent event(EVT_GLCANVAS_PLATE_SELECT);
            p->on_plate_selected(event);
        }
        if ((!ret)&&(p->background_process.can_switch_print()))
        {
            //select successfully
            p->partplate_list.update_slice_context_to_current_plate(p->background_process);
            p->preview->update_gcode_result(p->partplate_list.get_current_slice_result());
            p->update_print_volume_state();

            PartPlate* part_plate = p->partplate_list.get_curr_plate();
            bool result_valid = part_plate->is_slice_result_valid();
            PrintBase* print = nullptr;
            GCodeResult* gcode_result = nullptr;
            Print::ApplyStatus invalidated;

            part_plate->get_print(&print, &gcode_result, NULL);
            //always apply the current plate's print
            invalidated = p->background_process.apply(this->model(), wxGetApp().preset_bundle->full_config());
            bool model_fits, validate_err;
            validate_current_plate(model_fits, validate_err);

            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" %1%: after apply, invalidated= %2%, previous result_valid %3% ")%__LINE__ % invalidated %result_valid;
            if (result_valid)
            {
                if (invalidated & PrintBase::APPLY_STATUS_INVALIDATED)
                {
                    //bool model_fits, validate_err;
                    //validate_current_plate(model_fits, validate_err);
                    part_plate->update_slice_result_valid_state(false);

                    // BBS
                    //p->show_action_buttons(true);
                    //p->ready_to_slice = true;
                    p->main_frame->update_slice_print_status(MainFrame::eEventPlateUpdate, true);
                }
                else
                {
                    // BBS
                    //p->show_action_buttons(false);
                    //validate_current_plate(model_fits, validate_err);
                    //p->ready_to_slice = false;
                    p->main_frame->update_slice_print_status(MainFrame::eEventPlateUpdate, false);

                    refresh_print();
                }
            }
            else
            {
                //check inside status
                if (model_fits && !validate_err){
                    p->process_completed_with_error = -1;
                }
                else {
                    p->process_completed_with_error = p->partplate_list.get_curr_plate_index();
                }

                // BBS: don't show action buttons
                //p->show_action_buttons(true);
                //p->ready_to_slice = true;
                if (model_fits && part_plate->has_printable_instances())
                {
                    //p->view3D->get_canvas3d()->post_event(Event<bool>(EVT_GLCANVAS_ENABLE_ACTION_BUTTONS, true));
                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": will set can_slice to true");
                    p->main_frame->update_slice_print_status(MainFrame::eEventPlateUpdate, true);
                }
                else
                {
                    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": will set can_slice to false, has_printable_instances %1%")%part_plate->has_printable_instances();
                    //p->view3D->get_canvas3d()->post_event(Event<bool>(EVT_GLCANVAS_ENABLE_ACTION_BUTTONS, false));
                    p->main_frame->update_slice_print_status(MainFrame::eEventPlateUpdate, false);
                }
            }
        }
    }
    else if ((action == 1)&&(!right_click))
    {
        //delete plate
        ret = delete_plate(plate_index);
    }
    else if ((action == 2)&&(!right_click))
    {
        //arrange the plate
        //take_snapshot("select_orient partplate");
        ret = select_plate(plate_index);
        if (!ret)
        {
            set_prepare_state(Job::PREPARE_STATE_MENU);
            orient();
        }
        else
        {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "can not select plate %1%" << plate_index;
            ret = -1;
        }
    }
    else if ((action == 3)&&(!right_click))
    {
        //arrange the plate
        //take_snapshot("select_arrange partplate");
        ret = select_plate(plate_index);
        if (!ret)
        {
            set_prepare_state(Job::PREPARE_STATE_MENU);
            arrange();
        }
        else
        {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "can not select plate %1%" << plate_index;
            ret = -1;
        }
    }
    else if ((action == 4)&&(!right_click))
    {
        //lock the plate
        take_snapshot("lock partplate");
        ret = p->partplate_list.lock_plate(plate_index, !p->partplate_list.is_locked(plate_index));
    }
    else if ((action == 5)&&(!right_click))
    {
        //set the plate type
        ret = select_plate(plate_index);
        if (!ret) {
            PlateSettingsDialog dlg(this, wxID_ANY, _L("Plate Settings"));
            PartPlate* curr_plate = p->partplate_list.get_curr_plate();
            dlg.sync_bed_type(curr_plate->get_bed_type());

            auto curr_print_seq = curr_plate->get_print_seq();
            if (curr_print_seq != PrintSequence::ByDefault) {
                dlg.sync_print_seq(int(curr_print_seq) + 1);
            }
            else
                dlg.sync_print_seq(0);

            dlg.set_plate_name(curr_plate->get_plate_name());

            dlg.Bind(EVT_SET_BED_TYPE_CONFIRM, [this, plate_index, &dlg](wxCommandEvent& e) {
                PartPlate *curr_plate = p->partplate_list.get_curr_plate();
                BedType old_bed_type = curr_plate->get_bed_type();
                auto bt_sel = BedType(dlg.get_bed_type_choice());
                if (old_bed_type != bt_sel) {
                    curr_plate->set_bed_type(bt_sel);
                    update_project_dirty_from_presets();
                    set_plater_dirty(true);
                }
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format("select bed type %1% for plate %2% at plate side")%bt_sel %plate_index;

                int ps_sel = dlg.get_print_seq_choice();
                if (ps_sel != 0)
                    curr_plate->set_print_seq(PrintSequence(ps_sel - 1));
                else
                    curr_plate->set_print_seq(PrintSequence::ByDefault);
                update_project_dirty_from_presets();
                set_plater_dirty(true);
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format("select print sequence %1% for plate %2% at plate side")%ps_sel %plate_index;
                auto plate_config = *(curr_plate->config());
                wxGetApp().plater()->config_change_notification(plate_config, std::string("print_sequence"));
                });
            dlg.ShowModal();
            curr_plate->set_plate_name(dlg.get_plate_name().ToStdString());

            this->schedule_background_process();
        }
        else {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "can not select plate %1%" << plate_index;
            ret = -1;
        }
    }
    else
    {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "invalid action %1%, with right_click=%2%" << action << right_click;
        ret = -1;
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(" %1%: return %2%")%__LINE__ % ret;
    return ret;
}

//BBS: delete the plate, index= -1 means the current plate
int Plater::delete_plate(int plate_index)
{
    int index = plate_index, ret;

    if (plate_index == -1)
        index = p->partplate_list.get_curr_plate_index();

    take_snapshot("delete partplate");
    ret = p->partplate_list.delete_plate(index);

    //BBS: update the current print to the current plate
    p->partplate_list.update_slice_context_to_current_plate(p->background_process);
    p->preview->update_gcode_result(p->partplate_list.get_current_slice_result());
    p->sidebar->obj_list()->reload_all_plates();

    // BBS update default view
    //get_camera().select_view("topfront");
    //get_camera().requires_zoom_to_plate = REQUIRES_ZOOM_TO_ALL_PLATE;

    //need to call update
    update();
    return ret;
}

//BBS: set bed positions
void Plater::set_bed_position(Vec2d& pos)
{
    p->bed.set_position(pos);
}

//BBS: is the background process slicing currently
bool Plater::is_background_process_slicing() const
{
    return p->m_is_slicing;
}

//BBS: update slicing context
void Plater::update_slicing_context_to_current_partplate()
{
    p->partplate_list.update_slice_context_to_current_plate(p->background_process);
    p->preview->update_gcode_result(p->partplate_list.get_current_slice_result());
}

//BBS: show object info
void Plater::show_object_info()
{
    NotificationManager *notify_manager = get_notification_manager();
    const Selection& selection = get_selection();
    ModelObjectPtrs objects = model().objects;
    int obj_idx = selection.get_object_idx();
    std::string info_text;

    if (objects.empty() || (obj_idx < 0) || (obj_idx >= objects.size()) ||
        objects[obj_idx]->volumes.empty() ||// hack to avoid crash when deleting the last object on the bed
        (selection.is_single_full_object() && objects[obj_idx]->instances.size()> 1) ||
        !(selection.is_single_full_instance() || selection.is_single_volume()))
    {
        notify_manager->bbl_close_objectsinfo_notification();
        return;
    }

    const ModelObject* model_object = objects[obj_idx];
    int inst_idx = selection.get_instance_idx();
    if ((inst_idx < 0) || (inst_idx >= model_object->instances.size()))
    {
        notify_manager->bbl_close_objectsinfo_notification();
        return;
    }
    bool imperial_units = wxGetApp().app_config->get("use_inches") == "1";
    double koef = imperial_units ? GizmoObjectManipulation::mm_to_in : 1.0f;

    ModelVolume* vol = nullptr;
    Transform3d t;
    int face_count;
    Vec3d size;
    if (selection.is_single_volume()) {
        std::vector<int> obj_idxs, vol_idxs;
        wxGetApp().obj_list()->get_selection_indexes(obj_idxs, vol_idxs);
        if (vol_idxs.size() != 1)
        {
            //corner case when merge/split/remove
            return;
        }
        vol = model_object->volumes[vol_idxs[0]];
        t = model_object->instances[inst_idx]->get_matrix() * vol->get_matrix();
        info_text += (boost::format(_utf8(L("Part name: %1%\n"))) % vol->name).str();
        face_count = static_cast<int>(vol->mesh().facets_count());
        size = vol->get_convex_hull().transformed_bounding_box(t).size();
    }
    else {
        int obj_idx, vol_idx;
        wxGetApp().obj_list()->get_selected_item_indexes(obj_idx, vol_idx);
        if (obj_idx < 0) {
            //corner case when merge/split/remove
            return;
        }
        info_text += (boost::format(_utf8(L("Object name: %1%\n"))) % model_object->name).str();
        face_count = static_cast<int>(model_object->facets_count());
        size = model_object->instance_convex_hull_bounding_box(inst_idx).size();
    }

    //Vec3d size = vol ? vol->mesh().transformed_bounding_box(t).size() : model_object->instance_bounding_box(inst_idx).size();
    if (imperial_units)
        info_text += (boost::format(_utf8(L("Size: %1% x %2% x %3% in\n"))) %(size(0)*koef) %(size(1)*koef) %(size(2)*koef)).str();
    else
        info_text += (boost::format(_utf8(L("Size: %1% x %2% x %3% mm\n"))) %size(0) %size(1) %size(2)).str();

    const TriangleMeshStats& stats = vol ? vol->mesh().stats() : model_object->get_object_stl_stats();
    double volume_val = stats.volume;
    if (vol)
        volume_val *= std::fabs(t.matrix().block(0, 0, 3, 3).determinant());
    volume_val = volume_val * pow(koef,3);
    if (imperial_units)
        info_text += (boost::format(_utf8(L("Volume: %1% in³\n"))) %volume_val).str();
    else
        info_text += (boost::format(_utf8(L("Volume: %1% mm³\n"))) %volume_val).str();
    info_text += (boost::format(_utf8(L("Triangles: %1%\n"))) %face_count).str();

    wxString info_manifold;
    int non_manifold_edges = 0;
    auto mesh_errors = p->sidebar->obj_list()->get_mesh_errors_info(&info_manifold, &non_manifold_edges);

    #ifndef __WINDOWS__
    if (non_manifold_edges > 0) {
        info_manifold += into_u8("\n" + _L("Tips:") + "\n" +_L("\"Fix Model\" feature is currently only on Windows. Please repair the model on Orca Slicer(windows) or CAD softwares."));
    }
    #endif //APPLE & LINUX

    info_manifold = "<Error>" + info_manifold + "</Error>";
    info_text += into_u8(info_manifold);
    notify_manager->bbl_show_objectsinfo_notification(info_text, is_windows10()&&(non_manifold_edges > 0), !(p->current_panel == p->view3D));
}

bool Plater::show_publish_dialog(bool show)
{
    return p->show_publish_dlg(show);
}

void Plater::post_process_string_object_exception(StringObjectException &err)
{
    if (err.type == StringExceptionType::STRING_EXCEPT_FILAMENT_NOT_MATCH_BED_TYPE) {
        try {
            int extruder_id = atoi(err.params[2].c_str()) - 1;
            if (extruder_id < wxGetApp().preset_bundle->filament_presets.size()) {
                std::string filament_name = wxGetApp().preset_bundle->filament_presets[extruder_id];
                for (auto filament : wxGetApp().preset_bundle->filaments) {
                    if (filament.name == filament_name) {
                        filament_name = filament.alias;
                        break;
                    }
                }
                err.string = format(_L("Plate% d: %s is not suggested to be used to print filament %s(%s). If you still want to do this printing, please set this filament's bed temperature to non zero."),
                             err.params[0], err.params[1], err.params[2], filament_name);
                err.string += "\n";
            }
        } catch (...) {
            ;
        }
    }

    return;
}

#if ENABLE_ENVIRONMENT_MAP
void Plater::init_environment_texture()
{
    if (p->environment_texture.get_id() == 0)
        p->environment_texture.load_from_file(resources_dir() + "/images/Pmetal_001.png", false, GLTexture::SingleThreaded, false);
}

unsigned int Plater::get_environment_texture_id() const
{
    return p->environment_texture.get_id();
}
#endif // ENABLE_ENVIRONMENT_MAP

const BuildVolume& Plater::build_volume() const
{
    return p->bed.build_volume();
}

// BBS
#if 0
const GLToolbar& Plater::get_view_toolbar() const
{
    return p->view_toolbar;
}

GLToolbar& Plater::get_view_toolbar()
{
    return p->view_toolbar;
}
#endif

const GLToolbar& Plater::get_collapse_toolbar() const
{
    return p->collapse_toolbar;
}

GLToolbar& Plater::get_collapse_toolbar()
{
    return p->collapse_toolbar;
}

void Plater::update_preview_bottom_toolbar()
{
    p->update_preview_bottom_toolbar();
}

void Plater::reset_gcode_toolpaths()
{
    //BBS: add some logs
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": reset the gcode viewer's toolpaths");
    p->reset_gcode_toolpaths();
}

const Mouse3DController& Plater::get_mouse3d_controller() const
{
    return p->mouse3d_controller;
}

Mouse3DController& Plater::get_mouse3d_controller()
{
    return p->mouse3d_controller;
}

NotificationManager * Plater::get_notification_manager()
{
    return p->notification_manager.get();
}

const NotificationManager * Plater::get_notification_manager() const
{
    return p->notification_manager.get();
}

void Plater::init_notification_manager()
{
    p->init_notification_manager();
}

void Plater::show_status_message(std::string s)
{
    BOOST_LOG_TRIVIAL(trace) << "show_status_message:" << s;
}

void Plater::edit_text()
{
    auto &manager = get_view3D_canvas3D()->get_gizmos_manager();
    manager.open_gizmo(GLGizmosManager::Text);
    update();
}

bool Plater::can_edit_text() const { return p->can_edit_text(); }
bool Plater::can_delete() const { return p->can_delete(); }
bool Plater::can_delete_all() const { return p->can_delete_all(); }
bool Plater::can_add_model() const { return !is_background_process_slicing(); }
bool Plater::can_add_plate() const { return !is_background_process_slicing() && p->can_add_plate(); }
bool Plater::can_delete_plate() const { return p->can_delete_plate(); }
bool Plater::can_increase_instances() const { return p->can_increase_instances(); }
bool Plater::can_decrease_instances() const { return p->can_decrease_instances(); }
bool Plater::can_set_instance_to_object() const { return p->can_set_instance_to_object(); }
bool Plater::can_fix_through_netfabb() const { return p->can_fix_through_netfabb(); }
bool Plater::can_simplify() const { return p->can_simplify(); }
bool Plater::can_split_to_objects() const { return p->can_split_to_objects(); }
bool Plater::can_split_to_volumes() const { return p->can_split_to_volumes(); }
bool Plater::can_arrange() const { return p->can_arrange(); }
bool Plater::can_layers_editing() const { return p->can_layers_editing(); }
bool Plater::can_paste_from_clipboard() const
{
    if (!IsShown() || !p->is_view3D_shown()) return false;

    const Selection& selection = p->view3D->get_canvas3d()->get_selection();
    const Selection::Clipboard& clipboard = selection.get_clipboard();

    if (clipboard.is_empty() && p->sidebar->obj_list()->clipboard_is_empty())
        return false;

    if ((wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA) && !clipboard.is_sla_compliant())
        return false;

    Selection::EMode mode = clipboard.get_mode();
    if ((mode == Selection::Volume) && !selection.is_from_single_instance())
        return false;

    if ((mode == Selection::Instance) && (selection.get_mode() != Selection::Instance))
        return false;

    return true;
}

//BBS support cut
bool Plater::can_cut_to_clipboard() const
{
    if (is_selection_empty())
        return false;
    return true;
}

bool Plater::can_copy_to_clipboard() const
{
    if (!IsShown() || !p->is_view3D_shown())
        return false;

    if (is_selection_empty())
        return false;

    const Selection& selection = p->view3D->get_canvas3d()->get_selection();
    if ((wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA) && !selection.is_sla_compliant())
        return false;

    return true;
}

bool Plater::can_undo() const { return IsShown() && p->is_view3D_shown() && p->undo_redo_stack().has_undo_snapshot(); }
bool Plater::can_redo() const { return IsShown() && p->is_view3D_shown() && p->undo_redo_stack().has_redo_snapshot(); }
bool Plater::can_reload_from_disk() const { return p->can_reload_from_disk(); }
//BBS
bool Plater::can_fillcolor() const { return p->can_fillcolor(); }
bool Plater::has_assmeble_view() const { return p->has_assemble_view(); }
bool Plater::can_replace_with_stl() const { return p->can_replace_with_stl(); }
bool Plater::can_mirror() const { return p->can_mirror(); }
bool Plater::can_split(bool to_objects) const { return p->can_split(to_objects); }
#if ENABLE_ENHANCED_PRINT_VOLUME_FIT
bool Plater::can_scale_to_print_volume() const { return p->can_scale_to_print_volume(); }
#endif // ENABLE_ENHANCED_PRINT_VOLUME_FIT

const UndoRedo::Stack& Plater::undo_redo_stack_main() const { return p->undo_redo_stack_main(); }
void Plater::clear_undo_redo_stack_main() { p->undo_redo_stack_main().clear(); }
void Plater::enter_gizmos_stack() { p->enter_gizmos_stack(); }
bool Plater::leave_gizmos_stack() { return p->leave_gizmos_stack(); } // BBS: return false if not changed
bool Plater::inside_snapshot_capture() { return p->inside_snapshot_capture(); }

void Plater::toggle_render_statistic_dialog()
{
    p->show_render_statistic_dialog = !p->show_render_statistic_dialog;
}

bool Plater::is_render_statistic_dialog_visible() const
{
    return p->show_render_statistic_dialog;
}

void Plater::toggle_show_wireframe()
{
    p->show_wireframe = !p->show_wireframe;
}

bool Plater::is_show_wireframe() const
{
    return p->show_wireframe;
}

void Plater::enable_wireframe(bool status)
{
    p->wireframe_enabled = status;
}

bool Plater::is_wireframe_enabled() const
{
    return p->wireframe_enabled;
}


/*Plater::TakeSnapshot::TakeSnapshot(Plater *plater, const std::string &snapshot_name)
: TakeSnapshot(plater, from_u8(snapshot_name)) {}
Plater::TakeSnapshot::TakeSnapshot(Plater* plater, const std::string& snapshot_name, UndoRedo::SnapshotType snapshot_type)
: TakeSnapshot(plater, from_u8(snapshot_name), snapshot_type) {}*/


// Wrapper around wxWindow::PopupMenu to suppress error messages popping out while tracking the popup menu.
bool Plater::PopupMenu(wxMenu *menu, const wxPoint& pos)
{
    // Don't want to wake up and trigger reslicing while tracking the pop-up menu.
    SuppressBackgroundProcessingUpdate sbpu;
    // When tracking a pop-up menu, postpone error messages from the slicing result.
    m_tracking_popup_menu = true;
    bool out = this->wxPanel::PopupMenu(menu, pos);
    m_tracking_popup_menu = false;
    if (! m_tracking_popup_menu_error_message.empty()) {
        // Don't know whether the CallAfter is necessary, but it should not hurt.
        // The menus likely sends out some commands, so we may be safer if the dialog is shown after the menu command is processed.
        wxString message = std::move(m_tracking_popup_menu_error_message);
        wxTheApp->CallAfter([message, this]() { show_error(this, message); });
        m_tracking_popup_menu_error_message.clear();
    }
    return out;
}
void Plater::bring_instance_forward()
{
    p->bring_instance_forward();
}

bool Plater::need_update() const
{
    return p->need_update();
}

void Plater::set_need_update(bool need_update)
{
    p->set_need_update(need_update);
}

// BBS
//BBS: add popup logic for table object
bool Plater::PopupObjectTable(int object_id, int volume_id, const wxPoint& position)
{
    return p->PopupObjectTable(object_id, volume_id, position);
}

bool Plater::PopupObjectTableBySelection()
{
    wxDataViewItem item;
    int obj_idx, vol_idx;
    const wxPoint pos = wxPoint(0, 0);  //Fake position
    wxGetApp().obj_list()->get_selected_item_indexes(obj_idx, vol_idx, item);
    return p->PopupObjectTable(obj_idx, vol_idx, pos);
}


wxMenu* Plater::plate_menu()            { return p->menus.plate_menu();             }
wxMenu* Plater::object_menu()           { return p->menus.object_menu();            }
wxMenu* Plater::part_menu()             { return p->menus.part_menu();              }
wxMenu* Plater::sla_object_menu()       { return p->menus.sla_object_menu();        }
wxMenu* Plater::default_menu()          { return p->menus.default_menu();           }
wxMenu* Plater::instance_menu()         { return p->menus.instance_menu();          }
wxMenu* Plater::layer_menu()            { return p->menus.layer_menu();             }
wxMenu* Plater::multi_selection_menu()  { return p->menus.multi_selection_menu();   }

SuppressBackgroundProcessingUpdate::SuppressBackgroundProcessingUpdate() :
    m_was_scheduled(wxGetApp().plater()->is_background_process_update_scheduled())
{
    wxGetApp().plater()->suppress_background_process(m_was_scheduled);
}

SuppressBackgroundProcessingUpdate::~SuppressBackgroundProcessingUpdate()
{
    wxGetApp().plater()->schedule_background_process(m_was_scheduled);
}

}}    // namespace Slic3r::GUI
