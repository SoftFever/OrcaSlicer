#include "GUI.hpp"
#include "GUI_ObjectParts.hpp"

#include <wx/msgdlg.h>

namespace Slic3r
{
namespace GUI
{
void on_btn_load(wxWindow* parent, bool is_modifier /*= false*/)
{
	auto input_files = open_model(parent);
// 	for(auto input_file : input_files) {
// 		my $model = eval{ Slic3r::Model->read_from_file($input_file) };
// 		if ($@) {
// 			Slic3r::GUI::show_error($self, $@);
// 			next;
// 		}

// 		foreach my $object(@{$model->objects}) {
// 			foreach my $volume(@{$object->volumes}) {
// 				my $new_volume = $self->{model_object}->add_volume($volume);
// 				$new_volume->set_modifier($is_modifier);
// 				$new_volume->set_name(basename($input_file));
// 
// 				# apply the same translation we applied to the object
// 				$new_volume->mesh->translate(@{$self->{model_object}->origin_translation});
// 
// 				# set a default extruder value, since user can't add it manually
// 				$new_volume->config->set_ifndef('extruder', 0);
// 
// 				$self->{parts_changed} = 1;
// 			}
// 		}
// 	}

	parts_changed();
}

void parts_changed(){ ; }

} //namespace GUI
} //namespace Slic3r 