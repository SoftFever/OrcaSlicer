#include <algorithm>
#include <sstream>
#include "WipeTowerDialog.hpp"


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
    new wxStaticText(this,wxID_ANY,wxString("Total rammed volume (mm3):"),  wxPoint(500,135),      wxSize(200,25),wxALIGN_LEFT);
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








/*
void WipingPanel::fill_parameters(Slic3r::WipeTowerParameters& p) {
    p.wipe_volumes.clear();
    p.filament_wipe_volumes.clear();
    for (int i=0;i<4;++i) {
        // first go through the full matrix:
        p.wipe_volumes.push_back(std::vector<float>());
        for (int j=0;j<4;++j) {
            double val = 0.;
            edit_boxes[j][i]->GetValue().ToDouble(&val);
            p.wipe_volumes[i].push_back((float)val);  
        }
        
        // now the filament volumes:
        p.filament_wipe_volumes.push_back(std::make_pair(m_old[i]->GetValue(),m_new[i]->GetValue()));
    }
}
*/



WipingDialog::WipingDialog(wxWindow* parent,const std::vector<float>& init_data)
: wxDialog(parent, -1,  wxT("Wiping customization"), wxPoint(50,50), wxSize(800,550), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    this->Centre();
    
    m_panel_wiping  = new WipingPanel(this,init_data);
    this->Show();

    auto main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(m_panel_wiping, 1, wxEXPAND);
    main_sizer->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 10);
    SetSizer(main_sizer);
    SetMinSize(GetSize());
    main_sizer->SetSizeHints(this);
    
    this->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) { EndModal(wxCANCEL); });
    
    this->Bind(wxEVT_BUTTON,[this](wxCommandEvent&) {
        //m_output_data=read_dialog_values();
        EndModal(wxID_OK);
        },wxID_OK);
}



WipingPanel::WipingPanel(wxWindow* parent,const std::vector<float>& data)
: wxPanel(parent,wxID_ANY,wxPoint(50,50), wxSize(800,350),wxBORDER_RAISED)
{
    const int N = 4; // number of extruders
    new wxStaticText(this,wxID_ANY,wxString("Volume to wipe when the filament is being"),wxPoint(40,55) ,wxSize(500,25));
    new wxStaticText(this,wxID_ANY,wxString("unloaded"),wxPoint(110,75) ,wxSize(500,25));
    new wxStaticText(this,wxID_ANY,wxString("loaded"),wxPoint(195,75) ,wxSize(500,25));        
    m_widget_button = new wxButton(this,wxID_ANY,"-> Fill in the matrix ->",wxPoint(300,130),wxSize(175,50));
    for (int i=0;i<N;++i) {
        new wxStaticText(this,wxID_ANY,wxString("Filament #")<<i+1<<": ",wxPoint(20,105+30*i) ,wxSize(150,25),wxALIGN_LEFT);
        m_old.push_back(new wxSpinCtrl(this,wxID_ANY,wxEmptyString,wxPoint(120,100+30*i),wxSize(50,25),wxSP_ARROW_KEYS|wxALIGN_RIGHT,0,100,data[2*i]));
        m_new.push_back(new wxSpinCtrl(this,wxID_ANY,wxEmptyString,wxPoint(195,100+30*i),wxSize(50,25),wxSP_ARROW_KEYS|wxALIGN_RIGHT,0,100,data[2*i+1]));
    }
    
    wxPoint origin(515,55);
    for (int i=0;i<N;++i) {
        edit_boxes.push_back(std::vector<wxTextCtrl*>(0));
        new wxStaticText(this,wxID_ANY,wxString("")<<i+1,origin+wxPoint(45+60*i,25) ,wxSize(20,25));
        new wxStaticText(this,wxID_ANY,wxString("")<<i+1,origin+wxPoint(0,50+30*i) ,wxSize(500,25));
        for (int j=0;j<N;++j) {
            edit_boxes.back().push_back(new wxTextCtrl(this,wxID_ANY,wxEmptyString,origin+wxPoint(25+60*i,45+30*j),wxSize(50,25)));
            if (i==j)
                edit_boxes[i][j]->Disable();
            else
                edit_boxes[i][j]->SetValue(wxString("")<<int(0));//p.wipe_volumes[j][i]));
        }
        new wxStaticText(this,wxID_ANY,wxString("Filament changed to"),origin+wxPoint(75,0) ,wxSize(500,25));            
    }
    
    m_widget_button->Bind(wxEVT_BUTTON,[this](wxCommandEvent&){fill_in_matrix();});
    Refresh(this);
}

void WipingPanel::fill_in_matrix() {
    wxArrayString choices;
    choices.Add("sum");
    choices.Add("maximum");
    wxSingleChoiceDialog dialog(this,"How shall I calculate volume for any given pair?\n\nI can either sum volumes for old and new filament, or just use the higher value.","DEBUGGING",choices);
    if (dialog.ShowModal() == wxID_CANCEL)
        return;        
    for (unsigned i=0;i<4;++i) {
        for (unsigned j=0;j<4;++j) {
            if (i==j) continue;
            if (!dialog.GetSelection()) edit_boxes[j][i]->SetValue(wxString("")<< (m_old[i]->GetValue() + m_new[j]->GetValue()));
            else
              edit_boxes[j][i]->SetValue(wxString("")<< (std::max(m_old[i]->GetValue(), m_new[j]->GetValue())));
        }
    }
}








