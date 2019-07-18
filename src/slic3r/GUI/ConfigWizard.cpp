#include "ConfigWizard_private.hpp"

#include <algorithm>
#include <numeric>
#include <utility>
#include <unordered_map>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <wx/settings.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/dcclient.h>
#include <wx/statbmp.h>
#include <wx/checkbox.h>
#include <wx/statline.h>
#include <wx/dataview.h>
#include <wx/notebook.h>
#include <wx/display.h>
#include <wx/filefn.h>
#include <wx/debug.h>

#include "libslic3r/Utils.hpp"
#include "PresetBundle.hpp"
#include "GUI.hpp"
#include "GUI_Utils.hpp"
#include "slic3r/Utils/PresetUpdater.hpp"


namespace Slic3r {
namespace GUI {


// Printer model picker GUI control

struct PrinterPickerEvent : public wxEvent
{
    std::string vendor_id;
    std::string model_id;
    std::string variant_name;
    bool enable;

    PrinterPickerEvent(wxEventType eventType, int winid, std::string vendor_id, std::string model_id, std::string variant_name, bool enable)
        : wxEvent(winid, eventType)
        , vendor_id(std::move(vendor_id))
        , model_id(std::move(model_id))
        , variant_name(std::move(variant_name))
        , enable(enable)
    {}

    virtual wxEvent *Clone() const
    {
        return new PrinterPickerEvent(*this);
    }
};

wxDEFINE_EVENT(EVT_PRINTER_PICK, PrinterPickerEvent);

PrinterPicker::PrinterPicker(wxWindow *parent, const VendorProfile &vendor, wxString title, size_t max_cols, const AppConfig &appconfig_vendors, const ModelFilter &filter)
    : wxPanel(parent)
    , vendor_id(vendor.id)
    , width(0)
{
    const auto &models = vendor.models;

    auto *sizer = new wxBoxSizer(wxVERTICAL);

    const auto font_title = GetFont().MakeBold().Scaled(1.3f);
    const auto font_name = GetFont().MakeBold();
    const auto font_alt_nozzle = GetFont().Scaled(0.9f);

    // wxGrid appends widgets by rows, but we need to construct them in columns.
    // These vectors are used to hold the elements so that they can be appended in the right order.
    std::vector<wxStaticText*> titles;
    std::vector<wxStaticBitmap*> bitmaps;
    std::vector<wxPanel*> variants_panels;

    int max_row_width = 0;
    int current_row_width = 0;

    for (const auto &model : models) {
        if (! filter(model)) { continue; }

        wxBitmap bitmap;
        int bitmap_width = 0;
        const wxString bitmap_file = GUI::from_u8(Slic3r::var((boost::format("printers/%1%_%2%.png") % vendor.id % model.id).str()));
        if (wxFileExists(bitmap_file)) {
            bitmap.LoadFile(bitmap_file, wxBITMAP_TYPE_PNG);
            bitmap_width = bitmap.GetWidth();
        }

        auto *title = new wxStaticText(this, wxID_ANY, model.name, wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
        title->SetFont(font_name);
        const int wrap_width = std::max((int)MODEL_MIN_WRAP, bitmap_width);
        title->Wrap(wrap_width);

        current_row_width += wrap_width;
        if (titles.size() % max_cols == max_cols - 1) {
            max_row_width = std::max(max_row_width, current_row_width);
            current_row_width = 0;
        }

        titles.push_back(title);

        auto *bitmap_widget = new wxStaticBitmap(this, wxID_ANY, bitmap);
        bitmaps.push_back(bitmap_widget);

        auto *variants_panel = new wxPanel(this);
        auto *variants_sizer = new wxBoxSizer(wxVERTICAL);
        variants_panel->SetSizer(variants_sizer);
        const auto model_id = model.id;

        for (size_t i = 0; i < model.variants.size(); i++) {
            const auto &variant = model.variants[i];

            const auto label = model.technology == ptFFF
                ? wxString::Format("%s %s %s", variant.name, _(L("mm")), _(L("nozzle")))
                : from_u8(model.name);

            if (i == 1) {
                auto *alt_label = new wxStaticText(variants_panel, wxID_ANY, _(L("Alternate nozzles:")));
                alt_label->SetFont(font_alt_nozzle);
                variants_sizer->Add(alt_label, 0, wxBOTTOM, 3);
            }

            auto *cbox = new Checkbox(variants_panel, label, model_id, variant.name);
            i == 0 ? cboxes.push_back(cbox) : cboxes_alt.push_back(cbox);

            bool enabled = appconfig_vendors.get_variant("PrusaResearch", model_id, variant.name);
            cbox->SetValue(enabled);

            variants_sizer->Add(cbox, 0, wxBOTTOM, 3);

            cbox->Bind(wxEVT_CHECKBOX, [this, cbox](wxCommandEvent &event) {
                on_checkbox(cbox, event.IsChecked());
            });
        }

        variants_panels.push_back(variants_panel);
    }

    width = std::max(max_row_width, current_row_width);

    const size_t cols = std::min(max_cols, titles.size());

    auto *printer_grid = new wxFlexGridSizer(cols, 0, 20);
    printer_grid->SetFlexibleDirection(wxVERTICAL | wxHORIZONTAL);

    if (titles.size() > 0) {
        const size_t odd_items = titles.size() % cols;

        for (size_t i = 0; i < titles.size() - odd_items; i += cols) {
            for (size_t j = i; j < i + cols; j++) { printer_grid->Add(titles[j], 0, wxBOTTOM, 3); }
            for (size_t j = i; j < i + cols; j++) { printer_grid->Add(bitmaps[j], 0, wxBOTTOM, 20); }
            for (size_t j = i; j < i + cols; j++) { printer_grid->Add(variants_panels[j]); }

            // Add separator space
            if (i > 0) {
                for (size_t j = i; j < i + cols; j++) { printer_grid->Add(1, 100); }
            }
        }

        if (odd_items > 0) {
            for (size_t i = 0; i < cols; i++) { printer_grid->Add(1, 100); }

            const size_t rem = titles.size() - odd_items;

            for (size_t i = rem; i < titles.size(); i++) { printer_grid->Add(titles[i], 0, wxBOTTOM, 3); }
            for (size_t i = 0; i < cols - odd_items; i++) { printer_grid->AddSpacer(1); }
            for (size_t i = rem; i < titles.size(); i++) { printer_grid->Add(bitmaps[i], 0, wxBOTTOM, 20); }
            for (size_t i = 0; i < cols - odd_items; i++) { printer_grid->AddSpacer(1); }
            for (size_t i = rem; i < titles.size(); i++) { printer_grid->Add(variants_panels[i]); }
        }
    }

    auto *title_sizer = new wxBoxSizer(wxHORIZONTAL);
    if (! title.IsEmpty()) {
        auto *title_widget = new wxStaticText(this, wxID_ANY, title);
        title_widget->SetFont(font_title);
        title_sizer->Add(title_widget);
    }
    title_sizer->AddStretchSpacer();

    if (titles.size() > 1) {
        // It only makes sense to add the All / None buttons if there's multiple printers

        auto *sel_all_std = new wxButton(this, wxID_ANY, _(L("All standard")));
        auto *sel_all = new wxButton(this, wxID_ANY, _(L("All")));
        auto *sel_none = new wxButton(this, wxID_ANY, _(L("None")));
        sel_all_std->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &event) { this->select_all(true, false); });
        sel_all->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &event) { this->select_all(true, true); });
        sel_none->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &event) { this->select_all(false); });
        title_sizer->Add(sel_all_std, 0, wxRIGHT, BTN_SPACING);
        title_sizer->Add(sel_all, 0, wxRIGHT, BTN_SPACING);
        title_sizer->Add(sel_none);

        // fill button indexes used later for buttons rescaling
        m_button_indexes = { sel_all_std->GetId(), sel_all->GetId(), sel_none->GetId() };
    }

    sizer->Add(title_sizer, 0, wxEXPAND | wxBOTTOM, BTN_SPACING);
    sizer->Add(printer_grid);

    SetSizer(sizer);
}

PrinterPicker::PrinterPicker(wxWindow *parent, const VendorProfile &vendor, wxString title, size_t max_cols, const AppConfig &appconfig_vendors)
    : PrinterPicker(parent, vendor, std::move(title), max_cols, appconfig_vendors, [](const VendorProfile::PrinterModel&) { return true; })
{}

void PrinterPicker::select_all(bool select, bool alternates)
{
    for (const auto &cb : cboxes) {
        if (cb->GetValue() != select) {
            cb->SetValue(select);
            on_checkbox(cb, select);
        }
    }

    if (! select) { alternates = false; }

    for (const auto &cb : cboxes_alt) {
        if (cb->GetValue() != alternates) {
            cb->SetValue(alternates);
            on_checkbox(cb, alternates);
        }
    }
}

void PrinterPicker::select_one(size_t i, bool select)
{
    if (i < cboxes.size() && cboxes[i]->GetValue() != select) {
        cboxes[i]->SetValue(select);
        on_checkbox(cboxes[i], select);
    }
}

void PrinterPicker::on_checkbox(const Checkbox *cbox, bool checked)
{
    PrinterPickerEvent evt(EVT_PRINTER_PICK, GetId(), vendor_id, cbox->model, cbox->variant, checked);
    AddPendingEvent(evt);
}


// Wizard page base

ConfigWizardPage::ConfigWizardPage(ConfigWizard *parent, wxString title, wxString shortname, unsigned indent)
    : wxPanel(parent->p->hscroll)
    , parent(parent)
    , shortname(std::move(shortname))
    , indent(indent)
{
    auto *sizer = new wxBoxSizer(wxVERTICAL);

    auto *text = new wxStaticText(this, wxID_ANY, std::move(title), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    const auto font = GetFont().MakeBold().Scaled(1.5);
    text->SetFont(font);
    sizer->Add(text, 0, wxALIGN_LEFT, 0);
    sizer->AddSpacer(10);

    content = new wxBoxSizer(wxVERTICAL);
    sizer->Add(content, 1);

    SetSizer(sizer);

    this->Hide();

    Bind(wxEVT_SIZE, [this](wxSizeEvent &event) {
        this->Layout();
        event.Skip();
    });
}

ConfigWizardPage::~ConfigWizardPage() {}

void ConfigWizardPage::append_text(wxString text)
{
    auto *widget = new wxStaticText(this, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    widget->Wrap(WRAP_WIDTH);
    widget->SetMinSize(wxSize(WRAP_WIDTH, -1));
    append(widget);
}

void ConfigWizardPage::append_spacer(int space)
{
    content->AddSpacer(space);
}


// Wizard pages

PageWelcome::PageWelcome(ConfigWizard *parent)
    : ConfigWizardPage(parent, wxString::Format(
#ifdef __APPLE__
            _(L("Welcome to the %s Configuration Assistant"))
#else
            _(L("Welcome to the %s Configuration Wizard"))
#endif
            , SLIC3R_APP_NAME), _(L("Welcome")))
    , cbox_reset(nullptr)
{
    if (wizard_p()->run_reason == ConfigWizard::RR_DATA_EMPTY) {
        wxString::Format(_(L("Run %s")), ConfigWizard::name());
        append_text(wxString::Format(
            _(L("Hello, welcome to %s! This %s helps you with the initial configuration; just a few settings and you will be ready to print.")),
            SLIC3R_APP_NAME,
            ConfigWizard::name())
        );
    } else {
        cbox_reset = new wxCheckBox(this, wxID_ANY, _(L("Remove user profiles - install from scratch (a snapshot will be taken beforehand)")));
        append(cbox_reset);
    }

    Show();
}


PagePrinters::PagePrinters(ConfigWizard *parent, wxString title, wxString shortname, const VendorProfile &vendor, unsigned indent, Technology technology)
    : ConfigWizardPage(parent, std::move(title), std::move(shortname), indent)
{
    enum {
        COL_SIZE = 200,
    };

    bool check_first_variant = technology == T_FFF && wizard_p()->check_first_variant();

    AppConfig &appconfig_vendors = this->wizard_p()->appconfig_vendors;

    const auto families = vendor.families();
    for (const auto &family : families) {
        const auto filter = [&](const VendorProfile::PrinterModel &model) {
            return ((model.technology == ptFFF && technology & T_FFF)
                    || (model.technology == ptSLA && technology & T_SLA))
                && model.family == family;
        };

        if (std::find_if(vendor.models.begin(), vendor.models.end(), filter) == vendor.models.end()) {
            continue;
        }

        const auto picker_title = family.empty() ? wxString() : wxString::Format(_(L("%s Family")), family);
        auto *picker = new PrinterPicker(this, vendor, picker_title, MAX_COLS, appconfig_vendors, filter);

        if (check_first_variant) {
            // Select the default (first) model/variant on the Prusa vendor
            picker->select_one(0, true);
            check_first_variant = false;
        }

        picker->Bind(EVT_PRINTER_PICK, [this, &appconfig_vendors](const PrinterPickerEvent &evt) {
            appconfig_vendors.set_variant(evt.vendor_id, evt.model_id, evt.variant_name, evt.enable);
        });

        append(new wxStaticLine(this));

        append(picker);
        printer_pickers.push_back(picker);
    }
}

void PagePrinters::select_all(bool select, bool alternates)
{
    for (auto picker : printer_pickers) {
        picker->select_all(select, alternates);
    }
}

int PagePrinters::get_width() const
{
    return std::accumulate(printer_pickers.begin(), printer_pickers.end(), 0,
        [](int acc, const PrinterPicker *picker) { return std::max(acc, picker->get_width()); });
}


const char *PageCustom::default_profile_name = "My Settings";

PageCustom::PageCustom(ConfigWizard *parent)
    : ConfigWizardPage(parent, _(L("Custom Printer Setup")), _(L("Custom Printer")))
{
    cb_custom = new wxCheckBox(this, wxID_ANY, _(L("Define a custom printer profile")));
    tc_profile_name = new wxTextCtrl(this, wxID_ANY, default_profile_name);
    auto *label = new wxStaticText(this, wxID_ANY, _(L("Custom profile name:")));

    tc_profile_name->Enable(false);
    tc_profile_name->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent &evt) {
        if (tc_profile_name->GetValue().IsEmpty()) {
            if (profile_name_prev.IsEmpty()) { tc_profile_name->SetValue(default_profile_name); }
            else { tc_profile_name->SetValue(profile_name_prev); }
        } else {
            profile_name_prev = tc_profile_name->GetValue();
        }
        evt.Skip();
    });

    cb_custom->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &event) {
        tc_profile_name->Enable(custom_wanted());
        wizard_p()->on_custom_setup(custom_wanted());
    });

    append(cb_custom);
    append(label);
    append(tc_profile_name);
}

PageUpdate::PageUpdate(ConfigWizard *parent)
    : ConfigWizardPage(parent, _(L("Automatic updates")), _(L("Updates")))
    , version_check(true)
    , preset_update(true)
{
    const AppConfig *app_config = GUI::get_app_config();
    auto boldfont = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    boldfont.SetWeight(wxFONTWEIGHT_BOLD);

    auto *box_slic3r = new wxCheckBox(this, wxID_ANY, _(L("Check for application updates")));
    box_slic3r->SetValue(app_config->get("version_check") == "1");
    append(box_slic3r);
    append_text(wxString::Format(_(L(
        "If enabled, %s checks for new application versions online. When a new version becomes available, "
         "a notification is displayed at the next application startup (never during program usage). "
         "This is only a notification mechanisms, no automatic installation is done.")), SLIC3R_APP_NAME));

    append_spacer(VERTICAL_SPACING);

    auto *box_presets = new wxCheckBox(this, wxID_ANY, _(L("Update built-in Presets automatically")));
    box_presets->SetValue(app_config->get("preset_update") == "1");
    append(box_presets);
    append_text(wxString::Format(_(L(
        "If enabled, %s downloads updates of built-in system presets in the background."
        "These updates are downloaded into a separate temporary location."
        "When a new preset version becomes available it is offered at application startup.")), SLIC3R_APP_NAME));
    const auto text_bold = _(L("Updates are never applied without user's consent and never overwrite user's customized settings."));
    auto *label_bold = new wxStaticText(this, wxID_ANY, text_bold);
    label_bold->SetFont(boldfont);
    label_bold->Wrap(WRAP_WIDTH);
    append(label_bold);
    append_text(_(L("Additionally a backup snapshot of the whole configuration is created before an update is applied.")));

    box_slic3r->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &event) { this->version_check = event.IsChecked(); });
    box_presets->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &event) { this->preset_update = event.IsChecked(); });
}

PageVendors::PageVendors(ConfigWizard *parent)
    : ConfigWizardPage(parent, _(L("Other Vendors")), _(L("Other Vendors")))
{
    append_text(wxString::Format(_(L("Pick another vendor supported by %s:")), SLIC3R_APP_NAME));

    auto boldfont = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    boldfont.SetWeight(wxFONTWEIGHT_BOLD);

    AppConfig &appconfig_vendors = this->wizard_p()->appconfig_vendors;
    wxArrayString choices_vendors;

    for (const auto vendor_pair : wizard_p()->vendors) {
        const auto &vendor = vendor_pair.second;
        if (vendor.id == "PrusaResearch") { continue; }

        auto *picker = new PrinterPicker(this, vendor, "", MAX_COLS, appconfig_vendors);
        picker->Hide();
        pickers.push_back(picker);
        choices_vendors.Add(vendor.name);

        picker->Bind(EVT_PRINTER_PICK, [this, &appconfig_vendors](const PrinterPickerEvent &evt) {
            appconfig_vendors.set_variant(evt.vendor_id, evt.model_id, evt.variant_name, evt.enable);
        });
    }

    auto *vendor_picker = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, choices_vendors);
    if (choices_vendors.GetCount() > 0) {
        vendor_picker->SetSelection(0);
        on_vendor_pick(0);
    }

    vendor_picker->Bind(wxEVT_CHOICE, [this](wxCommandEvent &evt) {
        this->on_vendor_pick(evt.GetInt());
    });

    append(vendor_picker);
    for (PrinterPicker *picker : pickers) { this->append(picker); }
}

void PageVendors::on_vendor_pick(size_t i)
{
    for (PrinterPicker *picker : pickers) { picker->Hide(); }
    if (i < pickers.size()) {
        pickers[i]->Show();
        parent->Layout();
    }
}

PageFirmware::PageFirmware(ConfigWizard *parent)
    : ConfigWizardPage(parent, _(L("Firmware Type")), _(L("Firmware")), 1)
    , gcode_opt(*print_config_def.get("gcode_flavor"))
    , gcode_picker(nullptr)
{
    append_text(_(L("Choose the type of firmware used by your printer.")));
    append_text(_(gcode_opt.tooltip));

    wxArrayString choices;
    choices.Alloc(gcode_opt.enum_labels.size());
    for (const auto &label : gcode_opt.enum_labels) {
        choices.Add(label);
    }

    gcode_picker = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, choices);
    const auto &enum_values = gcode_opt.enum_values;
    auto needle = enum_values.cend();
    if (gcode_opt.default_value) {
        needle = std::find(enum_values.cbegin(), enum_values.cend(), gcode_opt.default_value->serialize());
    }
    if (needle != enum_values.cend()) {
        gcode_picker->SetSelection(needle - enum_values.cbegin());
    } else {
        gcode_picker->SetSelection(0);
    }

    append(gcode_picker);
}

void PageFirmware::apply_custom_config(DynamicPrintConfig &config)
{
    auto sel = gcode_picker->GetSelection();
    if (sel >= 0 && (size_t)sel < gcode_opt.enum_labels.size()) {
        auto *opt = new ConfigOptionEnum<GCodeFlavor>(static_cast<GCodeFlavor>(sel));
        config.set_key_value("gcode_flavor", opt);
    }
}

PageBedShape::PageBedShape(ConfigWizard *parent)
    : ConfigWizardPage(parent, _(L("Bed Shape and Size")), _(L("Bed Shape")), 1)
    , shape_panel(new BedShapePanel(this))
{
    append_text(_(L("Set the shape of your printer's bed.")));

    shape_panel->build_panel(*wizard_p()->custom_config->option<ConfigOptionPoints>("bed_shape"),
        *wizard_p()->custom_config->option<ConfigOptionString>("bed_custom_texture"));

    append(shape_panel);
}

void PageBedShape::apply_custom_config(DynamicPrintConfig &config)
{
    const std::vector<Vec2d>& points = shape_panel->get_shape();
    const std::string& custom_texture = shape_panel->get_custom_texture();
    config.set_key_value("bed_shape", new ConfigOptionPoints(points));
    config.set_key_value("bed_custom_texture", new ConfigOptionString(custom_texture));
}

PageDiameters::PageDiameters(ConfigWizard *parent)
    : ConfigWizardPage(parent, _(L("Filament and Nozzle Diameters")), _(L("Print Diameters")), 1)
    , spin_nozzle(new wxSpinCtrlDouble(this, wxID_ANY))
    , spin_filam(new wxSpinCtrlDouble(this, wxID_ANY))
{
    spin_nozzle->SetDigits(2);
    spin_nozzle->SetIncrement(0.1);
    auto *default_nozzle = print_config_def.get("nozzle_diameter")->get_default_value<ConfigOptionFloats>();
    spin_nozzle->SetValue(default_nozzle != nullptr && default_nozzle->size() > 0 ? default_nozzle->get_at(0) : 0.5);

    spin_filam->SetDigits(2);
    spin_filam->SetIncrement(0.25);
    auto *default_filam = print_config_def.get("filament_diameter")->get_default_value<ConfigOptionFloats>();
    spin_filam->SetValue(default_filam != nullptr && default_filam->size() > 0 ? default_filam->get_at(0) : 3.0);

    append_text(_(L("Enter the diameter of your printer's hot end nozzle.")));

    auto *sizer_nozzle = new wxFlexGridSizer(3, 5, 5);
    auto *text_nozzle = new wxStaticText(this, wxID_ANY, _(L("Nozzle Diameter:")));
    auto *unit_nozzle = new wxStaticText(this, wxID_ANY, _(L("mm")));
    sizer_nozzle->AddGrowableCol(0, 1);
    sizer_nozzle->Add(text_nozzle, 0, wxALIGN_CENTRE_VERTICAL);
    sizer_nozzle->Add(spin_nozzle);
    sizer_nozzle->Add(unit_nozzle, 0, wxALIGN_CENTRE_VERTICAL);
    append(sizer_nozzle);

    append_spacer(VERTICAL_SPACING);

    append_text(_(L("Enter the diameter of your filament.")));
    append_text(_(L("Good precision is required, so use a caliper and do multiple measurements along the filament, then compute the average.")));

    auto *sizer_filam = new wxFlexGridSizer(3, 5, 5);
    auto *text_filam = new wxStaticText(this, wxID_ANY, _(L("Filament Diameter:")));
    auto *unit_filam = new wxStaticText(this, wxID_ANY, _(L("mm")));
    sizer_filam->AddGrowableCol(0, 1);
    sizer_filam->Add(text_filam, 0, wxALIGN_CENTRE_VERTICAL);
    sizer_filam->Add(spin_filam);
    sizer_filam->Add(unit_filam, 0, wxALIGN_CENTRE_VERTICAL);
    append(sizer_filam);
}

void PageDiameters::apply_custom_config(DynamicPrintConfig &config)
{
    auto *opt_nozzle = new ConfigOptionFloats(1, spin_nozzle->GetValue());
    config.set_key_value("nozzle_diameter", opt_nozzle);
    auto *opt_filam = new ConfigOptionFloats(1, spin_filam->GetValue());
    config.set_key_value("filament_diameter", opt_filam);

    auto set_extrusion_width = [&config, opt_nozzle](const char *key, double dmr) {
        char buf[64];
        sprintf(buf, "%.2lf", dmr * opt_nozzle->values.front() / 0.4);
		config.set_key_value(key, new ConfigOptionFloatOrPercent(atof(buf), false));
	};

    set_extrusion_width("support_material_extrusion_width",   0.35);
	set_extrusion_width("top_infill_extrusion_width",		  0.40);
	set_extrusion_width("first_layer_extrusion_width",		  0.42);

	set_extrusion_width("extrusion_width",					  0.45);
	set_extrusion_width("perimeter_extrusion_width",		  0.45);
	set_extrusion_width("external_perimeter_extrusion_width", 0.45);
	set_extrusion_width("infill_extrusion_width",			  0.45);
	set_extrusion_width("solid_infill_extrusion_width",       0.45);
}

PageTemperatures::PageTemperatures(ConfigWizard *parent)
    : ConfigWizardPage(parent, _(L("Extruder and Bed Temperatures")), _(L("Temperatures")), 1)
    , spin_extr(new wxSpinCtrlDouble(this, wxID_ANY))
    , spin_bed(new wxSpinCtrlDouble(this, wxID_ANY))
{
    spin_extr->SetIncrement(5.0);
    const auto &def_extr = *print_config_def.get("temperature");
    spin_extr->SetRange(def_extr.min, def_extr.max);
    auto *default_extr = def_extr.get_default_value<ConfigOptionInts>();
    spin_extr->SetValue(default_extr != nullptr && default_extr->size() > 0 ? default_extr->get_at(0) : 200);

    spin_bed->SetIncrement(5.0);
    const auto &def_bed = *print_config_def.get("bed_temperature");
    spin_bed->SetRange(def_bed.min, def_bed.max);
    auto *default_bed = def_bed.get_default_value<ConfigOptionInts>();
    spin_bed->SetValue(default_bed != nullptr && default_bed->size() > 0 ? default_bed->get_at(0) : 0);

    append_text(_(L("Enter the temperature needed for extruding your filament.")));
    append_text(_(L("A rule of thumb is 160 to 230 °C for PLA, and 215 to 250 °C for ABS.")));

    auto *sizer_extr = new wxFlexGridSizer(3, 5, 5);
    auto *text_extr = new wxStaticText(this, wxID_ANY, _(L("Extrusion Temperature:")));
    auto *unit_extr = new wxStaticText(this, wxID_ANY, _(L("°C")));
    sizer_extr->AddGrowableCol(0, 1);
    sizer_extr->Add(text_extr, 0, wxALIGN_CENTRE_VERTICAL);
    sizer_extr->Add(spin_extr);
    sizer_extr->Add(unit_extr, 0, wxALIGN_CENTRE_VERTICAL);
    append(sizer_extr);

    append_spacer(VERTICAL_SPACING);

    append_text(_(L("Enter the bed temperature needed for getting your filament to stick to your heated bed.")));
    append_text(_(L("A rule of thumb is 60 °C for PLA and 110 °C for ABS. Leave zero if you have no heated bed.")));

    auto *sizer_bed = new wxFlexGridSizer(3, 5, 5);
    auto *text_bed = new wxStaticText(this, wxID_ANY, _(L("Bed Temperature:")));
    auto *unit_bed = new wxStaticText(this, wxID_ANY, _(L("°C")));
    sizer_bed->AddGrowableCol(0, 1);
    sizer_bed->Add(text_bed, 0, wxALIGN_CENTRE_VERTICAL);
    sizer_bed->Add(spin_bed);
    sizer_bed->Add(unit_bed, 0, wxALIGN_CENTRE_VERTICAL);
    append(sizer_bed);
}

void PageTemperatures::apply_custom_config(DynamicPrintConfig &config)
{
    auto *opt_extr = new ConfigOptionInts(1, spin_extr->GetValue());
    config.set_key_value("temperature", opt_extr);
    auto *opt_extr1st = new ConfigOptionInts(1, spin_extr->GetValue());
    config.set_key_value("first_layer_temperature", opt_extr1st);
    auto *opt_bed = new ConfigOptionInts(1, spin_bed->GetValue());
    config.set_key_value("bed_temperature", opt_bed);
    auto *opt_bed1st = new ConfigOptionInts(1, spin_bed->GetValue());
    config.set_key_value("first_layer_bed_temperature", opt_bed1st);
}


// Index

ConfigWizardIndex::ConfigWizardIndex(wxWindow *parent)
    : wxPanel(parent)
    , bg(ScalableBitmap(parent, "PrusaSlicer_192px_transparent.png", 192))
    , bullet_black(ScalableBitmap(parent, "bullet_black.png"))
    , bullet_blue(ScalableBitmap(parent, "bullet_blue.png"))
    , bullet_white(ScalableBitmap(parent, "bullet_white.png"))
    , item_active(0)
    , item_hover(-1)
    , last_page((size_t)-1)
{
    SetMinSize(bg.bmp().GetSize());

    const wxSize size = GetTextExtent("m");
    em_w = size.x;
    em_h = size.y;

    // Add logo bitmap.
    // This could be done in on_paint() along with the index labels, but I've found it tricky
    // to get the bitmap rendered well on all platforms with transparent background.
    // In some cases it didn't work at all. And so wxStaticBitmap is used here instead,
    // because it has all the platform quirks figured out.
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    logo = new wxStaticBitmap(this, wxID_ANY, bg.bmp());
    sizer->AddStretchSpacer();
    sizer->Add(logo);
    SetSizer(sizer);

    Bind(wxEVT_PAINT, &ConfigWizardIndex::on_paint, this);
    Bind(wxEVT_MOTION, &ConfigWizardIndex::on_mouse_move, this);

    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &evt) {
        if (item_hover != -1) {
            item_hover = -1;
            Refresh();
        }
        evt.Skip();
    });

    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &evt) {
        if (item_hover >= 0) { go_to(item_hover); }
    });
}

wxDECLARE_EVENT(EVT_INDEX_PAGE, wxCommandEvent);

void ConfigWizardIndex::add_page(ConfigWizardPage *page)
{
    last_page = items.size();
    items.emplace_back(Item { page->shortname, page->indent, page });
    Refresh();
}

void ConfigWizardIndex::add_label(wxString label, unsigned indent)
{
    items.emplace_back(Item { std::move(label), indent, nullptr });
    Refresh();
}

ConfigWizardPage* ConfigWizardIndex::active_page() const
{
    if (item_active >= items.size()) { return nullptr; }

    return items[item_active].page;
}

void ConfigWizardIndex::go_prev()
{
    // Search for a preceiding item that is a page (not a label, ie. page != nullptr)

    for (size_t i = item_active; i > 0; i--) {
        if (items[i - 1].page != nullptr) {
            go_to(i - 1);
            return;
        }
    }
}

void ConfigWizardIndex::go_next()
{
    // Search for a next item that is a page (not a label, ie. page != nullptr)

    for (size_t i = item_active + 1; i < items.size(); i++) {
        if (items[i].page != nullptr) {
            go_to(i);
            return;
        }
    }
}

void ConfigWizardIndex::go_to(size_t i)
{
    if (i < items.size() && items[i].page != nullptr) {
        auto *former_active = active_page();
        if (former_active != nullptr) { former_active->Hide(); }

        item_active = i;
        items[i].page->Show();

        wxCommandEvent evt(EVT_INDEX_PAGE, GetId());
        AddPendingEvent(evt);

        Refresh();
    }
}

void ConfigWizardIndex::go_to(ConfigWizardPage *page)
{
    if (page == nullptr) { return; }

    for (size_t i = 0; i < items.size(); i++) {
        if (items[i].page == page) { go_to(i); }
    }
}

void ConfigWizardIndex::clear()
{
    auto *former_active = active_page();
    if (former_active != nullptr) { former_active->Hide(); }

    items.clear();
    item_active = 0;
}

void ConfigWizardIndex::on_paint(wxPaintEvent & evt)
{
    const auto size = GetClientSize();
    if (size.GetHeight() == 0 || size.GetWidth() == 0) { return; }

    wxPaintDC dc(this);

    const auto bullet_w = bullet_black.bmp().GetSize().GetWidth();
    const auto bullet_h = bullet_black.bmp().GetSize().GetHeight();
    const int yoff_icon = bullet_h < em_h ? (em_h - bullet_h) / 2 : 0;
    const int yoff_text = bullet_h > em_h ? (bullet_h - em_h) / 2 : 0;
    const int yinc = item_height();

    int index_width = 0;

    unsigned y = 0;
    for (size_t i = 0; i < items.size(); i++) {
        const Item& item = items[i];
        unsigned x = em_w/2 + item.indent * em_w;

        if (i == item_active || (item_hover >= 0 && i == (size_t)item_hover)) {
            dc.DrawBitmap(bullet_blue.bmp(), x, y + yoff_icon, false);
        }
        else if (i < item_active)  { dc.DrawBitmap(bullet_black.bmp(), x, y + yoff_icon, false); }
        else if (i > item_active)  { dc.DrawBitmap(bullet_white.bmp(), x, y + yoff_icon, false); }

        x += + bullet_w + em_w/2;
        const auto text_size = dc.GetTextExtent(item.label);
        dc.DrawText(item.label, x, y + yoff_text);

        y += yinc;
        index_width = std::max(index_width, (int)x + text_size.x);
    }

    if (GetMinSize().x < index_width) {
        CallAfter([this, index_width]() {
            SetMinSize(wxSize(index_width, GetMinSize().y));
            Refresh();
        });
    }
}

void ConfigWizardIndex::on_mouse_move(wxMouseEvent &evt)
{
    const wxClientDC dc(this);
    const wxPoint pos = evt.GetLogicalPosition(dc);

    const ssize_t item_hover_new = pos.y / item_height();

	if (item_hover_new < ssize_t(items.size()) && item_hover_new != item_hover) {
        item_hover = item_hover_new;
        Refresh();
    }

    evt.Skip();
}

void ConfigWizardIndex::msw_rescale()
{
    const wxSize size = GetTextExtent("m");
    em_w = size.x;
    em_h = size.y;

    bg.msw_rescale();
    SetMinSize(bg.bmp().GetSize());
    logo->SetBitmap(bg.bmp());

    bullet_black.msw_rescale();
    bullet_blue.msw_rescale();
    bullet_white.msw_rescale();
    Refresh();
}


// priv

static const std::unordered_map<std::string, std::pair<std::string, std::string>> legacy_preset_map {{
    { "Original Prusa i3 MK2.ini",                           std::make_pair("MK2S", "0.4") },
    { "Original Prusa i3 MK2 MM Single Mode.ini",            std::make_pair("MK2SMM", "0.4") },
    { "Original Prusa i3 MK2 MM Single Mode 0.6 nozzle.ini", std::make_pair("MK2SMM", "0.6") },
    { "Original Prusa i3 MK2 MultiMaterial.ini",             std::make_pair("MK2SMM", "0.4") },
    { "Original Prusa i3 MK2 MultiMaterial 0.6 nozzle.ini",  std::make_pair("MK2SMM", "0.6") },
    { "Original Prusa i3 MK2 0.25 nozzle.ini",               std::make_pair("MK2S", "0.25") },
    { "Original Prusa i3 MK2 0.6 nozzle.ini",                std::make_pair("MK2S", "0.6") },
    { "Original Prusa i3 MK3.ini",                           std::make_pair("MK3",  "0.4") },
}};

void ConfigWizard::priv::load_pages(bool custom_setup)
{
    const auto former_active = index->active_item();

    index->clear();

    index->add_page(page_welcome);
    index->add_page(page_fff);
    index->add_page(page_msla);
    index->add_page(page_custom);

    if (custom_setup) {
        index->add_page(page_firmware);
        index->add_page(page_bed);
        index->add_page(page_diams);
        index->add_page(page_temps);
    }

    index->add_page(page_update);

    index->go_to(former_active);   // Will restore the active item/page if possible

    q->Layout();
}

void ConfigWizard::priv::init_dialog_size()
{
    // Clamp the Wizard size based on screen dimensions

    const auto idx = wxDisplay::GetFromWindow(q);
    wxDisplay display(idx != wxNOT_FOUND ? idx : 0u);

    const auto disp_rect = display.GetClientArea();
    wxRect window_rect(
        disp_rect.x + disp_rect.width / 20,
        disp_rect.y + disp_rect.height / 20,
        9*disp_rect.width / 10,
        9*disp_rect.height / 10);

    const int width_hint = index->GetSize().GetWidth() + page_fff->get_width() + 30 * em();    // XXX: magic constant, I found no better solution
    if (width_hint < window_rect.width) {
        window_rect.x += (window_rect.width - width_hint) / 2;
        window_rect.width = width_hint;
    }

    q->SetSize(window_rect);
}

bool ConfigWizard::priv::check_first_variant() const
{
    return run_reason == RR_DATA_EMPTY || run_reason == RR_DATA_LEGACY;
}

void ConfigWizard::priv::load_vendors()
{
    const auto vendor_dir = fs::path(Slic3r::data_dir()) / "vendor";
    const auto rsrc_vendor_dir = fs::path(resources_dir()) / "profiles";

    // Load vendors from the "vendors" directory in datadir
    for (auto &dir_entry : boost::filesystem::directory_iterator(vendor_dir))
        if (Slic3r::is_ini_file(dir_entry)) {
            try {
                auto vp = VendorProfile::from_ini(dir_entry.path());
                vendors[vp.id] = std::move(vp);
            }
            catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << boost::format("Error loading vendor bundle %1%: %2%") % dir_entry.path() % e.what();
            }
        }

    // Additionally load up vendors from the application resources directory, but only those not seen in the datadir
    for (auto &dir_entry : boost::filesystem::directory_iterator(rsrc_vendor_dir))
        if (Slic3r::is_ini_file(dir_entry)) {
            const auto id = dir_entry.path().stem().string();
            if (vendors.find(id) == vendors.end()) {
                try {
                    auto vp = VendorProfile::from_ini(dir_entry.path());
                    vendors_rsrc[vp.id] = dir_entry.path().filename().string();
                    vendors[vp.id] = std::move(vp);
                }
                catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(error) << boost::format("Error loading vendor bundle %1%: %2%") % dir_entry.path() % e.what();
                }
            }
        }

    // Load up the set of vendors / models / variants the user has had enabled up till now
    const AppConfig *app_config = GUI::get_app_config();
    if (! app_config->legacy_datadir()) {
        appconfig_vendors.set_vendors(*app_config);
    } else {
        // In case of legacy datadir, try to guess the preference based on the printer preset files that are present
        const auto printer_dir = fs::path(Slic3r::data_dir()) / "printer";
        for (auto &dir_entry : boost::filesystem::directory_iterator(printer_dir))
            if (Slic3r::is_ini_file(dir_entry)) {
                auto needle = legacy_preset_map.find(dir_entry.path().filename().string());
                if (needle == legacy_preset_map.end()) { continue; }

                const auto &model = needle->second.first;
                const auto &variant = needle->second.second;
                appconfig_vendors.set_variant("PrusaResearch", model, variant, true);
            }
    }
}

void ConfigWizard::priv::add_page(ConfigWizardPage *page)
{
    hscroll_sizer->Add(page, 0, wxEXPAND);
}

void ConfigWizard::priv::enable_next(bool enable)
{
    btn_next->Enable(enable);
    btn_finish->Enable(enable);
}

void ConfigWizard::priv::on_custom_setup(bool custom_wanted)
{
    load_pages(custom_wanted);
}

void ConfigWizard::priv::apply_config(AppConfig *app_config, PresetBundle *preset_bundle, const PresetUpdater *updater)
{
    const auto enabled_vendors = appconfig_vendors.vendors();

    // Install bundles from resources if needed:
    std::vector<std::string> install_bundles;
    for (const auto &vendor_rsrc : vendors_rsrc) {
        const auto vendor = enabled_vendors.find(vendor_rsrc.first);
        if (vendor == enabled_vendors.end()) { continue; }

        size_t size_sum = 0;
        for (const auto &model : vendor->second) { size_sum += model.second.size(); }

        if (size_sum > 0) {
            // This vendor needs to be installed
            install_bundles.emplace_back(vendor_rsrc.second);
        }
    }

    // Decide whether to create snapshot based on run_reason and the reset profile checkbox
    bool snapshot = true;
    switch (run_reason) {
        case ConfigWizard::RR_DATA_EMPTY:    snapshot = false; break;
        case ConfigWizard::RR_DATA_LEGACY:   snapshot = true; break;
        case ConfigWizard::RR_DATA_INCOMPAT: snapshot = false; break;      // In this case snapshot is done by PresetUpdater with the appropriate reason
        case ConfigWizard::RR_USER:          snapshot = page_welcome->reset_user_profile(); break;
    }
    if (install_bundles.size() > 0) {
        // Install bundles from resources.
        updater->install_bundles_rsrc(std::move(install_bundles), snapshot);
    } else {
        BOOST_LOG_TRIVIAL(info) << "No bundles need to be installed from resources";
    }

    if (page_welcome->reset_user_profile()) {
        BOOST_LOG_TRIVIAL(info) << "Resetting user profiles...";
        preset_bundle->reset(true);
    }

    app_config->set_vendors(appconfig_vendors);
    app_config->set("version_check", page_update->version_check ? "1" : "0");
    app_config->set("preset_update", page_update->preset_update ? "1" : "0");

    std::string preferred_model;

    // Figure out the default pre-selected printer based on the seletions in the picker.
    // The default is the first selected printer model (one with at least 1 variant selected).
    // The default is only applied by load_presets() if the user doesn't have a (visible) printer
    // selected already.
    const auto vendor_prusa = vendors.find("PrusaResearch");
    const auto config_prusa = enabled_vendors.find("PrusaResearch");
    if (vendor_prusa != vendors.end() && config_prusa != enabled_vendors.end()) {
        for (const auto &model : vendor_prusa->second.models) {
            const auto model_it = config_prusa->second.find(model.id);
            if (model_it != config_prusa->second.end() && model_it->second.size() > 0) {
                preferred_model = model.id;
                break;
            }
        }
    }

    preset_bundle->load_presets(*app_config, preferred_model);

    if (page_custom->custom_wanted()) {
        page_firmware->apply_custom_config(*custom_config);
        page_bed->apply_custom_config(*custom_config);
        page_diams->apply_custom_config(*custom_config);
        page_temps->apply_custom_config(*custom_config);

        const std::string profile_name = page_custom->profile_name();
        preset_bundle->load_config(profile_name, *custom_config);
    }

    // Update the selections from the compatibilty.
    preset_bundle->export_selections(*app_config);
}

// Public

ConfigWizard::ConfigWizard(wxWindow *parent, RunReason reason)
    : DPIDialog(parent, wxID_ANY, wxString(SLIC3R_APP_NAME) + " - " + name(), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , p(new priv(this))
{
    this->SetFont(wxGetApp().normal_font());
    p->run_reason = reason;

    p->load_vendors();
    p->custom_config.reset(DynamicPrintConfig::new_from_defaults_keys({
        "gcode_flavor", "bed_shape", "bed_custom_texture", "nozzle_diameter", "filament_diameter", "temperature", "bed_temperature",
    }));

    p->index = new ConfigWizardIndex(this);

    auto *vsizer = new wxBoxSizer(wxVERTICAL);
    auto *topsizer = new wxBoxSizer(wxHORIZONTAL);
    auto *hline = new wxStaticLine(this);
    p->btnsizer = new wxBoxSizer(wxHORIZONTAL);

    // Initially we _do not_ SetScrollRate in order to figure out the overall width of the Wizard  without scrolling.
    // Later, we compare that to the size of the current screen and set minimum width based on that (see below).
    p->hscroll = new wxScrolledWindow(this);
    p->hscroll_sizer = new wxBoxSizer(wxHORIZONTAL);
    p->hscroll->SetSizer(p->hscroll_sizer);

    topsizer->Add(p->index, 0, wxEXPAND);
    topsizer->AddSpacer(INDEX_MARGIN);
    topsizer->Add(p->hscroll, 1, wxEXPAND);

    p->btn_sel_all = new wxButton(this, wxID_ANY, _(L("Select all standard printers")));
    p->btnsizer->Add(p->btn_sel_all);

    p->btn_prev = new wxButton(this, wxID_ANY, _(L("< &Back")));
    p->btn_next = new wxButton(this, wxID_ANY, _(L("&Next >")));
    p->btn_finish = new wxButton(this, wxID_APPLY, _(L("&Finish")));
    p->btn_cancel = new wxButton(this, wxID_CANCEL, _(L("Cancel")));   // Note: The label needs to be present, otherwise we get accelerator bugs on Mac
    p->btnsizer->AddStretchSpacer();
    p->btnsizer->Add(p->btn_prev, 0, wxLEFT, BTN_SPACING);
    p->btnsizer->Add(p->btn_next, 0, wxLEFT, BTN_SPACING);
    p->btnsizer->Add(p->btn_finish, 0, wxLEFT, BTN_SPACING);
    p->btnsizer->Add(p->btn_cancel, 0, wxLEFT, BTN_SPACING);

    const auto &vendors = p->vendors;
    const auto vendor_prusa_it = vendors.find("PrusaResearch");
    wxCHECK_RET(vendor_prusa_it != vendors.cend(), "Vendor PrusaResearch not found");
    const VendorProfile &vendor_prusa = vendor_prusa_it->second;

    p->add_page(p->page_welcome = new PageWelcome(this));

    p->page_fff = new PagePrinters(this, _(L("Prusa FFF Technology Printers")), "Prusa FFF", vendor_prusa, 0, PagePrinters::T_FFF);
    p->add_page(p->page_fff);

    p->page_msla = new PagePrinters(this, _(L("Prusa MSLA Technology Printers")), "Prusa MSLA", vendor_prusa, 0, PagePrinters::T_SLA);
    p->add_page(p->page_msla);

    p->add_page(p->page_custom   = new PageCustom(this));
    p->add_page(p->page_update   = new PageUpdate(this));
    p->add_page(p->page_vendors  = new PageVendors(this));
    p->add_page(p->page_firmware = new PageFirmware(this));
    p->add_page(p->page_bed      = new PageBedShape(this));
    p->add_page(p->page_diams    = new PageDiameters(this));
    p->add_page(p->page_temps    = new PageTemperatures(this));

    p->load_pages(false);

    vsizer->Add(topsizer, 1, wxEXPAND | wxALL, DIALOG_MARGIN);
    vsizer->Add(hline, 0, wxEXPAND);
    vsizer->Add(p->btnsizer, 0, wxEXPAND | wxALL, DIALOG_MARGIN);

    SetSizer(vsizer);
    SetSizerAndFit(vsizer);

    // We can now enable scrolling on hscroll
    p->hscroll->SetScrollRate(30, 30);

    on_window_geometry(this, [this]() {
        p->init_dialog_size();
    });

    p->btn_prev->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &) { this->p->index->go_prev(); });
    p->btn_next->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &) { this->p->index->go_next(); });
    p->btn_finish->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &) { this->EndModal(wxID_OK); });
    p->btn_finish->Hide();

    p->btn_sel_all->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &) {
        p->page_fff->select_all(true, false);
        p->page_msla->select_all(true, false);
        p->index->go_to(p->page_update);
    });

    p->index->Bind(EVT_INDEX_PAGE, [this](const wxCommandEvent &) {
        const bool is_last = p->index->active_is_last();
        p->btn_next->Show(! is_last);
        p->btn_finish->Show(is_last);

        Layout();
    });
}

ConfigWizard::~ConfigWizard() {}

bool ConfigWizard::run(PresetBundle *preset_bundle, const PresetUpdater *updater)
{
    BOOST_LOG_TRIVIAL(info) << "Running ConfigWizard, reason: " << p->run_reason;
    if (ShowModal() == wxID_OK) {
        auto *app_config = GUI::get_app_config();
        p->apply_config(app_config, preset_bundle, updater);
        app_config->set_legacy_datadir(false);
        BOOST_LOG_TRIVIAL(info) << "ConfigWizard applied";
        return true;
    } else {
        BOOST_LOG_TRIVIAL(info) << "ConfigWizard cancelled";
        return false;
    }
}


const wxString& ConfigWizard::name(const bool from_menu/* = false*/)
{
    // A different naming convention is used for the Wizard on Windows vs. OSX & GTK.
#if __APPLE__
    static const wxString config_wizard_name =  _(L("Configuration Assistant"));
    static const wxString config_wizard_name_menu = _(L("Configuration &Assistant"));
#else
    static const wxString config_wizard_name = _(L("Configuration Wizard"));
    static const wxString config_wizard_name_menu = _(L("Configuration &Wizard"));
#endif
    return from_menu ? config_wizard_name_menu : config_wizard_name;
}

void ConfigWizard::on_dpi_changed(const wxRect &suggested_rect)
{
    p->index->msw_rescale();

    const int& em = em_unit();

    msw_buttons_rescale(this, em, { wxID_APPLY, 
                                    wxID_CANCEL,
                                    p->btn_sel_all->GetId(),
                                    p->btn_next->GetId(),
                                    p->btn_prev->GetId() });

    for (auto printer_picker: p->page_fff->printer_pickers)
        msw_buttons_rescale(this, em, printer_picker->get_button_indexes());

    p->init_dialog_size();

    Refresh();
}

}
}
