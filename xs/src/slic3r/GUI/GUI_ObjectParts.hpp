#ifndef slic3r_GUI_ObjectParts_hpp_
#define slic3r_GUI_ObjectParts_hpp_

namespace Slic3r
{
namespace GUI
{
bool is_parts_changed();
bool is_part_settings_changed();

void load_part(	wxWindow* parent, ModelObject* model_object, 
				wxArrayString& part_names, bool is_modifier); 
void on_btn_load(wxWindow* parent, bool is_modifier = false);
void parts_changed();		
} //namespace GUI
} //namespace Slic3r 
#endif  //slic3r_GUI_ObjectParts_hpp_