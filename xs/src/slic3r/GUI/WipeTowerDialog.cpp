#include <algorithm>
#include <sstream>
#include "WipeTowerDialog.hpp"

//! macro used to mark string used at localization,
//! return same string
#define L(s) s



RammingDialog::RammingDialog(wxWindow* parent,const std::string& parameters)
: wxDialog(parent, -1,  wxT("Ramming customization"), wxPoint(50,50), wxSize(800,550), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    this->Centre();
    m_panel_ramming  = new RammingPanel(this,parameters);
    m_panel_ramming->Show(true);
    this->Show();

    auto main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(m_panel_ramming, 1, wxEXPAND);
    main_sizer->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 10);
    SetSizer(main_sizer);
    SetMinSize(GetSize());
    main_sizer->SetSizeHints(this);
  
    this->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) { EndModal(wxCANCEL); });
    
    this->Bind(wxEVT_BUTTON,[this](wxCommandEvent&) {
        m_output_data = m_panel_ramming->get_parameters();
        EndModal(wxID_OK);
        },wxID_OK);
}





RammingPanel::RammingPanel(wxWindow* parent, const std::string& parameters)
: wxPanel(parent,wxID_ANY,wxPoint(50,50), wxSize(800,350),wxBORDER_RAISED)
{
    new wxStaticText(this,wxID_ANY,wxString("Total ramming time (s):"),     wxPoint(500,105),      wxSize(200,25),wxALIGN_LEFT);
    m_widget_time = new wxSpinCtrlDouble(this,wxID_ANY,wxEmptyString,       wxPoint(700,100),      wxSize(75,25),wxSP_ARROW_KEYS|wxALIGN_RIGHT,0.,5.0,3.,0.5);        
    new wxStaticText(this,wxID_ANY,wxString("Total rammed volume (mm\u00B3):"),  wxPoint(500,135),      wxSize(200,25),wxALIGN_LEFT);
    m_widget_volume = new wxSpinCtrl(this,wxID_ANY,wxEmptyString,           wxPoint(700,130),      wxSize(75,25),wxSP_ARROW_KEYS|wxALIGN_RIGHT,0,10000,0);        
    new wxStaticText(this,wxID_ANY,wxString("Ramming line width (%):"),     wxPoint(500,205),      wxSize(200,25),wxALIGN_LEFT);
    m_widget_ramming_line_width_multiplicator = new wxSpinCtrl(this,wxID_ANY,wxEmptyString,       wxPoint(700,200),      wxSize(75,25),wxSP_ARROW_KEYS|wxALIGN_RIGHT,10,200,100);        
    new wxStaticText(this,wxID_ANY,wxString("Ramming line spacing (%):"),   wxPoint(500,235),      wxSize(200,25),wxALIGN_LEFT);
    m_widget_ramming_step_multiplicator = new wxSpinCtrl(this,wxID_ANY,wxEmptyString,     wxPoint(700,230),      wxSize(75,25),wxSP_ARROW_KEYS|wxALIGN_RIGHT,10,200,100);        
    
    std::stringstream stream{parameters};
    stream >> m_ramming_line_width_multiplicator >> m_ramming_step_multiplicator;
    int ramming_speed_size = 0;
    float dummy = 0.f;
    while (stream >> dummy)
        ++ramming_speed_size;
    stream.clear();
    stream.get();    
    
    std::vector<std::pair<float,float>> buttons;
    float x = 0.f;
    float y = 0.f;
    while (stream >> x >> y)
        buttons.push_back(std::make_pair(x,y));        
    
    m_chart = new Chart(this,wxRect(10,10,480,360),buttons,ramming_speed_size,0.25f);
    
    m_widget_time->SetValue(m_chart->get_time());
    m_widget_time->SetDigits(2);
    m_widget_volume->SetValue(m_chart->get_volume());
    m_widget_volume->Disable();
    m_widget_ramming_line_width_multiplicator->SetValue(m_ramming_line_width_multiplicator);
    m_widget_ramming_step_multiplicator->SetValue(m_ramming_step_multiplicator);        
    
    m_widget_ramming_step_multiplicator->Bind(wxEVT_TEXT,[this](wxCommandEvent&) { line_parameters_changed(); });
    m_widget_ramming_line_width_multiplicator->Bind(wxEVT_TEXT,[this](wxCommandEvent&) { line_parameters_changed(); });
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




WipingDialog::WipingDialog(wxWindow* parent,const std::vector<float>& matrix, const std::vector<float>& extruders)
: wxDialog(parent, -1,  wxT(L("Wipe tower - Purging volume adjustment")), wxPoint(50,50), wxSize(800,550), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    this->Centre();
    
    m_panel_wiping  = new WipingPanel(this,matrix,extruders);
    this->Show();

    auto main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(m_panel_wiping, 1, wxEXPAND);
    main_sizer->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 10);
    SetSizer(main_sizer);
    SetMinSize(GetSize());
    main_sizer->SetSizeHints(this);
    
    this->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) { EndModal(wxCANCEL); });
    
    this->Bind(wxEVT_BUTTON,[this](wxCommandEvent&) {
        m_output_matrix    = m_panel_wiping->read_matrix_values();
        m_output_extruders = m_panel_wiping->read_extruders_values();
        EndModal(wxID_OK);
        },wxID_OK);
}




WipingPanel::WipingPanel(wxWindow* parent, const std::vector<float>& matrix, const std::vector<float>& extruders)
: wxPanel(parent,wxID_ANY,wxPoint(50,50), wxSize(500,350),wxBORDER_RAISED)
{
    m_number_of_extruders = (int)(sqrt(matrix.size())+0.001); // number of extruders
    m_notadvanced_widgets.push_back(new wxStaticText(this,wxID_ANY,wxString(L("Total purging volume is calculated by summing two values below, depending on which tools are loaded/unloaded.")),wxPoint(40,25) ,wxSize(500,35)));
    m_notadvanced_widgets.push_back(new wxStaticText(this,wxID_ANY,wxString(L("Volume to purge (mm\u00B3) when the filament is being")),wxPoint(40,85) ,wxSize(500,25)));
    m_notadvanced_widgets.push_back(new wxStaticText(this,wxID_ANY,wxString(L("unloaded")),wxPoint(110,105) ,wxSize(500,25)));
    m_notadvanced_widgets.push_back(new wxStaticText(this,wxID_ANY,wxString(L("loaded")),wxPoint(195,105) ,wxSize(500,25)));
    m_widget_button = new wxButton(this,wxID_ANY,"-",wxPoint(0,0),wxSize(170,20));

    for (unsigned int i=0;i<m_number_of_extruders;++i) {
        m_notadvanced_widgets.push_back(new wxStaticText(this,wxID_ANY,wxString(L("Tool #"))<<i+1<<": ",wxPoint(20,135+30*i) ,wxSize(150,25),wxALIGN_LEFT));
        m_old.push_back(new wxSpinCtrl(this,wxID_ANY,wxEmptyString,wxPoint(120,130+30*i),wxSize(50,25),wxSP_ARROW_KEYS|wxALIGN_RIGHT,0,300,extruders[2*i]));
        m_new.push_back(new wxSpinCtrl(this,wxID_ANY,wxEmptyString,wxPoint(195,130+30*i),wxSize(50,25),wxSP_ARROW_KEYS|wxALIGN_RIGHT,0,300,extruders[2*i+1]));
    }

    wxPoint origin(50,85);
    m_advanced_widgets.push_back(new wxStaticText(this,wxID_ANY,wxString(L("Here you can adjust required purging volume (mm\u00B3) for any given pair of tools.")),wxPoint(40,25) ,wxSize(500,35)));
    for (unsigned int i=0;i<m_number_of_extruders;++i) {
        edit_boxes.push_back(std::vector<wxTextCtrl*>(0));
        m_advanced_widgets.push_back(new wxStaticText(this,wxID_ANY,wxString("")<<i+1,origin+wxPoint(45+60*i,25) ,wxSize(20,25)));
        m_advanced_widgets.push_back(new wxStaticText(this,wxID_ANY,wxString("")<<i+1,origin+wxPoint(0,50+30*i) ,wxSize(500,25)));
        for (unsigned int j=0;j<m_number_of_extruders;++j) {
            edit_boxes.back().push_back(new wxTextCtrl(this,wxID_ANY,wxEmptyString,origin+wxPoint(25+60*i,45+30*j),wxSize(50,25)));
            if (i==j)
                edit_boxes[i][j]->Disable();
            else
                edit_boxes[i][j]->SetValue(wxString("")<<int(matrix[m_number_of_extruders*j+i]));
        }
        m_advanced_widgets.push_back(new wxStaticText(this,wxID_ANY,wxString(L("Filament changed to")),origin+wxPoint(75,0) ,wxSize(500,25)));
    }

    m_widget_button->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){toggle_advanced(true);});
    toggle_advanced();
}





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


std::vector<float> WipingPanel::read_extruders_values() {
    std::vector<float> output;
    for (unsigned int i=0;i<m_number_of_extruders;++i) {
        output.push_back(m_old[i]->GetValue());
        output.push_back(m_new[i]->GetValue());
    }
    return output;
}


void WipingPanel::fill_in_matrix() {
    for (unsigned i=0;i<m_number_of_extruders;++i) {
        for (unsigned j=0;j<m_number_of_extruders;++j) {
            if (i==j) continue;
                edit_boxes[j][i]->SetValue(wxString("")<< (m_old[i]->GetValue() + m_new[j]->GetValue()));
        }
    }
}




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



void WipingPanel::toggle_advanced(bool user_button) {
    if (m_advanced && !advanced_matches_simple() && user_button) {
        if (wxMessageDialog(this,wxString(L("Switching to simple settings will discard changes done in the advanced mode!\n\nDo you want to proceed?")),
                            wxString(L("Warning")),wxYES_NO|wxICON_EXCLAMATION).ShowModal() != wxID_YES)
            return;
    }

    m_advanced = !m_advanced;

    if (!user_button) {                 // we were called from constructor
        if (advanced_matches_simple())  // advanced and simple match - let's show simple version
            m_advanced = false;
        else
            m_advanced = true;
    }

    for (unsigned i=0;i<m_number_of_extruders;++i) {        // shows/hides input controls
            for (unsigned j=0;j<m_number_of_extruders;++j)
                edit_boxes[i][j]->Show(m_advanced);
            m_old[i]->Show(!m_advanced);
            m_new[i]->Show(!m_advanced);
    }
    for (const auto& widget : m_advanced_widgets)          // shows/hides other widgets
        widget->Show(m_advanced);
    for (const auto& widget : m_notadvanced_widgets)
        widget->Show(!m_advanced);

    if (m_advanced) {
        if (user_button) fill_in_matrix();  // otherwise keep values loaded from config
        m_widget_button->SetLabel(L("Show simplified settings"));
    }
    else
        m_widget_button->SetLabel(L("Show advanced settings"));

    this->Refresh();
}