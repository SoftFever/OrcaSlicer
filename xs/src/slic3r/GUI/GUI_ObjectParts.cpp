#include "GUI.hpp"
#include "GUI_ObjectParts.hpp"
#include "Model.hpp"
#include "wxExtensions.hpp"
#include "LambdaObjectDialog.hpp"
#include "../../libslic3r/Utils.hpp"

#include <wx/msgdlg.h>
#include <wx/frame.h>
#include <boost/filesystem.hpp>

namespace Slic3r
{
namespace GUI
{
bool m_parts_changed = false;
bool m_part_settings_changed = false;

bool is_parts_changed(){return m_parts_changed;}
bool is_part_settings_changed(){ return m_part_settings_changed; }

void load_part(	wxWindow* parent, ModelObject* model_object, 
				wxArrayString& part_names, const bool is_modifier)
{
	wxArrayString input_files;
	open_model(parent, input_files);
	for (int i = 0; i < input_files.size(); ++i) {
		std::string input_file = input_files.Item(i).ToStdString();

		Model model;
		try {
			model = Model::read_from_file(input_file);
		}
		catch (std::exception &e) {
			auto msg = _(L("Error! ")) + input_file + " : " + e.what() + ".";
			show_error(parent, msg);
			exit(1);
		}

		for ( auto object : model.objects) {
			for (auto volume : object->volumes) {
				auto new_volume = model_object->add_volume(*volume);
				new_volume->modifier = is_modifier;
				boost::filesystem::path(input_file).filename().string();
				new_volume->name = boost::filesystem::path(input_file).filename().string();

				part_names.Add(new_volume->name);

				// apply the same translation we applied to the object
				new_volume->mesh.translate( model_object->origin_translation.x,
											model_object->origin_translation.y, 
											model_object->origin_translation.y );
				// set a default extruder value, since user can't add it manually
				new_volume->config.set_key_value("extruder", new ConfigOptionInt(0));

				m_parts_changed = true;
			}
		}
	}
}

void load_lambda(	wxWindow* parent, ModelObject* model_object,
					wxArrayString& part_names, const bool is_modifier)
{
	auto dlg = new LambdaObjectDialog(parent);
	if (dlg->ShowModal() == wxID_CANCEL) {
		return;
	}

	std::string name = "lambda-";
	TriangleMesh mesh;

	auto params = dlg->ObjectParameters();
	switch (params.type)
	{
	case LambdaTypeBox:{
		mesh = make_cube(params.dim[0], params.dim[1], params.dim[2]);
		name += "Box";
		break;}
	case LambdaTypeCylinder:{
		mesh = make_cylinder(params.cyl_r, params.cyl_h);
		name += "Cylinder";
		break;}
	case LambdaTypeSphere:{
		mesh = make_sphere(params.sph_rho);
		name += "Sphere";
		break;}
	case LambdaTypeSlab:{
		const auto& size = model_object->bounding_box().size();
		mesh = make_cube(size.x*1.5, size.y*1.5, params.slab_h);
		// box sets the base coordinate at 0, 0, move to center of plate and move it up to initial_z
		mesh.translate(-size.x*1.5 / 2.0, -size.y*1.5 / 2.0, params.slab_z);
		name += "Slab";
		break; }
	default:
		break;
	}
	mesh.repair();

	auto new_volume = model_object->add_volume(mesh);
	new_volume->modifier = is_modifier;
	new_volume->name = name;
	// set a default extruder value, since user can't add it manually
	new_volume->config.set_key_value("extruder", new ConfigOptionInt(0));

	part_names.Add(name);

	m_parts_changed = true;
}

void on_btn_load(wxWindow* parent, bool is_modifier /*= false*/, bool is_lambda/* = false*/)
{
	auto objects_ctrl = get_objects_ctrl();
	auto item = objects_ctrl->GetSelection();
	if (!item)
		return;
	int obj_idx = -1;
	auto objects_model = get_objects_model();
	if (objects_model->GetParent(item) == wxDataViewItem(0))
		obj_idx = objects_model->GetIdByItem(item);
	else
		return;

	if (obj_idx < 0) return;
	wxArrayString part_names;
	ModelObjectPtrs& objects = get_objects();
	if (is_lambda)
		load_lambda(parent, objects[obj_idx], part_names, is_modifier);
	else
		load_part(parent, objects[obj_idx], part_names, is_modifier);

	parts_changed(obj_idx);

	const std::string icon_name = is_modifier ? "plugin.png" : "package.png";
	auto icon = wxIcon(Slic3r::GUI::from_u8(Slic3r::var(icon_name)), wxBITMAP_TYPE_PNG);
	for (int i = 0; i < part_names.size(); ++i)
		objects_ctrl->Select(objects_model->AddChild(item, part_names.Item(i), icon));
}

void parts_changed(int obj_idx)
{ 
	auto event = get_event_object_settings_changed();
	if (event <= 0)
		return;

	wxCommandEvent e(event);
	auto event_str = wxString::Format("%d %d %d", obj_idx,
		is_parts_changed() ? 1 : 0,
		is_part_settings_changed() ? 1 : 0);
	e.SetString(event_str);
	get_main_frame()->ProcessWindowEvent(e);
}

} //namespace GUI
} //namespace Slic3r 