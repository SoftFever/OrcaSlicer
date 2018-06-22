#ifndef slic3r_ConfigWizard_private_hpp_
#define slic3r_ConfigWizard_private_hpp_

#include "ConfigWizard.hpp"

#include <vector>
#include <set>
#include <unordered_map>
#include <boost/filesystem.hpp>

#include <wx/sizer.h>
#include <wx/panel.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/spinctrl.h>

#include "libslic3r/PrintConfig.hpp"
#include "slic3r/Utils/PresetUpdater.hpp"
#include "AppConfig.hpp"
#include "Preset.hpp"
#include "BedShapeDialog.hpp"

namespace fs = boost::filesystem;

namespace Slic3r {
namespace GUI {

enum {
	WRAP_WIDTH = 500,

	DIALOG_MARGIN = 15,
	INDEX_MARGIN = 40,
	BTN_SPACING = 10,
	INDENT_SPACING = 30,
	VERTICAL_SPACING = 10,
};

struct PrinterPicker: wxPanel
{
	struct Checkbox : wxCheckBox
	{
		Checkbox(wxWindow *parent, const wxString &label, const std::string &model, const std::string &variant) :
			wxCheckBox(parent, wxID_ANY, label),
			model(model),
			variant(variant)
		{}

		std::string model;
		std::string variant;
	};

	const std::string vendor_id;
	std::vector<Checkbox*> cboxes;
	unsigned variants_checked;

	PrinterPicker(wxWindow *parent, const VendorProfile &vendor, const AppConfig &appconfig_vendors);

	void select_all(bool select);
	void select_one(size_t i, bool select);
	void on_checkbox(const Checkbox *cbox, bool checked);
};

struct ConfigWizardPage: wxPanel
{
	ConfigWizard *parent;
	const wxString shortname;
	wxBoxSizer *content;

	ConfigWizardPage(ConfigWizard *parent, wxString title, wxString shortname);

	virtual ~ConfigWizardPage();

	ConfigWizardPage* page_prev() const { return p_prev; }
	ConfigWizardPage* page_next() const { return p_next; }
	ConfigWizardPage* chain(ConfigWizardPage *page);

	template<class T>
	void append(T *thing, int proportion = 0, int flag = wxEXPAND|wxTOP|wxBOTTOM, int border = 10)
	{
		content->Add(thing, proportion, flag, border);
	}

	void append_text(wxString text);
	void append_spacer(int space);

	ConfigWizard::priv *wizard_p() const { return parent->p.get(); }

	virtual bool Show(bool show = true);
	virtual bool Hide() { return Show(false); }
	virtual wxPanel* extra_buttons() { return nullptr; }
	virtual void on_page_set() {}
	virtual void apply_custom_config(DynamicPrintConfig &config) {}

	void enable_next(bool enable);
private:
	ConfigWizardPage *p_prev;
	ConfigWizardPage *p_next;
};

struct PageWelcome: ConfigWizardPage
{
	PrinterPicker *printer_picker;
	wxPanel *others_buttons;
	wxCheckBox *cbox_reset;

	PageWelcome(ConfigWizard *parent);

	virtual wxPanel* extra_buttons() { return others_buttons; }
	virtual void on_page_set();

	bool reset_user_profile() const { return cbox_reset != nullptr ? cbox_reset->GetValue() : false; }
	void on_variant_checked();
};

struct PageUpdate: ConfigWizardPage
{
	bool version_check;
	bool preset_update;

	PageUpdate(ConfigWizard *parent);
};

struct PageVendors: ConfigWizardPage
{
	std::vector<PrinterPicker*> pickers;

	PageVendors(ConfigWizard *parent);

	virtual void on_page_set();

	void on_vendor_pick(size_t i);
	void on_variant_checked();
};

struct PageFirmware: ConfigWizardPage
{
	const ConfigOptionDef &gcode_opt;
	wxChoice *gcode_picker;

	PageFirmware(ConfigWizard *parent);
	virtual void apply_custom_config(DynamicPrintConfig &config);
};

struct PageBedShape: ConfigWizardPage
{
	BedShapePanel *shape_panel;

	PageBedShape(ConfigWizard *parent);
	virtual void apply_custom_config(DynamicPrintConfig &config);
};

struct PageDiameters: ConfigWizardPage
{
	wxSpinCtrlDouble *spin_nozzle;
	wxSpinCtrlDouble *spin_filam;

	PageDiameters(ConfigWizard *parent);
	virtual void apply_custom_config(DynamicPrintConfig &config);
};

struct PageTemperatures: ConfigWizardPage
{
	wxSpinCtrlDouble *spin_extr;
	wxSpinCtrlDouble *spin_bed;

	PageTemperatures(ConfigWizard *parent);
	virtual void apply_custom_config(DynamicPrintConfig &config);
};


class ConfigWizardIndex: public wxPanel
{
public:
	ConfigWizardIndex(wxWindow *parent);

	void load_items(ConfigWizardPage *firstpage);
	void set_active(ConfigWizardPage *page);
private:
	const wxBitmap bg;
	const wxBitmap bullet_black;
	const wxBitmap bullet_blue;
	const wxBitmap bullet_white;
	int text_height;

	std::vector<wxString> items;
	std::vector<wxString>::const_iterator item_active;

	void on_paint(wxPaintEvent &evt);
};

struct ConfigWizard::priv
{
	ConfigWizard *q;
	ConfigWizard::RunReason run_reason;
	AppConfig appconfig_vendors;
	std::unordered_map<std::string, VendorProfile> vendors;
	std::unordered_map<std::string, std::string> vendors_rsrc;
	std::unique_ptr<DynamicPrintConfig> custom_config;

	wxScrolledWindow *hscroll = nullptr;
	wxBoxSizer *hscroll_sizer = nullptr;
	wxBoxSizer *btnsizer = nullptr;
	ConfigWizardPage *page_current = nullptr;
	ConfigWizardIndex *index = nullptr;
	wxButton *btn_prev = nullptr;
	wxButton *btn_next = nullptr;
	wxButton *btn_finish = nullptr;
	wxButton *btn_cancel = nullptr;

	PageWelcome      *page_welcome = nullptr;
	PageUpdate       *page_update = nullptr;
	PageVendors      *page_vendors = nullptr;
	PageFirmware     *page_firmware = nullptr;
	PageBedShape     *page_bed = nullptr;
	PageDiameters    *page_diams = nullptr;
	PageTemperatures *page_temps = nullptr;

	priv(ConfigWizard *q) : q(q) {}

	void load_vendors();
	void add_page(ConfigWizardPage *page);
	void index_refresh();
	void set_page(ConfigWizardPage *page);
	void layout_fit();
	void go_prev() { if (page_current != nullptr) { set_page(page_current->page_prev()); } }
	void go_next() { if (page_current != nullptr) { set_page(page_current->page_next()); } }
	void enable_next(bool enable);

	void on_other_vendors();
	void on_custom_setup();

	void apply_config(AppConfig *app_config, PresetBundle *preset_bundle, const PresetUpdater *updater);
};



}
}

#endif
