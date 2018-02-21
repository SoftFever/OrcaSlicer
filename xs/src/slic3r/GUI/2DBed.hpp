#include <wx/wx.h>
#include "Config.hpp"

namespace Slic3r {
namespace GUI {

class Bed_2D : public wxPanel
{
	bool		m_user_drawn_background = false;

	bool		m_painted = false;
	bool		m_interactive = false;
	double		m_scale_factor;
	Pointf		m_shift;
	Pointf		m_pos;
	std::function<void(Pointf)>	m_on_move = nullptr;

	Point		to_pixels(Pointf point);
	Pointf		to_units(Point point);
	void		repaint();
	void		mouse_event(wxMouseEvent event);
	void		set_pos(Pointf pos);

public:
	Bed_2D(wxWindow* parent) 
	{
		Create(parent, wxID_ANY, wxDefaultPosition, wxSize(250, -1), wxTAB_TRAVERSAL);
//		m_user_drawn_background = $^O ne 'darwin';
		m_user_drawn_background = true;
		Bind(wxEVT_PAINT, ([this](wxPaintEvent e) { repaint(); }));
//		EVT_ERASE_BACKGROUND($self, sub{}) if $self->{user_drawn_background};
//		Bind(EVT_MOUSE_EVENTS, ([this](wxMouseEvent  event){/*mouse_event()*/; }));
		Bind(wxEVT_LEFT_DOWN, ([this](wxMouseEvent  event){ mouse_event(event); }));
		Bind(wxEVT_MOTION, ([this](wxMouseEvent  event){ mouse_event(event); }));
		Bind(wxEVT_SIZE, ([this](wxSizeEvent e) { Refresh(); }));
	}
	~Bed_2D(){}

	std::vector<Pointf>		m_bed_shape;
		
};


} // GUI
} // Slic3r
