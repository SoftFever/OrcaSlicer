#include "ConfigWizard_private.hpp"

#include <algorithm>
#include <utility>
#include <unordered_map>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

#include <wx/settings.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/dcclient.h>
#include <wx/statbmp.h>
#include <wx/checkbox.h>
#include <wx/statline.h>

#include "libslic3r/Utils.hpp"
#include "PresetBundle.hpp"
#include "GUI.hpp"
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

	PrinterPickerEvent(wxEventType eventType, int winid, std::string vendor_id, std::string model_id, std::string variant_name, bool enable) :
		wxEvent(winid, eventType),
		vendor_id(std::move(vendor_id)),
		model_id(std::move(model_id)),
		variant_name(std::move(variant_name)),
		enable(enable)
	{}

	virtual wxEvent *Clone() const
	{
		return new PrinterPickerEvent(*this);
	}
};

wxDEFINE_EVENT(EVT_PRINTER_PICK, PrinterPickerEvent);

PrinterPicker::PrinterPicker(wxWindow *parent, const VendorProfile &vendor, const AppConfig &appconfig_vendors) :
	wxPanel(parent),
	vendor_id(vendor.id),
	variants_checked(0)
{
	const auto &models = vendor.models;

	auto *sizer = new wxBoxSizer(wxVERTICAL);

	auto *printer_grid = new wxFlexGridSizer(models.size(), 0, 20);
	printer_grid->SetFlexibleDirection(wxVERTICAL | wxHORIZONTAL);
	sizer->Add(printer_grid);

	auto namefont = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
	namefont.SetWeight(wxFONTWEIGHT_BOLD);

	// wxGrid appends widgets by rows, but we need to construct them in columns.
	// These vectors are used to hold the elements so that they can be appended in the right order.
	std::vector<wxStaticText*> titles;
	std::vector<wxStaticBitmap*> bitmaps;
	std::vector<wxPanel*> variants_panels;

	for (const auto &model : models) {
		auto bitmap_file = wxString::Format("printers/%s_%s.png", vendor.id, model.id);
		wxBitmap bitmap(GUI::from_u8(Slic3r::var(bitmap_file.ToStdString())), wxBITMAP_TYPE_PNG);

		auto *title = new wxStaticText(this, wxID_ANY, model.name, wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
		title->SetFont(namefont);
		title->Wrap(std::max((int)MODEL_MIN_WRAP, bitmap.GetWidth()));
		titles.push_back(title);

		auto *bitmap_widget = new wxStaticBitmap(this, wxID_ANY, bitmap);
		bitmaps.push_back(bitmap_widget);

		auto *variants_panel = new wxPanel(this);
		auto *variants_sizer = new wxBoxSizer(wxVERTICAL);
		variants_panel->SetSizer(variants_sizer);
		const auto model_id = model.id;

		bool default_variant = true;   // Mark the first variant as default in the GUI
		for (const auto &variant : model.variants) {
			const auto label = wxString::Format("%s %s %s %s", variant.name, _(L("mm")), _(L("nozzle")),
				(default_variant ? _(L("(default)")) : wxString()));
			default_variant = false;
			auto *cbox = new Checkbox(variants_panel, label, model_id, variant.name);
			const size_t idx = cboxes.size();
			cboxes.push_back(cbox);
			bool enabled = appconfig_vendors.get_variant("PrusaResearch", model_id, variant.name);
			variants_checked += enabled;
			cbox->SetValue(enabled);
			variants_sizer->Add(cbox, 0, wxBOTTOM, 3);
			cbox->Bind(wxEVT_CHECKBOX, [this, idx](wxCommandEvent &event) {
				if (idx >= this->cboxes.size()) { return; }
				this->on_checkbox(this->cboxes[idx], event.IsChecked());
			});
		}

		variants_panels.push_back(variants_panel);
	}

	for (auto title : titles)       { printer_grid->Add(title, 0, wxBOTTOM, 3); }
	for (auto bitmap : bitmaps)     { printer_grid->Add(bitmap, 0, wxBOTTOM, 20); }
	for (auto vp : variants_panels) { printer_grid->Add(vp); }

	auto *all_none_sizer = new wxBoxSizer(wxHORIZONTAL);
	auto *sel_all = new wxButton(this, wxID_ANY, _(L("Select all")));
	auto *sel_none = new wxButton(this, wxID_ANY, _(L("Select none")));
	sel_all->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &event) { this->select_all(true); });
	sel_none->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &event) { this->select_all(false); });
	all_none_sizer->AddStretchSpacer();
	all_none_sizer->Add(sel_all);
	all_none_sizer->Add(sel_none);
	sizer->AddStretchSpacer();
	sizer->Add(all_none_sizer, 0, wxEXPAND);

	SetSizer(sizer);
}

void PrinterPicker::select_all(bool select)
{
	for (const auto &cb : cboxes) {
		if (cb->GetValue() != select) {
			cb->SetValue(select);
			on_checkbox(cb, select);
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
	variants_checked += checked ? 1 : -1;
	PrinterPickerEvent evt(EVT_PRINTER_PICK, GetId(), vendor_id, cbox->model, cbox->variant, checked);
	AddPendingEvent(evt);
}


// Wizard page base

ConfigWizardPage::ConfigWizardPage(ConfigWizard *parent, wxString title, wxString shortname) :
	wxPanel(parent->p->hscroll),
	parent(parent),
	shortname(std::move(shortname)),
	p_prev(nullptr),
	p_next(nullptr)
{
	auto *sizer = new wxBoxSizer(wxVERTICAL);

	auto *text = new wxStaticText(this, wxID_ANY, std::move(title), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
	auto font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
	font.SetWeight(wxFONTWEIGHT_BOLD);
	font.SetPointSize(14);
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

ConfigWizardPage* ConfigWizardPage::chain(ConfigWizardPage *page)
{
	if (p_next != nullptr) { p_next->p_prev = nullptr; }
	p_next = page;
	if (page != nullptr) {
		if (page->p_prev != nullptr) { page->p_prev->p_next = nullptr; }
		page->p_prev = this;
	}

	return page;
}

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

bool ConfigWizardPage::Show(bool show)
{
	if (extra_buttons() != nullptr) { extra_buttons()->Show(show); }
	return wxPanel::Show(show);
}

void ConfigWizardPage::enable_next(bool enable) { parent->p->enable_next(enable); }


// Wizard pages

PageWelcome::PageWelcome(ConfigWizard *parent, bool check_first_variant) :
	ConfigWizardPage(parent, wxString::Format(_(L("Welcome to the Slic3r %s")), ConfigWizard::name()), _(L("Welcome"))),
	printer_picker(nullptr),
	others_buttons(new wxPanel(parent)),
	cbox_reset(nullptr)
{
	if (wizard_p()->run_reason == ConfigWizard::RR_DATA_EMPTY) {
		wxString::Format(_(L("Run %s")), ConfigWizard::name());
		append_text(wxString::Format(
			_(L("Hello, welcome to Slic3r Prusa Edition! This %s helps you with the initial configuration; just a few settings and you will be ready to print.")),
			ConfigWizard::name())
		);
	} else {
		cbox_reset = new wxCheckBox(this, wxID_ANY, _(L("Remove user profiles - install from scratch (a snapshot will be taken beforehand)")));
		append(cbox_reset);
	}

	const auto &vendors = wizard_p()->vendors;
	const auto vendor_prusa = vendors.find("PrusaResearch");

	if (vendor_prusa != vendors.cend()) {
		AppConfig &appconfig_vendors = this->wizard_p()->appconfig_vendors;

		printer_picker = new PrinterPicker(this, vendor_prusa->second, appconfig_vendors);
		if (check_first_variant) {
			// Select the default (first) model/variant on the Prusa vendor
			printer_picker->select_one(0, true);
		}
		printer_picker->Bind(EVT_PRINTER_PICK, [this, &appconfig_vendors](const PrinterPickerEvent &evt) {
			appconfig_vendors.set_variant(evt.vendor_id, evt.model_id, evt.variant_name, evt.enable);
			this->on_variant_checked();
		});

		append(printer_picker);
	}

	const size_t num_other_vendors = vendors.size() - (vendor_prusa != vendors.cend());
	auto *sizer = new wxBoxSizer(wxHORIZONTAL);
	auto *other_vendors = new wxButton(others_buttons, wxID_ANY, _(L("Other vendors")));
	other_vendors->Enable(num_other_vendors > 0);
	auto *custom_setup = new wxButton(others_buttons, wxID_ANY, _(L("Custom setup")));

	sizer->Add(other_vendors);
	sizer->AddSpacer(BTN_SPACING);
	sizer->Add(custom_setup);

	other_vendors->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &event) { this->wizard_p()->on_other_vendors(); });
	custom_setup->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &event) { this->wizard_p()->on_custom_setup(); });

	others_buttons->SetSizer(sizer);
}

void PageWelcome::on_page_set()
{
	chain(wizard_p()->page_update);
	on_variant_checked();
}

void PageWelcome::on_variant_checked()
{
	enable_next(printer_picker != nullptr ? printer_picker->variants_checked > 0 : false);
}

PageUpdate::PageUpdate(ConfigWizard *parent) :
	ConfigWizardPage(parent, _(L("Automatic updates")), _(L("Updates"))),
	version_check(true),
	preset_update(true)
{
	const AppConfig *app_config = GUI::get_app_config();
	auto boldfont = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
	boldfont.SetWeight(wxFONTWEIGHT_BOLD);

	auto *box_slic3r = new wxCheckBox(this, wxID_ANY, _(L("Check for application updates")));
	box_slic3r->SetValue(app_config->get("version_check") == "1");
	append(box_slic3r);
	append_text(_(L("If enabled, Slic3r checks for new versions of Slic3r PE online. When a new version becomes available a notification is displayed at the next application startup (never during program usage). This is only a notification mechanisms, no automatic installation is done.")));

	append_spacer(VERTICAL_SPACING);

	auto *box_presets = new wxCheckBox(this, wxID_ANY, _(L("Update built-in Presets automatically")));
	box_presets->SetValue(app_config->get("preset_update") == "1");
	append(box_presets);
	append_text(_(L("If enabled, Slic3r downloads updates of built-in system presets in the background. These updates are downloaded into a separate temporary location. When a new preset version becomes available it is offered at application startup.")));
	const auto text_bold = _(L("Updates are never applied without user's consent and never overwrite user's customized settings."));
	auto *label_bold = new wxStaticText(this, wxID_ANY, text_bold);
	label_bold->SetFont(boldfont);
	label_bold->Wrap(WRAP_WIDTH);
	append(label_bold);
	append_text(_(L("Additionally a backup snapshot of the whole configuration is created before an update is applied.")));

	box_slic3r->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &event) { this->version_check = event.IsChecked(); });
	box_presets->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &event) { this->preset_update = event.IsChecked(); });
}

PageVendors::PageVendors(ConfigWizard *parent) :
	ConfigWizardPage(parent, _(L("Other Vendors")), _(L("Other Vendors")))
{
	append_text(_(L("Pick another vendor supported by Slic3r PE:")));

	auto boldfont = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
	boldfont.SetWeight(wxFONTWEIGHT_BOLD);

	AppConfig &appconfig_vendors = this->wizard_p()->appconfig_vendors;
	wxArrayString choices_vendors;

	for (const auto vendor_pair : wizard_p()->vendors) {
		const auto &vendor = vendor_pair.second;
		if (vendor.id == "PrusaResearch") { continue; }

		auto *picker = new PrinterPicker(this, vendor, appconfig_vendors);
		picker->Hide();
		pickers.push_back(picker);
		choices_vendors.Add(vendor.name);

		picker->Bind(EVT_PRINTER_PICK, [this, &appconfig_vendors](const PrinterPickerEvent &evt) {
			appconfig_vendors.set_variant(evt.vendor_id, evt.model_id, evt.variant_name, evt.enable);
			this->on_variant_checked();
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

void PageVendors::on_page_set()
{
	on_variant_checked();
}

void PageVendors::on_vendor_pick(size_t i)
{
	for (PrinterPicker *picker : pickers) { picker->Hide(); }
	if (i < pickers.size()) {
		pickers[i]->Show();
		wizard_p()->layout_fit();
	}
}

void PageVendors::on_variant_checked()
{
	size_t variants_checked = 0;
	for (const PrinterPicker *picker : pickers) { variants_checked += picker->variants_checked; }
	enable_next(variants_checked > 0);
}

PageFirmware::PageFirmware(ConfigWizard *parent) :
	ConfigWizardPage(parent, _(L("Firmware Type")), _(L("Firmware"))),
	gcode_opt(print_config_def.options["gcode_flavor"]),
	gcode_picker(nullptr)
{
	append_text(_(L("Choose the type of firmware used by your printer.")));
	append_text(gcode_opt.tooltip);

	wxArrayString choices;
	choices.Alloc(gcode_opt.enum_labels.size());
	for (const auto &label : gcode_opt.enum_labels) {
		choices.Add(label);
	}

	gcode_picker = new wxChoice(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, choices);
	const auto &enum_values = gcode_opt.enum_values;
	auto needle = enum_values.cend();
	if (gcode_opt.default_value != nullptr) {
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
	if (sel >= 0 && sel < gcode_opt.enum_labels.size()) {
		auto *opt = new ConfigOptionEnum<GCodeFlavor>(static_cast<GCodeFlavor>(sel));
		config.set_key_value("gcode_flavor", opt);
	}
}

PageBedShape::PageBedShape(ConfigWizard *parent) :
	ConfigWizardPage(parent, _(L("Bed Shape and Size")), _(L("Bed Shape"))),
	shape_panel(new BedShapePanel(this))
{
	append_text(_(L("Set the shape of your printer's bed.")));

	shape_panel->build_panel(wizard_p()->custom_config->option<ConfigOptionPoints>("bed_shape"));
	append(shape_panel);
}

void PageBedShape::apply_custom_config(DynamicPrintConfig &config)
{
	const auto points(shape_panel->GetValue());
	auto *opt = new ConfigOptionPoints(points);
	config.set_key_value("bed_shape", opt);
}

PageDiameters::PageDiameters(ConfigWizard *parent) :
	ConfigWizardPage(parent, _(L("Filament and Nozzle Diameters")), _(L("Print Diameters"))),
	spin_nozzle(new wxSpinCtrlDouble(this, wxID_ANY)),
	spin_filam(new wxSpinCtrlDouble(this, wxID_ANY))
{
	spin_nozzle->SetDigits(2);
	spin_nozzle->SetIncrement(0.1);
	const auto &def_nozzle = print_config_def.options["nozzle_diameter"];
	auto *default_nozzle = dynamic_cast<const ConfigOptionFloats*>(def_nozzle.default_value);
	spin_nozzle->SetValue(default_nozzle != nullptr && default_nozzle->size() > 0 ? default_nozzle->get_at(0) : 0.5);

	spin_filam->SetDigits(2);
	spin_filam->SetIncrement(0.25);
	const auto &def_filam = print_config_def.options["filament_diameter"];
	auto *default_filam = dynamic_cast<const ConfigOptionFloats*>(def_filam.default_value);
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
}

PageTemperatures::PageTemperatures(ConfigWizard *parent) :
	ConfigWizardPage(parent, _(L("Extruder and Bed Temperatures")), _(L("Temperatures"))),
	spin_extr(new wxSpinCtrlDouble(this, wxID_ANY)),
	spin_bed(new wxSpinCtrlDouble(this, wxID_ANY))
{
	spin_extr->SetIncrement(5.0);
	const auto &def_extr = print_config_def.options["temperature"];
	spin_extr->SetRange(def_extr.min, def_extr.max);
	auto *default_extr = dynamic_cast<const ConfigOptionInts*>(def_extr.default_value);
	spin_extr->SetValue(default_extr != nullptr && default_extr->size() > 0 ? default_extr->get_at(0) : 200);

	spin_bed->SetIncrement(5.0);
	const auto &def_bed = print_config_def.options["bed_temperature"];
	spin_bed->SetRange(def_bed.min, def_bed.max);
	auto *default_bed = dynamic_cast<const ConfigOptionInts*>(def_bed.default_value);
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

ConfigWizardIndex::ConfigWizardIndex(wxWindow *parent) :
	wxPanel(parent),
	bg(GUI::from_u8(Slic3r::var("Slic3r_192px_transparent.png")), wxBITMAP_TYPE_PNG),
	bullet_black(GUI::from_u8(Slic3r::var("bullet_black.png")), wxBITMAP_TYPE_PNG),
	bullet_blue(GUI::from_u8(Slic3r::var("bullet_blue.png")), wxBITMAP_TYPE_PNG),
	bullet_white(GUI::from_u8(Slic3r::var("bullet_white.png")), wxBITMAP_TYPE_PNG)
{
	SetMinSize(bg.GetSize());

	wxClientDC dc(this);
	text_height = dc.GetCharHeight();

	// Add logo bitmap.
	// This could be done in on_paint() along with the index labels, but I've found it tricky
	// to get the bitmap rendered well on all platforms with transparent background.
	// In some cases it didn't work at all. And so wxStaticBitmap is used here instead,
	// because it has all the platform quirks figured out.
	auto *sizer = new wxBoxSizer(wxVERTICAL);
	auto *logo = new wxStaticBitmap(this, wxID_ANY, bg);
	sizer->AddStretchSpacer();
	sizer->Add(logo);
	SetSizer(sizer);

	Bind(wxEVT_PAINT, &ConfigWizardIndex::on_paint, this);
}

void ConfigWizardIndex::load_items(ConfigWizardPage *firstpage)
{
	items.clear();
	item_active = items.cend();

	for (auto *page = firstpage; page != nullptr; page = page->page_next()) {
		items.emplace_back(page->shortname);
	}

	Refresh();
}

void ConfigWizardIndex::set_active(ConfigWizardPage *page)
{
	item_active = std::find(items.cbegin(), items.cend(), page->shortname);
	Refresh();
}

void ConfigWizardIndex::on_paint(wxPaintEvent & evt)
{
	enum {
		MARGIN = 10,
		SPACING = 5,
	};

	const auto size = GetClientSize();
	if (size.GetHeight() == 0 || size.GetWidth() == 0) { return; }

	wxPaintDC dc(this);

	const auto bullet_w = bullet_black.GetSize().GetWidth();
	const auto bullet_h = bullet_black.GetSize().GetHeight();
	const int yoff_icon = bullet_h < text_height ? (text_height - bullet_h) / 2 : 0;
	const int yoff_text = bullet_h > text_height ? (bullet_h - text_height) / 2 : 0;
	const int yinc = std::max(bullet_h, text_height) + SPACING;

	unsigned y = 0;
	for (auto it = items.cbegin(); it != items.cend(); ++it) {
		if (it < item_active)  { dc.DrawBitmap(bullet_black, MARGIN, y + yoff_icon, false); }
		if (it == item_active) { dc.DrawBitmap(bullet_blue,  MARGIN, y + yoff_icon, false); }
		if (it > item_active)  { dc.DrawBitmap(bullet_white, MARGIN, y + yoff_icon, false); }
		dc.DrawText(*it, MARGIN + bullet_w + SPACING, y + yoff_text);
		y += yinc;
	}
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

void ConfigWizard::priv::load_vendors()
{
	const auto vendor_dir = fs::path(Slic3r::data_dir()) / "vendor";
	const auto rsrc_vendor_dir = fs::path(resources_dir()) / "profiles";

	// Load vendors from the "vendors" directory in datadir
	for (fs::directory_iterator it(vendor_dir); it != fs::directory_iterator(); ++it) {
		if (it->path().extension() == ".ini") {
			try {
				auto vp = VendorProfile::from_ini(it->path());
				vendors[vp.id] = std::move(vp);
			}
			catch (const std::exception& e) {
				BOOST_LOG_TRIVIAL(error) << boost::format("Error loading vendor bundle %1%: %2%") % it->path() % e.what();
			}

		}
	}

	// Additionally load up vendors from the application resources directory, but only those not seen in the datadir
	for (fs::directory_iterator it(rsrc_vendor_dir); it != fs::directory_iterator(); ++it) {
		if (it->path().extension() == ".ini") {
			const auto id = it->path().stem().string();
			if (vendors.find(id) == vendors.end()) {
				try {
					auto vp = VendorProfile::from_ini(it->path());
					vendors_rsrc[vp.id] = it->path().filename().string();
					vendors[vp.id] = std::move(vp);
				}
				catch (const std::exception& e) {
					BOOST_LOG_TRIVIAL(error) << boost::format("Error loading vendor bundle %1%: %2%") % it->path() % e.what();
				}
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
		for (fs::directory_iterator it(printer_dir); it != fs::directory_iterator(); ++it) {
			auto needle = legacy_preset_map.find(it->path().filename().string());
			if (needle == legacy_preset_map.end()) { continue; }

			const auto &model = needle->second.first;
			const auto &variant = needle->second.second;
			appconfig_vendors.set_variant("PrusaResearch", model, variant, true);
		}
	}
}

void ConfigWizard::priv::index_refresh()
{
	index->load_items(page_welcome);
}

void ConfigWizard::priv::add_page(ConfigWizardPage *page)
{
	hscroll_sizer->Add(page, 0, wxEXPAND);

	auto *extra_buttons = page->extra_buttons();
	if (extra_buttons != nullptr) {
		btnsizer->Prepend(extra_buttons, 0);
	}
}

void ConfigWizard::priv::set_page(ConfigWizardPage *page)
{
	if (page == nullptr) { return; }
	if (page_current != nullptr) { page_current->Hide(); }
	page_current = page;
	enable_next(true);

	page->on_page_set();
	index->load_items(page_welcome);
	index->set_active(page);
	page->Show();

	btn_prev->Enable(page->page_prev() != nullptr);
	btn_next->Show(page->page_next() != nullptr);
	btn_finish->Show(page->page_next() == nullptr);

	layout_fit();
}

void ConfigWizard::priv::layout_fit()
{
	q->Layout();
	q->Fit();
}

void ConfigWizard::priv::enable_next(bool enable)
{
	btn_next->Enable(enable);
	btn_finish->Enable(enable);
}

void ConfigWizard::priv::on_other_vendors()
{
	page_welcome
		->chain(page_vendors)
		->chain(page_update);
	set_page(page_vendors);
}

void ConfigWizard::priv::on_custom_setup()
{
	page_welcome->chain(page_firmware);
	page_temps->chain(page_update);
	set_page(page_firmware);
}

void ConfigWizard::priv::apply_config(AppConfig *app_config, PresetBundle *preset_bundle, const PresetUpdater *updater)
{
	const bool is_custom_setup = page_welcome->page_next() == page_firmware;

	if (! is_custom_setup) {
		const auto enabled_vendors = appconfig_vendors.vendors();

		// Install bundles from resources if needed:
		std::vector<std::string> install_bundles;
		for (const auto &vendor_rsrc : vendors_rsrc) {
			const auto vendor = enabled_vendors.find(vendor_rsrc.first);
			if (vendor == enabled_vendors.end()) { continue; }

			size_t size_sum = 0;
			for (const auto &model : vendor->second) { size_sum += model.second.size(); }
			if (size_sum == 0) { continue; }

			// This vendor needs to be installed
			install_bundles.emplace_back(vendor_rsrc.second);
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
		app_config->reset_selections();
		preset_bundle->load_presets(*app_config);
	} else {
		for (ConfigWizardPage *page = page_firmware; page != nullptr; page = page->page_next()) {
			page->apply_custom_config(*custom_config);
		}
		preset_bundle->load_config("My Settings", *custom_config);
	}
	// Update the selections from the compatibilty.
	preset_bundle->export_selections(*app_config);
}

// Public

ConfigWizard::ConfigWizard(wxWindow *parent, RunReason reason) :
	wxDialog(parent, wxID_ANY, name(), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	p(new priv(this))
{
	p->run_reason = reason;

	p->load_vendors();
	p->custom_config.reset(DynamicPrintConfig::new_from_defaults_keys({
		"gcode_flavor", "bed_shape", "nozzle_diameter", "filament_diameter", "temperature", "bed_temperature",
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

	p->btn_prev = new wxButton(this, wxID_NONE, _(L("< &Back")));
	p->btn_next = new wxButton(this, wxID_NONE, _(L("&Next >")));
	p->btn_finish = new wxButton(this, wxID_APPLY, _(L("&Finish")));
	p->btn_cancel = new wxButton(this, wxID_CANCEL);
	p->btnsizer->AddStretchSpacer();
	p->btnsizer->Add(p->btn_prev, 0, wxLEFT, BTN_SPACING);
	p->btnsizer->Add(p->btn_next, 0, wxLEFT, BTN_SPACING);
	p->btnsizer->Add(p->btn_finish, 0, wxLEFT, BTN_SPACING);
	p->btnsizer->Add(p->btn_cancel, 0, wxLEFT, BTN_SPACING);

	p->add_page(p->page_welcome  = new PageWelcome(this, reason == RR_DATA_EMPTY || reason == RR_DATA_LEGACY));
	p->add_page(p->page_update   = new PageUpdate(this));
	p->add_page(p->page_vendors  = new PageVendors(this));
	p->add_page(p->page_firmware = new PageFirmware(this));
	p->add_page(p->page_bed      = new PageBedShape(this));
	p->add_page(p->page_diams    = new PageDiameters(this));
	p->add_page(p->page_temps    = new PageTemperatures(this));
	p->index_refresh();

	p->page_welcome->chain(p->page_update);
	p->page_firmware
		->chain(p->page_bed)
		->chain(p->page_diams)
		->chain(p->page_temps);

	vsizer->Add(topsizer, 1, wxEXPAND | wxALL, DIALOG_MARGIN);
	vsizer->Add(hline, 0, wxEXPAND);
	vsizer->Add(p->btnsizer, 0, wxEXPAND | wxALL, DIALOG_MARGIN);

	p->set_page(p->page_welcome);
	SetSizer(vsizer);
	SetSizerAndFit(vsizer);

	// We can now enable scrolling on hscroll
	p->hscroll->SetScrollRate(30, 30);
	// Compare current ("ideal") wizard size with the size of the current screen.
	// If the screen is smaller, resize wizrad to match, which will enable scrollbars.
	auto wizard_size = GetSize();
	unsigned width, height;
	if (GUI::get_current_screen_size(this, width, height)) {
		wizard_size.SetWidth(std::min(wizard_size.GetWidth(), (int)(width - 2 * DIALOG_MARGIN)));
		wizard_size.SetHeight(std::min(wizard_size.GetHeight(), (int)(height - 2 * DIALOG_MARGIN)));
		SetMinSize(wizard_size);
	}
	Fit();

	p->btn_prev->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &evt) { this->p->go_prev(); });
	p->btn_next->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &evt) { this->p->go_next(); });
	p->btn_finish->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &evt) { this->EndModal(wxID_OK); });
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


const wxString& ConfigWizard::name()
{
	// A different naming convention is used for the Wizard on Windows vs. OSX & GTK.
#if WIN32
	static const wxString config_wizard_name = L("Configuration Wizard");
#else
	static const wxString config_wizard_name = L("Configuration Assistant");
#endif
	return config_wizard_name;
}

}
}
