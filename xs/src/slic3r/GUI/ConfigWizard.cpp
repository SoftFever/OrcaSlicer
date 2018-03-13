#include "ConfigWizard_private.hpp"

#include <iostream>   // XXX
#include <algorithm>
#include <utility>
#include <boost/filesystem.hpp>

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

namespace fs = boost::filesystem;

namespace Slic3r {
namespace GUI {


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
	// content->Add(widget, 1, wxALIGN_LEFT | wxTOP | wxBOTTOM, 10);
	content->Add(widget, 0, wxALIGN_LEFT | wxTOP | wxBOTTOM, 10);
}

void ConfigWizardPage::append_widget(wxWindow *widget, int proportion)
{
	content->Add(widget, proportion, wxEXPAND | wxTOP | wxBOTTOM, 10);
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

PageWelcome::PageWelcome(ConfigWizard *parent, const PresetBundle &bundle) :
	ConfigWizardPage(parent, _(L("Welcome to the Slic3r Configuration assistant")), _(L("Welcome"))),
	others_buttons(new wxPanel(parent)),
	variants_checked(0)
{
	append_text(_(L("Hello, welcome to Slic3r Prusa Edition! TODO: This text.")));

	const auto &vendors = bundle.vendors;
	const auto vendor_prusa = std::find(vendors.cbegin(), vendors.cend(), VendorProfile("PrusaResearch"));

	// TODO: preload checkiness from app config

	if (vendor_prusa != vendors.cend()) {
		const auto &models = vendor_prusa->models;

		auto *printer_picker = new wxPanel(this);
		auto *printer_grid = new wxFlexGridSizer(models.size(), 0, 20);
		printer_grid->SetFlexibleDirection(wxVERTICAL);
		printer_picker->SetSizer(printer_grid);

		auto namefont = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
		namefont.SetWeight(wxFONTWEIGHT_BOLD);

		for (auto model = models.cbegin(); model != models.cend(); ++model) {
			auto *panel = new wxPanel(printer_picker);
			auto *sizer = new wxBoxSizer(wxVERTICAL);
			panel->SetSizer(sizer);

			auto *title = new wxStaticText(panel, wxID_ANY, model->name, wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
			title->SetFont(namefont);
			sizer->Add(title, 0, wxBOTTOM, 3);

			auto bitmap_file = wxString::Format("printers/%s.png", model->id);
			wxBitmap bitmap(GUI::from_u8(Slic3r::var(bitmap_file.ToStdString())), wxBITMAP_TYPE_PNG);
			auto *bitmap_widget = new wxStaticBitmap(panel, wxID_ANY, bitmap);
			sizer->Add(bitmap_widget, 0, wxBOTTOM, 3);

			sizer->AddSpacer(20);

			for (const auto &variant : model->variants) {
				auto *cbox = new wxCheckBox(panel, wxID_ANY, wxString::Format("%s %s %s", variant.name, _(L("mm")), _(L("nozzle"))));
				sizer->Add(cbox, 0, wxBOTTOM, 3);
				cbox->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &event) {
					this->variants_checked += event.IsChecked() ? 1 : -1;
					this->on_variant_checked();
				});
			}

			printer_grid->Add(panel);
		}

		append_widget(printer_picker);
	}

	{
		auto *sizer = new wxBoxSizer(wxHORIZONTAL);
		auto *other_vendors = new wxButton(others_buttons, wxID_ANY, _(L("Other vendors")));
		auto *custom_setup = new wxButton(others_buttons, wxID_ANY, _(L("Custom setup")));

		sizer->Add(other_vendors);
		sizer->AddSpacer(BTN_SPACING);
		sizer->Add(custom_setup);

		other_vendors->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &event) { this->wizard_p()->on_other_vendors(); });
		custom_setup->Bind(wxEVT_BUTTON, [this](const wxCommandEvent &event) { this->wizard_p()->on_custom_setup(); });

		others_buttons->SetSizer(sizer);
	}
}

void PageWelcome::on_page_set()
{
	chain(wizard_p()->page_update);
	on_variant_checked();
}

void PageWelcome::on_variant_checked()
{
	enable_next(variants_checked > 0);
}

PageUpdate::PageUpdate(ConfigWizard *parent) :
	ConfigWizardPage(parent, _(L("Automatic updates")), _(L("Updates")))
{
	append_text(_(L("TODO: text")));
	auto *box_slic3r = new wxCheckBox(this, wxID_ANY, _(L("Check for Slic3r updates")));
	box_slic3r->SetValue(true);
	append_widget(box_slic3r);
	
	append_text(_(L("TODO: text")));
	auto *box_presets = new wxCheckBox(this, wxID_ANY, _(L("Update built-in Presets automatically")));
	box_presets->SetValue(true);
	append_widget(box_presets);
}

void PageUpdate::presets_update_enable(bool enable)
{
	// TODO
}

PageVendors::PageVendors(ConfigWizard *parent) :
	ConfigWizardPage(parent, _(L("Other Vendors")), _(L("Other Vendors")))
{}

PageFirmware::PageFirmware(ConfigWizard *parent) :
	ConfigWizardPage(parent, _(L("Firmware Type")), _(L("Firmware")))
{}

PageBedShape::PageBedShape(ConfigWizard *parent) :
	ConfigWizardPage(parent, _(L("Bed Shape and Size")), _(L("Bed Shape")))
{}

PageDiameters::PageDiameters(ConfigWizard *parent) :
	ConfigWizardPage(parent, _(L("Filament and Nozzle Diameter")), _(L("Print Diameters")))
{}

PageTemperatures::PageTemperatures(ConfigWizard *parent) :
	ConfigWizardPage(parent, _(L("Bed and Extruder Temperature")), _(L("Temperatures")))
{}


// Index

ConfigWizardIndex::ConfigWizardIndex(wxWindow *parent) :
	wxPanel(parent),
	bg(GUI::from_u8(Slic3r::var("Slic3r_192px_transparent.png")), wxBITMAP_TYPE_PNG),
	bullet_black(GUI::from_u8(Slic3r::var("bullet_black.png")), wxBITMAP_TYPE_PNG),
	bullet_blue(GUI::from_u8(Slic3r::var("bullet_blue.png")), wxBITMAP_TYPE_PNG),
	bullet_white(GUI::from_u8(Slic3r::var("bullet_white.png")), wxBITMAP_TYPE_PNG)
{
	SetMinSize(bg.GetSize());
	Bind(wxEVT_PAINT, &ConfigWizardIndex::on_paint, this);

	wxClientDC dc(this);
	text_height = dc.GetCharHeight();
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
	const auto h = size.GetHeight();
	const auto w = size.GetWidth();
	if (h == 0 || w == 0) { return; }

	wxPaintDC dc(this);
	dc.DrawBitmap(bg, 0, h - bg.GetHeight(), false);

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

	q->Layout();
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

// Public

ConfigWizard::ConfigWizard(wxWindow *parent, const PresetBundle &bundle) :
	wxDialog(parent, wxID_ANY, _(L("Configuration Assistant")), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
	p(new priv(this))
{
	p->index = new ConfigWizardIndex(this);

	auto *vsizer = new wxBoxSizer(wxVERTICAL);
	p->topsizer = new wxBoxSizer(wxHORIZONTAL);
	auto *hline = new wxStaticLine(this);
	p->btnsizer = new wxBoxSizer(wxHORIZONTAL);

	p->topsizer->Add(p->index, 0, wxEXPAND);
	p->topsizer->AddSpacer(INDEX_MARGIN);

	// TODO: btn labels vs default w/ icons ... use arrows from resources? (no apply icon)
	// Also: http://docs.wxwidgets.org/3.0/page_stockitems.html
	p->btn_prev = new wxButton(this, wxID_BACKWARD, _(L("< &Back")));
	p->btn_next = new wxButton(this, wxID_FORWARD, _(L("&Next >")));
	p->btn_finish = new wxButton(this, wxID_APPLY, _(L("&Finish")));
	p->btn_cancel = new wxButton(this, wxID_CANCEL);
	p->btnsizer->AddStretchSpacer();
	p->btnsizer->Add(p->btn_prev, 0, wxLEFT, BTN_SPACING);
	p->btnsizer->Add(p->btn_next, 0, wxLEFT, BTN_SPACING);
	p->btnsizer->Add(p->btn_finish, 0, wxLEFT, BTN_SPACING);
	p->btnsizer->Add(p->btn_cancel, 0, wxLEFT, BTN_SPACING);

	p->add_page(p->page_welcome  = new PageWelcome(this, bundle));
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
}

ConfigWizard::~ConfigWizard() {}

void ConfigWizard::run(wxWindow *parent)
{
	PresetBundle bundle;

	const auto profiles_dir = fs::path(resources_dir()) / "profiles";
	for (fs::directory_iterator it(profiles_dir); it != fs::directory_iterator(); ++it) {
		if (it->path().extension() == ".ini") {
			bundle.load_configbundle(it->path().native(), PresetBundle::LOAD_CFGBUNDLE_VENDOR_ONLY);
		}
	}

	// XXX
	for (const auto &vendor : bundle.vendors) {
		std::cerr << "vendor: " << vendor.name << std::endl;
		std::cerr << "  URL: " << vendor.config_update_url << std::endl;
		for (const auto &model : vendor.models) {
			std::cerr << "\tmodel: " << model.id << " (" << model.name << ")" << std::endl;
			for (const auto &variant : model.variants) {
				std::cerr << "\t\tvariant: " << variant.name << std::endl;
			}
		}
	}

	ConfigWizard wizard(parent, bundle);
	wizard.ShowModal();
}


}
}
