#include <wx/wx.h>
#include "Config.hpp"

namespace Slic3r {
namespace GUI {

class Bed_2D : public wxPanel
{
	bool		m_user_drawn_background = false;

	bool		m_painted = false;
	double		m_scale_factor;
	Pointf		m_shift;
	Point		m_pos;

	Point		to_pixels(Pointf point);
	void		repaint();
public:
	Bed_2D(wxWindow* parent) 
	{
		Create(parent, wxID_ANY, wxDefaultPosition, wxSize(250, -1), wxTAB_TRAVERSAL);
//		m_user_drawn_background = $^O ne 'darwin';
		m_user_drawn_background = true;
		Bind(wxEVT_PAINT, ([this](wxPaintEvent e)
		{
			repaint();
		}));
//		EVT_ERASE_BACKGROUND($self, sub{}) if $self->{user_drawn_background};
//		Bind(wxEVT_MOUSE_EVENTS, ([this](wxCommandEvent){/*mouse_event()*/; }));
		Bind(wxEVT_SIZE, ([this](wxSizeEvent){Refresh(); }));
	}
	~Bed_2D(){}

	std::vector<Pointf>		m_bed_shape;
		
};


} // GUI
} // Slic3r
