#ifndef RAMMING_CHART_H_
#define RAMMING_CHART_H_

#include <vector>
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
    #include <wx/wx.h>
#endif

wxDECLARE_EVENT(EVT_WIPE_TOWER_CHART_CHANGED, wxCommandEvent);


class Chart : public wxWindow {
        
public:
    Chart(wxWindow* parent, wxRect rect,const std::vector<std::vector<std::pair<float,float>>>& initial_buttons,std::vector<std::vector<float>> ramming_speed, float sampling) :
        wxWindow(parent,wxID_ANY,rect.GetTopLeft(),rect.GetSize())
    {
        m_rect=wxRect(wxPoint(30,0),rect.GetSize()-wxSize(30,30));
        for (int i=0;i<4;++i) {
            visible_areas.push_back(wxRect2DDouble(0.0, 0.0, sampling*ramming_speed[i].size(), 20.));
            m_buttons.push_back(std::vector<ButtonToDrag>());
            m_lines_to_draw.push_back(std::vector<int>());
            if (initial_buttons.size()>0)
                for (const auto& pair : initial_buttons[i])
                    m_buttons.back().push_back(wxPoint2DDouble(pair.first,pair.second));
            set_extruder(i); // to calculate all interpolating splines
        }
        set_extruder(0);
    }
    void set_extruder(unsigned ext) {
        if (ext>=4) return;
        m_current_extruder = ext;
        visible_area = &(visible_areas[ext]);
        m_line_to_draw = &(m_lines_to_draw[ext]);
        recalculate_line();
    }
    void set_xy_range(float x,float y) {
        x = int(x/0.5) * 0.5;
        if (x>=0) visible_area->SetRight(x);
        if (y>=0) visible_area->SetBottom(y);
        recalculate_line();
    }
    float get_volume() const { return m_total_volume; }
    float get_time()   const { return visible_area->m_width; }
    std::vector<std::vector<float>> get_ramming_speeds(float sampling) const; //returns sampled ramming speed for all extruders
    std::vector<std::vector<std::pair<float,float>>> get_buttons() const; // returns buttons position for all extruders
   
    
    void draw(wxDC& dc);
    
    void mouse_clicked(wxMouseEvent& event);
    void mouse_right_button_clicked(wxMouseEvent& event);
    void mouse_moved(wxMouseEvent& event);
    void mouse_double_clicked(wxMouseEvent& event);
    void mouse_left_window(wxMouseEvent&) { m_dragged = nullptr; }        
    void mouse_released(wxMouseEvent&)    { m_dragged = nullptr; }
    void paint_event(wxPaintEvent&) { wxPaintDC dc(this); draw(dc); }
    DECLARE_EVENT_TABLE()
    

        
private:
    static const bool fixed_x = true;
    static const bool splines = true;
    static const bool manual_points_manipulation = false;
    static const int side = 10; // side of draggable button

    class ButtonToDrag {
    public:
        bool operator<(const ButtonToDrag& a) const { return m_pos.m_x < a.m_pos.m_x; }
        ButtonToDrag(wxPoint2DDouble pos) : m_pos{pos} {};
        wxPoint2DDouble get_pos() const { return m_pos; }            
        void move(double x,double y) { m_pos.m_x+=x; m_pos.m_y+=y; }
    private:
        wxPoint2DDouble m_pos;              // position in math coordinates                       
    };
    
    
    
    wxPoint math_to_screen(const wxPoint2DDouble& math) const {
        wxPoint screen;
        screen.x = (math.m_x-visible_area->m_x) * (m_rect.GetWidth()  / visible_area->m_width  );
        screen.y = (math.m_y-visible_area->m_y) * (m_rect.GetHeight() / visible_area->m_height );
        screen.y *= -1;
        screen += m_rect.GetLeftBottom();            
        return screen;
    }
    wxPoint2DDouble screen_to_math(const wxPoint& screen) const {
        wxPoint2DDouble math = screen;
        math -= m_rect.GetLeftBottom();
        math.m_y *= -1;
        math.m_x *= visible_area->m_width   / m_rect.GetWidth();    // scales to [0;1]x[0,1]
        math.m_y *= visible_area->m_height / m_rect.GetHeight();
        return (math+visible_area->GetLeftTop());
    }
        
    int which_button_is_clicked(const wxPoint& point) const {
        if (!m_rect.Contains(point))
            return -1;
        for (uint i=0;i<m_buttons[m_current_extruder].size();++i) {
            wxRect rect(math_to_screen(m_buttons[m_current_extruder][i].get_pos())-wxPoint(side/2.,side/2.),wxSize(side,side)); // bounding rectangle of this button
            if ( rect.Contains(point) )
                return i;
        }
        return (-1);
    }
        
        
    void recalculate_line();
    void recalculate_volume();
     
    
    unsigned m_current_extruder = 0;
    wxRect m_rect;                  // rectangle on screen the chart is mapped into (screen coordinates)
    wxPoint m_previous_mouse;        
    std::vector< std::vector<ButtonToDrag> > m_buttons;
    std::vector< std::vector<int> > m_lines_to_draw;
    std::vector< wxRect2DDouble > visible_areas;
    wxRect2DDouble* visible_area = nullptr;
    std::vector<int>* m_line_to_draw = nullptr;
    ButtonToDrag* m_dragged = nullptr;
    float m_total_volume = 0.f;
    
    
};


#endif // RAMMING_CHART_H_