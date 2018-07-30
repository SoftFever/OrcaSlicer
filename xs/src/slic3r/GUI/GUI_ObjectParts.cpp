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
#include "Geometry.hpp"

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

wxIcon		m_icon_modifiermesh;
wxIcon		m_icon_solidmesh;
wxIcon		m_icon_manifold_warning;
wxBitmap	m_bmp_cog;
wxBitmap	m_bmp_split;

wxSlider*	m_mover_x = nullptr;
wxSlider*	m_mover_y = nullptr;
wxSlider*	m_mover_z = nullptr;
wxButton*	m_btn_move_up = nullptr;
wxButton*	m_btn_move_down = nullptr;
Point3		m_move_options;
Point3		m_last_coords;
int			m_selected_object_id = -1;

bool		g_prevent_list_events = false;		// We use this flag to avoid circular event handling Select() 
												// happens to fire a wxEVT_LIST_ITEM_SELECTED on OSX, whose event handler 
												// calls this method again and again and again
bool        g_is_percent_scale = false;         // It indicates if scale unit is percentage
int         g_rotation_x = 0;                   // Last value of the rotation around the X axis
int         g_rotation_y = 0;                   // Last value of the rotation around the Y axis
ModelObjectPtrs*			m_objects;
std::shared_ptr<DynamicPrintConfig*> m_config;
std::shared_ptr<DynamicPrintConfig> m_default_config;
wxBoxSizer*					m_option_sizer = nullptr;

// option groups for settings
std::vector <std::shared_ptr<ConfigOptionsGroup>> m_og_settings;

int			m_event_object_selection_changed = 0;
int			m_event_object_settings_changed = 0;
int			m_event_remove_object = 0;
int			m_event_update_scene = 0;

bool m_parts_changed = false;
bool m_part_settings_changed = false;

#ifdef __WXOSX__
    wxString g_selected_extruder = "";
#endif //__WXOSX__

// typedef std::map<std::string, std::string> t_category_icon;
typedef std::map<std::string, wxBitmap> t_category_icon;
inline t_category_icon& get_category_icon() {
	static t_category_icon CATEGORY_ICON;
	if (CATEGORY_ICON.empty()){
		CATEGORY_ICON[L("Layers and Perimeters")]	= wxBitmap(from_u8(Slic3r::var("layers.png")), wxBITMAP_TYPE_PNG);
		CATEGORY_ICON[L("Infill")]					= wxBitmap(from_u8(Slic3r::var("infill.png")), wxBITMAP_TYPE_PNG);
		CATEGORY_ICON[L("Support material")]		= wxBitmap(from_u8(Slic3r::var("building.png")), wxBITMAP_TYPE_PNG);
		CATEGORY_ICON[L("Speed")]					= wxBitmap(from_u8(Slic3r::var("time.png")), wxBITMAP_TYPE_PNG);
		CATEGORY_ICON[L("Extruders")]				= wxBitmap(from_u8(Slic3r::var("funnel.png")), wxBITMAP_TYPE_PNG);
		CATEGORY_ICON[L("Extrusion Width")]			= wxBitmap(from_u8(Slic3r::var("funnel.png")), wxBITMAP_TYPE_PNG);
// 		CATEGORY_ICON[L("Skirt and brim")]			= wxBitmap(from_u8(Slic3r::var("box.png")), wxBITMAP_TYPE_PNG);
// 		CATEGORY_ICON[L("Speed > Acceleration")]	= wxBitmap(from_u8(Slic3r::var("time.png")), wxBITMAP_TYPE_PNG);
		CATEGORY_ICON[L("Advanced")]				= wxBitmap(from_u8(Slic3r::var("wand.png")), wxBITMAP_TYPE_PNG);
	}
	return CATEGORY_ICON;
}

std::vector<std::string> get_options(const bool is_part)
{
	PrintRegionConfig reg_config;
	auto options = reg_config.keys();
	if (!is_part) {
		PrintObjectConfig obj_config;
		std::vector<std::string> obj_options = obj_config.keys();
		options.insert(options.end(), obj_options.begin(), obj_options.end());
	}
	return options;
}

//				  category ->		vector 			 ( option	;  label )
typedef std::map< std::string, std::vector< std::pair<std::string, std::string> > > settings_menu_hierarchy;
void get_options_menu(settings_menu_hierarchy& settings_menu, bool is_part)
{
	auto options = get_options(is_part);

	DynamicPrintConfig config;
	for (auto& option : options)
	{
		auto const opt = config.def()->get(option);
		auto category = opt->category;
		if (category.empty()) continue;

		std::pair<std::string, std::string> option_label(option, opt->label);
		std::vector< std::pair<std::string, std::string> > new_category;
		auto& cat_opt_label = settings_menu.find(category) == settings_menu.end() ? new_category : settings_menu.at(category);
		cat_opt_label.push_back(option_label);
		if (cat_opt_label.size() == 1)
			settings_menu[category] = cat_opt_label;
	}
}

void set_event_object_selection_changed(const int& event){
	m_event_object_selection_changed = event;
}
void set_event_object_settings_changed(const int& event){
	m_event_object_settings_changed = event;
}
void set_event_remove_object(const int& event){
	m_event_remove_object = event;
}
void set_event_update_scene(const int& event){
	m_event_update_scene = event;
}

void set_objects_from_model(Model &model) {
	m_objects = &(model.objects);
}

void init_mesh_icons(){
	m_icon_modifiermesh = wxIcon(Slic3r::GUI::from_u8(Slic3r::var("lambda_.png")), wxBITMAP_TYPE_PNG);//(Slic3r::var("plugin.png")), wxBITMAP_TYPE_PNG);
	m_icon_solidmesh = wxIcon(Slic3r::GUI::from_u8(Slic3r::var("object.png")), wxBITMAP_TYPE_PNG);//(Slic3r::var("package.png")), wxBITMAP_TYPE_PNG);

	// init icon for manifold warning
	m_icon_manifold_warning = wxIcon(Slic3r::GUI::from_u8(Slic3r::var("exclamation_mark_.png")), wxBITMAP_TYPE_PNG);//(Slic3r::var("error.png")), wxBITMAP_TYPE_PNG);

	// init bitmap for "Split to sub-objects" context menu
    m_bmp_split = wxBitmap(Slic3r::GUI::from_u8(Slic3r::var("split.png")), wxBITMAP_TYPE_PNG);

	// init bitmap for "Add Settings" context menu
	m_bmp_cog = wxBitmap(Slic3r::GUI::from_u8(Slic3r::var("cog.png")), wxBITMAP_TYPE_PNG);
}

bool is_parts_changed(){return m_parts_changed;}
bool is_part_settings_changed(){ return m_part_settings_changed; }

static wxString dots("â€¦", wxConvUTF8);

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
	m_objects_ctrl->AppendIconTextColumn(_(L("Name")), 0, wxDATAVIEW_CELL_INERT, 120,
		wxALIGN_LEFT, /*wxDATAVIEW_COL_SORTABLE | */wxDATAVIEW_COL_RESIZABLE);

	// column 1 of the view control:
	m_objects_ctrl->AppendTextColumn(_(L("Copy")), 1, wxDATAVIEW_CELL_INERT, 45,
		wxALIGN_CENTER_HORIZONTAL, wxDATAVIEW_COL_RESIZABLE);

	// column 2 of the view control:
	m_objects_ctrl->AppendTextColumn(_(L("Scale")), 2, wxDATAVIEW_CELL_INERT, 55,
		wxALIGN_CENTER_HORIZONTAL, wxDATAVIEW_COL_RESIZABLE);

	// column 3 of the view control:
	wxArrayString choices;
	choices.Add("default");
	choices.Add("1");
	choices.Add("2");
	choices.Add("3");
	choices.Add("4");
	wxDataViewChoiceRenderer *c =
		new wxDataViewChoiceRenderer(choices, wxDATAVIEW_CELL_EDITABLE, wxALIGN_CENTER_HORIZONTAL);
	wxDataViewColumn *column3 =
		new wxDataViewColumn(_(L("Extruder")), c, 3, 60, wxALIGN_CENTER_HORIZONTAL, wxDATAVIEW_COL_RESIZABLE);
	m_objects_ctrl->AppendColumn(column3);

	// column 4 of the view control:
	m_objects_ctrl->AppendBitmapColumn("", 4, wxDATAVIEW_CELL_INERT, 25,
		wxALIGN_CENTER_HORIZONTAL, wxDATAVIEW_COL_RESIZABLE);

	m_objects_ctrl->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [](wxEvent& event)
	{
		object_ctrl_selection_changed();
#ifdef __WXOSX__
        update_extruder_in_config(g_selected_extruder);
#endif //__WXOSX__        
	});

	m_objects_ctrl->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, [](wxEvent& event)
	{
		event.Skip();
		object_ctrl_context_menu();
		
	});

	m_objects_ctrl->Bind(wxEVT_CHAR, [](wxKeyEvent& event)
	{
		if (event.GetKeyCode() == WXK_TAB)
			m_objects_ctrl->Navigate(event.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward);
		else if (event.GetKeyCode() == WXK_DELETE
#ifdef __WXOSX__
			|| event.GetKeyCode() == WXK_BACK
#endif //__WXOSX__
			)
			remove();
		else 
			event.Skip();
	});

#ifdef __WXMSW__
	m_objects_ctrl->Bind(wxEVT_CHOICE, [](wxCommandEvent& event)
	{
        update_extruder_in_config(event.GetString());
	});
#else
    m_objects_ctrl->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, [](wxDataViewEvent& event)
    {
        if (event.GetColumn() == 3)
        {
        	wxVariant variant;
        	m_objects_model->GetValue(variant, event.GetItem(), 3);
#ifdef __WXOSX__
            g_selected_extruder = variant.GetString();
#else // --> for Linux
        	update_extruder_in_config(variant.GetString());
#endif //__WXOSX__  
        }
    });
#endif //__WXMSW__

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
	m_btn_move_up = new wxButton(win, wxID_ANY, "", wxDefaultPosition, wxDefaultSize/*wxSize(30, -1)*/, wxBU_LEFT);
	m_btn_move_down = new wxButton(win, wxID_ANY, "", wxDefaultPosition, wxDefaultSize/*wxSize(30, -1)*/, wxBU_LEFT);

	//*** button's functions
	btn_load_part->Bind(wxEVT_BUTTON, [win](wxEvent&) {
		on_btn_load(win);
	});

	btn_load_modifier->Bind(wxEVT_BUTTON, [win](wxEvent&) {
		on_btn_load(win, true);
	});

	btn_load_lambda_modifier->Bind(wxEVT_BUTTON, [win](wxEvent&) {
		on_btn_load(win, true, true);
	});

	btn_delete		->Bind(wxEVT_BUTTON, [](wxEvent&) { on_btn_del(); });
	btn_split		->Bind(wxEVT_BUTTON, [](wxEvent&) { on_btn_split(); });
	m_btn_move_up	->Bind(wxEVT_BUTTON, [](wxEvent&) { on_btn_move_up(); });
	m_btn_move_down	->Bind(wxEVT_BUTTON, [](wxEvent&) { on_btn_move_down(); });
	//***

	m_btn_move_up->SetMinSize(wxSize(20, -1));
	m_btn_move_down->SetMinSize(wxSize(20, -1));
	btn_load_part->SetBitmap(wxBitmap(from_u8(Slic3r::var("brick_add.png")), wxBITMAP_TYPE_PNG));
	btn_load_modifier->SetBitmap(wxBitmap(from_u8(Slic3r::var("brick_add.png")), wxBITMAP_TYPE_PNG));
	btn_load_lambda_modifier->SetBitmap(wxBitmap(from_u8(Slic3r::var("brick_add.png")), wxBITMAP_TYPE_PNG));
	btn_delete->SetBitmap(wxBitmap(from_u8(Slic3r::var("brick_delete.png")), wxBITMAP_TYPE_PNG));
	btn_split->SetBitmap(wxBitmap(from_u8(Slic3r::var("shape_ungroup.png")), wxBITMAP_TYPE_PNG));
	m_btn_move_up->SetBitmap(wxBitmap(from_u8(Slic3r::var("bullet_arrow_up.png")), wxBITMAP_TYPE_PNG));
	m_btn_move_down->SetBitmap(wxBitmap(from_u8(Slic3r::var("bullet_arrow_down.png")), wxBITMAP_TYPE_PNG));

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
		up_down_sizer->Add(m_btn_move_up, 1, wxEXPAND);
		up_down_sizer->Add(m_btn_move_down, 1, wxEXPAND);
		m_sizer_part_buttons->Add(up_down_sizer, 0, wxEXPAND);
	}
	m_sizer_part_buttons->Show(false);

	btn_load_part->SetFont(Slic3r::GUI::small_font());
	btn_load_modifier->SetFont(Slic3r::GUI::small_font());
	btn_load_lambda_modifier->SetFont(Slic3r::GUI::small_font());
	btn_delete->SetFont(Slic3r::GUI::small_font());
	btn_split->SetFont(Slic3r::GUI::small_font());
	m_btn_move_up->SetFont(Slic3r::GUI::small_font());
	m_btn_move_down->SetFont(Slic3r::GUI::small_font());

	sizer->Add(m_sizer_object_buttons, 0, wxEXPAND | wxLEFT, 20);
	sizer->Add(m_sizer_part_buttons, 0, wxEXPAND | wxLEFT, 20);
	return sizer;
}

void update_after_moving()
{
	auto item = m_objects_ctrl->GetSelection();
	if (!item || m_selected_object_id<0)
		return;

	auto volume_id = m_objects_model->GetVolumeIdByItem(item);
	if (volume_id < 0)
		return;

	Point3 m = m_move_options;
	Point3 l = m_last_coords;

	auto d = Pointf3(m.x - l.x, m.y - l.y, m.z - l.z);
	auto volume = (*m_objects)[m_selected_object_id]->volumes[volume_id];
	volume->mesh.translate(d.x,d.y,d.z);
	m_last_coords = m;

	m_parts_changed = true;
	parts_changed(m_selected_object_id);
}

wxSizer* object_movers(wxWindow *win)
{
// 	DynamicPrintConfig* config = &get_preset_bundle()->/*full_config();//*/printers.get_edited_preset().config; // TODO get config from Model_volume
	std::shared_ptr<ConfigOptionsGroup> optgroup = std::make_shared<ConfigOptionsGroup>(win, "Move"/*, config*/);
	optgroup->label_width = 20;
	optgroup->m_on_change = [](t_config_option_key opt_key, boost::any value){
		int val = boost::any_cast<int>(value);
		bool update = false;
		if (opt_key == "x" && m_move_options.x != val){
			update = true;
			m_move_options.x = val;
		}
		else if (opt_key == "y" && m_move_options.y != val){
			update = true;
			m_move_options.y = val;
		}
		else if (opt_key == "z" && m_move_options.z != val){
			update = true;
			m_move_options.z = val;
		}
		if (update) update_after_moving();
	};

	ConfigOptionDef def;
	def.label = L("X");
	def.type = coInt;
	def.gui_type = "slider";
	def.default_value = new ConfigOptionInt(0);

	Option option = Option(def, "x");
	option.opt.full_width = true;
	optgroup->append_single_option_line(option);
	m_mover_x = dynamic_cast<wxSlider*>(optgroup->get_field("x")->getWindow());

	def.label = L("Y");
	option = Option(def, "y");
	optgroup->append_single_option_line(option);
	m_mover_y = dynamic_cast<wxSlider*>(optgroup->get_field("y")->getWindow());

	def.label = L("Z");
	option = Option(def, "z");
	optgroup->append_single_option_line(option);
	m_mover_z = dynamic_cast<wxSlider*>(optgroup->get_field("z")->getWindow());

	get_optgroups().push_back(optgroup);  // ogObjectMovers

	m_sizer_object_movers = optgroup->sizer;
	m_sizer_object_movers->Show(false);

	m_move_options = Point3(0, 0, 0);
	m_last_coords = Point3(0, 0, 0);

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

void add_objects_list(wxWindow* parent, wxBoxSizer* sizer)
{
	const auto ol_sizer = content_objects_list(parent);
	sizer->Add(ol_sizer, 1, wxEXPAND | wxTOP, 20);
	set_objects_list_sizer(ol_sizer);
}

Line add_og_to_object_settings(const std::string& option_name, const std::string& sidetext, int def_value = 0)
{
	Line line = { _(option_name), "" };

	ConfigOptionDef def;
	def.type = coInt;
	def.default_value = new ConfigOptionInt(def_value);
	def.sidetext = sidetext;
	def.width = 70;

    if (option_name == "Rotation")
        def.min = -360;

	const std::string lower_name = boost::algorithm::to_lower_copy(option_name);

	std::vector<std::string> axes{ "x", "y", "z" };
	for (auto axis : axes) {
		def.label = boost::algorithm::to_upper_copy(axis);
		Option option = Option(def, lower_name + "_" + axis);
		option.opt.full_width = true;
		line.append_option(option);
	}

	if (option_name == "Scale")
	{
		def.label = L("Units");
		def.type = coStrings;
		def.gui_type = "select_open";
		def.enum_labels.push_back(L("%"));
		def.enum_labels.push_back(L("mm"));
		def.default_value = new ConfigOptionStrings{ "%" };
		def.sidetext = " ";

		Option option = Option(def, lower_name + "_unit");
		line.append_option(option);
	}

	return line;
}

void add_object_settings(wxWindow* parent, wxBoxSizer* sizer)
{
	auto optgroup = std::make_shared<ConfigOptionsGroup>(parent, _(L("Object Settings")));
	optgroup->label_width = 100;
	optgroup->set_grid_vgap(5);

	optgroup->m_on_change = [](t_config_option_key opt_key, boost::any value){
		if (opt_key == "scale_unit"){
			const wxString& selection = boost::any_cast<wxString>(value);
			std::vector<std::string> axes{ "x", "y", "z" };
			for (auto axis : axes) {
				std::string key = "scale_" + axis;
				get_optgroup(ogFrequentlyObjectSettings)->set_side_text(key, selection);
			}

            g_is_percent_scale = selection == _("%");
            update_scale_values();
		}
	};

// 	def.label = L("Name");
// 	def.type = coString;
// 	def.tooltip = L("Object name");
// 	def.full_width = true;
// 	def.default_value = new ConfigOptionString{ "BlaBla_object.stl" };
// 	optgroup->append_single_option_line(Option(def, "object_name"));

	ConfigOptionDef def;

	def.label = L("Name");
// 	def.type = coString;
    def.gui_type = "legend";
	def.tooltip = L("Object name");
	def.full_width = true;
	def.default_value = new ConfigOptionString{ " " };
	optgroup->append_single_option_line(Option(def, "object_name"));

	optgroup->set_flag(ogSIDE_OPTIONS_VERTICAL);
	optgroup->sidetext_width = 25;

	optgroup->append_line(add_og_to_object_settings(L("Position"), L("mm")));
	optgroup->append_line(add_og_to_object_settings(L("Rotation"), "°"));
	optgroup->append_line(add_og_to_object_settings(L("Scale"), "%"));

	optgroup->set_flag(ogDEFAULT);

	def.label = L("Place on bed");
	def.type = coBool;
	def.tooltip = L("Automatic placing of models on printing bed in Y axis");
	def.gui_type = "";
	def.sidetext = "";
	def.default_value = new ConfigOptionBool{ false };
	optgroup->append_single_option_line(Option(def, "place_on_bed"));

	m_option_sizer = new wxBoxSizer(wxVERTICAL);
	optgroup->sizer->Add(m_option_sizer, 1, wxEXPAND | wxLEFT, 5);

	sizer->Add(optgroup->sizer, 0, wxEXPAND | wxLEFT | wxTOP, 20);

	optgroup->disable();

	get_optgroups().push_back(optgroup);  // ogFrequentlyObjectSettings

// 	add_current_settings();
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
			if (!m_objects_ctrl->HasSelection())
				m_collpane_settings->Show(false);
		}
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
	wxString item_name = name;
	int scale = model_object->instances[0]->scaling_factor * 100;
	auto item = m_objects_model->Add(item_name, model_object->instances.size(), scale);
	m_objects_ctrl->Select(item);

	// Add error icon if detected auto-repaire
	auto stats = model_object->volumes[0]->mesh.stl.stats;
	int errors =	stats.degenerate_facets + stats.edges_fixed + stats.facets_removed + 
					stats.facets_added + stats.facets_reversed + stats.backwards_edges;
	if (errors > 0)		{
		const wxDataViewIconText data(item_name, m_icon_manifold_warning);
		wxVariant variant;
		variant << data;
		m_objects_model->SetValue(variant, item, 0);
	}

    if (model_object->volumes.size() > 1)
        for (auto id = 0; id < model_object->volumes.size(); id++)
            m_objects_model->AddChild(item, model_object->volumes[id]->name, m_icon_solidmesh, false);

	ModelObjectPtrs* objects = m_objects;
// 	part_selection_changed();
#ifdef __WXMSW__
	object_ctrl_selection_changed();
#endif //__WXMSW__
}

void delete_object_from_list()
{
	auto item = m_objects_ctrl->GetSelection();
	if (!item || m_objects_model->GetParent(item) != wxDataViewItem(0))
		return;
// 	m_objects_ctrl->Select(m_objects_model->Delete(item));
	m_objects_model->Delete(item);

// 	if (m_objects_model->IsEmpty())
// 		m_collpane_settings->Show(false);
}

void delete_all_objects_from_list()
{
	m_objects_model->DeleteAll();
// 	m_collpane_settings->Show(false);
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
    printf("UNSELECT OBJECTS\n");
	m_objects_ctrl->UnselectAll();
	part_selection_changed();

	get_optgroup(ogFrequentlyObjectSettings)->disable();
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
	part_selection_changed();
	g_prevent_list_events = false;

	get_optgroup(ogFrequentlyObjectSettings)->enable();
}

void remove()
{
	auto item = m_objects_ctrl->GetSelection();
	if (!item)
		return;
	
	if (m_objects_model->GetParent(item) == wxDataViewItem(0)) {
		if (m_event_remove_object > 0) {
			wxCommandEvent event(m_event_remove_object);
			get_main_frame()->ProcessWindowEvent(event);
		}
// 		delete_object_from_list();
	}
	else
		on_btn_del();
}

void object_ctrl_selection_changed()
{
	if (g_prevent_list_events) return;

	part_selection_changed();

// 	if (m_selected_object_id < 0) return;

	if (m_event_object_selection_changed > 0) {
		wxCommandEvent event(m_event_object_selection_changed);
		event.SetInt(int(m_objects_model->GetParent(/*item*/ m_objects_ctrl->GetSelection()) != wxDataViewItem(0)));
		event.SetId(m_selected_object_id);
		get_main_frame()->ProcessWindowEvent(event);
	}
}

//update_optgroup
void update_settings_list()
{
#ifdef __WXGTK__
    auto parent = get_optgroup(ogFrequentlyObjectSettings)->get_parent();
#else
    auto parent = get_optgroup(ogFrequentlyObjectSettings)->parent();
#endif /* __WXGTK__ */
    
// There is a bug related to Ubuntu overlay scrollbars, see https://github.com/prusa3d/Slic3r/issues/898 and https://github.com/prusa3d/Slic3r/issues/952.
// The issue apparently manifests when Show()ing a window with overlay scrollbars while the UI is frozen. For this reason,
// we will Thaw the UI prematurely on Linux. This means destroing the no_updates object prematurely.
#ifdef __linux__
	std::unique_ptr<wxWindowUpdateLocker> no_updates(new wxWindowUpdateLocker(parent));
#else
	wxWindowUpdateLocker noUpdates(parent);
#endif

	m_option_sizer->Clear(true);

    printf("update_settings_list\n");

	if (m_config) 
	{
		auto extra_column = [](wxWindow* parent, const Line& line)
		{
			auto opt_key = (line.get_options())[0].opt_id;  //we assume that we have one option per line

			auto btn = new wxBitmapButton(parent, wxID_ANY, wxBitmap(Slic3r::GUI::from_u8(Slic3r::var("erase.png")), wxBITMAP_TYPE_PNG),
				wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
			btn->Bind(wxEVT_BUTTON, [opt_key](wxEvent &event){
				(*m_config)->erase(opt_key);
				wxTheApp->CallAfter([]() { update_settings_list(); });
			});
			return btn;
		};

		std::map<std::string, std::vector<std::string>> cat_options;
		auto opt_keys = (*m_config)->keys();
		if (opt_keys.size() == 1 && opt_keys[0] == "extruder")
			return;

		for (auto& opt_key : opt_keys) {
			auto category = (*m_config)->def()->get(opt_key)->category;
			if (category.empty()) continue;

			std::vector< std::string > new_category;

			auto& cat_opt = cat_options.find(category) == cat_options.end() ? new_category : cat_options.at(category);
			cat_opt.push_back(opt_key);
			if (cat_opt.size() == 1)
				cat_options[category] = cat_opt;
		}


		m_og_settings.resize(0);
		for (auto& cat : cat_options) {
			if (cat.second.size() == 1 && cat.second[0] == "extruder")
				continue;

			auto optgroup = std::make_shared<ConfigOptionsGroup>(parent, cat.first, *m_config, false, ogDEFAULT, extra_column);
			optgroup->label_width = 100;
			optgroup->sidetext_width = 70;

			for (auto& opt : cat.second)
			{
				if (opt == "extruder")
					continue;
				Option option = optgroup->get_option(opt);
				option.opt.width = 70;
				optgroup->append_single_option_line(option);
			}
			optgroup->reload_config();
			m_option_sizer->Add(optgroup->sizer, 0, wxEXPAND | wxALL, 0);
			m_og_settings.push_back(optgroup);
		}
	}

#ifdef __linux__
	no_updates.reset(nullptr);
#endif

    get_right_panel()->Layout();
	get_right_panel()->GetParent()->Layout();
}

void get_settings_choice(wxMenu *menu, int id, bool is_part)
{
	auto category_name = menu->GetLabel(id);

	wxArrayString names;
	wxArrayInt selections;

	settings_menu_hierarchy settings_menu;
	get_options_menu(settings_menu, is_part);
	std::vector< std::pair<std::string, std::string> > *settings_list = nullptr;

	auto opt_keys = (*m_config)->keys();

	for (auto& cat : settings_menu)
	{
		if (_(cat.first) == category_name) {
			int sel = 0;
			for (auto& pair : cat.second) {
				names.Add(_(pair.second));
				if (find(opt_keys.begin(), opt_keys.end(), pair.first) != opt_keys.end())
					selections.Add(sel);
				sel++;
			}
			settings_list = &cat.second;
			break;
		}
	} 

	if (!settings_list)
		return;

	if (wxGetMultipleChoices(selections, _(L("Select showing settings")), category_name, names) ==0 )
		return;

	std::vector <std::string> selected_options;
	for (auto sel : selections)
		selected_options.push_back((*settings_list)[sel].first);

	for (auto& setting:(*settings_list) )
	{
		auto& opt_key = setting.first;
		if (find(opt_keys.begin(), opt_keys.end(), opt_key) != opt_keys.end() &&
			find(selected_options.begin(), selected_options.end(), opt_key) == selected_options.end())
			(*m_config)->erase(opt_key);
		
		if(find(opt_keys.begin(), opt_keys.end(), opt_key) == opt_keys.end() &&
				find(selected_options.begin(), selected_options.end(), opt_key) != selected_options.end())
			(*m_config)->set_key_value(opt_key, m_default_config.get()->option(opt_key)->clone());
	}

	update_settings_list();
}

wxMenu *create_add_part_popupmenu()
{
	wxMenu *menu = new wxMenu;
	std::vector<std::string> menu_items = { L("Add part"), L("Add modifier"), L("Add generic") };

	wxWindowID config_id_base = wxWindow::NewControlId(menu_items.size()+2);

	int i = 0;
	for (auto& item : menu_items) {
		auto menu_item = new wxMenuItem(menu, config_id_base + i, _(item));
		menu_item->SetBitmap(i == 0 ? m_icon_solidmesh : m_icon_modifiermesh);
		menu->Append(menu_item);
		i++;
    }

    menu->AppendSeparator();
    auto menu_item = new wxMenuItem(menu, config_id_base + 3, _(L("Split to sub-objects")));
    menu_item->SetBitmap(m_bmp_split);
    menu->Append(menu_item);

	wxWindow* win = get_tab_panel()->GetPage(0);

	menu->Bind(wxEVT_MENU, [config_id_base, win, menu](wxEvent &event){
		switch (event.GetId() - config_id_base) {
		case 0:
			on_btn_load(win);
			break;
		case 1:
			on_btn_load(win, true);
			break;
		case 2:
			on_btn_load(win, true, true);
			break;
		case 3:
			on_btn_split();
			break;
		default:{
			get_settings_choice(menu, event.GetId(), false);
			break;}
		}
	});

	menu->AppendSeparator();
	// Append settings popupmenu
	menu_item = new wxMenuItem(menu, config_id_base + 4, _(L("Add settings")));
	menu_item->SetBitmap(m_bmp_cog);

	auto sub_menu = create_add_settings_popupmenu(false);

	menu_item->SetSubMenu(sub_menu);
	menu->Append(menu_item);

	return menu;
}

wxMenu *create_add_settings_popupmenu(bool is_part)
{
	wxMenu *menu = new wxMenu;

 	auto categories = get_category_icon();

	settings_menu_hierarchy settings_menu;
	get_options_menu(settings_menu, is_part);

	for (auto cat : settings_menu)
	{
		auto menu_item = new wxMenuItem(menu, wxID_ANY/*config_id_base + inc*/, _(cat.first));
		menu_item->SetBitmap(categories.find(cat.first) == categories.end() ? 
								wxNullBitmap : categories.at(cat.first));
		menu->Append(menu_item);
	}

	menu->Bind(wxEVT_MENU, [menu](wxEvent &event) {
		get_settings_choice(menu, event.GetId(), true);
	});

	return menu;
}

void object_ctrl_context_menu()
{
// 	auto cur_column = m_objects_ctrl->GetCurrentColumn();
// 	auto action_column = m_objects_ctrl->GetColumn(4);
// 	if (cur_column == action_column)			
	{
		auto item = m_objects_ctrl->GetSelection();
		if (item)
		{
			if (m_objects_model->GetParent(item) == wxDataViewItem(0))				{
				auto menu = create_add_part_popupmenu();
				get_tab_panel()->GetPage(0)->PopupMenu(menu);
			}
			else {
// 				auto parent = m_objects_model->GetParent(item);
// 				// Take ID of the parent object to "inform" perl-side which object have to be selected on the scene
// 				obj_idx = m_objects_model->GetIdByItem(parent);
// 				auto volume_id = m_objects_model->GetVolumeIdByItem(item);
// 				if (volume_id < 0) return;
				auto menu = create_add_settings_popupmenu(true);
				get_tab_panel()->GetPage(0)->PopupMenu(menu);
			}
		}
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
		load_lambda(parent, (*m_objects)[obj_idx], part_names, is_modifier);
	else
		load_part(parent, (*m_objects)[obj_idx], part_names, is_modifier);

	parts_changed(obj_idx);

	for (int i = 0; i < part_names.size(); ++i)
		m_objects_ctrl->Select(	m_objects_model->AddChild(item, part_names.Item(i), 
								is_modifier ? m_icon_modifiermesh : m_icon_solidmesh));
// 	part_selection_changed();
#ifdef __WXMSW__
	object_ctrl_selection_changed();
#endif //__WXMSW__
}

void on_btn_del()
{
	auto item = m_objects_ctrl->GetSelection();
	if (!item) return;

	auto volume_id = m_objects_model->GetVolumeIdByItem(item);
	if (volume_id < 0)
		return;
	auto volume = (*m_objects)[m_selected_object_id]->volumes[volume_id];

	// if user is deleting the last solid part, throw error
	int solid_cnt = 0;
	for (auto vol : (*m_objects)[m_selected_object_id]->volumes)
		if (!vol->modifier)
			++solid_cnt;
	if (!volume->modifier && solid_cnt == 1) {
		Slic3r::GUI::show_error(nullptr, _(L("You can't delete the last solid part from this object.")));
		return;
	}

	(*m_objects)[m_selected_object_id]->delete_volume(volume_id);
	m_parts_changed = true;

	parts_changed(m_selected_object_id);

	m_objects_ctrl->Select(m_objects_model->Delete(item));
	part_selection_changed();
// #ifdef __WXMSW__
// 	object_ctrl_selection_changed();
// #endif //__WXMSW__
}

void on_btn_split()
{
	auto item = m_objects_ctrl->GetSelection();
	if (!item)
		return;
	auto volume_id = m_objects_model->GetVolumeIdByItem(item);
    ModelVolume* volume;
	if (volume_id < 0)
		volume = (*m_objects)[m_selected_object_id]->volumes[0];//return;
    else
	    volume = (*m_objects)[m_selected_object_id]->volumes[volume_id];
 	DynamicPrintConfig&	config = get_preset_bundle()->printers.get_edited_preset().config;
    auto nozzle_dmrs_cnt = config.option<ConfigOptionFloats>("nozzle_diameter")->values.size();
	if (volume->split(nozzle_dmrs_cnt) > 1)	{
        auto model_object = (*m_objects)[m_selected_object_id];
        for (auto id = 0; id < model_object->volumes.size(); id++)
            m_objects_model->AddChild(item, model_object->volumes[id]->name, m_icon_solidmesh, false);
	}
}

void on_btn_move_up(){
	auto item = m_objects_ctrl->GetSelection();
	if (!item)
		return;
	auto volume_id = m_objects_model->GetVolumeIdByItem(item);
	if (volume_id < 0)
		return;
	auto& volumes = (*m_objects)[m_selected_object_id]->volumes;
	if (0 < volume_id && volume_id < volumes.size()) {
		std::swap(volumes[volume_id - 1], volumes[volume_id]);
		m_parts_changed = true;
		m_objects_ctrl->Select(m_objects_model->MoveChildUp(item));
		part_selection_changed();
// #ifdef __WXMSW__
// 		object_ctrl_selection_changed();
// #endif //__WXMSW__
	}
}

void on_btn_move_down(){
	auto item = m_objects_ctrl->GetSelection();
	if (!item)
		return;
	auto volume_id = m_objects_model->GetVolumeIdByItem(item);
	if (volume_id < 0)
		return;
	auto& volumes = (*m_objects)[m_selected_object_id]->volumes;
	if (0 <= volume_id && volume_id+1 < volumes.size()) {
		std::swap(volumes[volume_id + 1], volumes[volume_id]);
		m_parts_changed = true;
		m_objects_ctrl->Select(m_objects_model->MoveChildDown(item));
		part_selection_changed();
// #ifdef __WXMSW__
// 		object_ctrl_selection_changed();
// #endif //__WXMSW__
	}
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
	
void update_settings_value()
{
    printf("update_settings_value\n");
	auto og = get_optgroup(ogFrequentlyObjectSettings);
    printf("selected_object_id = %d\n", m_selected_object_id);
	if (m_selected_object_id < 0 || m_objects->size() <= m_selected_object_id) {
		og->set_value("scale_x", 0);
		og->set_value("scale_y", 0);
		og->set_value("scale_z", 0);
        printf("return because of unselect\n");
		return;
	}
    g_is_percent_scale = boost::any_cast<wxString>(og->get_value("scale_unit")) == _("%");
    update_scale_values();
    update_rotation_values();
}

void part_selection_changed()
{
    printf("part_selection_changed\n");
	auto item = m_objects_ctrl->GetSelection();
	int obj_idx = -1;
	auto og = get_optgroup(ogFrequentlyObjectSettings);
	if (item)
	{
		bool is_part = false;
		if (m_objects_model->GetParent(item) == wxDataViewItem(0)) {
			obj_idx = m_objects_model->GetIdByItem(item);
			og->set_name(" " + _(L("Object Settings")) + " ");
			m_config = std::make_shared<DynamicPrintConfig*>(&(*m_objects)[obj_idx]->config);
		}
		else {
			auto parent = m_objects_model->GetParent(item);
			// Take ID of the parent object to "inform" perl-side which object have to be selected on the scene
			obj_idx = m_objects_model->GetIdByItem(parent);
			og->set_name(" " + _(L("Part Settings")) + " ");
			is_part = true;
			auto volume_id = m_objects_model->GetVolumeIdByItem(item);
			m_config = std::make_shared<DynamicPrintConfig*>(&(*m_objects)[obj_idx]->volumes[volume_id]->config);
		}

		auto config = m_config;
        og->set_value("object_name", m_objects_model->GetName(item));
		m_default_config = std::make_shared<DynamicPrintConfig>(*DynamicPrintConfig::new_from_defaults_keys(get_options(is_part)));
	}
    else {
        wxString empty_str = wxEmptyString;
        og->set_value("object_name", empty_str);
        m_config = nullptr;
    }

	update_settings_list();

	m_selected_object_id = obj_idx;

	update_settings_value();

/*	wxWindowUpdateLocker noUpdates(get_right_panel());

	m_move_options = Point3(0, 0, 0);
	m_last_coords = Point3(0, 0, 0);
	// reset move sliders
	std::vector<std::string> opt_keys = {"x", "y", "z"};
	auto og = get_optgroup(ogObjectMovers);
	for (auto opt_key: opt_keys)
		og->set_value(opt_key, int(0));

// 	if (!item || m_selected_object_id < 0){
	if (m_selected_object_id < 0){
		m_sizer_object_buttons->Show(false);
		m_sizer_part_buttons->Show(false);
		m_sizer_object_movers->Show(false);
		m_collpane_settings->Show(false);
		return;
	}

	m_collpane_settings->Show(true);

	auto volume_id = m_objects_model->GetVolumeIdByItem(item);
	if (volume_id < 0){
		m_sizer_object_buttons->Show(true);
		m_sizer_part_buttons->Show(false);
		m_sizer_object_movers->Show(false);
		m_collpane_settings->SetLabelText(_(L("Object Settings")) + ":");

// 		elsif($itemData->{type} eq 'object') {
// 			# select nothing in 3D preview
// 
// 			# attach object config to settings panel
// 			$self->{optgroup_movers}->disable;
// 			$self->{staticbox}->SetLabel('Object Settings');
// 			@opt_keys = (map @{$_->get_keys}, Slic3r::Config::PrintObject->new, Slic3r::Config::PrintRegion->new);
// 			$config = $self->{model_object}->config;
// 		}

		return;
	}

	m_collpane_settings->SetLabelText(_(L("Part Settings")) + ":");
	
	m_sizer_object_buttons->Show(false);
	m_sizer_part_buttons->Show(true);
	m_sizer_object_movers->Show(true);

	auto bb_size = m_objects[m_selected_object_id]->bounding_box().size();
	int scale = 10; //??

	m_mover_x->SetMin(-bb_size.x * 4 * scale);
	m_mover_x->SetMax(bb_size.x * 4 * scale);

	m_mover_y->SetMin(-bb_size.y * 4 * scale);
	m_mover_y->SetMax(bb_size.y * 4 * scale);

	m_mover_z->SetMin(-bb_size.z * 4 * scale);
	m_mover_z->SetMax(bb_size.z * 4 * scale);


	
//	my ($config, @opt_keys);
	m_btn_move_up->Enable(volume_id > 0);
	m_btn_move_down->Enable(volume_id + 1 < m_objects[m_selected_object_id]->volumes.size());

	// attach volume config to settings panel
	auto volume = m_objects[m_selected_object_id]->volumes[volume_id];

	if (volume->modifier) 
		og->enable();
	else 
		og->disable();

//	auto config = volume->config;

	// get default values
// 	@opt_keys = @{Slic3r::Config::PrintRegion->new->get_keys};
// 	} 
/*	
	# get default values
	my $default_config = Slic3r::Config::new_from_defaults_keys(\@opt_keys);

	# append default extruder
	push @opt_keys, 'extruder';
	$default_config->set('extruder', 0);
	$config->set_ifndef('extruder', 0);
	$self->{settings_panel}->set_default_config($default_config);
	$self->{settings_panel}->set_config($config);
	$self->{settings_panel}->set_opt_keys(\@opt_keys);
	$self->{settings_panel}->set_fixed_options([qw(extruder)]);
	$self->{settings_panel}->enable;
	}
	 */
}

void set_extruder_column_hidden(bool hide)
{
	m_objects_ctrl->GetColumn(3)->SetHidden(hide);
}

void update_extruder_in_config(const wxString& selection)
{
    if (!*m_config || selection.empty())
        return;

    int extruder = selection.size() > 1 ? 0 : atoi(selection.c_str());
    (*m_config)->set_key_value("extruder", new ConfigOptionInt(extruder));

    if (m_event_update_scene > 0) {
        wxCommandEvent e(m_event_update_scene);
        get_main_frame()->ProcessWindowEvent(e);
    }
}

void update_scale_values()
{
    update_scale_values((*m_objects)[m_selected_object_id]->instance_bounding_box(0).size(),
                        (*m_objects)[m_selected_object_id]->instances[0]->scaling_factor);
}

void update_scale_values(const Pointf3& size, float scaling_factor)
{
    auto og = get_optgroup(ogFrequentlyObjectSettings);

    if (g_is_percent_scale) {
        auto scale = scaling_factor * 100;
        og->set_value("scale_x", int(scale));
        og->set_value("scale_y", int(scale));
        og->set_value("scale_z", int(scale));
    }
    else {
        og->set_value("scale_x", int(size.x + 0.5));
        og->set_value("scale_y", int(size.y + 0.5));
        og->set_value("scale_z", int(size.z + 0.5));
    }
}

void update_rotation_values()
{
    auto og = get_optgroup(ogFrequentlyObjectSettings);

    og->set_value("rotation_x", 0);
    og->set_value("rotation_y", 0);

    auto rotation_z = (*m_objects)[m_selected_object_id]->instances[0]->rotation;
    auto deg = int(Geometry::rad2deg(rotation_z));
    if (deg > 180) deg -= 360;

    og->set_value("rotation_z", deg);
}

void update_rotation_value(const double angle, const std::string& axis)
{
    auto og = get_optgroup(ogFrequentlyObjectSettings);
    
    int deg = int(Geometry::rad2deg(angle));
    if (deg>180) deg -= 360;

    og->set_value("rotation_"+axis, deg);
}

} //namespace GUI
} //namespace Slic3r 