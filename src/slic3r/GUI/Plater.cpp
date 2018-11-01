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
#include <wx/numdlg.h> 

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
#include "wxExtensions.hpp"
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


wxDEFINE_EVENT(EVT_PROGRESS_BAR,      wxCommandEvent);
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

enum SlisedInfoIdx
{
    siFilament_m,
    siFilament_mm3,
    siFilament_g,
    siCost,
    siTimeNormal,
    siTimeSilent,
    siWTNumbetOfToolchanges,

    siCount
};

class SlicedInfo : public wxStaticBoxSizer
{
public:
    SlicedInfo(wxWindow *parent);
    void SetTextAndShow(SlisedInfoIdx idx, const wxString& text);

private:
    std::vector<std::pair<wxStaticText*, wxStaticText*>> info_vec;
};

SlicedInfo::SlicedInfo(wxWindow *parent) :
    wxStaticBoxSizer(new wxStaticBox(parent, wxID_ANY, _(L("Sliced Info"))), wxVERTICAL)
{
    GetStaticBox()->SetFont(wxGetApp().bold_font());

    auto *grid_sizer = new wxFlexGridSizer(2, 5, 5);
    grid_sizer->SetFlexibleDirection(wxHORIZONTAL);
    grid_sizer->AddGrowableCol(1, 1);

    info_vec.reserve(siCount);

    auto init_info_label = [this, parent, grid_sizer](wxString text_label) {
        auto *text = new wxStaticText(parent, wxID_ANY, text_label);
        text->SetFont(wxGetApp().small_font());
        auto info_label = new wxStaticText(parent, wxID_ANY, "N/A");
        info_label->SetFont(wxGetApp().small_font());
        grid_sizer->Add(text, 0);
        grid_sizer->Add(info_label, 0);
        info_vec.push_back(std::pair<wxStaticText*, wxStaticText*>(text, info_label));
    };

    init_info_label(_(L("Used Filament (m)")));
    init_info_label(_(L("Used Filament (mm³)")));
    init_info_label(_(L("Used Filament (g)")));
    init_info_label(_(L("Cost")));
    init_info_label(_(L("Estimated printing time (normal mode)")));
    init_info_label(_(L("Estimated printing time (silent mode)")));
    init_info_label(_(L("Number of tool changes")));

    Add(grid_sizer, 0, wxEXPAND);
    this->Show(false);
}

void SlicedInfo::SetTextAndShow(SlisedInfoIdx idx, const wxString& text)
{
    const bool show = text != "N/A";
    if (show)
        info_vec[idx].second->SetLabelText(text);
    info_vec[idx].first->Show(show);
    info_vec[idx].second->Show(show);
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

                //FIXME this is too expensive to call full_config to get just the extruder color!
                auto colors = static_cast<ConfigOptionStrings*>(wxGetApp().preset_bundle->full_config().option("extruder_colour")->clone());
                colors->values[extruder_idx] = dialog->GetColourData().GetColour().GetAsString(wxC2S_HTML_SYNTAX);

                cfg.set_key_value("extruder_colour", colors);

                wxGetApp().get_tab(Preset::TYPE_PRINTER)->load_config(cfg);
                wxGetApp().preset_bundle->update_platter_filament_ui(extruder_idx, this);
                wxGetApp().plater()->on_config_change(cfg);
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
    void            Show(const bool show);
};

FreqChangedParams::FreqChangedParams(wxWindow* parent, const int label_width) :
    OG_Settings(parent, false)
{
    DynamicPrintConfig*	config = &wxGetApp().preset_bundle->prints.get_edited_preset().config;

    m_og->set_config(config);
    m_og->label_width = label_width;

    m_og->m_on_change = [config, this](t_config_option_key opt_key, boost::any value) {
        TabPrint* tab_print = nullptr;
        for (size_t i = 0; i < wxGetApp().tab_panel()->GetPageCount(); ++i) {
            Tab *tab = dynamic_cast<Tab*>(wxGetApp().tab_panel()->GetPage(i));
            if (!tab)
                continue;
            if (tab->name() == "print") {
                tab_print = static_cast<TabPrint*>(tab);
                break;
            }
        }
        if (tab_print == nullptr)
            return;

        if (opt_key == "fill_density") {
            value = m_og->get_config_value(*config, opt_key);
            tab_print->set_value(opt_key, value);
            tab_print->update();
        }
        else{
            DynamicPrintConfig new_conf = *config;
            if (opt_key == "brim") {
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
    line.widget = [config, this](wxWindow* parent) {
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


void FreqChangedParams::Show(const bool show)
{
    bool is_wdb_shown = m_wiping_dialog_button->IsShown();
    m_og->sizer->Show(show);

    // correct showing of the FreqChangedParams sizer when m_wiping_dialog_button is hidden 
    if (show && !is_wdb_shown)
        m_wiping_dialog_button->Hide();
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
    const bool showSLA = wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA;

    wxWindowUpdateLocker noUpdates(wxGetApp().mainframe);

    for (size_t i = 0; i < 4; ++i)
        sizer_presets->Show(i, !showSLA);

    sizer_presets->Show(4, showSLA);
    sizer_presets->Show(5, showSLA);

    frequently_changed_parameters->Show(!showSLA);

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
	PresetBundle &preset_bundle = *wxGetApp().preset_bundle;

    switch (preset_type) {
    case Preset::TYPE_FILAMENT:
        if (p->combos_filament.size() == 1) {
            // Single filament printer, synchronize the filament presets.
			const std::string &name = preset_bundle.filaments.get_selected_preset().name;
			preset_bundle.set_filament_preset(0, name);
        }

        for (size_t i = 0; i < p->combos_filament.size(); i++) {
			preset_bundle.update_platter_filament_ui(i, p->combos_filament[i]);
        }

        break;

    case Preset::TYPE_PRINT:
		preset_bundle.prints.update_platter_ui(p->combo_print);
        break;

    case Preset::TYPE_SLA_MATERIAL:
		preset_bundle.sla_materials.update_platter_ui(p->combo_sla_material);
        break;

	case Preset::TYPE_PRINTER:
	{
		PrinterTechnology printer_technology = preset_bundle.printers.get_edited_preset().printer_technology();
		// Update the print choosers to only contain the compatible presets, update the dirty flags.
		if (printer_technology == ptFFF)
			preset_bundle.prints.update_platter_ui(p->combo_print);
		else
			preset_bundle.sla_materials.update_platter_ui(p->combo_sla_material);
		// Update the printer choosers, update the dirty flags.
		preset_bundle.printers.update_platter_ui(p->combo_printer);
		// Update the filament choosers to only contain the compatible presets, update the color preview,
		// update the dirty flags.
        if (printer_technology == ptFFF) {
            for (size_t i = 0; i < p->combos_filament.size(); ++ i)
                preset_bundle.update_platter_filament_ui(i, p->combos_filament[i]);
		}
		p->show_preset_comboboxes();
		break;
	}

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
//     p->sliced_info->Show(show && p->show_print_info);
}

void Sidebar::show_sliced_info_sizer(const bool show) 
{
    p->plater->Freeze();
//     p->show_print_info = show;
    p->sliced_info->Show(show);
    if (show) {
        const PrintStatistics& ps = p->plater->print().print_statistics();
        const bool is_wipe_tower = ps.total_wipe_tower_filament > 0;

        wxString info_text = is_wipe_tower ?
                            wxString::Format("%.2f  (%.2f %s + %.2f %s)", ps.total_used_filament / 1000,
                                            (ps.total_used_filament - ps.total_wipe_tower_filament) / 1000, _(L("objects")),
                                            ps.total_wipe_tower_filament / 1000, _(L("wipe tower"))) :
                            wxString::Format("%.2f", ps.total_used_filament / 1000);
        p->sliced_info->SetTextAndShow(siFilament_m,    info_text);
        p->sliced_info->SetTextAndShow(siFilament_mm3,  wxString::Format("%.2f", ps.total_extruded_volume));
        p->sliced_info->SetTextAndShow(siFilament_g,    wxString::Format("%.2f", ps.total_weight));

        info_text = is_wipe_tower ?
                    wxString::Format("%.2f  (%.2f %s + %.2f %s)", ps.total_cost,
                                    (ps.total_cost - ps.total_wipe_tower_cost), _(L("objects")),
                                    ps.total_wipe_tower_cost, _(L("wipe tower"))) :
                    wxString::Format("%.2f", ps.total_cost);
        p->sliced_info->SetTextAndShow(siCost,       info_text);
        p->sliced_info->SetTextAndShow(siTimeNormal, ps.estimated_normal_print_time);
        p->sliced_info->SetTextAndShow(siTimeSilent, ps.estimated_silent_print_time);

        // if there is a wipe tower, insert number of toolchanges info into the array:
        p->sliced_info->SetTextAndShow(siWTNumbetOfToolchanges, is_wipe_tower ? wxString::Format("%.d", p->plater->print().wipe_tower_data().number_of_toolchanges) : "N/A");
    }

    p->scrolled->Layout();
    p->plater->Layout();
    p->plater->Thaw();
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

#if !ENABLE_EXTENDED_SELECTION
// Plater::Object

struct PlaterObject
{
    std::string name;
    bool selected;

    PlaterObject(std::string name) : name(std::move(name)), selected(false) {}
};
#endif // !ENABLE_EXTENDED_SELECTION

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

const std::regex PlaterDropTarget::pattern_drop(".*[.](stl|obj|amf|3mf|prusa)", std::regex::icase);

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

    // Object popup menu
    wxMenu object_menu;

    // Data
    Slic3r::DynamicPrintConfig *config;
    Slic3r::Print print;
    Slic3r::Model model;
    Slic3r::GCodePreviewData gcode_preview_data;
#if !ENABLE_EXTENDED_SELECTION
    std::vector<PlaterObject> objects;
#endif // !ENABLE_EXTENDED_SELECTION

    // GUI elements
    wxNotebook *notebook;
    Sidebar *sidebar;
    wxGLCanvas *canvas3D;    // TODO: Use GLCanvas3D when we can
    Preview *preview;
    BackgroundSlicingProcess    background_process;
    wxTimer                     background_process_timer;

    static const std::regex pattern_bundle;
    static const std::regex pattern_3mf;
    static const std::regex pattern_zip_amf;

    priv(Plater *q, MainFrame *main_frame);

#if !ENABLE_EXTENDED_SELECTION
    std::vector<int> collect_selections();
#endif // !ENABLE_EXTENDED_SELECTION
    void update(bool force_autocenter = false);
    void select_view(const std::string& direction);
    void update_ui_from_settings();
    ProgressStatusBar* statusbar();
    std::string get_config(const std::string &key) const;
    BoundingBoxf bed_shape_bb() const;
    BoundingBox scaled_bed_shape_bb() const;
    std::vector<size_t> load_files(const std::vector<fs::path> &input_files);
    std::vector<size_t> load_model_objects(const ModelObjectPtrs &model_objects);
    std::unique_ptr<CheckboxFileDialog> get_export_file(GUI::FileType file_type);

#if ENABLE_EXTENDED_SELECTION
    const GLCanvas3D::Selection& get_selection() const;
    GLCanvas3D::Selection& get_selection();
    int get_selected_object_idx() const;
#else
    void select_object(optional<size_t> obj_idx);
    void select_object_from_cpp();
    optional<size_t> selected_object() const;
#endif // ENABLE_EXTENDED_SELECTION
    void selection_changed();
    void object_list_changed();
#if !ENABLE_EXTENDED_SELECTION
    void select_view();
#endif // !ENABLE_EXTENDED_SELECTION

    void remove(size_t obj_idx);
    void reset();
#if !ENABLE_EXTENDED_SELECTION
    void rotate();
#endif // !ENABLE_EXTENDED_SELECTION
    void mirror(Axis axis);
#if !ENABLE_EXTENDED_SELECTION
    void scale();
#endif // !ENABLE_EXTENDED_SELECTION
    void arrange();
    void split_object();
    void split_volume();
    void schedule_background_process();
    void async_apply_config();
    void start_background_process();
    void reload_from_disk();
    void export_object_stl();
    void fix_through_netfabb();

    void on_notebook_changed(wxBookCtrlEvent&);
    void on_select_preset(wxCommandEvent&);
    void on_progress_event(wxCommandEvent&);
    void on_update_print_preview(wxCommandEvent&);
    void on_process_completed(wxCommandEvent&);
    void on_layer_editing_toggled(bool enable);

    void on_schedule_background_process(SimpleEvent&);

    void on_action_add(SimpleEvent&);
    void on_action_split_objects(SimpleEvent&);
    void on_action_split_volumes(SimpleEvent&);
    void on_action_cut(SimpleEvent&);
#if !ENABLE_EXTENDED_SELECTION
    void on_action_settings(SimpleEvent&);
#endif // !ENABLE_EXTENDED_SELECTION
    void on_action_layersediting(SimpleEvent&);
#if !ENABLE_EXTENDED_SELECTION
    void on_action_selectbyparts(SimpleEvent&);
#endif // !ENABLE_EXTENDED_SELECTION

#if ENABLE_EXTENDED_SELECTION
    void on_object_select(SimpleEvent&);
#else
    void on_object_select(ObjectSelectEvent&);
#endif // ENABLE_EXTENDED_SELECTION
    void on_viewport_changed(SimpleEvent&);
    void on_right_click(Vec2dEvent&);
    void on_model_update(SimpleEvent&);
#if !ENABLE_EXTENDED_SELECTION
    void on_scale_uniformly(SimpleEvent&);
#endif // !ENABLE_EXTENDED_SELECTION
    void on_wipetower_moved(Vec3dEvent&);
    void on_enable_action_buttons(Event<bool>&);
    void on_update_geometry(Vec3dsEvent<2>&);

private:
    bool init_object_menu();

#if ENABLE_EXTENDED_SELECTION
    bool can_delete_object() const;
    bool can_increase_instances() const;
    bool can_decrease_instances() const;
    bool can_split_to_objects() const;
    bool can_split_to_volumes() const;
    bool can_cut_object() const;
    bool layers_height_allowed() const;
    bool can_delete_all() const;
    bool can_arrange() const;
    bool can_mirror() const;
#endif // ENABLE_EXTENDED_SELECTION
};

const std::regex Plater::priv::pattern_bundle(".*[.](amf|amf[.]xml|zip[.]amf|3mf|prusa)", std::regex::icase);
const std::regex Plater::priv::pattern_3mf(".*3mf", std::regex::icase);
const std::regex Plater::priv::pattern_zip_amf(".*[.]zip[.]amf", std::regex::icase);

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
    // Register progress callback from the Print class to the Platter.
    print.set_status_callback([this](int percent, const std::string &message) {
        wxCommandEvent event(EVT_PROGRESS_BAR);
        event.SetInt(percent);
        event.SetString(message);
        wxQueueEvent(this->q, event.Clone());
    });
    this->q->Bind(EVT_PROGRESS_BAR, &priv::on_progress_event, this);

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

    this->background_process_timer.SetOwner(this->q, 0);
    this->q->Bind(wxEVT_TIMER, [this](wxTimerEvent &evt) { this->async_apply_config(); });

    auto *bed_shape = config->opt<ConfigOptionPoints>("bed_shape");
    _3DScene::set_bed_shape(canvas3D, bed_shape->values);
    _3DScene::zoom_to_bed(canvas3D);
    preview->set_bed_shape(bed_shape->values);

    update();

    auto *hsizer = new wxBoxSizer(wxHORIZONTAL);
    hsizer->Add(notebook, 1, wxEXPAND | wxTOP, 1);
    hsizer->Add(sidebar, 0, wxEXPAND | wxLEFT | wxRIGHT, 0);
    q->SetSizer(hsizer);

    init_object_menu();

    // Events:

    // Notebook page change event
    notebook->Bind(wxEVT_NOTEBOOK_PAGE_CHANGED, &priv::on_notebook_changed, this);

    // Preset change event
    sidebar->Bind(wxEVT_COMBOBOX, &priv::on_select_preset, this);

    // 3DScene events:
    canvas3D->Bind(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS, &priv::on_schedule_background_process, this);
    canvas3D->Bind(EVT_GLCANVAS_OBJECT_SELECT, &priv::on_object_select, this);
    canvas3D->Bind(EVT_GLCANVAS_VIEWPORT_CHANGED, &priv::on_viewport_changed, this);
#if !ENABLE_EXTENDED_SELECTION
    canvas3D->Bind(EVT_GLCANVAS_DOUBLE_CLICK, [](SimpleEvent&) {});  // XXX: remove?
#endif // !ENABLE_EXTENDED_SELECTION
    canvas3D->Bind(EVT_GLCANVAS_RIGHT_CLICK, &priv::on_right_click, this);
    canvas3D->Bind(EVT_GLCANVAS_MODEL_UPDATE, &priv::on_model_update, this);
    canvas3D->Bind(EVT_GLCANVAS_REMOVE_OBJECT, [q](SimpleEvent&) { q->remove_selected(); });
    canvas3D->Bind(EVT_GLCANVAS_ARRANGE, [this](SimpleEvent&) { arrange(); });
#if !ENABLE_EXTENDED_SELECTION
    canvas3D->Bind(EVT_GLCANVAS_ROTATE_OBJECT, [this](Event<int> &evt) { /*TODO: call rotate */ });
    canvas3D->Bind(EVT_GLCANVAS_SCALE_UNIFORMLY, [this](SimpleEvent&) { this->scale(); });
#endif // !ENABLE_EXTENDED_SELECTION
    canvas3D->Bind(EVT_GLCANVAS_INCREASE_INSTANCES, [q](Event<int> &evt) { evt.data == 1 ? q->increase_instances() : q->decrease_instances(); });
    canvas3D->Bind(EVT_GLCANVAS_INSTANCE_MOVED, [this](SimpleEvent&) { update(); });
    canvas3D->Bind(EVT_GLCANVAS_WIPETOWER_MOVED, &priv::on_wipetower_moved, this);
    canvas3D->Bind(EVT_GLCANVAS_ENABLE_ACTION_BUTTONS, &priv::on_enable_action_buttons, this);
    canvas3D->Bind(EVT_GLCANVAS_UPDATE_GEOMETRY, &priv::on_update_geometry, this);
    // 3DScene/Toolbar:
    canvas3D->Bind(EVT_GLTOOLBAR_ADD, &priv::on_action_add, this);
    canvas3D->Bind(EVT_GLTOOLBAR_DELETE, [q](SimpleEvent&) { q->remove_selected(); } );
    canvas3D->Bind(EVT_GLTOOLBAR_DELETE_ALL, [this](SimpleEvent&) { reset(); });
    canvas3D->Bind(EVT_GLTOOLBAR_ARRANGE, [this](SimpleEvent&) { arrange(); });
    canvas3D->Bind(EVT_GLTOOLBAR_MORE, [q](SimpleEvent&) { q->increase_instances(); });
    canvas3D->Bind(EVT_GLTOOLBAR_FEWER, [q](SimpleEvent&) { q->decrease_instances(); });
    canvas3D->Bind(EVT_GLTOOLBAR_SPLIT_OBJECTS, &priv::on_action_split_objects, this);
    canvas3D->Bind(EVT_GLTOOLBAR_SPLIT_VOLUMES, &priv::on_action_split_volumes, this);
    canvas3D->Bind(EVT_GLTOOLBAR_CUT, &priv::on_action_cut, this);
#if !ENABLE_EXTENDED_SELECTION
    canvas3D->Bind(EVT_GLTOOLBAR_SETTINGS, &priv::on_action_settings, this);
#endif // !ENABLE_EXTENDED_SELECTION
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

#if !ENABLE_EXTENDED_SELECTION
    const auto selections = collect_selections();
    _3DScene::set_objects_selections(canvas3D, selections);
#endif // !ENABLE_EXTENDED_SELECTION
    _3DScene::reload_scene(canvas3D, false);
    preview->reset_gcode_preview_data();
    preview->reload_print();

    this->schedule_background_process();
}

void Plater::priv::select_view(const std::string& direction)
{
    int page_id = notebook->GetSelection();
    if (page_id != wxNOT_FOUND)
    {
        const wxString& page_text = notebook->GetPageText(page_id);
        if (page_text == _(L("3D")))
            _3DScene::select_view(canvas3D, direction);
        else if (page_text == _(L("Preview")))
            preview->select_view(direction);
    }
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

ProgressStatusBar* Plater::priv::statusbar()
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
                for (auto obj : model.objects)
                    if (obj->name.empty())
                        obj->name = fs::path(obj->input_file).filename().string();
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
                model_object->ensure_on_bed();
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

        auto loaded_idxs = load_model_objects(new_model->objects);
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
#if ENABLE_EXTENDED_SELECTION
    unsigned int obj_count = model.objects.size();
#endif // ENABLE_EXTENDED_SELECTION

    for (ModelObject *model_object : model_objects) {
        auto *object = model.add_object(*model_object);
        std::string object_name = object->name.empty() ? fs::path(object->input_file).filename().string() : object->name;
#if ENABLE_EXTENDED_SELECTION
        obj_idxs.push_back(obj_count++);
#else
        objects.emplace_back(std::move(object_name));
        obj_idxs.push_back(objects.size() - 1);
#endif // ENABLE_EXTENDED_SELECTION

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

        object->ensure_on_bed();

        // print.auto_assign_extruders(object);
        // print.add_model_object(object);
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
#if !ENABLE_MODIFIED_CAMERA_TARGET
    _3DScene::zoom_to_volumes(canvas3D);
#endif // !ENABLE_MODIFIED_CAMERA_TARGET
    object_list_changed();

    this->schedule_background_process();

    return obj_idxs;
}

std::unique_ptr<CheckboxFileDialog> Plater::priv::get_export_file(GUI::FileType file_type)
{
    wxString wildcard;
    switch (file_type) {
        case FT_STL:
        case FT_AMF:
        case FT_3MF:
        case FT_GCODE:
            wildcard = file_wildcards[file_type];
        break;
// FT_GCODE
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

    return dlg;
}

#if ENABLE_EXTENDED_SELECTION
const GLCanvas3D::Selection& Plater::priv::get_selection() const
{
    return _3DScene::get_canvas(canvas3D)->get_selection();
}

GLCanvas3D::Selection& Plater::priv::get_selection()
{
    return _3DScene::get_canvas(canvas3D)->get_selection();
}

int Plater::priv::get_selected_object_idx() const
{
    int idx = get_selection().get_object_idx();
    return ((0 <= idx) && (idx < 1000)) ? idx : -1;
}
#else
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
#endif // ENABLE_EXTENDED_SELECTION

void Plater::priv::selection_changed()
{
#if !ENABLE_EXTENDED_SELECTION
    const auto obj_idx = selected_object();
    const bool have_sel = !!obj_idx;
#endif // !ENABLE_EXTENDED_SELECTION

#if ENABLE_EXTENDED_SELECTION
    _3DScene::enable_toolbar_item(canvas3D, "delete", can_delete_object());
    _3DScene::enable_toolbar_item(canvas3D, "more", can_increase_instances());
    _3DScene::enable_toolbar_item(canvas3D, "fewer", can_decrease_instances());
    _3DScene::enable_toolbar_item(canvas3D, "splitobjects", can_split_to_objects());
    _3DScene::enable_toolbar_item(canvas3D, "splitvolumes", can_split_to_volumes());
    _3DScene::enable_toolbar_item(canvas3D, "cut", can_cut_object());
    _3DScene::enable_toolbar_item(canvas3D, "layersediting", layers_height_allowed());
    // forces a frame render to update the view (to avoid a missed update if, for example, the context menu appears)
    _3DScene::render(canvas3D);
#else
    _3DScene::enable_toolbar_item(canvas3D, "fewer", have_sel);
    _3DScene::enable_toolbar_item(canvas3D, "splitobjects", have_sel);
    _3DScene::enable_toolbar_item(canvas3D, "splitvolumes", have_sel);
    _3DScene::enable_toolbar_item(canvas3D, "cut", have_sel);
    _3DScene::enable_toolbar_item(canvas3D, "settings", have_sel);
    _3DScene::enable_toolbar_item(canvas3D, "layersediting", have_sel && config->opt_bool("variable_layer_height") && _3DScene::is_layers_editing_allowed(canvas3D));
#endif // ENABLE_EXTENDED_SELECTION

#if ENABLE_EXTENDED_SELECTION
    int obj_idx = get_selected_object_idx();
    bool have_sel = (obj_idx != -1);
#else
    bool can_select_by_parts = false;

    if (have_sel) {
        const auto *model_object = model.objects[*obj_idx];
        // XXX: ?
        can_select_by_parts = *obj_idx < 1000 && model_object->volumes.size() > 1;
        _3DScene::enable_toolbar_item(canvas3D, "fewer", model_object->instances.size() > 1);
    }

    if (can_select_by_parts) {
        // first disable to let the item in the toolbar to switch to the unpressed state   // XXX: ?
        _3DScene::enable_toolbar_item(canvas3D, "selectbyparts", false);
        _3DScene::enable_toolbar_item(canvas3D, "selectbyparts", true);
    } else {
        _3DScene::enable_toolbar_item(canvas3D, "selectbyparts", false);
        _3DScene::set_select_by(canvas3D, "object");
    }
#endif // ENABLE_EXTENDED_SELECTION

    wxWindowUpdateLocker freeze_guard(sidebar);
    if (have_sel) {
#if ENABLE_EXTENDED_SELECTION
        const ModelObject* model_object = model.objects[obj_idx];
#else
        const auto *model_object = model.objects[*obj_idx];
#endif // ENABLE_EXTENDED_SELECTION
        // FIXME print_info runs model fixing in two rounds, it is very slow, it should not be performed here!
        // # $model_object->print_info;

        const ModelInstance* model_instance = !model_object->instances.empty() ? model_object->instances.front() : nullptr;
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
#if ENABLE_EXTENDED_SELECTION
    _3DScene::enable_toolbar_item(canvas3D, "deleteall", can_delete_all());
    _3DScene::enable_toolbar_item(canvas3D, "arrange", can_arrange());
#else
    const bool have_objects = !objects.empty();
    _3DScene::enable_toolbar_item(canvas3D, "deleteall", have_objects);
    _3DScene::enable_toolbar_item(canvas3D, "arrange", have_objects);
#endif // ENABLE_EXTENDED_SELECTION

    const bool export_in_progress = this->background_process.is_export_scheduled(); // || ! send_gcode_file.empty());
    // XXX: is this right?
    const bool model_fits = _3DScene::check_volumes_outside_state(canvas3D, config) == ModelInstance::PVS_Inside;

#if ENABLE_EXTENDED_SELECTION
    sidebar->enable_buttons(!model.objects.empty() && !export_in_progress && model_fits);
#else
    sidebar->enable_buttons(have_objects && !export_in_progress && model_fits);
#endif // ENABLE_EXTENDED_SELECTION
}

#if !ENABLE_EXTENDED_SELECTION
void Plater::priv::select_view()
{
    // TODO
}
#endif // !ENABLE_EXTENDED_SELECTION

void Plater::priv::remove(size_t obj_idx)
{
    // Prevent toolpaths preview from rendering while we modify the Print object
    preview->set_enabled(false);

    if (_3DScene::is_layers_editing_enabled(canvas3D))
        _3DScene::enable_layers_editing(canvas3D, false);

#if !ENABLE_EXTENDED_SELECTION
    objects.erase(objects.begin() + obj_idx);
#endif // !ENABLE_EXTENDED_SELECTION
    model.delete_object(obj_idx);
//    print.delete_object(obj_idx);
    // Delete object from Sidebar list
    sidebar->obj_list()->delete_object_from_list(obj_idx);

    object_list_changed();

#if !ENABLE_EXTENDED_SELECTION
    select_object(boost::none);
#endif // !ENABLE_EXTENDED_SELECTION
    update();
}

void Plater::priv::reset()
{
    // Prevent toolpaths preview from rendering while we modify the Print object
    preview->set_enabled(false);

    if (_3DScene::is_layers_editing_enabled(canvas3D))
        _3DScene::enable_layers_editing(canvas3D, false);

#if !ENABLE_EXTENDED_SELECTION
    objects.clear();
#endif // !ENABLE_EXTENDED_SELECTION
    model.clear_objects();
//    print.clear_objects();

    // Delete all objects from list on c++ side
    sidebar->obj_list()->delete_all_objects_from_list();
    object_list_changed();

#if !ENABLE_EXTENDED_SELECTION
    select_object(boost::none);
#endif // !ENABLE_EXTENDED_SELECTION
    update();
}

#if !ENABLE_EXTENDED_SELECTION
void Plater::priv::rotate()
{
    // TODO
}
#endif // !ENABLE_EXTENDED_SELECTION

void Plater::priv::mirror(Axis axis)
{
#if ENABLE_EXTENDED_SELECTION
    _3DScene::mirror_selection(canvas3D, axis);
#endif // ENABLE_EXTENDED_SELECTION
}

#if !ENABLE_EXTENDED_SELECTION
void Plater::priv::scale()
{
    // TODO
}
#endif // !ENABLE_EXTENDED_SELECTION

void Plater::priv::arrange()
{
    main_frame->app_controller()->arrange_model();

    // ignore arrange failures on purpose: user has visual feedback and we don't need to warn him
    // when parts don't fit in print bed

    update();
}

void Plater::priv::split_object()
{
#if ENABLE_EXTENDED_SELECTION
    int obj_idx = get_selected_object_idx();
    if (obj_idx == -1)
        return;

    // we clone model object because split_object() adds the split volumes
    // into the same model object, thus causing duplicates when we call load_model_objects()
    Model new_model = model;
    ModelObject* current_model_object = new_model.objects[obj_idx];

    if (current_model_object->volumes.size() > 1)
    {
        Slic3r::GUI::warning_catcher(q, _(L("The selected object can't be split because it contains more than one volume/material.")));
        return;
    }

    ModelObjectPtrs new_objects;
    current_model_object->split(&new_objects);
    if (new_objects.size() == 1)
        Slic3r::GUI::warning_catcher(q, _(L("The selected object couldn't be split because it contains only one part.")));
    else
    {
        unsigned int counter = 1;
        for (ModelObject* m : new_objects)
        {
            m->name = current_model_object->name + "_" + std::to_string(counter++);
            for (ModelInstance* i : current_model_object->instances)
            {
                m->add_instance(*i);
            }
        }

        remove(obj_idx);

        // load all model objects at once, otherwise the plate would be rearranged after each one
        // causing original positions not to be kept
        std::vector<size_t> idxs = load_model_objects(new_objects);

        // select newly added objects
        for (size_t idx : idxs)
        {
            get_selection().add_object((unsigned int)idx, false);
        }
    }
#endif // ENABLE_EXTENDED_SELECTION
}

void Plater::priv::split_volume()
{
    wxGetApp().obj_list()->split(false);
}

void Plater::priv::schedule_background_process()
{
    // Trigger the timer event after 0.5s
    this->background_process_timer.Start(500, wxTIMER_ONE_SHOT);
}

void Plater::priv::async_apply_config()
{
    DynamicPrintConfig config = wxGetApp().preset_bundle->full_config();
    BoundingBox     bed_box_2D = get_extents(Polygon::new_scale(config.opt<ConfigOptionPoints>("bed_shape")->values));
    BoundingBoxf3   print_volume(unscale(bed_box_2D.min(0), bed_box_2D.min(1), 0.0), unscale(bed_box_2D.max(0), bed_box_2D.max(1), scale_(config.opt_float("max_print_height"))));
    // Allow the objects to protrude below the print bed, only the part of the object above the print bed will be sliced.
    print_volume.min(2) = -1e10;
    this->q->model().update_print_volume_state(print_volume);

    // Apply new config to the possibly running background task.
    Print::ApplyStatus invalidated = this->background_process.apply(this->q->model(), std::move(config));
    // Just redraw the 3D canvas without reloading the scene to consume the update of the layer height profile.
    if (Slic3r::_3DScene::is_layers_editing_enabled(this->canvas3D))
        this->canvas3D->Refresh();

    if (invalidated == Print::APPLY_STATUS_INVALIDATED) {
        // Some previously calculated data on the Print was invalidated.
        // Hide the slicing results, as the current slicing status is no more valid.
        this->sidebar->show_sliced_info_sizer(false);
        // Reset preview canvases. If the print has been invalidated, the preview canvases will be cleared.
        // Otherwise they will be just refreshed.
        this->gcode_preview_data.reset();
        if (this->preview != nullptr)
            this->preview->reload_print();
        // We also need to reload 3D scene because of the wipe tower preview box
        if (this->config->opt_bool("wipe_tower")) {
#if !ENABLE_EXTENDED_SELECTION
            std::vector<int> selections = this->collect_selections();
            Slic3r::_3DScene::set_objects_selections(this->canvas3D, selections);
            Slic3r::_3DScene::reload_scene(this->canvas3D, 1);
#endif /* !ENABLE_EXTENDED_SELECTION */
        }
    }
    if (invalidated != Print::APPLY_STATUS_UNCHANGED && this->get_config("background_processing") == "1" &&
        this->print.num_object_instances() > 0 && this->background_process.start())
		this->statusbar()->set_cancel_callback([this]() {
            this->statusbar()->set_status_text(L("Cancelling"));
			this->background_process.stop();
        });
}

void Plater::priv::start_background_process()
{
	if (this->background_process.running())
		return;
    // return if ! @{$self->{objects}} || $self->{background_slicing_process}->running;
    // Don't start process thread if Print is not valid.
    std::string err = this->q->print().validate();
    if (! err.empty()) {
        this->statusbar()->set_status_text(err);
	} else {
		// Copy the names of active presets into the placeholder parser.
		wxGetApp().preset_bundle->export_selections(this->q->print().placeholder_parser());
		// Start the background process.
		this->background_process.start();
	}
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
/*
    my ($self) = @_;
    my ($obj_idx, $object) = $self->selected_object;
    return if !defined $obj_idx;
    my $model_object = $self->{model}->objects->[$obj_idx];
    my $model_fixed = Slic3r::Model->new;
    Slic3r::GUI::fix_model_by_win10_sdk_gui($model_object, $self->{print}, $model_fixed);

    my @new_obj_idx = $self->load_model_objects(@{$model_fixed->objects});
    return if !@new_obj_idx;
    
    foreach my $new_obj_idx (@new_obj_idx) {
        my $o = $self->{model}->objects->[$new_obj_idx];
        $o->clear_instances;
        $o->add_instance($_) for @{$model_object->instances};
        #$o->invalidate_bounding_box;
        
        if ($o->volumes_count == $model_object->volumes_count) {
            for my $i (0..($o->volumes_count-1)) {
                $o->get_volume($i)->config->apply($model_object->get_volume($i)->config);
            }
        }
        #FIXME restore volumes and their configs, layer_height_ranges, layer_height_profile, layer_height_profile_valid,
    }
    
    $self->remove($obj_idx);
*/
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

    //! Because of The MSW and GTK version of wxBitmapComboBox derived from wxComboBox, 
    //! but the OSX version derived from wxOwnerDrawnCombo.
    //! So, to get selected string we do 
    //!     combo->GetString(combo->GetSelection()) 
    //! instead of 
    //!     combo->GetStringSelection().ToStdString()); 

    std::string selected_string = combo->GetString(combo->GetSelection()).ToUTF8().data();

    if (preset_type == Preset::TYPE_FILAMENT) {
        wxGetApp().preset_bundle->set_filament_preset(idx, selected_string);
    }

    // TODO: ?
    if (preset_type == Preset::TYPE_FILAMENT && sidebar->is_multifilament()) {
        // Only update the platter UI for the 2nd and other filaments.
        wxGetApp().preset_bundle->update_platter_filament_ui(idx, combo);
    } 
    else {
        for (Tab* tab : wxGetApp().tabs_list) {
            if (tab->type() == preset_type) {
                tab->select_preset(selected_string);
                break;
            }
        }
    }

    // Synchronize config.ini with the current selections.
    wxGetApp().preset_bundle->export_selections(*wxGetApp().app_config);
    // update plater with new config
    wxGetApp().plater()->on_config_change(wxGetApp().preset_bundle->full_config());
}

void Plater::priv::on_progress_event(wxCommandEvent &evt)
{
    this->statusbar()->set_progress(evt.GetInt());
    this->statusbar()->set_status_text(evt.GetString() + wxString::FromUTF8("…"));
}

void Plater::priv::on_update_print_preview(wxCommandEvent &)
{
    if (this->preview != nullptr)
        this->preview->reload_print();
    // in case this was MM print, wipe tower bounding box on 3D tab might need redrawing with exact depth:
#if !ENABLE_EXTENDED_SELECTION
    auto selections = collect_selections();
    _3DScene::set_objects_selections(canvas3D, selections);
    if (canvas3D)
        _3DScene::reload_scene(canvas3D, true);
#endif // !ENABLE_EXTENDED_SELECTION
}

void Plater::priv::on_process_completed(wxCommandEvent &evt)
{
    // Stop the background task, wait until the thread goes into the "Idle" state.
    // At this point of time the thread should be either finished or canceled,
    // so the following call just confirms, that the produced data were consumed.
    this->background_process.stop();
    this->statusbar()->reset_cancel_callback();
    this->statusbar()->stop_busy();
  
	bool canceled = evt.GetInt() < 0;
    bool success  = evt.GetInt() > 0;
    // Reset the "export G-code path" name, so that the automatic background processing will be enabled again.
    this->background_process.reset_export();
    if (! success) {
        wxString message = evt.GetString();
        if (message.IsEmpty())
            message = _(L("Export failed"));
        this->statusbar()->set_status_text(message);
    }
	if (canceled)
		this->statusbar()->set_status_text(L("Cancelled"));

    this->sidebar->show_sliced_info_sizer(success);

    // this updates buttons status
    //$self->object_list_changed;
    
    // refresh preview
    if (this->preview != nullptr)
        this->preview->reload_print();
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

void Plater::priv::on_schedule_background_process(SimpleEvent&)
{
    schedule_background_process();
}

void Plater::priv::on_action_add(SimpleEvent&)
{
    if (q != nullptr)
        q->add();
}

void Plater::priv::on_action_split_objects(SimpleEvent&)
{
    split_object();
}

void Plater::priv::on_action_split_volumes(SimpleEvent&)
{
    split_volume();
}

void Plater::priv::on_action_cut(SimpleEvent&)
{
    // TODO
}

#if !ENABLE_EXTENDED_SELECTION
void Plater::priv::on_action_settings(SimpleEvent&)
{
    // TODO
}
#endif // !ENABLE_EXTENDED_SELECTION

void Plater::priv::on_action_layersediting(SimpleEvent&)
{
    bool enable = !_3DScene::is_layers_editing_enabled(canvas3D);
    _3DScene::enable_layers_editing(canvas3D, enable);
    if (enable && !_3DScene::is_layers_editing_enabled(canvas3D))
        _3DScene::enable_toolbar_item(canvas3D, "layersediting", false);
}

#if !ENABLE_EXTENDED_SELECTION
void Plater::priv::on_action_selectbyparts(SimpleEvent&)
{
    // TODO
}
#endif // !ENABLE_EXTENDED_SELECTION

#if ENABLE_EXTENDED_SELECTION
void Plater::priv::on_object_select(SimpleEvent& evt)
{
    selection_changed();
    wxGetApp().obj_list()->update_selections();
}
#else
void Plater::priv::on_object_select(ObjectSelectEvent &evt)
{
    const auto obj_idx = evt.object_id();
    const auto vol_idx = evt.volume_id();

    if (obj_idx >= 0 && obj_idx < 1000 && vol_idx == -1) {
        // Ignore the special objects (the wipe tower proxy and such).
        select_object(obj_idx);
}
}
#endif // ENABLE_EXTENDED_SELECTION

void Plater::priv::on_viewport_changed(SimpleEvent& evt)
{
    wxObject* o = evt.GetEventObject();
    if (o == preview->get_wxglcanvas())
        preview->set_viewport_into_scene(canvas3D);
    else if (o == canvas3D)
        preview->set_viewport_from_scene(canvas3D);
}

void Plater::priv::on_right_click(Vec2dEvent& evt)
{
#if ENABLE_EXTENDED_SELECTION
    int obj_idx = get_selected_object_idx();
    if (obj_idx == -1)
        return;

    if (q != nullptr)
        q->PopupMenu(&object_menu, (int)evt.data.x(), (int)evt.data.y());
#else
    // TODO
#endif // ENABLE_EXTENDED_SELECTION
}

void Plater::priv::on_model_update(SimpleEvent&)
{
    // TODO
}

#if !ENABLE_EXTENDED_SELECTION
void Plater::priv::on_scale_uniformly(SimpleEvent&)
{
//     my ($scale) = @_;

//     my ($obj_idx, $object) = $self->selected_object;
    const auto obj_idx = selected_object();
    if (! obj_idx) { return; }
//     return if !defined $obj_idx;

//     my $model_object = $self->{model}->objects->[$obj_idx];
//     my $model_instance = $model_object->instances->[0];

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
    
    this->schedule_background_process();
}
#endif // !ENABLE_EXTENDED_SELECTION

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

bool Plater::priv::init_object_menu()
{
    wxMenuItem* item_delete = append_menu_item(&object_menu, wxID_ANY, _(L("Delete\tDel")), _(L("Remove the selected object")),
        [this](wxCommandEvent&) { q->remove_selected(); }, "brick_delete.png");
    wxMenuItem* item_increase = append_menu_item(&object_menu, wxID_ANY, _(L("Increase copies\t+")), _(L("Place one more copy of the selected object")),
        [this](wxCommandEvent&) { q->increase_instances(); }, "add.png");
    wxMenuItem* item_decrease = append_menu_item(&object_menu, wxID_ANY, _(L("Decrease copies\t-")), _(L("Remove one copy of the selected object")),
        [this](wxCommandEvent&) { q->decrease_instances(); }, "delete.png");
    wxMenuItem* item_set_number_of_copies = append_menu_item(&object_menu, wxID_ANY, _(L("Set number of copies…")), _(L("Change the number of copies of the selected object")),
        [this](wxCommandEvent&) { q->set_number_of_copies(); }, "textfield.png");

    object_menu.AppendSeparator();
    
    wxMenu* mirror_menu = new wxMenu();
    if (mirror_menu == nullptr)
        return false;

    append_menu_item(mirror_menu, wxID_ANY, _(L("Along X axis")), _(L("Mirror the selected object along the X axis")),
        [this](wxCommandEvent&) { mirror(X); }, "bullet_red.png", &object_menu);
    append_menu_item(mirror_menu, wxID_ANY, _(L("Along Y axis")), _(L("Mirror the selected object along the Y axis")),
        [this](wxCommandEvent&) { mirror(Y); }, "bullet_green.png", &object_menu);
    append_menu_item(mirror_menu, wxID_ANY, _(L("Along Z axis")), _(L("Mirror the selected object along the Z axis")),
        [this](wxCommandEvent&) { mirror(Z); }, "bullet_blue.png", &object_menu);

    wxMenuItem* item_mirror = append_submenu(&object_menu, mirror_menu, wxID_ANY, _(L("Mirror")), _(L("Mirror the selected object")));

    wxMenu* split_menu = new wxMenu();
    if (split_menu == nullptr)
        return false;

    wxMenuItem* item_split_objects = append_menu_item(split_menu, wxID_ANY, _(L("To objects")), _(L("Split the selected object into individual objects")),
        [this](wxCommandEvent&) { split_object(); }, "shape_ungroup.png", &object_menu);
    wxMenuItem* item_split_volumes = append_menu_item(split_menu, wxID_ANY, _(L("To parts")), _(L("Split the selected object into individual sub-parts")),
        [this](wxCommandEvent&) { split_volume(); }, "shape_ungroup.png", &object_menu);

    wxMenuItem* item_split = append_submenu(&object_menu, split_menu, wxID_ANY, _(L("Split")), _(L("Split the selected object")), "shape_ungroup.png");

#if ENABLE_EXTENDED_SELECTION
    // ui updates needs to be binded to the parent panel
    if (q != nullptr)
    {
        q->Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { evt.Enable(can_mirror()); }, item_mirror->GetId());
        q->Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { evt.Enable(can_delete_object()); }, item_delete->GetId());
        q->Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { evt.Enable(can_increase_instances()); }, item_increase->GetId());
        q->Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { evt.Enable(can_decrease_instances()); }, item_decrease->GetId());
        q->Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { evt.Enable(can_increase_instances()); }, item_set_number_of_copies->GetId());
        q->Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { evt.Enable(can_split_to_objects() || can_split_to_volumes()); }, item_split->GetId());
        q->Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { evt.Enable(can_split_to_objects()); }, item_split_objects->GetId());
        q->Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt) { evt.Enable(can_split_to_volumes()); }, item_split_volumes->GetId());
    }
#endif // ENABLE_EXTENDED_SELECTION

    return true;
}

#if ENABLE_EXTENDED_SELECTION
bool Plater::priv::can_delete_object() const
{
    int obj_idx = get_selected_object_idx();
    return (0 <= obj_idx) && (obj_idx < (int)model.objects.size());
}

bool Plater::priv::can_increase_instances() const
{
    int obj_idx = get_selected_object_idx();
    return (0 <= obj_idx) && (obj_idx < (int)model.objects.size());
}

bool Plater::priv::can_decrease_instances() const
{
    int obj_idx = get_selected_object_idx();
    return (0 <= obj_idx) && (obj_idx < (int)model.objects.size()) && (model.objects[obj_idx]->instances.size() > 1);
}

bool Plater::priv::can_split_to_objects() const
{
    int obj_idx = get_selected_object_idx();
    return (0 <= obj_idx) && (obj_idx < (int)model.objects.size()) && !model.objects[obj_idx]->is_multiparts();
}

bool Plater::priv::can_split_to_volumes() const
{
    int obj_idx = get_selected_object_idx();
    return (0 <= obj_idx) && (obj_idx < (int)model.objects.size()) && !model.objects[obj_idx]->is_multiparts();
}

bool Plater::priv::can_cut_object() const
{
    int obj_idx = get_selected_object_idx();
    return (0 <= obj_idx) && (obj_idx < (int)model.objects.size());
}

bool Plater::priv::layers_height_allowed() const
{
    int obj_idx = get_selected_object_idx();
    return (0 <= obj_idx) && (obj_idx < (int)model.objects.size()) && config->opt_bool("variable_layer_height") && _3DScene::is_layers_editing_allowed(canvas3D);
}

bool Plater::priv::can_delete_all() const
{
    return !model.objects.empty();
}

bool Plater::priv::can_arrange() const
{
    return !model.objects.empty();
}

bool Plater::priv::can_mirror() const
{
    return get_selection().is_from_single_instance();
}
#endif // ENABLE_EXTENDED_SELECTION

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

void Plater::add()
{
    wxArrayString input_files;
    wxGetApp().open_model(this, input_files);

    std::vector<fs::path> input_paths;
    for (const auto &file : input_files) {
        input_paths.push_back(file.wx_str());
    }
    load_files(input_paths);
}

void Plater::load_files(const std::vector<fs::path> &input_files) { p->load_files(input_files); }

void Plater::update(bool force_autocenter) { p->update(force_autocenter); }

void Plater::select_view(const std::string& direction) { p->select_view(direction); }

void Plater::remove(size_t obj_idx) { p->remove(obj_idx); }

void Plater::remove_selected()
{
#if ENABLE_EXTENDED_SELECTION
    int obj_idx = p->get_selected_object_idx();
    if (obj_idx != -1)
        remove((size_t)obj_idx);
#else
    const auto selected = p->selected_object();
    if (selected) {
        remove(*selected);
    }
#endif // ENABLE_EXTENDED_SELECTION
}

void Plater::increase_instances(size_t num)
{
#if ENABLE_EXTENDED_SELECTION
    int obj_idx = p->get_selected_object_idx();
    if (obj_idx == -1)
        return;

    ModelObject* model_object = p->model.objects[obj_idx];
    ModelInstance* model_instance = model_object->instances.back();
#else
    const auto obj_idx = p->selected_object();
    if (! obj_idx) { return; }

    auto *model_object = p->model.objects[*obj_idx];
    auto *model_instance = model_object->instances[model_object->instances.size() - 1];
#endif // ENABLE_EXTENDED_SELECTION

    float offset = 10.0;
    for (size_t i = 0; i < num; i++, offset += 10.0) {
        Vec3d offset_vec = model_instance->get_offset() + Vec3d(offset, offset, 0.0);
        model_object->add_instance(offset_vec, model_instance->get_scaling_factor(), model_instance->get_rotation());
#if ENABLE_EXTENDED_SELECTION
//        p->print.get_object(obj_idx)->add_copy(Slic3r::to_2d(offset_vec));
#else
//        p->print.get_object(*obj_idx)->add_copy(Slic3r::to_2d(offset_vec));
#endif // ENABLE_EXTENDED_SELECTION
    }

#if ENABLE_EXTENDED_SELECTION
    sidebar().obj_list()->increase_object_instances(obj_idx, num);
#else
    sidebar().obj_list()->increase_object_instances(*obj_idx, num);
#endif // ENABLE_EXTENDED_SELECTION

    if (p->get_config("autocenter") == "1") {
        p->arrange();
    } else {
        p->update();
    }

#if ENABLE_EXTENDED_SELECTION
    p->get_selection().add_instance(obj_idx, (int)model_object->instances.size() - 1);
#endif // ENABLE_EXTENDED_SELECTION

    p->selection_changed();

    this->p->schedule_background_process();
}

void Plater::decrease_instances(size_t num)
{
#if ENABLE_EXTENDED_SELECTION
    int obj_idx = p->get_selected_object_idx();
    if (obj_idx == -1)
        return;

    ModelObject* model_object = p->model.objects[obj_idx];
#else
    const auto obj_idx = p->selected_object();
    if (! obj_idx) { return; }

    auto *model_object = p->model.objects[*obj_idx];
#endif // ENABLE_EXTENDED_SELECTION
    if (model_object->instances.size() > num) {
        for (size_t i = 0; i < num; i++) {
            model_object->delete_last_instance();
#if ENABLE_EXTENDED_SELECTION
//            p->print.get_object(obj_idx)->delete_last_copy();
#else
//            p->print.get_object(*obj_idx)->delete_last_copy();
#endif // ENABLE_EXTENDED_SELECTION
        }
#if ENABLE_EXTENDED_SELECTION
        sidebar().obj_list()->decrease_object_instances(obj_idx, num);
#else
        sidebar().obj_list()->decrease_object_instances(*obj_idx, num);
#endif // ENABLE_EXTENDED_SELECTION
    }
    else {
#if ENABLE_EXTENDED_SELECTION
        remove(obj_idx);
#else
        remove(*obj_idx);
#endif // ENABLE_EXTENDED_SELECTION
    }

    p->update();

#if ENABLE_EXTENDED_SELECTION
    if (!model_object->instances.empty())
        p->get_selection().add_instance(obj_idx, (int)model_object->instances.size() - 1);
#endif // ENABLE_EXTENDED_SELECTION

    p->selection_changed();
    this->p->schedule_background_process();
}

void Plater::set_number_of_copies(/*size_t num*/)
{
#if ENABLE_EXTENDED_SELECTION
    int obj_idx = p->get_selected_object_idx();
    if (obj_idx == -1)
        return;

    ModelObject* model_object = p->model.objects[obj_idx];
#else
    const auto obj_idx = p->selected_object();
    if (! obj_idx) { return; }

    auto *model_object = p->model.objects[*obj_idx];
#endif // ENABLE_EXTENDED_SELECTION

    const auto num = wxGetNumberFromUser( " ", _("Enter the number of copies:"), 
                                    _("Copies of the selected object"), model_object->instances.size(), 0, 1000, this );
    if (num < 0)
        return;

    int diff = (int)num - (int)model_object->instances.size();
    if (diff > 0)
        increase_instances(diff);
    else if (diff < 0)
        decrease_instances(-diff);
}

void Plater::export_gcode(fs::path output_path)
{
#if ENABLE_EXTENDED_SELECTION
    if (p->model.objects.empty())
        return;
#else
    if (p->objects.empty())
        return;
#endif // ENABLE_EXTENDED_SELECTION

    if (this->p->background_process.is_export_scheduled()) {
        GUI::show_error(this, _(L("Another export job is currently running.")));
        return;
    }

    std::string err = wxGetApp().preset_bundle->full_config().validate();
    if (err.empty())
        err = p->print.validate();
    if (! err.empty()) {
        // The config is not valid
        GUI::show_error(this, _(err));
        return;
    }

    // Copy the names of active presets into the placeholder parser.
    wxGetApp().preset_bundle->export_selections(p->print.placeholder_parser());

    // select output file
    if (output_path.empty()) {
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

        if (dlg.ShowModal() == wxID_OK) {
            fs::path path(dlg.GetPath());
            wxGetApp().app_config->update_last_output_dir(path.parent_path().string());
            output_path = path;
        }
    }

    if (! output_path.empty()) {
        this->p->background_process.schedule_export(p->print.output_filepath(output_path.string()));
        this->p->background_process.start();
    }
}

void Plater::export_stl()
{
#if ENABLE_EXTENDED_SELECTION
    if (p->model.objects.empty()) { return; }
#else
    if (p->objects.empty()) { return; }
#endif // ENABLE_EXTENDED_SELECTION

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
#if ENABLE_EXTENDED_SELECTION
    if (p->model.objects.empty()) { return; }
#else
    if (p->objects.empty()) { return; }
#endif // ENABLE_EXTENDED_SELECTION

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
#if ENABLE_EXTENDED_SELECTION
    if (p->model.objects.empty()) { return; }
#else
    if (p->objects.empty()) { return; }
#endif // ENABLE_EXTENDED_SELECTION

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
    // explicitly cancel a previous thread and start a new one.
    // Don't reslice if export of G-code or sending to OctoPrint is running.
//    if (! defined($self->{export_gcode_output_file}) && ! defined($self->{send_gcode_file})) {
    // Stop the background processing threads, stop the async update timer.
//    this->p->stop_background_process();
    // Rather perform one additional unnecessary update of the print object instead of skipping a pending async update.
    this->p->async_apply_config();
	this->p->statusbar()->set_cancel_callback([this]() {
		this->p->statusbar()->set_status_text(L("Cancelling"));
		this->p->background_process.stop();
    });
    this->p->start_background_process();
}

void Plater::send_gcode()
{
//    p->send_gcode_file = export_gcode();
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

void Plater::on_config_change(const DynamicPrintConfig &config)
{
    bool update_scheduled = false;
    for (auto opt_key : p->config->diff(config)) {
        p->config->set_key_value(opt_key, config.option(opt_key)->clone());
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
//             sidebar()->p->btn_print->Show(config.get("serial_port"));  // ???: btn_print is removed
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

    if (p->main_frame->is_loaded())
        this->p->schedule_background_process();
}

wxGLCanvas* Plater::canvas3D()
{
    return p->canvas3D;
}

void Plater::changed_object(int obj_idx)
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
        model_object->ensure_on_bed();
        _3DScene::reload_scene(p->canvas3D, false);
    }

    // update print
    if (list->is_parts_changed() || list->is_part_settings_changed()) {
        this->p->schedule_background_process();
#if !ENABLE_EXTENDED_SELECTION
        if (p->canvas3D) _3DScene::reload_scene(p->canvas3D, true);
        auto selections = p->collect_selections();
        _3DScene::set_objects_selections(p->canvas3D, selections);
#endif // !ENABLE_EXTENDED_SELECTION
#if !ENABLE_MODIFIED_CAMERA_TARGET
        _3DScene::zoom_to_volumes(p->canvas3D);
#endif // !ENABLE_MODIFIED_CAMERA_TARGET
    }
    else {
        this->p->schedule_background_process();
    }

}


}}    // namespace Slic3r::GUI
