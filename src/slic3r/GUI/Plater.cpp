#include "Plater.hpp"

#include <cstddef>
#include <algorithm>
#include <vector>
#include <string>
#include <regex>
#include <boost/optional.hpp>
#include <boost/filesystem/path.hpp>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/notebook.h>
#include <wx/button.h>
#include <wx/bmpcbox.h>
#include <wx/statbox.h>
#include <wx/statbmp.h>
#include <wx/filedlg.h>
#include <wx/dnd.h>
#include <wx/progdlg.h>
#include <wx/wupdlock.h>
#include <wx/colordlg.h>

#include "libslic3r/libslic3r.h"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/GCode/PreviewData.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/Format/STL.hpp"
#include "libslic3r/Format/AMF.hpp"
#include "libslic3r/Format/3mf.hpp"
#include "slic3r/AppController.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "GUI_ObjectManipulation.hpp"
#include "GUI_Utils.hpp"
#include "MainFrame.hpp"
#include "3DScene.hpp"
#include "GLCanvas3D.hpp"
#include "GLToolbar.hpp"
#include "GUI_Preview.hpp"
#include "Tab.hpp"
#include "PresetBundle.hpp"
#include "BackgroundSlicingProcess.hpp"
#include "ProgressStatusBar.hpp"
#include "slic3r/Utils/ASCIIFolding.hpp"
#include "PrintConfig.hpp"

#include <wx/glcanvas.h>    // Needs to be last because reasons :-/
#include "WipeTowerDialog.hpp"

using boost::optional;
namespace fs = boost::filesystem;
using Slic3r::_3DScene;
using Slic3r::Preset;


namespace Slic3r {
namespace GUI {


wxDEFINE_EVENT(EVT_SLICING_COMPLETED, wxCommandEvent);
wxDEFINE_EVENT(EVT_PROCESS_COMPLETED, wxCommandEvent);


// Sidebar widgets

// struct InfoBox : public wxStaticBox
// {
//     InfoBox(wxWindow *parent, const wxString &label) :
//         wxStaticBox(parent, wxID_ANY, label)
//     {
//         SetFont(GUI::small_font().Bold());
//     }
// };

class ObjectInfo : public wxStaticBoxSizer
{
public:
    ObjectInfo(wxWindow *parent);

    wxStaticBitmap *manifold_warning_icon;
private:
    wxStaticText *info_size;
    wxStaticText *info_volume;
    wxStaticText *info_facets;
    wxStaticText *info_materials;
    wxStaticText *info_manifold;
};

ObjectInfo::ObjectInfo(wxWindow *parent) :
    wxStaticBoxSizer(new wxStaticBox(parent, wxID_ANY, _(L("Info"))), wxVERTICAL)
{
    GetStaticBox()->SetFont(wxGetApp().bold_font());

    auto *grid_sizer = new wxFlexGridSizer(4, 5, 5);
    grid_sizer->SetFlexibleDirection(wxHORIZONTAL);
    grid_sizer->AddGrowableCol(1, 1);
    grid_sizer->AddGrowableCol(3, 1);

    auto init_info_label = [parent, grid_sizer](wxStaticText **info_label, wxString text_label) {
        auto *text = new wxStaticText(parent, wxID_ANY, text_label);
        text->SetFont(wxGetApp().small_font());
        *info_label = new wxStaticText(parent, wxID_ANY, "");
        (*info_label)->SetFont(wxGetApp().small_font());
        grid_sizer->Add(text, 0);
        grid_sizer->Add(*info_label, 0);
    };

    init_info_label(&info_size, _(L("Size")));
    init_info_label(&info_volume, _(L("Volume")));
    init_info_label(&info_facets, _(L("Facets")));
    init_info_label(&info_materials, _(L("Materials")));

    auto *info_manifold_text = new wxStaticText(parent, wxID_ANY, _(L("Manifold")));
    info_manifold_text->SetFont(wxGetApp().small_font());
    info_manifold = new wxStaticText(parent, wxID_ANY, "");
    info_manifold->SetFont(wxGetApp().small_font());
    wxBitmap bitmap(GUI::from_u8(Slic3r::var("error.png")), wxBITMAP_TYPE_PNG);
    manifold_warning_icon = new wxStaticBitmap(parent, wxID_ANY, bitmap);
    auto *sizer_manifold = new wxBoxSizer(wxHORIZONTAL);
    sizer_manifold->Add(info_manifold_text, 0);
    sizer_manifold->Add(manifold_warning_icon, 0, wxLEFT, 2);
    sizer_manifold->Add(info_manifold, 0, wxLEFT, 2);
    grid_sizer->Add(sizer_manifold, 0, wxEXPAND | wxTOP, 4);

    Add(grid_sizer, 0, wxEXPAND);
}

class SlicedInfo : public wxStaticBoxSizer
{
public:
    SlicedInfo(wxWindow *parent);

private:
    wxStaticText *info_filament_m;
    wxStaticText *info_filament_mm3;
    wxStaticText *info_filament_g;
    wxStaticText *info_cost;
    wxStaticText *info_time_normal;
    wxStaticText *info_time_silent;
};

SlicedInfo::SlicedInfo(wxWindow *parent) :
    wxStaticBoxSizer(new wxStaticBox(parent, wxID_ANY, _(L("Sliced Info"))), wxVERTICAL)
{
    GetStaticBox()->SetFont(wxGetApp().bold_font());

    auto *grid_sizer = new wxFlexGridSizer(2, 5, 5);
    grid_sizer->SetFlexibleDirection(wxHORIZONTAL);
    grid_sizer->AddGrowableCol(1, 1);

    auto init_info_label = [parent, grid_sizer](wxStaticText *&info_label, wxString text_label) {
        auto *text = new wxStaticText(parent, wxID_ANY, text_label);
        text->SetFont(wxGetApp().small_font());
        info_label = new wxStaticText(parent, wxID_ANY, "N/A");
        info_label->SetFont(wxGetApp().small_font());
        grid_sizer->Add(text, 0);
        grid_sizer->Add(info_label, 0);
    };

    init_info_label(info_filament_m, _(L("Used Filament (m)")));
    init_info_label(info_filament_mm3, _(L("Used Filament (mm³)")));
    init_info_label(info_filament_g, _(L("Used Filament (g)")));
    init_info_label(info_cost, _(L("Cost")));
    init_info_label(info_time_normal, _(L("Estimated printing time (normal mode)")));
    init_info_label(info_time_silent, _(L("Estimated printing time (silent mode)")));

    Add(grid_sizer, 0, wxEXPAND);
}

PresetComboBox::PresetComboBox(wxWindow *parent, Preset::Type preset_type) :
    wxBitmapComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY),
    preset_type(preset_type),
    last_selected(wxNOT_FOUND)
{
    Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &evt) {
        auto selected_item = this->GetSelection();

        auto marker = reinterpret_cast<Marker>(this->GetClientData(selected_item));
        if (marker == LABEL_ITEM_MARKER) {
            this->SetSelection(this->last_selected);
            evt.StopPropagation();
        } else if (this->last_selected != selected_item) {
            this->last_selected = selected_item;
            evt.SetInt(this->preset_type);
            evt.Skip();
        } else {
            evt.StopPropagation();
        }
    });
    
    if (preset_type == Slic3r::Preset::TYPE_FILAMENT)
    {
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &event) {
            if (extruder_idx < 0 || event.GetLogicalPosition(wxClientDC(this)).x > 24) {
                // Let the combo box process the mouse click.
                event.Skip();
                return;
            }
            
            // Swallow the mouse click and open the color picker.
            auto data = new wxColourData();
            data->SetChooseFull(1);
            auto dialog = new wxColourDialog(wxGetApp().mainframe, data);
            if (dialog->ShowModal() == wxID_OK) {
                DynamicPrintConfig cfg = *wxGetApp().get_tab(Preset::TYPE_PRINTER)->get_config(); 

                auto colors = static_cast<ConfigOptionStrings*>(wxGetApp().preset_bundle->full_config().option("extruder_colour")->clone());
                colors->values[extruder_idx] = dialog->GetColourData().GetColour().GetAsString(wxC2S_HTML_SYNTAX);

                cfg.set_key_value("extruder_colour", colors);

                wxGetApp().get_tab(Preset::TYPE_PRINTER)->load_config(cfg);
                wxGetApp().preset_bundle->update_platter_filament_ui(extruder_idx, this);
                wxGetApp().plater()->on_config_change(&cfg);
            }
            dialog->Destroy();
        });
    }
}

PresetComboBox::~PresetComboBox() {}


void PresetComboBox::set_label_marker(int item)
{
    this->SetClientData(item, (void*)LABEL_ITEM_MARKER);
}

// Frequently changed parameters

class FreqChangedParams : public OG_Settings
{
    double		    m_brim_width = 0.0;
    wxButton*       m_wiping_dialog_button{ nullptr };
public:
    FreqChangedParams(wxWindow* parent, const int label_width);
    ~FreqChangedParams() {}

    wxButton*       get_wiping_dialog_button() { return m_wiping_dialog_button; }
};

FreqChangedParams::FreqChangedParams(wxWindow* parent, const int label_width) :
    OG_Settings(parent, false)
{
    DynamicPrintConfig*	config = &wxGetApp().preset_bundle->prints.get_edited_preset().config;

    m_og->set_config(config);
    m_og->label_width = label_width;

    m_og->m_on_change = [config, this](t_config_option_key opt_key, boost::any value){
        TabPrint* tab_print = nullptr;
        for (size_t i = 0; i < wxGetApp().tab_panel()->GetPageCount(); ++i) {
            Tab *tab = dynamic_cast<Tab*>(wxGetApp().tab_panel()->GetPage(i));
            if (!tab)
                continue;
            if (tab->name() == "print"){
                tab_print = static_cast<TabPrint*>(tab);
                break;
            }
        }
        if (tab_print == nullptr)
            return;

        if (opt_key == "fill_density"){
            value = m_og->get_config_value(*config, opt_key);
            tab_print->set_value(opt_key, value);
            tab_print->update();
        }
        else{
            DynamicPrintConfig new_conf = *config;
            if (opt_key == "brim"){
                double new_val;
                double brim_width = config->opt_float("brim_width");
                if (boost::any_cast<bool>(value) == true)
                {
                    new_val = m_brim_width == 0.0 ? 10 :
                        m_brim_width < 0.0 ? m_brim_width * (-1) :
                        m_brim_width;
                }
                else{
                    m_brim_width = brim_width * (-1);
                    new_val = 0;
                }
                new_conf.set_key_value("brim_width", new ConfigOptionFloat(new_val));
            }
            else{ //(opt_key == "support")
                const wxString& selection = boost::any_cast<wxString>(value);

                auto support_material = selection == _("None") ? false : true;
                new_conf.set_key_value("support_material", new ConfigOptionBool(support_material));

                if (selection == _("Everywhere"))
                    new_conf.set_key_value("support_material_buildplate_only", new ConfigOptionBool(false));
                else if (selection == _("Support on build plate only"))
                    new_conf.set_key_value("support_material_buildplate_only", new ConfigOptionBool(true));
            }
            tab_print->load_config(new_conf);
        }

        tab_print->update_dirty();
    };

    Option option = m_og->get_option("fill_density");
    option.opt.sidetext = "";
    option.opt.full_width = true;
    m_og->append_single_option_line(option);

    ConfigOptionDef def;

    def.label = L("Support");
    def.type = coStrings;
    def.gui_type = "select_open";
    def.tooltip = L("Select what kind of support do you need");
    def.enum_labels.push_back(L("None"));
    def.enum_labels.push_back(L("Support on build plate only"));
    def.enum_labels.push_back(L("Everywhere"));
    std::string selection = !config->opt_bool("support_material") ?
        "None" :
        config->opt_bool("support_material_buildplate_only") ?
        "Support on build plate only" :
        "Everywhere";
    def.default_value = new ConfigOptionStrings{ selection };
    option = Option(def, "support");
    option.opt.full_width = true;
    m_og->append_single_option_line(option);

    m_brim_width = config->opt_float("brim_width");
    def.label = L("Brim");
    def.type = coBool;
    def.tooltip = L("This flag enables the brim that will be printed around each object on the first layer.");
    def.gui_type = "";
    def.default_value = new ConfigOptionBool{ m_brim_width > 0.0 ? true : false };
    option = Option(def, "brim");
    m_og->append_single_option_line(option);


    Line line = { "", "" };
    line.widget = [config, this](wxWindow* parent){
        m_wiping_dialog_button = new wxButton(parent, wxID_ANY, _(L("Purging volumes")) + dots, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT);
        auto sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(m_wiping_dialog_button);
        m_wiping_dialog_button->Bind(wxEVT_BUTTON, ([parent](wxCommandEvent& e)
        {
            auto &config = wxGetApp().preset_bundle->project_config;
            const std::vector<double> &init_matrix = (config.option<ConfigOptionFloats>("wiping_volumes_matrix"))->values;
            const std::vector<double> &init_extruders = (config.option<ConfigOptionFloats>("wiping_volumes_extruders"))->values;

            WipingDialog dlg(parent, cast<float>(init_matrix), cast<float>(init_extruders));

            if (dlg.ShowModal() == wxID_OK) {
                std::vector<float> matrix = dlg.get_matrix();
                std::vector<float> extruders = dlg.get_extruders();
                (config.option<ConfigOptionFloats>("wiping_volumes_matrix"))->values = std::vector<double>(matrix.begin(), matrix.end());
                (config.option<ConfigOptionFloats>("wiping_volumes_extruders"))->values = std::vector<double>(extruders.begin(), extruders.end());
                g_on_request_update_callback.call();
            }
        }));
        return sizer;
    };
    m_og->append_line(line);
}


// Sidebar / private

struct Sidebar::priv
{
    Plater *plater;

    wxScrolledWindow *scrolled;

    wxFlexGridSizer *sizer_presets;
    PresetComboBox *combo_print;
    std::vector<PresetComboBox*> combos_filament;
    wxBoxSizer *sizer_filaments;
    PresetComboBox *combo_sla_material;
    PresetComboBox *combo_printer;

    wxBoxSizer *sizer_params;
    FreqChangedParams   *frequently_changed_parameters;
    ObjectList          *object_list;
    ObjectManipulation  *object_manipulation;
    ObjectInfo *object_info;
    SlicedInfo *sliced_info;

    wxButton *btn_export_gcode;
    wxButton *btn_reslice;
    // wxButton *btn_print;  // XXX: remove
    wxButton *btn_send_gcode;

    priv(Plater *plater) : plater(plater) {}

    bool show_manifold_warning_icon = false;
    bool show_print_info = false;


    void show_preset_comboboxes();
};

void Sidebar::priv::show_preset_comboboxes()
{
    const bool showSLA = wxGetApp().preset_bundle->printers.get_selected_preset().printer_technology() == ptSLA;

    wxWindowUpdateLocker noUpdates(wxGetApp().mainframe);

    for (size_t i = 0; i < 4; ++i)
        sizer_presets->Show(i, !showSLA);

    sizer_presets->Show(4, showSLA);
    sizer_presets->Show(5, showSLA);

    frequently_changed_parameters->get_sizer()->Show(!showSLA);

    wxGetApp().plater()->Layout();
    wxGetApp().mainframe->Layout();
}


// Sidebar / public

Sidebar::Sidebar(Plater *parent)
    : wxPanel(parent), p(new priv(parent))
{
    p->scrolled = new wxScrolledWindow(this);

    // The preset chooser
    p->sizer_presets = new wxFlexGridSizer(4, 2, 1, 2);
    p->sizer_presets->AddGrowableCol(1, 1);
    p->sizer_presets->SetFlexibleDirection(wxBOTH);
    p->sizer_filaments = new wxBoxSizer(wxVERTICAL);

    auto init_combo = [this](PresetComboBox **combo, wxString label, Preset::Type preset_type, bool filament) {
        auto *text = new wxStaticText(p->scrolled, wxID_ANY, label);
        text->SetFont(wxGetApp().small_font());
        *combo = new PresetComboBox(p->scrolled, preset_type);

        auto *sizer_presets = this->p->sizer_presets;
        auto *sizer_filaments = this->p->sizer_filaments;
        sizer_presets->Add(text, 0, wxALIGN_RIGHT | wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        if (! filament) {
            sizer_presets->Add(*combo, 1, wxALIGN_CENTER_VERTICAL | wxEXPAND | wxBOTTOM, 1);
        } else {
            sizer_filaments->Add(*combo, 1, wxEXPAND | wxBOTTOM, 1);
            (*combo)->set_extruder_idx(0);
            sizer_presets->Add(sizer_filaments, 1, wxEXPAND);
        }
    };

    p->combos_filament.push_back(nullptr);
    init_combo(&p->combo_print, _(L("Print settings")), Preset::TYPE_PRINT, false);
    init_combo(&p->combos_filament[0], _(L("Filament")), Preset::TYPE_FILAMENT, true);
    init_combo(&p->combo_sla_material, _(L("SLA material")), Preset::TYPE_SLA_MATERIAL, false);
    init_combo(&p->combo_printer, _(L("Printer")), Preset::TYPE_PRINTER, false);

    // calculate width of the preset labels 
    p->sizer_presets->Layout();
    const wxArrayInt& ar = p->sizer_presets->GetColWidths();
    int label_width = ar.IsEmpty() ? 100 : ar.front()-4;

    p->sizer_params = new wxBoxSizer(wxVERTICAL);

    // Frequently changed parameters
    p->frequently_changed_parameters = new FreqChangedParams(p->scrolled, label_width);
    p->sizer_params->Add(p->frequently_changed_parameters->get_sizer(), 0, wxEXPAND | wxBOTTOM | wxLEFT, 2);
    
    // Object List
    p->object_list = new ObjectList(p->scrolled);
    p->sizer_params->Add(p->object_list->get_sizer(), 1, wxEXPAND | wxTOP, 20);
 
    // Frequently Object Settings
    p->object_manipulation = new ObjectManipulation(p->scrolled);
    p->sizer_params->Add(p->object_manipulation->get_sizer(), 0, wxEXPAND | wxLEFT | wxTOP, 20);

    // Buttons in the scrolled area
    wxBitmap arrow_up(GUI::from_u8(Slic3r::var("brick_go.png")), wxBITMAP_TYPE_PNG);
    p->btn_send_gcode = new wxButton(p->scrolled, wxID_ANY, _(L("Send to printer")));
    p->btn_send_gcode->SetBitmap(arrow_up);
    p->btn_send_gcode->Hide();
    auto *btns_sizer_scrolled = new wxBoxSizer(wxHORIZONTAL);
    btns_sizer_scrolled->Add(p->btn_send_gcode);

    // Info boxes
    p->object_info = new ObjectInfo(p->scrolled);
    p->sliced_info = new SlicedInfo(p->scrolled);

    // Sizer in the scrolled area
    auto *scrolled_sizer = new wxBoxSizer(wxVERTICAL);
    scrolled_sizer->SetMinSize(320, -1);
    p->scrolled->SetSizer(scrolled_sizer);
    p->scrolled->SetScrollbars(0, 1, 1, 1);
    scrolled_sizer->Add(p->sizer_presets, 0, wxEXPAND | wxLEFT, 2);
    scrolled_sizer->Add(p->sizer_params, 1, wxEXPAND);
    scrolled_sizer->Add(p->object_info, 0, wxEXPAND | wxTOP | wxLEFT, 20);
    scrolled_sizer->Add(btns_sizer_scrolled, 0, wxEXPAND, 0);
    scrolled_sizer->Add(p->sliced_info, 0, wxEXPAND | wxTOP | wxLEFT, 20);

    // Buttons underneath the scrolled area
    p->btn_export_gcode = new wxButton(this, wxID_ANY, _(L("Export G-code…")));
    p->btn_export_gcode->SetFont(wxGetApp().bold_font());
    p->btn_reslice = new wxButton(this, wxID_ANY, _(L("Slice now")));
    p->btn_reslice->SetFont(wxGetApp().bold_font());

    auto *btns_sizer = new wxBoxSizer(wxVERTICAL);
    btns_sizer->Add(p->btn_reslice, 0, wxEXPAND | wxTOP, 5);
    btns_sizer->Add(p->btn_export_gcode, 0, wxEXPAND | wxTOP, 5);

    auto *sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(p->scrolled, 1, wxEXPAND | wxTOP, 5);
    sizer->Add(btns_sizer, 0, wxEXPAND | wxLEFT, 20);
    SetSizer(sizer);

    // Events
    p->btn_export_gcode->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { p->plater->export_gcode(); });
    p->btn_reslice->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { p->plater->reslice(); });
    p->btn_send_gcode->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { p->plater->send_gcode(); });
}

Sidebar::~Sidebar() {}

void Sidebar::init_filament_combo(PresetComboBox **combo, const int extr_idx) {
    *combo = new PresetComboBox(p->scrolled, Slic3r::Preset::TYPE_FILAMENT);
//         # copy icons from first choice
//         $choice->SetItemBitmap($_, $choices->[0]->GetItemBitmap($_)) for 0..$#presets;

    (*combo)->set_extruder_idx(extr_idx);

    auto /***/sizer_filaments = this->p->sizer_filaments;
    sizer_filaments->Add(*combo, 1, wxEXPAND | wxBOTTOM, 1);
}

void Sidebar::remove_unused_filament_combos(const int current_extruder_count)
{
    if (current_extruder_count >= p->combos_filament.size())
        return;
    auto sizer_filaments = this->p->sizer_filaments;
    while (p->combos_filament.size() > current_extruder_count) {
        const int last = p->combos_filament.size() - 1;
        sizer_filaments->Remove(last);
        (*p->combos_filament[last]).Destroy();
        p->combos_filament.pop_back();
    }
}

void Sidebar::update_presets(Preset::Type preset_type)
{
    switch (preset_type) {
    case Preset::TYPE_FILAMENT:
        if (p->combos_filament.size() == 1) {
            // Single filament printer, synchronize the filament presets.
            const std::string &name = wxGetApp().preset_bundle->filaments.get_selected_preset().name;
            wxGetApp().preset_bundle->set_filament_preset(0, name);
        }

        for (size_t i = 0; i < p->combos_filament.size(); i++) {
            wxGetApp().preset_bundle->update_platter_filament_ui(i, p->combos_filament[i]);
        }

        break;

    case Preset::TYPE_PRINT:
        wxGetApp().preset_bundle->prints.update_platter_ui(p->combo_print);
        break;

    case Preset::TYPE_SLA_MATERIAL:
        wxGetApp().preset_bundle->sla_materials.update_platter_ui(p->combo_sla_material);
        break;

    case Preset::TYPE_PRINTER:
        // Update the print choosers to only contain the compatible presets, update the dirty flags.
        wxGetApp().preset_bundle->prints.update_platter_ui(p->combo_print);
        // Update the printer choosers, update the dirty flags.
        wxGetApp().preset_bundle->printers.update_platter_ui(p->combo_printer);
        // Update the filament choosers to only contain the compatible presets, update the color preview,
        // update the dirty flags.
        for (size_t i = 0; i < p->combos_filament.size(); i++) {
            wxGetApp().preset_bundle->update_platter_filament_ui(i, p->combos_filament[i]);
        }
        p->show_preset_comboboxes();
        break;

    default: break;
    }

    // Synchronize config.ini with the current selections.
    wxGetApp().preset_bundle->export_selections(*wxGetApp().app_config);
}

ObjectManipulation* Sidebar::obj_manipul()
{
    return p->object_manipulation;
}

ObjectList* Sidebar::obj_list()
{
    return p->object_list;
}

ConfigOptionsGroup* Sidebar::og_freq_chng_params()
{
    return p->frequently_changed_parameters->get_og();
}

wxButton* Sidebar::get_wiping_dialog_button()
{
    return p->frequently_changed_parameters->get_wiping_dialog_button();
}

void Sidebar::update_objects_list_extruder_column(int extruders_count)
{
    p->object_list->update_objects_list_extruder_column(extruders_count);
}

void Sidebar::show_info_sizers(const bool show)
{
    p->object_info->Show(show);
    p->object_info->manifold_warning_icon->Show(show && p->show_manifold_warning_icon); // where is g_show_manifold_warning_icon updating? #ys_FIXME
    p->sliced_info->Show(show && p->show_print_info); // where is g_show_print_info updating? #ys_FIXME
}

void Sidebar::show_buttons(const bool show)
{
    p->btn_reslice->Show(show);
    for (size_t i = 0; i < wxGetApp().tab_panel()->GetPageCount(); ++i) {
        TabPrinter *tab = dynamic_cast<TabPrinter*>(wxGetApp().tab_panel()->GetPage(i));
        if (!tab)
            continue;
        if (wxGetApp().preset_bundle->printers.get_selected_preset().printer_technology() == ptFFF) {
            p->btn_send_gcode->Show(show && !tab->m_config->opt_string("print_host").empty());
        }
        break;
    }
}

void Sidebar::enable_buttons(bool enable)
{
    p->btn_reslice->Enable(enable);
    p->btn_export_gcode->Enable(enable);
    p->btn_send_gcode->Enable(enable);
}

void Sidebar::show_button(ButtonAction but_action, bool show)
{
    switch (but_action)
    {
    case baReslice:
        p->btn_reslice->Show(show);
        break;
    case baExportGcode:
        p->btn_export_gcode->Show(show);
        break;
    case baSendGcode:
        p->btn_send_gcode->Show(show);
        break;
    default:
        break;
    }
}

bool Sidebar::is_multifilament()
{
    return p->combos_filament.size() > 0;
}


std::vector<PresetComboBox*>& Sidebar::combos_filament()
{
    return p->combos_filament;
}


// Plater::Object

struct PlaterObject
{
    std::string name;
    bool selected;

    PlaterObject(std::string name) : name(std::move(name)), selected(false) {}
};

// Plater::DropTarget

class PlaterDropTarget : public wxFileDropTarget
{
public:
    PlaterDropTarget(Plater *plater) : plater(plater) {}

    virtual bool OnDropFiles(wxCoord x, wxCoord y, const wxArrayString &filenames);

private:
    Plater *plater;

    static const std::regex pattern_drop;
};

const std::regex PlaterDropTarget::pattern_drop("[.](stl|obj|amf|3mf|prusa)$", std::regex::icase);

bool PlaterDropTarget::OnDropFiles(wxCoord x, wxCoord y, const wxArrayString &filenames)
{
    std::vector<fs::path> paths;

    for (const auto &filename : filenames) {
        fs::path path(filename);

        if (std::regex_match(path.string(), pattern_drop)) {
            paths.push_back(std::move(path));
        } else {
            return false;
        }
    }

    plater->load_files(paths);
    return true;
}

// Plater / private

struct Plater::priv
{
    // PIMPL back pointer ("Q-Pointer")
    Plater *q;
    MainFrame *main_frame;

    // Data
    Slic3r::DynamicPrintConfig *config;
    Slic3r::Print print;
    Slic3r::Model model;
    Slic3r::GCodePreviewData gcode_preview_data;
    std::vector<PlaterObject> objects;

    fs::path export_gcode_output_file;
    fs::path send_gcode_file;

    // GUI elements
    wxNotebook *notebook;
    Sidebar *sidebar;
    wxGLCanvas *canvas3D;    // TODO: Use GLCanvas3D when we can
    Preview *preview;
    BackgroundSlicingProcess background_process;

    static const std::regex pattern_bundle;
    static const std::regex pattern_3mf;
    static const std::regex pattern_zip_amf;

    priv(Plater *q, MainFrame *main_frame);

#if !ENABLE_EXTENDED_SELECTION
    std::vector<int> collect_selections();
#endif // !ENABLE_EXTENDED_SELECTION
    void update(bool force_autocenter = false);
    void update_ui_from_settings();
    ProgressStatusBar* statusbar();
    std::string get_config(const std::string &key) const;
    BoundingBoxf bed_shape_bb() const;
    BoundingBox scaled_bed_shape_bb() const;
    std::vector<size_t> load_files(const std::vector<fs::path> &input_files);
    std::vector<size_t> load_model_objects(const ModelObjectPtrs &model_objects);
    std::unique_ptr<CheckboxFileDialog> get_export_file(GUI::FileType file_type);

    void select_object(optional<size_t> obj_idx);
    void select_object_from_cpp();
    optional<size_t> selected_object() const;
    void selection_changed();
    void object_list_changed();
    void select_view();

    void remove(size_t obj_idx);
    void reset();
    void rotate();
    void mirror(const Axis &axis);
    void scale();
    void arrange();
    void split_object();
    void schedule_background_process();
    void async_apply_config();
    void start_background_process();
    void stop_background_process();
    void reload_from_disk();
    void export_object_stl();
    void fix_through_netfabb();
    void item_changed_selection();

    void on_notebook_changed(wxBookCtrlEvent&);
    void on_select_preset(wxCommandEvent&);
    void on_progress_event();
    void on_update_print_preview(wxCommandEvent&);
    void on_process_completed(wxCommandEvent&);
    void on_layer_editing_toggled(bool enable);

    void on_action_add(SimpleEvent&);
    void on_action_split(SimpleEvent&);
    void on_action_cut(SimpleEvent&);
    void on_action_settings(SimpleEvent&);
    void on_action_layersediting(SimpleEvent&);
#if !ENABLE_EXTENDED_SELECTION
    void on_action_selectbyparts(SimpleEvent&);
#endif // !ENABLE_EXTENDED_SELECTION

    void on_object_select(ObjectSelectEvent&);
    void on_viewport_changed(SimpleEvent&);
    void on_right_click(Vec2dEvent&);
    void on_model_update(SimpleEvent&);
    void on_scale_uniformly(SimpleEvent&);
    void on_wipetower_moved(Vec3dEvent&);
    void on_enable_action_buttons(Event<bool>&);
    void on_update_geometry(Vec3dsEvent<2>&);
};

const std::regex Plater::priv::pattern_bundle("[.](amf|amf[.]xml|zip[.]amf|3mf|prusa)$", std::regex::icase);
const std::regex Plater::priv::pattern_3mf("[.]3mf$", std::regex::icase);
const std::regex Plater::priv::pattern_zip_amf("[.]zip[.]amf$", std::regex::icase);

Plater::priv::priv(Plater *q, MainFrame *main_frame) :
    q(q),
    main_frame(main_frame),
    config(Slic3r::DynamicPrintConfig::new_from_defaults_keys({
        "bed_shape", "complete_objects", "extruder_clearance_radius", "skirts", "skirt_distance",
        "brim_width", "variable_layer_height", "serial_port", "serial_speed", "host_type", "print_host",
        "printhost_apikey", "printhost_cafile", "nozzle_diameter", "single_extruder_multi_material",
        "wipe_tower", "wipe_tower_x", "wipe_tower_y", "wipe_tower_width", "wipe_tower_rotation_angle",
        "extruder_colour", "filament_colour", "max_print_height", "printer_model"
    })),
    notebook(new wxNotebook(q, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_BOTTOM)),
    sidebar(new Sidebar(q)),
    canvas3D(GLCanvas3DManager::create_wxglcanvas(notebook))
{
    background_process.set_print(&print);
    background_process.set_gcode_preview_data(&gcode_preview_data);
    background_process.set_sliced_event(EVT_SLICING_COMPLETED);
    background_process.set_finished_event(EVT_PROCESS_COMPLETED);

    _3DScene::add_canvas(canvas3D);
    _3DScene::allow_multisample(canvas3D, GLCanvas3DManager::can_multisample());
    notebook->AddPage(canvas3D, _(L("3D")));
    preview = new GUI::Preview(notebook, config, &print, &gcode_preview_data);

    // XXX: If have OpenGL
    _3DScene::enable_picking(canvas3D, true);
    _3DScene::enable_moving(canvas3D, true);
    // XXX: more config from 3D.pm
#if !ENABLE_EXTENDED_SELECTION
    _3DScene::set_select_by(canvas3D, "object");
    _3DScene::set_drag_by(canvas3D, "instance");
#endif // !ENABLE_EXTENDED_SELECTION
    _3DScene::set_model(canvas3D, &model);
    _3DScene::set_print(canvas3D, &print);
    _3DScene::set_config(canvas3D, config);
    _3DScene::enable_gizmos(canvas3D, true);
    _3DScene::enable_toolbar(canvas3D, true);
    _3DScene::enable_shader(canvas3D, true);
    _3DScene::enable_force_zoom_to_bed(canvas3D, true);

    // XXX: apply_config_timer
    // {
    //  my $timer_id = Wx::NewId();
    //  $self->{apply_config_timer} = Wx::Timer->new($self, $timer_id);
    //  EVT_TIMER($self, $timer_id, sub {
    //      my ($self, $event) = @_;
    //      $self->async_apply_config;
    //  });
    // }

    auto *bed_shape = config->opt<ConfigOptionPoints>("bed_shape");
    _3DScene::set_bed_shape(canvas3D, bed_shape->values);
    _3DScene::zoom_to_bed(canvas3D);
    preview->set_bed_shape(bed_shape->values);

    update();

    auto *hsizer = new wxBoxSizer(wxHORIZONTAL);
    hsizer->Add(notebook, 1, wxEXPAND | wxTOP, 1);
    hsizer->Add(sidebar, 0, wxEXPAND | wxLEFT | wxRIGHT, 0);
    q->SetSizer(hsizer);

    // Events:

    // Notebook page change event
    notebook->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, &priv::on_notebook_changed, this);

    // Preset change event
    sidebar->Bind(wxEVT_COMBOBOX, &priv::on_select_preset, this);

    // 3DScene events:
    canvas3D->Bind(EVT_GLCANVAS_OBJECT_SELECT, &priv::on_object_select, this);
    canvas3D->Bind(EVT_GLCANVAS_VIEWPORT_CHANGED, &priv::on_viewport_changed, this);
    // canvas3D->Bind(EVT_GLCANVAS_DOUBLE_CLICK, [](SimpleEvent&) { });  // XXX: remove?
    canvas3D->Bind(EVT_GLCANVAS_RIGHT_CLICK, &priv::on_right_click, this);
    canvas3D->Bind(EVT_GLCANVAS_MODEL_UPDATE, &priv::on_model_update, this);
    canvas3D->Bind(EVT_GLCANVAS_REMOVE_OBJECT, [q](SimpleEvent&) { q->remove_selected(); });
    canvas3D->Bind(EVT_GLCANVAS_ARRANGE, [this](SimpleEvent&) { arrange(); });
    canvas3D->Bind(EVT_GLCANVAS_ROTATE_OBJECT, [this](Event<int> &evt) { /*TODO: call rotate */ });
    canvas3D->Bind(EVT_GLCANVAS_SCALE_UNIFORMLY, [this](SimpleEvent&) { scale(); });
    canvas3D->Bind(EVT_GLCANVAS_INCREASE_OBJECTS, [q](Event<int> &evt) { evt.data == 1 ? q->increase() : q->decrease(); });
    canvas3D->Bind(EVT_GLCANVAS_INSTANCE_MOVED, [this](SimpleEvent&) { update(); });
    canvas3D->Bind(EVT_GLCANVAS_WIPETOWER_MOVED, &priv::on_wipetower_moved, this);
    canvas3D->Bind(EVT_GLCANVAS_ENABLE_ACTION_BUTTONS, &priv::on_enable_action_buttons, this);
    canvas3D->Bind(EVT_GLCANVAS_UPDATE_GEOMETRY, &priv::on_update_geometry, this);
    // 3DScene/Toolbar:
    canvas3D->Bind(EVT_GLTOOLBAR_ADD, &priv::on_action_add, this);
    canvas3D->Bind(EVT_GLTOOLBAR_DELETE, [q](SimpleEvent&) { q->remove_selected(); } );
    canvas3D->Bind(EVT_GLTOOLBAR_DELETE_ALL, [this](SimpleEvent&) { reset(); });
    canvas3D->Bind(EVT_GLTOOLBAR_ARRANGE, [this](SimpleEvent&) { arrange(); });
    canvas3D->Bind(EVT_GLTOOLBAR_MORE, [q](SimpleEvent&) { q->increase(); });
    canvas3D->Bind(EVT_GLTOOLBAR_FEWER, [q](SimpleEvent&) { q->decrease(); });
    canvas3D->Bind(EVT_GLTOOLBAR_SPLIT, &priv::on_action_split, this);
    canvas3D->Bind(EVT_GLTOOLBAR_CUT, &priv::on_action_cut, this);
    canvas3D->Bind(EVT_GLTOOLBAR_SETTINGS, &priv::on_action_settings, this);
    canvas3D->Bind(EVT_GLTOOLBAR_LAYERSEDITING, &priv::on_action_layersediting, this);
#if !ENABLE_EXTENDED_SELECTION
    canvas3D->Bind(EVT_GLTOOLBAR_SELECTBYPARTS, &priv::on_action_selectbyparts, this);
#endif // !ENABLE_EXTENDED_SELECTION

    // Preview events:
    preview->get_wxglcanvas()->Bind(EVT_GLCANVAS_VIEWPORT_CHANGED, &priv::on_viewport_changed, this);

    q->Bind(EVT_SLICING_COMPLETED, &priv::on_update_print_preview, this);
    q->Bind(EVT_PROCESS_COMPLETED, &priv::on_process_completed, this);

    // Drop target:
    q->SetDropTarget(new PlaterDropTarget(q));   // if my understanding is right, wxWindow takes the owenership

    update_ui_from_settings();
    q->Layout();
}

#if !ENABLE_EXTENDED_SELECTION
std::vector<int> Plater::priv::collect_selections()
{
    std::vector<int> res;
    for (const auto &obj : objects) {
        res.push_back(obj.selected);
    }
    return res;
}
#endif // !ENABLE_EXTENDED_SELECTION

void Plater::priv::update(bool force_autocenter)
{
    wxWindowUpdateLocker freeze_guard(q);
    if (get_config("autocenter") == "1" || force_autocenter) {
        // auto *bed_shape_opt = config->opt<ConfigOptionPoints>("bed_shape");
        // const auto bed_shape = Slic3r::Polygon::new_scale(bed_shape_opt->values);
        // const BoundingBox bed_shape_bb = bed_shape.bounding_box();
        const Vec2d& bed_center = bed_shape_bb().center();
        model.center_instances_around_point(bed_center);
    }

    // stop_background_process();   // TODO
    print.reload_model_instances();

#if !ENABLE_EXTENDED_SELECTION
    const auto selections = collect_selections();
    _3DScene::set_objects_selections(canvas3D, selections);
#endif // !ENABLE_EXTENDED_SELECTION
    _3DScene::reload_scene(canvas3D, false);
    preview->reset_gcode_preview_data();
    preview->reload_print();

    // schedule_background_process();   // TODO
}

void Plater::priv::update_ui_from_settings()
{
    // TODO: (?)
    // my ($self) = @_;
    // if (defined($self->{btn_reslice}) && $self->{buttons_sizer}->IsShown($self->{btn_reslice}) != (! wxTheApp->{app_config}->get("background_processing"))) {
    //     $self->{buttons_sizer}->Show($self->{btn_reslice}, ! wxTheApp->{app_config}->get("background_processing"));
    //     $self->{buttons_sizer}->Layout;
    // }
}

ProgressStatusBar*  Plater::priv::statusbar()
{
    return main_frame->m_statusbar;
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
    const auto *bed_shape_opt = config->opt<ConfigOptionPoints>("bed_shape");
    const auto bed_shape = Slic3r::Polygon::new_scale(bed_shape_opt->values);
    return bed_shape.bounding_box();
}

std::vector<size_t> Plater::priv::load_files(const std::vector<fs::path> &input_files)
{
    if (input_files.empty()) { return std::vector<size_t>(); }

    auto *nozzle_dmrs = config->opt<ConfigOptionFloats>("nozzle_diameter");

    bool one_by_one = input_files.size() == 1 || nozzle_dmrs->values.size() <= 1;
    if (! one_by_one) {
        for (const auto &path : input_files) {
            if (std::regex_match(path.string(), pattern_bundle)) {
                one_by_one = true;
                break;
            }
        }
    }

    const auto loading = _(L("Loading…"));
    wxProgressDialog dlg(loading, loading);
    dlg.Pulse();

    auto *new_model = one_by_one ? nullptr : new Slic3r::Model();
    std::vector<size_t> obj_idxs;

    for (size_t i = 0; i < input_files.size(); i++) {
        const auto &path = input_files[i];
        const auto filename = path.filename();
        const auto dlg_info = wxString::Format(_(L("Processing input file %s\n")), filename.string());
        dlg.Update(100 * i / input_files.size(), dlg_info);

        const bool type_3mf = std::regex_match(path.string(), pattern_3mf);
        const bool type_zip_amf = !type_3mf && std::regex_match(path.string(), pattern_zip_amf);

        Slic3r::Model model;
        try {
            if (type_3mf || type_zip_amf) {
                DynamicPrintConfig config;
                config.apply(FullPrintConfig::defaults());
                model = Slic3r::Model::read_from_archive(path.string(), &config, false);
                Preset::normalize(config);
                wxGetApp().preset_bundle->load_config_model(filename.string(), std::move(config));
                for (const auto &kv : main_frame->options_tabs()) { kv.second->load_current_preset(); }
                wxGetApp().app_config->update_config_dir(path.parent_path().string());
                // forces the update of the config here, or it will invalidate the imported layer heights profile if done using the timer
                // and if the config contains a "layer_height" different from the current defined one
                // TODO:
                // $self->async_apply_config;
            } else {
                model = Slic3r::Model::read_from_file(path.string(), nullptr, false);
            }
        }
        catch (const std::runtime_error &e) {
            GUI::show_error(q, e.what());
            continue;
        }

        // The model should now be initialized

        if (model.looks_like_multipart_object()) {
            wxMessageDialog dlg(q, _(L(
                    "This file contains several objects positioned at multiple heights. "
                    "Instead of considering them as multiple objects, should I consider\n"
                    "this file as a single object having multiple parts?\n"
                )), _(L("Multi-part object detected")), wxICON_WARNING | wxYES | wxNO);
            if (dlg.ShowModal() == wxID_YES) {
                model.convert_multipart_object(nozzle_dmrs->values.size());
            }
        }

        if (type_3mf) {
            for (ModelObject* model_object : model.objects) {
                model_object->center_around_origin();
            }
        }

        if (one_by_one) {
            auto loaded_idxs = load_model_objects(model.objects);
            obj_idxs.insert(obj_idxs.end(), loaded_idxs.begin(), loaded_idxs.end());
        } else {
            // This must be an .stl or .obj file, which may contain a maximum of one volume.
            for (const ModelObject* model_object : model.objects) {
                new_model->add_object(*model_object);
            }
        }
    }

    if (new_model != nullptr) {
        wxMessageDialog dlg(q, _(L(
                "Multiple objects were loaded for a multi-material printer.\n"
                "Instead of considering them as multiple objects, should I consider\n"
                "these files to represent a single object having multiple parts?\n"
            )), _(L("Multi-part object detected")), wxICON_WARNING | wxYES | wxNO);
        if (dlg.ShowModal() == wxID_YES) {
            new_model->convert_multipart_object(nozzle_dmrs->values.size());
        }

        auto loaded_idxs = load_model_objects(model.objects);
        obj_idxs.insert(obj_idxs.end(), loaded_idxs.begin(), loaded_idxs.end());
    }

    wxGetApp().app_config->update_skein_dir(input_files[input_files.size() - 1].parent_path().string());
    // XXX: Plater.pm had @loaded_files, but didn't seem to fill them with the filenames...
    statusbar()->set_status_text(_(L("Loaded")));
    return obj_idxs;
}

std::vector<size_t> Plater::priv::load_model_objects(const ModelObjectPtrs &model_objects)
{
    const BoundingBoxf bed_shape = bed_shape_bb();
    const Vec3d bed_center = Slic3r::to_3d(bed_shape.center().cast<double>(), 0.0);
    const Vec3d bed_size = Slic3r::to_3d(bed_shape.size().cast<double>(), 1.0);

    bool need_arrange = false;
    bool scaled_down = false;
    std::vector<size_t> obj_idxs;

    for (ModelObject *model_object : model_objects) {
        auto *object = model.add_object(*model_object);
        std::string object_name = object->name.empty() ? fs::path(object->input_file).filename().string() : object->name;
        objects.emplace_back(std::move(object_name));
        obj_idxs.push_back(objects.size() - 1);

        if (model_object->instances.empty()) {
            // if object has no defined position(s) we need to rearrange everything after loading
            need_arrange = true;

            // add a default instance and center object around origin
            object->center_around_origin();  // also aligns object to Z = 0
            auto *instance = object->add_instance();
            instance->set_offset(bed_center);
        }

        const Vec3d size = object->bounding_box().size();
        const Vec3d ratio = size.cwiseQuotient(bed_size);
        const double max_ratio = std::max(ratio(0), ratio(1));
        if (max_ratio > 10000) {
            // the size of the object is too big -> this could lead to overflow when moving to clipper coordinates,
            // so scale down the mesh
            // const Vec3d inverse = ratio.cwiseInverse();
            // object->scale(inverse);
            object->scale(ratio.cwiseInverse());
            scaled_down = true;
        } else if (max_ratio > 5) {
            const Vec3d inverse = ratio.cwiseInverse();
            for (ModelInstance *instance : model_object->instances) {
                instance->set_scaling_factor(inverse);
            }
        }

        print.auto_assign_extruders(object);
        print.add_model_object(object);
    }

    // if user turned autocentering off, automatic arranging would disappoint them
    if (get_config("autocenter") != "1") {
        need_arrange = false;
    }

    if (scaled_down) {
        GUI::show_info(q,
            _(L("Your object appears to be too large, so it was automatically scaled down to fit your print bed.")),
            _(L("Object too large?")));
    }

    for (const size_t idx : obj_idxs) {
        wxGetApp().obj_list()->add_object_to_list(idx);
    }

    if (need_arrange) {
        // arrange();   // TODO
    }

    update();
    _3DScene::zoom_to_volumes(canvas3D);
    object_list_changed();

    // $self->schedule_background_process;

    return obj_idxs;
}

std::unique_ptr<CheckboxFileDialog> Plater::priv::get_export_file(GUI::FileType file_type)
{
    wxString wildcard;
    switch (file_type) {
        case FT_STL:
        case FT_AMF:
        case FT_3MF:
            wildcard = file_wildcards[FT_STL];
        break;

        default:
            wildcard = file_wildcards[FT_MODEL];
        break;
    }

    fs::path output_file(print.output_filepath(std::string()));

    switch (file_type) {
        case FT_STL: output_file.replace_extension("stl"); break;
        case FT_AMF: output_file.replace_extension("zip.amf"); break;   // XXX: Problem on OS X with double extension?
        case FT_3MF: output_file.replace_extension("3mf"); break;
        default: break;
    }

    wxGetApp().preset_bundle->export_selections(print.placeholder_parser());

    auto dlg = Slic3r::make_unique<CheckboxFileDialog>(q,
        _(L("Export print config")),
        true,
        _(L("Save file as:")),
        output_file.parent_path().string(),
        output_file.filename().string(),
        wildcard,
        wxFD_SAVE | wxFD_OVERWRITE_PROMPT
    );

    if (dlg->ShowModal() != wxID_OK) {
        return nullptr;
    }

    fs::path path(dlg->GetPath());
    wxGetApp().app_config->update_last_output_dir(path.parent_path().string());
    export_gcode_output_file = path;

    return dlg;
}

void Plater::priv::select_object(optional<size_t> obj_idx)
{
    for (auto &obj : objects) {
        obj.selected = false;
    }

    if (obj_idx) {
        objects[*obj_idx].selected = true;
    }

    selection_changed();
}

void Plater::priv::select_object_from_cpp()
{
    // TODO
}

optional<size_t> Plater::priv::selected_object() const
{
    for (size_t i = 0; i < objects.size(); i++) {
        if (objects[i].selected) { return i; }
    }

    return boost::none;
}

void Plater::priv::selection_changed()
{
    // TODO

    const auto obj_idx = selected_object();
    const bool have_sel = !!obj_idx;
    const bool layers_height_allowed = config->opt<ConfigOptionBool>("variable_layer_height")->value;

    wxWindowUpdateLocker freeze_guard(sidebar);

    _3DScene::enable_toolbar_item(canvas3D, "delete", have_sel);
    _3DScene::enable_toolbar_item(canvas3D, "more", have_sel);
    _3DScene::enable_toolbar_item(canvas3D, "fewer", have_sel);
    _3DScene::enable_toolbar_item(canvas3D, "split", have_sel);
    _3DScene::enable_toolbar_item(canvas3D, "cut", have_sel);
    _3DScene::enable_toolbar_item(canvas3D, "settings", have_sel);

    _3DScene::enable_toolbar_item(canvas3D, "layersediting", layers_height_allowed);

#if !ENABLE_EXTENDED_SELECTION
    bool can_select_by_parts = false;
#endif // !ENABLE_EXTENDED_SELECTION

    if (have_sel) {
        const auto *model_object = model.objects[*obj_idx];
#if !ENABLE_EXTENDED_SELECTION
        // XXX: ?
        can_select_by_parts = *obj_idx < 1000 && model_object->volumes.size() > 1;
#endif // !ENABLE_EXTENDED_SELECTION
        _3DScene::enable_toolbar_item(canvas3D, "fewer", model_object->instances.size() > 1);
    }

#if !ENABLE_EXTENDED_SELECTION
    if (can_select_by_parts) {
        // first disable to let the item in the toolbar to switch to the unpressed state   // XXX: ?
        _3DScene::enable_toolbar_item(canvas3D, "selectbyparts", false);
        _3DScene::enable_toolbar_item(canvas3D, "selectbyparts", true);
    } else {
        _3DScene::enable_toolbar_item(canvas3D, "selectbyparts", false);
        _3DScene::set_select_by(canvas3D, "object");
    }
#endif // !ENABLE_EXTENDED_SELECTION

    if (have_sel) {
        const auto *model_object = model.objects[*obj_idx];
        // FIXME print_info runs model fixing in two rounds, it is very slow, it should not be performed here!
        // # $model_object->print_info;

        // my $model_instance = $model_object->instances->[0];
        const auto *model_instance = model_object->instances[0];
        // TODO
        // $self->{object_info_size}->SetLabel(sprintf("%.2f x %.2f x %.2f", @{$model_object->instance_bounding_box(0)->size}));
        // $self->{object_info_materials}->SetLabel($model_object->materials_count);

        // if (my $stats = $model_object->mesh_stats) {
        //     $self->{object_info_volume}->SetLabel(sprintf('%.2f', $stats->{volume} * ($model_instance->scaling_factor**3)));
        //     $self->{object_info_facets}->SetLabel(sprintf(L('%d (%d shells)'), $model_object->facets_count, $stats->{number_of_parts}));
        //     if (my $errors = sum(@$stats{qw(degenerate_facets edges_fixed facets_removed facets_added facets_reversed backwards_edges)})) {
        //         $self->{object_info_manifold}->SetLabel(sprintf(L("Auto-repaired (%d errors)"), $errors));
        //         #$self->{object_info_manifold_warning_icon}->Show;
        //         $self->{"object_info_manifold_warning_icon_show"}->(1);

        //         # we don't show normals_fixed because we never provide normals
        //         # to admesh, so it generates normals for all facets
        //         my $message = sprintf L('%d degenerate facets, %d edges fixed, %d facets removed, %d facets added, %d facets reversed, %d backwards edges'),
        //             @$stats{qw(degenerate_facets edges_fixed facets_removed facets_added facets_reversed backwards_edges)};
        //         $self->{object_info_manifold}->SetToolTipString($message);
        //         $self->{object_info_manifold_warning_icon}->SetToolTipString($message);
        //     } else {
        //         $self->{object_info_manifold}->SetLabel(L("Yes"));
        //         #$self->{object_info_manifold_warning_icon}->Hide;
        //         $self->{"object_info_manifold_warning_icon_show"}->(0);
        //         $self->{object_info_manifold}->SetToolTipString("");
        //         $self->{object_info_manifold_warning_icon}->SetToolTipString("");
        //     }
        // } else {
        //     $self->{object_info_facets}->SetLabel($object->facets);
        // }
    } else {
        // $self->{"object_info_$_"}->SetLabel("") for qw(size volume facets materials manifold);
        // $self->{"object_info_manifold_warning_icon_show"}->(0);
        // $self->{object_info_manifold}->SetToolTipString("");
        // $self->{object_info_manifold_warning_icon}->SetToolTipString("");
    }

    q->Layout();
}

void Plater::priv::object_list_changed()
{
    // Enable/disable buttons depending on whether there are any objects on the platter.
    const bool have_objects = !objects.empty();

    _3DScene::enable_toolbar_item(canvas3D, "deleteall", have_objects);
    _3DScene::enable_toolbar_item(canvas3D, "arrange", have_objects);

    const bool export_in_progress = !(export_gcode_output_file.empty() && send_gcode_file.empty());
    // XXX: is this right?
    const bool model_fits = _3DScene::check_volumes_outside_state(canvas3D, config) == ModelInstance::PVS_Inside;

    sidebar->enable_buttons(have_objects && !export_in_progress && model_fits);
}

void Plater::priv::select_view()
{
    // TODO
}

void Plater::priv::remove(size_t obj_idx)
{
    // $self->stop_background_process;   // TODO

    // Prevent toolpaths preview from rendering while we modify the Print object
    preview->set_enabled(false);

    objects.erase(objects.begin() + obj_idx);
    model.delete_object(obj_idx);
    print.delete_object(obj_idx);
    // Delete object from Sidebar list
    sidebar->obj_list()->delete_object_from_list(obj_idx);

    object_list_changed();

    select_object(boost::none);
    update();
}

void Plater::priv::reset()
{
    // $self->stop_background_process;   // TODO

    // Prevent toolpaths preview from rendering while we modify the Print object
    preview->set_enabled(false);

    objects.clear();
    model.clear_objects();
    print.clear_objects();

    // Delete all objects from list on c++ side
    sidebar->obj_list()->delete_all_objects_from_list();
    object_list_changed();

    select_object(boost::none);
    update();
}

void Plater::priv::rotate()
{
    // TODO
}

void Plater::priv::mirror(const Axis &axis)
{
    const auto obj_idx = selected_object();
    if (! obj_idx) { return; }

    auto *model_object = model.objects[*obj_idx];
    auto *model_instance = model_object->instances[0];

    // XXX: ?
    // # apply Z rotation before mirroring
    // if ($model_instance->rotation != 0) {
    //     $model_object->rotate($model_instance->rotation, Slic3r::Pointf3->new(0, 0, 1));
    //     $_->set_rotation(0) for @{ $model_object->instances };
    // }

    model_object->mirror(axis);

    // $self->stop_background_process;  // TODO
    print.add_model_object(model_object, *obj_idx);
    selection_changed();
    update();
}

void Plater::priv::scale()
{
    // TODO
}

void Plater::priv::arrange()
{
    // $self->stop_background_process;

    main_frame->app_controller()->arrange_model();

    // ignore arrange failures on purpose: user has visual feedback and we don't need to warn him
    // when parts don't fit in print bed

    update();
}

void Plater::priv::split_object()
{
    // TODO
}

void Plater::priv::schedule_background_process()
{
    // TODO
}

void Plater::priv::async_apply_config()
{
    // TODO
}

void Plater::priv::start_background_process()
{
    // TODO
}

void Plater::priv::stop_background_process()
{
    // TODO
}

void Plater::priv::reload_from_disk()
{
    // TODO
}

void Plater::priv::export_object_stl()
{
    // TODO
}

void Plater::priv::fix_through_netfabb()
{
    // TODO
}

void Plater::priv::item_changed_selection()
{
    // TODO
}

void Plater::priv::on_notebook_changed(wxBookCtrlEvent&)
{
    const auto current_id = notebook->GetCurrentPage()->GetId();
    if (current_id == canvas3D->GetId()) {
        if (_3DScene::is_reload_delayed(canvas3D)) {
#if !ENABLE_EXTENDED_SELECTION
            _3DScene::set_objects_selections(canvas3D, collect_selections());
#endif // !ENABLE_EXTENDED_SELECTION
            _3DScene::reload_scene(canvas3D, true);
        }
        // sets the canvas as dirty to force a render at the 1st idle event (wxWidgets IsShownOnScreen() is buggy and cannot be used reliably)
        _3DScene::set_as_dirty(canvas3D);
    } else if (current_id == preview->GetId()) {
        preview->reload_print();
        preview->set_canvas_as_dirty();
    }
}

void Plater::priv::on_select_preset(wxCommandEvent &evt)
{
    auto preset_type = static_cast<Preset::Type>(evt.GetInt());
    auto *combo = static_cast<PresetComboBox*>(evt.GetEventObject());

    auto idx = combo->get_extruder_idx();

    if (preset_type == Preset::TYPE_FILAMENT) {
        wxGetApp().preset_bundle->set_filament_preset(idx, combo->GetStringSelection().ToStdString());
    }

    // TODO: ?
    if (preset_type == Preset::TYPE_FILAMENT && sidebar->is_multifilament()) {
        // Only update the platter UI for the 2nd and other filaments.
        wxGetApp().preset_bundle->update_platter_filament_ui(idx, combo);
    } 
    else {
        for (Tab* tab : wxGetApp().tabs_list) {
            if (tab->type() == preset_type) {
                tab->select_preset(combo->GetStringSelection().ToStdString());
                break;
            }
        }
    }

    // Synchronize config.ini with the current selections.
    wxGetApp().preset_bundle->export_selections(*wxGetApp().app_config);
    // TODO:
    // # get new config and generate on_config_change() event for updating plater and other things
    // $self->on_config_change(wxTheApp->{preset_bundle}->full_config);
}

void Plater::priv::on_progress_event()
{
    // TODO
}

void Plater::priv::on_update_print_preview(wxCommandEvent &)
{
    // TODO
}

void Plater::priv::on_process_completed(wxCommandEvent &)
{
    // TODO
}

void Plater::priv::on_layer_editing_toggled(bool enable)
{
    _3DScene::enable_layers_editing(canvas3D, enable);
    if (enable && !_3DScene::is_layers_editing_enabled(canvas3D)) {
        // Initialization of the OpenGL shaders failed. Disable the tool.
        _3DScene::enable_toolbar_item(canvas3D, "layersediting", false);
    }
    canvas3D->Refresh();
    canvas3D->Update();
}

void Plater::priv::on_action_add(SimpleEvent&)
{
    wxArrayString input_files;
    wxGetApp().open_model(q, input_files);

    std::vector<fs::path> input_paths;
    for (const auto &file : input_files) {
        input_paths.push_back(file.wx_str());
    }
    load_files(input_paths);
}

void Plater::priv::on_action_split(SimpleEvent&)
{
    // TODO
}

void Plater::priv::on_action_cut(SimpleEvent&)
{
    // TODO
}

void Plater::priv::on_action_settings(SimpleEvent&)
{
    // TODO
}

void Plater::priv::on_action_layersediting(SimpleEvent&)
{
    // TODO
}

#if !ENABLE_EXTENDED_SELECTION
void Plater::priv::on_action_selectbyparts(SimpleEvent&)
{
    // TODO
}
#endif // !ENABLE_EXTENDED_SELECTION

void Plater::priv::on_object_select(ObjectSelectEvent &evt)
{
    const auto obj_idx = evt.object_id();
    const auto vol_idx = evt.volume_id();

    if (obj_idx >= 0 && obj_idx < 1000 && vol_idx == -1) {
        // Ignore the special objects (the wipe tower proxy and such).
        select_object(obj_idx);
        item_changed_selection();
    }
#if ENABLE_EXTENDED_SELECTION
    wxGetApp().obj_list()->update_selections();
#endif // ENABLE_EXTENDED_SELECTION
}

void Plater::priv::on_viewport_changed(SimpleEvent& evt)
{
    wxObject* o = evt.GetEventObject();
    if (o == preview->get_wxglcanvas())
        preview->set_viewport_into_scene(canvas3D);
    else if (o == canvas3D)
        preview->set_viewport_from_scene(canvas3D);
}

void Plater::priv::on_right_click(Vec2dEvent&)
{
    // TODO
}

void Plater::priv::on_model_update(SimpleEvent&)
{
    // TODO
}

void Plater::priv::on_scale_uniformly(SimpleEvent&)
{
//     my ($scale) = @_;

//     my ($obj_idx, $object) = $self->selected_object;
    const auto obj_idx = selected_object();
    if (! obj_idx) { return; }
//     return if !defined $obj_idx;

//     my $model_object = $self->{model}->objects->[$obj_idx];
//     my $model_instance = $model_object->instances->[0];

//     $self->stop_background_process;

//     my $variation = $scale / $model_instance->scaling_factor;
//     #FIXME Scale the layer height profile?
//     foreach my $range (@{ $model_object->layer_height_ranges }) {
//         $range->[0] *= $variation;
//         $range->[1] *= $variation;
//     }
//     $_->set_scaling_factor($scale) for @{ $model_object->instances };

//     # Set object scale on c++ side
// #        Slic3r::GUI::set_object_scale($obj_idx, $model_object->instances->[0]->scaling_factor * 100); 

// #        $object->transform_thumbnail($self->{model}, $obj_idx);

//     #update print and start background processing
//     $self->{print}->add_model_object($model_object, $obj_idx);

//     $self->selection_changed(1);  # refresh info (size, volume etc.)
//     $self->update;
//     $self->schedule_background_process;
}

void Plater::priv::on_wipetower_moved(Vec3dEvent &evt)
{
    DynamicPrintConfig cfg;
    cfg.opt<ConfigOptionFloat>("wipe_tower_x", true)->value = evt.data(0);
    cfg.opt<ConfigOptionFloat>("wipe_tower_y", true)->value = evt.data(1);
    main_frame->get_preset_tab("print")->load_config(cfg);
}

void Plater::priv::on_enable_action_buttons(Event<bool>&)
{
    // TODO
}

void Plater::priv::on_update_geometry(Vec3dsEvent<2>&)
{
    // TODO
}


// Plater / Public

Plater::Plater(wxWindow *parent, MainFrame *main_frame)
    : wxPanel(parent), p(new priv(this, main_frame))
{
    // Initialization performed in the private c-tor
}

Plater::~Plater()
{
    _3DScene::remove_canvas(p->canvas3D);
}

Sidebar& Plater::sidebar() { return *p->sidebar; }
Model& Plater::model()  { return p->model; }
Print& Plater::print()  { return p->print; }

void Plater::load_files(const std::vector<fs::path> &input_files) { p->load_files(input_files); }

void Plater::update(bool force_autocenter) { p->update(force_autocenter); }
void Plater::remove(size_t obj_idx) { p->remove(obj_idx); }

void Plater::remove_selected()
{
    const auto selected = p->selected_object();
    if (selected) {
        remove(*selected);
    }
}

void Plater::increase(size_t num)
{
    const auto obj_idx = p->selected_object();
    if (! obj_idx) { return; }

    auto *model_object = p->model.objects[*obj_idx];
    auto *model_instance = model_object->instances[model_object->instances.size() - 1];

    // $self->stop_background_process;

    float offset = 10.0;
    for (size_t i = 0; i < num; i++, offset += 10.0) {
        Vec3d offset_vec = model_instance->get_offset() + Vec3d(offset, offset, 0.0);
        auto *new_instance = model_object->add_instance(offset_vec, model_instance->get_scaling_factor(), model_instance->get_rotation());
        p->print.get_object(*obj_idx)->add_copy(Slic3r::to_2d(offset_vec));
    }

    sidebar().obj_list()->set_object_count(*obj_idx, model_object->instances.size());

    if (p->get_config("autocenter") == "1") {
        p->arrange();
    } else {
        p->update();
    }

    p->selection_changed();

    // $self->schedule_background_process;
}

void Plater::decrease(size_t num)
{
    const auto obj_idx = p->selected_object();
    if (! obj_idx) { return; }

    auto *model_object = p->model.objects[*obj_idx];
    if (model_object->instances.size() > num) {
        for (size_t i = 0; i < num; i++) {
            model_object->delete_last_instance();
            p->print.get_object(*obj_idx)->delete_last_copy();
        }
        sidebar().obj_list()->set_object_count(*obj_idx, model_object->instances.size());
    } else {
        remove(*obj_idx);
    }

    p->update();
}

void Plater::set_number_of_copies(size_t num)
{
    const auto obj_idx = p->selected_object();
    if (! obj_idx) { return; }

    auto *model_object = p->model.objects[*obj_idx];

    auto diff = (ptrdiff_t)num - (ptrdiff_t)model_object->instances.size();
    if (diff > 0) {
        increase(diff);
    } else if (diff < 0) {
        decrease(-diff);
    }
}

fs::path Plater::export_gcode(const fs::path &output_path)
{
    if (p->objects.empty()) { return ""; }

    if (! p->export_gcode_output_file.empty()) {
        GUI::show_error(this, _(L("Another export job is currently running.")));
        return "";
    }

    std::string err = wxGetApp().preset_bundle->full_config().validate();
    if (err.empty()) {
        err = p->print.validate();
    }
    if (! err.empty()) {
        // The config is not valid
        GUI::show_error(this, _(err));
        return fs::path();
    }

    // Copy the names of active presets into the placeholder parser.
    wxGetApp().preset_bundle->export_selections(p->print.placeholder_parser());

    // select output file
    if (! output_path.empty()) {
        p->export_gcode_output_file = fs::path(p->print.output_filepath(output_path.string()));
        // FIXME: ^ errors to handle?
    } else {

        // XXX: take output path from CLI opts? Ancient Slic3r versions used to do that...

        // If possible, remove accents from accented latin characters.
        // This function is useful for generating file names to be processed by legacy firmwares.
        auto default_output_file = fs::path(Slic3r::fold_utf8_to_ascii(
            p->print.output_filepath(output_path.string())
            // FIXME: ^ errors to handle?
        ));
        auto start_dir = wxGetApp().app_config->get_last_output_dir(default_output_file.parent_path().string());
        wxFileDialog dlg(this, _(L("Save G-code file as:")),
            start_dir,
            default_output_file.filename().string(),
            GUI::file_wildcards[FT_GCODE],
            wxFD_SAVE | wxFD_OVERWRITE_PROMPT
        );

        if (dlg.ShowModal() != wxID_OK) { return ""; }
        fs::path path(dlg.GetPath());
        wxGetApp().app_config->update_last_output_dir(path.parent_path().string());
        p->export_gcode_output_file = path;
    }

    return p->export_gcode_output_file;
}

void Plater::export_stl()
{
    if (p->objects.empty()) { return; }

    auto dialog = p->get_export_file(FT_STL);
    if (! dialog) { return; }

    // Store a binary STL
    wxString path = dialog->GetPath();
    auto path_cstr = path.c_str();
    auto mesh = p->model.mesh();
    Slic3r::store_stl(path_cstr, &mesh, true);
    p->statusbar()->set_status_text(wxString::Format(_(L("STL file exported to %s")), path));
}

void Plater::export_amf()
{
    if (p->objects.empty()) { return; }

    auto dialog = p->get_export_file(FT_AMF);
    if (! dialog) { return; }

    wxString path = dialog->GetPath();
    auto path_cstr = path.c_str();

    if (Slic3r::store_amf(path_cstr, &p->model, &p->print, dialog->get_checkbox_value())) {
        // Success
        p->statusbar()->set_status_text(wxString::Format(_(L("AMF file exported to %s")), path));
    } else {
        // Failure
        p->statusbar()->set_status_text(wxString::Format(_(L("Error exporting AMF file %s")), path));
    }
}

void Plater::export_3mf()
{
    if (p->objects.empty()) { return; }

    auto dialog = p->get_export_file(FT_3MF);
    if (! dialog) { return; }

    wxString path = dialog->GetPath();
    auto path_cstr = path.c_str();

    if (Slic3r::store_3mf(path_cstr, &p->model, &p->print, dialog->get_checkbox_value())) {
        // Success
        p->statusbar()->set_status_text(wxString::Format(_(L("3MF file exported to %s")), path));
    } else {
        // Failure
        p->statusbar()->set_status_text(wxString::Format(_(L("Error exporting 3MF file %s")), path));
    }
}


void Plater::reslice()
{
    // TODO
}

void Plater::send_gcode()
{
    p->send_gcode_file = export_gcode();
}

void Plater::on_extruders_change(int num_extruders)
{
    auto& choices = sidebar().combos_filament();

    int i = choices.size();
    while ( i < num_extruders )
    {
        PresetComboBox* choice/*{ nullptr }*/;
        sidebar().init_filament_combo(&choice, i);
        choices.push_back(choice);

        // initialize selection
        wxGetApp().preset_bundle->update_platter_filament_ui(i, choice);
        ++i;
    }

    // remove unused choices if any
    sidebar().remove_unused_filament_combos(num_extruders);

    sidebar().Layout();
    GetParent()->Layout();
}

void Plater::on_config_change(DynamicPrintConfig* config)
{
    bool update_scheduled = false;
    for ( auto opt_key: p->config->diff(*config)) {
        p->config->set_key_value(opt_key, config->option(opt_key)->clone());
        if (opt_key  == "bed_shape") {
            if (p->canvas3D) _3DScene::set_bed_shape(p->canvas3D, p->config->option<ConfigOptionPoints>(opt_key)->values);
            if (p->preview) p->preview->set_bed_shape(p->config->option<ConfigOptionPoints>(opt_key)->values);
            update_scheduled = true;
        } 
        else if(opt_key == "wipe_tower" /*|| opt_key == "filament_minimal_purge_on_wipe_tower"*/ || // ? #ys_FIXME
                opt_key == "single_extruder_multi_material") {
            update_scheduled = true;
        } 
//         else if(opt_key == "serial_port") {
//             sidebar()->p->btn_print->Show(config->get("serial_port"));  // ???: btn_print is removed
//             Layout();
//         } 
        else if (opt_key == "print_host") {
            sidebar().show_button(baReslice, !p->config->option<ConfigOptionString>(opt_key)->value.empty());
            Layout();
        }
        else if(opt_key == "variable_layer_height") {
            if (p->config->opt_bool("variable_layer_height") != true) {
                _3DScene::enable_toolbar_item(p->canvas3D, "layersediting", false);
                _3DScene::enable_layers_editing(p->canvas3D, 0);
                p->canvas3D->Refresh();
                p->canvas3D->Update();
            }
            else if (_3DScene::is_layers_editing_allowed(p->canvas3D)) {
                _3DScene::enable_toolbar_item(p->canvas3D, "layersediting", true);
            }
        } 
        else if(opt_key == "extruder_colour") {
            update_scheduled = true;
            p->preview->set_number_extruders(p->config->option<ConfigOptionStrings>(opt_key)->values.size());
        } else if(opt_key == "max_print_height") {
            update_scheduled = true;
        } else if(opt_key == "printer_model") {
            // update to force bed selection(for texturing)
            if (p->canvas3D) _3DScene::set_bed_shape(p->canvas3D, p->config->option<ConfigOptionPoints>("bed_shape")->values);
            if (p->preview) p->preview->set_bed_shape(p->config->option<ConfigOptionPoints>("bed_shape")->values);
            update_scheduled = true;
        }
    }

    if (update_scheduled) 
        update();

    if (!p->main_frame->is_loaded()) return ;

    // (re)start timer
//     schedule_background_process();
}

wxGLCanvas* Plater::canvas3D()
{
    return p->canvas3D;
}

void Plater::changed_object_settings(int obj_idx)
{
    if (obj_idx < 0)
        return;
    auto list = wxGetApp().obj_list();
    wxASSERT(list != nullptr);
    if (list == nullptr)
        return;

    if (list->is_parts_changed()) {
        // recenter and re - align to Z = 0
        auto model_object = p->model.objects[obj_idx];
        model_object->center_around_origin();
    }

    // update print
    if (list->is_parts_changed() || list->is_part_settings_changed()) {
//         stop_background_process();
//         $self->{print}->reload_object($obj_idx);
//         schedule_background_process();
#if !ENABLE_EXTENDED_SELECTION
        if (p->canvas3D) _3DScene::reload_scene(p->canvas3D, true);
        auto selections = p->collect_selections();
        _3DScene::set_objects_selections(p->canvas3D, selections);
#endif // !ENABLE_EXTENDED_SELECTION
        _3DScene::reload_scene(p->canvas3D, false);
    }
    else {
//         schedule_background_process();
    }

}


}}    // namespace Slic3r::GUI
