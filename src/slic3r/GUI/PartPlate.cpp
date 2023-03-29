#include <cstddef>
#include <algorithm>
#include <numeric>
#include <vector>
#include <string>
#include <regex>
#include <future>
#include <GL/glew.h>
#include <boost/algorithm/string.hpp>
#include <boost/optional.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Tesselate.hpp"
#include "libslic3r/GCode/ThumbnailData.hpp"
#include "libslic3r/Utils.hpp"

#include "I18N.hpp"
#include "GUI_App.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "BackgroundSlicingProcess.hpp"
#include "Widgets/Label.hpp"
#include "3DBed.hpp"
#include "PartPlate.hpp"
#include "Camera.hpp"
#include "GUI_Colors.hpp"
#include "GUI_ObjectList.hpp"
#include "Tab.hpp"
#include "format.hpp"
#include <imgui/imgui_internal.h>

using boost::optional;
namespace fs = boost::filesystem;

static const float GROUND_Z = -0.03f;
static const float GRABBER_X_FACTOR = 0.20f;
static const float GRABBER_Y_FACTOR = 0.03f;
static const float GRABBER_Z_VALUE = 0.5f;
static unsigned int GLOBAL_PLATE_INDEX = 0;

static const double LOGICAL_PART_PLATE_GAP = 1. / 5.;
static const int PARTPLATE_ICON_SIZE = 16;
static const int PARTPLATE_ICON_GAP_TOP = 3;
static const int PARTPLATE_ICON_GAP_LEFT = 3;
static const int PARTPLATE_ICON_GAP_Y = 5;
static const int PARTPLATE_TEXT_OFFSET_X1 = 3;
static const int PARTPLATE_TEXT_OFFSET_X2 = 1;
static const int PARTPLATE_TEXT_OFFSET_Y = 1;


namespace Slic3r {
namespace GUI {

class Bed3D;

std::array<float, 4> PartPlate::SELECT_COLOR		= { 0.2666f, 0.2784f, 0.2784f, 1.0f }; //{ 0.4196f, 0.4235f, 0.4235f, 1.0f };
std::array<float, 4> PartPlate::UNSELECT_COLOR		= { 0.82f, 0.82f, 0.82f, 1.0f };
std::array<float, 4> PartPlate::UNSELECT_DARK_COLOR		= { 0.384f, 0.384f, 0.412f, 1.0f };
std::array<float, 4> PartPlate::DEFAULT_COLOR		= { 0.5f, 0.5f, 0.5f, 1.0f };
std::array<float, 4> PartPlate::LINE_TOP_COLOR		= { 0.89f, 0.89f, 0.89f, 1.0f };
std::array<float, 4> PartPlate::LINE_TOP_DARK_COLOR		= { 0.431f, 0.431f, 0.463f, 1.0f };
std::array<float, 4> PartPlate::LINE_TOP_SEL_COLOR  = { 0.5294f, 0.5451, 0.5333f, 1.0f};
std::array<float, 4> PartPlate::LINE_TOP_SEL_DARK_COLOR = { 0.298f, 0.298f, 0.3333f, 1.0f};
std::array<float, 4> PartPlate::LINE_BOTTOM_COLOR	= { 0.8f, 0.8f, 0.8f, 0.4f };
std::array<float, 4> PartPlate::HEIGHT_LIMIT_TOP_COLOR		= { 0.6f, 0.6f, 1.0f, 1.0f };
std::array<float, 4> PartPlate::HEIGHT_LIMIT_BOTTOM_COLOR	= { 0.4f, 0.4f, 1.0f, 1.0f };

// get text extent with wxMemoryDC
void get_text_extent(const wxString &msg, wxCoord &w, wxCoord &h, wxFont *font)
{
  wxMemoryDC memDC;
  if (font)
    memDC.SetFont(*font);
  memDC.GetTextExtent(msg, &w, &h);
}


wxFont* find_font(const std::string& text_str, int max_size = 32)
{
  auto is_font_suitable = [](std::string str, wxFont &font, int max_size) {
    wxString msg(str);
	wxCoord w, h;
	get_text_extent(msg, w, h, &font);

    if (w <= max_size)
      return true;
    else
      return false;
  };
  wxFont *font = nullptr;
  if (is_font_suitable(text_str, Label::Head_24, max_size))
    font = &Label::Head_24;
  else if (is_font_suitable(text_str, Label::Head_20, max_size))
    font = &Label::Head_20;
  else if (is_font_suitable(text_str, Label::Head_18, max_size))
    font = &Label::Head_18;
  else if (is_font_suitable(text_str, Label::Head_16, max_size))
    font = &Label::Head_16;
  else if (is_font_suitable(text_str, Label::Head_14, max_size))
    font = &Label::Head_14;
  else
    font = &Label::Head_12;

  return font;
}
void PartPlate::update_render_colors()
{
	PartPlate::SELECT_COLOR			= GLColor(RenderColor::colors[RenderCol_Plate_Selected]);
	PartPlate::UNSELECT_COLOR		= GLColor(RenderColor::colors[RenderCol_Plate_Unselected]);
	PartPlate::DEFAULT_COLOR		= GLColor(RenderColor::colors[RenderCol_Plate_Default]);
	PartPlate::LINE_TOP_COLOR		= GLColor(RenderColor::colors[RenderCol_Plate_Line_Top]);
	PartPlate::LINE_BOTTOM_COLOR	= GLColor(RenderColor::colors[RenderCol_Plate_Line_Bottom]);
}

void PartPlate::load_render_colors()
{
	RenderColor::colors[RenderCol_Plate_Selected] = IMColor(SELECT_COLOR);
	RenderColor::colors[RenderCol_Plate_Unselected] = IMColor(UNSELECT_COLOR);
	RenderColor::colors[RenderCol_Plate_Default] = IMColor(DEFAULT_COLOR);
	RenderColor::colors[RenderCol_Plate_Line_Top] = IMColor(LINE_TOP_COLOR);
	RenderColor::colors[RenderCol_Plate_Line_Bottom] = IMColor(LINE_BOTTOM_COLOR);
}


PartPlate::PartPlate()
	: ObjectBase(-1), m_plater(nullptr), m_model(nullptr), m_quadric(nullptr)
{
	assert(this->id().invalid());
	init();
}

PartPlate::PartPlate(PartPlateList *partplate_list, Vec3d origin, int width, int depth, int height, Plater* platerObj, Model* modelObj, bool printable, PrinterTechnology tech)
	:m_partplate_list(partplate_list), m_plater(platerObj), m_model(modelObj), printer_technology(tech), m_origin(origin), m_width(width), m_depth(depth), m_height(height),  m_printable(printable)
{
	init();
}

PartPlate::~PartPlate()
{
	clear();
	//if (m_quadric != nullptr)
	//	::gluDeleteQuadric(m_quadric);
	release_opengl_resource();

	//boost::nowide::remove(m_tmp_gcode_path.c_str());
}

void PartPlate::init()
{
	m_locked = false;
	m_ready_for_slice = true;
	m_slice_result_valid = false;
	m_slice_percent = 0.0f;
	m_hover_id = -1;
	m_selected = false;
	//m_quadric = ::gluNewQuadric();
	//if (m_quadric != nullptr)
	//	::gluQuadricDrawStyle(m_quadric, GLU_FILL);

	m_print_index = -1;
	m_print = nullptr;
	m_plate_name_vbo_id = 0;
}

BedType PartPlate::get_bed_type() const
{
	std::string bed_type_key = "curr_bed_type";

	// should be called in GUI context
	assert(m_plater != nullptr);
	if (m_config.has(bed_type_key)) {
		BedType bed_type = m_config.opt_enum<BedType>(bed_type_key);
		return bed_type;
	}

	return btDefault;
}

void PartPlate::set_bed_type(BedType bed_type)
{
    std::string bed_type_key = "curr_bed_type";

    // should be called in GUI context
    assert(m_plater != nullptr);

    // update slice state
    BedType old_real_bed_type = get_bed_type();
    if (old_real_bed_type == btDefault) {
        DynamicConfig& proj_cfg = wxGetApp().preset_bundle->project_config;
        if (proj_cfg.has(bed_type_key))
            old_real_bed_type = proj_cfg.opt_enum<BedType>(bed_type_key);
    }
    BedType new_real_bed_type = bed_type;
    if (bed_type == BedType::btDefault) {
        DynamicConfig& proj_cfg = wxGetApp().preset_bundle->project_config;
        if (proj_cfg.has(bed_type_key))
            new_real_bed_type = proj_cfg.opt_enum<BedType>(bed_type_key);
    }
    if (old_real_bed_type != new_real_bed_type) {
        update_slice_result_valid_state(false);
    }

    if (bed_type == BedType::btDefault)
        m_config.erase(bed_type_key);
    else
        m_config.set_key_value("curr_bed_type", new ConfigOptionEnum<BedType>(bed_type));
}

void PartPlate::reset_bed_type()
{
    m_config.erase("curr_bed_type");
}

void PartPlate::set_print_seq(PrintSequence print_seq)
{
    std::string print_seq_key = "print_sequence";

    // should be called in GUI context
    assert(m_plater != nullptr);

    // update slice state
    PrintSequence old_real_print_seq = get_print_seq();
    if (old_real_print_seq == PrintSequence::ByDefault) {
        auto curr_preset_config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
        if (curr_preset_config.has(print_seq_key))
            old_real_print_seq = curr_preset_config.option<ConfigOptionEnum<PrintSequence>>(print_seq_key)->value;
    }

    PrintSequence new_real_print_seq = print_seq;

    if (print_seq == PrintSequence::ByDefault) {
        auto curr_preset_config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
        if (curr_preset_config.has(print_seq_key))
            new_real_print_seq = curr_preset_config.option<ConfigOptionEnum<PrintSequence>>(print_seq_key)->value;
    }

    if (old_real_print_seq != new_real_print_seq) {
        update_slice_result_valid_state(false);
    }

    //print_seq_same_global = same_global;
    if (print_seq == PrintSequence::ByDefault)
        m_config.erase(print_seq_key);
    else
        m_config.set_key_value(print_seq_key, new ConfigOptionEnum<PrintSequence>(print_seq));
}

PrintSequence PartPlate::get_print_seq() const
{
    std::string print_seq_key = "print_sequence";

    // should be called in GUI context
    assert(m_plater != nullptr);

    if (m_config.has(print_seq_key)) {
        PrintSequence print_seq = m_config.opt_enum<PrintSequence>(print_seq_key);
        return print_seq;
    }

    return PrintSequence::ByDefault;
}

PrintSequence PartPlate::get_real_print_seq() const
{
    PrintSequence curr_plate_seq = get_print_seq();
    if (curr_plate_seq == PrintSequence::ByDefault) {
        auto curr_preset_config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
        if (curr_preset_config.has("print_sequence")) curr_plate_seq = curr_preset_config.option<ConfigOptionEnum<PrintSequence>>("print_sequence")->value;
    }
    return curr_plate_seq;
}

bool PartPlate::valid_instance(int obj_id, int instance_id)
{
	if ((obj_id >= 0) && (obj_id < m_model->objects.size()))
	{
		ModelObject* object = m_model->objects[obj_id];
		if ((instance_id >= 0) && (instance_id < object->instances.size()))
			return true;
	}

	return false;
}

void PartPlate::calc_bounding_boxes() const {
	BoundingBoxf3* bounding_box = const_cast<BoundingBoxf3*>(&m_bounding_box);
	*bounding_box = BoundingBoxf3();
	for (const Vec2d& p : m_shape) {
		bounding_box->merge({ p(0), p(1), 0.0 });
	}

	BoundingBoxf3* extended_bounding_box = const_cast<BoundingBoxf3*>(&m_extended_bounding_box);
	*extended_bounding_box = m_bounding_box;

	double half_x = bounding_box->size().x() * GRABBER_X_FACTOR;
	double half_y = bounding_box->size().y() * 1.0f * GRABBER_Y_FACTOR;
	double half_z = GRABBER_Z_VALUE;
	Vec3d center(bounding_box->center().x(), bounding_box->min(1) -half_y, GROUND_Z);
	m_grabber_box.min = Vec3d(center.x() - half_x, center.y() - half_y, center.z() - half_z);
	m_grabber_box.max = Vec3d(center.x() + half_x, center.y() + half_y, center.z() + half_z);
	m_grabber_box.defined = true;
	extended_bounding_box->merge(m_grabber_box);

    //calc exclude area bounding box
    m_exclude_bounding_box.clear();
    BoundingBoxf3 exclude_bb;
    for (int index = 0; index < m_exclude_area.size(); index ++) {
		const Vec2d& p = m_exclude_area[index];

		if (index % 4 == 0)
			exclude_bb = BoundingBoxf3();

		exclude_bb.merge({ p(0), p(1), 0.0 });

		if (index % 4 == 3)
		{
			exclude_bb.max(2) = m_depth;
			exclude_bb.min(2) = GROUND_Z;
			m_exclude_bounding_box.emplace_back(exclude_bb);
		}
	}
}

void PartPlate::calc_triangles(const ExPolygon& poly) {
	if (!m_triangles.set_from_triangles(triangulate_expolygon_2f(poly, NORMALS_UP), GROUND_Z))
		BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":Unable to create plate triangles\n";
}

void PartPlate::calc_exclude_triangles(const ExPolygon& poly) {
	if (!m_exclude_triangles.set_from_triangles(triangulate_expolygon_2f(poly, NORMALS_UP), GROUND_Z))
		BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":Unable to create exclude triangles\n";
}

void PartPlate::calc_gridlines(const ExPolygon& poly, const BoundingBox& pp_bbox) {
	Polylines axes_lines, axes_lines_bolder;
	int count = 0;
	for (coord_t x = pp_bbox.min(0); x <= pp_bbox.max(0); x += scale_(10.0)) {
		Polyline line;
		line.append(Point(x, pp_bbox.min(1)));
		line.append(Point(x, pp_bbox.max(1)));

		if ( (count % 5) == 0 )
			axes_lines_bolder.push_back(line);
		else
			axes_lines.push_back(line);
		count ++;
	}
	count = 0;
	for (coord_t y = pp_bbox.min(1); y <= pp_bbox.max(1); y += scale_(10.0)) {
		Polyline line;
		line.append(Point(pp_bbox.min(0), y));
		line.append(Point(pp_bbox.max(0), y));
		axes_lines.push_back(line);

		if ( (count % 5) == 0 )
			axes_lines_bolder.push_back(line);
		else
			axes_lines.push_back(line);
		count ++;
	}

	// clip with a slightly grown expolygon because our lines lay on the contours and may get erroneously clipped
	Lines gridlines = to_lines(intersection_pl(axes_lines, offset(poly, (float)SCALED_EPSILON)));
	Lines gridlines_bolder = to_lines(intersection_pl(axes_lines_bolder, offset(poly, (float)SCALED_EPSILON)));

	// append bed contours
	Lines contour_lines = to_lines(poly);
	std::copy(contour_lines.begin(), contour_lines.end(), std::back_inserter(gridlines));

	if (!m_gridlines.set_from_lines(gridlines, GROUND_Z))
		BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "Unable to create bed grid lines\n";

	if (!m_gridlines_bolder.set_from_lines(gridlines_bolder, GROUND_Z))
		BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "Unable to create bed grid lines\n";
}

void PartPlate::calc_height_limit() {
	Lines3 bottom_h_lines, top_lines, top_h_lines, common_lines;
	int shape_count = m_shape.size();
	float first_z = 0.02f;
	for (int i = 0; i < shape_count; i++) {
		auto &cur_p = m_shape[i];
		Vec3crd p1(scale_(cur_p.x()), scale_(cur_p.y()), scale_(first_z));
		Vec3crd p2(scale_(cur_p.x()), scale_(cur_p.y()), scale_(m_height_to_rod));
		Vec3crd p3(scale_(cur_p.x()), scale_(cur_p.y()), scale_(m_height_to_lid));

		common_lines.emplace_back(p1, p2);
		top_lines.emplace_back(p2, p3);

		Vec2d next_p;
		if (i < (shape_count - 1)) {
			next_p = m_shape[i+1];

		}
		else {
			next_p = m_shape[0];
		}
		Vec3crd p4(scale_(cur_p.x()), scale_(cur_p.y()), scale_(m_height_to_rod));
		Vec3crd p5(scale_(next_p.x()), scale_(next_p.y()), scale_(m_height_to_rod));
		bottom_h_lines.emplace_back(p4, p5);

		Vec3crd p6(scale_(cur_p.x()), scale_(cur_p.y()), scale_(m_height_to_lid));
		Vec3crd p7(scale_(next_p.x()), scale_(next_p.y()), scale_(m_height_to_lid));
		top_h_lines.emplace_back(p6, p7);
	}
	//std::copy(bottom_lines.begin(), bottom_lines.end(), std::back_inserter(bottom_h_lines));
	std::copy(top_lines.begin(), top_lines.end(), std::back_inserter(top_h_lines));

	if (!m_height_limit_common.set_from_3d_Lines(common_lines))
		BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "Unable to create height limit bottom lines\n";

	if (!m_height_limit_bottom.set_from_3d_Lines(bottom_h_lines))
		BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "Unable to create height limit bottom lines\n";

	if (!m_height_limit_top.set_from_3d_Lines(top_h_lines))
		BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "Unable to create height limit top lines\n";
}

void PartPlate::calc_vertex_for_number(int index, bool one_number, GeometryBuffer &buffer)
{
	ExPolygon poly;
#if 0 //in the up area
	Vec2d& p = m_shape[2];
	float offset_x = one_number?PARTPLATE_TEXT_OFFSET_X1: PARTPLATE_TEXT_OFFSET_X2;

	poly.contour.append({ scale_(p(0) + PARTPLATE_ICON_GAP + offset_x), scale_(p(1) - index * (PARTPLATE_ICON_SIZE + PARTPLATE_ICON_GAP) - PARTPLATE_ICON_GAP - PARTPLATE_ICON_SIZE + PARTPLATE_TEXT_OFFSET_Y) });
	poly.contour.append({ scale_(p(0) + PARTPLATE_ICON_GAP + PARTPLATE_ICON_SIZE - offset_x), scale_(p(1) - index * (PARTPLATE_ICON_SIZE + PARTPLATE_ICON_GAP)- PARTPLATE_ICON_GAP - PARTPLATE_ICON_SIZE + PARTPLATE_TEXT_OFFSET_Y) });
	poly.contour.append({ scale_(p(0) + PARTPLATE_ICON_GAP + PARTPLATE_ICON_SIZE - offset_x), scale_(p(1) - index * (PARTPLATE_ICON_SIZE + PARTPLATE_ICON_GAP)- PARTPLATE_ICON_GAP - PARTPLATE_TEXT_OFFSET_Y)});
	poly.contour.append({ scale_(p(0) + PARTPLATE_ICON_GAP + offset_x), scale_(p(1) - index * (PARTPLATE_ICON_SIZE + PARTPLATE_ICON_GAP)- PARTPLATE_ICON_GAP - PARTPLATE_TEXT_OFFSET_Y) });
#else //in the bottom
	Vec2d& p = m_shape[1];
	float offset_x = one_number?PARTPLATE_TEXT_OFFSET_X1: PARTPLATE_TEXT_OFFSET_X2;

	poly.contour.append({ scale_(p(0) + PARTPLATE_ICON_GAP_LEFT + offset_x), scale_(p(1) + PARTPLATE_TEXT_OFFSET_Y) });
	poly.contour.append({ scale_(p(0) + PARTPLATE_ICON_GAP_LEFT + PARTPLATE_ICON_SIZE - offset_x), scale_(p(1) + PARTPLATE_TEXT_OFFSET_Y) });
	poly.contour.append({ scale_(p(0) + PARTPLATE_ICON_GAP_LEFT + PARTPLATE_ICON_SIZE - offset_x), scale_(p(1) + PARTPLATE_ICON_SIZE - PARTPLATE_TEXT_OFFSET_Y)});
	poly.contour.append({ scale_(p(0) + PARTPLATE_ICON_GAP_LEFT + offset_x), scale_(p(1) + PARTPLATE_ICON_SIZE - PARTPLATE_TEXT_OFFSET_Y) });
#endif
	auto triangles = triangulate_expolygon_2f(poly, NORMALS_UP);
	if (!buffer.set_from_triangles(triangles, GROUND_Z))
		BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "Unable to generate geometry buffers for icons\n";
}

void PartPlate::calc_vertex_for_icons(int index, GeometryBuffer &buffer)
{
	ExPolygon poly;
	Vec2d& p = m_shape[2];

	poly.contour.append({ scale_(p(0) + PARTPLATE_ICON_GAP_LEFT), scale_(p(1) - index * (PARTPLATE_ICON_SIZE + PARTPLATE_ICON_GAP_Y) - PARTPLATE_ICON_GAP_TOP - PARTPLATE_ICON_SIZE) });
	poly.contour.append({ scale_(p(0) + PARTPLATE_ICON_GAP_LEFT + PARTPLATE_ICON_SIZE), scale_(p(1) - index * (PARTPLATE_ICON_SIZE + PARTPLATE_ICON_GAP_Y)- PARTPLATE_ICON_GAP_TOP - PARTPLATE_ICON_SIZE) });
	poly.contour.append({ scale_(p(0) + PARTPLATE_ICON_GAP_LEFT + PARTPLATE_ICON_SIZE), scale_(p(1) - index * (PARTPLATE_ICON_SIZE + PARTPLATE_ICON_GAP_Y)- PARTPLATE_ICON_GAP_TOP)});
	poly.contour.append({ scale_(p(0) + PARTPLATE_ICON_GAP_LEFT), scale_(p(1) - index * (PARTPLATE_ICON_SIZE + PARTPLATE_ICON_GAP_Y)- PARTPLATE_ICON_GAP_TOP) });

	auto triangles = triangulate_expolygon_2f(poly, NORMALS_UP);
	if (!buffer.set_from_triangles(triangles, GROUND_Z))
		BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "Unable to generate geometry buffers for icons\n";
}

void PartPlate::calc_vertex_for_icons_background(int icon_count, GeometryBuffer &buffer)
{
	ExPolygon poly;
	Vec2d& p = m_shape[2];

	poly.contour.append({ scale_(p(0) + PARTPLATE_ICON_GAP_LEFT), scale_(p(1) - icon_count * (PARTPLATE_ICON_SIZE + PARTPLATE_ICON_GAP_Y) - PARTPLATE_ICON_GAP_TOP) });
	poly.contour.append({ scale_(p(0) + PARTPLATE_ICON_GAP_LEFT + PARTPLATE_ICON_SIZE), scale_(p(1) - icon_count * (PARTPLATE_ICON_SIZE + PARTPLATE_ICON_GAP_Y)- PARTPLATE_ICON_GAP_TOP) });
	poly.contour.append({ scale_(p(0) + PARTPLATE_ICON_GAP_LEFT + PARTPLATE_ICON_SIZE), scale_(p(1) - PARTPLATE_ICON_GAP_TOP)});
	poly.contour.append({ scale_(p(0) + PARTPLATE_ICON_GAP_LEFT), scale_(p(1) - PARTPLATE_ICON_GAP_TOP) });

	auto triangles = triangulate_expolygon_2f(poly, NORMALS_UP);
	if (!buffer.set_from_triangles(triangles, GROUND_Z))
		BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "Unable to generate geometry buffers for icons\n";
}

void PartPlate::render_background(bool force_default_color) const {
	unsigned int triangles_vcount = m_triangles.get_vertices_count();

	//return directly for current plate
	if (m_selected && !force_default_color) return;

	// draw background
	glsafe(::glDepthMask(GL_FALSE));

	if (!force_default_color) {
		if (m_selected) {
			glsafe(::glColor4fv(PartPlate::SELECT_COLOR.data()));
		}
		else {
			glsafe(m_partplate_list->m_is_dark ? ::glColor4fv(PartPlate::UNSELECT_DARK_COLOR.data()) : ::glColor4fv(PartPlate::UNSELECT_COLOR.data()));
		}
	}
	else {
		glsafe(::glColor4fv(PartPlate::DEFAULT_COLOR.data()));
	}
	glsafe(::glNormal3d(0.0f, 0.0f, 1.0f));
	glsafe(::glVertexPointer(3, GL_FLOAT, m_triangles.get_vertex_data_size(), (GLvoid*)m_triangles.get_vertices_data()));
	glsafe(::glDrawArrays(GL_TRIANGLES, 0, (GLsizei)triangles_vcount));
	glsafe(::glDepthMask(GL_TRUE));
}

void PartPlate::render_logo_texture(GLTexture &logo_texture, const GeometryBuffer& logo_buffer, bool bottom, unsigned int vbo_id) const
{
	//check valid
	if (logo_texture.unsent_compressed_data_available()) {
		// sends to gpu the already available compressed levels of the main texture
		logo_texture.send_compressed_data_to_gpu();
	}

	if (logo_buffer.get_vertices_count() > 0) {
		GLShaderProgram* shader = wxGetApp().get_shader("printbed");
		if (shader != nullptr) {
			shader->start_using();
			shader->set_uniform("transparent_background", 0);
			shader->set_uniform("svg_source", 0);

			//glsafe(::glEnable(GL_DEPTH_TEST));
			glsafe(::glDepthMask(GL_FALSE));
			if (bottom)
				glsafe(::glFrontFace(GL_CW));

			unsigned int stride = logo_buffer.get_vertex_data_size();

			GLint position_id = shader->get_attrib_location("v_position");
			GLint tex_coords_id = shader->get_attrib_location("v_tex_coords");
			if (position_id != -1) {
				glsafe(::glEnableVertexAttribArray(position_id));
			}
			if (tex_coords_id != -1) {
				glsafe(::glEnableVertexAttribArray(tex_coords_id));
			}

			// show the temporary texture while no compressed data is available
			GLuint tex_id = (GLuint)logo_texture.get_id();

			glsafe(::glBindTexture(GL_TEXTURE_2D, tex_id));
			glsafe(::glBindBuffer(GL_ARRAY_BUFFER, vbo_id));

			if (position_id != -1)
				glsafe(::glVertexAttribPointer(position_id, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(intptr_t)logo_buffer.get_position_offset()));
			if (tex_coords_id != -1)
				glsafe(::glVertexAttribPointer(tex_coords_id, 2, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(intptr_t)logo_buffer.get_tex_coords_offset()));
			glsafe(::glDrawArrays(GL_TRIANGLES, 0, (GLsizei)logo_buffer.get_vertices_count()));

			if (tex_coords_id != -1)
				glsafe(::glDisableVertexAttribArray(tex_coords_id));

			if (position_id != -1)
				glsafe(::glDisableVertexAttribArray(position_id));

			glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
			glsafe(::glBindTexture(GL_TEXTURE_2D, 0));

			if (bottom)
				glsafe(::glFrontFace(GL_CCW));

			glsafe(::glDepthMask(GL_TRUE));

			shader->stop_using();
		}
	}
}

void PartPlate::render_logo(bool bottom) const
{
	if (!m_partplate_list->render_bedtype_logo) {
		// render third-party printer texture logo
		if (m_partplate_list->m_logo_texture_filename.empty()) {
			m_partplate_list->m_logo_texture.reset();
			return;
		}

		//GLTexture* temp_texture = const_cast<GLTexture*>(&m_temp_texture);

		if (m_partplate_list->m_logo_texture.get_id() == 0 || m_partplate_list->m_logo_texture.get_source() != m_partplate_list->m_logo_texture_filename) {
			m_partplate_list->m_logo_texture.reset();

			if (boost::algorithm::iends_with(m_partplate_list->m_logo_texture_filename, ".svg")) {
				/*// use higher resolution images if graphic card and opengl version allow
				GLint max_tex_size = OpenGLManager::get_gl_info().get_max_tex_size();
				if (temp_texture->get_id() == 0 || temp_texture->get_source() != m_texture_filename) {
					// generate a temporary lower resolution texture to show while no main texture levels have been compressed
					if (!temp_texture->load_from_svg_file(m_texture_filename, false, false, false, max_tex_size / 8)) {
						render_default(bottom, false);
						return;
					}
					canvas.request_extra_frame();
				}*/

				// starts generating the main texture, compression will run asynchronously
				GLint max_tex_size = OpenGLManager::get_gl_info().get_max_tex_size();
				GLint logo_tex_size = (max_tex_size < 2048) ? max_tex_size : 2048;
				if (!m_partplate_list->m_logo_texture.load_from_svg_file(m_partplate_list->m_logo_texture_filename, true, true, true, logo_tex_size)) {
					BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(": load logo texture from %1% failed!") % m_partplate_list->m_logo_texture_filename;
					return;
				}
			}
			else if (boost::algorithm::iends_with(m_partplate_list->m_logo_texture_filename, ".png")) {
				// generate a temporary lower resolution texture to show while no main texture levels have been compressed
				/* if (temp_texture->get_id() == 0 || temp_texture->get_source() != m_logo_texture_filename) {
					if (!temp_texture->load_from_file(m_logo_texture_filename, false, GLTexture::None, false)) {
						render_default(bottom, false);
						return;
					}
					canvas.request_extra_frame();
				}*/

				// starts generating the main texture, compression will run asynchronously
				if (!m_partplate_list->m_logo_texture.load_from_file(m_partplate_list->m_logo_texture_filename, true, GLTexture::MultiThreaded, true)) {
					BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(": load logo texture from %1% failed!") % m_partplate_list->m_logo_texture_filename;
					return;
				}
			}
			else {
				BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(": can not load logo texture from %1%, unsupported format") % m_partplate_list->m_logo_texture_filename;
				return;
			}
		}
		else if (m_partplate_list->m_logo_texture.unsent_compressed_data_available()) {
			// sends to gpu the already available compressed levels of the main texture
			m_partplate_list->m_logo_texture.send_compressed_data_to_gpu();

			// the temporary texture is not needed anymore, reset it
			//if (temp_texture->get_id() != 0)
			//    temp_texture->reset();

			//canvas.request_extra_frame();
		}

		if (m_vbo_id == 0) {
			unsigned int* vbo_id_ptr = const_cast<unsigned int*>(&m_vbo_id);
			glsafe(::glGenBuffers(1, vbo_id_ptr));
			glsafe(::glBindBuffer(GL_ARRAY_BUFFER, *vbo_id_ptr));
			glsafe(::glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)m_logo_triangles.get_vertices_data_size(), (const GLvoid*)m_logo_triangles.get_vertices_data(), GL_STATIC_DRAW));
			glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
		}
		if (m_vbo_id != 0 && m_logo_triangles.get_vertices_count() > 0)
			render_logo_texture(m_partplate_list->m_logo_texture, m_logo_triangles, bottom, m_vbo_id);
		return;
	}

	m_partplate_list->load_bedtype_textures();

	// btDefault should be skipped
	auto curr_bed_type = get_bed_type();
	if (curr_bed_type == btDefault) {
        DynamicConfig& proj_cfg = wxGetApp().preset_bundle->project_config;
        if (proj_cfg.has(std::string("curr_bed_type")))
            curr_bed_type = proj_cfg.opt_enum<BedType>(std::string("curr_bed_type"));
	}
	int bed_type_idx = (int)curr_bed_type;
	for (auto &part : m_partplate_list->bed_texture_info[bed_type_idx].parts) {
		if (part.texture) {
			if (part.buffer && part.buffer->get_vertices_count() > 0
				//&& part.vbo_id != 0
				) {
				if (part.offset.x() != m_origin.x() || part.offset.y() != m_origin.y()) {
					part.offset = Vec2d(m_origin.x(), m_origin.y());
					part.update_buffer();
				}
				render_logo_texture(*(part.texture),
									*(part.buffer),
									bottom,
									part.vbo_id);
			}
		}
	}
}

void PartPlate::render_exclude_area(bool force_default_color) const {
	if (force_default_color) //for thumbnail case
		return;

	unsigned int triangles_vcount = m_exclude_triangles.get_vertices_count();
	std::array<float, 4> select_color{ 0.765f, 0.7686f, 0.7686f, 1.0f };
	std::array<float, 4> unselect_color{ 0.9f, 0.9f, 0.9f, 1.0f };
	std::array<float, 4> default_color{ 0.9f, 0.9f, 0.9f, 1.0f };

	// draw exclude area
	glsafe(::glDepthMask(GL_FALSE));

	if (m_selected) {
		glsafe(::glColor4fv(select_color.data()));
	}
	else {
		glsafe(::glColor4fv(unselect_color.data()));
	}

	glsafe(::glNormal3d(0.0f, 0.0f, 1.0f));
	glsafe(::glVertexPointer(3, GL_FLOAT, m_exclude_triangles.get_vertex_data_size(), (GLvoid*)m_exclude_triangles.get_vertices_data()));
	glsafe(::glDrawArrays(GL_TRIANGLES, 0, (GLsizei)triangles_vcount));
	glsafe(::glDepthMask(GL_TRUE));
}


/*void PartPlate::render_background_for_picking(const float* render_color) const
{
	unsigned int triangles_vcount = m_triangles.get_vertices_count();

	glsafe(::glDepthMask(GL_FALSE));
	glsafe(::glColor4fv(render_color));
	glsafe(::glNormal3d(0.0f, 0.0f, 1.0f));
	glsafe(::glVertexPointer(3, GL_FLOAT, m_triangles.get_vertex_data_size(), (GLvoid*)m_triangles.get_vertices_data()));
	glsafe(::glDrawArrays(GL_TRIANGLES, 0, (GLsizei)triangles_vcount));
	glsafe(::glDepthMask(GL_TRUE));
}*/

void PartPlate::render_grid(bool bottom) const {
	//glsafe(::glEnable(GL_MULTISAMPLE));
	// draw grid
	glsafe(::glLineWidth(1.0f * m_scale_factor));
	if (bottom)
		glsafe(::glColor4fv(LINE_BOTTOM_COLOR.data()));
	else {
		if (m_selected)
			glsafe(m_partplate_list->m_is_dark ? ::glColor4fv(LINE_TOP_SEL_DARK_COLOR.data()) : ::glColor4fv(LINE_TOP_SEL_COLOR.data()));
		else
			glsafe(m_partplate_list->m_is_dark ? ::glColor4fv(LINE_TOP_DARK_COLOR.data()) : ::glColor4fv(LINE_TOP_COLOR.data()));
	}
	glsafe(::glVertexPointer(3, GL_FLOAT, m_gridlines.get_vertex_data_size(), (GLvoid*)m_gridlines.get_vertices_data()));
	glsafe(::glDrawArrays(GL_LINES, 0, (GLsizei)m_gridlines.get_vertices_count()));

	glsafe(::glLineWidth(2.0f * m_scale_factor));
	glsafe(::glVertexPointer(3, GL_FLOAT, m_gridlines_bolder.get_vertex_data_size(), (GLvoid*)m_gridlines_bolder.get_vertices_data()));
	glsafe(::glDrawArrays(GL_LINES, 0, (GLsizei)m_gridlines_bolder.get_vertices_count()));
}

void PartPlate::render_height_limit(PartPlate::HeightLimitMode mode) const
{
	if (m_print && m_print->config().print_sequence == PrintSequence::ByObject && mode != HEIGHT_LIMIT_NONE)
	{
		// draw lower limit
		glsafe(::glLineWidth(3.0f * m_scale_factor));
		glsafe(::glColor4fv(HEIGHT_LIMIT_BOTTOM_COLOR.data()));
		glsafe(::glVertexPointer(3, GL_FLOAT, m_height_limit_common.get_vertex_data_size(), (GLvoid*)m_height_limit_common.get_vertices_data()));
		glsafe(::glDrawArrays(GL_LINES, 0, (GLsizei)m_height_limit_common.get_vertices_count()));

		if ((mode == HEIGHT_LIMIT_BOTTOM) || (mode == HEIGHT_LIMIT_BOTH)) {
			glsafe(::glLineWidth(3.0f * m_scale_factor));
			glsafe(::glColor4fv(HEIGHT_LIMIT_BOTTOM_COLOR.data()));
			glsafe(::glVertexPointer(3, GL_FLOAT, m_height_limit_bottom.get_vertex_data_size(), (GLvoid*)m_height_limit_bottom.get_vertices_data()));
			glsafe(::glDrawArrays(GL_LINES, 0, (GLsizei)m_height_limit_bottom.get_vertices_count()));
		}

		// draw upper limit
		if ((mode == HEIGHT_LIMIT_TOP) || (mode == HEIGHT_LIMIT_BOTH)){
			glsafe(::glLineWidth(3.0f * m_scale_factor));
			glsafe(::glColor4fv(HEIGHT_LIMIT_TOP_COLOR.data()));
			glsafe(::glVertexPointer(3, GL_FLOAT, m_height_limit_top.get_vertex_data_size(), (GLvoid*)m_height_limit_top.get_vertices_data()));
			glsafe(::glDrawArrays(GL_LINES, 0, (GLsizei)m_height_limit_top.get_vertices_count()));
		}
	}
}


void PartPlate::render_icon_texture(int position_id, int tex_coords_id, const GeometryBuffer &buffer, GLTexture &texture, unsigned int &vbo_id) const
{
	if (vbo_id == 0) {
		glsafe(::glGenBuffers(1, &vbo_id));
		glsafe(::glBindBuffer(GL_ARRAY_BUFFER, vbo_id));
		glsafe(::glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)buffer.get_vertices_data_size(), (const GLvoid*)buffer.get_vertices_data(), GL_STATIC_DRAW));
		glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
	}

	unsigned int stride = buffer.get_vertex_data_size();
	GLuint tex_id = (GLuint)texture.get_id();
	glsafe(::glBindTexture(GL_TEXTURE_2D, tex_id));
	glsafe(::glBindBuffer(GL_ARRAY_BUFFER, vbo_id));
	if (position_id != -1)
		glsafe(::glVertexAttribPointer(position_id, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(intptr_t)buffer.get_position_offset()));
	if (tex_coords_id != -1)
		glsafe(::glVertexAttribPointer(tex_coords_id, 2, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(intptr_t)buffer.get_tex_coords_offset()));
	glsafe(::glDrawArrays(GL_TRIANGLES, 0, (GLsizei)buffer.get_vertices_count()));

	glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
	glsafe(::glBindTexture(GL_TEXTURE_2D, 0));
}
void PartPlate::render_plate_name_texture(int position_id, int tex_coords_id)
{
	if (m_name_texture.get_id() == 0)
		generate_plate_name_texture();

	if (m_plate_name_vbo_id == 0 && m_plate_name_icon.get_vertices_data_size() > 0) {
		glsafe(::glGenBuffers(1, &m_plate_name_vbo_id));
		glsafe(::glBindBuffer(GL_ARRAY_BUFFER, m_plate_name_vbo_id));
		glsafe(::glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)m_plate_name_icon.get_vertices_data_size(), (const GLvoid*)m_plate_name_icon.get_vertices_data(), GL_STATIC_DRAW));
		glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
	}

	unsigned int stride = m_plate_name_icon.get_vertex_data_size();
	GLuint tex_id = (GLuint)m_name_texture.get_id();
	glsafe(::glBindTexture(GL_TEXTURE_2D, tex_id));
	glsafe(::glBindBuffer(GL_ARRAY_BUFFER, m_plate_name_vbo_id));
	if (position_id != -1)
		glsafe(::glVertexAttribPointer(position_id, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(intptr_t)m_plate_name_icon.get_position_offset()));
	if (tex_coords_id != -1)
		glsafe(::glVertexAttribPointer(tex_coords_id, 2, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(intptr_t)m_plate_name_icon.get_tex_coords_offset()));
	glsafe(::glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_plate_name_icon.get_vertices_count()));

	glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
	glsafe(::glBindTexture(GL_TEXTURE_2D, 0));
}

void PartPlate::render_icons(bool bottom, bool only_name, int hover_id)
{
	GLShaderProgram* shader = wxGetApp().get_shader("printbed");
	if (shader != nullptr) {
		shader->start_using();
		shader->set_uniform("transparent_background", bottom);
		//shader->set_uniform("svg_source", boost::algorithm::iends_with(m_partplate_list->m_del_texture.get_source(), ".svg"));
		shader->set_uniform("svg_source", 0);

        //if (bottom)
        //    glsafe(::glFrontFace(GL_CW));
        glsafe(::glDepthMask(GL_FALSE));

        GLint position_id = shader->get_attrib_location("v_position");
        GLint tex_coords_id = shader->get_attrib_location("v_tex_coords");
        if (position_id != -1) {
            glsafe(::glEnableVertexAttribArray(position_id));
        }
        if (tex_coords_id != -1) {
            glsafe(::glEnableVertexAttribArray(tex_coords_id));
        }
        if (!only_name) {
            if (hover_id == 1)
                render_icon_texture(position_id, tex_coords_id, m_del_icon, m_partplate_list->m_del_hovered_texture,
                                    m_del_vbo_id);
            else
                render_icon_texture(position_id, tex_coords_id, m_del_icon, m_partplate_list->m_del_texture,
                                    m_del_vbo_id);

            if (hover_id == 2)
                render_icon_texture(position_id, tex_coords_id, m_orient_icon,
                                    m_partplate_list->m_orient_hovered_texture, m_orient_vbo_id);
            else
                render_icon_texture(position_id, tex_coords_id, m_orient_icon, m_partplate_list->m_orient_texture,
                                    m_orient_vbo_id);

            if (hover_id == 3)
                render_icon_texture(position_id, tex_coords_id, m_arrange_icon,
                                    m_partplate_list->m_arrange_hovered_texture, m_arrange_vbo_id);
            else
                render_icon_texture(position_id, tex_coords_id, m_arrange_icon, m_partplate_list->m_arrange_texture,
                                    m_arrange_vbo_id);

            if (hover_id == 4) {
                if (this->is_locked())
                    render_icon_texture(position_id, tex_coords_id, m_lock_icon,
                                        m_partplate_list->m_locked_hovered_texture, m_lock_vbo_id);
                else
                    render_icon_texture(position_id, tex_coords_id, m_lock_icon,
                                        m_partplate_list->m_lockopen_hovered_texture, m_lock_vbo_id);
            } else {
                if (this->is_locked())
                    render_icon_texture(position_id, tex_coords_id, m_lock_icon, m_partplate_list->m_locked_texture,
                                        m_lock_vbo_id);
                else
                    render_icon_texture(position_id, tex_coords_id, m_lock_icon, m_partplate_list->m_lockopen_texture,
                                        m_lock_vbo_id);
            }

            if (m_partplate_list->render_plate_settings) {
                if (hover_id == 5) {
                    if (get_bed_type() == BedType::btDefault && get_print_seq() == PrintSequence::ByDefault)
                      render_icon_texture(position_id, tex_coords_id, m_plate_settings_icon,
                                          m_partplate_list->m_plate_settings_hovered_texture, m_plate_settings_vbo_id);
                    else
                      render_icon_texture(position_id, tex_coords_id, m_plate_settings_icon,
                                          m_partplate_list->m_plate_settings_changed_hovered_texture,
                                          m_plate_settings_vbo_id);
                } else {
                    if (get_bed_type() == BedType::btDefault && get_print_seq() == PrintSequence::ByDefault)
                      render_icon_texture(position_id, tex_coords_id, m_plate_settings_icon,
                                          m_partplate_list->m_plate_settings_texture, m_plate_settings_vbo_id);
                    else
                      render_icon_texture(position_id, tex_coords_id, m_plate_settings_icon,
                                          m_partplate_list->m_plate_settings_changed_texture, m_plate_settings_vbo_id);
                }
            }

            if (m_plate_index >= 0 && m_plate_index < MAX_PLATE_COUNT) {
                render_icon_texture(position_id, tex_coords_id, m_plate_idx_icon,
                                    m_partplate_list->m_idx_textures[m_plate_index], m_plate_idx_vbo_id);
            }
        }
		render_plate_name_texture(position_id, tex_coords_id);
        if (tex_coords_id != -1)
            glsafe(::glDisableVertexAttribArray(tex_coords_id));

        if (position_id != -1)
            glsafe(::glDisableVertexAttribArray(position_id));

        //if (bottom)
        //    glsafe(::glFrontFace(GL_CCW));

        glsafe(::glDepthMask(GL_TRUE));
        shader->stop_using();
    }
}

void PartPlate::render_only_numbers(bool bottom) const
{
	GLShaderProgram* shader = wxGetApp().get_shader("printbed");
	if (shader != nullptr) {
		shader->start_using();
		shader->set_uniform("transparent_background", bottom);
		//shader->set_uniform("svg_source", boost::algorithm::iends_with(m_partplate_list->m_del_texture.get_source(), ".svg"));
		shader->set_uniform("svg_source", 0);

        //if (bottom)
        //    glsafe(::glFrontFace(GL_CW));
        glsafe(::glDepthMask(GL_FALSE));

        GLint position_id = shader->get_attrib_location("v_position");
        GLint tex_coords_id = shader->get_attrib_location("v_tex_coords");
        if (position_id != -1) {
            glsafe(::glEnableVertexAttribArray(position_id));
        }
        if (tex_coords_id != -1) {
            glsafe(::glEnableVertexAttribArray(tex_coords_id));
        }

        if (m_plate_index >=0 && m_plate_index < MAX_PLATE_COUNT) {
            render_icon_texture(position_id, tex_coords_id, m_plate_idx_icon, m_partplate_list->m_idx_textures[m_plate_index], m_plate_idx_vbo_id);
        }

        if (tex_coords_id != -1)
            glsafe(::glDisableVertexAttribArray(tex_coords_id));

        if (position_id != -1)
            glsafe(::glDisableVertexAttribArray(position_id));

        //if (bottom)
        //    glsafe(::glFrontFace(GL_CCW));

        glsafe(::glDepthMask(GL_TRUE));
        shader->stop_using();
    }
}

void PartPlate::render_rectangle_for_picking(const GeometryBuffer &buffer, const float* render_color) const
{
	unsigned int triangles_vcount = buffer.get_vertices_count();

	//glsafe(::glDepthMask(GL_FALSE));
	glsafe(::glEnableClientState(GL_VERTEX_ARRAY));
	glsafe(::glColor4fv(render_color));
	glsafe(::glNormal3d(0.0f, 0.0f, 1.0f));
	glsafe(::glVertexPointer(3, GL_FLOAT, buffer.get_vertex_data_size(), (GLvoid*)buffer.get_vertices_data()));
	glsafe(::glDrawArrays(GL_TRIANGLES, 0, (GLsizei)triangles_vcount));
	glsafe(::glDisableClientState(GL_VERTEX_ARRAY));
	//glsafe(::glDepthMask(GL_TRUE));
}

void PartPlate::render_label(GLCanvas3D& canvas) const {
	std::string label = (boost::format("Plate %1%") % (m_plate_index + 1)).str();
	const Camera& camera = wxGetApp().plater()->get_camera();
	Transform3d world_to_eye = camera.get_view_matrix();
	Transform3d world_to_screen = camera.get_projection_matrix() * world_to_eye;
	const std::array<int, 4>& viewport = camera.get_viewport();

	BoundingBoxf3* bounding_box = const_cast<BoundingBoxf3*>(&m_bounding_box);
	Vec3d screen_box_center = world_to_screen * bounding_box->min;

	float x = 0.0f;
	float y = 0.0f;
	if (camera.get_type() == Camera::EType::Perspective) {
		x = (0.5f + 0.001f * 0.5f * (float)screen_box_center(0)) * viewport[2];
		y = (0.5f - 0.001f * 0.5f * (float)screen_box_center(1)) * viewport[3];
	}
	else {
		x = (0.5f + 0.5f * (float)screen_box_center(0)) * viewport[2];
		y = (0.5f - 0.5f * (float)screen_box_center(1)) * viewport[3];
	}

	ImGuiWrapper& imgui = *wxGetApp().imgui();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.75f, 0.75f, 0.75f, 1.0f));
	imgui.set_next_window_pos(x, y, ImGuiCond_Always, 0.5f, 0.5f);
	imgui.begin(label, ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);
	ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
	float win_w = ImGui::GetWindowWidth();
	float label_len = imgui.calc_text_size(label).x;
	ImGui::SetCursorPosX(0.5f * (win_w - label_len));
	ImGui::AlignTextToFramePadding();
	imgui.text(label);

	// force re-render while the windows gets to its final size (it takes several frames)
	if (ImGui::GetWindowContentRegionWidth() + 2.0f * ImGui::GetStyle().WindowPadding.x != ImGui::CalcWindowNextAutoFitSize(ImGui::GetCurrentWindow()).x)
		canvas.request_extra_frame();

	imgui.end();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);

}

void PartPlate::render_grabber(const float* render_color, bool use_lighting) const
{
	BoundingBoxf3* bounding_box = const_cast<BoundingBoxf3*>(&m_bounding_box);
	const Vec3d& center = m_grabber_box.center();

	if (use_lighting)
		glsafe(::glEnable(GL_LIGHTING));
	glsafe(::glColor4fv(render_color));
	glsafe(::glPushMatrix());

	glsafe(::glTranslated(center(0), center(1), center(2)));

	Vec3d angles(Vec3d::Zero());
	glsafe(::glRotated(Geometry::rad2deg(angles(2)), 0.0, 0.0, 1.0));
	glsafe(::glRotated(Geometry::rad2deg(angles(1)), 0.0, 1.0, 0.0));
	glsafe(::glRotated(Geometry::rad2deg(angles(0)), 1.0, 0.0, 0.0));

	float half_x = bounding_box->size().x() * GRABBER_X_FACTOR;
	float half_y = bounding_box->size().y() * GRABBER_Y_FACTOR;
	float half_z = GRABBER_Z_VALUE;
	// face min x
	glsafe(::glPushMatrix());
	glsafe(::glTranslatef(-(GLfloat)half_x, 0, 0.0f));
	glsafe(::glRotatef(-90.0f, 0.0f, 1.0f, 0.0f));
	render_face(half_z, half_y);
	glsafe(::glPopMatrix());

	// face max x
	glsafe(::glPushMatrix());
	glsafe(::glTranslatef((GLfloat)half_x, 0, 0.0f));
	glsafe(::glRotatef(90.0f, 0.0f, 1.0f, 0.0f));
	render_face(half_z, half_y);
	glsafe(::glPopMatrix());

	// face min y
	glsafe(::glPushMatrix());
	glsafe(::glTranslatef(0.0f, -(GLfloat)half_y, 0.0f));
	glsafe(::glRotatef(90.0f, 1.0f, 0.0f, 0.0f));
	render_face(half_x, half_z);
	glsafe(::glPopMatrix());

	// face max y
	glsafe(::glPushMatrix());
	glsafe(::glTranslatef(0.0f, (GLfloat)half_y, 0.0f));
	glsafe(::glRotatef(-90.0f, 1.0f, 0.0f, 0.0f));
	render_face(half_x, half_z);
	glsafe(::glPopMatrix());

	// face min z
	glsafe(::glPushMatrix());
	glsafe(::glTranslatef(0.0f, 0.0f, -(GLfloat)half_z));
	glsafe(::glRotatef(180.0f, 1.0f, 0.0f, 0.0f));
	render_face(half_x, half_y);
	glsafe(::glPopMatrix());

	// face max z
	glsafe(::glPushMatrix());
	glsafe(::glTranslatef(0.0f, 0.0f, (GLfloat)half_z));
	render_face(half_x, half_y);
	glsafe(::glPopMatrix());

	glsafe(::glPopMatrix());

	if (use_lighting)
		glsafe(::glDisable(GL_LIGHTING));
}

void PartPlate::render_face(float x_size, float y_size) const
{
	::glBegin(GL_TRIANGLES);
	::glNormal3f(0.0f, 0.0f, 1.0f);
	::glVertex3f(-(GLfloat)x_size, -(GLfloat)y_size, 0.0f);
	::glVertex3f((GLfloat)x_size, -(GLfloat)y_size, 0.0f);
	::glVertex3f((GLfloat)x_size, (GLfloat)y_size, 0.0f);
	::glVertex3f((GLfloat)x_size, (GLfloat)y_size, 0.0f);
	::glVertex3f(-(GLfloat)x_size, (GLfloat)y_size, 0.0f);
	::glVertex3f(-(GLfloat)x_size, -(GLfloat)y_size, 0.0f);
	glsafe(::glEnd());
}

void PartPlate::render_arrows(const float* render_color, bool use_lighting) const
{
#if 0
	if (m_quadric == nullptr)
		return;
	double radius = m_grabber_box.size().y() * 0.5f;
	double height = radius * 2.0f;
	double position = m_grabber_box.size().x() * 0.8f;
	if (use_lighting)
		glsafe(::glEnable(GL_LIGHTING));

	glsafe(::glColor4fv(render_color));
	glsafe(::glPushMatrix());
	glsafe(::glTranslated(m_grabber_box.center().x(), m_grabber_box.center().y(), m_grabber_box.center().z()));
	glsafe(::glRotated(90.0, 0.0, 1.0, 0.0));
	glsafe(::glTranslated(0.0, 0.0, position));

	::gluQuadricOrientation(m_quadric, GLU_OUTSIDE);
	::gluCylinder(m_quadric, 0.9 * radius, 0.0, height, 36, 1);
	::gluQuadricOrientation(m_quadric, GLU_INSIDE);
	::gluDisk(m_quadric, 0.0, 0.9 * radius, 36, 1);
	glsafe(::glPopMatrix());

	glsafe(::glPushMatrix());
	glsafe(::glTranslated(m_grabber_box.center().x(), m_grabber_box.center().y(), m_grabber_box.center().z()));
	glsafe(::glRotated(-90.0, 0.0, 1.0, 0.0));
	glsafe(::glTranslated(0.0, 0.0, position));
	::gluQuadricOrientation(m_quadric, GLU_OUTSIDE);
	::gluCylinder(m_quadric, 0.9 * radius, 0.0, height, 36, 1);
	::gluQuadricOrientation(m_quadric, GLU_INSIDE);
	::gluDisk(m_quadric, 0.0, 0.9 * radius, 36, 1);
	glsafe(::glPopMatrix());

	if (use_lighting)
		glsafe(::glDisable(GL_LIGHTING));
#endif
}

void PartPlate::render_left_arrow(const float* render_color, bool use_lighting) const
{
#if 0
	if (m_quadric == nullptr)
		return;
	double radius = m_grabber_box.size().y() * 0.5f;
	double height = radius * 2.0f;
	double position = m_grabber_box.size().x() * 0.8f;
	if (use_lighting)
		glsafe(::glEnable(GL_LIGHTING));

	glsafe(::glColor4fv(render_color));

	glsafe(::glPushMatrix());
	glsafe(::glTranslated(m_grabber_box.center().x(), m_grabber_box.center().y(), m_grabber_box.center().z()));
	glsafe(::glRotated(-90.0, 0.0, 1.0, 0.0));
	glsafe(::glTranslated(0.0, 0.0, position));
	::gluQuadricOrientation(m_quadric, GLU_OUTSIDE);
	::gluCylinder(m_quadric, 0.9 * radius, 0.0, height, 36, 1);
	::gluQuadricOrientation(m_quadric, GLU_INSIDE);
	::gluDisk(m_quadric, 0.0, 0.9 * radius, 36, 1);
	glsafe(::glPopMatrix());

	if (use_lighting)
		glsafe(::glDisable(GL_LIGHTING));
#endif
}
void PartPlate::render_right_arrow(const float* render_color, bool use_lighting) const
{
#if 0
	if (m_quadric == nullptr)
		return;
	double radius = m_grabber_box.size().y() * 0.5f;
	double height = radius * 2.0f;
	double position = m_grabber_box.size().x() * 0.8f;
	if (use_lighting)
		glsafe(::glEnable(GL_LIGHTING));

	glsafe(::glColor4fv(render_color));
	glsafe(::glPushMatrix());
	glsafe(::glTranslated(m_grabber_box.center().x(), m_grabber_box.center().y(), m_grabber_box.center().z()));
	glsafe(::glRotated(90.0, 0.0, 1.0, 0.0));
	glsafe(::glTranslated(0.0, 0.0, position));
	::gluQuadricOrientation(m_quadric, GLU_OUTSIDE);
	::gluCylinder(m_quadric, 0.9 * radius, 0.0, height, 36, 1);
	::gluQuadricOrientation(m_quadric, GLU_INSIDE);
	::gluDisk(m_quadric, 0.0, 0.9 * radius, 36, 1);
	glsafe(::glPopMatrix());

	if (use_lighting)
		glsafe(::glDisable(GL_LIGHTING));
#endif
}

void PartPlate::on_render_for_picking() const {
	//glsafe(::glDisable(GL_DEPTH_TEST));
	int hover_id = 0;
	std::array<float, 4> color = picking_color_component(hover_id);
	m_grabber_color[0] = color[0];
	m_grabber_color[1] = color[1];
	m_grabber_color[2] = color[2];
	m_grabber_color[3] = color[3];
	//render_grabber(m_grabber_color, false);
	render_rectangle_for_picking(m_triangles, m_grabber_color);
	hover_id = 1;
	color = picking_color_component(hover_id);
	m_grabber_color[0] = color[0];
	m_grabber_color[1] = color[1];
	m_grabber_color[2] = color[2];
	m_grabber_color[3] = color[3];
	//render_left_arrow(m_grabber_color, false);
	render_rectangle_for_picking(m_del_icon, m_grabber_color);
	hover_id = 2;
	color = picking_color_component(hover_id);
	m_grabber_color[0] = color[0];
	m_grabber_color[1] = color[1];
	m_grabber_color[2] = color[2];
	m_grabber_color[3] = color[3];
	render_rectangle_for_picking(m_orient_icon, m_grabber_color);
    hover_id = 3;
	color = picking_color_component(hover_id);
	m_grabber_color[0] = color[0];
	m_grabber_color[1] = color[1];
	m_grabber_color[2] = color[2];
	m_grabber_color[3] = color[3];
	render_rectangle_for_picking(m_arrange_icon, m_grabber_color);
	hover_id = 4;
	color = picking_color_component(hover_id);
	m_grabber_color[0] = color[0];
	m_grabber_color[1] = color[1];
	m_grabber_color[2] = color[2];
	m_grabber_color[3] = color[3];
	//render_right_arrow(m_grabber_color, false);
	render_rectangle_for_picking(m_lock_icon, m_grabber_color);
	hover_id = 5;
	color = picking_color_component(hover_id);
	m_grabber_color[0] = color[0];
	m_grabber_color[1] = color[1];
	m_grabber_color[2] = color[2];
	m_grabber_color[3] = color[3];
    if (m_partplate_list->render_plate_settings)
        render_rectangle_for_picking(m_plate_settings_icon, m_grabber_color);
}

std::array<float, 4> PartPlate::picking_color_component(int idx) const
{
	static const float INV_255 = 1.0f / 255.0f;
	unsigned int id = PLATE_BASE_ID - this->m_plate_index * GRABBER_COUNT - idx;
	return std::array<float, 4> {
		float((id >> 0) & 0xff)* INV_255, // red
			float((id >> 8) & 0xff)* INV_255, // greeen
			float((id >> 16) & 0xff)* INV_255, // blue
			float(picking_checksum_alpha_channel(id & 0xff, (id >> 8) & 0xff, (id >> 16) & 0xff))* INV_255
	};
}

void PartPlate::release_opengl_resource()
{
	if (m_vbo_id > 0) {
		glsafe(::glDeleteBuffers(1, &m_vbo_id));
		m_vbo_id = 0;
	}
	if (m_del_vbo_id > 0) {
		glsafe(::glDeleteBuffers(1, &m_del_vbo_id));
		m_del_vbo_id = 0;
	}
    if (m_orient_vbo_id > 0) {
		glsafe(::glDeleteBuffers(1, &m_orient_vbo_id));
		m_orient_vbo_id = 0;
	}
	if (m_arrange_vbo_id > 0) {
		glsafe(::glDeleteBuffers(1, &m_arrange_vbo_id));
		m_arrange_vbo_id = 0;
	}
	if (m_lock_vbo_id > 0) {
		glsafe(::glDeleteBuffers(1, &m_lock_vbo_id));
		m_lock_vbo_id = 0;
	}
	if (m_plate_settings_vbo_id > 0) {
		glsafe(::glDeleteBuffers(1, &m_plate_settings_vbo_id));
		m_plate_settings_vbo_id = 0;
	}
	if (m_plate_idx_vbo_id > 0) {
		glsafe(::glDeleteBuffers(1, &m_plate_idx_vbo_id));
		m_plate_idx_vbo_id = 0;
	}
	if (m_plate_name_vbo_id > 0) {
		glsafe(::glDeleteBuffers(1, &m_plate_name_vbo_id));
		m_plate_name_vbo_id = 0;
	}
}

std::vector<int> PartPlate::get_extruders(bool conside_custom_gcode) const
{
	std::vector<int> plate_extruders;
	// if gcode.3mf file
	if (m_model->objects.empty()) {
		for (int i = 0; i < slice_filaments_info.size(); i++) {
			plate_extruders.push_back(slice_filaments_info[i].id + 1);
		}
		return plate_extruders;
	}

	// if 3mf file
	const DynamicPrintConfig& glb_config = wxGetApp().preset_bundle->prints.get_edited_preset().config;
	int glb_support_intf_extr = glb_config.opt_int("support_interface_filament");
	int glb_support_extr = glb_config.opt_int("support_filament");
	bool glb_support = glb_config.opt_bool("enable_support");
    glb_support |= glb_config.opt_int("raft_layers") > 0;

	for (int obj_idx = 0; obj_idx < m_model->objects.size(); obj_idx++) {
		if (!contain_instance_totally(obj_idx, 0))
			continue;

		ModelObject* mo = m_model->objects[obj_idx];
		for (ModelVolume* mv : mo->volumes) {
			std::vector<int> volume_extruders = mv->get_extruders();
			plate_extruders.insert(plate_extruders.end(), volume_extruders.begin(), volume_extruders.end());
		}

		bool obj_support = false;
		const ConfigOption* obj_support_opt = mo->config.option("enable_support");
        const ConfigOption *obj_raft_opt    = mo->config.option("raft_layers");
		if (obj_support_opt != nullptr || obj_raft_opt != nullptr) {
            if (obj_support_opt != nullptr)
				obj_support = obj_support_opt->getBool();
            if (obj_raft_opt != nullptr)
				obj_support |= obj_raft_opt->getInt() > 0;
        }
		else
			obj_support = glb_support;

		if (!obj_support)
			continue;

		int obj_support_intf_extr = 0;
		const ConfigOption* support_intf_extr_opt = mo->config.option("support_interface_filament");
		if (support_intf_extr_opt != nullptr)
			obj_support_intf_extr = support_intf_extr_opt->getInt();
		if (obj_support_intf_extr != 0)
			plate_extruders.push_back(obj_support_intf_extr);
		else if (glb_support_intf_extr != 0)
			plate_extruders.push_back(glb_support_intf_extr);

		int obj_support_extr = 0;
		const ConfigOption* support_extr_opt = mo->config.option("support_filament");
		if (support_extr_opt != nullptr)
			obj_support_extr = support_extr_opt->getInt();
		if (obj_support_extr != 0)
			plate_extruders.push_back(obj_support_extr);
		else if (glb_support_extr != 0)
			plate_extruders.push_back(glb_support_extr);
	}

	if (conside_custom_gcode) {
		//BBS
		if (m_model->plates_custom_gcodes.find(m_plate_index) != m_model->plates_custom_gcodes.end()) {
			for (auto item : m_model->plates_custom_gcodes.at(m_plate_index).gcodes) {
				if (item.type == CustomGCode::Type::ToolChange)
					plate_extruders.push_back(item.extruder);
			}
		}
	}

	std::sort(plate_extruders.begin(), plate_extruders.end());
	auto it_end = std::unique(plate_extruders.begin(), plate_extruders.end());
	plate_extruders.resize(std::distance(plate_extruders.begin(), it_end));
	return plate_extruders;
}

std::vector<int> PartPlate::get_used_extruders()
{
	std::vector<int> used_extruders;
	// if gcode.3mf file
	if (m_model->objects.empty()) {
		for (int i = 0; i < slice_filaments_info.size(); i++) {
			used_extruders.push_back(slice_filaments_info[i].id + 1);
		}
		return used_extruders;
	}

	GCodeProcessorResult* result = get_slice_result();
	if (!result)
		return used_extruders;

	PrintEstimatedStatistics& ps = result->print_statistics;
	for (auto it = ps.volumes_per_extruder.begin(); it != ps.volumes_per_extruder.end(); it++) {
		used_extruders.push_back(it->first + 1);
	}
	return used_extruders;
}

Vec3d PartPlate::estimate_wipe_tower_size(const double w, const double wipe_volume) const
{
	Vec3d wipe_tower_size;
	std::vector<int> plate_extruders = get_extruders(true);
	double layer_height = 0.08f; // hard code layer height
	double max_height = 0.f;
	wipe_tower_size.setZero();
	wipe_tower_size(0) = w;

	ConfigOption* layer_height_opt = wxGetApp().preset_bundle->prints.get_edited_preset().config.option("layer_height");
	if (layer_height_opt)
		layer_height = layer_height_opt->getFloat();

	// empty plate
	if (plate_extruders.empty())
		return wipe_tower_size;

	for (int obj_idx = 0; obj_idx < m_model->objects.size(); obj_idx++) {
		if (!contain_instance_totally(obj_idx, 0))
			continue;

		BoundingBoxf3 bbox = m_model->objects[obj_idx]->bounding_box();
		max_height = std::max(bbox.size().z(), max_height);
	}
	wipe_tower_size(2) = max_height;

	const DynamicPrintConfig &dconfig = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    auto timelapse_type    = dconfig.option<ConfigOptionEnum<TimelapseType>>("timelapse_type");
    bool timelapse_enabled = timelapse_type ? (timelapse_type->value == TimelapseType::tlSmooth) : false;

	double depth = wipe_volume * (plate_extruders.size() - 1) / (layer_height * w);
    if (timelapse_enabled || depth > EPSILON) {
		float min_wipe_tower_depth = 0.f;
		auto iter = WipeTower::min_depth_per_height.begin();
		while (iter != WipeTower::min_depth_per_height.end()) {
			auto curr_height_to_depth = *iter;

			// This is the case that wipe tower height is lower than the first min_depth_to_height member.
			if (curr_height_to_depth.first >= max_height) {
				min_wipe_tower_depth = curr_height_to_depth.second;
				break;
			}

			iter++;

			// If curr_height_to_depth is the last member, use its min_depth.
			if (iter == WipeTower::min_depth_per_height.end()) {
				min_wipe_tower_depth = curr_height_to_depth.second;
				break;
			}

			// If wipe tower height is between the current and next member, set the min_depth as linear interpolation between them
			auto next_height_to_depth = *iter;
			if (next_height_to_depth.first > max_height) {
				float height_base = curr_height_to_depth.first;
				float height_diff = next_height_to_depth.first - curr_height_to_depth.first;
				float min_depth_base = curr_height_to_depth.second;
				float depth_diff = next_height_to_depth.second - curr_height_to_depth.second;

				min_wipe_tower_depth = min_depth_base + (max_height - curr_height_to_depth.first) / height_diff * depth_diff;
				break;
			}
		}
		depth = std::max((double)min_wipe_tower_depth, depth);
	}
	wipe_tower_size(1) = depth;
	return wipe_tower_size;
}

bool PartPlate::operator<(PartPlate& plate) const
{
	int index = plate.get_index();
	return (this->m_plate_index < index);
}

//set the plate's index
void PartPlate::set_index(int index)
{
	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": plate_id update from %1% to %2%") % m_plate_index % index;

	m_plate_index = index;
	if (m_print != nullptr)
		m_print->set_plate_index(index);
}

void PartPlate::clear(bool clear_sliced_result)
{
	obj_to_instance_set.clear();
	instance_outside_set.clear();
	if (clear_sliced_result) {
		m_ready_for_slice = true;
		update_slice_result_valid_state(false);
	}
	m_name_texture.reset();
	return;
}

/* size and position related functions*/
//set position and size
void PartPlate::set_pos_and_size(Vec3d& origin, int width, int depth, int height, bool with_instance_move)
{
	bool size_changed = false; //size changed means the machine changed
	bool pos_changed = false;

	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": plate_id %1%, before, origin {%2%,%3%,%4%}, plate_width %5%, plate_depth %6%, plate_height %7%")\
		% m_plate_index % m_origin.x() % m_origin.y() % m_origin.z() % m_width % m_depth % m_height;
	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": with_instance_move %1%, after, origin {%2%,%3%,%4%}, plate_width %5%, plate_depth %6%, plate_height %7%")\
		% with_instance_move % origin.x() % origin.y() % origin.z() % width % depth % height;
	size_changed = ((width != m_width) || (depth != m_depth) || (height != m_height));
	pos_changed = (m_origin != origin);

	if ((!size_changed) && (!pos_changed))
	{
		//size and position the same with before, just return
		return;
	}

	if (with_instance_move)
	{
		for (std::set<std::pair<int, int>>::iterator it = obj_to_instance_set.begin(); it != obj_to_instance_set.end(); ++it) {
			int obj_id = it->first;
			int instance_id = it->second;
			ModelObject* object = m_model->objects[obj_id];
			ModelInstance* instance = object->instances[instance_id];

			//move this instance into the new plate's same position
			Vec3d offset = instance->get_transformation().get_offset();
			int off_x, off_y;

			if (size_changed)
			{
				//change position due to the bed size changes
				//off_x = (width - m_width) * m_plate_index + (width - m_width) / 2;
				//off_y = (depth - m_depth) * m_plate_index + (depth - m_depth) / 2;
				off_x = origin.x() - m_origin.x() + (width - m_width) / 2;
				off_y = origin.y() - m_origin.y() + (depth - m_depth) / 2;
			}
			else
			{
				//change position due to the plate moves
				off_x = origin.x() - m_origin.x();
				off_y = origin.y() - m_origin.y();
			}
			offset.x() = offset.x() + off_x;
			offset.y() = offset.y() + off_y;

			BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": object %1%, instance %2%, moved {%3%,%4%} to {%5%, %6%}")\
				% obj_id % instance_id % off_x % off_y % offset.x() % offset.y();

			instance->set_offset(offset);
			object->invalidate_bounding_box();
		}
	}
	else
	{
		clear();
	}

    if (m_print)
        m_print->set_plate_origin(origin);

	m_origin = origin;
	m_width = width;
	m_depth = depth;
	m_height = height;

	return;
}

//get the plate's center point origin
Vec3d PartPlate::get_center_origin()
{
	Vec3d origin;

	origin(0) = (m_bounding_box.min(0) + m_bounding_box.max(0)) / 2;//m_origin.x() + m_width / 2;
	origin(1) = (m_bounding_box.min(1) + m_bounding_box.max(1)) / 2; //m_origin.y() + m_depth / 2;
	origin(2) = m_origin.z();

	return origin;
}

void PartPlate::generate_plate_name_texture()
{
	// generate m_name_texture texture from m_name with generate_from_text_string
	m_name_texture.reset();
	auto text = m_name.empty()? _L("Untitled") : m_name;
	wxCoord w, h;
	auto* font = &Label::Head_48;
	wxColour foreground(0x0, 0x96, 0x88, 0xff);
    if (!m_name_texture.generate_from_text_string(text.ToStdString(), *font, *wxBLACK, foreground))
		BOOST_LOG_TRIVIAL(error) << "PartPlate::generate_plate_name_texture(): generate_from_text_string() failed";
    auto bed_ext = get_extents(m_shape);
    auto factor = bed_ext.size()(1) / 200.0;
	ExPolygon poly;
	float offset_x = 1;
    w = int(factor * (m_name_texture.get_width() * 16) / m_name_texture.get_height());
    h = int(factor * 16);
    Vec2d p = m_shape[3] + Vec2d(0, h*0.6);
	poly.contour.append({ scale_(p(0) + PARTPLATE_ICON_GAP_LEFT + offset_x), scale_(p(1) - h + PARTPLATE_TEXT_OFFSET_Y) });
	poly.contour.append({ scale_(p(0) + PARTPLATE_ICON_GAP_LEFT + w - offset_x), scale_(p(1) - h + PARTPLATE_TEXT_OFFSET_Y) });
	poly.contour.append({ scale_(p(0) + PARTPLATE_ICON_GAP_LEFT + w - offset_x), scale_(p(1) - PARTPLATE_TEXT_OFFSET_Y)});
	poly.contour.append({ scale_(p(0) + PARTPLATE_ICON_GAP_LEFT + offset_x), scale_(p(1) - PARTPLATE_TEXT_OFFSET_Y) });

	auto triangles = triangulate_expolygon_2f(poly, NORMALS_UP);
	if (!m_plate_name_icon.set_from_triangles(triangles, GROUND_Z))
		BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << "Unable to generate geometry buffers for icons\n";

	if (m_plate_name_vbo_id > 0) {
		glsafe(::glDeleteBuffers(1, &m_plate_name_vbo_id));
		m_plate_name_vbo_id = 0;
	}
}
void PartPlate::set_plate_name(const std::string& name) 
{ 
	// compare if name equal to m_name, case sensitive
    if (boost::equals(m_name, name))
        return;

	m_name = name;
    if (m_print != nullptr)
        m_print->set_plate_name(name);

	generate_plate_name_texture();
}

//get the print's object, result and index
void PartPlate::get_print(PrintBase** print, GCodeResult** result, int* index)
{
	if (print && (printer_technology == PrinterTechnology::ptFFF))
		*print = m_print;

	if (result)
		*result = m_gcode_result;

	if (index)
		*index = m_print_index;

	return;
}

//set the print object, result and it's index
void PartPlate::set_print(PrintBase* print, GCodeResult* result, int index)
{
	if (printer_technology == PrinterTechnology::ptFFF)
		m_print = static_cast<Print*>(print);
	//todo, for other printers

	m_gcode_result = result;
	if (index >= 0)
		m_print_index = index;

	m_print->set_plate_origin(m_origin);

	return;
}

std::string PartPlate::get_gcode_filename()
{
	if (is_slice_result_valid() && get_slice_result()) {
		return m_gcode_result->filename;
	}
	return "";
}

bool PartPlate::is_valid_gcode_file()
{
	if (get_gcode_filename().empty())
		return false;
	boost::filesystem::path gcode_file(m_gcode_result->filename);
	if (!boost::filesystem::exists(gcode_file)) {
		BOOST_LOG_TRIVIAL(info) << "invalid gcode file, file is missing, file = " << m_gcode_result->filename;
		return false;
	}
	return true;
}

ModelInstance* PartPlate::get_instance(int obj_id, int instance_id)
{
	if (!contain_instance(obj_id, instance_id))
		return nullptr;
	else
		return m_model->objects[obj_id]->instances[instance_id];
}

/* instance related operations*/
//judge whether instance is bound in plate or not
bool PartPlate::contain_instance(int obj_id, int instance_id)
{
	bool result = false;
	std::set<std::pair<int, int>>::iterator it;

	it = obj_to_instance_set.find(std::pair(obj_id, instance_id));
	if (it != obj_to_instance_set.end()) {
		result = true;
	}

	return result;
}

//judge whether instance is bound in plate or not
bool PartPlate::contain_instance_totally(ModelObject* object, int instance_id) const
{
	bool result = false;
	int obj_id = -1;

	for (int index = 0; index < m_model->objects.size(); index ++)
	{
		if (m_model->objects[index] == object)
		{
			obj_id = index;
			break;
		}
	}

	if ((obj_id >= 0 ) && (obj_id < m_model->objects.size()))
		result = contain_instance_totally(obj_id, instance_id);

	return result;
}

//judge whether instance is totally included in plate or not
bool PartPlate::contain_instance_totally(int obj_id, int instance_id) const
{
	bool result = false;
	std::set<std::pair<int, int>>::iterator it;

	it = obj_to_instance_set.find(std::pair(obj_id, instance_id));
	if (it != obj_to_instance_set.end()) {
		it = instance_outside_set.find(std::pair(obj_id, instance_id));
		if (it == instance_outside_set.end())
			result = true;
	}

	return result;
}

//check whether instance is outside the plate or not
bool PartPlate::check_outside(int obj_id, int instance_id, BoundingBoxf3* bounding_box)
{
	bool outside = true;

	ModelObject* object = m_model->objects[obj_id];
	ModelInstance* instance = object->instances[instance_id];

	BoundingBoxf3 instance_box = bounding_box? *bounding_box: object->instance_convex_hull_bounding_box(instance_id);
	Polygon hull = instance->convex_hull_2d();
	Vec3d up_point(m_origin.x() + m_width + Slic3r::BuildVolume::SceneEpsilon, m_origin.y() + m_depth + Slic3r::BuildVolume::SceneEpsilon, m_origin.z() + m_height + Slic3r::BuildVolume::SceneEpsilon);
	Vec3d low_point(m_origin.x() - Slic3r::BuildVolume::SceneEpsilon, m_origin.y() - Slic3r::BuildVolume::SceneEpsilon, m_origin.z() - Slic3r::BuildVolume::SceneEpsilon);
	BoundingBoxf3 plate_box(low_point, up_point);

	if (plate_box.contains(instance_box))
	{
		if (m_exclude_bounding_box.size() > 0)
		{
			int index;
			for (index = 0; index < m_exclude_bounding_box.size(); index ++)
			{
				Polygon p = m_exclude_bounding_box[index].polygon(true);  // instance convex hull is scaled, so we need to scale here
				if (intersection({ p }, { hull }).empty() == false)
				//if (m_exclude_bounding_box[index].intersects(instance_box))
				{
					break;
				}
			}
			if (index >= m_exclude_bounding_box.size())
				outside = false;
		}
		else
			outside = false;
	}

	return outside;
}

//judge whether instance is intesected with plate or not
bool PartPlate::intersect_instance(int obj_id, int instance_id, BoundingBoxf3* bounding_box)
{
	bool result = false;

	if (!valid_instance(obj_id, instance_id))
	{
		BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": plate_id %1%, invalid obj_id %2%, instance_id %3%") % m_plate_index % obj_id % instance_id;
		return false;
	}

	if (m_printable)
	{
		ModelObject* object = m_model->objects[obj_id];
		ModelInstance* instance = object->instances[instance_id];
		BoundingBoxf3 instance_box = bounding_box? *bounding_box: object->instance_convex_hull_bounding_box(instance_id);
		Vec3d up_point(m_origin.x() + m_width, m_origin.y() + m_depth, m_origin.z() + m_height);
		Vec3d low_point(m_origin.x(), m_origin.y(), m_origin.z() - 5.0f);
		BoundingBoxf3 plate_box(low_point, up_point);

		result = plate_box.intersects(instance_box);
	}
	else
	{
		result = is_left_top_of(obj_id, instance_id);
	}

	return result;
}

//judge whether the plate's origin is at the left of instance or not
bool PartPlate::is_left_top_of(int obj_id, int instance_id)
{
	bool result = false;

	if (!valid_instance(obj_id, instance_id))
	{
		BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": plate_id %1%, invalid obj_id %2%, instance_id %3%") % m_plate_index % obj_id % instance_id;
		return false;
	}

	ModelObject* object = m_model->objects[obj_id];
	ModelInstance* instance = object->instances[instance_id];
	std::pair<int, int> pair(obj_id, instance_id);
	BoundingBoxf3 instance_box = object->instance_convex_hull_bounding_box(instance_id);

	result = (m_origin.x() <= instance_box.min.x()) && (m_origin.y() >= instance_box.min.y());
	return result;
}

//add an instance into plate
int PartPlate::add_instance(int obj_id, int instance_id, bool move_position, BoundingBoxf3* bounding_box)
{
	if (!valid_instance(obj_id, instance_id))
	{
		BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(": plate_id %1%, invalid obj_id %2%, instance_id %3%, move_position %4%") % m_plate_index % obj_id % instance_id % move_position;
		return -1;
	}

	ModelObject* object = m_model->objects[obj_id];
	ModelInstance* instance = object->instances[instance_id];
	std::pair<int, int> pair(obj_id, instance_id);

	obj_to_instance_set.insert(pair);

	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": plate_id %1%, add instance obj_id %2%, instance_id %3%, move_position %4%") % m_plate_index % obj_id % instance_id % move_position;

	if (move_position)
	{
		//move this instance into the new position
		Vec3d center = get_center_origin();
		center.z() = instance->get_transformation().get_offset(Z);

		instance->set_offset(center);
		object->invalidate_bounding_box();
	}

	//need to judge whether this instance has an outer part
	bool outside = check_outside(obj_id, instance_id, bounding_box);
	if (outside)
		instance_outside_set.insert(pair);

	if (m_ready_for_slice && outside)
	{
		m_ready_for_slice = false;
	}
	else if ((obj_to_instance_set.size() == 1) && (!m_ready_for_slice) && !outside)
	{
		m_ready_for_slice = true;
	}

	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": plate %1% , m_ready_for_slice changes to %2%") % m_plate_index %m_ready_for_slice;
	return 0;
}

//remove instance from plate
int PartPlate::remove_instance(int obj_id, int instance_id)
{
	bool result;
	std::set<std::pair<int, int>>::iterator it;

	it = obj_to_instance_set.find(std::pair(obj_id, instance_id));
	if (it != obj_to_instance_set.end()) {
		obj_to_instance_set.erase(it);
		BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(":plate_id %1%, found obj_id %2%, instance_id %3%") % m_plate_index % obj_id % instance_id;
		result = 0;
	}
	else {
		BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": plate_id %1%, can not find obj_id %2%, instance_id %3%") % m_plate_index % obj_id % instance_id;
		result = -1;
		return result;
	}

	it = instance_outside_set.find(std::pair(obj_id, instance_id));
	if (it != instance_outside_set.end()) {
		instance_outside_set.erase(it);
	}
	if (!m_ready_for_slice)
		update_states();

	return result;
}

//translate instance on the plate
void PartPlate::translate_all_instance(Vec3d position)
{
    for (std::set<std::pair<int, int>>::iterator it = obj_to_instance_set.begin(); it != obj_to_instance_set.end(); ++it)
    {
        int obj_id = it->first;
        int instance_id = it->second;

        if ((obj_id >= 0) && (obj_id < m_model->objects.size()))
        {
            ModelObject* object = m_model->objects[obj_id];
            if ((instance_id >= 0) && (instance_id < object->instances.size()))
            {
                ModelInstance* instance = object->instances[instance_id];
                const Vec3d& offset =  instance->get_offset();
                instance->set_offset(offset + position);
            }
        }
    }
    return;
}


//update instance exclude state
void PartPlate::update_instance_exclude_status(int obj_id, int instance_id, BoundingBoxf3* bounding_box)
{
	bool outside;
	std::set<std::pair<int, int>>::iterator it;

	outside = check_outside(obj_id, instance_id, bounding_box);

	it = instance_outside_set.find(std::pair(obj_id, instance_id));
	if (it == instance_outside_set.end()) {
		if (outside)
			instance_outside_set.insert(std::pair(obj_id, instance_id));
	}
	else {
		if (!outside)
			instance_outside_set.erase(it);
	}
}

//update object's index caused by original object deleted
void PartPlate::update_object_index(int obj_idx_removed, int obj_idx_max)
{
	std::set<std::pair<int, int>> temp_set;
	std::set<std::pair<int, int>>::iterator it;
	//update the obj_to_instance_set
	for (it = obj_to_instance_set.begin(); it != obj_to_instance_set.end(); ++it)
	{
		if (it->first >= obj_idx_removed)
			temp_set.insert(std::pair(it->first-1, it->second));
		else
			temp_set.insert(std::pair(it->first, it->second));
	}
	obj_to_instance_set.clear();
	obj_to_instance_set = temp_set;

	//update the instance_outside_set
	temp_set.clear();
	for (it = instance_outside_set.begin(); it != instance_outside_set.end(); ++it)
	{
		if (it->first >= obj_idx_removed)
			temp_set.insert(std::pair(it->first - 1, it->second));
		else
			temp_set.insert(std::pair(it->first, it->second));
	}
	instance_outside_set.clear();
	instance_outside_set = temp_set;

}

int PartPlate::printable_instance_size()
{
    int size = 0;
    for (std::set<std::pair<int, int>>::iterator it = obj_to_instance_set.begin(); it != obj_to_instance_set.end(); ++it) {
        int obj_id      = it->first;
        int instance_id = it->second;

        if (obj_id >= m_model->objects.size())
			continue;

        ModelObject *  object   = m_model->objects[obj_id];
        ModelInstance *instance = object->instances[instance_id];

        if ((instance->printable) && (instance_outside_set.find(std::pair(obj_id, instance_id)) == instance_outside_set.end())) {
            size++;
        }
    }
    return size;
}

//whether it is has printable instances
bool PartPlate::has_printable_instances()
{
	bool result = false;

	for (std::set<std::pair<int, int>>::iterator it = obj_to_instance_set.begin(); it != obj_to_instance_set.end(); ++it)
	{
		int obj_id = it->first;
		int instance_id = it->second;

		if (obj_id >= m_model->objects.size())
			continue;

		ModelObject* object = m_model->objects[obj_id];
		ModelInstance* instance = object->instances[instance_id];

		if ((instance->printable)&&(instance_outside_set.find(std::pair(obj_id, instance_id)) == instance_outside_set.end()))
		{
			result = true;
			break;
		}
	}

	return result;
}

//move instances to left or right PartPlate
void PartPlate::move_instances_to(PartPlate& left_plate, PartPlate& right_plate, BoundingBoxf3* bounding_box)
{
	for (std::set<std::pair<int, int>>::iterator it = obj_to_instance_set.begin(); it != obj_to_instance_set.end(); ++it)
	{
		int obj_id = it->first;
		int instance_id = it->second;

		if (left_plate.intersect_instance(obj_id, instance_id, bounding_box))
			left_plate.add_instance(obj_id, instance_id, false, bounding_box);
		else
			right_plate.add_instance(obj_id, instance_id, false, bounding_box);
	}

	return;
}

void PartPlate::generate_logo_polygon(ExPolygon &logo_polygon)
{
	if (m_shape.size() == 4)
	{
		//rectangle case
		for (int i = 0; i < 4; i++)
		{
			const Vec2d& p = m_shape[i];
			if ((i  == 0) || (i  == 1)) {
				logo_polygon.contour.append({ scale_(p(0)), scale_(p(1) - 12.f) });
			}
			else {
				logo_polygon.contour.append({ scale_(p(0)), scale_(p(1)) });
			}
		}
	}
	else {
		for (const Vec2d& p : m_shape) {
			logo_polygon.contour.append({ scale_(p(0)), scale_(p(1)) });
		}
	}
}

void PartPlate::generate_print_polygon(ExPolygon &print_polygon)
{
	auto compute_points = [&print_polygon](Vec2d& center, double radius, double start_angle, double stop_angle, int count)
	{
		double angle, angle_steps;
		angle_steps = (stop_angle - start_angle) / (count - 1);
		for(int j = 0; j < count; j++ )
		{
			double angle = start_angle + j * angle_steps;
			double x = center(0) + ::cos(angle) * radius;
			double y = center(1) + ::sin(angle) * radius;
			print_polygon.contour.append({ scale_(x), scale_(y) });
		}
	};

	int points_count = 8;
	if (m_shape.size() == 4)
	{
			//rectangle case
			for (int i = 0; i < 4; i++)
			{
				const Vec2d& p = m_shape[i];
				Vec2d center;
				double start_angle, stop_angle, angle_steps, radius_x, radius_y, radius;
				switch (i) {
					case 0:
						radius = 5.f;
						center(0) = p(0) + radius;
						center(1) = p(1) + radius;
						start_angle = PI;
						stop_angle = 1.5 * PI;
						compute_points(center, radius, start_angle, stop_angle, points_count);
						break;
					case 1:
						print_polygon.contour.append({ scale_(p(0)), scale_(p(1)) });
						break;
					case 2:
						radius_x = (int)(p(0)) % 10;
                        radius_y = (int)(p(1)) % 10;
						radius = (radius_x > radius_y)?radius_y: radius_x;
						if (radius < 5.0)
							radius = 5.f;
						center(0) = p(0) - radius;
						center(1) = p(1) - radius;
						start_angle = 0;
						stop_angle = 0.5 * PI;
						compute_points(center, radius, start_angle, stop_angle, points_count);
						break;
					case 3:
                        radius_x = (int)(p(0)) % 10;
						radius_y = (int)(p(1)) % 10;
						radius = (radius_x > radius_y)?radius_y: radius_x;
						if (radius < 5.0)
							radius = 5.f;
						center(0) = p(0) + radius;
						center(1) = p(1) - radius;
						start_angle = 0.5 * PI;
						stop_angle = PI;
						compute_points(center, radius, start_angle, stop_angle, points_count);
						break;
				}
			}
	}
	else {
		for (const Vec2d& p : m_shape) {
			print_polygon.contour.append({ scale_(p(0)), scale_(p(1)) });
		}
	}
}

void PartPlate::generate_exclude_polygon(ExPolygon &exclude_polygon)
{
	auto compute_exclude_points = [&exclude_polygon](Vec2d& center, double radius, double start_angle, double stop_angle, int count)
	{
		double angle, angle_steps;
		angle_steps = (stop_angle - start_angle) / (count - 1);
		for(int j = 0; j < count; j++ )
		{
			double angle = start_angle + j * angle_steps;
			double x = center(0) + ::cos(angle) * radius;
			double y = center(1) + ::sin(angle) * radius;
			exclude_polygon.contour.append({ scale_(x), scale_(y) });
		}
	};

	int points_count = 8;
	if (m_exclude_area.size() == 4)
	{
			//rectangle case
			for (int i = 0; i < 4; i++)
			{
				const Vec2d& p = m_exclude_area[i];
				Vec2d center;
				double start_angle, stop_angle, angle_steps, radius_x, radius_y, radius;
				switch (i) {
					case 0:
						radius = 5.f;
						center(0) = p(0) + radius;
						center(1) = p(1) + radius;
						start_angle = PI;
						stop_angle = 1.5 * PI;
						compute_exclude_points(center, radius, start_angle, stop_angle, points_count);
						break;
					case 1:
						exclude_polygon.contour.append({ scale_(p(0)), scale_(p(1)) });
						break;
					case 2:
						radius = 3.f;
						center(0) = p(0) - radius;
						center(1) = p(1) - radius;
						start_angle = 0;
						stop_angle = 0.5 * PI;
						compute_exclude_points(center, radius, start_angle, stop_angle, points_count);
						break;
					case 3:
						exclude_polygon.contour.append({ scale_(p(0)), scale_(p(1)) });
						break;
				}
			}
	}
	else {
		for (const Vec2d& p : m_exclude_area) {
			exclude_polygon.contour.append({ scale_(p(0)), scale_(p(1)) });
		}
	}
}

bool PartPlate::set_shape(const Pointfs& shape, const Pointfs& exclude_areas, Vec2d position, float height_to_lid, float height_to_rod)
{
	Pointfs new_shape, new_exclude_areas;

	for (const Vec2d& p : shape) {
		new_shape.push_back(Vec2d(p.x() + position.x(), p.y() + position.y()));
	}

	for (const Vec2d& p : exclude_areas) {
		new_exclude_areas.push_back(Vec2d(p.x() + position.x(), p.y() + position.y()));
	}
	if ((m_shape == new_shape)&&(m_exclude_area == new_exclude_areas)
		&&(m_height_to_lid == height_to_lid)&&(m_height_to_rod == height_to_rod)) {
		BOOST_LOG_TRIVIAL(info) << "PartPlate same shape, skip directly";
		return false;
	}

	m_height_to_lid =  height_to_lid;
	m_height_to_rod =  height_to_rod;

	if ((m_shape != new_shape) || (m_exclude_area != new_exclude_areas))
	{
		/*m_shape.clear();
		for (const Vec2d& p : shape) {
			m_shape.push_back(Vec2d(p.x() + position.x(), p.y() + position.y()));
		}

		m_exclude_area.clear();
		for (const Vec2d& p : exclude_areas) {
			m_exclude_area.push_back(Vec2d(p.x() + position.x(), p.y() + position.y()));
		}*/
		m_shape = std::move(new_shape);
		m_exclude_area = std::move(new_exclude_areas);

		calc_bounding_boxes();

		ExPolygon logo_poly;
		generate_logo_polygon(logo_poly);
		if (!m_logo_triangles.set_from_triangles(triangulate_expolygon_2f(logo_poly, NORMALS_UP), GROUND_Z+0.02f))
			BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":Unable to create logo triangles\n";
		else {
			;
		}

		ExPolygon poly;
		/*for (const Vec2d& p : m_shape) {
			poly.contour.append({ scale_(p(0)), scale_(p(1)) });
		}*/
		generate_print_polygon(poly);
		calc_triangles(poly);

		ExPolygon exclude_poly;
		/*for (const Vec2d& p : m_exclude_area) {
			exclude_poly.contour.append({ scale_(p(0)), scale_(p(1)) });
		}*/
		generate_exclude_polygon(exclude_poly);
		calc_exclude_triangles(exclude_poly);

		const BoundingBox& pp_bbox = poly.contour.bounding_box();
		calc_gridlines(poly, pp_bbox);

		//calc_vertex_for_icons_background(5, m_del_and_background_icon);
		//calc_vertex_for_icons(4, m_del_icon);
		calc_vertex_for_icons(0, m_del_icon);
		calc_vertex_for_icons(1, m_orient_icon);
		calc_vertex_for_icons(2, m_arrange_icon);
		calc_vertex_for_icons(3, m_lock_icon);
		calc_vertex_for_icons(4, m_plate_settings_icon);
		//calc_vertex_for_number(0, (m_plate_index < 9), m_plate_idx_icon);
		calc_vertex_for_number(0, false, m_plate_idx_icon);
		// calc vertex for plate name
		generate_plate_name_texture();
	}

	calc_height_limit();

	release_opengl_resource();

	return true;
}

const BoundingBox PartPlate::get_bounding_box_crd()
{
	const auto plate_shape = Slic3r::Polygon::new_scale(m_shape);

	return plate_shape.bounding_box();
}

bool PartPlate::contains(const Vec3d& point) const
{
	return m_bounding_box.contains(point);
}

bool PartPlate::contains(const GLVolume& v) const
{
	return m_bounding_box.contains(v.bounding_box());
}

bool PartPlate::contains(const BoundingBoxf3& bb) const
{
	// Allow the objects to protrude below the print bed
	BoundingBoxf3 print_volume(Vec3d(m_bounding_box.min(0), m_bounding_box.min(1), 0.0), Vec3d(m_bounding_box.max(0), m_bounding_box.max(1), 1e3));
	print_volume.min(2) = -1e10;
	print_volume.min(0) -= Slic3r::BuildVolume::BedEpsilon;
	print_volume.min(1) -= Slic3r::BuildVolume::BedEpsilon;
	print_volume.max(0) += Slic3r::BuildVolume::BedEpsilon;
	print_volume.max(1) += Slic3r::BuildVolume::BedEpsilon;
	return print_volume.contains(bb);
}

bool PartPlate::intersects(const BoundingBoxf3& bb) const
{
	// Allow the objects to protrude below the print bed
	BoundingBoxf3 print_volume(Vec3d(m_bounding_box.min(0), m_bounding_box.min(1), 0.0), Vec3d(m_bounding_box.max(0), m_bounding_box.max(1), 1e3));
	print_volume.min(2) = -1e10;
	print_volume.min(0) -= Slic3r::BuildVolume::BedEpsilon;
	print_volume.min(1) -= Slic3r::BuildVolume::BedEpsilon;
	print_volume.max(0) += Slic3r::BuildVolume::BedEpsilon;
	print_volume.max(1) += Slic3r::BuildVolume::BedEpsilon;
	return print_volume.intersects(bb);
}

void PartPlate::render(bool bottom, bool only_body, bool force_background_color, HeightLimitMode mode, int hover_id)
{
	glsafe(::glEnable(GL_DEPTH_TEST));
	glsafe(::glEnable(GL_BLEND));
	glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
	glsafe(::glEnableClientState(GL_VERTEX_ARRAY));

	if (!bottom) {
		// draw background
		render_background(force_background_color);

		render_exclude_area(force_background_color);
	}

	render_grid(bottom);

	if (!bottom && m_selected && !force_background_color) {
		render_logo(bottom);
	}

	render_height_limit(mode);

	render_icons(bottom, only_body, hover_id);
	if (!force_background_color){
		render_only_numbers(bottom);
	}
	glsafe(::glDisableClientState(GL_VERTEX_ARRAY));
	glsafe(::glDisable(GL_BLEND));

	//if (with_label) {
	//	render_label(canvas);
	//}
	glsafe(::glDisable(GL_DEPTH_TEST));
}

void PartPlate::set_selected() {
	m_selected = true;
}

void PartPlate::set_unselected() {
	m_selected = false;
}


/*status related functions*/
//update status
void PartPlate::update_states()
{
	//currently let judge outside partplate when plate is empty
	/*if (obj_to_instance_set.size() == 0)
	{
		m_ready_for_slice = false;
		return;
	}*/
	m_ready_for_slice = true;
	for (std::set<std::pair<int, int>>::iterator it = obj_to_instance_set.begin(); it != obj_to_instance_set.end(); ++it) {
		int obj_id = it->first;
		int instance_id = it->second;

		//if (check_outside(obj_id, instance_id))
		if (instance_outside_set.find(std::pair(obj_id, instance_id)) != instance_outside_set.end())
		{
			m_ready_for_slice = false;
			//currently only check whether ready to slice
			break;
		}
	}

	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": plate %1% , m_ready_for_slice changes to %2%") % m_plate_index %m_ready_for_slice;
	return;
}

/*slice related functions*/
//invalid sliced result
void PartPlate::update_slice_result_valid_state(bool valid)
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": plate %1% , update slice result from %2% to %3%") % m_plate_index %m_slice_result_valid %valid;
    m_slice_result_valid = valid;
    if (valid)
        m_slice_percent = 100.0f;
    else {
        m_slice_percent = -1.0f;
    }
}

//update current slice context into backgroud slicing process
void PartPlate::update_slice_context(BackgroundSlicingProcess & process)
{
	auto statuscb = [this](const Slic3r::PrintBase::SlicingStatus& status) {
		Slic3r::SlicingStatusEvent *event = new Slic3r::SlicingStatusEvent(EVT_SLICING_UPDATE, 0, status);
		//BBS: GUI refactor: add plate info befor message
		if (status.message_type == Slic3r::PrintStateBase::SlicingDefaultNotification) {
			auto temp = Slic3r::format(_u8L(" plate %1%:"), std::to_string(m_plate_index + 1));
			event->status.text = temp + event->status.text;
		}
		wxQueueEvent(m_plater, event);
	};

	process.set_fff_print(m_print);
	process.set_gcode_result(m_gcode_result);
	process.select_technology(this->printer_technology);
	process.set_current_plate(this);
	m_print->set_status_callback(statuscb);
	process.switch_print_preprocess();

	return;
}

// BBS: delay calc gcode path in backup dir
std::string PartPlate::get_tmp_gcode_path()
{
    if (m_tmp_gcode_path.empty()) {
        boost::filesystem::path temp_path(m_model->get_backup_path("Metadata"));
        temp_path /= (boost::format(".%1%.%2%.gcode") % get_current_pid() %
                      GLOBAL_PLATE_INDEX++).str();
        m_tmp_gcode_path = temp_path.string();
    }
    return m_tmp_gcode_path;
}

std::string PartPlate::get_temp_config_3mf_path()
{
	if (m_temp_config_3mf_path.empty()) {
		boost::filesystem::path temp_path(m_model->get_backup_path("Metadata"));
		temp_path /= (boost::format(".%1%.%2%_config.3mf") % get_current_pid() %
			GLOBAL_PLATE_INDEX++).str();
		m_temp_config_3mf_path = temp_path.string();

	}
	return m_temp_config_3mf_path;
}

// load gcode from file
int PartPlate::load_gcode_from_file(const std::string& filename)
{
	int ret = 0;

	// process gcode
	DynamicPrintConfig full_config = wxGetApp().preset_bundle->full_config();
	full_config.apply(m_config, true);
	m_print->apply(*m_model, full_config);
	//BBS: need to apply two times, for after the first apply, the m_print got its object,
	//which will affect the config when new_full_config.normalize_fdm(used_filaments);
	m_print->apply(*m_model, full_config);

	// BBS: use backup path to save temp gcode
    // auto path = get_tmp_gcode_path();
    // if (boost::filesystem::exists(boost::filesystem::path(path))) {
    //	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": file %1% exists, delete it firstly") % filename.c_str();
    //	boost::nowide::remove(path.c_str());
    //}

    // std::error_code error = rename_file(filename, path);
    // if (error) {
    //	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format("Failed to rename the output G-code file from %1% to %2%, error code %3%") % filename.c_str() % path.c_str() %
    //error.message(); 	return -1;
    //}
    if (boost::filesystem::exists(filename)) {
        assert(m_tmp_gcode_path.empty());
        m_tmp_gcode_path         = filename;
        m_gcode_result->filename = filename;
        m_print->set_gcode_file_ready();

        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": from %1% to %2%, finished") % filename.c_str() % filename.c_str();
    }

	update_slice_result_valid_state(true);
	m_ready_for_slice = true;
	return ret;
}

int PartPlate::load_thumbnail_data(std::string filename)
{
	bool result = true;
	wxImage img;
	if (boost::algorithm::iends_with(filename, ".png")) {
		result = img.LoadFile(wxString::FromUTF8(filename.c_str()), wxBITMAP_TYPE_PNG);
		img = img.Mirror(false);
	}
	if (result) {
		thumbnail_data.set(img.GetWidth(), img.GetHeight());
		for (int i = 0; i < img.GetWidth() * img.GetHeight(); i++) {
			memcpy(&thumbnail_data.pixels[4 * i], (unsigned char*)(img.GetData() + 3 * i), 3);
			if (img.HasAlpha()) {
				thumbnail_data.pixels[4 * i + 3] = *(unsigned char*)(img.GetAlpha() + i);
			}
		}
	} else {
		return -1;
	}
	return 0;
}

int PartPlate::load_pattern_thumbnail_data(std::string filename)
{
	bool result = true;
	wxImage img;
	result = load_image(filename, img);
	if (result) {
		cali_thumbnail_data.set(img.GetWidth(), img.GetHeight());
		for (int i = 0; i < img.GetWidth() * img.GetHeight(); i++) {
			memcpy(&cali_thumbnail_data.pixels[4 * i], (unsigned char*)(img.GetData() + 3 * i), 3);
			if (img.HasAlpha()) {
				cali_thumbnail_data.pixels[4 * i + 3] = *(unsigned char*)(img.GetAlpha() + i);
			}
		}
	}
	else {
		return -1;
	}
	return 0;
}

//load pattern box data from file
int PartPlate::load_pattern_box_data(std::string filename)
{
    try {
        nlohmann::json j;
        boost::nowide::ifstream ifs(filename);
        ifs >> j;

        PlateBBoxData bbox_data;
        bbox_data.from_json(j);
        cali_bboxes_data = bbox_data;
        return 0;
    }
    catch(std::exception &ex) {
        BOOST_LOG_TRIVIAL(trace) << boost::format("catch an exception %1%")%ex.what();
        return -1;
    }
}

void PartPlate::print() const
{
	unsigned int count=0;

	BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << boost::format(": plate index %1%, pointer %2%, print_index %3% print pointer %4%") % m_plate_index % this % m_print_index % m_print;
	BOOST_LOG_TRIVIAL(trace) << boost::format("\t origin {%1%,%2%,%3%}, width %4%,  depth %5%, height %6%") % m_origin.x() % m_origin.y() % m_origin.z() % m_width % m_depth % m_height;
	BOOST_LOG_TRIVIAL(trace) << boost::format("\t m_printable %1%, m_locked %2%, m_ready_for_slice %3%, m_slice_result_valid %4%,  m_tmp_gcode_path %5%, set size %6%")\
		% m_printable % m_locked % m_ready_for_slice % m_slice_result_valid % m_tmp_gcode_path % obj_to_instance_set.size();
	/*for (std::set<std::pair<int, int>>::iterator it = obj_to_instance_set.begin(); it != obj_to_instance_set.end(); ++it) {
		int obj_id = it->first;
		int instance_id = it->second;

		BOOST_LOG_TRIVIAL(trace) << boost::format("\t the %1%th instance, obj_id %2%, instance id %3%") % count++ % obj_id % instance_id;
	}*/
	BOOST_LOG_TRIVIAL(trace) << boost::format("excluded instance set size %1%")%instance_outside_set.size();
	/*for (std::set<std::pair<int, int>>::iterator it = instance_outside_set.begin(); it != instance_outside_set.end(); ++it) {
		int obj_id = it->first;
		int instance_id = it->second;

		BOOST_LOG_TRIVIAL(trace) << boost::format("\t obj_id %1%, instance id %2%") % obj_id % instance_id;
	}*/

	return;
}

/* PartPlate List related functions*/
PartPlateList::PartPlateList(int width, int depth, int height, Plater* platerObj, Model* modelObj, PrinterTechnology tech)
	:m_plate_width(width), m_plate_depth(depth), m_plate_height(height), m_plater(platerObj), m_model(modelObj), printer_technology(tech),
	unprintable_plate(this, Vec3d(0.0 + width * (1. + LOGICAL_PART_PLATE_GAP), 0.0, 0.0), width, depth, height, platerObj, modelObj, false, tech)
{
	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":plate_width %1%, plate_depth %2%, plate_height %3%") % width % depth % height;

	init();
}
PartPlateList::PartPlateList(Plater* platerObj, Model* modelObj, PrinterTechnology tech)
	:m_plate_width(0), m_plate_depth(0), m_plate_height(0), m_plater(platerObj), m_model(modelObj), printer_technology(tech),
	unprintable_plate(this, Vec3d(0.0, 0.0, 0.0), m_plate_width, m_plate_depth, m_plate_height, platerObj, modelObj, false, tech)
{
	init();
}

PartPlateList::~PartPlateList()
{
	clear(true, true);
	release_icon_textures();
}

void PartPlateList::init()
{
	m_intialized = false;
	PartPlate* first_plate = NULL;
	first_plate = new PartPlate(this, Vec3d(0.0, 0.0, 0.0), m_plate_width, m_plate_depth, m_plate_height, m_plater, m_model, true, printer_technology);
	assert(first_plate != NULL);
	m_plate_list.push_back(first_plate);

	m_print_index = 0;
	if (printer_technology == ptFFF)
	{
		Print* print = new Print();
		GCodeResult* gcode = new GCodeResult();
		m_print_list.emplace(m_print_index, print);
		m_gcode_result_list.emplace(m_print_index, gcode);
		first_plate->set_print(print, gcode, m_print_index);
		m_print_index++;
	}
	first_plate->set_index(0);

	m_plate_count = 1;
	m_plate_cols = 1;
	m_current_plate = 0;

	select_plate(0);
	unprintable_plate.set_index(1);

	m_intialized = true;
}

//compute the origin for printable plate with index i
Vec3d PartPlateList::compute_origin(int i, int cols)
{
	Vec3d origin;
	int row, col;

	row = i / cols;
	col = i % cols;

	origin(0) = col * (m_plate_width * (1. + LOGICAL_PART_PLATE_GAP));
	origin(1) = -row * (m_plate_depth * (1. + LOGICAL_PART_PLATE_GAP));
	origin(2) = 0;

	return origin;
}

//compute the origin for printable plate with index i using new width
Vec3d PartPlateList::compute_origin_using_new_size(int i, int new_width, int new_depth)
{
	Vec3d origin;
	int row, col;

	row = i / m_plate_cols;
	col = i % m_plate_cols;

	origin(0) = col * (new_width * (1. + LOGICAL_PART_PLATE_GAP));
	origin(1) = -row * (new_depth * (1. + LOGICAL_PART_PLATE_GAP));
	origin(2) = 0;

	return origin;
}


//compute the origin for printable plate with index i
Vec3d PartPlateList::compute_origin_for_unprintable()
{
	int max_count = m_plate_cols * m_plate_cols;
	if (m_plate_count == max_count)
		return compute_origin(max_count + m_plate_cols - 1, m_plate_cols + 1);
	else
		return compute_origin(m_plate_count, m_plate_cols);
}

//compute shape position
Vec2d PartPlateList::compute_shape_position(int index, int cols)
{
	Vec2d pos;
	int row, col;

	row = index / cols;
	col = index % cols;

	pos(0) = col * plate_stride_x();
	pos(1) = -row * plate_stride_y();

	return pos;
}

//generate icon textures
void PartPlateList::generate_icon_textures()
{
	// use higher resolution images if graphic card and opengl version allow
	GLint max_tex_size = OpenGLManager::get_gl_info().get_max_tex_size(), icon_size = max_tex_size / 8;
	std::string path = resources_dir() + "/images/";
	std::string file_name;

	if (icon_size > 256)
		icon_size = 256;
	//if (m_del_texture.get_id() == 0)
	{
		file_name = path + (m_is_dark ? "plate_close_dark.svg" : "plate_close.svg");
		if (!m_del_texture.load_from_svg_file(file_name, true, false, false, icon_size)) {
			BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(":load file %1% failed") % file_name;
		}
	}

	//if (m_del_hovered_texture.get_id() == 0)
	{
		file_name = path + (m_is_dark ? "plate_close_hover_dark.svg" : "plate_close_hover.svg");
		if (!m_del_hovered_texture.load_from_svg_file(file_name, true, false, false, icon_size)) {
			BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(":load file %1% failed") % file_name;
		}
	}

	//if (m_arrange_texture.get_id() == 0)
	{
		file_name = path + (m_is_dark ? "plate_arrange_dark.svg" : "plate_arrange.svg");
		if (!m_arrange_texture.load_from_svg_file(file_name, true, false, false, icon_size)) {
			BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(":load file %1% failed") % file_name;
		}
	}

	//if (m_arrange_hovered_texture.get_id() == 0)
	{
		file_name = path + (m_is_dark ? "plate_arrange_hover_dark.svg" : "plate_arrange_hover.svg");
		if (!m_arrange_hovered_texture.load_from_svg_file(file_name, true, false, false, icon_size)) {
			BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(":load file %1% failed") % file_name;
		}
	}

	//if (m_orient_texture.get_id() == 0)
	{
		file_name = path + (m_is_dark ? "plate_orient_dark.svg" : "plate_orient.svg");
		if (!m_orient_texture.load_from_svg_file(file_name, true, false, false, icon_size)) {
			BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(":load file %1% failed") % file_name;
		}
	}

	//if (m_orient_hovered_texture.get_id() == 0)
	{
		file_name = path + (m_is_dark ? "plate_orient_hover_dark.svg" : "plate_orient_hover.svg");
		if (!m_orient_hovered_texture.load_from_svg_file(file_name, true, false, false, icon_size)) {
			BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(":load file %1% failed") % file_name;
		}
	}

	//if (m_locked_texture.get_id() == 0)
	{
		file_name = path + (m_is_dark ? "plate_locked_dark.svg" : "plate_locked.svg");
		if (!m_locked_texture.load_from_svg_file(file_name, true, false, false, icon_size)) {
			BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(":load file %1% failed") % file_name;
		}
	}

	//if (m_locked_hovered_texture.get_id() == 0)
	{
		file_name = path + (m_is_dark ? "plate_locked_hover_dark.svg" : "plate_locked_hover.svg");
		if (!m_locked_hovered_texture.load_from_svg_file(file_name, true, false, false, icon_size)) {
			BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(":load file %1% failed") % file_name;
		}
	}

	//if (m_lockopen_texture.get_id() == 0)
	{
		file_name = path + (m_is_dark ? "plate_unlocked_dark.svg" : "plate_unlocked.svg");
		if (!m_lockopen_texture.load_from_svg_file(file_name, true, false, false, icon_size)) {
			BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(":load file %1% failed") % file_name;
		}
	}

	//if (m_lockopen_hovered_texture.get_id() == 0)
	{
		file_name = path + (m_is_dark ? "plate_unlocked_hover_dark.svg" : "plate_unlocked_hover.svg");
		if (!m_lockopen_hovered_texture.load_from_svg_file(file_name, true, false, false, icon_size)) {
			BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(":load file %1% failed") % file_name;
		}
	}

	//if (m_bedtype_texture.get_id() == 0)
	{
		file_name = path + (m_is_dark ? "plate_settings_dark.svg" : "plate_settings.svg");
		if (!m_plate_settings_texture.load_from_svg_file(file_name, true, false, false, icon_size)) {
			BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(":load file %1% failed") % file_name;
		}
	}

	//if (m_bedtype_changed_texture.get_id() == 0)
	{
		file_name = path + (m_is_dark ? "plate_settings_changed_dark.svg" : "plate_settings_changed.svg");
		if (!m_plate_settings_changed_texture.load_from_svg_file(file_name, true, false, false, icon_size)) {
			BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(":load file %1% failed") % file_name;
		}
	}

	//if (m_bedtype_hovered_texture.get_id() == 0)
	{
		file_name = path + (m_is_dark ? "plate_settings_hover_dark.svg" : "plate_settings_hover.svg");
		if (!m_plate_settings_hovered_texture.load_from_svg_file(file_name, true, false, false, icon_size)) {
			BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(":load file %1% failed") % file_name;
		}
	}

	//if (m_bedtype_changed_hovered_texture.get_id() == 0)
	{
		file_name = path + (m_is_dark ? "plate_settings_changed_hover_dark.svg" : "plate_settings_changed_hover.svg");
		if (!m_plate_settings_changed_hovered_texture.load_from_svg_file(file_name, true, false, false, icon_size)) {
			BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(":load file %1% failed") % file_name;
		}
	}


	std::string text_str = "01";
	wxFont* font = find_font(text_str,32);

	for (int i = 0; i < MAX_PLATE_COUNT; i++) {
		if (m_idx_textures[i].get_id() == 0) {
			//file_name = path + (boost::format("plate_%1%.svg") % (i + 1)).str();
			if ( i < 9 )
				file_name = std::string("0") + std::to_string(i+1);
			else
				file_name = std::to_string(i+1);

			wxColour foreground(0x0, 0x96, 0x88, 0xff);
			if (!m_idx_textures[i].generate_from_text_string(file_name, *font, *wxBLACK, foreground)) {
				BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(":load file %1% failed") % file_name;
			}
		}
	}
}

void PartPlateList::release_icon_textures()
{
	m_logo_texture.reset();
	m_del_texture.reset();
	m_del_hovered_texture.reset();
	m_arrange_texture.reset();
	m_arrange_hovered_texture.reset();
	m_orient_texture.reset();
	m_orient_hovered_texture.reset();
	m_locked_texture.reset();
	m_locked_hovered_texture.reset();
	m_lockopen_texture.reset();
	m_lockopen_hovered_texture.reset();
	m_plate_settings_texture.reset();
	m_plate_settings_texture.reset();
	m_plate_settings_texture.reset();
	m_plate_settings_hovered_texture.reset();

	for (int i = 0;i < MAX_PLATE_COUNT; i++) {
		m_idx_textures[i].reset();
	}
	//reset
	PartPlateList::is_load_bedtype_textures = false;
	for (int i = 0; i < btCount; i++) {
		for (auto& part: bed_texture_info[i].parts) {
			if (part.texture) {
				part.texture->reset();
				delete part.texture;
			}
			if (part.vbo_id != 0) {
				glsafe(::glDeleteBuffers(1, &part.vbo_id));
				part.vbo_id = 0;
			}
			if (part.buffer) {
				delete part.buffer;
			}
		}
	}
}

//this may be happened after machine changed
void PartPlateList::reset_size(int width, int depth, int height, bool reload_objects, bool update_shapes)
{
	Vec3d origin1, origin2;

	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":before size: plate_width %1%, plate_depth %2%, plate_height %3%") % m_plate_width % m_plate_depth % m_plate_height;
	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":after size: plate_width %1%, plate_depth %2%, plate_height %3%") % width % depth % height;
	if ((m_plate_width != width) || (m_plate_depth != depth) || (m_plate_height != height))
	{
		m_plate_width = width;
		m_plate_depth = depth;
		m_plate_height = height;
		update_all_plates_pos_and_size(false, false);
		if (update_shapes) {
			set_shapes(m_shape, m_exclude_areas, m_logo_texture_filename, m_height_to_lid, m_height_to_rod);
		}
		if (reload_objects)
			reload_all_objects();
		else
			clear(false, false, false, -1);
	}

	return;
}

//clear all the instances in the plate, but keep the plates
void PartPlateList::clear(bool delete_plates, bool release_print_list, bool except_locked, int plate_index)
{
	for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		PartPlate* plate = m_plate_list[i];
		assert(plate != NULL);

		if (except_locked && plate->is_locked())
			plate->clear(false);
		else if ((plate_index != -1) && (plate_index != i))
			plate->clear(false);
		else
			plate->clear();
		if (delete_plates)
			delete plate;
	}

	if (delete_plates)
	{
		//also delete print related to the plate
		m_plate_list.clear();
		m_current_plate = 0;
	}

	if (release_print_list)
	{
		for (std::map<int, PrintBase*>::iterator it = m_print_list.begin(); it != m_print_list.end(); ++it)
		{
			PrintBase* print = it->second;
			assert(print != NULL);

			delete print;
		}
		m_print_list.clear();
		for (std::map<int, GCodeResult*>::iterator it = m_gcode_result_list.begin(); it != m_gcode_result_list.end(); ++it)
		{
			GCodeResult* gcode = it->second;
			assert(gcode != NULL);

			delete gcode;
		}
		m_gcode_result_list.clear();
	}

	unprintable_plate.clear();
}

//clear all the instances in the plate, and delete the plates, only keep the first default plate
void PartPlateList::reset(bool do_init)
{
	clear(true, false);

	//m_plate_list.clear();

	if (do_init)
		init();

	return;
}

//reset partplate to init states
void PartPlateList::reinit()
{
	clear(true, true);

	init();

	//reset plate 0's position
	Vec2d pos = compute_shape_position(0, m_plate_cols);
	m_plate_list[0]->set_shape(m_shape, m_exclude_areas, pos, m_height_to_lid, m_height_to_rod);
	//reset unprintable plate's position
	Vec3d origin2 = compute_origin_for_unprintable();
	unprintable_plate.set_pos_and_size(origin2, m_plate_width, m_plate_depth, m_plate_height, false);
	//re-calc the bounding boxes
	calc_bounding_boxes();

	return;
}

/*basic plate operations*/
//create an empty plate, and return its index
//these model instances which are not in any plates should not be affected also
int PartPlateList::create_plate(bool adjust_position)
{
	PartPlate* plate = NULL;
	Vec3d origin;
	int new_index;

	new_index = m_plate_list.size();
	if (new_index >= MAX_PLATES_COUNT)
		return -1;
	int cols = compute_colum_count(new_index + 1);
	int old_cols = compute_colum_count(new_index);

	origin = compute_origin(new_index, cols);
	plate = new PartPlate(this, origin, m_plate_width, m_plate_depth, m_plate_height, m_plater, m_model, true, printer_technology);
	assert(plate != NULL);

	if (printer_technology == ptFFF)
	{
		Print* print = new Print();
		GCodeResult* gcode = new GCodeResult();
		m_print_list.emplace(m_print_index, print);
		m_gcode_result_list.emplace(m_print_index, gcode);
		plate->set_print(print, gcode, m_print_index);
		m_print_index++;
	}

	plate->set_index(new_index);
	Vec2d pos = compute_shape_position(new_index, cols);
	plate->set_shape(m_shape, m_exclude_areas, pos, m_height_to_lid, m_height_to_rod);
	m_plate_list.emplace_back(plate);
	update_plate_cols();
	if (old_cols != cols)
	{
		BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(":old_cols %1% -> new_cols %2%") % old_cols % cols;
		//update the origin of each plate
		update_all_plates_pos_and_size(adjust_position, false);
		set_shapes(m_shape, m_exclude_areas, m_logo_texture_filename, m_height_to_lid, m_height_to_rod);

		if (m_plater) {
			Vec2d pos = compute_shape_position(m_current_plate, cols);
			m_plater->set_bed_position(pos);
		}
	}
	else
	{
		BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": the same cols %1%") % old_cols;
		Vec3d origin2 = compute_origin_for_unprintable();
		unprintable_plate.set_pos_and_size(origin2, m_plate_width, m_plate_depth, m_plate_height, false);

		//update bounding_boxes
		calc_bounding_boxes();
	}

	// update wipe tower config
	if (m_plater) {
		// In GUI mode
		DynamicConfig& proj_cfg = wxGetApp().preset_bundle->project_config;
		ConfigOptionFloats* wipe_tower_x = proj_cfg.opt<ConfigOptionFloats>("wipe_tower_x");
		ConfigOptionFloats* wipe_tower_y = proj_cfg.opt<ConfigOptionFloats>("wipe_tower_y");
		wipe_tower_x->values.resize(m_plate_list.size(), wipe_tower_x->values.front());
		wipe_tower_y->values.resize(m_plate_list.size(), wipe_tower_y->values.front());
	}

	unprintable_plate.set_index(new_index+1);

	//reload all objects here
	if (adjust_position)
		construct_objects_list_for_new_plate(new_index);

	if (m_plater) {
		// In GUI mode
		wxGetApp().obj_list()->on_plate_added(plate);
	}

	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(":created a new plate %1%") % new_index;
	return new_index;
}

//destroy print's objects and results
int PartPlateList::destroy_print(int print_index)
{
	int result = 0;

	if (print_index >= 0)
	{
		std::map<int, PrintBase*>::iterator it = m_print_list.find(print_index);
		if (it != m_print_list.end())
		{
			BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(":delete Print %1% for print_index %2%") % it->second % print_index;
			delete it->second;
			m_print_list.erase(it);
		}
		else
		{
			BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(":can not find Print for print_index %1%") % print_index;
			result = -1;
		}
		std::map<int, GCodeResult*>::iterator it2 = m_gcode_result_list.find(print_index);
		if (it2 != m_gcode_result_list.end())
		{
			BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(":delete GCodeResult %1% for print_index %2%") % it2->second % print_index;
			delete it2->second;
			m_gcode_result_list.erase(it2);
		}
		else
		{
			BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(":can not find GCodeResult for print_index %1%") % print_index;
			result = -1;
		}
	}

	return result;
}

//delete a plate by index
//keep its instance at origin position and add them into next plate if have
//update the plate index and position after it
int PartPlateList::delete_plate(int index)
{
	int ret = 0;
	PartPlate* plate = NULL;

	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(":delete plate %1%, count %2%") % index % m_plate_list.size();
	if (index >= m_plate_list.size())
	{
		BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(":can not find plate");
		return -1;
	}
	if (m_plate_list.size() <= 1)
	{
		BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(":only one plate left, can not delete");
		return -1;
	}

	plate = m_plate_list[index];
	if (index != plate->get_index())
	{
		BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(":plate %1%, has an invalid index %2%") % index % plate->get_index();
		return -1;
	}

	if (m_plater) {
		// In GUI mode
		// BBS: add wipe tower logic
		DynamicConfig& proj_cfg = wxGetApp().preset_bundle->project_config;
		ConfigOptionFloats* wipe_tower_x = proj_cfg.opt<ConfigOptionFloats>("wipe_tower_x");
		ConfigOptionFloats* wipe_tower_y = proj_cfg.opt<ConfigOptionFloats>("wipe_tower_y");
		// wipe_tower_x and wip_tower_y may be less than plate count in the following case:
		// 1. wipe_tower is enabled after creating new plates
		// 2. wipe tower is not enabled
		if (index < wipe_tower_x->values.size())
			wipe_tower_x->values.erase(wipe_tower_x->values.begin() + index);
		if (index < wipe_tower_y->values.size())
			wipe_tower_y->values.erase(wipe_tower_y->values.begin() + index);
	}

	int cols = compute_colum_count(m_plate_list.size() - 1);
	int old_cols = compute_colum_count(m_plate_list.size());

	m_plate_list.erase(m_plate_list.begin() + index);
	update_plate_cols();
	//update this plate
	//move this plate's instance to the end
	Vec3d current_origin;
	current_origin = compute_origin_for_unprintable();
	plate->set_pos_and_size(current_origin, m_plate_width, m_plate_depth, m_plate_height, true);

	//update the plates after it
	for (unsigned int i = index; i < (unsigned int)m_plate_list.size(); ++i)
	{
		PartPlate* plate = m_plate_list[i];
		assert(plate != NULL);

		plate->set_index(i);
		Vec3d origin = compute_origin(i, m_plate_cols);
		plate->set_pos_and_size(origin, m_plate_width, m_plate_depth, m_plate_height, true);

		//update render shapes
		Vec2d pos = compute_shape_position(i, m_plate_cols);
		plate->set_shape(m_shape, m_exclude_areas, pos, m_height_to_lid, m_height_to_rod);
	}

	//update current_plate if delete current
	if (m_current_plate == index && index == 0) {
		select_plate(0);
	}
	else if (m_current_plate >= index) {
		select_plate(m_current_plate - 1);
	}
	else {
		//delete the plate behind current, just need to update the position of Bed3D
		Vec2d pos = compute_shape_position(m_current_plate, m_plate_cols);
		if (m_plater)
			m_plater->set_bed_position(pos);
	}

	unprintable_plate.set_index(m_plate_list.size());

	if (old_cols != cols)
	{
		//update the origin of each plate
		update_all_plates_pos_and_size();
		set_shapes(m_shape, m_exclude_areas, m_logo_texture_filename, m_height_to_lid, m_height_to_rod);
	}
	else
	{
		//update the position of the unprintable plate
		Vec3d origin2 = compute_origin_for_unprintable();
		unprintable_plate.set_pos_and_size(origin2, m_plate_width, m_plate_depth, m_plate_height, true);

		//update bounding_boxes
		calc_bounding_boxes();
	}

	plate->move_instances_to(*(m_plate_list[m_plate_list.size()-1]), unprintable_plate);
	//destroy the print object
	int print_index;
	plate->get_print(nullptr, nullptr, &print_index);
	destroy_print(print_index);

	delete plate;

    // FIX: context of BackgroundSliceProcess and gcode preview need to be updated before ObjectList::reload_all_plates().
#if 0
	if (m_plater != nullptr) {
		// In GUI mode
		wxGetApp().obj_list()->reload_all_plates();
	}
#endif
	return ret;
}

void PartPlateList::delete_selected_plate()
{
	delete_plate(m_current_plate);
}

//get a plate pointer by index
PartPlate* PartPlateList::get_plate(int index)
{
	PartPlate* plate = NULL;

	if (index >= m_plate_list.size())
	{
		BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(":can not find index %1%, size %2%") % index % m_plate_list.size();
		return NULL;
	}

	plate = m_plate_list[index];
	assert(plate != NULL);

	return plate;
}

PartPlate* PartPlateList::get_selected_plate()
{
	if (m_current_plate < 0 || m_current_plate >= m_plate_list.size()) {
		BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(":can not find m_current_plate  %1%, size %2%") % m_current_plate % m_plate_list.size();
		return NULL;
	}
	return m_plate_list[m_current_plate];
}

std::vector<PartPlate*> PartPlateList::get_nonempty_plate_list()
{
	std::vector<PartPlate*> nonempty_plate_list;
	for (auto plate : m_plate_list){
		if (plate->get_extruders().size() != 0) {
			nonempty_plate_list.push_back(plate);
		}
	}
	return nonempty_plate_list;
}

std::vector<const GCodeProcessorResult*> PartPlateList::get_nonempty_plates_slice_results() {
	std::vector<const GCodeProcessorResult*> nonempty_plates_slice_result;
	for (auto plate : get_nonempty_plate_list()) {
		nonempty_plates_slice_result.push_back(plate->get_slice_result());
	}
	return nonempty_plates_slice_result;
}

//select plate
int PartPlateList::select_plate(int index)
{
	const std::lock_guard<std::mutex> local_lock(m_plates_mutex);
	if (m_plate_list.empty() || index >= m_plate_list.size()) {
		return -1;
	}

	// BBS: erase unnecessary snapshot
	if (get_curr_plate_index() != index && m_intialized) {
		if (m_plater)
			m_plater->take_snapshot("select partplate!");
	}

	std::vector<PartPlate *>::iterator it = m_plate_list.begin();
	for (it = m_plate_list.begin(); it != m_plate_list.end(); it++) {
		(*it)->set_unselected();
	}

	m_current_plate = index;
	m_plate_list[m_current_plate]->set_selected();

	//BBS
	if(m_model)
		m_model->curr_plate_index = index;

	//BBS update bed origin
	if (m_intialized && m_plater) {
		Vec2d pos = compute_shape_position(index, m_plate_cols);
        m_plater->set_bed_position(pos);
		//wxQueueEvent(m_plater, new SimpleEvent(EVT_GLCANVAS_PLATE_SELECT));
	}

	return 0;
}

void PartPlateList::set_hover_id(int id)
{
	int index = id / PartPlate::GRABBER_COUNT;
	int sub_hover_id = id % PartPlate::GRABBER_COUNT;
	m_plate_list[index]->set_hover_id(sub_hover_id);
}

void PartPlateList::reset_hover_id()
{
	const std::lock_guard<std::mutex> local_lock(m_plates_mutex);
	std::vector<PartPlate*>::iterator it = m_plate_list.begin();
	for (it = m_plate_list.begin(); it != m_plate_list.end(); it++) {
		(*it)->set_hover_id(-1);
	}
}

bool PartPlateList::intersects(const BoundingBoxf3& bb)
{
	bool result = false;
	std::vector<PartPlate*>::iterator it = m_plate_list.begin();
	for (it = m_plate_list.begin(); it != m_plate_list.end(); it++) {
		if ((*it)->intersects(bb)) {
			result = true;
		}
	}
	return result;
}

bool PartPlateList::contains(const BoundingBoxf3& bb)
{
	bool result = false;
	std::vector<PartPlate*>::iterator it = m_plate_list.begin();
	for (it = m_plate_list.begin(); it != m_plate_list.end(); it++) {
		if ((*it)->contains(bb)) {
			result = true;
		}
	}
	return result;
}

double PartPlateList::plate_stride_x()
{
	//const auto plate_shape = Slic3r::Polygon::new_scale(m_shape);
	//double plate_width = plate_shape.bounding_box().size().x();
	//return unscaled<double>((1. + LOGICAL_PART_PLATE_GAP) * plate_width);
	return m_plate_width * (1. + LOGICAL_PART_PLATE_GAP);
}

double PartPlateList::plate_stride_y()
{
	//const auto plate_shape = Slic3r::Polygon::new_scale(m_shape);
	//double plate_depth = plate_shape.bounding_box().size().y();
	//return unscaled<double>((1. + LOGICAL_PART_PLATE_GAP) * plate_depth);
	return m_plate_depth * (1. + LOGICAL_PART_PLATE_GAP);
}

//get the plate counts, not including the invalid plate
int PartPlateList::get_plate_count() const
{
	int ret = 0;

	ret = m_plate_list.size();

	return ret;
}

//update the plate cols due to plate count change
void PartPlateList::update_plate_cols()
{
	m_plate_count = m_plate_list.size();

	m_plate_cols = compute_colum_count(m_plate_count);
	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":m_plate_count %1%, m_plate_cols change to %2%") % m_plate_count % m_plate_cols;
	return;
}

void PartPlateList::update_all_plates_pos_and_size(bool adjust_position, bool with_unprintable_move)
{
	Vec3d origin1, origin2;
	for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		PartPlate* plate = m_plate_list[i];
		assert(plate != NULL);

		//compute origin1 for PartPlate
		origin1 = compute_origin(i, m_plate_cols);
		plate->set_pos_and_size(origin1, m_plate_width, m_plate_depth, m_plate_height, adjust_position);
	}

	origin2 = compute_origin_for_unprintable();
	unprintable_plate.set_pos_and_size(origin2, m_plate_width, m_plate_depth, m_plate_height, with_unprintable_move);
}

//move the plate to position index
int PartPlateList::move_plate_to_index(int old_index, int new_index)
{
	int ret = 0, delta;
	Vec3d origin;


	if (old_index == new_index)
	{
		BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(":should not happen, the same index %1%") % old_index;
		return -1;
	}

	if (old_index < new_index)
	{
		delta = 1;
	}
	else
	{
		delta = -1;
	}

	PartPlate* plate = m_plate_list[old_index];
	//update the plates between old_index and new_index
	for (unsigned int i = (unsigned int)old_index; i != (unsigned int)new_index; i = i + delta)
	{
		m_plate_list[i] = m_plate_list[i + delta];
		m_plate_list[i]->set_index(i);

		origin = compute_origin(i, m_plate_cols);
		m_plate_list[i]->set_pos_and_size(origin, m_plate_width, m_plate_depth, m_plate_height, true);
	}
	origin = compute_origin(new_index, m_plate_cols);
	m_plate_list[new_index] = plate;
	plate->set_index(new_index);
	plate->set_pos_and_size(origin, m_plate_width, m_plate_depth, m_plate_height, true);

	//update the new plate index
	m_current_plate = new_index;

	return ret;
}

//lock plate
int PartPlateList::lock_plate(int index, bool state)
{
	int ret = 0;
	PartPlate* plate = NULL;

	plate = get_plate(index);
	if (!plate)
	{
		BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(":can not get plate for index %1%, size %2%") % index % m_plate_list.size();
		return -1;
	}
	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":lock plate %1%, to state %2%") % index % state;

	plate->lock(state);

	return ret;
}

//find plate by print index, return -1 if not found
int PartPlateList::find_plate_by_print_index(int print_index)
{
	int plate_index = -1;

	for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		PartPlate* plate = m_plate_list[i];

		if (plate->m_print_index == print_index)
		{
			plate_index = i;
			break;
		}
	}

	return plate_index;
}

/*instance related operations*/
//find instance in which plate, return -1 when not found
//this function only judges whether it is intersect with plate
int PartPlateList::find_instance(int obj_id, int instance_id)
{
	int ret = -1;

	//update the plates after it
	for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		PartPlate* plate = m_plate_list[i];
		assert(plate != NULL);

		if (plate->contain_instance(obj_id, instance_id))
			return i;
	}

	//return -1 for not found
	return ret;
}

/*instance related operations*/
//find instance in which plate, return -1 when not found
//this function only judges whether it is intersect with plate
int PartPlateList::find_instance(BoundingBoxf3& bounding_box)
{
	int ret = -1;

	//update the plates after it
	for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		PartPlate* plate = m_plate_list[i];
		assert(plate != NULL);

		if (plate->intersects(bounding_box))
			return i;
	}

	//return -1 for not found
	return ret;
}

//this function not only judges whether it is intersect with plate, but also judges whether it is fully included in plate
//returns -1 when can not find any plate
int PartPlateList::find_instance_belongs(int obj_id, int instance_id)
{
	int ret = -1;

	//update the plates after it
	for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		PartPlate* plate = m_plate_list[i];
		assert(plate != NULL);

		if (plate->contain_instance_totally(obj_id, instance_id))
			return i;
	}

	//return -1 for not found
	return ret;
}

//notify instance's update, need to refresh the instance in plates
//newly added or modified
int PartPlateList::notify_instance_update(int obj_id, int instance_id)
{
	int ret = 0, index;
	PartPlate* plate = NULL;
	ModelObject* object = NULL;

	if ((obj_id >= 0) && (obj_id < m_model->objects.size()))
	{
		object = m_model->objects[obj_id];
	}
	else if (obj_id >= 1000 && obj_id < 1000 + m_plate_count) {
		//wipe tower updates
		PartPlate* plate = m_plate_list[obj_id - 1000];
		plate->update_slice_result_valid_state( false );
		plate->thumbnail_data.reset();

		return 0;
	}
    else
		return -1;

	BoundingBoxf3 boundingbox = object->instance_convex_hull_bounding_box(instance_id);

	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": obj_id %1%, instance_id %2%") % obj_id % instance_id;
	index = find_instance(obj_id, instance_id);
	if (index != -1)
	{
		//found it added before
		BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": found it in previous plate %1%") % index;
		plate = m_plate_list[index];
		if (!plate->intersect_instance(obj_id, instance_id, &boundingbox))
		{
			//not include anymore, remove it from original plate
			BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": not in plate %1% anymore, remove it") % index;
			plate->remove_instance(obj_id, instance_id);
		}
		else
		{
			BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": still in original plate %1%, no need to be updated") % index;
			plate->update_instance_exclude_status(obj_id, instance_id, &boundingbox);
			plate->update_states();
			plate->update_slice_result_valid_state();
			plate->thumbnail_data.reset();
			return 0;
		}
		plate->update_slice_result_valid_state();
		plate->thumbnail_data.reset();
	}
	else if (unprintable_plate.contain_instance(obj_id, instance_id))
	{
		//found it in the unprintable plate
		BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": found it in unprintable plate");
		if (!unprintable_plate.intersect_instance(obj_id, instance_id, &boundingbox))
		{
			//not include anymore, remove it from original plate
			BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": not in unprintable plate anymore, remove it");
			unprintable_plate.remove_instance(obj_id, instance_id);
		}
		else
		{
			BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": still in unprintable plate, no need to be updated");
			return 0;
		}
	}

	//try to find a new plate
	for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		PartPlate* plate = m_plate_list[i];
		assert(plate != NULL);

		if (plate->intersect_instance(obj_id, instance_id, &boundingbox))
		{
			//found a new plate, add it to plate
			plate->add_instance(obj_id, instance_id, false, &boundingbox);
			plate->update_slice_result_valid_state();
			plate->thumbnail_data.reset();
			BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": add it to new plate %1%") % i;
			return 0;
		}
	}

	if (unprintable_plate.intersect_instance(obj_id, instance_id, &boundingbox))
	{
		//found in unprintable plate, add it to plate
		unprintable_plate.add_instance(obj_id, instance_id, false, &boundingbox);
		BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": add it to unprintable plate");
		return 0;
	}

	return 0;
}

//notify instance is removed
int PartPlateList::notify_instance_removed(int obj_id, int instance_id)
{
	int ret = 0, index, instance_to_delete = instance_id;
	PartPlate* plate = NULL;

	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": obj_id %1%, instance_id %2%") % obj_id % instance_id;
	if (instance_id == -1) {
		instance_to_delete = 0;
	}
	index = find_instance(obj_id, instance_to_delete);
	if (index != -1)
	{
		//found it added before
		BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": found it in plate %1%, remove it") % index;
		plate = m_plate_list[index];
		plate->remove_instance(obj_id, instance_to_delete);
		plate->update_slice_result_valid_state();
		plate->thumbnail_data.reset();
	}

	if (unprintable_plate.contain_instance(obj_id, instance_to_delete))
	{
		//found in unprintable plate, add it to plate
		unprintable_plate.remove_instance(obj_id, instance_to_delete);
		BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": found it in unprintable plate, remove it");
	}

	if (instance_id == -1) {
		//update all the obj_ids which is bigger
		for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
		{
			PartPlate* plate = m_plate_list[i];
			assert(plate != NULL);

			plate->update_object_index(obj_id, m_model->objects.size());
		}
		unprintable_plate.update_object_index(obj_id, m_model->objects.size());
	}

	return 0;
}

//add instance to special plate, need to remove from the original plate
//called from the right-mouse menu when a instance selected
int PartPlateList::add_to_plate(int obj_id, int instance_id, int plate_id)
{
	int ret = 0, index;
	PartPlate* plate = NULL;

	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": plate_id %1%, found obj_id %2%, instance_id %3%") % plate_id % obj_id % instance_id;
	index = find_instance(obj_id, instance_id);
	if (index != -1)
	{
		//found it added before
		BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": found it in previous plate %1%") % index;
		if (index != plate_id)
		{
			//remove it from original plate first
			plate = m_plate_list[index];
			plate->remove_instance(obj_id, instance_id);
		}
		else
		{
			BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": already in this plate, no need to be added");
			return 0;
		}
	}
	else
	{
		BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": not added to plate before, add it to center");
	}
	plate = get_plate(plate_id);
	if (!plate)
	{
		BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(":can not get plate for index %1%, size %2%") % index % m_plate_list.size();
		return -1;
	}
	ret = plate->add_instance(obj_id, instance_id, true);

	return ret;
}

//reload all objects
int PartPlateList::reload_all_objects(bool except_locked, int plate_index)
{
	int ret = 0;
	unsigned int i, j, k;

	clear(false, false, except_locked, plate_index);

	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": m_model->objects.size() is %1%") % m_model->objects.size();
	//try to find a new plate
	for (i = 0; i < (unsigned int)m_model->objects.size(); ++i)
	{
		ModelObject* object = m_model->objects[i];
		for (j = 0; j < (unsigned int)object->instances.size(); ++j)
		{
			ModelInstance* instance = object->instances[j];
			BoundingBoxf3 boundingbox = object->instance_convex_hull_bounding_box(j);
			for (k = 0; k < (unsigned int)m_plate_list.size(); ++k)
			{
				PartPlate* plate = m_plate_list[k];
				assert(plate != NULL);

				if (plate->intersect_instance(i, j, &boundingbox))
				{
					//found a new plate, add it to plate
					plate->add_instance(i, j, false, &boundingbox);
					BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": found plate_id %1%, for obj_id %2%, instance_id %3%") % k % i % j;

					//need to judge whether this instance has an outer part
					/*if (plate->check_outside(i, j))
					{
						plate->m_ready_for_slice = false;
					}*/
					break;
				}
			}

			if ((k == m_plate_list.size()) && (unprintable_plate.intersect_instance(i, j, &boundingbox)))
			{
				//found in unprintable plate, add it to plate
				unprintable_plate.add_instance(i, j, false, &boundingbox);
				BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": found in unprintable plate, obj_id %1%, instance_id %2%") % i % j;
			}
		}

	}

	return ret;
}

//reload objects for newly created plate
int PartPlateList::construct_objects_list_for_new_plate(int plate_index)
{
	int ret = 0;
	unsigned int i, j, k;
	PartPlate* new_plate = m_plate_list[plate_index];
	bool already_included;

	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": m_model->objects.size() is %1%") % m_model->objects.size();
	unprintable_plate.clear();
	//try to find a new plate
	for (i = 0; i < (unsigned int)m_model->objects.size(); ++i)
	{
		ModelObject* object = m_model->objects[i];
		for (j = 0; j < (unsigned int)object->instances.size(); ++j)
		{
			ModelInstance* instance = object->instances[j];
			already_included = false;

			for (k = 0; k < (unsigned int)plate_index; ++k)
			{
				PartPlate* plate = m_plate_list[k];
				if (plate->contain_instance(i, j))
				{
					already_included = true;
					break;
				}
			}

			if (already_included)
				continue;

			BoundingBoxf3 boundingbox = object->instance_convex_hull_bounding_box(j);
			if (new_plate->intersect_instance(i, j, &boundingbox))
			{
				//found a new plate, add it to plate
				ret |= new_plate->add_instance(i, j, false, &boundingbox);
				BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": added to plate_id %1%, for obj_id %2%, instance_id %3%") % plate_index % i % j;

				continue;
			}

			if ( (unprintable_plate.intersect_instance(i, j, &boundingbox)))
			{
				//found in unprintable plate, add it to plate
				unprintable_plate.add_instance(i, j, false, &boundingbox);
				BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": found in unprintable plate, obj_id %1%, instance_id %2%") % i % j;
			}
		}
	}

	return ret;
}


//compute the plate index
int PartPlateList::compute_plate_index(arrangement::ArrangePolygon& arrange_polygon)
{
	int row, col;

	float col_value = (unscale<double>(arrange_polygon.translation(X))) / plate_stride_x();
	float row_value = (plate_stride_y() - unscale<double>(arrange_polygon.translation(Y))) / plate_stride_y();

	row = round(row_value);
	col = round(col_value);

	return row * m_plate_cols + col;
}

//preprocess a arrangement::ArrangePolygon, return true if it is in a locked plate
bool PartPlateList::preprocess_arrange_polygon(int obj_index, int instance_index, arrangement::ArrangePolygon& arrange_polygon, bool selected)
{
	bool locked = false;
	int lockplate_cnt = 0;

	for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		if (m_plate_list[i]->contain_instance(obj_index, instance_index))
		{
			if (m_plate_list[i]->is_locked())
			{
				locked = true;
				arrange_polygon.bed_idx = i;
				arrange_polygon.row = i / m_plate_cols;
				arrange_polygon.col = i % m_plate_cols;
				arrange_polygon.translation(X) -= scaled<double>(plate_stride_x() * arrange_polygon.col);
				arrange_polygon.translation(Y) += scaled<double>(plate_stride_y() * arrange_polygon.row);
			}
			else
			{
				if (!selected)
				{
					//will be treated as fixeditem later
					arrange_polygon.bed_idx = i - lockplate_cnt;
					arrange_polygon.row = i / m_plate_cols;
					arrange_polygon.col = i % m_plate_cols;
					arrange_polygon.translation(X) -= scaled<double>(plate_stride_x() * arrange_polygon.col);
					arrange_polygon.translation(Y) += scaled<double>(plate_stride_y() * arrange_polygon.row);
				}
			}
			BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": obj_id %1% instance_id %2% already in plate %3%, locked %4%, row %5%, col %6%\n") % obj_index % instance_index % i % locked % arrange_polygon.row % arrange_polygon.col;
			return locked;
		}
		if (m_plate_list[i]->is_locked())
			lockplate_cnt++;
	}
	//not be contained by any plates
	if (!selected)
		arrange_polygon.bed_idx = PartPlateList::MAX_PLATES_COUNT;

	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": not in any plates, bed_idx %1%, translation(x) %2%, (y) %3%") % arrange_polygon.bed_idx % unscale<double>(arrange_polygon.translation(X)) % unscale<double>(arrange_polygon.translation(Y));

	return locked;
}

//preprocess a arrangement::ArrangePolygon, return true if it is not in current plate
bool PartPlateList::preprocess_arrange_polygon_other_locked(int obj_index, int instance_index, arrangement::ArrangePolygon& arrange_polygon, bool selected)
{
	bool locked = false;

	if (selected)
	{
		//arrange_polygon.translation(X) -= scaled<double>(plate_stride_x() * m_current_plate);
	}
	else
	{
		locked = true;
		for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
		{
			if (m_plate_list[i]->contain_instance(obj_index, instance_index))
			{
				arrange_polygon.bed_idx = i;
				arrange_polygon.row = i / m_plate_cols;
				arrange_polygon.col = i % m_plate_cols;
				arrange_polygon.translation(X) -= scaled<double>(plate_stride_x() * arrange_polygon.col);
				arrange_polygon.translation(Y) += scaled<double>(plate_stride_y() * arrange_polygon.row);
				BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": obj_id %1% instance_id %2% in plate %3%, locked %4%, row %5%, col %6%\n") % obj_index % instance_index % i % locked % arrange_polygon.row % arrange_polygon.col;
				return locked;
			}
		}
		arrange_polygon.bed_idx = PartPlateList::MAX_PLATES_COUNT;
	}
	return locked;
}

bool PartPlateList::preprocess_exclude_areas(arrangement::ArrangePolygons& unselected, int num_plates, float inflation)
{
	bool added = false;

	if (m_exclude_areas.size() > 0)
	{
		//has exclude areas
		PartPlate *plate = m_plate_list[0];

		for (int index = 0; index < plate->m_exclude_bounding_box.size(); index ++)
		{
			Polygon ap({
				{scaled(plate->m_exclude_bounding_box[index].min.x()), scaled(plate->m_exclude_bounding_box[index].min.y())},
				{scaled(plate->m_exclude_bounding_box[index].max.x()), scaled(plate->m_exclude_bounding_box[index].min.y())},
				{scaled(plate->m_exclude_bounding_box[index].max.x()), scaled(plate->m_exclude_bounding_box[index].max.y())},
				{scaled(plate->m_exclude_bounding_box[index].min.x()), scaled(plate->m_exclude_bounding_box[index].max.y())}
				});

			for (int j = 0; j < num_plates; j++)
			{
				arrangement::ArrangePolygon ret;
				ret.poly.contour = ap;
				ret.translation  = Vec2crd(0, 0);
				ret.rotation     = 0.0f;
				ret.is_virt_object = true;
				ret.bed_idx      = j;
				ret.height      = 1;
				ret.name = "ExcludedRegion" + std::to_string(index);
				ret.inflation = inflation;

				unselected.emplace_back(std::move(ret));
			}
			added = true;
		}
	}

	return added;
}

bool PartPlateList::preprocess_nonprefered_areas(arrangement::ArrangePolygons& regions, int num_plates, float inflation)
{
	bool added = false;

	std::vector<BoundingBoxf> nonprefered_regions;
	nonprefered_regions.emplace_back(Vec2d{ 45,0 }, Vec2d{ 225,25 }); // extrusion calibration region
	nonprefered_regions.emplace_back(Vec2d{ 25,0 }, Vec2d{ 50,60 });  // hand-eye calibration region

	//has exclude areas
	PartPlate* plate = m_plate_list[0];
	for (int index = 0; index < nonprefered_regions.size(); index++)
	{
		Polygon ap = scaled(nonprefered_regions[index]).polygon();
		for (int j = 0; j < num_plates; j++)
		{
			arrangement::ArrangePolygon ret;
			ret.poly.contour = ap;
			ret.translation = Vec2crd(0, 0);
			ret.rotation = 0.0f;
			ret.is_virt_object = true;
            ret.is_extrusion_cali_object = true;
			ret.bed_idx = j;
			ret.height = 1;
			ret.name = "NonpreferedRegion" + std::to_string(index);
			ret.inflation = inflation;

			regions.emplace_back(std::move(ret));
		}
		added = true;
	}
	return added;
}


//postprocess an arrangement::ArrangePolygon's bed index
void PartPlateList::postprocess_bed_index_for_selected(arrangement::ArrangePolygon& arrange_polygon)
{
	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": bed_idx %1%, locked_plate %2%, translation(x) %3%, (y) %4%") % arrange_polygon.bed_idx % arrange_polygon.locked_plate % unscale<double>(arrange_polygon.translation(X)) % unscale<double>(arrange_polygon.translation(Y));

	if (arrange_polygon.bed_idx == -1)
	{
		//outarea for large object, can not process here for the plate number maybe increased later
		BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(": can not be arranged inside plate!");
		return;
	}

	for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		if (m_plate_list[i]->is_locked())
		{
			BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": found locked_plate %1%, increate index by 1") % i;
			//arrange_polygon.translation(X) += scaled<double>(plate_stride_x());
			arrange_polygon.bed_idx += 1;
			//offset_x += scaled<double>(plate_stride_x());
		}
		else
		{
			//judge whether it is at the left side of the plate border
			if (arrange_polygon.bed_idx <= i)
			{
				BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(":found in plate_index %1%, bed_idx %2%") % i % arrange_polygon.bed_idx;
				return;
			}
		}
	}

	//create a new plate which can hold this arrange_polygon
	int plate_index = create_plate(false);

	while (plate_index != -1)
	{
		if (arrange_polygon.bed_idx <= plate_index)
		{
			BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(":new plate_index %1%, matches bed_idx %2%") % plate_index % arrange_polygon.bed_idx;
			break;
		}

		plate_index = create_plate(false);
	}

	return;
}

//postprocess an arrangement::ArrangePolygon's bed index
void PartPlateList::postprocess_bed_index_for_unselected(arrangement::ArrangePolygon& arrange_polygon)
{
	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": bed_idx %1%, locked_plate %2%, translation(x) %3%, (y) %4%") % arrange_polygon.bed_idx % arrange_polygon.locked_plate % unscale<double>(arrange_polygon.translation(X)) % unscale<double>(arrange_polygon.translation(Y));

	if (arrange_polygon.bed_idx == PartPlateList::MAX_PLATES_COUNT)
		return;

	for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		if (m_plate_list[i]->is_locked())
		{
			BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": found locked_plate %1%, increate index by 1") % i;
			//arrange_polygon.translation(X) += scaled<double>(plate_stride_x());
			arrange_polygon.bed_idx += 1;
			//offset_x += scaled<double>(plate_stride_x());
		}
		else
		{
			//judge whether it is at the left side of the plate border
			if (arrange_polygon.bed_idx <= i)
			{
				BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(":found in plate_index %1%, bed_idx %2%") % i % arrange_polygon.bed_idx;
				return;
			}
		}
	}

	return;
}

//postprocess an arrangement::ArrangePolygon, other instances are under locked states
void PartPlateList::postprocess_bed_index_for_current_plate(arrangement::ArrangePolygon& arrange_polygon)
{
	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": bed_idx %1%, locked_plate %2%, translation(x) %3%, (y) %4%") % arrange_polygon.bed_idx % arrange_polygon.locked_plate % unscale<double>(arrange_polygon.translation(X)) % unscale<double>(arrange_polygon.translation(Y));

	if (arrange_polygon.bed_idx == -1)
	{
		//outarea for large object
		BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(": can not be arranged inside plate!");
	}
	else if (arrange_polygon.bed_idx == 0)
		arrange_polygon.bed_idx += m_current_plate;
	else
		arrange_polygon.bed_idx = m_plate_list.size();

	return;
}

//postprocess an arrangement::ArrangePolygon
void PartPlateList::postprocess_arrange_polygon(arrangement::ArrangePolygon& arrange_polygon, bool selected)
{
	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": bed_idx %1%, selected %2%, translation(x) %3%, (y) %4%") % arrange_polygon.bed_idx % selected % unscale<double>(arrange_polygon.translation(X)) % unscale<double>(arrange_polygon.translation(Y));

	if ((selected) || (arrange_polygon.bed_idx != PartPlateList::MAX_PLATES_COUNT))
	{
		if (arrange_polygon.bed_idx == -1)
		{
			// outarea for large object
			arrange_polygon.bed_idx = m_plate_list.size();
			BoundingBox apbox(arrange_polygon.poly);
			auto        apbox_size = apbox.size();

			//arrange_polygon.translation(X) = scaled<double>(0.5 * plate_stride_x());
			//arrange_polygon.translation(Y) = scaled<double>(0.5 * plate_stride_y());
			arrange_polygon.translation(X) = 0.5 * apbox_size[0];
			arrange_polygon.translation(Y) = scaled<double>(static_cast<double>(m_plate_depth)) - 0.5 * apbox_size[1];
		}

		arrange_polygon.row = arrange_polygon.bed_idx / m_plate_cols;
		arrange_polygon.col = arrange_polygon.bed_idx % m_plate_cols;
		arrange_polygon.translation(X) += scaled<double>(plate_stride_x() * arrange_polygon.col);
		arrange_polygon.translation(Y) -= scaled<double>(plate_stride_y() * arrange_polygon.row);
	}

	return;
}

/*rendering related functions*/
//render
void PartPlateList::render(bool bottom, bool only_current, bool only_body, int hover_id)
{
	const std::lock_guard<std::mutex> local_lock(m_plates_mutex);
	std::vector<PartPlate*>::iterator it = m_plate_list.begin();

	int plate_hover_index = -1;
	int plate_hover_action = -1;
	if (hover_id != -1) {
		plate_hover_index = hover_id / PartPlate::GRABBER_COUNT;
		plate_hover_action = hover_id % PartPlate::GRABBER_COUNT;
	}

	static bool last_dark_mode_status = m_is_dark;
	if (m_is_dark != last_dark_mode_status) {
		last_dark_mode_status = m_is_dark;
		generate_icon_textures();
	}else if(m_del_texture.get_id() == 0)
		generate_icon_textures();
	for (it = m_plate_list.begin(); it != m_plate_list.end(); it++) {
		int current_index = (*it)->get_index();
		if (only_current && (current_index != m_current_plate))
			continue;
		if (current_index == m_current_plate) {
			PartPlate::HeightLimitMode height_mode = (only_current)?PartPlate::HEIGHT_LIMIT_NONE:m_height_limit_mode;
			if (plate_hover_index == current_index)
				(*it)->render(bottom, only_body, false, height_mode, plate_hover_action);
			else
				(*it)->render(bottom, only_body, false, height_mode, -1);
		}
		else {
			if (plate_hover_index == current_index)
				(*it)->render(bottom, only_body, false, PartPlate::HEIGHT_LIMIT_NONE, plate_hover_action);
			else
				(*it)->render(bottom, only_body, false, PartPlate::HEIGHT_LIMIT_NONE, -1);
		}
	}
}

void PartPlateList::render_for_picking_pass()
{
	const std::lock_guard<std::mutex> local_lock(m_plates_mutex);
	std::vector<PartPlate*>::iterator it = m_plate_list.begin();
	for (it = m_plate_list.begin(); it != m_plate_list.end(); it++) {
		(*it)->render_for_picking();
	}
}

/*int PartPlateList::select_plate_by_hover_id(int hover_id)
{
	int index = hover_id / PartPlate::GRABBER_COUNT;
	int sub_hover_id = hover_id % PartPlate::GRABBER_COUNT;
	if (sub_hover_id == 0) {
		select_plate(index);
	}
	else if (sub_hover_id == 1) {
		if (m_current_plate == 0) {
			select_plate(0);
		}
		else {
			select_plate(index - 1);
		}
	}
	else if (sub_hover_id == 2) {
		if (m_current_plate == (get_plate_count() - 1)) {
			select_plate(m_current_plate);
		}
		else {
			select_plate(index + 1);
		}
	}
	else {
		return -1;
	}
	return 0;
}*/

void PartPlateList::set_render_option(bool bedtype_texture, bool plate_settings)
{
    render_bedtype_logo = bedtype_texture;
    render_plate_settings = plate_settings;
}

int PartPlateList::select_plate_by_obj(int obj_index, int instance_index)
{
	int ret = 0, index;
	PartPlate* plate = NULL;

	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": obj_id %1%, instance_id %2%") % obj_index % instance_index;
	index = find_instance(obj_index, instance_index);
	if (index != -1)
	{
		//found it in plate
		BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": found it in plate %1%") % index;
		select_plate(index);
		return 0;
	}
	return -1;
}

void PartPlateList::calc_bounding_boxes()
{
	m_bounding_box.reset();
	std::vector<PartPlate*>::iterator it = m_plate_list.begin();
	for (it = m_plate_list.begin(); it != m_plate_list.end(); it++) {
		m_bounding_box.merge((*it)->get_bounding_box(true));
	}
}

void PartPlateList::select_plate_view()
{
	if (m_current_plate < 0 || m_current_plate >= m_plate_list.size()) return;

	Vec3d target = m_plate_list[m_current_plate]->get_bounding_box(false).center();
	Vec3d position(target.x(), target.y(), m_plater->get_camera().get_distance());
	m_plater->get_camera().look_at(position, target, Vec3d::UnitY());
	m_plater->get_camera().select_view("topfront");
}

bool PartPlateList::set_shapes(const Pointfs& shape, const Pointfs& exclude_areas, const std::string& texture_filename, float height_to_lid, float height_to_rod)
{
	const std::lock_guard<std::mutex> local_lock(m_plates_mutex);
	m_shape = shape;
	m_exclude_areas = exclude_areas;
	m_height_to_lid = height_to_lid;
	m_height_to_rod = height_to_rod;

	double stride_x = plate_stride_x();
	double stride_y = plate_stride_y();
	for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		PartPlate* plate = m_plate_list[i];
		assert(plate != NULL);

		Vec2d pos;

		pos = compute_shape_position(i, m_plate_cols);
		plate->set_shape(shape, exclude_areas, pos, height_to_lid, height_to_rod);
	}

	calc_bounding_boxes();

	auto check_texture = [](const std::string& texture) {
		boost::system::error_code ec; // so the exists call does not throw (e.g. after a permission problem)
		return !texture.empty() && (boost::algorithm::iends_with(texture, ".png") || boost::algorithm::iends_with(texture, ".svg")) && boost::filesystem::exists(texture, ec);
	};
	if (! texture_filename.empty() && ! check_texture(texture_filename)) {
		BOOST_LOG_TRIVIAL(error) << "Unable to load bed texture: " << texture_filename;
	}
	else
		m_logo_texture_filename = texture_filename;

	return true;
}

/*slice related functions*/
//update current slice context into backgroud slicing process
void PartPlateList::update_slice_context_to_current_plate(BackgroundSlicingProcess& process)
{
	PartPlate* current_plate;

	current_plate = m_plate_list[m_current_plate];
	assert(current_plate != NULL);

	current_plate->update_slice_context(process);

	return;
}

//return the current fff print object
Print& PartPlateList::get_current_fff_print() const
{
	PartPlate* current_plate;
	Print* print;

	current_plate = m_plate_list[m_current_plate];
	//BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":m_current_plate %1%, current_plate %2%") % m_current_plate % current_plate;
	assert(current_plate != NULL);

	current_plate->get_print((PrintBase **)&print, nullptr, nullptr);

	return *print;
}

//return the slice result
GCodeProcessorResult* PartPlateList::get_current_slice_result() const
{
	PartPlate* current_plate;

	current_plate = m_plate_list[m_current_plate];
	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":m_current_plate %1%, current_plate %2%") % m_current_plate % current_plate;
	assert(current_plate != NULL);

	return current_plate->get_slice_result();
}

//invalid all the plater's slice result
void PartPlateList::invalid_all_slice_result()
{
	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": plates count %1%") % m_plate_list.size();
	for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		m_plate_list[i]->update_slice_result_valid_state(false);
	}

	return;
}

//check whether all plates's slice result valid
bool PartPlateList::is_all_slice_results_valid() const
{
	for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		if (!m_plate_list[i]->is_slice_result_valid())
			return false;
	}
	return true;
}

//check whether all plates's slice result valid for print
bool PartPlateList::is_all_slice_results_ready_for_print() const
{
	for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		if (!m_plate_list[i]->is_slice_result_ready_for_print()
			&& m_plate_list[i]->has_printable_instances()
			)
			return false;
	}
	return true;
}


//check whether all plates ready for slice
bool PartPlateList::is_all_plates_ready_for_slice() const
{
    for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		if (m_plate_list[i]->can_slice())
			return true;
	}
	return false;
}

//will create a plate and load gcode, return the plate index
int PartPlateList::create_plate_from_gcode_file(const std::string& filename)
{
	int ret = 0;

	return ret;
}

void PartPlateList::get_sliced_result(std::vector<bool>& sliced_result, std::vector<std::string>& gcode_paths)
{
	sliced_result.resize(m_plate_list.size());
	gcode_paths.resize(m_plate_list.size());

	for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		sliced_result[i] = m_plate_list[i]->m_slice_result_valid;
		gcode_paths[i] = m_plate_list[i]->m_tmp_gcode_path;
	}
}
//rebuild data which are not serialized after de-serialize
int PartPlateList::rebuild_plates_after_deserialize(std::vector<bool>& previous_sliced_result, std::vector<std::string>& previous_gcode_paths)
{
	int ret = 0;

	BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": plates count %1%") % m_plate_list.size();
	update_plate_cols();
	set_shapes(m_shape, m_exclude_areas, m_logo_texture_filename, m_height_to_lid, m_height_to_rod);
	for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		bool need_reset_print = false;
		m_plate_list[i]->m_plater = this->m_plater;
		m_plate_list[i]->m_partplate_list = this;
		m_plate_list[i]->m_model = this->m_model;
		m_plate_list[i]->printer_technology = this->printer_technology;
		//check the previous sliced result
		if (m_plate_list[i]->m_slice_result_valid) {
			if ((i >= previous_sliced_result.size()) || !previous_sliced_result[i])
				m_plate_list[i]->update_slice_result_valid_state(false);
		}
		if ((i < previous_gcode_paths.size())
			&& !previous_gcode_paths[i].empty()
			&& (m_plate_list[i]->m_tmp_gcode_path != previous_gcode_paths[i])) {
			if (boost::filesystem::exists(previous_gcode_paths[i])) {
				boost::nowide::remove(previous_gcode_paths[i].c_str());
				need_reset_print = true;
			}
		}

		std::map<int, PrintBase*>::iterator it = m_print_list.find(m_plate_list[i]->m_print_index);
		std::map<int, GCodeResult*>::iterator it2 = m_gcode_result_list.find(m_plate_list[i]->m_print_index);
		if (it != m_print_list.end())
		{
			//find it
			if (it2 == m_gcode_result_list.end())
			{
				//should not happen
				assert(0);
				BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(":can not find gcode result for plate %1%, print index %2%") % i % m_plate_list[i]->m_print_index;
				delete it->second;
				m_print_list.erase(it);
			}
			else
			{
				m_plate_list[i]->set_print(it->second, it2->second, m_plate_list[i]->m_print_index);
				it->second->set_plate_index(i);
				if (need_reset_print) {
					Print *print = dynamic_cast<Print*>(it->second);
					it2->second->reset();
					print->set_gcode_file_invalidated();
					if ((i == m_current_plate)&&m_plater)
						m_plater->reset_gcode_toolpaths();
				}
				continue;
			}
		}

		//can not find, create a new one
		Print* print = new Print();
		GCodeResult* gcode = new GCodeResult();
		m_print_list.emplace(m_print_index, print);
		m_gcode_result_list.emplace(m_print_index, gcode);
		m_plate_list[i]->set_print(print, gcode, m_print_index);
		print->set_plate_index(i);
		m_print_index++;
	}

	//go through the print list, and delete the one not used by plate
	std::map<int, PrintBase*>::iterator it = m_print_list.begin();
	int print_index;
	std::vector<int> delete_list;
	while (it != m_print_list.end())
	{
		print_index = it->first;

		int plate_index = find_plate_by_print_index(print_index);
		if (plate_index < 0)
		{
			delete_list.push_back(print_index);
		}
		it++;
	}

	for (unsigned int index = 0; index < delete_list.size(); index++)
	{
		destroy_print(delete_list[index]);
	}

	//update the bed's position
	Vec2d pos = compute_shape_position(m_current_plate, m_plate_cols);
	m_plater->set_bed_position(pos);

	//not used
	/*if (m_plate_width == 0)
	{
		BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": jump to the first init state, need to re-set size!");
		Vec3d max = m_plater->get_bed().get_bounding_box(false).max;
		Vec3d min = m_plater->get_bed().get_bounding_box(false).min;
		double z = m_plater->config()->opt_float("printable_height");
		reset_size(max.x() - min.x(), max.y() - min.y(), z);
	}*/
	return ret;
}

//retruct plates structures after auto-arrangement
int PartPlateList::rebuild_plates_after_arrangement(bool recycle_plates, bool except_locked, int plate_index)
{
	int ret = 0;

	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":before rebuild, plates count %1%, recycle_plates %2%") % m_plate_list.size() % recycle_plates;

	// sort by arrange_order
	std::sort(m_model->objects.begin(), m_model->objects.end(), [](auto a, auto b) {return a->instances[0]->arrange_order < b->instances[0]->arrange_order; });
	//for (auto object : m_model->objects)
	//	std::sort(object->instances.begin(), object->instances.end(), [](auto a, auto b) {return a->arrange_order < b->arrange_order; });

	ret = reload_all_objects(except_locked, plate_index);

	if (recycle_plates)
	{
		for (unsigned int i = (unsigned int)m_plate_list.size() - 1; i > 0; --i)
		{
			if (m_plate_list[i]->empty()
				|| !m_plate_list[i]->has_printable_instances())
			{
				//delete it
				BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":delete plate %1% for empty") % i;
				delete_plate(i);
			}
			else if (m_plate_list[i]->is_locked()) {
				continue;
			}
			else
			{
				break;
			}
		}
	}

#if 0
	if (m_plater != nullptr) {
		// In GUI mode
		wxGetApp().obj_list()->reload_all_plates();
	}
#endif

	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":after rebuild, plates count %1%") % m_plate_list.size();
	return ret;
}

int PartPlateList::store_to_3mf_structure(PlateDataPtrs& plate_data_list, bool with_slice_info, int plate_idx)
{
	int ret = 0;

	plate_data_list.clear();
	plate_data_list.reserve(m_plate_list.size());
	for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		PlateData* plate_data_item = new PlateData();
		plate_data_item->locked = m_plate_list[i]->m_locked;
		plate_data_item->plate_index = m_plate_list[i]->m_plate_index;
		plate_data_item->plate_name = m_plate_list[i]->get_plate_name();
		BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": plate %1% before load, width %2%, height %3%, size %4%!")
			%(i+1) %m_plate_list[i]->thumbnail_data.width %m_plate_list[i]->thumbnail_data.height %m_plate_list[i]->thumbnail_data.pixels.size();
		plate_data_item->plate_thumbnail.load_from(m_plate_list[i]->thumbnail_data);
		BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": plate %1% after load, width %2%, height %3%, size %4%!")
			%(i+1) %plate_data_item->plate_thumbnail.width %plate_data_item->plate_thumbnail.height %plate_data_item->plate_thumbnail.pixels.size();
		plate_data_item->config.apply(*m_plate_list[i]->config());

		if (m_plate_list[i]->obj_to_instance_set.size() > 0)
		{
			for (std::set<std::pair<int, int>>::iterator it = m_plate_list[i]->obj_to_instance_set.begin(); it != m_plate_list[i]->obj_to_instance_set.end(); ++it)
				plate_data_item->objects_and_instances.emplace_back(it->first, it->second);
		}

		BOOST_LOG_TRIVIAL(info) << __FUNCTION__ <<boost::format(": plate %1%, gcode_filename=%2%, with_slice_info=%3%, slice_valid %4%, object item count %5%.")
			%i %m_plate_list[i]->m_gcode_result->filename % with_slice_info %m_plate_list[i]->is_slice_result_valid()%plate_data_item->objects_and_instances.size();

		if (with_slice_info) {
			if (m_plate_list[i]->get_slice_result() && m_plate_list[i]->is_slice_result_valid()) {
				// BBS only include current palte_idx
				if (plate_idx == i || plate_idx == PLATE_CURRENT_IDX || plate_idx == PLATE_ALL_IDX) {
					//load calibration thumbnail
					if (m_plate_list[i]->cali_thumbnail_data.is_valid())
						plate_data_item->pattern_file = "valid_pattern";
					if (m_plate_list[i]->cali_bboxes_data.is_valid())
						plate_data_item->pattern_bbox_file = "valid_pattern_bbox";
					plate_data_item->gcode_file       = m_plate_list[i]->m_gcode_result->filename;
					plate_data_item->is_sliced_valid  = true;
					plate_data_item->gcode_prediction = std::to_string(
						(int) m_plate_list[i]->get_slice_result()->print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].time);
					plate_data_item->toolpath_outside = m_plate_list[i]->m_gcode_result->toolpath_outside;
					Print *print                      = nullptr;
					m_plate_list[i]->get_print((PrintBase **) &print, nullptr, nullptr);
					if (print) {
						const PrintStatistics &ps = print->print_statistics();
						if (ps.total_weight != 0.0) {
							CNumericLocalesSetter locales_setter;
							plate_data_item->gcode_weight =wxString::Format("%.2f", ps.total_weight).ToStdString();
						}
						plate_data_item->is_support_used = print->is_support_used();
					} else {
						BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format("print is null!");
					}
					//parse filament info
					plate_data_item->parse_filament_info(m_plate_list[i]->get_slice_result());
				} else {
					BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "slice result = " << m_plate_list[i]->get_slice_result()
										<< ", result valid = " << m_plate_list[i]->is_slice_result_valid();
				}
			}
		}

		plate_data_list.push_back(plate_data_item);
	}
	BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":stored %1% plates!") % m_plate_list.size();

	return ret;
}

int PartPlateList::load_from_3mf_structure(PlateDataPtrs& plate_data_list)
{
	int ret = 0;

	if (plate_data_list.size() <= 0)
	{
		BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(":no plates, should not happen!");
		return -1;
	}
	clear(true, true);
	for (unsigned int i = 0; i < (unsigned int)plate_data_list.size(); ++i)
	{
		int index = create_plate(false);
		m_plate_list[index]->m_locked = plate_data_list[i]->locked;
		m_plate_list[index]->config()->apply(plate_data_list[i]->config);
		m_plate_list[index]->set_plate_name(plate_data_list[i]->plate_name);
		if (plate_data_list[i]->plate_index != index)
		{
			BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(":plate index %1% seems invalid, skip it")% plate_data_list[i]->plate_index;
		}
		BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": plate %1%, gcode_file %2%, is_sliced_valid %3%, toolpath_outside %4%, is_support_used %5%")
			%i %plate_data_list[i]->gcode_file %plate_data_list[i]->is_sliced_valid %plate_data_list[i]->toolpath_outside %plate_data_list[i]->is_support_used;
		//load object and instance from 3mf
		//just test for file correct or not, we will rebuild later
		/*for (std::vector<std::pair<int, int>>::iterator it = plate_data_list[i]->objects_and_instances.begin(); it != plate_data_list[i]->objects_and_instances.end(); ++it)
			m_plate_list[index]->obj_to_instance_set.insert(std::pair(it->first, it->second));*/
		if (!plate_data_list[i]->gcode_file.empty()) {
			m_plate_list[index]->m_gcode_path_from_3mf = plate_data_list[i]->gcode_file;
		}
		GCodeResult* gcode_result = nullptr;
		PrintBase* fff_print = nullptr;
		m_plate_list[index]->get_print(&fff_print, &gcode_result, nullptr);
		PrintStatistics& ps = (dynamic_cast<Print*>(fff_print))->print_statistics();
		gcode_result->print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].time = atoi(plate_data_list[i]->gcode_prediction.c_str());
		ps.total_weight = atof(plate_data_list[i]->gcode_weight.c_str());
		ps.total_used_filament = 0.f;
		for (auto filament_item: plate_data_list[i]->slice_filaments_info)
		{
			ps.total_used_filament += filament_item.used_m;
		}
		ps.total_used_filament *= 1000; //koef
		gcode_result->toolpath_outside = plate_data_list[i]->toolpath_outside;
		m_plate_list[index]->slice_filaments_info = plate_data_list[i]->slice_filaments_info;
		gcode_result->warnings = plate_data_list[i]->warnings;
		if (m_plater && !plate_data_list[i]->thumbnail_file.empty()) {
			BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": plate %1%, load thumbnail from %2%.")%(i+1) %plate_data_list[i]->thumbnail_file;
			if (boost::filesystem::exists(plate_data_list[i]->thumbnail_file)) {
				m_plate_list[index]->load_thumbnail_data(plate_data_list[i]->thumbnail_file);
				BOOST_LOG_TRIVIAL(info) << __FUNCTION__ <<boost::format(": plate %1% after load, width %2%, height %3%, size %4%!")
					%(i+1) %m_plate_list[index]->thumbnail_data.width %m_plate_list[index]->thumbnail_data.height %m_plate_list[index]->thumbnail_data.pixels.size();
			}
		}

		if (m_plater && !plate_data_list[i]->pattern_file.empty()) {
			if (boost::filesystem::exists(plate_data_list[i]->pattern_file)) {
				//no need to load pattern data currently
				//m_plate_list[index]->load_pattern_thumbnail_data(plate_data_list[i]->pattern_file);
			}
		}
		if (m_plater && !plate_data_list[i]->pattern_bbox_file.empty()) {
			if (boost::filesystem::exists(plate_data_list[i]->pattern_bbox_file)) {
				m_plate_list[index]->load_pattern_box_data(plate_data_list[i]->pattern_bbox_file);
			}
		}

	}
	print();
	ret = reload_all_objects();
	print();

	return ret;
}

//load gcode files
int PartPlateList::load_gcode_files()
{
	int ret = 0;

	//only do this while m_plater valid for gui mode
	if (!m_plater)
		return ret;

	for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		if (!m_plate_list[i]->m_gcode_path_from_3mf.empty()) {
			//the same as plater::priv::update_print_volume_state();
			//BoundingBoxf3   print_volume = m_plate_list[i]->get_bounding_box(false);
			//print_volume.max(2) = this->m_plate_height;
			//print_volume.min(2) = -1e10;
			m_model->update_print_volume_state({m_plate_list[i]->get_shape(), (double)this->m_plate_height });

			if (!m_plate_list[i]->load_gcode_from_file(m_plate_list[i]->m_gcode_path_from_3mf))
				ret ++;
		}
	}

	BOOST_LOG_TRIVIAL(trace) << boost::format("totally got %1% gcode files") % ret;

	return ret;
}

void PartPlateList::print() const
{
	BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << boost::format("PartPlateList %1%, m_plate_count %2%, current_plate %3%, print_count %4%, current print index %5%, plate cols %6%") % this % m_plate_count % m_current_plate % m_print_list.size() % m_print_index % m_plate_cols;
	BOOST_LOG_TRIVIAL(trace) << boost::format("m_plate_width %1%, m_plate_depth %2%, m_plate_height %3%, plate count %4%\nplate list:") % m_plate_width % m_plate_depth % m_plate_height % m_plate_list.size();
	for (unsigned int i = 0; i < (unsigned int)m_plate_list.size(); ++i)
	{
		BOOST_LOG_TRIVIAL(trace) << boost::format("the %1%th plate") % i;
		m_plate_list[i]->print();
	}
	BOOST_LOG_TRIVIAL(trace) << boost::format("the unprintable plate:");
	unprintable_plate.print();

	flush_logs();
	return;
}

bool PartPlateList::is_load_bedtype_textures = false;

void PartPlateList::BedTextureInfo::TexturePart::update_buffer()
{
	if (w == 0 || h == 0) {
		return;
	}

	Pointfs rectangle;
	rectangle.push_back(Vec2d(x, y));
	rectangle.push_back(Vec2d(x+w, y));
	rectangle.push_back(Vec2d(x+w, y+h));
	rectangle.push_back(Vec2d(x, y+h));
	ExPolygon poly;

	for (int i = 0; i < 4; i++) {
		const Vec2d & p = rectangle[i];
		for (auto& p : rectangle) {
			Vec2d pp = Vec2d(p.x() + offset.x(), p.y() + offset.y());
			poly.contour.append({ scale_(pp(0)), scale_(pp(1)) });
		}
	}

	if (!buffer)
		buffer = new GeometryBuffer();

	if (buffer->set_from_triangles(triangulate_expolygon_2f(poly, NORMALS_UP), GROUND_Z + 0.02f)) {
		if (vbo_id != 0) {
			glsafe(::glDeleteBuffers(1, &vbo_id));
			vbo_id = 0;
		}
		unsigned int* vbo_id_ptr = const_cast<unsigned int*>(&vbo_id);
		glsafe(::glGenBuffers(1, vbo_id_ptr));
		glsafe(::glBindBuffer(GL_ARRAY_BUFFER, *vbo_id_ptr));
		glsafe(::glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)buffer->get_vertices_data_size(), (const GLvoid*)buffer->get_vertices_data(), GL_STATIC_DRAW));
		glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
	} else {
		BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ":Unable to create buffer triangles\n";
	}
}

void PartPlateList::init_bed_type_info()
{
	BedTextureInfo::TexturePart pc_part1(  5, 130,  10, 110, "bbl_bed_pc_left.svg");
	BedTextureInfo::TexturePart pc_part2( 74, -12, 150,  12, "bbl_bed_pc_bottom.svg");
	BedTextureInfo::TexturePart ep_part1(  4,  87,  12, 153, "bbl_bed_ep_left.svg");
	BedTextureInfo::TexturePart ep_part2( 72, -11, 150,  12, "bbl_bed_ep_bottom.svg");
	BedTextureInfo::TexturePart pei_part1( 6,  50,  12, 190, "bbl_bed_pei_left.svg");
	BedTextureInfo::TexturePart pei_part2(72, -11, 150,  12, "bbl_bed_pei_bottom.svg");
	BedTextureInfo::TexturePart pte_part1( 6,  40,  12, 200, "bbl_bed_pte_left.svg");
	BedTextureInfo::TexturePart pte_part2(72, -11, 150,  12, "bbl_bed_pte_bottom.svg");

	bed_texture_info[btPC].parts.push_back(pc_part1);
	bed_texture_info[btPC].parts.push_back(pc_part2);
	bed_texture_info[btEP].parts.push_back(ep_part1);
	bed_texture_info[btEP].parts.push_back(ep_part2);
	bed_texture_info[btPEI].parts.push_back(pei_part1);
	bed_texture_info[btPEI].parts.push_back(pei_part2);
	bed_texture_info[btPTE].parts.push_back(pte_part1);
	bed_texture_info[btPTE].parts.push_back(pte_part2);

	for (int i = 0; i < btCount; i++) {
		for (int j = 0; j < bed_texture_info[i].parts.size(); j++) {
			bed_texture_info[i].parts[j].update_buffer();
		}
	}
}

void PartPlateList::load_bedtype_textures()
{
	if (PartPlateList::is_load_bedtype_textures) return;

	init_bed_type_info();
	GLint max_tex_size = OpenGLManager::get_gl_info().get_max_tex_size();
	GLint logo_tex_size = (max_tex_size < 2048) ? max_tex_size : 2048;
	for (int i = 0; i < (unsigned int)btCount; ++i) {
		for (int j = 0; j < bed_texture_info[i].parts.size(); j++) {
			std::string filename = resources_dir() + "/images/" + bed_texture_info[i].parts[j].filename;
			if (boost::filesystem::exists(filename)) {
				PartPlateList::bed_texture_info[i].parts[j].texture = new GLTexture();
				if (!PartPlateList::bed_texture_info[i].parts[j].texture->load_from_svg_file(filename, true, true, true, logo_tex_size)) {
					BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(": load logo texture from %1% failed!") % filename;
				}
			} else {
				BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(": load logo texture from %1% failed!") % filename;
			}
		}
	}
	PartPlateList::is_load_bedtype_textures = true;
}

}//end namespace GUI
}//end namespace slic3r
