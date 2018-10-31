#ifndef slic3r_2DBed_hpp_
#define slic3r_2DBed_hpp_

#include <wx/wx.h>
#include "Config.hpp"

namespace Slic3r {
namespace GUI {

class Bed_2D : public wxPanel
{
	bool		m_user_drawn_background = true;

	bool		m_painted = false;
	bool		m_interactive = false;
	double		m_scale_factor;
	Vec2d		m_shift = Vec2d::Zero();
	Vec2d		m_pos = Vec2d::Zero();
	std::function<void(Vec2d)>	m_on_move = nullptr;

	Point		to_pixels(Vec2d point);
	Vec2d		to_units(Point point);
	void		repaint();
	void		mouse_event(wxMouseEvent event);
	void		set_pos(Vec2d pos);

public:
	Bed_2D(wxWindow* parent) 
	{
		Create(parent, wxID_ANY, wxDefaultPosition, wxSize(250, -1), wxTAB_TRAVERSAL);
//		m_user_drawn_background = $^O ne 'darwin';
#ifdef __APPLE__
		m_user_drawn_background = false;
#endif /*__APPLE__*/
		Bind(wxEVT_PAINT, ([this](wxPaintEvent e) { repaint(); }));
//		EVT_ERASE_BACKGROUND($self, sub{}) if $self->{user_drawn_background};
//		Bind(EVT_MOUSE_EVENTS, ([this](wxMouseEvent  event) {/*mouse_event()*/; }));
		Bind(wxEVT_LEFT_DOWN, ([this](wxMouseEvent  event) { mouse_event(event); }));
		Bind(wxEVT_MOTION, ([this](wxMouseEvent  event) { mouse_event(event); }));
		Bind(wxEVT_SIZE, ([this](wxSizeEvent e) { Refresh(); }));
	}
	~Bed_2D() {}

	std::vector<Vec2d>		m_bed_shape;
		
};


} // GUI
} // Slic3r

#endif /* slic3r_2DBed_hpp_ */
