#include "BedShapeDialog.hpp"

#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/wx.h> 
#include "Polygon.hpp"
#include "BoundingBox.hpp"
#include <wx/numformatter.h>
#include "Model.hpp"
#include "boost/nowide/iostream.hpp"

namespace Slic3r {
namespace GUI {

void BedShapeDialog::build_dialog(ConfigOptionPoints* default_pt)
{
	m_panel = new BedShapePanel(this);
	m_panel->build_panel(default_pt);

	auto main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(m_panel, 1, wxEXPAND);
	main_sizer->Add(CreateButtonSizer(wxOK | wxCANCEL), 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 10);

	SetSizer(main_sizer);
	SetMinSize(GetSize());
	main_sizer->SetSizeHints(this);

	// needed to actually free memory
	this->Bind(wxEVT_CLOSE_WINDOW, ([this](wxCloseEvent e){
		EndModal(wxID_OK);
		Destroy();
	}));
}

void BedShapePanel::build_panel(ConfigOptionPoints* default_pt)
{
//  on_change(nullptr);

	auto box = new wxStaticBox(this, wxID_ANY, _(L("Shape")));
	auto sbsizer = new wxStaticBoxSizer(box, wxVERTICAL);

	// shape options
	m_shape_options_book = new wxChoicebook(this, wxID_ANY, wxDefaultPosition, wxSize(300, -1), wxCHB_TOP);
	sbsizer->Add(m_shape_options_book);

	auto optgroup = init_shape_options_page(_(L("Rectangular")));
		ConfigOptionDef def;
		def.type = coPoints;
		def.default_value = new ConfigOptionPoints{ Pointf(200, 200) };
		def.label = L("Size");
		def.tooltip = L("Size in X and Y of the rectangular plate.");
		Option option(def, "rect_size");
		optgroup->append_single_option_line(option);

		def.type = coPoints;
		def.default_value = new ConfigOptionPoints{ Pointf(0, 0) };
		def.label = L("Origin");
		def.tooltip = L("Distance of the 0,0 G-code coordinate from the front left corner of the rectangle.");
		option = Option(def, "rect_origin");
		optgroup->append_single_option_line(option);

		optgroup = init_shape_options_page(_(L("Circular")));
		def.type = coFloat;
		def.default_value = new ConfigOptionFloat(200);
		def.sidetext = L("mm");
		def.label = L("Diameter");
		def.tooltip = L("Diameter of the print bed. It is assumed that origin (0,0) is located in the center.");
		option = Option(def, "diameter");
		optgroup->append_single_option_line(option);

		optgroup = init_shape_options_page(_(L("Custom")));
		Line line{ "", "" };
		line.full_width = 1;
		line.widget = [this](wxWindow* parent) {
			auto btn = new wxButton(parent, wxID_ANY, _(L("Load shape from STL...")), wxDefaultPosition, wxDefaultSize);
			
			auto sizer = new wxBoxSizer(wxHORIZONTAL);
			sizer->Add(btn);

			btn->Bind(wxEVT_BUTTON, ([this](wxCommandEvent e)
			{
				load_stl();
			}));

			return sizer;
		};
		optgroup->append_line(line);

	Bind(wxEVT_CHOICEBOOK_PAGE_CHANGED, ([this](wxCommandEvent e)
	{
		update_shape();
	}));

	// right pane with preview canvas
	m_canvas = new Bed_2D(this);
	m_canvas->m_bed_shape = default_pt->values;

	// main sizer
	auto top_sizer = new wxBoxSizer(wxHORIZONTAL);
	top_sizer->Add(sbsizer, 0, wxEXPAND | wxLeft | wxTOP | wxBOTTOM, 10);
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
ConfigOptionsGroupShp BedShapePanel::init_shape_options_page(wxString title){

	auto panel = new wxPanel(m_shape_options_book);
	ConfigOptionsGroupShp optgroup;
	optgroup = std::make_shared<ConfigOptionsGroup>(panel, _(L("Settings")));

	optgroup->label_width = 100;
	optgroup->m_on_change = [this](t_config_option_key opt_key, boost::any value){
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
void BedShapePanel::set_shape(ConfigOptionPoints* points)
{
	auto polygon = Polygon::new_scale(points->values);

	// is this a rectangle ?
	if (points->size() == 4) {
		auto lines = polygon.lines();
		if (lines[0].parallel_to(lines[2]) && lines[1].parallel_to(lines[3])) {
			// okay, it's a rectangle
			// find origin
			// the || 0 hack prevents "-0" which might confuse the user
			int x_min, x_max, y_min, y_max;
			x_max = x_min = points->values[0].x;
			y_max = y_min = points->values[0].y;
			for (auto pt : points->values){
				if (x_min > pt.x) x_min = pt.x;
				if (x_max < pt.x) x_max = pt.x;
				if (y_min > pt.y) y_min = pt.y;
				if (y_max < pt.y) y_max = pt.y;
			}
			if (x_min < 0) x_min = 0;
			if (x_max < 0) x_max = 0;
			if (y_min < 0) y_min = 0;
			if (y_max < 0) y_max = 0;
			auto origin = new ConfigOptionPoints{ Pointf(-x_min, -y_min) };

			m_shape_options_book->SetSelection(SHAPE_RECTANGULAR);
			auto optgroup = m_optgroups[SHAPE_RECTANGULAR];
			optgroup->set_value("rect_size", new ConfigOptionPoints{ Pointf(x_max - x_min, y_max - y_min) });//[x_max - x_min, y_max - y_min]);
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
			double distance = center.distance_to(pt);
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
			boost::any ret = wxNumberFormatter::ToString(unscale(avg_dist * 2), 0);
 			optgroup->set_value("diameter", ret);
			update_shape();
			return;
		}
	}

	if (points->size() < 3) {
		// Invalid polygon.Revert to default bed dimensions.
		m_shape_options_book->SetSelection(SHAPE_RECTANGULAR);
		auto optgroup = m_optgroups[SHAPE_RECTANGULAR];
		optgroup->set_value("rect_size", new ConfigOptionPoints{ Pointf(200, 200) });
		optgroup->set_value("rect_origin", new ConfigOptionPoints{ Pointf(0, 0) });
		update_shape();
		return;
	}

	// This is a custom bed shape, use the polygon provided.
	m_shape_options_book->SetSelection(SHAPE_CUSTOM);
	// Copy the polygon to the canvas, make a copy of the array.
	m_canvas->m_bed_shape = points->values;
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
		Pointf rect_size, rect_origin;
		try{
			rect_size = boost::any_cast<Pointf>(m_optgroups[SHAPE_RECTANGULAR]->get_value("rect_size")); }
		catch (const std::exception &e){
			return;}
		try{
			rect_origin = boost::any_cast<Pointf>(m_optgroups[SHAPE_RECTANGULAR]->get_value("rect_origin"));
		}
		catch (const std::exception &e){
			return;}
		
		auto x = rect_size.x;
		auto y = rect_size.y;
		// empty strings or '-' or other things
		if (x == 0 || y == 0)	return;
		double x0 = 0.0;
		double y0 = 0.0;
		double x1 = x;
		double y1 = y;

		auto dx = rect_origin.x;
		auto dy = rect_origin.y;

		x0 -= dx;
		x1 -= dx;
		y0 -= dy;
		y1 -= dy;
		m_canvas->m_bed_shape = {	Pointf(x0, y0),
									Pointf(x1, y0),
									Pointf(x1, y1),
									Pointf(x0, y1)};
	} 
	else if(page_idx == SHAPE_CIRCULAR) {
		double diameter;
		try{
			diameter = boost::any_cast<double>(m_optgroups[SHAPE_CIRCULAR]->get_value("diameter"));
		}
		catch (const std::exception &e){
			return;
		} 
 		if (diameter == 0.0) return ;
		auto r = diameter / 2;
		auto twopi = 2 * PI;
		auto edges = 60;
		std::vector<Pointf> points;
		for (size_t i = 1; i <= 60; ++i){
			auto angle = i * twopi / edges;
			points.push_back(Pointf(r*cos(angle), r*sin(angle)));
		}
		m_canvas->m_bed_shape = points;
	}

//	$self->{on_change}->();
	update_preview();
}

// Loads an stl file, projects it to the XY plane and calculates a polygon.
void BedShapePanel::load_stl()
{
	t_file_wild_card vec_FILE_WILDCARDS = get_file_wild_card();
    std::vector<std::string> file_types = { "known", "stl", "obj", "amf", "3mf", "prusa" };
    wxString MODEL_WILDCARD;
	for (auto file_type: file_types)
		MODEL_WILDCARD += vec_FILE_WILDCARDS.at(file_type) + "|";

	auto dialog = new wxFileDialog(this, _(L("Choose a file to import bed shape from (STL/OBJ/AMF/3MF/PRUSA):")), "", "",
		MODEL_WILDCARD, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (dialog->ShowModal() != wxID_OK) {
		dialog->Destroy();
		return;
	}
	wxArrayString input_file;
	dialog->GetPaths(input_file);
	dialog->Destroy();

	std::string file_name = input_file[0].ToStdString();

	Model model;
	try {
		model = Model::read_from_file(file_name);
	}
	catch (std::exception &e) {
		auto msg = _(L("Error! ")) + file_name + " : " + e.what() + ".";
		show_error(this, msg);
		exit(1);
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
	std::vector<Pointf> points;
	for (auto pt : polygon.points)
		points.push_back(Pointf::new_unscale(pt));
	m_canvas->m_bed_shape = points;
	update_preview();
}

} // GUI
} // Slic3r
