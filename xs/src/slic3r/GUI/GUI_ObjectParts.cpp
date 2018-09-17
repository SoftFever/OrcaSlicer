#include "GUI.hpp"
#include "OptionsGroup.hpp"
#include "PresetBundle.hpp"
#include "GUI_ObjectParts.hpp"
#include "Model.hpp"
#include "wxExtensions.hpp"
#include "LambdaObjectDialog.hpp"
#include "../../libslic3r/Utils.hpp"

#include <wx/msgdlg.h>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string.hpp>
#include "Geometry.hpp"
#include "slic3r/Utils/FixModelByWin10.hpp"

#include <wx/glcanvas.h>
#include "3DScene.hpp"

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
PrusaDoubleSlider           *m_slider = nullptr;
wxGLCanvas                  *m_preview_canvas = nullptr;

wxBitmap	m_icon_modifiermesh;
wxBitmap	m_icon_solidmesh;
wxBitmap	m_icon_manifold_warning;
wxBitmap	m_bmp_cog;
wxBitmap	m_bmp_split;

wxSlider*	m_mover_x = nullptr;
wxSlider*	m_mover_y = nullptr;
wxSlider*	m_mover_z = nullptr;
wxButton*	m_btn_move_up = nullptr;
wxButton*	m_btn_move_down = nullptr;
Vec3d		m_move_options;
Vec3d		m_last_coords;
int			m_selected_object_id = -1;

bool		g_prevent_list_events = false;		// We use this flag to avoid circular event handling Select() 
												// happens to fire a wxEVT_LIST_ITEM_SELECTED on OSX, whose event handler 
												// calls this method again and again and again
bool        g_is_percent_scale = false;         // It indicates if scale unit is percentage
bool        g_is_uniform_scale = false;         // It indicates if scale is uniform
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

    auto extruders_cnt = get_preset_bundle()->printers.get_selected_preset().printer_technology() == ptSLA ? 1 :
                         get_preset_bundle()->printers.get_edited_preset().config.option<ConfigOptionFloats>("nozzle_diameter")->values.size();

	DynamicPrintConfig config;
	for (auto& option : options)
	{
		auto const opt = config.def()->get(option);
		auto category = opt->category;
        if (category.empty() ||
            (category == "Extruders" && extruders_cnt == 1)) continue;

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
    m_icon_modifiermesh = wxBitmap(Slic3r::GUI::from_u8(Slic3r::var("lambda.png")), wxBITMAP_TYPE_PNG);//(Slic3r::var("plugin.png")), wxBITMAP_TYPE_PNG);
    m_icon_solidmesh = wxBitmap(Slic3r::GUI::from_u8(Slic3r::var("object.png")), wxBITMAP_TYPE_PNG);//(Slic3r::var("package.png")), wxBITMAP_TYPE_PNG);

	// init icon for manifold warning
    m_icon_manifold_warning = wxBitmap(Slic3r::GUI::from_u8(Slic3r::var("exclamation_mark_.png")), wxBITMAP_TYPE_PNG);//(Slic3r::var("error.png")), wxBITMAP_TYPE_PNG);

	// init bitmap for "Split to sub-objects" context menu
    m_bmp_split = wxBitmap(Slic3r::GUI::from_u8(Slic3r::var("split.png")), wxBITMAP_TYPE_PNG);

	// init bitmap for "Add Settings" context menu
	m_bmp_cog = wxBitmap(Slic3r::GUI::from_u8(Slic3r::var("cog.png")), wxBITMAP_TYPE_PNG);
}

bool is_parts_changed(){return m_parts_changed;}
bool is_part_settings_changed(){ return m_part_settings_changed; }

static wxString dots("â€¦", wxConvUTF8);

void set_tooltip_for_item(const wxPoint& pt)
{
    wxDataViewItem item;
    wxDataViewColumn* col;
    m_objects_ctrl->HitTest(pt, item, col);
    if (!item) return;

    if (col->GetTitle() == " ")
        m_objects_ctrl->GetMainWindow()->SetToolTip(_(L("Right button click the icon to change the object settings")));
    else if (col->GetTitle() == _("Name") &&
        m_objects_model->GetIcon(item).GetRefData() == m_icon_manifold_warning.GetRefData()) {
        int obj_idx = m_objects_model->GetIdByItem(item);
        auto& stats = (*m_objects)[obj_idx]->volumes[0]->mesh.stl.stats;
        int errors = stats.degenerate_facets + stats.edges_fixed + stats.facets_removed +
            stats.facets_added + stats.facets_reversed + stats.backwards_edges;

        wxString tooltip = wxString::Format(_(L("Auto-repaired (%d errors):\n")), errors);

        std::map<std::string, int> error_msg;
        error_msg[L("degenerate facets")] = stats.degenerate_facets;
        error_msg[L("edges fixed")] = stats.edges_fixed;
        error_msg[L("facets removed")] = stats.facets_removed;
        error_msg[L("facets added")] = stats.facets_added;
        error_msg[L("facets reversed")] = stats.facets_reversed;
        error_msg[L("backwards edges")] = stats.backwards_edges;

        for (auto error : error_msg)
        {
            if (error.second > 0)
                tooltip += wxString::Format(_("\t%d %s\n"), error.second, error.first);
        }
// OR
//             tooltip += wxString::Format(_(L("%d degenerate facets, %d edges fixed, %d facets removed, "
//                                             "%d facets added, %d facets reversed, %d backwards edges")),
//                                             stats.degenerate_facets, stats.edges_fixed, stats.facets_removed,
//                                             stats.facets_added, stats.facets_reversed, stats.backwards_edges);

        if (is_windows10())
            tooltip += _(L("Right button click the icon to fix STL through Netfabb"));

        m_objects_ctrl->GetMainWindow()->SetToolTip(tooltip);
    }
    else
        m_objects_ctrl->GetMainWindow()->SetToolTip(""); // hide tooltip
}

wxPoint get_mouse_position_in_control() {
    const wxPoint& pt = wxGetMousePosition();
    wxWindow* win = m_objects_ctrl->GetMainWindow();
    return wxPoint(pt.x - win->GetScreenPosition().x, 
                   pt.y - win->GetScreenPosition().y);
}

bool is_mouse_position_in_control(wxPoint& pt) {
    pt = get_mouse_position_in_control();
    const wxSize& cz = m_objects_ctrl->GetSize();
    if (pt.x > 0 && pt.x < cz.x &&
        pt.y > 0 && pt.y < cz.y)
        return true;
    return false;
}

wxDataViewColumn* object_ctrl_create_extruder_column(int extruders_count)
{
    wxArrayString choices;
    choices.Add("default");
    for (int i = 1; i <= extruders_count; ++i)
        choices.Add(wxString::Format("%d", i));
    wxDataViewChoiceRenderer *c =
        new wxDataViewChoiceRenderer(choices, wxDATAVIEW_CELL_EDITABLE, wxALIGN_CENTER_HORIZONTAL);
    wxDataViewColumn* column = new wxDataViewColumn(_(L("Extruder")), c, 2, 60, wxALIGN_CENTER_HORIZONTAL, wxDATAVIEW_COL_RESIZABLE);
    return column;
}

void create_objects_ctrl(wxWindow* win, wxBoxSizer*& objects_sz)
{
	m_objects_ctrl = new wxDataViewCtrl(win, wxID_ANY, wxDefaultPosition, wxDefaultSize);
	m_objects_ctrl->SetMinSize(wxSize(-1, 150)); // TODO - Set correct height according to the opened/closed objects

	objects_sz = new wxBoxSizer(wxVERTICAL);
	objects_sz->Add(m_objects_ctrl, 1, wxGROW | wxLEFT, 20);

	m_objects_model = new PrusaObjectDataViewModel;
	m_objects_ctrl->AssociateModel(m_objects_model);
#if wxUSE_DRAG_AND_DROP && wxUSE_UNICODE
	m_objects_ctrl->EnableDragSource(wxDF_UNICODETEXT);
	m_objects_ctrl->EnableDropTarget(wxDF_UNICODETEXT);
#endif // wxUSE_DRAG_AND_DROP && wxUSE_UNICODE

	// column 0(Icon+Text) of the view control: 
    // And Icon can be consisting of several bitmaps
    m_objects_ctrl->AppendColumn(new wxDataViewColumn(_(L("Name")), new PrusaBitmapTextRenderer(),
                                 0, 200, wxALIGN_LEFT, wxDATAVIEW_COL_RESIZABLE));

	// column 1 of the view control:
	m_objects_ctrl->AppendTextColumn(_(L("Copy")), 1, wxDATAVIEW_CELL_INERT, 45,
		wxALIGN_CENTER_HORIZONTAL, wxDATAVIEW_COL_RESIZABLE);

	// column 2 of the view control:
    m_objects_ctrl->AppendColumn(object_ctrl_create_extruder_column(4));

	// column 3 of the view control:
	m_objects_ctrl->AppendBitmapColumn(" ", 3, wxDATAVIEW_CELL_INERT, 25,
		wxALIGN_CENTER_HORIZONTAL, wxDATAVIEW_COL_RESIZABLE);
}

// ****** from GUI.cpp
wxBoxSizer* create_objects_list(wxWindow *win)
{
    wxBoxSizer* objects_sz;
    // create control
    create_objects_ctrl(win, objects_sz);

    // describe control behavior 
    m_objects_ctrl->Bind(wxEVT_DATAVIEW_SELECTION_CHANGED, [](wxEvent& event) {
		object_ctrl_selection_changed();
#ifndef __WXMSW__
        set_tooltip_for_item(get_mouse_position_in_control());
#endif //__WXMSW__        
	});

    m_objects_ctrl->Bind(wxEVT_DATAVIEW_ITEM_CONTEXT_MENU, [](wxDataViewEvent& event) {
        object_ctrl_context_menu();
//		event.Skip();
	});

    m_objects_ctrl->Bind(wxEVT_CHAR, [](wxKeyEvent& event) { object_ctrl_key_event(event); }); // doesn't work on OSX

#ifdef __WXMSW__
    // Extruder value changed
	m_objects_ctrl->Bind(wxEVT_CHOICE, [](wxCommandEvent& event) { update_extruder_in_config(event.GetString()); });

    m_objects_ctrl->GetMainWindow()->Bind(wxEVT_MOTION, [](wxMouseEvent& event) {
         set_tooltip_for_item(event.GetPosition());
         event.Skip();
    });
#else
    // equivalent to wxEVT_CHOICE on __WXMSW__
    m_objects_ctrl->Bind(wxEVT_DATAVIEW_ITEM_VALUE_CHANGED, [](wxDataViewEvent& event) { object_ctrl_item_value_change(event); });
#endif //__WXMSW__

    m_objects_ctrl->Bind(wxEVT_DATAVIEW_ITEM_BEGIN_DRAG,    [](wxDataViewEvent& e) {on_begin_drag(e);});
    m_objects_ctrl->Bind(wxEVT_DATAVIEW_ITEM_DROP_POSSIBLE, [](wxDataViewEvent& e) {on_drop_possible(e); });
    m_objects_ctrl->Bind(wxEVT_DATAVIEW_ITEM_DROP,          [](wxDataViewEvent& e) {on_drop(e);});
	return objects_sz;
}

wxBoxSizer* create_edit_object_buttons(wxWindow* win)
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
// 		on_btn_load(win);
	});

	btn_load_modifier->Bind(wxEVT_BUTTON, [win](wxEvent&) {
// 		on_btn_load(win, true);
	});

	btn_load_lambda_modifier->Bind(wxEVT_BUTTON, [win](wxEvent&) {
// 		on_btn_load(win, true, true);
	});

	btn_delete		->Bind(wxEVT_BUTTON, [](wxEvent&) { on_btn_del(); });
	btn_split		->Bind(wxEVT_BUTTON, [](wxEvent&) { on_btn_split(true); });
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

	Vec3d m = m_move_options;
	Vec3d l = m_last_coords;

    auto d = Vec3d(m(0) - l(0), m(1) - l(1), m(2) - l(2));
	auto volume = (*m_objects)[m_selected_object_id]->volumes[volume_id];
    volume->mesh.translate(d(0), d(1), d(2));
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
        if (opt_key == "x" && m_move_options(0) != val){
			update = true;
            m_move_options(0) = val;
		}
        else if (opt_key == "y" && m_move_options(1) != val){
			update = true;
            m_move_options(1) = val;
		}
        else if (opt_key == "z" && m_move_options(2) != val){
			update = true;
            m_move_options(2) = val;
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

	m_move_options = Vec3d(0, 0, 0);
	m_last_coords = Vec3d(0, 0, 0);

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
	sizer->Add(create_edit_object_buttons(win), 0, wxEXPAND, 0); // *** Edit Object Buttons***

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
	const auto ol_sizer = create_objects_list(parent);
	sizer->Add(ol_sizer, 1, wxEXPAND | wxTOP, 20);
	set_objects_list_sizer(ol_sizer);
}

Line add_og_to_object_settings(const std::string& option_name, const std::string& sidetext, int def_value = 0)
{
	Line line = { _(option_name), "" };
    if (option_name == "Scale") {
        line.near_label_widget = [](wxWindow* parent) {
            auto btn = new PrusaLockButton(parent, wxID_ANY);
            btn->Bind(wxEVT_BUTTON, [btn](wxCommandEvent &event){
                event.Skip();
                wxTheApp->CallAfter([btn]() { set_uniform_scaling(btn->IsLocked()); });
            });
            return btn;
        };
    }

	ConfigOptionDef def;
	def.type = coInt;
	def.default_value = new ConfigOptionInt(def_value);
	def.width = 55;

    if (option_name == "Rotation")
        def.min = -360;

	const std::string lower_name = boost::algorithm::to_lower_copy(option_name);

	std::vector<std::string> axes{ "x", "y", "z" };
	for (auto axis : axes) {
        if (axis == "z" && option_name != "Scale")
            def.sidetext = sidetext;
		Option option = Option(def, lower_name + "_" + axis);
		option.opt.full_width = true;
		line.append_option(option);
	}

	if (option_name == "Scale")
	{
	    def.width = 45;
		def.type = coStrings; 
		def.gui_type = "select_open";
		def.enum_labels.push_back(L("%"));
		def.enum_labels.push_back(L("mm"));
		def.default_value = new ConfigOptionStrings{ "mm" };

		const Option option = Option(def, lower_name + "_unit");
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

	ConfigOptionDef def;

    // Objects(sub-objects) name
	def.label = L("Name");
// 	def.type = coString;
    def.gui_type = "legend";
	def.tooltip = L("Object name");
	def.full_width = true;
	def.default_value = new ConfigOptionString{ " " };
	optgroup->append_single_option_line(Option(def, "object_name"));


    // Legend for object modification
    auto line = Line{ "", "" };
    def.label = "";
    def.type = coString;
    def.width = 55;

    std::vector<std::string> axes{ "x", "y", "z" };
    for (const auto axis : axes) {
		const auto label = boost::algorithm::to_upper_copy(axis);
        def.default_value = new ConfigOptionString{ "   "+label };
        Option option = Option(def, axis + "_axis_legend");
        line.append_option(option);
    }
    optgroup->append_line(line);


    // Settings table
	optgroup->append_line(add_og_to_object_settings(L("Position"), L("mm")));
	optgroup->append_line(add_og_to_object_settings(L("Rotation"), "°"));
	optgroup->append_line(add_og_to_object_settings(L("Scale"), "mm"));


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
	auto collpane = add_collapsible_pane(parent, sizer, "Objects List:", create_objects_list);
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
	auto item = m_objects_model->Add(item_name, model_object->instances.size());
	m_objects_ctrl->Select(item);

	// Add error icon if detected auto-repaire
	auto stats = model_object->volumes[0]->mesh.stl.stats;
	int errors =	stats.degenerate_facets + stats.edges_fixed + stats.facets_removed + 
					stats.facets_added + stats.facets_reversed + stats.backwards_edges;
	if (errors > 0)		{
        const PrusaDataViewBitmapText data(item_name, m_icon_manifold_warning);
		wxVariant variant;
		variant << data;
		m_objects_model->SetValue(variant, item, 0);
	}

    if (model_object->volumes.size() > 1) {
        for (auto id = 0; id < model_object->volumes.size(); id++)
            m_objects_model->AddChild(item, 
                                      model_object->volumes[id]->name, 
                                      m_icon_solidmesh, 
                                      model_object->volumes[id]->config.option<ConfigOptionInt>("extruder")->value,
                                      false);
        m_objects_ctrl->Expand(item);
    }

#ifndef __WXOSX__ 
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

    part_selection_changed();

// 	if (m_objects_model->IsEmpty())
// 		m_collpane_settings->Show(false);
}

void delete_all_objects_from_list()
{
	m_objects_model->DeleteAll();

    part_selection_changed();
// 	m_collpane_settings->Show(false);
}

void set_object_count(int idx, int count)
{
	m_objects_model->SetValue(wxString::Format("%d", count), idx, 1);
	m_objects_ctrl->Refresh();
}

void unselect_objects()
{
    if (!m_objects_ctrl->GetSelection())
        return;

    g_prevent_list_events = true;
    m_objects_ctrl->UnselectAll();
    part_selection_changed();
    g_prevent_list_events = false;
}

void select_current_object(int idx)
{
	g_prevent_list_events = true;
	m_objects_ctrl->UnselectAll();
    if (idx>=0)
        m_objects_ctrl->Select(m_objects_model->GetItemById(idx));
	part_selection_changed();
	g_prevent_list_events = false;
}

void select_current_volume(int idx, int vol_idx)
{
    if (vol_idx < 0) {
        select_current_object(idx);
        return;
    }
    g_prevent_list_events = true;
    m_objects_ctrl->UnselectAll();
    if (idx >= 0)
        m_objects_ctrl->Select(m_objects_model->GetItemByVolumeId(idx, vol_idx));
    part_selection_changed();
    g_prevent_list_events = false;
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

	if (m_event_object_selection_changed > 0) {
		wxCommandEvent event(m_event_object_selection_changed);
		event.SetId(m_selected_object_id); // set $obj_idx
        const wxDataViewItem item = m_objects_ctrl->GetSelection();
        if (!item || m_objects_model->GetParent(item) == wxDataViewItem(0))
            event.SetInt(-1); // set $vol_idx
        else {
            const int vol_idx = m_objects_model->GetVolumeIdByItem(item);
            if (vol_idx == -2) // is settings item
                event.SetInt(m_objects_model->GetVolumeIdByItem(m_objects_model->GetParent(item))); // set $vol_idx
            else
                event.SetInt(vol_idx);
        }
		get_main_frame()->ProcessWindowEvent(event);
	}

#ifdef __WXOSX__
    update_extruder_in_config(g_selected_extruder);
#endif //__WXOSX__        
}

void object_ctrl_context_menu()
{
    wxDataViewItem item;
    wxDataViewColumn* col;
//     printf("object_ctrl_context_menu\n");
    const wxPoint pt = get_mouse_position_in_control();
//     printf("mouse_position_in_control: x = %d, y = %d\n", pt.x, pt.y);
    m_objects_ctrl->HitTest(pt, item, col);
    if (!item)
#ifdef __WXOSX__ // #ys_FIXME temporary workaround for OSX 
                 // after Yosemite OS X version, HitTest return undefined item
        item = m_objects_ctrl->GetSelection();
    if (item) 
        show_context_menu();
    else
        printf("undefined item\n");
    return;
#else
        return;
#endif // __WXOSX__
//     printf("item exists\n");
    const wxString title = col->GetTitle();
//     printf("title = *%s*\n", title.data().AsChar());

    if (title == " ")
        show_context_menu();
// #ys_FIXME
//         else if (title == _("Name") && pt.x >15 &&
//                     m_objects_model->GetIcon(item).GetRefData() == m_icon_manifold_warning.GetRefData())
//         {
//             if (is_windows10())
//                 fix_through_netfabb();
//         }
#ifndef __WXMSW__
    m_objects_ctrl->GetMainWindow()->SetToolTip(""); // hide tooltip
#endif //__WXMSW__
}

void object_ctrl_key_event(wxKeyEvent& event)
{
    if (event.GetKeyCode() == WXK_TAB)
        m_objects_ctrl->Navigate(event.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward);
    else if (event.GetKeyCode() == WXK_DELETE
#ifdef __WXOSX__
        || event.GetKeyCode() == WXK_BACK
#endif //__WXOSX__
        ){
        printf("WXK_BACK\n");
        remove();
    }
    else
        event.Skip();
}

void object_ctrl_item_value_change(wxDataViewEvent& event)
{
    if (event.GetColumn() == 2)
    {
        wxVariant variant;
        m_objects_model->GetValue(variant, event.GetItem(), 2);
#ifdef __WXOSX__
        g_selected_extruder = variant.GetString();
#else // --> for Linux
        update_extruder_in_config(variant.GetString());
#endif //__WXOSX__  
    }
}

void show_manipulation_og(const bool show)
{
    wxGridSizer* grid_sizer = get_optgroup(ogFrequentlyObjectSettings)->get_grid_sizer();
    if (show == grid_sizer->IsShown(2))
        return;
    for (size_t id = 2; id < 12; id++)
        grid_sizer->Show(id, show);
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

    bool show_manipulations = true;
    const auto item = m_objects_ctrl->GetSelection();
	if (m_config && m_objects_model->IsSettingsItem(item)) 
	{
        auto extra_column = [](wxWindow* parent, const Line& line)
		{
			auto opt_key = (line.get_options())[0].opt_id;  //we assume that we have one option per line

			auto btn = new wxBitmapButton(parent, wxID_ANY, wxBitmap(from_u8(var("colorchange_delete_on.png")), wxBITMAP_TYPE_PNG),
				wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
#ifdef __WXMSW__
            btn->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif // __WXMSW__
			btn->Bind(wxEVT_BUTTON, [opt_key](wxEvent &event){
				(*m_config)->erase(opt_key);
				wxTheApp->CallAfter([]() { update_settings_list(); });
			});
			return btn;
		};

		std::map<std::string, std::vector<std::string>> cat_options;
		auto opt_keys = (*m_config)->keys();
        m_og_settings.resize(0);
        std::vector<std::string> categories;
        if (!(opt_keys.size() == 1 && opt_keys[0] == "extruder"))// return;
        {
            auto extruders_cnt = get_preset_bundle()->printers.get_selected_preset().printer_technology() == ptSLA ? 1 :
                get_preset_bundle()->printers.get_edited_preset().config.option<ConfigOptionFloats>("nozzle_diameter")->values.size();

            for (auto& opt_key : opt_keys) {
                auto category = (*m_config)->def()->get(opt_key)->category;
                if (category.empty() ||
                    (category == "Extruders" && extruders_cnt == 1)) continue;

                std::vector< std::string > new_category;

                auto& cat_opt = cat_options.find(category) == cat_options.end() ? new_category : cat_options.at(category);
                cat_opt.push_back(opt_key);
                if (cat_opt.size() == 1)
                    cat_options[category] = cat_opt;
            }

            for (auto& cat : cat_options) {
                if (cat.second.size() == 1 && cat.second[0] == "extruder")
                    continue;

                auto optgroup = std::make_shared<ConfigOptionsGroup>(parent, cat.first, *m_config, false, ogDEFAULT, extra_column);
                optgroup->label_width = 150;
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

                categories.push_back(cat.first);
            }
        }

        if (m_og_settings.empty()) {
            m_objects_ctrl->Select(m_objects_model->Delete(item));
            part_selection_changed();
        }
        else {
            if (!categories.empty())
                m_objects_model->UpdateSettingsDigest(item, categories);
            show_manipulations = false;
        }
	}

    show_manipulation_og(show_manipulations);
    show_info_sizer(show_manipulations && item && m_objects_model->GetParent(item) == wxDataViewItem(0));

#ifdef __linux__
	no_updates.reset(nullptr);
#endif

    parent->Layout();
    get_right_panel()->GetParent()->Layout();
}

void get_settings_choice(wxMenu *menu, int id, bool is_part)
{
	const auto category_name = menu->GetLabel(id);

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


    // Add settings item for object
    const auto item = m_objects_ctrl->GetSelection();
    if (item) {
        const auto settings_item = m_objects_model->HasSettings(item);
        m_objects_ctrl->Select(settings_item ? settings_item : 
                               m_objects_model->AddSettingsChild(item));
#ifndef __WXOSX__
        part_selection_changed();
#endif //no __WXOSX__
    }
    else
	update_settings_list();
}

void menu_item_add_generic(wxMenuItem* &menu, int id) {
    auto sub_menu = new wxMenu;

    std::vector<std::string> menu_items = { L("Box"), L("Cylinder"), L("Sphere"), L("Slab") };
    for (auto& item : menu_items)
        sub_menu->Append(new wxMenuItem(sub_menu, ++id, _(item)));

#ifndef __WXMSW__
    sub_menu->Bind(wxEVT_MENU, [sub_menu](wxEvent &event) {
        load_lambda(sub_menu->GetLabel(event.GetId()).ToStdString());
    });
#endif //no __WXMSW__

    menu->SetSubMenu(sub_menu);
}

wxMenuItem* menu_item_split(wxMenu* menu, int id) {
    auto menu_item = new wxMenuItem(menu, id, _(L("Split to parts")));
    menu_item->SetBitmap(m_bmp_split);
    return menu_item;
}

wxMenuItem* menu_item_settings(wxMenu* menu, int id, const bool is_part) {
    auto  menu_item = new wxMenuItem(menu, id, _(L("Add settings")));
    menu_item->SetBitmap(m_bmp_cog);

    auto sub_menu = create_add_settings_popupmenu(is_part);
    menu_item->SetSubMenu(sub_menu);
    return menu_item;
}

wxMenu *create_add_part_popupmenu()
{
	wxMenu *menu = new wxMenu;
	std::vector<std::string> menu_items = { L("Add part"), L("Add modifier"), L("Add generic") };

	wxWindowID config_id_base = wxWindow::NewControlId(menu_items.size()+4+2);

	int i = 0;
	for (auto& item : menu_items) {
		auto menu_item = new wxMenuItem(menu, config_id_base + i, _(item));
		menu_item->SetBitmap(i == 0 ? m_icon_solidmesh : m_icon_modifiermesh);
        if (item == "Add generic")
            menu_item_add_generic(menu_item, config_id_base + i);
        menu->Append(menu_item);
		i++;
    }

    menu->AppendSeparator();
    auto menu_item = menu_item_split(menu, config_id_base + i + 4);
    menu->Append(menu_item);
    menu_item->Enable(is_splittable_object(false));

    menu->AppendSeparator();
    // Append settings popupmenu
    menu->Append(menu_item_settings(menu, config_id_base + i + 5, false));

	menu->Bind(wxEVT_MENU, [config_id_base, menu](wxEvent &event){
		switch (event.GetId() - config_id_base) {
		case 0:
			on_btn_load();
			break;
		case 1:
			on_btn_load(true);
			break;
		case 2:
// 			on_btn_load(true, true);
			break;
        case 3:
        case 4:
        case 5:
        case 6:
#ifdef __WXMSW__
		    load_lambda(menu->GetLabel(event.GetId()).ToStdString());
#endif // __WXMSW__
            break;
		case 7: //3:
			on_btn_split(false);
			break;
		default:
#ifdef __WXMSW__
			get_settings_choice(menu, event.GetId(), false);
#endif // __WXMSW__
			break;
		}
	});

	return menu;
}

wxMenu *create_part_settings_popupmenu()
{
    wxMenu *menu = new wxMenu;
    wxWindowID config_id_base = wxWindow::NewControlId(2);

    auto menu_item = menu_item_split(menu, config_id_base);
    menu->Append(menu_item);
    menu_item->Enable(is_splittable_object(true));

    menu->AppendSeparator();
    // Append settings popupmenu
    menu->Append(menu_item_settings(menu, config_id_base + 1, true));

    menu->Bind(wxEVT_MENU, [config_id_base, menu](wxEvent &event){
        switch (event.GetId() - config_id_base) {
        case 0:
            on_btn_split(true);
            break;
        default:{
            get_settings_choice(menu, event.GetId(), true);
            break; }
        }
    });

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
		auto menu_item = new wxMenuItem(menu, wxID_ANY, _(cat.first));
		menu_item->SetBitmap(categories.find(cat.first) == categories.end() ? 
								wxNullBitmap : categories.at(cat.first));
		menu->Append(menu_item);
	}
#ifndef __WXMSW__
    menu->Bind(wxEVT_MENU, [menu,is_part](wxEvent &event) {
        get_settings_choice(menu, event.GetId(), is_part);
	});
#endif //no __WXMSW__
	return menu;
}

void show_context_menu()
{
    const auto item = m_objects_ctrl->GetSelection();
    if (item)
    {
        if (m_objects_model->IsSettingsItem(item))
            return;
        const auto menu = m_objects_model->GetParent(item) == wxDataViewItem(0) ? 
                            create_add_part_popupmenu() : 
                            create_part_settings_popupmenu();
        get_tab_panel()->GetPage(0)->PopupMenu(menu);
    }
}

// ******

void load_part(	ModelObject* model_object, 
				wxArrayString& part_names, const bool is_modifier)
{
    wxWindow* parent = get_tab_panel()->GetPage(0);

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
				new_volume->set_type(is_modifier ? ModelVolume::PARAMETER_MODIFIER : ModelVolume::MODEL_PART);
				boost::filesystem::path(input_file).filename().string();
				new_volume->name = boost::filesystem::path(input_file).filename().string();

				part_names.Add(new_volume->name);

				// apply the same translation we applied to the object
				new_volume->mesh.translate( model_object->origin_translation(0),
											model_object->origin_translation(1), 
											model_object->origin_translation(2) );
				// set a default extruder value, since user can't add it manually
				new_volume->config.set_key_value("extruder", new ConfigOptionInt(0));

				m_parts_changed = true;
			}
		}
	}
}

void load_lambda(	ModelObject* model_object,
					wxArrayString& part_names, const bool is_modifier)
{
    auto dlg = new LambdaObjectDialog(m_objects_ctrl->GetMainWindow());
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
        mesh = make_cube(size(0)*1.5, size(1)*1.5, params.slab_h);
		// box sets the base coordinate at 0, 0, move to center of plate and move it up to initial_z
        mesh.translate(-size(0)*1.5 / 2.0, -size(1)*1.5 / 2.0, params.slab_z);
		name += "Slab";
		break; }
	default:
		break;
	}
	mesh.repair();

	auto new_volume = model_object->add_volume(mesh);
	new_volume->set_type(is_modifier ? ModelVolume::PARAMETER_MODIFIER : ModelVolume::MODEL_PART);

	new_volume->name = name;
	// set a default extruder value, since user can't add it manually
	new_volume->config.set_key_value("extruder", new ConfigOptionInt(0));

	part_names.Add(name);

	m_parts_changed = true;
}

void load_lambda(const std::string& type_name)
{
    if (m_selected_object_id < 0) return;

    auto dlg = new LambdaObjectDialog(m_objects_ctrl->GetMainWindow(), type_name);
    if (dlg->ShowModal() == wxID_CANCEL)
        return;

    const std::string name = "lambda-"+type_name;
    TriangleMesh mesh;

    const auto params = dlg->ObjectParameters();
    if (type_name == _("Box"))
        mesh = make_cube(params.dim[0], params.dim[1], params.dim[2]);
    else if (type_name == _("Cylinder"))
        mesh = make_cylinder(params.cyl_r, params.cyl_h);
    else if (type_name == _("Sphere"))
        mesh = make_sphere(params.sph_rho);
    else if (type_name == _("Slab")){
        const auto& size = (*m_objects)[m_selected_object_id]->bounding_box().size();
        mesh = make_cube(size(0)*1.5, size(1)*1.5, params.slab_h);
        // box sets the base coordinate at 0, 0, move to center of plate and move it up to initial_z
        mesh.translate(-size(0)*1.5 / 2.0, -size(1)*1.5 / 2.0, params.slab_z);
    }
    mesh.repair();

    auto new_volume = (*m_objects)[m_selected_object_id]->add_volume(mesh);
    new_volume->set_type(ModelVolume::PARAMETER_MODIFIER);

    new_volume->name = name;
    // set a default extruder value, since user can't add it manually
    new_volume->config.set_key_value("extruder", new ConfigOptionInt(0));

    m_parts_changed = true;
    parts_changed(m_selected_object_id);

    m_objects_ctrl->Select(m_objects_model->AddChild(m_objects_ctrl->GetSelection(), 
                                                     name, m_icon_modifiermesh));
#ifndef __WXOSX__ //#ifdef __WXMSW__ // #ys_FIXME
    object_ctrl_selection_changed();
#endif //no __WXOSX__ //__WXMSW__
}

void on_btn_load(bool is_modifier /*= false*/, bool is_lambda/* = false*/)
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
		load_lambda((*m_objects)[obj_idx], part_names, is_modifier);
	else
		load_part((*m_objects)[obj_idx], part_names, is_modifier);

	parts_changed(obj_idx);

	for (int i = 0; i < part_names.size(); ++i)
		m_objects_ctrl->Select(	m_objects_model->AddChild(item, part_names.Item(i), 
								is_modifier ? m_icon_modifiermesh : m_icon_solidmesh));
#ifndef __WXOSX__ //#ifdef __WXMSW__ // #ys_FIXME
	object_ctrl_selection_changed();
#endif //no __WXOSX__//__WXMSW__
}

void remove_settings_from_config()
{
    auto opt_keys = (*m_config)->keys();
    if (opt_keys.size() == 1 && opt_keys[0] == "extruder")
        return;
    int extruder = -1;
    if ((*m_config)->has("extruder"))
        extruder = (*m_config)->option<ConfigOptionInt>("extruder")->value;

    (*m_config)->clear();

    if (extruder >=0 )
        (*m_config)->set_key_value("extruder", new ConfigOptionInt(extruder));
}

bool remove_subobject_from_object(const int volume_id)
{
    const auto volume = (*m_objects)[m_selected_object_id]->volumes[volume_id];

    // if user is deleting the last solid part, throw error
    int solid_cnt = 0;
    for (auto vol : (*m_objects)[m_selected_object_id]->volumes)
        if (vol->is_model_part())
            ++solid_cnt;
    if (volume->is_model_part() && solid_cnt == 1) {
        Slic3r::GUI::show_error(nullptr, _(L("You can't delete the last solid part from this object.")));
        return false;
    }

    (*m_objects)[m_selected_object_id]->delete_volume(volume_id);
    m_parts_changed = true;

    parts_changed(m_selected_object_id);
    return true;
}

void on_btn_del()
{
	auto item = m_objects_ctrl->GetSelection();
	if (!item) return;

	const auto volume_id = m_objects_model->GetVolumeIdByItem(item);
	if (volume_id ==-1)
		return;
    
    if (volume_id ==-2)
        remove_settings_from_config();
	else if (!remove_subobject_from_object(volume_id)) 
        return;

	m_objects_ctrl->Select(m_objects_model->Delete(item));
	part_selection_changed();
}

bool get_volume_by_item(const bool split_part, const wxDataViewItem& item, ModelVolume*& volume)
{
    if (!item || m_selected_object_id < 0)
        return false;
    const auto volume_id = m_objects_model->GetVolumeIdByItem(item);
    if (volume_id < 0) {
        if (split_part) return false;
        volume = (*m_objects)[m_selected_object_id]->volumes[0]; 
    }
    else
        volume = (*m_objects)[m_selected_object_id]->volumes[volume_id];
    if (volume)
        return true;
    return false;
}

bool is_splittable_object(const bool split_part)
{
    const wxDataViewItem item = m_objects_ctrl->GetSelection();
    if (!item) return false;

    wxDataViewItemArray children;
    if (!split_part && m_objects_model->GetChildren(item, children) > 0)
        return false;

    ModelVolume* volume;
    if (!get_volume_by_item(split_part, item, volume) || !volume)
        return false;

    TriangleMeshPtrs meshptrs = volume->mesh.split();
    if (meshptrs.size() <= 1) {
        delete meshptrs.front();
        return false;
    }

    return true;
}

void on_btn_split(const bool split_part)
{
	const auto item = m_objects_ctrl->GetSelection();
	if (!item || m_selected_object_id<0)
		return;
    ModelVolume* volume;
    if (!get_volume_by_item(split_part, item, volume)) return;
    DynamicPrintConfig&	config = get_preset_bundle()->printers.get_edited_preset().config;
    const auto nozzle_dmrs_cnt = config.option<ConfigOptionFloats>("nozzle_diameter")->values.size();
    if (volume->split(nozzle_dmrs_cnt) == 1) {
        wxMessageBox(_(L("The selected object couldn't be split because it contains only one part.")));
        return;
    }

    auto model_object = (*m_objects)[m_selected_object_id];

    if (split_part) {
        auto parent = m_objects_model->GetParent(item);
        m_objects_model->DeleteChildren(parent);

        for (auto id = 0; id < model_object->volumes.size(); id++)
            m_objects_model->AddChild(parent, model_object->volumes[id]->name,
                                      model_object->volumes[id]->is_modifier() ? m_icon_modifiermesh : m_icon_solidmesh,
                                      model_object->volumes[id]->config.has("extruder") ?
                                        model_object->volumes[id]->config.option<ConfigOptionInt>("extruder")->value : 0,
                                      false);

        m_objects_ctrl->Expand(parent);
    }
    else {
        for (auto id = 0; id < model_object->volumes.size(); id++)
            m_objects_model->AddChild(item, model_object->volumes[id]->name, 
                                      m_icon_solidmesh,
                                      model_object->volumes[id]->config.has("extruder") ?
                                        model_object->volumes[id]->config.option<ConfigOptionInt>("extruder")->value : 0, 
                                      false);
        m_objects_ctrl->Expand(item);
    }

    m_parts_changed = true;
    parts_changed(m_selected_object_id);
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
	auto og = get_optgroup(ogFrequentlyObjectSettings);
	if (m_selected_object_id < 0 || m_objects->size() <= m_selected_object_id) {
        og->set_value("position_x", 0);
        og->set_value("position_y", 0);
        og->set_value("position_z", 0);
        og->set_value("scale_x", 0);
		og->set_value("scale_y", 0);
		og->set_value("scale_z", 0);
        og->set_value("rotation_x", 0);
        og->set_value("rotation_y", 0);
        og->set_value("rotation_z", 0);
        og->disable();
		return;
	}
    g_is_percent_scale = boost::any_cast<wxString>(og->get_value("scale_unit")) == _("%");
    update_position_values();
    update_scale_values();
    update_rotation_values();
    og->enable();
}

void part_selection_changed()
{
	auto item = m_objects_ctrl->GetSelection();
	int obj_idx = -1;
	auto og = get_optgroup(ogFrequentlyObjectSettings);
    m_config = nullptr;
    wxString object_name = wxEmptyString;
	if (item)
	{
        const bool is_settings_item = m_objects_model->IsSettingsItem(item);
		bool is_part = false;
        wxString og_name = wxEmptyString;
        if (m_objects_model->GetParent(item) == wxDataViewItem(0)) {
			obj_idx = m_objects_model->GetIdByItem(item);
			og_name = _(L("Object manipulation"));
			m_config = std::make_shared<DynamicPrintConfig*>(&(*m_objects)[obj_idx]->config);
		}
		else {
			auto parent = m_objects_model->GetParent(item);
            // Take ID of the parent object to "inform" perl-side which object have to be selected on the scene
			obj_idx = m_objects_model->GetIdByItem(parent);
			if (is_settings_item) {
                if (m_objects_model->GetParent(parent) == wxDataViewItem(0)) {
                    og_name = _(L("Object Settings to modify"));
                    m_config = std::make_shared<DynamicPrintConfig*>(&(*m_objects)[obj_idx]->config);
                }
                else {
                    og_name = _(L("Part Settings to modify"));
			        is_part = true;
                    auto main_parent = m_objects_model->GetParent(parent);
                    obj_idx = m_objects_model->GetIdByItem(main_parent);
			        const auto volume_id = m_objects_model->GetVolumeIdByItem(parent);
			        m_config = std::make_shared<DynamicPrintConfig*>(&(*m_objects)[obj_idx]->volumes[volume_id]->config);
                }
            }
            else {
                og_name = _(L("Part manipulation"));
                is_part = true;
                const auto volume_id = m_objects_model->GetVolumeIdByItem(item);
                m_config = std::make_shared<DynamicPrintConfig*>(&(*m_objects)[obj_idx]->volumes[volume_id]->config);
            }
		}

        og->set_name(" " + og_name + " ");
        object_name = m_objects_model->GetName(item);
		m_default_config = std::make_shared<DynamicPrintConfig>(*DynamicPrintConfig::new_from_defaults_keys(get_options(is_part)));
	}
    og->set_value("object_name", object_name);

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
	m_objects_ctrl->GetColumn(2)->SetHidden(hide);
}

void update_extruder_in_config(const wxString& selection)
{
    if (!m_config || selection.empty())
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
    auto og = get_optgroup(ogFrequentlyObjectSettings);
    auto instance = (*m_objects)[m_selected_object_id]->instances.front();
    auto size = (*m_objects)[m_selected_object_id]->instance_bounding_box(0).size();

    if (g_is_percent_scale) {
        auto scale = instance->scaling_factor * 100.0;
        og->set_value("scale_x", int(scale));
        og->set_value("scale_y", int(scale));
        og->set_value("scale_z", int(scale));
    }
    else {
        og->set_value("scale_x", int(instance->scaling_factor * size(0) + 0.5));
        og->set_value("scale_y", int(instance->scaling_factor * size(1) + 0.5));
        og->set_value("scale_z", int(instance->scaling_factor * size(2) + 0.5));
    }
}

void update_position_values()
{
    auto og = get_optgroup(ogFrequentlyObjectSettings);
    auto instance = (*m_objects)[m_selected_object_id]->instances.front();

#if ENABLE_MODELINSTANCE_3D_OFFSET
    og->set_value("position_x", int(instance->get_offset(X)));
    og->set_value("position_y", int(instance->get_offset(Y)));
    og->set_value("position_z", int(instance->get_offset(Z)));
#else
    og->set_value("position_x", int(instance->offset(0)));
    og->set_value("position_y", int(instance->offset(1)));
    og->set_value("position_z", 0);
#endif // ENABLE_MODELINSTANCE_3D_OFFSET
}

void update_position_values(const Vec3d& position)
{
    auto og = get_optgroup(ogFrequentlyObjectSettings);

    og->set_value("position_x", int(position(0)));
    og->set_value("position_y", int(position(1)));
    og->set_value("position_z", int(position(2)));
}

void update_scale_values(double scaling_factor)
{
    auto og = get_optgroup(ogFrequentlyObjectSettings);

    // this is temporary
    // to be able to update the values as size
    // we need to store somewhere the original size
    // or have it passed as parameter
    if (!g_is_percent_scale)
        og->set_value("scale_unit", _("%"));

    auto scale = scaling_factor * 100.0;
    og->set_value("scale_x", int(scale));
    og->set_value("scale_y", int(scale));
    og->set_value("scale_z", int(scale));
}

void update_rotation_values()
{
    auto og = get_optgroup(ogFrequentlyObjectSettings);
    auto instance = (*m_objects)[m_selected_object_id]->instances.front();
    og->set_value("rotation_x", 0);
    og->set_value("rotation_y", 0);
    og->set_value("rotation_z", int(Geometry::rad2deg(instance->rotation)));
}

void update_rotation_value(double angle, Axis axis)
{
    auto og = get_optgroup(ogFrequentlyObjectSettings);

    std::string axis_str;
    switch (axis)
    {
    case X:
    {
        axis_str = "rotation_x";
        break;
    }
    case Y:
    {
        axis_str = "rotation_y";
        break;
    }
    case Z:
    {
        axis_str = "rotation_z";
        break;
    }
    }

    og->set_value(axis_str, int(Geometry::rad2deg(angle)));
}

void set_uniform_scaling(const bool uniform_scale)
{
    g_is_uniform_scale = uniform_scale;
}

void on_begin_drag(wxDataViewEvent &event)
{
    wxDataViewItem item(event.GetItem());

    // only allow drags for item, not containers
    if (m_objects_model->GetParent(item) == wxDataViewItem(0) || m_objects_model->IsSettingsItem(item)) {
        event.Veto();
        return;
    }

    /* Under MSW or OSX, DnD moves an item to the place of another selected item
     * But under GTK, DnD moves an item between another two items.
     * And as a result - call EVT_CHANGE_SELECTION to unselect all items.
     * To prevent such behavior use g_prevent_list_events
    **/
    g_prevent_list_events = true;//it's needed for GTK

    wxTextDataObject *obj = new wxTextDataObject;
    obj->SetText(wxString::Format("%d", m_objects_model->GetVolumeIdByItem(item)));
    event.SetDataObject(obj);
    event.SetDragFlags(/*wxDrag_AllowMove*/wxDrag_DefaultMove); // allows both copy and move;
}

void on_drop_possible(wxDataViewEvent &event)
{
    wxDataViewItem item(event.GetItem());

    // only allow drags for item or background, not containers
    if (item.IsOk() && m_objects_model->GetParent(item) == wxDataViewItem(0) ||
        event.GetDataFormat() != wxDF_UNICODETEXT || m_objects_model->IsSettingsItem(item))
        event.Veto();
}

void on_drop(wxDataViewEvent &event)
{
    wxDataViewItem item(event.GetItem());

    // only allow drops for item, not containers
    if (item.IsOk() && m_objects_model->GetParent(item) == wxDataViewItem(0) ||
        event.GetDataFormat() != wxDF_UNICODETEXT || m_objects_model->IsSettingsItem(item)) {
        event.Veto();
        return;
    }    

    wxTextDataObject obj;
    obj.SetData(wxDF_UNICODETEXT, event.GetDataSize(), event.GetDataBuffer());

    int from_volume_id = std::stoi(obj.GetText().ToStdString());
    int to_volume_id = m_objects_model->GetVolumeIdByItem(item);

#ifdef __WXGTK__
    /* Under GTK, DnD moves an item between another two items.
     * And event.GetItem() return item, which is under "insertion line"
     * So, if we move item down we should to decrease the to_volume_id value
    **/
    if (to_volume_id > from_volume_id) to_volume_id--;
#endif // __WXGTK__

    m_objects_ctrl->Select(m_objects_model->ReorganizeChildren(from_volume_id, to_volume_id,
                                                               m_objects_model->GetParent(item)));

    auto& volumes = (*m_objects)[m_selected_object_id]->volumes;
    auto delta = to_volume_id < from_volume_id ? -1 : 1;
    int cnt = 0;
    for (int id = from_volume_id; cnt < abs(from_volume_id - to_volume_id); id+=delta, cnt++)
        std::swap(volumes[id], volumes[id +delta]);

    m_parts_changed = true;
    parts_changed(m_selected_object_id);

    g_prevent_list_events = false;
}

void update_objects_list_extruder_column(int extruders_count)
{
    if (get_preset_bundle()->printers.get_selected_preset().printer_technology() == ptSLA)
        extruders_count = 1;

    // delete old 3rd column
    m_objects_ctrl->DeleteColumn(m_objects_ctrl->GetColumn(2));
    // insert new created 3rd column
    m_objects_ctrl->InsertColumn(2, object_ctrl_create_extruder_column(extruders_count));
    // set show/hide for this column 
    set_extruder_column_hidden(extruders_count <= 1);
}

void create_double_slider(wxWindow* parent, wxBoxSizer* sizer, wxGLCanvas* canvas)
{
    m_slider = new PrusaDoubleSlider(parent, wxID_ANY, 0, 0, 0, 100);
    sizer->Add(m_slider, 0, wxEXPAND, 0);

    m_preview_canvas = canvas;
    m_preview_canvas->Bind(wxEVT_KEY_DOWN, update_double_slider_from_canvas);

    m_slider->Bind(wxEVT_SCROLL_CHANGED, [parent](wxEvent& event) {
        _3DScene::set_toolpaths_range(m_preview_canvas, m_slider->GetLowerValueD() - 1e-6, m_slider->GetHigherValueD() + 1e-6);
        if (parent->IsShown())
            m_preview_canvas->Refresh();
    });
}

void fill_slider_values(std::vector<std::pair<int, double>> &values, 
                        const std::vector<double> &layers_z)
{
    std::vector<double> layers_all_z = _3DScene::get_current_print_zs(m_preview_canvas, false);
    if (layers_all_z.size() == layers_z.size())
        for (int i = 0; i < layers_z.size(); i++)
            values.push_back(std::pair<int, double>(i+1, layers_z[i]));
    else if (layers_all_z.size() > layers_z.size()) {
        int cur_id = 0;
        for (int i = 0; i < layers_z.size(); i++)
            for (int j = cur_id; j < layers_all_z.size(); j++)
                if (layers_z[i] - 1e-6 < layers_all_z[j] && layers_all_z[j] < layers_z[i] + 1e-6) {
                    values.push_back(std::pair<int, double>(j+1, layers_z[i]));
                    cur_id = j;
                    break;
                }
    }
}

void set_double_slider_thumbs(  const bool force_sliders_full_range, 
                                const std::vector<double> &layers_z, 
                                const double z_low, const double z_high)
{
    // Force slider full range only when slider is created.
    // Support selected diapason on the all next steps
    if (/*force_sliders_full_range*/z_high == 0.0) { 
        m_slider->SetLowerValue(0);
        m_slider->SetHigherValue(layers_z.size() - 1);
        return;
    }
    
    for (int i = layers_z.size() - 1; i >= 0; i--)
        if (z_low >= layers_z[i]) {
            m_slider->SetLowerValue(i);
            break;
        }
    for (int i = layers_z.size() - 1; i >= 0 ; i--)
        if (z_high >= layers_z[i]) {
            m_slider->SetHigherValue(i);
            break;
        }
}

void update_double_slider(bool force_sliders_full_range)
{
    std::vector<std::pair<int, double>> values;
    std::vector<double> layers_z = _3DScene::get_current_print_zs(m_preview_canvas, true);
    fill_slider_values(values, layers_z);

    const double z_low = m_slider->GetLowerValueD();
    const double z_high = m_slider->GetHigherValueD();
    m_slider->SetMaxValue(layers_z.size() - 1);
    m_slider->SetSliderValues(values);

    set_double_slider_thumbs(force_sliders_full_range, layers_z, z_low, z_high);
}

void reset_double_slider()
{
    m_slider->SetHigherValue(0);
    m_slider->SetLowerValue(0);
}

void update_double_slider_from_canvas(wxKeyEvent& event)
{
    if (event.HasModifiers()) {
        event.Skip();
        return;
    }

    const auto key = event.GetKeyCode();

    if (key == 'U' || key == 'D') {
        const int new_pos = key == 'U' ? m_slider->GetHigherValue() + 1 : m_slider->GetHigherValue() - 1;
        m_slider->SetHigherValue(new_pos);
        if (event.ShiftDown()) m_slider->SetLowerValue(m_slider->GetHigherValue());
    }
    else if (key == 'S')
        m_slider->ChangeOneLayerLock();
    else
        event.Skip();
}

void show_manipulation_sizer(const bool is_simple_mode)
{
    auto item = m_objects_ctrl->GetSelection();
    if (!item || !is_simple_mode)
        return;

    if (m_objects_model->IsSettingsItem(item)) {
        m_objects_ctrl->Select(m_objects_model->GetParent(item));
        part_selection_changed();
    }
}

} //namespace GUI
} //namespace Slic3r 