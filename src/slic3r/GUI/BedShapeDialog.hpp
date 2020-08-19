#ifndef slic3r_BedShapeDialog_hpp_
#define slic3r_BedShapeDialog_hpp_
// The bed shape dialog.
// The dialog opens from Print Settins tab->Bed Shape : Set...

#include "GUI_Utils.hpp"
#include "2DBed.hpp"
#include "I18N.hpp"

#include <wx/dialog.h>
#include <wx/choicebk.h>

namespace Slic3r {
namespace GUI {

class ConfigOptionsGroup;

using ConfigOptionsGroupShp = std::shared_ptr<ConfigOptionsGroup>;

struct BedShape {

    enum class Type {
        Rectangular = 0,
        Circular,
        Custom,
        Invalid
    };

    enum class Parameter {
        RectSize,
        RectOrigin,
        Diameter
    };

    BedShape(const ConfigOptionPoints& points) {
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

                type = Type::Rectangular;
                rectSize = Vec2d(x_max - x_min, y_max - y_min);
                rectOrigin = Vec2d(-x_min, -y_min);

                return;
            }
        }

        // is this a circle ?
        {
            // Analyze the array of points.Do they reside on a circle ?
            auto center = polygon.bounding_box().center();
            std::vector<double> vertex_distances;
            double avg_dist = 0;
            for (auto pt : polygon.points)
            {
                double distance = (pt - center).cast<double>().norm();
                vertex_distances.push_back(distance);
                avg_dist += distance;
            }

            avg_dist /= vertex_distances.size();
            bool defined_value = true;
            for (auto el : vertex_distances)
            {
                if (abs(el - avg_dist) > 10 * SCALED_EPSILON)
                    defined_value = false;
                break;
            }
            if (defined_value) {
                // all vertices are equidistant to center
                type = Type::Circular;
                diameter = unscale<double>(avg_dist * 2);

                return;
            }
        }

        if (points.size() < 3) {
            type = Type::Invalid;
            return;
        }

        // This is a custom bed shape, use the polygon provided.
        type = Type::Custom;
    }

    static void     append_option_line(ConfigOptionsGroupShp optgroup, Parameter param);
    static wxString get_name(Type type);

    wxString        get_full_name_with_params();

    Type type = Type::Invalid;
    Vec2d rectSize;
    Vec2d rectOrigin;

    double diameter;
};

class BedShapePanel : public wxPanel
{
    static const std::string NONE;
    static const std::string EMPTY_STRING;

	Bed_2D*			   m_canvas;
    std::vector<Vec2d> m_shape;
    std::vector<Vec2d> m_loaded_shape;
	std::string        m_custom_shape;
    std::string        m_custom_texture;
    std::string        m_custom_model;

public:
    BedShapePanel(wxWindow* parent) : wxPanel(parent, wxID_ANY), m_custom_texture(NONE), m_custom_model(NONE) {}

    void build_panel(const ConfigOptionPoints& default_pt, const ConfigOptionString& custom_texture, const ConfigOptionString& custom_model);

    // Returns the resulting bed shape polygon. This value will be stored to the ini file.
    const std::vector<Vec2d>& get_shape() const { return m_shape; }
    const std::string& get_custom_texture() const { return (m_custom_texture != NONE) ? m_custom_texture : EMPTY_STRING; }
    const std::string& get_custom_model() const { return (m_custom_model != NONE) ? m_custom_model : EMPTY_STRING; }

private:
    ConfigOptionsGroupShp	init_shape_options_page(const wxString& title);
    wxPanel*    init_texture_panel();
    wxPanel*    init_model_panel();
    void		set_shape(const ConfigOptionPoints& points);
    void		update_preview();
	void		update_shape();
	void		load_stl();
    void		load_texture();
    void		load_model();

	wxChoicebook*	m_shape_options_book;
	std::vector <ConfigOptionsGroupShp>	m_optgroups;

    friend class BedShapeDialog;
};

class BedShapeDialog : public DPIDialog
{
	BedShapePanel*	m_panel;
public:
	BedShapeDialog(wxWindow* parent) : DPIDialog(parent, wxID_ANY, _(L("Bed Shape")),
        wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {}

    void build_dialog(const ConfigOptionPoints& default_pt, const ConfigOptionString& custom_texture, const ConfigOptionString& custom_model);

    const std::vector<Vec2d>& get_shape() const { return m_panel->get_shape(); }
    const std::string& get_custom_texture() const { return m_panel->get_custom_texture(); }
    const std::string& get_custom_model() const { return m_panel->get_custom_model(); }

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;
};

} // GUI
} // Slic3r


#endif  /* slic3r_BedShapeDialog_hpp_ */
