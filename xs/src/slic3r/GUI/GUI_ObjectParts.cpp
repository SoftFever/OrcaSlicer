#include "GUI.hpp"
#include "OptionsGroup.hpp"
#include "PresetBundle.hpp"
#include "GUI_ObjectParts.hpp"
#include "Model.hpp"
#include "wxExtensions.hpp"
#include "LambdaObjectDialog.hpp"
#include "../../libslic3r/Utils.hpp"

#include <wx/msgdlg.h>
#include <wx/frame.h>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>

namespace Slic3r
{
namespace GUI
{
wxSizer		*m_sizer_object_buttons = nullptr;
wxSizer		*m_sizer_part_buttons = nullptr;
wxSizer		*m_sizer_object_movers = nullptr;
wxDataViewCtrl				*m_objects_ctrl = nullptr;
PrusaObjectDataViewModel	*m_objects_model = nullptr;
wxCollapsiblePane			*m_collpane_settings = nullptr;

wxSlider*		mover_x = nullptr;
wxSlider*		mover_y = nullptr;
wxSlider*		mover_z = nullptr;

bool		g_prevent_list_events = false;		// We use this flag to avoid circular event handling Select() 
												// happens to fire a wxEVT_LIST_ITEM_SELECTED on OSX, whose event handler 
												// calls this method again and again and again
ModelObjectPtrs				m_objects;

int			m_event_object_selection_changed = 0;
int			m_event_object_settings_changed = 0;

bool m_parts_changed = false;
bool m_part_settings_changed = false;

void set_event_object_selection_changed(const int& event){
	m_event_object_selection_changed = event;
}
void set_event_object_settings_changed(const int& event){
	m_event_object_settings_changed = event;
}

bool is_parts_changed(){return m_parts_changed;}
bool is_part_settings_changed(){ return m_part_settings_changed; }

static wxString dots("…", wxConvUTF8);

// ****** from GUI.cpp
wxBoxSizer* content_objects_list(wxWindow *win)
{
	m_objects_ctrl = new wxDataViewCtrl(win, wxID_ANY, wxDefaultPosition, wxDefaultSize);
	m_objects_ctrl->SetInitialSize(wxSize(-1, 150)); // TODO - Set correct height according to the opened/closed objects

	auto objects_sz = new wxBoxSizer(wxVERTICAL);
	objects_sz->Add(m_objects_ctrl, 1, wxGROW | wxLEFT, 20);

	m_objects_model = new PrusaObjectDataViewModel;
	m_objects_ctrl->AssociateModel(m_objects_model);
#if wxUSE_DRAG_AND_DROP && wxUSE_UNICODE
	m_objects_ctrl->EnableDragSource(wxDF_UNICODETEXT);
	m_objects_ctrl->EnableDropTarget(wxDF_UNICODETEXT);
#endif // wxUSE_DRAG_AND_DROP && wxUSE_UNICODE

	// column 0(Icon+Text) of the view control:
	m_objects_ctrl->AppendIconTextColumn(_(L("Name")), 0, wxDATAVIEW_CELL_INERT, 150,
		wxALIGN_LEFT, wxDATAVIEW_COL_SORTABLE | wxDATAVIEW_COL_RESIZABLE);

	// column 1 of the view control:
	m_objects_ctrl->AppendTextColumn(_(L("Copy")), 1, wxDATAVIEW_CELL_INERT, 65,
		wxALIGN_CENTER_HORIZONTAL, wxDATAVIEW_COL_SORTABLE | wxDATAVIEW_COL_RESIZABLE);

	// column 2 of the view control:
	m_objects_ctrl->AppendTextColumn(_(L("Scale")), 2, wxDATAVIEW_CELL_INERT, 70,
		wxALIGN_CENTER_HORIZONTAL, wxDATAVIEW_COL_SORTABLE | wxDATAVIEW_COL_RESIZABLE);

	m_objects_ctrl->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [](wxEvent& event)
	{
		if (g_prevent_list_events) return;

		wxWindowUpdateLocker noUpdates(get_right_panel());
		auto item = m_objects_ctrl->GetSelection();
		int obj_idx = -1;
		if (!item)
			unselect_objects();
		else
		{
			if (m_objects_model->GetParent(item) == wxDataViewItem(0))
				obj_idx = m_objects_model->GetIdByItem(item);
			else {
				auto parent = m_objects_model->GetParent(item);
				obj_idx = m_objects_model->GetIdByItem(parent); // TODO Temporary decision for sub-objects selection
			}
		}

		if (m_event_object_selection_changed > 0) {
			wxCommandEvent event(m_event_object_selection_changed);
			event.SetInt(int(m_objects_model->GetParent(item) != wxDataViewItem(0)));
			event.SetId(obj_idx);
			get_main_frame()->ProcessWindowEvent(event);
		}

		if (obj_idx < 0) return;

//		m_objects_ctrl->SetSize(m_objects_ctrl->GetBestSize()); // TODO override GetBestSize(), than use it

		auto show_obj_sizer = m_objects_model->GetParent(item) == wxDataViewItem(0);
		m_sizer_object_buttons->Show(show_obj_sizer);
		m_sizer_part_buttons->Show(!show_obj_sizer);
		m_sizer_object_movers->Show(!show_obj_sizer);

		if (!show_obj_sizer)
		{
			auto bb_size = m_objects[obj_idx]->bounding_box().size();
			int scale = 10; //??

			mover_x->SetMin(-bb_size.x * 4*scale);
			mover_x->SetMax( bb_size.x * 4*scale);

			mover_y->SetMin(-bb_size.y * 4*scale);
			mover_y->SetMax( bb_size.y * 4*scale);

			mover_z->SetMin(-bb_size.z * 4 * scale);
			mover_z->SetMax( bb_size.z * 4 * scale);
		}

		m_collpane_settings->SetLabelText((show_obj_sizer ? _(L("Object Settings")) : _(L("Part Settings"))) + ":");
		m_collpane_settings->Show(true);
	});

	m_objects_ctrl->Bind(wxEVT_KEY_DOWN, [](wxKeyEvent& event)
	{
		if (event.GetKeyCode() == WXK_TAB)
			m_objects_ctrl->Navigate(event.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward);
		else
			event.Skip();
	});

	return objects_sz;
}

wxBoxSizer* content_edit_object_buttons(wxWindow* win)
{
	auto sizer = new wxBoxSizer(wxVERTICAL);

	auto btn_load_part = new wxButton(win, wxID_ANY, /*Load */"part" + dots, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT | wxNO_BORDER/*wxBU_LEFT*/);
	auto btn_load_modifier = new wxButton(win, wxID_ANY, /*Load */"modifier" + dots, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT | wxNO_BORDER/*wxBU_LEFT*/);
	auto btn_load_lambda_modifier = new wxButton(win, wxID_ANY, /*Load */"generic" + dots, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT | wxNO_BORDER/*wxBU_LEFT*/);
	auto btn_delete = new wxButton(win, wxID_ANY, "Delete"/*" part"*/, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT | wxNO_BORDER/*wxBU_LEFT*/);
	auto btn_split = new wxButton(win, wxID_ANY, "Split"/*" part"*/, wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT | wxNO_BORDER/*wxBU_LEFT*/);
	auto btn_move_up = new wxButton(win, wxID_ANY, "", wxDefaultPosition, wxDefaultSize/*wxSize(30, -1)*/, wxBU_LEFT);
	auto btn_move_down = new wxButton(win, wxID_ANY, "", wxDefaultPosition, wxDefaultSize/*wxSize(30, -1)*/, wxBU_LEFT);

	//*** button's functions
	btn_load_part->Bind(wxEVT_BUTTON, [win](wxEvent&)
	{
		on_btn_load(win);
	});

	btn_load_modifier->Bind(wxEVT_BUTTON, [win](wxEvent&)
	{
		on_btn_load(win, true);
	});

	btn_load_lambda_modifier->Bind(wxEVT_BUTTON, [win](wxEvent&)
	{
		on_btn_load(win, true, true);
	});

	btn_delete->Bind(wxEVT_BUTTON, [](wxEvent&)
	{
		auto item = m_objects_ctrl->GetSelection();
		if (!item) return;
		m_objects_ctrl->Select(m_objects_model->Delete(item));
	});
	//***

	btn_move_up->SetMinSize(wxSize(20, -1));
	btn_move_down->SetMinSize(wxSize(20, -1));
	btn_load_part->SetBitmap(wxBitmap(from_u8(Slic3r::var("brick_add.png")), wxBITMAP_TYPE_PNG));
	btn_load_modifier->SetBitmap(wxBitmap(from_u8(Slic3r::var("brick_add.png")), wxBITMAP_TYPE_PNG));
	btn_load_lambda_modifier->SetBitmap(wxBitmap(from_u8(Slic3r::var("brick_add.png")), wxBITMAP_TYPE_PNG));
	btn_delete->SetBitmap(wxBitmap(from_u8(Slic3r::var("brick_delete.png")), wxBITMAP_TYPE_PNG));
	btn_split->SetBitmap(wxBitmap(from_u8(Slic3r::var("shape_ungroup.png")), wxBITMAP_TYPE_PNG));
	btn_move_up->SetBitmap(wxBitmap(from_u8(Slic3r::var("bullet_arrow_up.png")), wxBITMAP_TYPE_PNG));
	btn_move_down->SetBitmap(wxBitmap(from_u8(Slic3r::var("bullet_arrow_down.png")), wxBITMAP_TYPE_PNG));

	m_sizer_object_buttons = new wxGridSizer(1, 3, 0, 0);
	m_sizer_object_buttons->Add(btn_load_part, 0, wxEXPAND);
	m_sizer_object_buttons->Add(btn_load_modifier, 0, wxEXPAND);
	m_sizer_object_buttons->Add(btn_load_lambda_modifier, 0, wxEXPAND);
	m_sizer_object_buttons->Show(false);

	m_sizer_part_buttons = new wxGridSizer(1, 3, 0, 0);
	m_sizer_part_buttons->Add(btn_delete, 0, wxEXPAND);
	m_sizer_part_buttons->Add(btn_split, 0, wxEXPAND);
	{
		auto up_down_sizer = new wxGridSizer(1, 2, 0, 0);
		up_down_sizer->Add(btn_move_up, 1, wxEXPAND);
		up_down_sizer->Add(btn_move_down, 1, wxEXPAND);
		m_sizer_part_buttons->Add(up_down_sizer, 0, wxEXPAND);
	}
	m_sizer_part_buttons->Show(false);

	btn_load_part->SetFont(Slic3r::GUI::small_font());
	btn_load_modifier->SetFont(Slic3r::GUI::small_font());
	btn_load_lambda_modifier->SetFont(Slic3r::GUI::small_font());
	btn_delete->SetFont(Slic3r::GUI::small_font());
	btn_split->SetFont(Slic3r::GUI::small_font());
	btn_move_up->SetFont(Slic3r::GUI::small_font());
	btn_move_down->SetFont(Slic3r::GUI::small_font());

	sizer->Add(m_sizer_object_buttons, 0, wxEXPAND | wxLEFT, 20);
	sizer->Add(m_sizer_part_buttons, 0, wxEXPAND | wxLEFT, 20);
	return sizer;
}

wxSizer* object_movers(wxWindow *win)
{
// 	DynamicPrintConfig* config = &get_preset_bundle()->/*full_config();//*/printers.get_edited_preset().config; // TODO get config from Model_volume
	std::shared_ptr<ConfigOptionsGroup> optgroup = std::make_shared<ConfigOptionsGroup>(win, "Move"/*, config*/);
	optgroup->label_width = 20;

	ConfigOptionDef def;
	def.label = L("X");
	def.type = coInt;
	def.gui_type = "slider";
	def.default_value = new ConfigOptionInt(0);

	Option option = Option(def, "x");
	option.opt.full_width = true;
	optgroup->append_single_option_line(option);
	mover_x = dynamic_cast<wxSlider*>(optgroup->get_field("x")->getWindow());

	def.label = L("Y");
	option = Option(def, "y");
	optgroup->append_single_option_line(option);
	mover_y = dynamic_cast<wxSlider*>(optgroup->get_field("y")->getWindow());

	def.label = L("Z");
	option = Option(def, "z");
	optgroup->append_single_option_line(option);
	mover_z = dynamic_cast<wxSlider*>(optgroup->get_field("z")->getWindow());

	get_optgroups().push_back(optgroup);  // ogObjectMovers
	m_sizer_object_movers = optgroup->sizer;
	m_sizer_object_movers->Show(false);
	return optgroup->sizer;
}

wxBoxSizer* content_settings(wxWindow *win)
{
	DynamicPrintConfig* config = &get_preset_bundle()->/*full_config();//*/printers.get_edited_preset().config; // TODO get config from Model_volume
	std::shared_ptr<ConfigOptionsGroup> optgroup = std::make_shared<ConfigOptionsGroup>(win, "Extruders", config);
	optgroup->label_width = label_width();

	Option option = optgroup->get_option("extruder");
	option.opt.default_value = new ConfigOptionInt(1);
	optgroup->append_single_option_line(option);

	get_optgroups().push_back(optgroup);  // ogObjectSettings

	auto sizer = new wxBoxSizer(wxVERTICAL);
	sizer->Add(content_edit_object_buttons(win), 0, wxEXPAND, 0); // *** Edit Object Buttons***

	sizer->Add(optgroup->sizer, 1, wxEXPAND | wxLEFT, 20);

	auto add_btn = new wxButton(win, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxBU_EXACTFIT | wxNO_BORDER);
	if (wxMSW) add_btn->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
	add_btn->SetBitmap(wxBitmap(from_u8(Slic3r::var("add.png")), wxBITMAP_TYPE_PNG));
	sizer->Add(add_btn, 0, wxALIGN_LEFT | wxLEFT, 20);

	sizer->Add(object_movers(win), 0, wxEXPAND | wxLEFT, 20);

	return sizer;
}


// add Collapsible Pane to sizer
wxCollapsiblePane* add_collapsible_pane(wxWindow* parent, wxBoxSizer* sizer_parent, const wxString& name, std::function<wxSizer *(wxWindow *)> content_function)
{
#ifdef __WXMSW__
	auto *collpane = new PrusaCollapsiblePaneMSW(parent, wxID_ANY, name);
#else
	auto *collpane = new PrusaCollapsiblePane/*wxCollapsiblePane*/(parent, wxID_ANY, name);
#endif // __WXMSW__
	// add the pane with a zero proportion value to the sizer which contains it
	sizer_parent->Add(collpane, 0, wxGROW | wxALL, 0);

	wxWindow *win = collpane->GetPane();

	wxSizer *sizer = content_function(win);

	wxSizer *sizer_pane = new wxBoxSizer(wxVERTICAL);
	sizer_pane->Add(sizer, 1, wxGROW | wxEXPAND | wxBOTTOM, 2);
	win->SetSizer(sizer_pane);
	// 	sizer_pane->SetSizeHints(win);
	return collpane;
}

void add_collapsible_panes(wxWindow* parent, wxBoxSizer* sizer)
{
	// *** Objects List ***	
	auto collpane = add_collapsible_pane(parent, sizer, "Objects List:", content_objects_list);
	collpane->Bind(wxEVT_COLLAPSIBLEPANE_CHANGED, ([collpane](wxCommandEvent& e){
		// 		wxWindowUpdateLocker noUpdates(g_right_panel);
		if (collpane->IsCollapsed()) {
			m_sizer_object_buttons->Show(false);
			m_sizer_part_buttons->Show(false);
			m_sizer_object_movers->Show(false);
			m_collpane_settings->Show(false);
		}
// 		else 
// 			m_objects_ctrl->UnselectAll();

// 		e.Skip();
//		g_right_panel->Layout();
	}));

	// *** Object/Part Settings ***
	m_collpane_settings = add_collapsible_pane(parent, sizer, "Object Settings", content_settings);
}

void show_collpane_settings(bool expert_mode)
{
	m_collpane_settings->Show(expert_mode && !m_objects_model->IsEmpty());
}

void add_object_to_list(const std::string &name, ModelObject* model_object)
{
	wxString item = name;
	int scale = model_object->instances[0]->scaling_factor * 100;
	m_objects_ctrl->Select(m_objects_model->Add(item, model_object->instances.size(), scale));
	m_objects.push_back(model_object);
}

void delete_object_from_list()
{
	auto item = m_objects_ctrl->GetSelection();
	if (!item || m_objects_model->GetParent(item) != wxDataViewItem(0))
		return;
// 	m_objects_ctrl->Select(m_objects_model->Delete(item));
	m_objects_model->Delete(item);

	if (m_objects_model->IsEmpty())
		m_collpane_settings->Show(false);
}

void delete_all_objects_from_list()
{
	m_objects_model->DeleteAll();
	m_collpane_settings->Show(false);
}

void set_object_count(int idx, int count)
{
	m_objects_model->SetValue(wxString::Format("%d", count), idx, 1);
	m_objects_ctrl->Refresh();
}

void set_object_scale(int idx, int scale)
{
	m_objects_model->SetValue(wxString::Format("%d%%", scale), idx, 2);
	m_objects_ctrl->Refresh();
}

void unselect_objects()
{
	m_objects_ctrl->UnselectAll();
	if (m_sizer_object_buttons->IsShown(1))
		m_sizer_object_buttons->Show(false);
	if (m_sizer_part_buttons->IsShown(1))
		m_sizer_part_buttons->Show(false);
	if (m_sizer_object_movers->IsShown(1))
		m_sizer_object_movers->Show(false);
	if (m_collpane_settings->IsShown())
		m_collpane_settings->Show(false);
}

void select_current_object(int idx)
{
	g_prevent_list_events = true;
	m_objects_ctrl->UnselectAll();
	if (idx < 0) {
		g_prevent_list_events = false;
		return;
	}
	m_objects_ctrl->Select(m_objects_model->GetItemById(idx));
	g_prevent_list_events = false;

	if (is_expert_mode()){
		if (!m_sizer_object_buttons->IsShown(1))
			m_sizer_object_buttons->Show(true);
		if (!m_sizer_object_movers->IsShown(1))
			m_sizer_object_movers->Show(true);
		if (!m_collpane_settings->IsShown())
			m_collpane_settings->Show(true);
	}
}
// ******

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
	auto item = m_objects_ctrl->GetSelection();
	if (!item)
		return;
	int obj_idx = -1;
	if (m_objects_model->GetParent(item) == wxDataViewItem(0))
		obj_idx = m_objects_model->GetIdByItem(item);
	else
		return;

	if (obj_idx < 0) return;
	wxArrayString part_names;
	if (is_lambda)
		load_lambda(parent, m_objects[obj_idx], part_names, is_modifier);
	else
		load_part(parent, m_objects[obj_idx], part_names, is_modifier);

	parts_changed(obj_idx);

	const std::string icon_name = is_modifier ? "plugin.png" : "package.png";
	auto icon = wxIcon(Slic3r::GUI::from_u8(Slic3r::var(icon_name)), wxBITMAP_TYPE_PNG);
	for (int i = 0; i < part_names.size(); ++i)
		m_objects_ctrl->Select(m_objects_model->AddChild(item, part_names.Item(i), icon));
}

void parts_changed(int obj_idx)
{ 
	if (m_event_object_settings_changed <= 0) return;

	wxCommandEvent e(m_event_object_settings_changed);
	auto event_str = wxString::Format("%d %d %d", obj_idx,
		is_parts_changed() ? 1 : 0,
		is_part_settings_changed() ? 1 : 0);
	e.SetString(event_str);
	get_main_frame()->ProcessWindowEvent(e);
}

} //namespace GUI
} //namespace Slic3r 