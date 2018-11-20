#include "2DBed.hpp"

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

	auto cbb = BoundingBoxf(Vec2d(0, 0),Vec2d(cw, ch));
	// leave space for origin point
	cbb.min(0) += 4;
	cbb.max -= Vec2d(4., 4.);

	// leave space for origin label
	cbb.max(1) -= 13;

	// read new size
	cw = cbb.size()(0);
	ch = cbb.size()(1);

	auto ccenter = cbb.center();

	// get bounding box of bed shape in G - code coordinates
	auto bed_shape = m_bed_shape;
	auto bed_polygon = Polygon::new_scale(m_bed_shape);
	auto bb = BoundingBoxf(m_bed_shape);
	bb.merge(Vec2d(0, 0));  // origin needs to be in the visible area
	auto bw = bb.size()(0);
	auto bh = bb.size()(1);
	auto bcenter = bb.center();

	// calculate the scaling factor for fitting bed shape in canvas area
	auto sfactor = std::min(cw/bw, ch/bh);
	auto shift = Vec2d(
		ccenter(0) - bcenter(0) * sfactor,
		ccenter(1) - bcenter(1) * sfactor
		);
	m_scale_factor = sfactor;
	m_shift = Vec2d(shift(0) + cbb.min(0),
					shift(1) - (cbb.max(1) - GetSize().GetHeight()));

	// draw bed fill
	dc.SetBrush(wxBrush(wxColour(255, 255, 255), wxSOLID));
	wxPointList pt_list;
	for (auto pt: m_bed_shape)
	{
		Point pt_pix = to_pixels(pt);
		pt_list.push_back(new wxPoint(pt_pix(0), pt_pix(1)));
	}
	dc.DrawPolygon(&pt_list, 0, 0);

	// draw grid
	auto step = 10;  // 1cm grid
	Polylines polylines;
	for (auto x = bb.min(0) - fmod(bb.min(0), step) + step; x < bb.max(0); x += step) {
		polylines.push_back(Polyline::new_scale({ Vec2d(x, bb.min(1)), Vec2d(x, bb.max(1)) }));
	}
	for (auto y = bb.min(1) - fmod(bb.min(1), step) + step; y < bb.max(1); y += step) {
		polylines.push_back(Polyline::new_scale({ Vec2d(bb.min(0), y), Vec2d(bb.max(0), y) }));
	}
	polylines = intersection_pl(polylines, bed_polygon);

	dc.SetPen(wxPen(wxColour(230, 230, 230), 1, wxSOLID));
	for (auto pl : polylines)
	{
		for (size_t i = 0; i < pl.points.size()-1; i++) {
			Point pt1 = to_pixels(unscale(pl.points[i]));
			Point pt2 = to_pixels(unscale(pl.points[i+1]));
			dc.DrawLine(pt1(0), pt1(1), pt2(0), pt2(1));
		}
	}

	// draw bed contour
	dc.SetPen(wxPen(wxColour(0, 0, 0), 1, wxSOLID));
	dc.SetBrush(wxBrush(wxColour(0, 0, 0), wxTRANSPARENT));
	dc.DrawPolygon(&pt_list, 0, 0);

	auto origin_px = to_pixels(Vec2d(0, 0));

	// draw axes
	auto axes_len = 50;
	auto arrow_len = 6;
	auto arrow_angle = Geometry::deg2rad(45.0);
	dc.SetPen(wxPen(wxColour(255, 0, 0), 2, wxSOLID));  // red
	auto x_end = Vec2d(origin_px(0) + axes_len, origin_px(1));
	dc.DrawLine(wxPoint(origin_px(0), origin_px(1)), wxPoint(x_end(0), x_end(1)));
	for (auto angle : { -arrow_angle, arrow_angle }) {
		auto end = Eigen::Translation2d(x_end) * Eigen::Rotation2Dd(angle) * Eigen::Translation2d(- x_end) * Eigen::Vector2d(x_end(0) - arrow_len, x_end(1));
		dc.DrawLine(wxPoint(x_end(0), x_end(1)), wxPoint(end(0), end(1)));
	}

	dc.SetPen(wxPen(wxColour(0, 255, 0), 2, wxSOLID));  // green
	auto y_end = Vec2d(origin_px(0), origin_px(1) - axes_len);
	dc.DrawLine(wxPoint(origin_px(0), origin_px(1)), wxPoint(y_end(0), y_end(1)));
	for (auto angle : { -arrow_angle, arrow_angle }) {
		auto end = Eigen::Translation2d(y_end) * Eigen::Rotation2Dd(angle) * Eigen::Translation2d(- y_end) * Eigen::Vector2d(y_end(0), y_end(1) + arrow_len);
		dc.DrawLine(wxPoint(y_end(0), y_end(1)), wxPoint(end(0), end(1)));
	}

	// draw origin
	dc.SetPen(wxPen(wxColour(0, 0, 0), 1, wxSOLID));
	dc.SetBrush(wxBrush(wxColour(0, 0, 0), wxSOLID));
	dc.DrawCircle(origin_px(0), origin_px(1), 3);

	static const auto origin_label = wxString("(0,0)");
	dc.SetTextForeground(wxColour(0, 0, 0));
	dc.SetFont(wxFont(10, wxDEFAULT, wxNORMAL, wxNORMAL));
	auto extent = dc.GetTextExtent(origin_label);
	const auto origin_label_x = origin_px(0) <= cw / 2 ? origin_px(0) + 1 : origin_px(0) - 1 - extent.GetWidth();
	const auto origin_label_y = origin_px(1) <= ch / 2 ? origin_px(1) + 1 : origin_px(1) - 1 - extent.GetHeight();
	dc.DrawText(origin_label, origin_label_x, origin_label_y);

	// draw current position
	if (m_pos!= Vec2d(0, 0)) {
		auto pos_px = to_pixels(m_pos);
		dc.SetPen(wxPen(wxColour(200, 0, 0), 2, wxSOLID));
		dc.SetBrush(wxBrush(wxColour(200, 0, 0), wxTRANSPARENT));
		dc.DrawCircle(pos_px(0), pos_px(1), 5);

		dc.DrawLine(pos_px(0) - 15, pos_px(1), pos_px(0) + 15, pos_px(1));
		dc.DrawLine(pos_px(0), pos_px(1) - 15, pos_px(0), pos_px(1) + 15);
	}

	m_painted = true;
}

// convert G - code coordinates into pixels
Point Bed_2D::to_pixels(Vec2d point)
{
	auto p = point * m_scale_factor + m_shift;
	return Point(p(0), GetSize().GetHeight() - p(1)); 
}

void Bed_2D::mouse_event(wxMouseEvent event)
{
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
Vec2d Bed_2D::to_units(Point point)
{
	return (Vec2d(point(0), GetSize().GetHeight() - point(1)) - m_shift) * (1. / m_scale_factor);
}

void Bed_2D::set_pos(Vec2d pos)
{
	m_pos = pos;
	Refresh();
}

} // GUI
} // Slic3r