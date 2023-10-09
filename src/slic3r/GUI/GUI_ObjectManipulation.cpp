#include "GUI_ObjectManipulation.hpp"
#include "I18N.hpp"
#include "format.hpp"
#include "BitmapComboBox.hpp"

#include "GLCanvas3D.hpp"
#include "OptionsGroup.hpp"
#include "GUI_App.hpp"
#include "wxExtensions.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Geometry.hpp"
#include "Selection.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"

#include <wx/glcanvas.h>

#include <boost/algorithm/string.hpp>
#include "slic3r/Utils/FixModelByWin10.hpp"

namespace Slic3r
{
namespace GUI
{

const double ObjectManipulation::in_to_mm = 25.4;
const double ObjectManipulation::mm_to_in = 1 / ObjectManipulation::in_to_mm;

// Helper function to be used by drop to bed button. Returns lowest point of this
// volume in world coordinate system.
static double get_volume_min_z(const GLVolume& volume)
{
    return volume.transformed_convex_hull_bounding_box().min.z();
}

static choice_ctrl* create_word_local_combo(wxWindow *parent)
{
    wxSize size(15 * wxGetApp().em_unit(), -1);

    choice_ctrl* temp = nullptr;
#ifdef __WXOSX__
    /* wxBitmapComboBox with wxCB_READONLY style return NULL for GetTextCtrl(),
     * so ToolTip doesn't shown.
     * Next workaround helps to solve this problem
     */
    temp = new wxBitmapComboBox();
    temp->SetTextCtrlStyle(wxTE_READONLY);
	temp->Create(parent, wxID_ANY, wxString(""), wxDefaultPosition, size, 0, nullptr);
#else
	temp = new choice_ctrl(parent, wxID_ANY, wxString(""), wxDefaultPosition, size, 0, nullptr, wxCB_READONLY | wxBORDER_SIMPLE);
#endif //__WXOSX__

    temp->SetFont(Slic3r::GUI::wxGetApp().normal_font());
    if (!wxOSX) temp->SetBackgroundStyle(wxBG_STYLE_PAINT);

    temp->Append(ObjectManipulation::coordinate_type_str(ECoordinatesType::World));
    temp->Append(ObjectManipulation::coordinate_type_str(ECoordinatesType::Instance));
    temp->Append(ObjectManipulation::coordinate_type_str(ECoordinatesType::Local));
    temp->Select((int)ECoordinatesType::World);

    temp->SetToolTip(_L("Select coordinate space, in which the transformation will be performed."));
	return temp;
}

void msw_rescale_word_local_combo(choice_ctrl* combo)
{
#ifdef __WXOSX__
    const wxString selection = combo->GetString(combo->GetSelection());

    /* To correct scaling (set new controll size) of a wxBitmapCombobox
     * we need to refill control with new bitmaps. So, in our case :
     * 1. clear control
     * 2. add content
     * 3. add scaled "empty" bitmap to the at least one item
     */
    combo->Clear();
    wxSize size(wxDefaultSize);
    size.SetWidth(15 * wxGetApp().em_unit());

    // Set rescaled min height to correct layout
    combo->SetMinSize(wxSize(-1, int(1.5f*combo->GetFont().GetPixelSize().y + 0.5f)));
    // Set rescaled size
    combo->SetSize(size);
    
    combo->Append(ObjectManipulation::coordinate_type_str(ECoordinatesType::World));
    combo->Append(ObjectManipulation::coordinate_type_str(ECoordinatesType::Instance));
    combo->Append(ObjectManipulation::coordinate_type_str(ECoordinatesType::Local));

    combo->SetValue(selection);
#else
#ifdef _WIN32
    combo->Rescale();
#endif
    combo->SetMinSize(wxSize(15 * wxGetApp().em_unit(), -1));
#endif
}

static void set_font_and_background_style(wxWindow* win, const wxFont& font)
{
    win->SetFont(font);
    win->SetBackgroundStyle(wxBG_STYLE_PAINT);
}

static const wxString axes_color_text[] = { "#990000", "#009900", "#000099" };
static const wxString axes_color_back[] = { "#f5dcdc", "#dcf5dc", "#dcdcf5" };

ObjectManipulation::ObjectManipulation(wxWindow* parent) :
    OG_Settings(parent, true)
{
    m_imperial_units = wxGetApp().app_config->get("use_inches") == "1";
    m_use_colors     = wxGetApp().app_config->get("color_mapinulation_panel") == "1";

    m_manifold_warning_bmp = ScalableBitmap(parent, "exclamation");

    // Load bitmaps to be used for the mirroring buttons:
    m_mirror_bitmap_on     = ScalableBitmap(parent, "mirroring_on");

    const int border = wxOSX ? 0 : 4;
    const int em = wxGetApp().em_unit();
    m_main_grid_sizer = new wxFlexGridSizer(2, 3, 3); // "Name/label", "String name / Editors"
    m_main_grid_sizer->SetFlexibleDirection(wxBOTH);

    // Add "Name" label with warning icon
    auto sizer = new wxBoxSizer(wxHORIZONTAL);

    m_fix_throught_netfab_bitmap = new wxStaticBitmap(parent, wxID_ANY, wxNullBitmap);
    if (is_windows10())
        m_fix_throught_netfab_bitmap->Bind(wxEVT_CONTEXT_MENU, [this](wxCommandEvent& e)
            {
                // if object/sub-object has no errors
                if (m_fix_throught_netfab_bitmap->GetBitmap().GetRefData() == wxNullBitmap.GetRefData())
                    return;

                wxGetApp().obj_list()->fix_through_netfabb();
                update_warning_icon_state(wxGetApp().obj_list()->get_mesh_errors_info());
            });

    sizer->Add(m_fix_throught_netfab_bitmap);

    auto name_label = new wxStaticText(m_parent, wxID_ANY, _L("Name")+":");
    set_font_and_background_style(name_label, wxGetApp().normal_font());
    name_label->SetToolTip(_L("Object name"));
    sizer->Add(name_label);

    m_main_grid_sizer->Add(sizer);

    // Add name of the item
    const wxSize name_size = wxSize(20 * em, wxDefaultCoord);
    m_item_name = new wxStaticText(m_parent, wxID_ANY, "", wxDefaultPosition, name_size, wxST_ELLIPSIZE_MIDDLE);
    set_font_and_background_style(m_item_name, wxGetApp().bold_font());

    m_main_grid_sizer->Add(m_item_name, 0, wxEXPAND);

    // Add labels grid sizer
    m_labels_grid_sizer = new wxFlexGridSizer(1, 3, 3); // "Name/label", "String name / Editors"
    m_labels_grid_sizer->SetFlexibleDirection(wxBOTH);

    // Add world local combobox
    m_word_local_combo = create_word_local_combo(parent);
    m_word_local_combo->Bind(wxEVT_COMBOBOX, ([this](wxCommandEvent& evt) { this->set_coordinates_type(evt.GetString()); }), m_word_local_combo->GetId());

    // Small trick to correct layouting in different view_mode :
    // Show empty string of a same height as a m_word_local_combo, when m_word_local_combo is hidden
    m_word_local_combo_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_empty_str = new wxStaticText(parent, wxID_ANY, "");
    m_word_local_combo_sizer->Add(m_word_local_combo);
    m_word_local_combo_sizer->Add(m_empty_str);
    m_word_local_combo_sizer->SetMinSize(wxSize(-1, m_word_local_combo->GetBestHeight(-1)));
    m_labels_grid_sizer->Add(m_word_local_combo_sizer);

    // Text trick to grid sizer layout:
    // Height of labels should be equivalent to the edit boxes
    int height = wxTextCtrl(parent, wxID_ANY, "Br").GetBestHeight(-1);
#ifdef __WXGTK__
    // On Linux button with bitmap has bigger height then regular button or regular TextCtrl 
    // It can cause a wrong alignment on show/hide of a reset buttons 
    const int bmp_btn_height = ScalableButton(parent, wxID_ANY, "undo") .GetBestHeight(-1);
    if (bmp_btn_height > height)
        height = bmp_btn_height;
#endif //__WXGTK__

    auto add_label = [this, height](wxStaticText** label, const std::string& name, wxSizer* reciver = nullptr)
    {
        *label = new wxStaticText(m_parent, wxID_ANY, _(name) + ":");
        set_font_and_background_style(*label, wxGetApp().normal_font());

        wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->SetMinSize(wxSize(-1, height));
        sizer->Add(*label, 0, wxALIGN_CENTER_VERTICAL);
      
        if (reciver)
            reciver->Add(sizer);
        else
            m_labels_grid_sizer->Add(sizer);

        m_rescalable_sizers.push_back(sizer);
    };

    // Add labels
    add_label(&m_move_Label,    L("Position"));
    add_label(&m_rotate_Label,  L("Rotation"));

    // additional sizer for lock and labels "Scale" & "Size"
    sizer = new wxBoxSizer(wxHORIZONTAL);

    m_lock_bnt = new LockButton(parent, wxID_ANY);
    m_lock_bnt->Bind(wxEVT_BUTTON, [this](wxCommandEvent& event) {
        event.Skip();
        wxTheApp->CallAfter([this]() { set_uniform_scaling(m_lock_bnt->IsLocked()); });
    });
    sizer->Add(m_lock_bnt, 0, wxALIGN_CENTER_VERTICAL);

    auto v_sizer = new wxGridSizer(1, 3, 3);

    add_label(&m_scale_Label,   L("Scale"), v_sizer);
    wxStaticText* size_Label {nullptr};
    add_label(&size_Label, L("Size [World]"), v_sizer);
    if (wxOSX) set_font_and_background_style(size_Label, wxGetApp().normal_font());

    sizer->Add(v_sizer, 0, wxLEFT, border);
    m_labels_grid_sizer->Add(sizer);
    m_main_grid_sizer->Add(m_labels_grid_sizer, 0, wxEXPAND);


    // Add editors grid sizer
    wxFlexGridSizer* editors_grid_sizer = new wxFlexGridSizer(5, 3, 3); // "Name/label", "String name / Editors"
    editors_grid_sizer->SetFlexibleDirection(wxBOTH);

    // Add Axes labels with icons
    static const char axes[] = { 'X', 'Y', 'Z' };
//    std::vector<wxString> axes_color = {"#EE0000", "#00EE00", "#0000EE"};
    for (size_t axis_idx = 0; axis_idx < sizeof(axes); axis_idx++) {
        const char label = axes[axis_idx];

        wxStaticText* axis_name = new wxStaticText(m_parent, wxID_ANY, wxString(label));
        set_font_and_background_style(axis_name, wxGetApp().bold_font());
        //if (m_use_colors)
        //    axis_name->SetForegroundColour(wxColour(axes_color_text[axis_idx]));

        sizer = new wxBoxSizer(wxHORIZONTAL);
        // Under OSX or Linux with GTK3 we use font, smaller than default font, so
        // there is a next trick for an equivalent layout of coordinates combobox and axes labels in they own sizers
        // if (wxOSX || wxGTK3)
        sizer->SetMinSize(-1, m_word_local_combo->GetBestHeight(-1));
        sizer->Add(axis_name, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, border);

        // We will add a button to toggle mirroring to each axis:
        auto btn = new ScalableButton(parent, wxID_ANY, "mirroring_off", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER | wxTRANSPARENT_WINDOW);
        btn->SetToolTip(format_wxstr(_L("Mirror along %1% axis"), label));
        m_mirror_buttons[axis_idx] = btn;

        sizer->AddStretchSpacer(2);
        sizer->Add(btn, 0, wxALIGN_CENTER_VERTICAL);

        btn->Bind(wxEVT_BUTTON, [this, axis_idx](wxCommandEvent&) {
            GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
            Selection& selection = canvas->get_selection();
            TransformationType transformation_type;
            if (is_local_coordinates())
                transformation_type.set_local();
            else if (is_instance_coordinates())
                transformation_type.set_instance();

            transformation_type.set_relative();

            selection.setup_cache();
            selection.mirror((Axis)axis_idx);

            // Copy mirroring values from GLVolumes into Model (ModelInstance / ModelVolume), trigger background processing.
            canvas->do_mirror(L("Set Mirror"));
            UpdateAndShow(true);
        });

        editors_grid_sizer->Add(sizer, 0, wxALIGN_CENTER_HORIZONTAL);
    }

    m_mirror_warning_bitmap = new wxStaticBitmap(parent, wxID_ANY, wxNullBitmap);
    editors_grid_sizer->Add(m_mirror_warning_bitmap, 0, wxALIGN_CENTER_VERTICAL);
    editors_grid_sizer->AddStretchSpacer(1);

    // add EditBoxes 
    auto add_edit_boxes = [this, editors_grid_sizer](const std::string& opt_key, int axis)
    {
        ManipulationEditor* editor = new ManipulationEditor(this, opt_key, axis);
        m_editors.push_back(editor);

        editors_grid_sizer->Add(editor, 0, wxALIGN_CENTER_VERTICAL);
    };
    
    // add Units 
    auto add_unit_text = [this, parent, editors_grid_sizer, height](std::string unit, wxStaticText** unit_text)
    {
        *unit_text = new wxStaticText(parent, wxID_ANY, _(unit));
        set_font_and_background_style(*unit_text, wxGetApp().normal_font()); 

        // Unit text should be the same height as labels      
        wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->SetMinSize(wxSize(-1, height));
        sizer->Add(*unit_text, 0, wxALIGN_CENTER_VERTICAL);

        editors_grid_sizer->Add(sizer);
        m_rescalable_sizers.push_back(sizer);
    };

    for (size_t axis_idx = 0; axis_idx < sizeof(axes); axis_idx++)
        add_edit_boxes("position", axis_idx);
    add_unit_text(m_imperial_units ? L("in") : L("mm"), &m_position_unit);

    // Add drop to bed button
    m_drop_to_bed_button = new ScalableButton(parent, wxID_ANY, ScalableBitmap(parent, "drop_to_bed"));
    m_drop_to_bed_button->SetToolTip(_L("Drop to bed"));
    m_drop_to_bed_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent& e) {
        // ???
        GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
        Selection& selection = canvas->get_selection();

        if (selection.is_single_volume_or_modifier()) {
            const GLVolume* volume = selection.get_first_volume();
            const double min_z = get_volume_min_z(*volume);
            if (!is_world_coordinates()) {
                const Vec3d diff = m_cache.position - volume->get_instance_transformation().get_matrix_no_offset().inverse() * (min_z * Vec3d::UnitZ());

                Plater::TakeSnapshot snapshot(wxGetApp().plater(), _L("Drop to bed"));
                change_position_value(0, diff.x());
                change_position_value(1, diff.y());
                change_position_value(2, diff.z());
            }
            else {
                Plater::TakeSnapshot snapshot(wxGetApp().plater(), _L("Drop to bed"));
                change_position_value(2, m_cache.position.z() - min_z);
            }
        }
        else if (selection.is_single_full_instance()) {
            const double min_z = selection.get_scaled_instance_bounding_box().min.z();
            if (!is_world_coordinates()) {
                const GLVolume* volume = selection.get_first_volume();
                const Vec3d diff = m_cache.position - volume->get_instance_transformation().get_matrix_no_offset().inverse() * (min_z * Vec3d::UnitZ());

                Plater::TakeSnapshot snapshot(wxGetApp().plater(), _L("Drop to bed"));
                change_position_value(0, diff.x());
                change_position_value(1, diff.y());
                change_position_value(2, diff.z());
            }
            else {
                Plater::TakeSnapshot snapshot(wxGetApp().plater(), _L("Drop to bed"));
                change_position_value(2, m_cache.position.z() - min_z);
            }
        }
        });
    editors_grid_sizer->Add(m_drop_to_bed_button);

    for (size_t axis_idx = 0; axis_idx < sizeof(axes); axis_idx++)
        add_edit_boxes("rotation", axis_idx);
    wxStaticText* rotation_unit{ nullptr };
    add_unit_text("°", &rotation_unit);

    // Add reset rotation button
    m_reset_rotation_button = new ScalableButton(parent, wxID_ANY, ScalableBitmap(parent, "undo"));
    m_reset_rotation_button->SetToolTip(_L("Reset rotation"));
    m_reset_rotation_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
        GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
        Selection& selection = canvas->get_selection();
        selection.setup_cache();
        if (selection.is_single_volume_or_modifier()) {
            GLVolume* vol = const_cast<GLVolume*>(selection.get_first_volume());
            Geometry::Transformation trafo = vol->get_volume_transformation();
            trafo.reset_rotation();
            vol->set_volume_transformation(trafo);
        }
        else if (selection.is_single_full_instance()) {
            Geometry::Transformation trafo = selection.get_first_volume()->get_instance_transformation();
            trafo.reset_rotation();
            for (unsigned int idx : selection.get_volume_idxs()) {
                const_cast<GLVolume*>(selection.get_volume(idx))->set_instance_transformation(trafo);
            }
        }
        else
            return;

        // Synchronize instances/volumes.

        selection.synchronize_unselected_instances(Selection::SyncRotationType::RESET);
        selection.synchronize_unselected_volumes();

        // Copy rotation values from GLVolumes into Model (ModelInstance / ModelVolume), trigger background processing.
        canvas->do_rotate(L("Reset Rotation"));

        UpdateAndShow(true);
    });
    editors_grid_sizer->Add(m_reset_rotation_button);

    for (size_t axis_idx = 0; axis_idx < sizeof(axes); axis_idx++)
        add_edit_boxes("scale", axis_idx);
    wxStaticText* scale_unit{ nullptr };
    add_unit_text("%", &scale_unit);

    // Add reset scale button
    m_reset_scale_button = new ScalableButton(parent, wxID_ANY, ScalableBitmap(parent, "undo"));
    m_reset_scale_button->SetToolTip(_L("Reset scale"));
    m_reset_scale_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
        GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
        Selection& selection = canvas->get_selection();
        selection.setup_cache();
        if (selection.is_single_volume_or_modifier()) {
            GLVolume* vol = const_cast<GLVolume*>(selection.get_first_volume());
            Geometry::Transformation trafo = vol->get_volume_transformation();
            trafo.reset_scaling_factor();
            vol->set_volume_transformation(trafo);
        }
        else if (selection.is_single_full_instance()) {
            Geometry::Transformation trafo = selection.get_first_volume()->get_instance_transformation();
            trafo.reset_scaling_factor();
            for (unsigned int idx : selection.get_volume_idxs()) {
                const_cast<GLVolume*>(selection.get_volume(idx))->set_instance_transformation(trafo);
            }
        }
        else
            return;

        // Synchronize instances/volumes.
        selection.synchronize_unselected_instances(Selection::SyncRotationType::GENERAL);
        selection.synchronize_unselected_volumes();

        canvas->do_scale(L("Reset scale"));
        UpdateAndShow(true);
        });
    editors_grid_sizer->Add(m_reset_scale_button);

    for (size_t axis_idx = 0; axis_idx < sizeof(axes); axis_idx++)
        add_edit_boxes("size", axis_idx);
    add_unit_text(m_imperial_units ? L("in") : L("mm"), &m_size_unit);
    editors_grid_sizer->AddStretchSpacer(1);

    m_main_grid_sizer->Add(editors_grid_sizer, 1, wxEXPAND);

    m_skew_label = new wxStaticText(parent, wxID_ANY, _L("Skew [World]"));
    m_main_grid_sizer->Add(m_skew_label, 1, wxEXPAND);

    m_reset_skew_button = new ScalableButton(parent, wxID_ANY, ScalableBitmap(parent, "undo"));
    m_reset_skew_button->SetToolTip(_L("Reset skew"));
    m_reset_skew_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
        GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
        Selection& selection = canvas->get_selection();
        if (selection.is_single_full_instance() || selection.is_single_volume_or_modifier()) {
            selection.setup_cache();
            selection.reset_skew();
            canvas->do_reset_skew(L("Reset skew"));
            UpdateAndShow(true);
        }
        });
    m_main_grid_sizer->Add(m_reset_skew_button);

    m_check_inch = new wxCheckBox(parent, wxID_ANY, _L("Inches"));
    m_check_inch->SetFont(wxGetApp().normal_font());

    m_check_inch->SetValue(m_imperial_units);
    m_check_inch->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
        wxGetApp().app_config->set("use_inches", m_check_inch->GetValue() ? "1" : "0");
        wxGetApp().sidebar().update_ui_from_settings();
    });

    m_main_grid_sizer->Add(m_check_inch, 1, wxEXPAND);

    m_og->activate();
    m_og->sizer->Clear(true);
    m_og->sizer->Add(m_main_grid_sizer, 1, wxEXPAND | wxALL, border);
}

void ObjectManipulation::Show(const bool show)
{
    if (show != IsShown()) {
        // Show all lines of the panel. Some of these lines will be hidden in the lines below.
        m_og->Show(show);

        if (show && wxGetApp().get_mode() != comSimple) {
            // Show the label and the name of the STL in simple mode only.
            // Label "Name: "
            m_main_grid_sizer->Show(size_t(0), false);
            // The actual name of the STL.
            m_main_grid_sizer->Show(size_t(1), false);
        }
    }

    if (show) {
        ECoordinatesType coordinates_type = m_coordinates_type;

        // Show the "World Coordinates" / "Local Coordintes" Combo in Advanced / Expert mode only.
        const Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();
        bool show_world_local_combo = wxGetApp().get_mode() != comSimple && (selection.is_single_full_instance() || selection.is_single_volume_or_modifier());
        if (selection.is_single_volume_or_modifier() && m_word_local_combo->GetCount() < 3) {
#ifdef __linux__
            m_word_local_combo->Insert(coordinate_type_str(ECoordinatesType::Local), 2);
#else
            m_word_local_combo->Insert(coordinate_type_str(ECoordinatesType::Local), wxNullBitmap, 2);
#endif // __linux__
        }
        else if (selection.is_single_full_instance() && m_word_local_combo->GetCount() > 2) {
            m_word_local_combo->Delete(2);
            if (coordinates_type > ECoordinatesType::Instance)
                coordinates_type = ECoordinatesType::World;
        }
        m_word_local_combo->Show(show_world_local_combo);
        m_empty_str->Show(!show_world_local_combo);
    }
}

bool ObjectManipulation::IsShown()
{
	return dynamic_cast<const wxStaticBoxSizer*>(m_og->sizer)->GetStaticBox()->IsShown(); //  m_og->get_grid_sizer()->IsShown(2);
}

void ObjectManipulation::UpdateAndShow(const bool show)
{
	if (show) {
        this->set_dirty();
		this->update_if_dirty();
	}

    OG_Settings::UpdateAndShow(show);
}

void ObjectManipulation::Enable(const bool enable)
{
    m_is_enabled = m_is_enabled_size_and_scale = enable;
    for (wxWindow* win : std::initializer_list<wxWindow*>{ m_reset_scale_button, m_reset_rotation_button, m_drop_to_bed_button, m_check_inch, m_lock_bnt
      , m_reset_skew_button })
        win->Enable(enable);
}

void ObjectManipulation::DisableScale()
{
    m_is_enabled = true;
    m_is_enabled_size_and_scale = false;
    for (wxWindow* win : std::initializer_list<wxWindow*>{ m_reset_scale_button, m_lock_bnt, m_reset_skew_button })
        win->Enable(false);
}

void ObjectManipulation::DisableUnuniformScale()
{
    m_lock_bnt->Enable(false);
}

void ObjectManipulation::update_ui_from_settings()
{
    if (m_imperial_units != wxGetApp().app_config->get_bool("use_inches")) {
        m_imperial_units  = wxGetApp().app_config->get_bool("use_inches");

        auto update_unit_text = [](const wxString& new_unit_text, wxStaticText* widget) {
            widget->SetLabel(new_unit_text);
            if (wxOSX) set_font_and_background_style(widget, wxGetApp().normal_font());
        };
        update_unit_text(m_imperial_units ? _L("in") : _L("mm"), m_position_unit);
        update_unit_text(m_imperial_units ? _L("in") : _L("mm"), m_size_unit);

        for (int i = 0; i < 3; ++i) {
            auto update = [this, i](/*ManipulationEditorKey*/int key_id, const Vec3d& new_value) {
                double value = new_value(i);
                if (m_imperial_units)
                    value *= mm_to_in;
                wxString new_text = double_to_string(value, m_imperial_units && key_id == 3/*meSize*/ ? 4 : 2);
                const int id = key_id * 3 + i;
                if (id >= 0) m_editors[id]->set_value(new_text);
            };
            update(0/*mePosition*/, m_new_position);
            update(3/*meSize*/,     m_new_size);
        }
    }
    m_check_inch->SetValue(m_imperial_units);

    if (m_use_colors != wxGetApp().app_config->get_bool("color_mapinulation_panel")) {
        m_use_colors  = wxGetApp().app_config->get_bool("color_mapinulation_panel");
        // update colors for edit-boxes
        int axis_id = 0;
        for (ManipulationEditor* editor : m_editors) {
//            editor->SetForegroundColour(m_use_colors ? wxColour(axes_color_text[axis_id]) : wxGetApp().get_label_clr_default());
            if (m_use_colors) {
                editor->SetBackgroundColour(wxColour(axes_color_back[axis_id]));
                if (wxGetApp().dark_mode())
                    editor->SetForegroundColour(*wxBLACK);
            }
            else {
#ifdef _WIN32
                wxGetApp().UpdateDarkUI(editor);
#else
                editor->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
                editor->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
#endif /* _WIN32 */
            }
            editor->Refresh();
            if (++axis_id == 3)
                axis_id = 0;
        }
    }
}

void ObjectManipulation::update_settings_value(const Selection& selection)
{
    if (selection.is_empty()) {
        // No selection, reset the cache.
        reset_settings_value();
        return;
    }

    m_new_move_label_string   = L("Position");
    m_new_rotate_label_string = L("Rotation");
    m_new_scale_label_string  = L("Scale factors");

    ObjectList* obj_list = wxGetApp().obj_list();
    if (selection.is_single_full_instance()) {
        // all volumes in the selection belongs to the same instance, any of them contains the needed instance data, so we take the first one
        const GLVolume* volume = selection.get_first_volume();

        if (is_world_coordinates()) {
            m_new_position = volume->get_instance_offset();
            m_new_size = selection.get_bounding_box_in_current_reference_system().first.size();
            m_new_scale = m_new_size.cwiseQuotient(selection.get_unscaled_instance_bounding_box().size()) * 100.0;
            m_new_rotate_label_string = L("Rotate (relative)");
            m_new_rotation = Vec3d::Zero();
        }
        else {
            m_new_move_label_string = L("Translate (relative) [World]");
            m_new_rotate_label_string = L("Rotate (relative)");
            m_new_position = Vec3d::Zero();
            m_new_rotation = Vec3d::Zero();
            m_new_size = selection.get_bounding_box_in_current_reference_system().first.size();
            m_new_scale = m_new_size.cwiseQuotient(selection.get_full_unscaled_instance_local_bounding_box().size()) * 100.0;
        }

        m_new_enabled  = true;
    }
    else if (selection.is_single_full_object() && obj_list->is_selected(itObject)) {
        const BoundingBoxf3& box = selection.get_bounding_box();
        m_new_position = box.center();
        m_new_rotation = Vec3d::Zero();
        m_new_scale    = Vec3d(100.0, 100.0, 100.0);
        m_new_size = selection.get_bounding_box_in_current_reference_system().first.size();
        m_new_rotate_label_string = L("Rotate");
        m_new_scale_label_string  = L("Scale");
        m_new_enabled  = true;
    }
    else if (selection.is_single_volume_or_modifier()) {
        // the selection contains a single volume
        const GLVolume* volume = selection.get_first_volume();
        if (is_world_coordinates()) {
            const Geometry::Transformation trafo(volume->world_matrix());

            const Vec3d& offset = trafo.get_offset();

            m_new_position = offset;
            m_new_rotate_label_string = L("Rotate (relative)");
            m_new_scale_label_string = L("Scale");
            m_new_scale = Vec3d(100.0, 100.0, 100.0);
            m_new_rotation = Vec3d::Zero();
            m_new_size = selection.get_bounding_box_in_current_reference_system().first.size();
        }
        else if (is_local_coordinates()) {
            m_new_move_label_string = L("Translate (relative) [World]");
            m_new_rotate_label_string = L("Rotate (relative)");
            m_new_position = Vec3d::Zero();
            m_new_rotation = Vec3d::Zero();
            m_new_scale = volume->get_volume_scaling_factor() * 100.0;
            m_new_size = selection.get_bounding_box_in_current_reference_system().first.size();
        }
        else {
            m_new_position = volume->get_volume_offset();
            m_new_rotate_label_string = L("Rotate (relative)");
            m_new_rotation = Vec3d::Zero();
            m_new_scale_label_string = L("Scale");
            m_new_scale = Vec3d(100.0, 100.0, 100.0);
            m_new_size = selection.get_bounding_box_in_current_reference_system().first.size();
        }
        m_new_enabled = true;
    }
    else if (obj_list->is_connectors_item_selected() || obj_list->multiple_selection() || obj_list->is_selected(itInstanceRoot)) {
        reset_settings_value();
        m_new_move_label_string   = L("Translate");
        m_new_rotate_label_string = L("Rotate");
        m_new_scale_label_string  = L("Scale");
        m_new_size = selection.get_bounding_box_in_current_reference_system().first.size();
        m_new_enabled  = true;
    }
}

void ObjectManipulation::update_if_dirty()
{
    if (! m_dirty)
        return;

    const Selection &selection = wxGetApp().plater()->canvas3D()->get_selection();
    this->update_settings_value(selection);

    auto update_label = [](wxString &label_cache, const std::string &new_label, wxStaticText *widget) {
        wxString new_label_localized = _(new_label) + ":";
        if (label_cache != new_label_localized) {
            label_cache = new_label_localized;
            widget->SetLabel(new_label_localized);
            if (wxOSX) set_font_and_background_style(widget, wxGetApp().normal_font());
        }
    };
    update_label(m_cache.move_label_string,   m_new_move_label_string,   m_move_Label);
    update_label(m_cache.rotate_label_string, m_new_rotate_label_string, m_rotate_Label);
    update_label(m_cache.scale_label_string,  m_new_scale_label_string,  m_scale_Label);

    enum ManipulationEditorKey
    {
        mePosition = 0,
        meRotation,
        meScale,
        meSize
    };

    for (int i = 0; i < 3; ++ i) {
        auto update = [this, i](Vec3d &cached, Vec3d &cached_rounded, ManipulationEditorKey key_id, const Vec3d &new_value) {
			wxString new_text = double_to_string(new_value(i), m_imperial_units && key_id == meSize ? 4 : 2);
			double new_rounded;
			new_text.ToDouble(&new_rounded);
			if (std::abs(cached_rounded(i) - new_rounded) > EPSILON) {
				cached_rounded(i) = new_rounded;
                const int id = key_id*3+i;
                if (m_imperial_units) {
                    double inch_value = new_value(i) * mm_to_in;
                    if (key_id == mePosition)
                        new_text = double_to_string(inch_value, 2);
                    if (key_id == meSize) {
                        if(std::abs(m_cache.size_inches(i) - inch_value) > EPSILON)
                            m_cache.size_inches(i) = inch_value;
                        new_text = double_to_string(inch_value, 4);
                    }
                }
                if (id >= 0) m_editors[id]->set_value(new_text);
            }
			cached(i) = new_value(i);
		};
        update(m_cache.position, m_cache.position_rounded, mePosition, m_new_position);
        update(m_cache.scale,    m_cache.scale_rounded,    meScale,    m_new_scale);
        update(m_cache.size,     m_cache.size_rounded,     meSize,     m_new_size);
        update(m_cache.rotation, m_cache.rotation_rounded, meRotation, m_new_rotation);
    }

    m_lock_bnt->SetLock(m_uniform_scale);
    m_lock_bnt->SetToolTip(wxEmptyString);
    m_lock_bnt->enable();

    if (m_new_enabled)
        m_og->enable();
    else
        m_og->disable();

    if (!wxGetApp().plater()->canvas3D()->is_dragging()) {
        update_reset_buttons_visibility();
        update_mirror_buttons_visibility();
    }

    m_dirty = false;
}

void ObjectManipulation::update_reset_buttons_visibility()
{
    GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
    if (!canvas)
        return;

    bool show_drop_to_bed = false;
    bool show_rotation = false;
    bool show_scale = false;
    bool show_mirror = false;
    bool show_skew = false;

    const Selection& selection = canvas->get_selection();
    if (selection.is_single_full_instance() || selection.is_single_volume_or_modifier()) {
        const double min_z = selection.is_single_full_instance() ? selection.get_scaled_instance_bounding_box().min.z() :
            get_volume_min_z(*selection.get_first_volume());

        show_drop_to_bed = std::abs(min_z) > EPSILON;
        const GLVolume* volume = selection.get_first_volume();
        const Geometry::Transformation trafo = selection.is_single_full_instance() ? volume->get_instance_transformation() : volume->get_volume_transformation();

        const Geometry::TransformationSVD trafo_svd(trafo);
        show_rotation = trafo_svd.rotation;
        show_scale    = trafo_svd.scale;
        show_mirror   = trafo_svd.mirror;
        show_skew     = Geometry::TransformationSVD(volume->world_matrix()).skew;
    }

    wxGetApp().CallAfter([this, show_drop_to_bed, show_rotation, show_scale, show_mirror, show_skew] {
        // There is a case (under OSX), when this function is called after the Manipulation panel is hidden
        // So, let check if Manipulation panel is still shown for this moment
        if (!this->IsShown())
            return;
        m_drop_to_bed_button->Show(show_drop_to_bed);
        m_reset_rotation_button->Show(show_rotation);
        m_reset_scale_button->Show(show_scale);
        m_mirror_warning_bitmap->SetBitmap(show_mirror ? m_manifold_warning_bmp.bmp() : wxNullBitmap);
        m_mirror_warning_bitmap->SetMinSize(show_mirror ? m_manifold_warning_bmp.GetSize() : wxSize(0, 0));
        m_mirror_warning_bitmap->SetToolTip(show_mirror ? _L("Left handed") : "");
        m_reset_skew_button->Show(show_skew);
        m_skew_label->Show(show_skew);

        // Because of CallAfter we need to layout sidebar after Show/hide of reset buttons one more time
        Sidebar& panel = wxGetApp().sidebar();
        if (!panel.IsFrozen()) {
            panel.Freeze();
            panel.Layout();
            panel.Thaw();
        }
    });
}

void ObjectManipulation::update_mirror_buttons_visibility()
{
    const bool can_mirror = wxGetApp().plater()->can_mirror();
    for (ScalableButton* button : m_mirror_buttons) {
        button->Enable(can_mirror);
    }
}




#ifndef __APPLE__
void ObjectManipulation::emulate_kill_focus()
{
    if (!m_focused_editor)
        return;

    m_focused_editor->kill_focus(this);
}
#endif // __APPLE__

void ObjectManipulation::update_item_name(const wxString& item_name)
{
    m_item_name->SetLabel(item_name);
}

void ObjectManipulation::update_warning_icon_state(const MeshErrorsInfo& warning)
{   
    if (const std::string& warning_icon_name = warning.warning_icon_name;
        !warning_icon_name.empty())
        m_manifold_warning_bmp = ScalableBitmap(m_parent, warning_icon_name);
    const wxString& tooltip = warning.tooltip;
    m_fix_throught_netfab_bitmap->SetBitmap(tooltip.IsEmpty() ? wxNullBitmap : m_manifold_warning_bmp.bmp());
    m_fix_throught_netfab_bitmap->SetMinSize(tooltip.IsEmpty() ? wxSize(0,0) : m_manifold_warning_bmp.GetSize());
    m_fix_throught_netfab_bitmap->SetToolTip(tooltip);
}

wxString ObjectManipulation::coordinate_type_str(ECoordinatesType type)
{
    switch (type)
    {
    case ECoordinatesType::World:    { return _L("World coordinates"); }
    case ECoordinatesType::Instance: { return _L("Object coordinates"); }
    case ECoordinatesType::Local:    { return _L("Part coordinates"); }
    default:                         { assert(false); return _L("Unknown"); }
    }
}

#if ENABLE_OBJECT_MANIPULATION_DEBUG
void ObjectManipulation::render_debug_window()
{
    ImGuiWrapper& imgui = *wxGetApp().imgui();
//   ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);
    imgui.begin(std::string("ObjectManipulation"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);
    imgui.text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, "Coordinates type");
    ImGui::SameLine();
    imgui.text(coordinate_type_str(m_coordinates_type));
    imgui.end();
}
#endif // ENABLE_OBJECT_MANIPULATION_DEBUG

void ObjectManipulation::reset_settings_value()
{
    m_new_position = Vec3d::Zero();
    m_new_rotation = Vec3d::Zero();
    m_new_scale = Vec3d::Ones() * 100.;
    m_new_size = Vec3d::Zero();
    m_new_enabled = false;
    // no need to set the dirty flag here as this method is called from update_settings_value(),
    // which is called from update_if_dirty(), which resets the dirty flag anyways.
//    m_dirty = true;
}

void ObjectManipulation::change_position_value(int axis, double value)
{
    if (std::abs(m_cache.position_rounded(axis) - value) < EPSILON)
        return;

    Vec3d position = m_cache.position;
    position(axis) = value;

    auto canvas = wxGetApp().plater()->canvas3D();
    Selection& selection = canvas->get_selection();
    selection.setup_cache();
    TransformationType trafo_type;
    trafo_type.set_relative();
    switch (get_coordinates_type())
    {
    case ECoordinatesType::Instance: { trafo_type.set_instance(); break; }
    case ECoordinatesType::Local:    { trafo_type.set_local(); break; }
    default:                         { break; }
    }
    selection.translate(position - m_cache.position, trafo_type);
    canvas->do_move(L("Set Position"));

    m_cache.position = position;
	m_cache.position_rounded(axis) = DBL_MAX;
    this->UpdateAndShow(true);
}

void ObjectManipulation::change_rotation_value(int axis, double value)
{
    if (std::abs(m_cache.rotation_rounded(axis) - value) < EPSILON)
        return;

    Vec3d rotation = m_cache.rotation;
    rotation(axis) = value;

    GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
    Selection& selection = canvas->get_selection();

    TransformationType transformation_type;
    transformation_type.set_relative();
    if (selection.is_single_full_instance())
        transformation_type.set_independent();

    if (is_local_coordinates())
        transformation_type.set_local();

    if (is_instance_coordinates())
        transformation_type.set_instance();

    selection.setup_cache();
	selection.rotate(
		(M_PI / 180.0) * (transformation_type.absolute() ? rotation : rotation - m_cache.rotation), 
		transformation_type);
    canvas->do_rotate(L("Set Orientation"));

    m_cache.rotation = rotation;
	m_cache.rotation_rounded(axis) = DBL_MAX;
    this->UpdateAndShow(true);
}

void ObjectManipulation::change_scale_value(int axis, double value)
{
    if (value <= 0.0)
        return;

    if (std::abs(m_cache.scale_rounded(axis) - value) < EPSILON)
        return;

    Vec3d scale = m_cache.scale;
    scale(axis) = value;

    const Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();
    Vec3d ref_scale = m_cache.scale;
    if (selection.is_single_volume_or_modifier()) {
        scale = scale.cwiseQuotient(ref_scale);
        ref_scale = Vec3d::Ones();
    }
    else if (selection.is_single_full_instance())
        ref_scale = 100.0 * Vec3d::Ones();

    this->do_scale(axis, scale.cwiseQuotient(ref_scale));

    m_cache.scale = scale;
    m_cache.scale_rounded(axis) = DBL_MAX;
    this->UpdateAndShow(true);
}


void ObjectManipulation::change_size_value(int axis, double value)
{
    if (value <= 0.0)
        return;

    if (m_imperial_units) {
        if (std::abs(m_cache.size_inches(axis) - value) < EPSILON)
            return;
        m_cache.size_inches(axis) = value;
        value *= in_to_mm;
    }

    if (std::abs(m_cache.size_rounded(axis) - value) < EPSILON)
        return;

    Vec3d size = m_cache.size;
    size(axis) = value;

    const Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();

    Vec3d ref_size = m_cache.size;
    if (selection.is_single_volume_or_modifier()) {
        size = size.cwiseQuotient(ref_size);
        ref_size = Vec3d::Ones();
    }
    else if (selection.is_single_full_instance()) {
        if (is_world_coordinates())
            ref_size = selection.get_full_unscaled_instance_bounding_box().size();
        else
            ref_size = selection.get_full_unscaled_instance_local_bounding_box().size();
    }

    this->do_size(axis, size.cwiseQuotient(ref_size));

    m_cache.size = size;
    m_cache.size_rounded(axis) = DBL_MAX;
    this->UpdateAndShow(true);
}

void ObjectManipulation::do_scale(int axis, const Vec3d &scale) const
{
    TransformationType transformation_type;
    if (is_local_coordinates())
        transformation_type.set_local();
    else if (is_instance_coordinates())
        transformation_type.set_instance();

    Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();
    if (selection.is_single_volume_or_modifier() && !is_local_coordinates())
        transformation_type.set_relative();

    const Vec3d scaling_factor = m_uniform_scale ? scale(axis) * Vec3d::Ones() : scale;
    selection.setup_cache();
    selection.scale(scaling_factor, transformation_type);
    wxGetApp().plater()->canvas3D()->do_scale(L("Set Scale"));
}

void ObjectManipulation::do_size(int axis, const Vec3d& scale) const
{
    Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();

    TransformationType transformation_type;
    if (is_local_coordinates())
        transformation_type.set_local();
    else if (is_instance_coordinates())
        transformation_type.set_instance();

    const Vec3d scaling_factor = m_uniform_scale ? scale(axis) * Vec3d::Ones() : scale;
    selection.setup_cache();
    selection.scale(scaling_factor, transformation_type);
    wxGetApp().plater()->canvas3D()->do_scale(L("Set Size"));
}

void ObjectManipulation::on_change(const std::string& opt_key, int axis, double new_value)
{
    if (!m_cache.is_valid())
        return;

    if (opt_key == "position") {
        if (m_imperial_units)
            new_value *= in_to_mm;
        change_position_value(axis, new_value);
    }
    else if (opt_key == "rotation")
        change_rotation_value(axis, new_value);
    else if (opt_key == "scale") {
        if (new_value > 0.0)
            change_scale_value(axis, new_value);
        else {
            new_value = m_cache.scale(axis);
            m_cache.scale(axis) = 0.0;
            m_cache.scale_rounded(axis) = DBL_MAX;
            change_scale_value(axis, new_value);
        }
    }
    else if (opt_key == "size") {
        if (new_value > 0.0)
            change_size_value(axis, new_value);
        else {
            Vec3d& size = m_imperial_units ? m_cache.size_inches : m_cache.size;
            new_value = size(axis);
            size(axis) = 0.0;
            m_cache.size_rounded(axis) = DBL_MAX;
            change_size_value(axis, new_value);
        }
    }
}

void ObjectManipulation::set_uniform_scaling(const bool use_uniform_scale)
{ 
    if (!use_uniform_scale)
        // Recalculate cached values at this panel, refresh the screen.
        this->UpdateAndShow(true);

    m_uniform_scale = use_uniform_scale;
    set_dirty();
}

void ObjectManipulation::set_coordinates_type(ECoordinatesType type)
{
    if (wxGetApp().get_mode() == comSimple)
        type = ECoordinatesType::World;

    if (m_coordinates_type == type)
        return;

    m_coordinates_type = type;
    m_word_local_combo->SetSelection((int)m_coordinates_type);
    this->UpdateAndShow(true);
    GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
    canvas->get_gizmos_manager().update_data();
    canvas->set_as_dirty();
    canvas->request_extra_frame();
}

ECoordinatesType ObjectManipulation::get_coordinates_type() const
{
    return m_coordinates_type;
}

void ObjectManipulation::msw_rescale()
{
    const int em = wxGetApp().em_unit();
    m_item_name->SetMinSize(wxSize(20*em, wxDefaultCoord));
    msw_rescale_word_local_combo(m_word_local_combo);
    m_word_local_combo_sizer->SetMinSize(wxSize(-1, m_word_local_combo->GetBestHeight(-1)));

    const wxString& tooltip = m_fix_throught_netfab_bitmap->GetToolTipText();
    m_fix_throught_netfab_bitmap->SetBitmap(tooltip.IsEmpty() ? wxNullBitmap : m_manifold_warning_bmp.bmp());
    m_fix_throught_netfab_bitmap->SetMinSize(tooltip.IsEmpty() ? wxSize(0, 0) : m_manifold_warning_bmp.GetSize());

    // rescale label-heights
    // Text trick to grid sizer layout:
    // Height of labels should be equivalent to the edit boxes
    const int height = wxTextCtrl(parent(), wxID_ANY, "Br").GetBestHeight(-1);
    for (wxBoxSizer* sizer : m_rescalable_sizers)
        sizer->SetMinSize(wxSize(-1, height));

    // rescale edit-boxes
    for (ManipulationEditor* editor : m_editors)
        editor->msw_rescale();

    // rescale "inches" checkbox
    m_check_inch->SetInitialSize(m_check_inch->GetBestSize());

    get_og()->msw_rescale();
}

void ObjectManipulation::sys_color_changed()
{
#ifdef _WIN32
    get_og()->sys_color_changed();
    wxGetApp().UpdateDarkUI(m_word_local_combo);
    wxGetApp().UpdateDarkUI(m_check_inch);
#endif
    for (ManipulationEditor* editor : m_editors)
        editor->sys_color_changed(this);

    m_mirror_bitmap_on.sys_color_changed();
    m_reset_scale_button->sys_color_changed();
    m_reset_rotation_button->sys_color_changed();
    m_drop_to_bed_button->sys_color_changed();
    m_lock_bnt->sys_color_changed();

    for (int id = 0; id < 3; ++id) {
        m_mirror_buttons[id]->sys_color_changed();
    }
}

void ObjectManipulation::set_coordinates_type(const wxString& type_string)
{
  if (type_string == coordinate_type_str(ECoordinatesType::Instance))
      this->set_coordinates_type(ECoordinatesType::Instance);
  else if (type_string == coordinate_type_str(ECoordinatesType::Local))
      this->set_coordinates_type(ECoordinatesType::Local);
  else
      this->set_coordinates_type(ECoordinatesType::World);
}

static const char axes[] = { 'x', 'y', 'z' };
ManipulationEditor::ManipulationEditor(ObjectManipulation* parent,
                                       const std::string& opt_key,
                                       int axis) :
    wxTextCtrl(parent->parent(), wxID_ANY, wxEmptyString, wxDefaultPosition,
        wxSize((wxOSX ? 5 : 6)*int(wxGetApp().em_unit()), wxDefaultCoord), wxTE_PROCESS_ENTER
#ifdef _WIN32
        | wxBORDER_SIMPLE
#endif 
    ),
    m_opt_key(opt_key),
    m_axis(axis)
{
    set_font_and_background_style(this, wxGetApp().normal_font());
#ifdef __WXOSX__
    this->OSXDisableAllSmartSubstitutions();
#endif // __WXOSX__
    if (parent->use_colors()) {
        this->SetBackgroundColour(wxColour(axes_color_back[axis]));
        this->SetForegroundColour(*wxBLACK);
    } else {
        wxGetApp().UpdateDarkUI(this);
    }

    // A name used to call handle_sidebar_focus_event()
    m_full_opt_name = m_opt_key+"_"+axes[axis];

    // Reset m_enter_pressed flag to _false_, when value is editing
    this->Bind(wxEVT_TEXT, [this](wxEvent&) { m_enter_pressed = false; }, this->GetId());

    this->Bind(wxEVT_TEXT_ENTER, [this, parent](wxEvent&)
    {
        m_enter_pressed = true;
        parent->on_change(m_opt_key, m_axis, get_value());
    }, this->GetId());

    this->Bind(wxEVT_KILL_FOCUS, [this, parent](wxFocusEvent& e)
    {
        parent->set_focused_editor(nullptr);

        if (!m_enter_pressed)
            kill_focus(parent);

        e.Skip();
    }, this->GetId());

    this->Bind(wxEVT_SET_FOCUS, [this, parent](wxFocusEvent& e)
    {
        parent->set_focused_editor(this);

        // needed to show the visual hints in 3D scene
        wxGetApp().plater()->canvas3D()->handle_sidebar_focus_event(m_full_opt_name, true);
        e.Skip();
    }, this->GetId());

    this->Bind(wxEVT_CHAR, ([this](wxKeyEvent& event)
    {
        // select all text using Ctrl+A
        if (wxGetKeyState(wxKeyCode('A')) && wxGetKeyState(WXK_CONTROL))
            this->SetSelection(-1, -1); //select all
        event.Skip();
    }));

    this->Bind(wxEVT_UPDATE_UI, [parent, this](wxUpdateUIEvent& evt) {
        const bool is_gizmo_in_editing_mode = wxGetApp().plater()->canvas3D()->get_gizmos_manager().is_in_editing_mode();
        const bool is_enabled_editing       = has_opt_key("scale") || has_opt_key("size") ? parent->is_enabled_size_and_scale() : true;
        evt.Enable(!is_gizmo_in_editing_mode && parent->is_enabled() && is_enabled_editing);
    });
}

void ManipulationEditor::msw_rescale()
{
    const int em = wxGetApp().em_unit();
    SetMinSize(wxSize(5 * em, wxDefaultCoord));
}

void ManipulationEditor::sys_color_changed(ObjectManipulation* parent)
{
    if (parent->use_colors())
        SetForegroundColour(*wxBLACK);
    else
#ifdef _WIN32
        wxGetApp().UpdateDarkUI(this);
#else
        SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
#endif // _WIN32
}

double ManipulationEditor::get_value()
{
    wxString str = GetValue();

    double value;
    const char dec_sep = is_decimal_separator_point() ? '.' : ',';
    const char dec_sep_alt = dec_sep == '.' ? ',' : '.';
    // Replace the first incorrect separator in decimal number.
    if (str.Replace(dec_sep_alt, dec_sep, false) != 0)
        SetValue(str);

    if (str == ".")
        value = 0.0;

    if ((str.IsEmpty() || !str.ToDouble(&value)) && !m_valid_value.IsEmpty()) {
        str = m_valid_value;
        SetValue(str);
        str.ToDouble(&value);
    }

    return value;
}

void ManipulationEditor::set_value(const wxString& new_value)
{
    if (new_value.IsEmpty())
        return;
    m_valid_value = new_value;
    SetValue(m_valid_value);
}

void ManipulationEditor::kill_focus(ObjectManipulation* parent)
{
    parent->on_change(m_opt_key, m_axis, get_value());

    // if the change does not come from the user pressing the ENTER key
    // we need to hide the visual hints in 3D scene
    wxGetApp().plater()->canvas3D()->handle_sidebar_focus_event(m_full_opt_name, false);
}

} //namespace GUI
} //namespace Slic3r 
