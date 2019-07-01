#ifndef slic3r_Tab_hpp_
#define slic3r_Tab_hpp_

//	 The "Expert" tab at the right of the main tabbed window.
//	
//	 This file implements following packages:
//	   Slic3r::GUI::Tab;
//	       Slic3r::GUI::Tab::Print;
//	       Slic3r::GUI::Tab::Filament;
//	       Slic3r::GUI::Tab::Printer;
//	   Slic3r::GUI::Tab::Page
//	       - Option page: For example, the Slic3r::GUI::Tab::Print has option pages "Layers and perimeters", "Infill", "Skirt and brim" ...
//	   Slic3r::GUI::SavePresetWindow
//	       - Dialog to select a new preset name to store the configuration.
//	   Slic3r::GUI::Tab::Preset;
//	       - Single preset item: name, file is default or external.

#include <wx/panel.h>
#include <wx/notebook.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/bmpcbox.h>
#include <wx/bmpbuttn.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>

#include <map>
#include <vector>
#include <memory>

#include "BedShapeDialog.hpp"
#include "ButtonsDescription.hpp"
#include "Event.hpp"
#include "wxExtensions.hpp"

namespace Slic3r {
namespace GUI {


// Single Tab page containing a{ vsizer } of{ optgroups }
// package Slic3r::GUI::Tab::Page;
using ConfigOptionsGroupShp = std::shared_ptr<ConfigOptionsGroup>;
class Page : public wxScrolledWindow
{
	wxWindow*		m_parent;
	wxString		m_title;
	size_t			m_iconID;
	wxBoxSizer*		m_vsizer;
    bool            m_show = true;
public:
    Page(wxWindow* parent, const wxString title, const int iconID, const std::vector<ScalableBitmap>& mode_bmp_cache) :
			m_parent(parent),
			m_title(title),
			m_iconID(iconID),
            m_mode_bitmap_cache(mode_bmp_cache)
	{
		Create(m_parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
		m_vsizer = new wxBoxSizer(wxVERTICAL);
        m_item_color = &wxGetApp().get_label_clr_default();
		SetSizer(m_vsizer);
	}
	~Page() {}

	bool				m_is_modified_values{ false };
	bool				m_is_nonsys_values{ true };

    // Delayed layout after resizing the main window.
    bool 				layout_valid = false;
    const std::vector<ScalableBitmap>&   m_mode_bitmap_cache;

public:
	std::vector <ConfigOptionsGroupShp> m_optgroups;
	DynamicPrintConfig* m_config;

	wxBoxSizer*	vsizer() const { return m_vsizer; }
	wxWindow*	parent() const { return m_parent; }
	wxString	title()	 const { return m_title; }
	size_t		iconID() const { return m_iconID; }
	void		set_config(DynamicPrintConfig* config_in) { m_config = config_in; }
	void		reload_config();
    void        update_visibility(ConfigOptionMode mode);
    void        msw_rescale();
	Field*		get_field(const t_config_option_key& opt_key, int opt_index = -1) const;
	bool		set_value(const t_config_option_key& opt_key, const boost::any& value);
	ConfigOptionsGroupShp	new_optgroup(const wxString& title, int noncommon_label_width = -1);

	bool		set_item_colour(const wxColour *clr) {
		if (m_item_color != clr) {
			m_item_color = clr;
			return true;
		}
		return false;
	}

	const wxColour	get_item_colour() {
			return *m_item_color;
	}
    bool get_show() const { return m_show; }

protected:
	// Color of TreeCtrlItem. The wxColour will be updated only if the new wxColour pointer differs from the currently rendered one.
	const wxColour*		m_item_color;
};


wxDECLARE_EVENT(EVT_TAB_VALUE_CHANGED, wxCommandEvent);
wxDECLARE_EVENT(EVT_TAB_PRESETS_CHANGED, SimpleEvent);


using PageShp = std::shared_ptr<Page>;
class Tab: public wxPanel
{
	wxNotebook*			m_parent;
#ifdef __WXOSX__
	wxPanel*			m_tmp_panel;
	int					m_size_move = -1;
#endif // __WXOSX__
protected:
    Preset::Type        m_type;
	std::string			m_name;
	const wxString		m_title;
	wxBitmapComboBox*	m_presets_choice;
	ScalableButton*		m_btn_save_preset;
	ScalableButton*		m_btn_delete_preset;
	ScalableButton*		m_btn_hide_incompatible_presets;
	wxBoxSizer*			m_hsizer;
	wxBoxSizer*			m_left_sizer;
	wxTreeCtrl*			m_treectrl;
	wxImageList*		m_icons;

    ModeSizer*     m_mode_sizer;

   	struct PresetDependencies {
		Preset::Type type	  = Preset::TYPE_INVALID;
		wxCheckBox 	*checkbox = nullptr;
		ScalableButton 	*btn  = nullptr;
		std::string  key_list; // "compatible_printers"
		std::string  key_condition;
		std::string  dialog_title;
		std::string  dialog_label;
	};
	PresetDependencies 	m_compatible_printers;
	PresetDependencies 	m_compatible_prints;

    /* Indicates, that default preset or preset inherited from default is selected
     * This value is used for a options color updating 
     * (use green color only for options, which values are equal to system values)
     */
    bool                    m_is_default_preset {false};

	ScalableButton*			m_undo_btn;
	ScalableButton*			m_undo_to_sys_btn;
	ScalableButton*			m_question_btn;

	// Cached bitmaps.
	// A "flag" icon to be displayned next to the preset name in the Tab's combo box.
	ScalableBitmap			m_bmp_show_incompatible_presets;
	ScalableBitmap			m_bmp_hide_incompatible_presets;
	// Bitmaps to be shown on the "Revert to system" aka "Lock to system" button next to each input field.
	ScalableBitmap 			m_bmp_value_lock;
	ScalableBitmap 			m_bmp_value_unlock;
	ScalableBitmap 			m_bmp_white_bullet;
	// The following bitmap points to either m_bmp_value_unlock or m_bmp_white_bullet, depending on whether the current preset has a parent preset.
	ScalableBitmap 		   *m_bmp_non_system;
	// Bitmaps to be shown on the "Undo user changes" button next to each input field.
	ScalableBitmap 			m_bmp_value_revert;
    
    std::vector<ScalableButton*>	m_scaled_buttons = {};    
    std::vector<ScalableBitmap*>	m_scaled_bitmaps = {};    
    std::vector<ScalableBitmap>     m_scaled_icons_list = {};
    std::vector<ScalableBitmap>     m_mode_bitmap_cache = {};

	// Colors for ui "decoration"
	wxColour			m_sys_label_clr;
	wxColour			m_modified_label_clr;
	wxColour			m_default_text_clr;

	// Tooltip text for reset buttons (for whole options group)
	wxString			m_ttg_value_lock;
	wxString			m_ttg_value_unlock;
	wxString			m_ttg_white_bullet_ns;
	// The following text points to either m_ttg_value_unlock or m_ttg_white_bullet_ns, depending on whether the current preset has a parent preset.
	wxString			*m_ttg_non_system;
	// Tooltip text to be shown on the "Undo user changes" button next to each input field.
	wxString			m_ttg_white_bullet;
	wxString			m_ttg_value_revert;

	// Tooltip text for reset buttons (for each option in group)
	wxString			m_tt_value_lock;
	wxString			m_tt_value_unlock;
	// The following text points to either m_tt_value_unlock or m_ttg_white_bullet_ns, depending on whether the current preset has a parent preset.
	wxString			*m_tt_non_system;
	// Tooltip text to be shown on the "Undo user changes" button next to each input field.
	wxString			m_tt_white_bullet;
	wxString			m_tt_value_revert;

	int					m_icon_count;
	std::map<std::string, size_t>	m_icon_index;		// Map from an icon file name to its index
	std::vector<PageShp>			m_pages;
	bool				m_disable_tree_sel_changed_event;
	bool				m_show_incompatible_presets;

    std::vector<Preset::Type>	m_dependent_tabs = {};
	enum OptStatus { osSystemValue = 1, osInitValue = 2 };
	std::map<std::string, int>	m_options_list;
	int							m_opt_status_value = 0;

	std::vector<ButtonsDescription::Entry>	m_icon_descriptions = {};

	bool				m_is_modified_values{ false };
	bool				m_is_nonsys_values{ true };
	bool				m_postpone_update_ui {false};

	size_t				m_selected_preset_item{ 0 };

    void                set_type();

    int                 m_em_unit;
    // To avoid actions with no-completed Tab
    bool                m_complited { false };
    ConfigOptionMode    m_mode = comSimple;

public:
	PresetBundle*		m_preset_bundle;
	bool				m_show_btn_incompatible_presets = false;
	PresetCollection*	m_presets;
	DynamicPrintConfig*	m_config;
	ogStaticText*		m_parent_preset_description_line;
	wxStaticText*		m_colored_Label = nullptr;
    // Counter for the updating (because of an update() function can have a recursive behavior):
    // 1. increase value from the very beginning of an update() function
    // 2. decrease value at the end of an update() function
    // 3. propagate changed configuration to the Platter when (m_update_cnt == 0) only
    int                 m_update_cnt = 0;

public:
// 	Tab(wxNotebook* parent, const wxString& title, const char* name); 
    Tab(wxNotebook* parent, const wxString& title, Preset::Type type);
    ~Tab() {}

	wxWindow*	parent() const { return m_parent; }
	wxString	title()	 const { return m_title; }
// 	std::string	name()	 const { return m_name; }
	std::string	name()	 const { return m_presets->name(); }
    Preset::Type type()  const { return m_type; }
    bool complited()     const { return m_complited; }
    virtual bool supports_printer_technology(const PrinterTechnology tech) = 0;

	void		create_preset_tab();
    void        add_scaled_button(wxWindow* parent, ScalableButton** btn, const std::string& icon_name, 
                                  const wxString& label = wxEmptyString, 
                                  long style = wxBU_EXACTFIT | wxNO_BORDER);
    void        add_scaled_bitmap(wxWindow* parent, ScalableBitmap& btn, const std::string& icon_name);
    void		load_current_preset();
	void        rebuild_page_tree();
	void        update_page_tree_visibility();
	// Select a new preset, possibly delete the current one.
	void		select_preset(std::string preset_name = "", bool delete_current = false);
	bool		may_discard_current_dirty_preset(PresetCollection* presets = nullptr, const std::string& new_printer_name = "");
    bool        may_switch_to_SLA_preset();

	void		OnTreeSelChange(wxTreeEvent& event);
	void		OnKeyDown(wxKeyEvent& event);

	void		save_preset(std::string name = "");
	void		delete_preset();
	void		toggle_show_hide_incompatible();
	void		update_show_hide_incompatible_button();
	void		update_ui_from_settings();
	void		update_labels_colour();
	void		update_changed_ui();
	void		get_sys_and_mod_flags(const std::string& opt_key, bool& sys_page, bool& modified_page);
	void		update_changed_tree_ui();
	void		update_undo_buttons();

	void		on_roll_back_value(const bool to_sys = false);

	PageShp		add_options_page(const wxString& title, const std::string& icon, bool is_extruder_pages = false);

	virtual void	OnActivate();
	virtual void	on_preset_loaded() {}
	virtual void	build() = 0;
	virtual void	update() = 0;
	virtual void	init_options_list();
	void			load_initial_data();
	void			update_dirty();
	void			update_tab_ui();
	void			load_config(const DynamicPrintConfig& config);
	virtual void	reload_config();
    void            update_mode();
    void            update_visibility();
    void            msw_rescale();
	Field*			get_field(const t_config_option_key& opt_key, int opt_index = -1) const;
	bool			set_value(const t_config_option_key& opt_key, const boost::any& value);
	wxSizer*		description_line_widget(wxWindow* parent, ogStaticText** StaticText);
	bool			current_preset_is_dirty();

	DynamicPrintConfig*	get_config() { return m_config; }
	PresetCollection*	get_presets() { return m_presets; }
	size_t				get_selected_preset_item() { return m_selected_preset_item; }

	void			on_value_change(const std::string& opt_key, const boost::any& value);

    void            update_wiping_button_visibility();

protected:
	wxSizer*		compatible_widget_create(wxWindow* parent, PresetDependencies &deps);
	void 			compatible_widget_reload(PresetDependencies &deps);
	void			load_key_value(const std::string& opt_key, const boost::any& value, bool saved_value = false);

	void			on_presets_changed();
	void			update_preset_description_line();
	void			update_frequently_changed_parameters();
	void			fill_icon_descriptions();
	void			set_tooltips_text();
};

class TabPrint : public Tab
{
    bool is_msg_dlg_already_exist {false};
public:
	TabPrint(wxNotebook* parent) : 
// 		Tab(parent, _(L("Print Settings")), L("print")) {}
        Tab(parent, _(L("Print Settings")), Slic3r::Preset::TYPE_PRINT) {}
	~TabPrint() {}

	ogStaticText*	m_recommended_thin_wall_thickness_description_line;
	bool		m_support_material_overhangs_queried = false;

	void		build() override;
	void		reload_config() override;
	void		update() override;
	void		OnActivate() override;
    bool 		supports_printer_technology(const PrinterTechnology tech) override { return tech == ptFFF; }
};
class TabFilament : public Tab
{
	ogStaticText*	m_volumetric_speed_description_line;
	ogStaticText*	m_cooling_description_line;
public:
	TabFilament(wxNotebook* parent) : 
// 		Tab(parent, _(L("Filament Settings")), L("filament")) {}
		Tab(parent, _(L("Filament Settings")), Slic3r::Preset::TYPE_FILAMENT) {}
	~TabFilament() {}

	void		build() override;
	void		reload_config() override;
	void		update() override;
	void		OnActivate() override;
    bool 		supports_printer_technology(const PrinterTechnology tech) override { return tech == ptFFF; }
};

class TabPrinter : public Tab
{
	bool		m_has_single_extruder_MM_page = false;
	bool		m_use_silent_mode = false;
	void		append_option_line(ConfigOptionsGroupShp optgroup, const std::string opt_key);
	bool		m_rebuild_kinematics_page = false;

    std::vector<PageShp>			m_pages_fff;
    std::vector<PageShp>			m_pages_sla;

    void build_printhost(ConfigOptionsGroup *optgroup);
public:
	wxButton*	m_serial_test_btn = nullptr;
	ScalableButton*	m_print_host_test_btn = nullptr;
	ScalableButton*	m_printhost_browse_btn = nullptr;

	size_t		m_extruders_count;
	size_t		m_extruders_count_old = 0;
	size_t		m_initial_extruders_count;
	size_t		m_sys_extruders_count;

    PrinterTechnology               m_printer_technology = ptFFF;

// 	TabPrinter(wxNotebook* parent) : Tab(parent, _(L("Printer Settings")), L("printer")) {}
    TabPrinter(wxNotebook* parent) : 
        Tab(parent, _(L("Printer Settings")), Slic3r::Preset::TYPE_PRINTER) {}
	~TabPrinter() {}

	void		build() override;
    void		build_fff();
    void		build_sla();
    void		update() override;
    void		update_fff();
    void		update_sla();
    void        update_pages(); // update m_pages according to printer technology
	void		update_serial_ports();
	void		extruders_count_changed(size_t extruders_count);
	PageShp		build_kinematics_page();
	void		build_unregular_pages();
	void		on_preset_loaded() override;
	void		init_options_list() override;
    bool 		supports_printer_technology(const PrinterTechnology /* tech */) override { return true; }
};

class TabSLAMaterial : public Tab
{
public:
    TabSLAMaterial(wxNotebook* parent) :
// 		Tab(parent, _(L("Material Settings")), L("sla_material")) {}
		Tab(parent, _(L("Material Settings")), Slic3r::Preset::TYPE_SLA_MATERIAL) {}
    ~TabSLAMaterial() {}

	void		build() override;
	void		reload_config() override;
	void		update() override;
    void		init_options_list() override;
    bool 		supports_printer_technology(const PrinterTechnology tech) override { return tech == ptSLA; }
};

class TabSLAPrint : public Tab
{
public:
    TabSLAPrint(wxNotebook* parent) :
//         Tab(parent, _(L("Print Settings")), L("sla_print")) {}
        Tab(parent, _(L("Print Settings")), Slic3r::Preset::TYPE_SLA_PRINT) {}
    ~TabSLAPrint() {}
    void		build() override;
	void		reload_config() override;
    void		update() override;
//     void		init_options_list() override;
    bool 		supports_printer_technology(const PrinterTechnology tech) override { return tech == ptSLA; }
};

class SavePresetWindow :public wxDialog
{
public:
	SavePresetWindow(wxWindow* parent) :wxDialog(parent, wxID_ANY, _(L("Save preset"))) {}
	~SavePresetWindow() {}

	std::string		m_chosen_name;
	wxComboBox*		m_combo;

	void			build(const wxString& title, const std::string& default_name, std::vector<std::string> &values);
	void			accept();
	std::string		get_name() { return m_chosen_name; }
};

} // GUI
} // Slic3r

#endif /* slic3r_Tab_hpp_ */
