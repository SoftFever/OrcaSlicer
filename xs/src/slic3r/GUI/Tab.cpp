#include <wx/app.h>
#include <wx/button.h>
#include <wx/frame.h>
#include <wx/menu.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/sizer.h>

#include <wx/bmpcbox.h>
#include <wx/bmpbuttn.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>

#include "Tab.h"

namespace Slic3r {
namespace GUI {

// Declare some IDs. 
/*const int BUTTON1 = 100;

// Attach the event handlers. Put this after Slis3rFrame declaration.
BEGIN_EVENT_TABLE(MyFrame, wxFrame)
EVT_BUTTON(BUTTON1, MyFrame::OnButton1)
END_EVENT_TABLE()
*/

// sub new
void CTab::create_preset_tab()
{
	// Vertical sizer to hold the choice menu and the rest of the page.
	CTab *panel = this;
	auto  *sizer = new wxBoxSizer(wxVERTICAL);
	sizer->SetSizeHints(panel);
	(panel)->SetSizer(sizer);
	panel->SetSizer(sizer);

	// preset chooser
	// choice menu for Experiments
	wxString choices[] =
	{
		_T("Washington"),
		_T("Adams"),
		_T("Jefferson"),
		_T("Madison"),
		_T("Lincoln"),
		_T("One"),
		_T("Two"),
		_T("Three"),
		_T("Four")
	};
	int nCntEl = 9;

	presets_choice = new wxBitmapComboBox(panel, wxID_ANY, "", wxDefaultPosition, wxSize(270, -1), nCntEl, choices, wxCB_READONLY);

	//buttons
	wxBitmap bmpMenu;
	bmpMenu = wxBitmap(wxT("var\\disk.png"), wxBITMAP_TYPE_PNG);
	auto *btn_save_preset = new wxBitmapButton(panel, wxID_ANY, bmpMenu, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
	bmpMenu = wxBitmap(wxT("var\\delete.png"), wxBITMAP_TYPE_PNG);
	auto *btn_delete_preset = new wxBitmapButton(panel, wxID_ANY, bmpMenu, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);

	//	$self->{show_incompatible_presets} = 0;			// !!!

	bmp_show_incompatible_presets = new wxBitmap(wxT("var\\flag-red-icon.png"), wxBITMAP_TYPE_PNG);
	bmp_hide_incompatible_presets = new wxBitmap(wxT("var\\flag-green-icon.png"), wxBITMAP_TYPE_PNG);
	btn_hide_incompatible_presets = new wxBitmapButton(panel, wxID_ANY, *bmp_hide_incompatible_presets, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);

	wxString stTitle = _T("Save current ") + wxString(_title);//. lc($self->title) 
	btn_save_preset->SetToolTip(stTitle);
	btn_delete_preset->SetToolTip(_T("Delete this preset"));
	btn_delete_preset->Disable();

	hsizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(hsizer, 0, wxBOTTOM, 3);
	hsizer->Add(presets_choice, 1, wxLEFT | wxRIGHT | wxTOP | wxALIGN_CENTER_VERTICAL, 3);
	hsizer->AddSpacer(4);
	hsizer->Add(btn_save_preset, 0, wxALIGN_CENTER_VERTICAL);
	hsizer->AddSpacer(4);
	hsizer->Add(btn_delete_preset, 0, wxALIGN_CENTER_VERTICAL);
	hsizer->AddSpacer(16);
	hsizer->Add(btn_hide_incompatible_presets, 0, wxALIGN_CENTER_VERTICAL);

	//Horizontal sizer to hold the tree and the selected page.
	hsizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(hsizer, 1, wxEXPAND, 0);

	//left vertical sizer
	left_sizer = new wxBoxSizer(wxVERTICAL);
	hsizer->Add(left_sizer, 0, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, 3);

	// tree
	auto *treectrl = new wxTreeCtrl(panel, wxID_ANY, wxDefaultPosition, wxSize(185, -1), wxTR_NO_BUTTONS | wxTR_HIDE_ROOT | wxTR_SINGLE | wxTR_NO_LINES | wxBORDER_SUNKEN | wxWANTS_CHARS);
	left_sizer->Add(treectrl, 1, wxEXPAND);
	icons = new wxImageList(16, 16, 1);
	// Map from an icon file name to its index in $self->{icons}.
	//    $self->{icon_index} = {};
	// Index of the last icon inserted into $self->{icons}.
	icon_count = -1;
	treectrl->AssignImageList(icons);
	treectrl->AddRoot("root");
	//    $self->{pages} = [];
	treectrl->SetIndent(0);
	disable_tree_sel_changed_event = 0;

	/*   EVT_TREE_SEL_CHANGED($parent, $self->{treectrl}, sub {
	return if $self->{disable_tree_sel_changed_event};
	my $page = first { $_->{title} eq $self->{treectrl}->GetItemText($self->{treectrl}->GetSelection) } @{$self->{pages}}
	or return;
	$_->Hide for @{$self->{pages}};
	$page->Show;
	$self->{hsizer}->Layout;
	$self->Refresh;
	});
	EVT_KEY_DOWN($self->{treectrl}, sub {
	my ($treectrl, $event) = @_;
	if ($event->GetKeyCode == WXK_TAB) {
	$treectrl->Navigate($event->ShiftDown ? &Wx::wxNavigateBackward : &Wx::wxNavigateForward);
	} else {
	$event->Skip;
	}
	});

	EVT_COMBOBOX($parent, $self->{presets_choice}, sub {
	$self->select_preset($self->{presets_choice}->GetStringSelection);
	});

	EVT_BUTTON($self, $self->{btn_save_preset}, sub { $self->save_preset });
	EVT_BUTTON($self, $self->{btn_delete_preset}, sub { $self->delete_preset });
	EVT_BUTTON($self, $self->{btn_hide_incompatible_presets}, sub { $self->_toggle_show_hide_incompatible });
*/
	// Initialize the DynamicPrintConfig by default keys/values.
	// Possible %params keys: no_controller
//	build(/*%params*/);
//	rebuild_page_tree();
//	_update();


	return;//$self;
}

void CTab::OnTreeSelChange(wxCommandEvent& event)
{
	if (disable_tree_sel_changed_event) return;
	CPage* page = nullptr;
	for (auto& el : pages)
		if (el.title() == treectrl->GetSelection())
		{
			page = &el;
			break;
		}
	if (page == nullptr) return;

	for (auto& el : pages)
		el.Hide();
	page->Show();
	hsizer->Layout();
	this->Refresh();
};

void CTab::OnKeyDown(wxKeyEvent& event)
{
	event.GetKeyCode() == WXK_TAB ?
		treectrl->Navigate(event.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward) :
		event.Skip();
};

void CTab::save_preset(wxCommandEvent &event){};
void CTab::delete_preset(wxCommandEvent &event){};
void CTab::_toggle_show_hide_incompatible(wxCommandEvent &event){};

} // GUI
} // Slic3r
