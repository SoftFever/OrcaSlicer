#include "2DBed.hpp"
#include "GUI_App.hpp"

#include "3DBed.hpp"
#include "PartPlate.hpp"

#include <wx/dcbuffer.h>

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/ClipperUtils.hpp"

namespace Slic3r {
namespace GUI {


Bed_2D::Bed_2D(wxWindow* parent) : 
wxPanel(parent, wxID_ANY, wxDefaultPosition, wxSize(25 * wxGetApp().em_unit(), -1), wxTAB_TRAVERSAL)
{
#ifdef __APPLE__
    m_user_drawn_background = false;
#else
    SetBackgroundStyle(wxBG_STYLE_PAINT); // to avoid assert message after wxAutoBufferedPaintDC 
#endif /*__APPLE__*/
}

void Bed_2D::repaint(const std::vector<Vec2d>& shape)
{
	wxAutoBufferedPaintDC dc(this);
	auto cw = GetSize().GetWidth();
	auto ch = GetSize().GetHeight();
	// when canvas is not rendered yet, size is 0, 0
	if (cw == 0) return ; 
    bool is_dark = wxGetApp().dark_mode();

	if (m_user_drawn_background) {
		// On all systems the AutoBufferedPaintDC() achieves double buffering.
		// On MacOS the background is erased, on Windows the background is not erased
		// and on Linux / GTK the background is erased to gray color.
		// Fill DC with the background on Windows & Linux / GTK.
		wxColour color;
		if (is_dark) {// SetBackgroundColour
			color = wxColour(45, 45, 49);
		}
		else {
			color = *wxWHITE;
		}
		dc.SetPen(*new wxPen(color, 1, wxPENSTYLE_SOLID));
		dc.SetBrush(*new wxBrush(color, wxBRUSHSTYLE_SOLID));
		auto rect = GetUpdateRegion().GetBox();
		dc.DrawRectangle(rect.GetLeft(), rect.GetTop(), rect.GetWidth(), rect.GetHeight());
	}

    if (shape.empty())
        return;

    // reduce size to have some space around the drawn shape
    cw -= (2 * Border);
    ch -= (2 * Border);

	auto cbb = BoundingBoxf(Vec2d(0, 0),Vec2d(cw, ch));
	auto ccenter = cbb.center();

	// get bounding box of bed shape in G - code coordinates
    auto bed_polygon = Polygon::new_scale(shape);
    auto bb = BoundingBoxf(shape);
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
    m_shift = Vec2d(shift(0) + cbb.min(0), shift(1) - (cbb.max(1) - ch));

    // ORCA match colors
    ColorRGBA   bed_rgba   = is_dark ? Bed3D::DEFAULT_MODEL_COLOR_DARK    : Bed3D::DEFAULT_MODEL_COLOR;
    std::string bed_color  = encode_color(ColorRGBA(bed_rgba[0] * 0.8f, bed_rgba[1] * 0.8f,bed_rgba[2] * 0.8f, bed_rgba[3]));
    ColorRGBA   grid_color = is_dark ? PartPlate::LINE_TOP_SEL_DARK_COLOR : PartPlate::LINE_TOP_SEL_COLOR;
    std::string lines_bold_color = encode_color(grid_color);
    std::string lines_thin_color = encode_color(grid_color * 0.85);
    wxColour    text_color = wxColour(lines_bold_color);

	// draw bed fill
	dc.SetBrush(wxBrush(wxColour(bed_color), wxBRUSHSTYLE_SOLID));
	wxPointList pt_list;
    for (auto pt : shape)
    {
        Point pt_pix = to_pixels(pt, ch);
        pt_list.push_back(new wxPoint(pt_pix(0), pt_pix(1)));
	}
	dc.DrawPolygon(&pt_list, 0, 0);

	// draw grid
	Polylines lines_thin, lines_bold; // Calculations should be same with PartPlate::calc_gridlines
	int count = 0;
	int step  = 10;                   //                Uses up to 599mm    Main Grid: 10 x 5 = 50mm
	// Orca: use 500 x 500 bed size as baseline.
    Vec2d grid_counts = bb.size() / (step * 50);
    // if the grid is too dense, we increase the step
    auto min_edge_scaled = (bb.size()).minCoeff(); // calculations should match with PartPlate.cpp
    if (     min_edge_scaled >= 6000) // Switch when short edge >= 6000mm   Main Grid: 100 x 5 = 500mm
        step = 100;
    else if (min_edge_scaled >= 1200) // Switch when short edge >= 1200mm   Main Grid:  50 x 5 = 250mm
        step = 50;
    else if (min_edge_scaled >= 600)  // Switch when short edge >= 600mm    Main Grid:  20 x 5 = 100mm
        step = 20;

    // ORCA draw grid lines relative to origin
    for (auto x = m_pos.x(); x >= bb.min(0); x -= step) { // Negative X axis
        (count % 5 == 0 ? lines_bold : lines_thin).push_back(Polyline::new_scale({
            Vec2d(x, bb.min(1)),
            Vec2d(x, bb.max(1))
        }));
        count ++;
    }
    count = 0;
    for (auto x = m_pos.x(); x <= bb.max(0); x += step) { // Positive X axis
        (count % 5 == 0 ? lines_bold : lines_thin).push_back(Polyline::new_scale({
            Vec2d(x, bb.min(1)),
            Vec2d(x, bb.max(1))
        }));
        count ++;
    }
    count = 0;
    for (auto y = m_pos.y(); y >= bb.min(1); y -= step) { // Negative Y axis
        (count % 5 == 0 ? lines_bold : lines_thin).push_back(Polyline::new_scale({
            Vec2d(bb.min(0), y),
            Vec2d(bb.max(0), y)
        }));
        count ++;
    }
    count = 0;
    for (auto y = m_pos.y(); y <= bb.max(1); y += step) { // Positive Y axis
        (count % 5 == 0 ? lines_bold : lines_thin).push_back(Polyline::new_scale({
            Vec2d(bb.min(0), y),
            Vec2d(bb.max(0), y)
        }));
        count ++;
    }
    count = 0;

    // clip with a slightly grown expolygon because our lines lay on the contours and may get erroneously clipped
    dc.SetPen(wxPen(wxColour(lines_thin_color), 1, wxPENSTYLE_SOLID));
	for (auto pl : intersection_pl(lines_thin, bed_polygon)) {
		for (size_t i = 0; i < pl.points.size()-1; i++) {
            Point pt1 = to_pixels(unscale(pl.points[i])    , ch);
            Point pt2 = to_pixels(unscale(pl.points[i + 1]), ch);
            dc.DrawLine(pt1(0), pt1(1), pt2(0), pt2(1));
		}
	}
    dc.SetPen(wxPen(wxColour(lines_bold_color), 1, wxPENSTYLE_SOLID));
	for (auto pl : intersection_pl(lines_bold, bed_polygon)) {
		for (size_t i = 0; i < pl.points.size()-1; i++) {
            Point pt1 = to_pixels(unscale(pl.points[i])    , ch);
            Point pt2 = to_pixels(unscale(pl.points[i + 1]), ch);
            dc.DrawLine(pt1(0), pt1(1), pt2(0), pt2(1));
		}
	}

	// draw bed contour
    dc.SetPen(  wxPen(  wxColour(lines_bold_color), 1, wxPENSTYLE_SOLID));
	dc.SetBrush(wxBrush(wxColour(lines_bold_color), wxBRUSHSTYLE_TRANSPARENT));
	dc.DrawPolygon(&pt_list, 0, 0);

    auto origin_px = to_pixels(Vec2d(0, 0), ch);

	// draw axes
	auto axes_len = 5 * wxGetApp().em_unit(); // scale axis
	auto arrow_len = 6;
	auto arrow_angle = Geometry::deg2rad(45.0);
    dc.SetPen(wxPen(wxColour(255, 0, 0), 2, wxPENSTYLE_SOLID));  // red
	auto x_end = Vec2d(origin_px(0) + axes_len, origin_px(1));
	dc.DrawLine(wxPoint(origin_px(0), origin_px(1)), wxPoint(x_end(0), x_end(1)));
	for (auto angle : { -arrow_angle, arrow_angle }) {
		Vec2d end = Eigen::Translation2d(x_end) * Eigen::Rotation2Dd(angle) * Eigen::Translation2d(- x_end) * Eigen::Vector2d(x_end(0) - arrow_len, x_end(1));
		dc.DrawLine(wxPoint(x_end(0), x_end(1)), wxPoint(end(0), end(1)));
	}

    dc.SetPen(wxPen(wxColour(0, 255, 0), 2, wxPENSTYLE_SOLID));  // green
	auto y_end = Vec2d(origin_px(0), origin_px(1) - axes_len);
	dc.DrawLine(wxPoint(origin_px(0), origin_px(1)), wxPoint(y_end(0), y_end(1)));
	for (auto angle : { -arrow_angle, arrow_angle }) {
		Vec2d end = Eigen::Translation2d(y_end) * Eigen::Rotation2Dd(angle) * Eigen::Translation2d(- y_end) * Eigen::Vector2d(y_end(0), y_end(1) + arrow_len);
		dc.DrawLine(wxPoint(y_end(0), y_end(1)), wxPoint(end(0), end(1)));
	}

	// draw origin
    dc.SetPen(wxPen(wxColour(0, 0, 0), 1, wxPENSTYLE_SOLID));
    dc.SetBrush(wxBrush(wxColour(0, 0, 0), wxBRUSHSTYLE_SOLID));
	dc.DrawCircle(origin_px(0), origin_px(1), 3);

	static const auto origin_label = wxString("(0,0)");
	dc.SetTextForeground(wxColour("#FFFFFF"));
    dc.SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
	auto extent = dc.GetTextExtent(origin_label);
	const auto origin_label_x = origin_px(0) + 2;                       // ORCA always draw (0,0) text in axes bounding box
	const auto origin_label_y = origin_px(1) - extent.GetHeight() - 2;
    dc.SetPen(  wxPen(  wxColour(wxColour(bed_color)), 1, wxPENSTYLE_SOLID));
    dc.SetBrush(wxBrush(wxColour(wxColour(bed_color)), wxBRUSHSTYLE_SOLID));
    dc.DrawRectangle(wxPoint(origin_label_x, origin_label_y), extent);  // ORCA draw a background to origin position text to improve readability when overlaps with grid
	dc.DrawText(origin_label, origin_label_x, origin_label_y);

    // ORCA add grid size value as information for large scale beds
    auto grid_label = wxString("1x1 Grid: " + std::to_string(step) + " mm");
    Point draw_bb = to_pixels(Vec2d(
        std::min(m_pos(0),bb.min(0)),
        std::min(m_pos(1),bb.min(1))
    ),ch);
    dc.SetTextForeground(wxColour(StateColor::darkModeColorFor("#262E30")));
    dc.SetFont(wxFont(10, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
    dc.DrawText(grid_label, draw_bb(0), draw_bb(1) + 5);

	// draw current position
	if (m_pos!= Vec2d(0, 0)) {
        auto pos_px = to_pixels(m_pos, ch);
        dc.SetPen(wxPen(wxColour(200, 0, 0), 2, wxPENSTYLE_SOLID));
        dc.SetBrush(wxBrush(wxColour(200, 0, 0), wxBRUSHSTYLE_TRANSPARENT));
		dc.DrawCircle(pos_px(0), pos_px(1), 5);

		dc.DrawLine(pos_px(0) - 15, pos_px(1), pos_px(0) + 15, pos_px(1));
		dc.DrawLine(pos_px(0), pos_px(1) - 15, pos_px(0), pos_px(1) + 15);
	}
}


// convert G - code coordinates into pixels
Point Bed_2D::to_pixels(const Vec2d& point, int height)
{
	auto p = point * m_scale_factor + m_shift;
    return Point(p(0) + Border, height - p(1) + Border);
}

void Bed_2D::set_pos(const Vec2d& pos)
{
	m_pos = pos;
	Refresh();
}

} // GUI
} // Slic3r
