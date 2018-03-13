#ifndef slic3r_ConfigWizard_private_hpp_
#define slic3r_ConfigWizard_private_hpp_

#include "ConfigWizard.hpp"

#include <vector>

#include <wx/sizer.h>
#include <wx/panel.h>
#include <wx/button.h>


namespace Slic3r {
namespace GUI {


enum {
	DIALOG_MARGIN = 15,
	INDEX_MARGIN = 40,
	BTN_SPACING = 10,
};

struct ConfigWizardPage: wxPanel
{
	enum {
		CONTENT_WIDTH = 500,
	};

	ConfigWizard *parent;
	const wxString shortname;
	wxBoxSizer *content;

	ConfigWizardPage(ConfigWizard *parent, wxString title, wxString shortname);

	virtual ~ConfigWizardPage();

	ConfigWizardPage *page_prev() const { return p_prev; }
	ConfigWizardPage *page_next() const { return p_next; }
	ConfigWizardPage* chain(ConfigWizardPage *page);

	void append_text(wxString text);
	void append_widget(wxWindow *widget, int proportion = 0);
	void append_spacer(int space);
	
	ConfigWizard::priv *wizard_p() const { return parent->p.get(); }

	virtual bool Show(bool show = true);
	virtual bool Hide() { return Show(false); }
	virtual wxPanel* extra_buttons() { return nullptr; }
	virtual void on_page_set() {}

	void enable_next(bool enable);
private:
	ConfigWizardPage *p_prev;
	ConfigWizardPage *p_next;
};

struct PageWelcome: ConfigWizardPage
{
	wxPanel *others_buttons;
	unsigned variants_checked;

	PageWelcome(ConfigWizard *parent, const PresetBundle &bundle);

	virtual wxPanel* extra_buttons() { return others_buttons; }
	virtual void on_page_set();

	void on_variant_checked();
};

struct PageUpdate: ConfigWizardPage
{
	PageUpdate(ConfigWizard *parent);

	void presets_update_enable(bool enable);
};

struct PageVendors: ConfigWizardPage
{
	PageVendors(ConfigWizard *parent);
};

struct PageFirmware: ConfigWizardPage
{
	PageFirmware(ConfigWizard *parent);
};

struct PageBedShape: ConfigWizardPage
{
	PageBedShape(ConfigWizard *parent);
};

struct PageDiameters: ConfigWizardPage
{
	PageDiameters(ConfigWizard *parent);
};

struct PageTemperatures: ConfigWizardPage
{
	PageTemperatures(ConfigWizard *parent);
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

	void on_paint(wxPaintEvent & evt);
};

struct ConfigWizard::priv
{
	ConfigWizard *q;
	wxBoxSizer *topsizer = nullptr;
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

	void add_page(ConfigWizardPage *page);
	void index_refresh();
	void set_page(ConfigWizardPage *page);
	void go_prev() { if (page_current != nullptr) { set_page(page_current->page_prev()); } }
	void go_next() { if (page_current != nullptr) { set_page(page_current->page_next()); } }
	void enable_next(bool enable);

	void on_other_vendors();
	void on_custom_setup();
};



}
}

#endif
