#include "LambdaObjectDialog.hpp"

#include <wx/window.h>
#include <wx/button.h>
#include "OptionsGroup.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"

namespace Slic3r
{
namespace GUI
{

LambdaObjectDialog::LambdaObjectDialog(wxWindow* parent,
                                       const wxString type_name):
                                       m_type_name(type_name)
{
	Create(parent, wxID_ANY, _(L("Lambda Object")),
        parent->GetScreenPosition(), wxDefaultSize,
		wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

	// instead of double dim[3] = { 1.0, 1.0, 1.0 };
	object_parameters.dim[0] = 1.0;
	object_parameters.dim[1] = 1.0;
	object_parameters.dim[2] = 1.0;

	sizer = new wxBoxSizer(wxVERTICAL);

	// modificator options
    if (m_type_name == wxEmptyString) {
        m_modificator_options_book = new wxChoicebook(  this, wxID_ANY, wxDefaultPosition,
                                                        wxDefaultSize, wxCHB_TOP);
        sizer->Add(m_modificator_options_book, 1, wxEXPAND | wxALL, 10);
    }
    else {
        m_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
        sizer->Add(m_panel, 1, wxEXPAND | wxALL, 10);
    }

    ConfigOptionDef def;
    def.width = 70;
	auto optgroup = init_modificator_options_page(_(L("Box")));
    if (optgroup) {
		optgroup->m_on_change = [this](t_config_option_key opt_key, boost::any value) {
			int opt_id =	opt_key == "l" ? 0 :
							opt_key == "w" ? 1 : 
							opt_key == "h" ? 2 : -1;
			if (opt_id < 0) return;
			object_parameters.dim[opt_id] = boost::any_cast<double>(value);
		};

		def.type = coFloat;
		def.set_default_value(new ConfigOptionFloat{ 1.0 });
		def.label = L("Length");
		Option option(def, "l");
		optgroup->append_single_option_line(option);
		
		def.label = L("Width");
		option = Option(def, "w");
		optgroup->append_single_option_line(option);
		
		def.label = L("Height");
		option = Option(def, "h");
		optgroup->append_single_option_line(option);
	}

	optgroup = init_modificator_options_page(_(L("Cylinder")));
	if (optgroup) {
		optgroup->m_on_change = [this](t_config_option_key opt_key, boost::any value) {
			int val = boost::any_cast<int>(value);
			if (opt_key == "cyl_r")
				object_parameters.cyl_r = val;
			else if (opt_key == "cyl_h")
				object_parameters.cyl_h = val;
			else return;
		};

		def.type = coInt;
		def.set_default_value(new ConfigOptionInt{ 1 });
		def.label = L("Radius");
		auto option = Option(def, "cyl_r");
		optgroup->append_single_option_line(option);

		def.label = L("Height");
		option = Option(def, "cyl_h");
		optgroup->append_single_option_line(option);
    }

	optgroup = init_modificator_options_page(_(L("Sphere")));
	if (optgroup) {
		optgroup->m_on_change = [this](t_config_option_key opt_key, boost::any value) {
			if (opt_key == "sph_rho")
				object_parameters.sph_rho = boost::any_cast<double>(value);
			else return;
		};

		def.type = coFloat;
		def.set_default_value(new ConfigOptionFloat{ 1.0 });
		def.label = L("Rho");
		auto option = Option(def, "sph_rho");
		optgroup->append_single_option_line(option);
	}

	optgroup = init_modificator_options_page(_(L("Slab")));
	if (optgroup) {
		optgroup->m_on_change = [this](t_config_option_key opt_key, boost::any value) {
			double val = boost::any_cast<double>(value);
			if (opt_key == "slab_z")
				object_parameters.slab_z = val;
			else if (opt_key == "slab_h")
				object_parameters.slab_h = val;
			else return;
		};

		def.type = coFloat;
		def.set_default_value(new ConfigOptionFloat{ 1.0 });
		def.label = L("Height");
		auto option = Option(def, "slab_h");
		optgroup->append_single_option_line(option);

		def.label = L("Initial Z");
		option = Option(def, "slab_z");
		optgroup->append_single_option_line(option);
	}

	Bind(wxEVT_CHOICEBOOK_PAGE_CHANGED, ([this](wxCommandEvent e)
	{
		auto page_idx = m_modificator_options_book->GetSelection();
		if (page_idx < 0) return;
		switch (page_idx)
		{
		case 0:
			object_parameters.type = LambdaTypeBox;
			break;
		case 1:
			object_parameters.type = LambdaTypeCylinder;
			break;
		case 2:
			object_parameters.type = LambdaTypeSphere;
			break;
		case 3:
			object_parameters.type = LambdaTypeSlab;
			break;
		default:
			break;
		}
	}));

	const auto button_sizer = CreateStdDialogButtonSizer(wxOK | wxCANCEL);

	wxButton* btn_OK = static_cast<wxButton*>(FindWindowById(wxID_OK, this));
	btn_OK->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		// validate user input
		if (!CanClose())return;
		EndModal(wxID_OK);
		Destroy();
	});

	wxButton* btn_CANCEL = static_cast<wxButton*>(FindWindowById(wxID_CANCEL, this));
	btn_CANCEL->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		// validate user input
		if (!CanClose())return;
		EndModal(wxID_CANCEL);
		Destroy();
	});

	sizer->Add(button_sizer, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM, 10);

	SetSizer(sizer);
	sizer->Fit(this);
	sizer->SetSizeHints(this);
}

// Called from the constructor.
// Create a panel for a rectangular / circular / custom bed shape.
ConfigOptionsGroupShp LambdaObjectDialog::init_modificator_options_page(const wxString& title)
{
    if (!m_type_name.IsEmpty() && m_type_name != title)
        return nullptr;

    auto panel = m_type_name.IsEmpty() ? new wxPanel(m_modificator_options_book) : m_panel;

	ConfigOptionsGroupShp optgroup;
	optgroup = std::make_shared<ConfigOptionsGroup>(panel, _(L("Add")) + " " +title + " " +dots);
	optgroup->label_width = 100;

	m_optgroups.push_back(optgroup);

    if (m_type_name.IsEmpty()) {
        panel->SetSizerAndFit(optgroup->sizer);
        m_modificator_options_book->AddPage(panel, title);
    }
    else
        panel->SetSizer(optgroup->sizer);

    return optgroup;
}


} //namespace GUI
} //namespace Slic3r 
