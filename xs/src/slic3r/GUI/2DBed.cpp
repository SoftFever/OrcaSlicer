#include "2DBed.hpp";

#include <wx/dcbuffer.h>
#include "BoundingBox.hpp"
#include "Geometry.hpp"
#include "ClipperUtils.hpp"

namespace Slic3r {
namespace GUI {

void Bed_2D::repaint()
{
	wxAutoBufferedPaintDC dc(this);
	auto cw = GetSize().GetWidth();
	auto ch = GetSize().GetHeight();
	// when canvas is not rendered yet, size is 0, 0
	if (cw == 0) return ; 

	if (m_user_drawn_background) {
		// On all systems the AutoBufferedPaintDC() achieves double buffering.
		// On MacOS the background is erased, on Windows the background is not erased
		// and on Linux / GTK the background is erased to gray color.
		// Fill DC with the background on Windows & Linux / GTK.
		auto color = wxSystemSettings::GetColour(wxSYS_COLOUR_3DLIGHT); //GetSystemColour
		dc.SetPen(*new wxPen(color, 1, wxPENSTYLE_SOLID));
		dc.SetBrush(*new wxBrush(color, wxBRUSHSTYLE_SOLID));
		auto rect = GetUpdateRegion().GetBox();
		dc.DrawRectangle(rect.GetLeft(), rect.GetTop(), rect.GetWidth(), rect.GetHeight());
	}

	// turn cw and ch from sizes to max coordinates
	cw--;
	ch--;

	auto cbb = BoundingBoxf(Pointf(0, 0),Pointf(cw, ch));
	// leave space for origin point
	cbb.min.translate(4, 0);
	cbb.max.translate(-4, -4);

	// leave space for origin label
	cbb.max.translate(0, -13);

	// read new size
	cw = cbb.size().x;
	ch = cbb.size().y;

	auto ccenter = cbb.center();

	// get bounding box of bed shape in G - code coordinates
	auto bed_shape = m_bed_shape;
	auto bed_polygon = Polygon::new_scale(m_bed_shape);
	auto bb = BoundingBoxf(m_bed_shape);
	bb.merge(Pointf(0, 0));  // origin needs to be in the visible area
	auto bw = bb.size().x;
	auto bh = bb.size().y;
	auto bcenter = bb.center();

	// calculate the scaling factor for fitting bed shape in canvas area
	auto sfactor = std::min(cw/bw, ch/bh);
	auto shift = Pointf(
		ccenter.x - bcenter.x * sfactor,
		ccenter.y - bcenter.y * sfactor
		);
	m_scale_factor = sfactor;
	m_shift = Pointf(shift.x + cbb.min.x,
					shift.y - (cbb.max.y - GetSize().GetHeight()));

	// draw bed fill
	dc.SetBrush(*new wxBrush(*new wxColour(255, 255, 255), wxSOLID));
	wxPointList pt_list;
	for (auto pt: m_bed_shape)
	{
		Point pt_pix = to_pixels(pt);
		pt_list.push_back(new wxPoint(pt_pix.x, pt_pix.y));
	}
	dc.DrawPolygon(&pt_list, 0, 0);

	// draw grid
	auto step = 10;  // 1cm grid
	Polylines polylines;
	for (auto x = bb.min.x - fmod(bb.min.x, step) + step; x < bb.max.x; x += step) {
		Polyline pl = Polyline::new_scale({ Pointf(x, bb.min.y), Pointf(x, bb.max.y) });
		polylines.push_back(pl);
	}
	for (auto y = bb.min.y - fmod(bb.min.y, step) + step; y < bb.max.y; y += step) {
		polylines.push_back(Polyline::new_scale({ Pointf(bb.min.x, y), Pointf(bb.max.x, y) }));
	}
	polylines = intersection_pl(polylines, bed_polygon);

	dc.SetPen(*new wxPen(*new wxColour(230, 230, 230), 1, wxSOLID));
	for (auto pl : polylines)
	{
		for (size_t i = 0; i < pl.points.size()-1; i++){
			Point pt1 = to_pixels(Pointf::new_unscale(pl.points[i]));
			Point pt2 = to_pixels(Pointf::new_unscale(pl.points[i+1]));
			dc.DrawLine(pt1.x, pt1.y, pt2.x, pt2.y);
		}
	}

	// draw bed contour
	dc.SetPen(*new wxPen(*new wxColour(0, 0, 0), 1, wxSOLID));
	dc.SetBrush(*new wxBrush(*new wxColour(0, 0, 0), wxTRANSPARENT));
	dc.DrawPolygon(&pt_list, 0, 0);

	auto origin_px = to_pixels(Pointf(0, 0));

	// draw axes
	auto axes_len = 50;
	auto arrow_len = 6;
	auto arrow_angle = Geometry::deg2rad(45.0);
	dc.SetPen(*new wxPen(*new wxColour(255, 0, 0), 2, wxSOLID));  // red
	auto x_end = Pointf(origin_px.x + axes_len, origin_px.y);
	dc.DrawLine(wxPoint(origin_px.x, origin_px.y), wxPoint(x_end.x, x_end.y));
	for (auto angle : { -arrow_angle, arrow_angle }){
		auto end = x_end;
		end.translate(-arrow_len, 0);
		end.rotate(angle, x_end);
		dc.DrawLine(wxPoint(x_end.x, x_end.y), wxPoint(end.x, end.y));
	}

	dc.SetPen(*new wxPen(*new wxColour(0, 255, 0), 2, wxSOLID));  // green
	auto y_end = Pointf(origin_px.x, origin_px.y - axes_len);
	dc.DrawLine(wxPoint(origin_px.x, origin_px.y), wxPoint(y_end.x, y_end.y));
	for (auto angle : { -arrow_angle, arrow_angle }) {
		auto end = y_end;
		end.translate(0, +arrow_len);
		end.rotate(angle, y_end);
		dc.DrawLine(wxPoint(y_end.x, y_end.y), wxPoint(end.x, end.y));
	}

	// draw origin
	dc.SetPen(*new wxPen(*new wxColour(0, 0, 0), 1, wxSOLID));
	dc.SetBrush(*new wxBrush(*new wxColour(0, 0, 0), wxSOLID));
	dc.DrawCircle(origin_px.x, origin_px.y, 3);

	dc.SetTextForeground(*new wxColour(0, 0, 0));
	dc.SetFont(*new wxFont(10, wxDEFAULT, wxNORMAL, wxNORMAL));
	dc.DrawText("(0,0)", origin_px.x + 1, origin_px.y + 2);

	// draw current position
	if (m_pos!= Pointf(0, 0)) {
		auto pos_px = to_pixels(m_pos);
		dc.SetPen(*new wxPen(*new wxColour(200, 0, 0), 2, wxSOLID));
		dc.SetBrush(*new wxBrush(*new wxColour(200, 0, 0), wxTRANSPARENT));
		dc.DrawCircle(pos_px.x, pos_px.y, 5);

		dc.DrawLine(pos_px.x - 15, pos_px.y, pos_px.x + 15, pos_px.y);
		dc.DrawLine(pos_px.x, pos_px.y - 15, pos_px.x, pos_px.y + 15);
	}

	m_painted = true;
}

// convert G - code coordinates into pixels
Point Bed_2D::to_pixels(Pointf point){
	auto p = Pointf(point);
	p.scale(m_scale_factor);
	p.translate(m_shift);
	return Point(p.x, GetSize().GetHeight() - p.y); 
}

void Bed_2D::mouse_event(wxMouseEvent event){
	if (!m_interactive) return;
	if (!m_painted) return;

	auto pos = event.GetPosition();
	auto point = to_units(Point(pos.x, pos.y));  
	if (event.LeftDown() || event.Dragging()) {
		if (m_on_move)
			m_on_move(point) ;
		Refresh();
	}
}

// convert pixels into G - code coordinates
Pointf Bed_2D::to_units(Point point){
	auto p = Pointf(point.x, GetSize().GetHeight() - point.y);
	p.translate(m_shift.negative());
	p.scale(1 / m_scale_factor);
	return p;
}

void Bed_2D::set_pos(Pointf pos){
	m_pos = pos;
	Refresh();
}

} // GUI
} // Slic3r