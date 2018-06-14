#include "GUI.hpp"
#include "GUI_ObjectParts.hpp"
#include "Model.hpp"
#include "wxExtensions.hpp"

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

void load_part(wxWindow* parent, ModelObject* model_object, wxArrayString& part_names, bool is_modifier)
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

	parts_changed();
}

void on_btn_load(wxWindow* parent, bool is_modifier /*= false*/)
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
	load_part(parent, objects[obj_idx], part_names, is_modifier);

	auto event = get_event_object_settings_changed();
	if (event > 0) {
		wxCommandEvent e(event);
		auto event_str = wxString::Format("%d %d %d", obj_idx,
			is_parts_changed() ? 1 : 0,
			is_part_settings_changed() ? 1 : 0);
		e.SetString(event_str);
		get_main_frame()->ProcessWindowEvent(e);
	}

	for (int i = 0; i < part_names.size(); ++i)
		objects_ctrl->Select(objects_model->AddChild(item, part_names.Item(i)));
}

void parts_changed(){ ; }

} //namespace GUI
} //namespace Slic3r 