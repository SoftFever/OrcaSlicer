#ifndef slic3r_GUI_SendToSDcard_hpp_
#define slic3r_GUI_SendToSDcard_hpp_

#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/collpane.h>
#include <wx/dataview.h>
#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/dataview.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/hyperlink.h>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/popupwin.h>
#include <wx/spinctrl.h>
#include <wx/artprov.h>
#include <wx/wrapsizer.h>
#include <wx/srchctrl.h>

#include "SelectMachine.hpp"
#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include "DeviceManager.hpp"
#include "Plater.hpp"
#include "BBLStatusBar.hpp"
#include "BBLStatusBarSend.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/ScrolledWindow.hpp"
#include <wx/simplebook.h>
#include <wx/hashmap.h>

namespace Slic3r {
namespace GUI {

class SendToPrinterDialog : public DPIDialog
{
private:
	void init_model();
	void init_bind();
	void init_timer();

	int m_print_plate_idx;
	PrintDialogStatus m_print_status { PrintStatusInit };

	std::string m_printer_last_select;
	std::vector<wxString>               m_bedtype_list;
	std::map<std::string, ::CheckBox*> m_checkbox_list;

	wxColour m_colour_def_color{ wxColour(255, 255, 255) };
	wxColour m_colour_bold_color{ wxColour(38, 46, 48) };

protected:
	Plater* m_plater{ nullptr };
	wxPanel* m_line_top{ nullptr };
	wxPanel* m_panel_image{ nullptr };
	wxStaticText* m_stext_time{ nullptr };
	wxStaticText* m_stext_weight{ nullptr };
	wxPanel* m_line_materia{ nullptr };
	wxStaticText* m_stext_printer_title{ nullptr };

	wxStaticText* m_statictext_printer_msg{ nullptr };
	wxStaticBitmap* m_staticbitmap{ nullptr };
	ThumbnailPanel* m_thumbnailPanel{ nullptr };

	::ComboBox* m_comboBox_printer{ nullptr };
	::ComboBox* m_comboBox_bed{ nullptr };
	wxStaticText* m_staticText_bed_title{ nullptr };
	wxPanel* m_line_schedule{ nullptr };
	wxPanel* m_panel_sending{ nullptr };
	wxStaticText* m_stext_sending{ nullptr };
	wxPanel* m_panel_prepare{ nullptr };
	Button* m_button_refresh{ nullptr };
	Button* m_button_ensure{ nullptr };
	wxPanel* m_panel_finish{ nullptr };
	wxSimplebook* m_simplebook{ nullptr };
	wxStaticText* m_statictext_finish{ nullptr };

	StateColor btn_bg_enable;
	int        m_current_filament_id;
	bool       m_is_in_sending_mode{ false };

	wxBoxSizer* sizer_thumbnail;
	wxBoxSizer* m_sizer_main;
	wxBoxSizer* m_sizer_bottom;

	bool		enable_prepare_mode{true};
	bool        m_need_adaptation_screen{ false };
	wxPanel* m_scrollable_region;
	wxBoxSizer* m_sizer_scrollable_region;


	void stripWhiteSpace(std::string& str);
	wxString format_text(wxString& m_msg);
	void update_priner_status_msg(wxString msg, bool is_warning = false);
	void update_print_status_msg(wxString msg, bool is_warning = false, bool is_printer = true);

public:
	SendToPrinterDialog(Plater* plater = nullptr);
	~SendToPrinterDialog();

	void      prepare_mode();
	void      sending_mode();
	void      prepare(int print_plate_idx);
	bool	  Show(bool show);

	/* model */
	wxObjectDataPtr<MachineListModel> machine_model;
	std::shared_ptr<BBLStatusBarSend> m_status_bar;
	bool                              m_export_3mf_cancel{ false };
	bool                              m_is_canceled{ false };

protected:
	std::vector<MachineObject*> m_list;
	wxDataViewCtrl* m_dataViewListCtrl_machines{ nullptr };
	wxStaticText* m_staticText_left{ nullptr };
	wxHyperlinkCtrl* m_hyperlink_add_machine{ nullptr };
	wxGauge* m_gauge_job_progress{ nullptr };
	wxPanel* m_panel_status{ nullptr };
	wxButton* m_button_cancel{ nullptr };

	std::string                  m_print_info;
	int                          timeout_count = 0;
	bool                         is_timeout();
	void                         reset_timeout();
	void                         update_user_printer();
	void                         update_show_status();

	wxTimer* m_refresh_timer{ nullptr };

	std::shared_ptr<SendJob> m_send_job{nullptr};

	// Virtual event handlers, overide them in your derived class
	void                     update_printer_combobox(wxCommandEvent& event);
	void                     on_cancel(wxCloseEvent& event);
	void                     on_ok(wxCommandEvent& event);
	void                     on_refresh(wxCommandEvent& event);
	void                     on_print_job_cancel(wxCommandEvent& evt);
	void                     set_default();
	void                     on_timer(wxTimerEvent& event);
	void                     on_selection_changed(wxCommandEvent& event);
	void                     Enable_Refresh_Button(bool en);
	void				     show_status(PrintDialogStatus status, std::vector<wxString> params = std::vector<wxString>());
	void                     Enable_Send_Button(bool en);
	void                     on_dpi_changed(const wxRect& suggested_rect) override;
	void                     update_user_machine_list();
	std::vector<std::string> sort_string(std::vector<std::string> strArray);
};

}
}

#endif
