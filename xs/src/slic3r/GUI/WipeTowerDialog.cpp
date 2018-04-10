#include <algorithm>
#include <sstream>
#include "WipeTowerDialog.hpp"

#include <wx/sizer.h>

//! macro used to mark string used at localization,
//! return same string
#define L(s) s



RammingDialog::RammingDialog(wxWindow* parent,const std::string& parameters)
: wxDialog(parent, wxID_ANY, _(L("Ramming customization")), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE/* | wxRESIZE_BORDER*/)
{
    m_panel_ramming  = new RammingPanel(this,parameters);

    // Not found another way of getting the background colours of RammingDialog, RammingPanel and Chart correct than setting
    // them all explicitely. Reading the parent colour yielded colour that didn't really match it, no wxSYS_COLOUR_... matched
    // colour used for the dialog. Same issue (and "solution") here : https://forums.wxwidgets.org/viewtopic.php?f=1&t=39608
    // Whoever can fix this, feel free to do so.
    this->           SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_FRAMEBK));
    m_panel_ramming->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_FRAMEBK));
    m_panel_ramming->Show(true);
    this->Show();

    auto main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(m_panel_ramming, 1, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, 5);
    main_sizer->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxALIGN_CENTER_HORIZONTAL | wxTOP | wxBOTTOM, 10);
    SetSizer(main_sizer);
    main_sizer->SetSizeHints(this);

    this->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) { EndModal(wxCANCEL); });

    this->Bind(wxEVT_BUTTON,[this](wxCommandEvent&) {
        m_output_data = m_panel_ramming->get_parameters();
        EndModal(wxID_OK);
        },wxID_OK);
    this->Show();
    wxMessageDialog(this,_(L("Ramming denotes the rapid extrusion just before a tool change in a single-extruder MM printer. Its purpose is to "
                   "properly shape the end of the unloaded filament so it does not prevent insertion of the new filament and can itself "
                   "be reinserted later. This phase is important and different materials can require different extrusion speeds to get "
                   "the good shape. For this reason, the extrusion rates during ramming are adjustable.\n\nThis is an expert-level "
                   "setting, incorrect adjustment will likely lead to jams, extruder wheel grinding into filament etc.")),_(L("Warning")),wxOK|wxICON_EXCLAMATION).ShowModal();
}





RammingPanel::RammingPanel(wxWindow* parent, const std::string& parameters)
: wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize/*,wxPoint(50,50), wxSize(800,350),wxBORDER_RAISED*/)
{
	auto sizer_chart = new wxBoxSizer(wxVERTICAL);
	auto sizer_param = new wxBoxSizer(wxVERTICAL);

	std::stringstream stream{ parameters };
	stream >> m_ramming_line_width_multiplicator >> m_ramming_step_multiplicator;
	int ramming_speed_size = 0;
	float dummy = 0.f;
	while (stream >> dummy)
		++ramming_speed_size;
	stream.clear();
	stream.get();

	std::vector<std::pair<float, float>> buttons;
	float x = 0.f;
	float y = 0.f;
	while (stream >> x >> y)
		buttons.push_back(std::make_pair(x, y));

	m_chart = new Chart(this, wxRect(10, 10, 480, 360), buttons, ramming_speed_size, 0.25f);
    m_chart->SetBackgroundColour(parent->GetBackgroundColour()); // see comment in RammingDialog constructor
 	sizer_chart->Add(m_chart, 0, wxALL, 5);

    m_widget_time						= new wxSpinCtrlDouble(this,wxID_ANY,wxEmptyString,wxDefaultPosition,wxSize(75, -1),wxSP_ARROW_KEYS,0.,5.0,3.,0.5);        
    m_widget_volume							  = new wxSpinCtrl(this,wxID_ANY,wxEmptyString,wxDefaultPosition,wxSize(75, -1),wxSP_ARROW_KEYS,0,10000,0);        
    m_widget_ramming_line_width_multiplicator = new wxSpinCtrl(this,wxID_ANY,wxEmptyString,wxDefaultPosition,wxSize(75, -1),wxSP_ARROW_KEYS,10,200,100);        
    m_widget_ramming_step_multiplicator		  = new wxSpinCtrl(this,wxID_ANY,wxEmptyString,wxDefaultPosition,wxSize(75, -1),wxSP_ARROW_KEYS,10,200,100);        

	auto gsizer_param = new wxFlexGridSizer(2, 5, 15);
	gsizer_param->Add(new wxStaticText(this, wxID_ANY, wxString(_(L("Total ramming time (s):")))), 0, wxALIGN_CENTER_VERTICAL);
	gsizer_param->Add(m_widget_time);
	gsizer_param->Add(new wxStaticText(this, wxID_ANY, wxString(_(L("Total rammed volume (mm"))+"\u00B3):")), 0, wxALIGN_CENTER_VERTICAL);
	gsizer_param->Add(m_widget_volume);
	gsizer_param->AddSpacer(20);
	gsizer_param->AddSpacer(20);
	gsizer_param->Add(new wxStaticText(this, wxID_ANY, wxString(_(L("Ramming line width (%):")))), 0, wxALIGN_CENTER_VERTICAL);
	gsizer_param->Add(m_widget_ramming_line_width_multiplicator);
	gsizer_param->Add(new wxStaticText(this, wxID_ANY, wxString(_(L("Ramming line spacing (%):")))), 0, wxALIGN_CENTER_VERTICAL);
	gsizer_param->Add(m_widget_ramming_step_multiplicator);

	sizer_param->Add(gsizer_param, 0, wxTOP, 100);

    m_widget_time->SetValue(m_chart->get_time());
    m_widget_time->SetDigits(2);
    m_widget_volume->SetValue(m_chart->get_volume());
    m_widget_volume->Disable();
    m_widget_ramming_line_width_multiplicator->SetValue(m_ramming_line_width_multiplicator);
    m_widget_ramming_step_multiplicator->SetValue(m_ramming_step_multiplicator);        
    
    m_widget_ramming_step_multiplicator->Bind(wxEVT_TEXT,[this](wxCommandEvent&) { line_parameters_changed(); });
    m_widget_ramming_line_width_multiplicator->Bind(wxEVT_TEXT,[this](wxCommandEvent&) { line_parameters_changed(); });

	auto sizer = new wxBoxSizer(wxHORIZONTAL);
	sizer->Add(sizer_chart, 0, wxALL, 5);
	sizer->Add(sizer_param, 0, wxALL, 10);

	sizer->SetSizeHints(this);
	SetSizer(sizer);

    m_widget_time->Bind(wxEVT_TEXT,[this](wxCommandEvent&) {m_chart->set_xy_range(m_widget_time->GetValue(),-1);});
    m_widget_time->Bind(wxEVT_CHAR,[](wxKeyEvent&){});      // do nothing - prevents the user to change the value
    m_widget_volume->Bind(wxEVT_CHAR,[](wxKeyEvent&){});    // do nothing - prevents the user to change the value   
    Bind(EVT_WIPE_TOWER_CHART_CHANGED,[this](wxCommandEvent&) {m_widget_volume->SetValue(m_chart->get_volume()); m_widget_time->SetValue(m_chart->get_time());} );
    Refresh(this);
}

void RammingPanel::line_parameters_changed() {
    m_ramming_line_width_multiplicator = m_widget_ramming_line_width_multiplicator->GetValue();
    m_ramming_step_multiplicator = m_widget_ramming_step_multiplicator->GetValue();
}

std::string RammingPanel::get_parameters()
{
    std::vector<float> speeds = m_chart->get_ramming_speed(0.25f);
    std::vector<std::pair<float,float>> buttons = m_chart->get_buttons();
    std::stringstream stream;
    stream << m_ramming_line_width_multiplicator << " " << m_ramming_step_multiplicator;
    for (const float& speed_value : speeds)
        stream << " " << speed_value;
    stream << "|";    
    for (const auto& button : buttons)
        stream << " " << button.first << " " << button.second;
    return stream.str();
}


#define	ITEM_WIDTH	60
// Parent dialog for purging volume adjustments - it fathers WipingPanel widget (that contains all controls) and a button to toggle simple/advanced mode:
WipingDialog::WipingDialog(wxWindow* parent,const std::vector<float>& matrix, const std::vector<float>& extruders)
: wxDialog(parent, wxID_ANY, _(L("Wipe tower - Purging volume adjustment")), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE/* | wxRESIZE_BORDER*/)
{
    auto widget_button = new wxButton(this,wxID_ANY,"-",wxPoint(0,0),wxDefaultSize);
    m_panel_wiping  = new WipingPanel(this,matrix,extruders, widget_button);

    auto main_sizer = new wxBoxSizer(wxVERTICAL);

	// set min sizer width according to extruders count
	const auto sizer_width = (int)((sqrt(matrix.size()) + 2.8)*ITEM_WIDTH);
	main_sizer->SetMinSize(wxSize(sizer_width, -1));

    main_sizer->Add(m_panel_wiping, 0, wxEXPAND | wxALL, 5);
	main_sizer->Add(widget_button, 0, wxALIGN_CENTER_HORIZONTAL | wxCENTER | wxBOTTOM, 5);
    main_sizer->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 10);
    SetSizer(main_sizer);
    main_sizer->SetSizeHints(this);

    this->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) { EndModal(wxCANCEL); });
    
    this->Bind(wxEVT_BUTTON,[this](wxCommandEvent&) {                 // if OK button is clicked..
        m_output_matrix    = m_panel_wiping->read_matrix_values();    // ..query wiping panel and save returned values
        m_output_extruders = m_panel_wiping->read_extruders_values(); // so they can be recovered later by calling get_...()
        EndModal(wxID_OK);
        },wxID_OK);

    this->Show();
}

// This function allows to "play" with sizers parameters (like align or border)
void WipingPanel::format_sizer(wxSizer* sizer, wxPanel* page, wxGridSizer* grid_sizer, const wxString& info, const wxString& table_title, int table_lshift/*=0*/)
{
	sizer->Add(new wxStaticText(page, wxID_ANY, info,wxDefaultPosition,wxSize(0,50)), 0, wxEXPAND | wxLEFT, 15);
	auto table_sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(table_sizer, 0, wxALIGN_CENTER | wxCENTER, table_lshift);
	table_sizer->Add(new wxStaticText(page, wxID_ANY, table_title), 0, wxALIGN_CENTER | wxTOP, 50);
	table_sizer->Add(grid_sizer, 0, wxALIGN_CENTER | wxTOP, 10);
}

// This panel contains all control widgets for both simple and advanced mode (these reside in separate sizers)
WipingPanel::WipingPanel(wxWindow* parent, const std::vector<float>& matrix, const std::vector<float>& extruders, wxButton* widget_button)
: wxPanel(parent,wxID_ANY, wxDefaultPosition, wxDefaultSize/*,wxBORDER_RAISED*/)
{
    m_widget_button = widget_button;    // pointer to the button in parent dialog
    m_widget_button->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){ toggle_advanced(true); });

    m_number_of_extruders = (int)(sqrt(matrix.size())+0.001);

	// Create two switched panels with their own sizers
    m_sizer_simple          = new wxBoxSizer(wxVERTICAL);
    m_sizer_advanced        = new wxBoxSizer(wxVERTICAL);
	m_page_simple			= new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
	m_page_advanced			= new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
	m_page_simple->SetSizer(m_sizer_simple);
	m_page_advanced->SetSizer(m_sizer_advanced);

    auto gridsizer_simple   = new wxGridSizer(3, 5, 10);
    m_gridsizer_advanced = new wxGridSizer(m_number_of_extruders+1, 5, 1);

	// First create controls for advanced mode and assign them to m_page_advanced:
	for (unsigned int i = 0; i < m_number_of_extruders; ++i) {
		edit_boxes.push_back(std::vector<wxTextCtrl*>(0));

		for (unsigned int j = 0; j < m_number_of_extruders; ++j) {
			edit_boxes.back().push_back(new wxTextCtrl(m_page_advanced, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(ITEM_WIDTH, -1)));
			if (i == j)
				edit_boxes[i][j]->Disable();
			else
				edit_boxes[i][j]->SetValue(wxString("") << int(matrix[m_number_of_extruders*j + i]));
		}
	}
	m_gridsizer_advanced->Add(new wxStaticText(m_page_advanced, wxID_ANY, wxString("")));
	for (unsigned int i = 0; i < m_number_of_extruders; ++i)
		m_gridsizer_advanced->Add(new wxStaticText(m_page_advanced, wxID_ANY, wxString("") << i + 1), 0, wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL);
	for (unsigned int i = 0; i < m_number_of_extruders; ++i) {
		m_gridsizer_advanced->Add(new wxStaticText(m_page_advanced, wxID_ANY, wxString("") << i + 1), 0, wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL);
		for (unsigned int j = 0; j < m_number_of_extruders; ++j)
			m_gridsizer_advanced->Add(edit_boxes[j][i], 0);
	}

	// collect and format sizer
	format_sizer(m_sizer_advanced, m_page_advanced, m_gridsizer_advanced,
		_(L("Here you can adjust required purging volume (mm\u00B3) for any given pair of tools.")),
		_(L("Extruder changed to")));

	// Hide preview page before new page creating 
	// It allows to do that from a beginning of the main panel
	m_page_advanced->Hide(); 

	// Now the same for simple mode:
	gridsizer_simple->Add(new wxStaticText(m_page_simple, wxID_ANY, wxString("")), 0, wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL);
	gridsizer_simple->Add(new wxStaticText(m_page_simple, wxID_ANY, wxString(_(L("unloaded")))), 0, wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL);
    gridsizer_simple->Add(new wxStaticText(m_page_simple,wxID_ANY,wxString(_(L("loaded")))), 0, wxALIGN_CENTER | wxALIGN_CENTER_VERTICAL);

	for (unsigned int i=0;i<m_number_of_extruders;++i) {
        m_old.push_back(new wxSpinCtrl(m_page_simple,wxID_ANY,wxEmptyString,wxDefaultPosition, wxSize(80, -1),wxSP_ARROW_KEYS|wxALIGN_RIGHT,0,300,extruders[2*i]));
        m_new.push_back(new wxSpinCtrl(m_page_simple,wxID_ANY,wxEmptyString,wxDefaultPosition, wxSize(80, -1),wxSP_ARROW_KEYS|wxALIGN_RIGHT,0,300,extruders[2*i+1]));
		gridsizer_simple->Add(new wxStaticText(m_page_simple, wxID_ANY, wxString(_(L("Tool #"))) << i + 1 << ": "), 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL);
        gridsizer_simple->Add(m_old.back(),0);
        gridsizer_simple->Add(m_new.back(),0);
	}

	// collect and format sizer
	format_sizer(m_sizer_simple, m_page_simple, gridsizer_simple,
		_(L("Total purging volume is calculated by summing two values below, depending on which tools are loaded/unloaded.")),
		_(L("Volume to purge (mm\u00B3) when the filament is being")), 50);

	m_sizer = new wxBoxSizer(wxVERTICAL);
	m_sizer->Add(m_page_simple, 0, wxEXPAND | wxALL, 25);
	m_sizer->Add(m_page_advanced, 0, wxEXPAND | wxALL, 25);

	m_sizer->SetSizeHints(this);
	SetSizer(m_sizer);

    toggle_advanced(); // to show/hide what is appropriate
    
    m_page_advanced->Bind(wxEVT_PAINT,[this](wxPaintEvent&) {
                                              wxPaintDC dc(m_page_advanced);
                                              int y_pos = 0.5 * (edit_boxes[0][0]->GetPosition().y + edit_boxes[0][edit_boxes.size()-1]->GetPosition().y + edit_boxes[0][edit_boxes.size()-1]->GetSize().y);
                                              wxString label = _(L("From"));
                                              int text_width = 0;
                                              int text_height = 0;
                                              dc.GetTextExtent(label,&text_width,&text_height);
                                              int xpos = m_gridsizer_advanced->GetPosition().x;
                                              dc.DrawRotatedText(label,xpos-text_height,y_pos + text_width/2.f,90);
    });
}




// Reads values from the (advanced) wiping matrix:
std::vector<float> WipingPanel::read_matrix_values() {
    if (!m_advanced)
        fill_in_matrix();
    std::vector<float> output;
    for (unsigned int i=0;i<m_number_of_extruders;++i) {
        for (unsigned int j=0;j<m_number_of_extruders;++j) {
            double val = 0.;
            edit_boxes[j][i]->GetValue().ToDouble(&val);
            output.push_back((float)val);
        }
    }
    return output;
}

// Reads values from simple mode to save them for next time:
std::vector<float> WipingPanel::read_extruders_values() {
    std::vector<float> output;
    for (unsigned int i=0;i<m_number_of_extruders;++i) {
        output.push_back(m_old[i]->GetValue());
        output.push_back(m_new[i]->GetValue());
    }
    return output;
}

// This updates the "advanced" matrix based on values from "simple" mode
void WipingPanel::fill_in_matrix() {
    for (unsigned i=0;i<m_number_of_extruders;++i) {
        for (unsigned j=0;j<m_number_of_extruders;++j) {
            if (i==j) continue;
                edit_boxes[j][i]->SetValue(wxString("")<< (m_old[i]->GetValue() + m_new[j]->GetValue()));
        }
    }
}



// Function to check if simple and advanced settings are matching
bool WipingPanel::advanced_matches_simple() {
    for (unsigned i=0;i<m_number_of_extruders;++i) {
        for (unsigned j=0;j<m_number_of_extruders;++j) {
            if (i==j) continue;
            if (edit_boxes[j][i]->GetValue() != (wxString("")<< (m_old[i]->GetValue() + m_new[j]->GetValue())))
                return false;
        }
    }
    return true;
}


// Switches the dialog from simple to advanced mode and vice versa
void WipingPanel::toggle_advanced(bool user_action) {
    if (m_advanced && !advanced_matches_simple() && user_action) {
        if (wxMessageDialog(this,wxString(_(L("Switching to simple settings will discard changes done in the advanced mode!\n\nDo you want to proceed?"))),
                            wxString(_(L("Warning"))),wxYES_NO|wxICON_EXCLAMATION).ShowModal() != wxID_YES)
            return;
    }
    if (user_action)
        m_advanced = !m_advanced;                // user demands a change -> toggle
    else
        m_advanced = !advanced_matches_simple(); // if called from constructor, show what is appropriate

	(m_advanced ? m_page_advanced : m_page_simple)->Show();
	(!m_advanced ? m_page_advanced : m_page_simple)->Hide();

    m_widget_button->SetLabel(m_advanced ? _(L("Show simplified settings")) : _(L("Show advanced settings")));
    if (m_advanced)
        if (user_action) fill_in_matrix();  // otherwise keep values loaded from config

   m_sizer->Layout();
   Refresh();
}
