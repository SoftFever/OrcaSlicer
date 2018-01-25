#include "2DBed.hpp";

#include <wx/dcbuffer.h>
#include "BoundingBox.hpp"
#include "Geometry.hpp"

namespace Slic3r {
namespace GUI {

void Bed_2D::repaint()
{
//	auto dc = new wxAutoBufferedPaintDC(this);
	wxClientDC dc(this);
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
		dc.SetPen(/**new wxPen(color, 1, wxPENSTYLE_SOLID)*/*new wxPen(*new wxColour(0, 0, 0), 1, wxSOLID));
		dc.SetBrush(*new wxBrush(color, wxBRUSHSTYLE_SOLID));
		auto rect = GetUpdateRegion().GetBox();
		dc.DrawRectangle(rect.GetLeft(), rect.GetTop(), rect.GetWidth(), rect.GetHeight());
	}

	// turn cw and ch from sizes to max coordinates
/*	cw--;
	ch--;

	auto cbb = BoundingBoxf(Pointf(0, 0),Pointf(cw, ch));
	// leave space for origin point
	cbb.min.translate(4, 0);	// 	cbb.set_x_min(cbb.min.x + 4);
	cbb.max.translate(-4, -4);	// 	cbb.set_x_max(cbb.max.x - 4);cbb.set_y_max(cbb.max.y - 4);

	// leave space for origin label
	cbb.max.translate(0, -13);	//	$cbb->set_y_max($cbb->y_max - 13);

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
//	{
		dc->SetPen(*new wxPen(*new wxColour(0, 0, 0), 1, wxSOLID));
		dc->SetBrush(*new wxBrush(*new wxColour(255, 255, 255), wxSOLID));
		wxPointList pt_list;
		for (auto pt: m_bed_shape)
		{
			Point pt_pix = to_pixels(pt);
			pt_list.push_back(new wxPoint(pt_pix.x, pt_pix.y));
		}
		dc->DrawPolygon(&pt_list, 0, 0);
//	}
*/
	// draw grid
// 	{
// 		auto step = 10;  // 1cm grid
// 		Polylines polylines;
// 		for (auto x = bb.min.x - (bb.min.x % step) + step; x < bb.max.x; x += step) {
// //			push @polylines, Slic3r::Polyline->new_scale([$x, $bb->y_min], [$x, $bb->y_max]);
// 		}
// 		for (auto y = bb.min.y - (bb.min.y % step) + step; y < bb.max.y; y += step) {
// //			push @polylines, Slic3r::Polyline->new_scale([$bb->x_min, $y], [$bb->x_max, $y]);
// 		}
// //		@polylines = @{intersection_pl(\@polylines, [$bed_polygon])};
// 
// 		dc->SetPen(*new wxPen(*new wxColour(230, 230, 230), 1, wxSOLID));
// // 		for (auto pl: polylines)
// // 		dc->DrawLine(map @{$self->to_pixels([map unscale($_), @$_])}, @$_[0, -1]);
// 	}
// 
// 	// draw bed contour
// 	{
// 		dc->SetPen(*new wxPen(*new wxColour(0, 0, 0), 1, wxSOLID));
// 		dc->SetBrush(*new wxBrush(*new wxColour(255, 255, 255), wxTRANSPARENT));
// //		dc->DrawPolygon([map $self->to_pixels($_), @$bed_shape], 0, 0);
// 	}

// 	auto origin_px = to_pixels(Pointf(0, 0));
// 
// 	// draw axes
// 	{
// 		auto axes_len = 50;
// 		auto arrow_len = 6;
// 		auto arrow_angle = Geometry::deg2rad(45);
// 		dc->SetPen(*new wxPen(*new wxColour(255, 0, 0), 2, wxSOLID));  // red
// 		auto x_end = Pointf(origin_px.x + axes_len, origin_px.y);
// 		dc->DrawLine(wxPoint(origin_px), wxPoint(x_end));
// 		foreach my $angle(-$arrow_angle, +$arrow_angle) {
// 			auto end = x_end.clone;
// 			end->translate(-arrow_len, 0);
// 			end->rotate(angle, x_end);
// 			dc->DrawLine(x_end, end);
// 		}
// 
// 		dc->SetPen(*new wxPen(*new wxColour(0, 255, 0), 2, wxSOLID));  // green
// 		auto y_end = Pointf(origin_px.x, origin_px.y - axes_len);
// 		dc->DrawLine(origin_px, y_end);
// 		foreach my $angle(-$arrow_angle, +$arrow_angle) {
// 			auto end = y_end->clone;
// 			end->translate(0, +arrow_len);
// 			end->rotate(angle, y_end);
// 			dc->DrawLine(y_end, end);
// 		}
// 	}
// 
// 	// draw origin
// 	{
// 		dc->SetPen(*new wxPen(*new wxColour(0, 0, 0), 1, wxSOLID));
// 		dc->SetBrush(*new wxBrush(*new wxColour(0, 0, 0), wxSOLID));
// 		dc->DrawCircle(origin_px.x, origin_px.y, 3);
// 
// 		dc->SetTextForeground(*new wxColour(0, 0, 0));
// 		dc->SetFont(*new wxFont(10, wxDEFAULT, wxNORMAL, wxNORMAL));
// 		dc->DrawText("(0,0)", origin_px.x + 1, origin_px.y + 2);
// 	}
// 
// 	// draw current position
// 	if (m_pos) {
// 		auto pos_px = to_pixels(m_pos);
// 		dc->SetPen(*new wxPen(*new wxColour(200, 0, 0), 2, wxSOLID));
// 		dc->SetBrush(*new wxBrush(*new wxColour(200, 0, 0), wxTRANSPARENT));
// 		dc->DrawCircle(pos_px, 5);
// 
// 		dc->DrawLine(pos_px.x - 15, pos_px.y, pos_px.x + 15, pos_px.y);
// 		dc->DrawLine(pos_px.x, pos_px.y - 15, pos_px.x, pos_px.y + 15);
// 	}

	m_painted = true;
}

// convert G - code coordinates into pixels
Point Bed_2D::to_pixels(Pointf point){
	auto p = Pointf(point);
	p.scale(m_scale_factor);
	p.translate(m_shift);
	return Point(p.x, GetSize().GetHeight() - p.y); 
}

} // GUI
} // Slic3r