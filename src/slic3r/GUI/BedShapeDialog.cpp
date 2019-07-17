#include "BedShapeDialog.hpp"

#include <wx/wx.h> 
#include <wx/numformatter.h>
#include <wx/sizer.h>
#include <wx/statbox.h>

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Polygon.hpp"

#include "boost/nowide/iostream.hpp"

#include <algorithm>

namespace Slic3r {
namespace GUI {

void BedShapeDialog::build_dialog(const ConfigOptionPoints& default_pt)
{
    SetFont(wxGetApp().normal_font());
	m_panel = new BedShapePanel(this);
	m_panel->build_panel(default_pt);

	auto main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(m_panel, 1, wxEXPAND);
	main_sizer->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 10);

	SetSizer(main_sizer);
	SetMinSize(GetSize());
	main_sizer->SetSizeHints(this);

    this->Bind(wxEVT_CLOSE_WINDOW, ([this](wxCloseEvent& evt) {
        EndModal(wxID_CANCEL);
    }));
}

void BedShapeDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    const int& em = em_unit();
    m_panel->m_shape_options_book->SetMinSize(wxSize(25 * em, -1));

    for (auto og : m_panel->m_optgroups)
        og->msw_rescale();

    const wxSize& size = wxSize(50 * em, -1);

    SetMinSize(size);
    SetSize(size);

    Refresh();
}

void BedShapePanel::build_panel(const ConfigOptionPoints& default_pt)
{
    auto sbsizer = new wxStaticBoxSizer(wxVERTICAL, this, _(L("Shape")));
    sbsizer->GetStaticBox()->SetFont(wxGetApp().bold_font());

	// shape options
    m_shape_options_book = new wxChoicebook(this, wxID_ANY, wxDefaultPosition, 
                           wxSize(25*wxGetApp().em_unit(), -1), wxCHB_TOP);
    sbsizer->Add(m_shape_options_book);

	auto optgroup = init_shape_options_page(_(L("Rectangular")));
	ConfigOptionDef def;
	def.type = coPoints;
	def.set_default_value(new ConfigOptionPoints{ Vec2d(200, 200) });
	def.label = L("Size");
	def.tooltip = L("Size in X and Y of the rectangular plate.");
	Option option(def, "rect_size");
	optgroup->append_single_option_line(option);

	def.type = coPoints;
	def.set_default_value(new ConfigOptionPoints{ Vec2d(0, 0) });
	def.label = L("Origin");
	def.tooltip = L("Distance of the 0,0 G-code coordinate from the front left corner of the rectangle.");
	option = Option(def, "rect_origin");
	optgroup->append_single_option_line(option);

	optgroup = init_shape_options_page(_(L("Circular")));
	def.type = coFloat;
	def.set_default_value(new ConfigOptionFloat(200));
	def.sidetext = L("mm");
	def.label = L("Diameter");
	def.tooltip = L("Diameter of the print bed. It is assumed that origin (0,0) is located in the center.");
	option = Option(def, "diameter");
	optgroup->append_single_option_line(option);

	optgroup = init_shape_options_page(_(L("Custom")));
	Line line{ "", "" };
	line.full_width = 1;
	line.widget = [this](wxWindow* parent) {
        wxButton* shape_btn = new wxButton(parent, wxID_ANY, _(L("Load shape from STL...")));
        wxSizer* shape_sizer = new wxBoxSizer(wxHORIZONTAL);
        shape_sizer->Add(shape_btn, 1, wxEXPAND);

        wxSizer* sizer = new wxBoxSizer(wxVERTICAL);
        sizer->Add(shape_sizer, 1, wxEXPAND);

        shape_btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent& e)
        {
			load_stl();
		}));

		return sizer;
	};
	optgroup->append_line(line);

    Bind(wxEVT_CHOICEBOOK_PAGE_CHANGED, ([this](wxCommandEvent& e)
    {
		update_shape();
	}));

	// right pane with preview canvas
	m_canvas = new Bed_2D(this);
    m_canvas->m_bed_shape = default_pt.values;

	// main sizer
	auto top_sizer = new wxBoxSizer(wxHORIZONTAL);
    top_sizer->Add(sbsizer, 0, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, 10);
    if (m_canvas)
		top_sizer->Add(m_canvas, 1, wxEXPAND | wxALL, 10) ;

	SetSizerAndFit(top_sizer);

	set_shape(default_pt);
	update_preview();
}

#define SHAPE_RECTANGULAR	0
#define SHAPE_CIRCULAR		1
#define SHAPE_CUSTOM		2

// Called from the constructor.
// Create a panel for a rectangular / circular / custom bed shape.
ConfigOptionsGroupShp BedShapePanel::init_shape_options_page(const wxString& title)
{
    wxPanel* panel = new wxPanel(m_shape_options_book);
    ConfigOptionsGroupShp optgroup = std::make_shared<ConfigOptionsGroup>(panel, _(L("Settings")));

    optgroup->label_width = 10;
    optgroup->m_on_change = [this](t_config_option_key opt_key, boost::any value) {
        update_shape();
    };
	
    m_optgroups.push_back(optgroup);
    panel->SetSizerAndFit(optgroup->sizer);
    m_shape_options_book->AddPage(panel, title);

    return optgroup;
}

// Called from the constructor.
// Set the initial bed shape from a list of points.
// Deduce the bed shape type(rect, circle, custom)
// This routine shall be smart enough if the user messes up
// with the list of points in the ini file directly.
void BedShapePanel::set_shape(const ConfigOptionPoints& points)
{
    auto polygon = Polygon::new_scale(points.values);

	// is this a rectangle ?
    if (points.size() == 4) {
        auto lines = polygon.lines();
		if (lines[0].parallel_to(lines[2]) && lines[1].parallel_to(lines[3])) {
			// okay, it's a rectangle
			// find origin
            coordf_t x_min, x_max, y_min, y_max;
            x_max = x_min = points.values[0](0);
            y_max = y_min = points.values[0](1);
            for (auto pt : points.values)
            {
                x_min = std::min(x_min, pt(0));
                x_max = std::max(x_max, pt(0));
                y_min = std::min(y_min, pt(1));
                y_max = std::max(y_max, pt(1));
            }

            auto origin = new ConfigOptionPoints{ Vec2d(-x_min, -y_min) };

			m_shape_options_book->SetSelection(SHAPE_RECTANGULAR);
			auto optgroup = m_optgroups[SHAPE_RECTANGULAR];
			optgroup->set_value("rect_size", new ConfigOptionPoints{ Vec2d(x_max - x_min, y_max - y_min) });//[x_max - x_min, y_max - y_min]);
			optgroup->set_value("rect_origin", origin);
			update_shape();
			return;
		}
	}

	// is this a circle ?
	{
		// Analyze the array of points.Do they reside on a circle ?
		auto center = polygon.bounding_box().center();
		std::vector<double> vertex_distances;
		double avg_dist = 0;
		for (auto pt: polygon.points)
		{
			double distance = (pt - center).cast<double>().norm();
			vertex_distances.push_back(distance);
			avg_dist += distance;
		}

		avg_dist /= vertex_distances.size();
		bool defined_value = true;
		for (auto el: vertex_distances)
		{
			if (abs(el - avg_dist) > 10 * SCALED_EPSILON)
				defined_value = false;
			break;
		}
		if (defined_value) {
			// all vertices are equidistant to center
			m_shape_options_book->SetSelection(SHAPE_CIRCULAR);
			auto optgroup = m_optgroups[SHAPE_CIRCULAR];
			boost::any ret = wxNumberFormatter::ToString(unscale<double>(avg_dist * 2), 0);
 			optgroup->set_value("diameter", ret);
			update_shape();
			return;
		}
	}

    if (points.size() < 3) {
        // Invalid polygon.Revert to default bed dimensions.
		m_shape_options_book->SetSelection(SHAPE_RECTANGULAR);
		auto optgroup = m_optgroups[SHAPE_RECTANGULAR];
		optgroup->set_value("rect_size", new ConfigOptionPoints{ Vec2d(200, 200) });
		optgroup->set_value("rect_origin", new ConfigOptionPoints{ Vec2d(0, 0) });
		update_shape();
		return;
	}

	// This is a custom bed shape, use the polygon provided.
	m_shape_options_book->SetSelection(SHAPE_CUSTOM);
	// Copy the polygon to the canvas, make a copy of the array.
    m_loaded_bed_shape = points.values;
    update_shape();
}

void BedShapePanel::update_preview()
{
	if (m_canvas) m_canvas->Refresh();
	Refresh();
}

// Update the bed shape from the dialog fields.
void BedShapePanel::update_shape()
{
	auto page_idx = m_shape_options_book->GetSelection();
	if (page_idx == SHAPE_RECTANGULAR) {
		Vec2d rect_size(Vec2d::Zero());
		Vec2d rect_origin(Vec2d::Zero());
		try{
			rect_size = boost::any_cast<Vec2d>(m_optgroups[SHAPE_RECTANGULAR]->get_value("rect_size")); }
		catch (const std::exception & /* e */) {
			return;
		}
		try {
			rect_origin = boost::any_cast<Vec2d>(m_optgroups[SHAPE_RECTANGULAR]->get_value("rect_origin"));
		}
		catch (const std::exception & /* e */) {
			return;
		}
		
		auto x = rect_size(0);
		auto y = rect_size(1);
		// empty strings or '-' or other things
		if (x == 0 || y == 0)	return;
		double x0 = 0.0;
		double y0 = 0.0;
		double x1 = x;
		double y1 = y;

		auto dx = rect_origin(0);
		auto dy = rect_origin(1);

		x0 -= dx;
		x1 -= dx;
		y0 -= dy;
		y1 -= dy;
		m_canvas->m_bed_shape = {	Vec2d(x0, y0),
									Vec2d(x1, y0),
									Vec2d(x1, y1),
									Vec2d(x0, y1)};
	} 
	else if(page_idx == SHAPE_CIRCULAR) {
		double diameter;
		try{
			diameter = boost::any_cast<double>(m_optgroups[SHAPE_CIRCULAR]->get_value("diameter"));
		}
		catch (const std::exception & /* e */) {
			return;
		} 
 		if (diameter == 0.0) return ;
		auto r = diameter / 2;
		auto twopi = 2 * PI;
		auto edges = 60;
		std::vector<Vec2d> points;
		for (size_t i = 1; i <= 60; ++i) {
			auto angle = i * twopi / edges;
			points.push_back(Vec2d(r*cos(angle), r*sin(angle)));
		}
		m_canvas->m_bed_shape = points;
	}
    else if (page_idx == SHAPE_CUSTOM) 
        m_canvas->m_bed_shape = m_loaded_bed_shape;

	update_preview();
}

// Loads an stl file, projects it to the XY plane and calculates a polygon.
void BedShapePanel::load_stl()
{
    wxFileDialog dialog(this, _(L("Choose a file to import bed shape from (STL/OBJ/AMF/3MF/PRUSA):")), "", "",
        file_wildcards(FT_MODEL), wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (dialog.ShowModal() != wxID_OK)
        return;

    wxArrayString input_file;
    dialog.GetPaths(input_file);

	std::string file_name = input_file[0].ToUTF8().data();

	Model model;
	try {
		model = Model::read_from_file(file_name);
	}
	catch (std::exception &) {
        show_error(this, _(L("Error! Invalid model")));
        return;
    }

	auto mesh = model.mesh();
	auto expolygons = mesh.horizontal_projection();

	if (expolygons.size() == 0) {
		show_error(this, _(L("The selected file contains no geometry.")));
		return;
	}
	if (expolygons.size() > 1) {
		show_error(this, _(L("The selected file contains several disjoint areas. This is not supported.")));
		return;
	}

	auto polygon = expolygons[0].contour;
	std::vector<Vec2d> points;
	for (auto pt : polygon.points)
		points.push_back(unscale(pt));

    m_loaded_bed_shape = points;
    update_shape();
}

} // GUI
} // Slic3r
