#ifndef slic3r_BedShapeDialog_hpp_
#define slic3r_BedShapeDialog_hpp_
// The bed shape dialog.
// The dialog opens from Print Settins tab->Bed Shape : Set...

#include "OptionsGroup.hpp"
#include "2DBed.hpp"
#include "I18N.hpp"

#include <wx/dialog.h>
#include <wx/choicebk.h>

namespace Slic3r {
namespace GUI {

using ConfigOptionsGroupShp = std::shared_ptr<ConfigOptionsGroup>;
class BedShapePanel : public wxPanel
{
    static const std::string NONE;
    static const std::string EMPTY_STRING;

	Bed_2D*			   m_canvas;
    std::vector<Vec2d> m_shape;
    std::vector<Vec2d> m_loaded_shape;
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
