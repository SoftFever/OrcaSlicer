#include "ConfigWizard_private.hpp"

#include <iostream>   // XXX
#include <algorithm>
#include <utility>

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
	variants_checked(0)
{
	const auto vendor_id = vendor.id;
	const auto &models = vendor.models;

	auto *printer_grid = new wxFlexGridSizer(models.size(), 0, 20);
	printer_grid->SetFlexibleDirection(wxVERTICAL);
	SetSizer(printer_grid);

	auto namefont = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
	namefont.SetWeight(wxFONTWEIGHT_BOLD);

	for (const auto &model : models) {
		auto *panel = new wxPanel(this);
		auto *sizer = new wxBoxSizer(wxVERTICAL);
		panel->SetSizer(sizer);

		auto *title = new wxStaticText(panel, wxID_ANY, model.name, wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
		title->SetFont(namefont);
		sizer->Add(title, 0, wxBOTTOM, 3);

		auto bitmap_file = wxString::Format("printers/%s_%s.png", vendor.id, model.id);
		wxBitmap bitmap(GUI::from_u8(Slic3r::var(bitmap_file.ToStdString())), wxBITMAP_TYPE_PNG);
		auto *bitmap_widget = new wxStaticBitmap(panel, wxID_ANY, bitmap);
		sizer->Add(bitmap_widget, 0, wxBOTTOM, 3);

		sizer->AddSpacer(20);

		const auto model_id = model.id;

		for (const auto &variant : model.variants) {
			const auto variant_name = variant.name;
			auto *cbox = new wxCheckBox(panel, wxID_ANY, wxString::Format("%s %s %s", variant.name, _(L("mm")), _(L("nozzle"))));
			bool enabled = appconfig_vendors.get_variant("PrusaResearch", model_id, variant_name);
			variants_checked += enabled;
			cbox->SetValue(enabled);
			sizer->Add(cbox, 0, wxBOTTOM, 3);
			cbox->Bind(wxEVT_CHECKBOX, [=](wxCommandEvent &event) {
				this->variants_checked += event.IsChecked() ? 1 : -1;
				PrinterPickerEvent evt(EVT_PRINTER_PICK, this->GetId(), std::move(vendor_id), std::move(model_id), std::move(variant_name), event.IsChecked());
				this->AddPendingEvent(evt);
			});
		}

		printer_grid->Add(panel);
	}

}


// Wizard page base

ConfigWizardPage::ConfigWizardPage(ConfigWizard *parent, wxString title, wxString shortname) :
	wxPanel(parent),
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
	widget->Wrap(CONTENT_WIDTH);
	widget->SetMinSize(wxSize(CONTENT_WIDTH, -1));
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

PageWelcome::PageWelcome(ConfigWizard *parent) :
	ConfigWizardPage(parent, _(L("Welcome to the Slic3r Configuration assistant")), _(L("Welcome"))),
	printer_picker(nullptr),
	others_buttons(new wxPanel(parent))
{
	append_text(_(L("Hello, welcome to Slic3r Prusa Edition! TODO: This text.")));

	// const PresetBundle &bundle = wizard_p()->bundle_vendors;
	// const auto &vendors = bundle.vendors;
	const auto &vendors = wizard_p()->vendors;
	// const auto vendor_prusa = std::find(vendors.cbegin(), vendors.cend(), VendorProfile("PrusaResearch"));
	const auto vendor_prusa = vendors.find("PrusaResearch");

	if (vendor_prusa != vendors.cend()) {
		const auto &models = vendor_prusa->second.models;

		AppConfig &appconfig_vendors = this->wizard_p()->appconfig_vendors;

		printer_picker = new PrinterPicker(this, vendor_prusa->second, appconfig_vendors);
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

	append_text(_(L("TODO: text")));
	auto *box_slic3r = new wxCheckBox(this, wxID_ANY, _(L("Check for Slic3r updates")));
	box_slic3r->SetValue(app_config->get("version_check") == "1");
	append(box_slic3r);

	append_text(_(L("TODO: text")));
	auto *box_presets = new wxCheckBox(this, wxID_ANY, _(L("Update built-in Presets automatically")));
	box_presets->SetValue(app_config->get("preset_update") == "1");
	append(box_presets);

	box_slic3r->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &event) { this->version_check = event.IsChecked(); });
	box_presets->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &event) { this->preset_update = event.IsChecked(); });
}

PageVendors::PageVendors(ConfigWizard *parent) :
	ConfigWizardPage(parent, _(L("Other Vendors")), _(L("Other Vendors")))
{
	append_text(_(L("Pick another vendor supported by Slic3r PE:")));

	// const PresetBundle &bundle = wizard_p()->bundle_vendors;
	// const auto &vendors = wizard_p()->vendors;
	auto boldfont = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
	boldfont.SetWeight(wxFONTWEIGHT_BOLD);

	AppConfig &appconfig_vendors = this->wizard_p()->appconfig_vendors;
	wxArrayString choices_vendors;

	// for (const auto &vendor : vendors) {
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
	ConfigOptionEnum<GCodeFlavor> opt;

	auto sel = gcode_picker->GetSelection();
	if (sel != wxNOT_FOUND && opt.deserialize(gcode_picker->GetString(sel).ToStdString())) {
		config.set_key_value("gcode_flavor", &opt);
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

void ConfigWizard::priv::load_vendors()
{
	const auto vendor_dir = fs::path(Slic3r::data_dir()) / "vendor";
	const auto rsrc_vendor_dir = fs::path(resources_dir()) / "profiles";

	// Load vendors from the "vendors" directory in datadir
	for (fs::directory_iterator it(vendor_dir); it != fs::directory_iterator(); ++it) {
		if (it->path().extension() == ".ini") {
			auto vp = VendorProfile::from_ini(it->path());
			vendors[vp.id] = std::move(vp);
		}
	}

	// Additionally load up vendors from the application resources directory, but only those not seen in the datadir
	for (fs::directory_iterator it(rsrc_vendor_dir); it != fs::directory_iterator(); ++it) {
		if (it->path().extension() == ".ini") {
			const auto id = it->path().stem().string();
			if (vendors.find(id) == vendors.end()) {
				auto vp = VendorProfile::from_ini(it->path());
				vendors_rsrc[vp.id] = it->path();
				vendors[vp.id] = std::move(vp);
			}
		}
	}

	appconfig_vendors.set_vendors(*GUI::get_app_config());
}

void ConfigWizard::priv::index_refresh()
{
	index->load_items(page_welcome);
}

void ConfigWizard::priv::add_page(ConfigWizardPage *page)
{
	topsizer->Add(page, 0, wxEXPAND);

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

void ConfigWizard::priv::apply_config(AppConfig *app_config, PresetBundle *preset_bundle)
{
	const bool is_custom_setup = page_welcome->page_next() == page_firmware;

	if (! is_custom_setup) {
		const auto enabled_vendors = appconfig_vendors.vendors();
		for (const auto &vendor_rsrc : vendors_rsrc) {
			const auto vendor = enabled_vendors.find(vendor_rsrc.first);
			if (vendor == enabled_vendors.end()) { continue; }
	
			size_t size_sum = 0;
			for (const auto &model : vendor->second) { size_sum += model.second.size(); }
			if (size_sum == 0) { continue; }

			// This vendor needs to be installed
			PresetBundle::install_vendor_configbundle(vendor_rsrc.second);
		}

		app_config->set_vendors(appconfig_vendors);
		app_config->set("version_check", page_update->version_check ? "1" : "0");
		app_config->set("preset_update", page_update->preset_update ? "1" : "0");
		app_config->reset_selections();     // XXX: only on "fresh start"?
		preset_bundle->load_presets(*app_config);
	} else {
		for (ConfigWizardPage *page = page_firmware; page != nullptr; page = page->page_next()) {
			page->apply_custom_config(*custom_config);
		}
		preset_bundle->load_config("My Settings", *custom_config);
	}
}

// Public

ConfigWizard::ConfigWizard(wxWindow *parent) :
	wxDialog(parent, wxID_ANY, _(L("Configuration Assistant")), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	p(new priv(this))
{
	p->load_vendors();
	p->custom_config.reset(DynamicPrintConfig::new_from_defaults_keys({
		"gcode_flavor", "bed_shape", "nozzle_diameter", "filament_diameter", "temperature", "bed_temperature",
	}));

	p->index = new ConfigWizardIndex(this);

	auto *vsizer = new wxBoxSizer(wxVERTICAL);
	p->topsizer = new wxBoxSizer(wxHORIZONTAL);
	auto *hline = new wxStaticLine(this);
	p->btnsizer = new wxBoxSizer(wxHORIZONTAL);

	p->topsizer->Add(p->index, 0, wxEXPAND);
	p->topsizer->AddSpacer(INDEX_MARGIN);

	p->btn_prev = new wxButton(this, wxID_BACKWARD);
	p->btn_next = new wxButton(this, wxID_FORWARD);
	p->btn_finish = new wxButton(this, wxID_APPLY, _(L("&Finish")));
	p->btn_cancel = new wxButton(this, wxID_CANCEL);
	p->btnsizer->AddStretchSpacer();
	p->btnsizer->Add(p->btn_prev, 0, wxLEFT, BTN_SPACING);
	p->btnsizer->Add(p->btn_next, 0, wxLEFT, BTN_SPACING);
	p->btnsizer->Add(p->btn_finish, 0, wxLEFT, BTN_SPACING);
	p->btnsizer->Add(p->btn_cancel, 0, wxLEFT, BTN_SPACING);

	p->add_page(p->page_welcome  = new PageWelcome(this));
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

	vsizer->Add(p->topsizer, 1, wxEXPAND | wxALL, DIALOG_MARGIN);
	vsizer->Add(hline, 0, wxEXPAND);
	vsizer->Add(p->btnsizer, 0, wxEXPAND | wxALL, DIALOG_MARGIN);

	p->set_page(p->page_welcome);
	SetSizerAndFit(vsizer);
	SetMinSize(GetSize());

	p->btn_prev->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &evt) { this->p->go_prev(); });
	p->btn_next->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &evt) { this->p->go_next(); });
	p->btn_finish->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &evt) { this->EndModal(wxID_OK); });
}

ConfigWizard::~ConfigWizard() {}

bool ConfigWizard::run(wxWindow *parent, PresetBundle *preset_bundle)
{
	ConfigWizard wizard(parent);
	if (wizard.ShowModal() == wxID_OK) {
		wizard.p->apply_config(GUI::get_app_config(), preset_bundle);
		return true;
	} else {
		return false;
	}
}


}
}
