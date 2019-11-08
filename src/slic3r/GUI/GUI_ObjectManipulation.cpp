#include "GUI_ObjectManipulation.hpp"
#include "GUI_ObjectList.hpp"
#include "I18N.hpp"

#include "OptionsGroup.hpp"
#include "wxExtensions.hpp"
#include "PresetBundle.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Geometry.hpp"
#include "Selection.hpp"

#include <boost/algorithm/string.hpp>
#include "slic3r/Utils/FixModelByWin10.hpp"

namespace Slic3r
{
namespace GUI
{


// Helper function to be used by drop to bed button. Returns lowest point of this
// volume in world coordinate system.
static double get_volume_min_z(const GLVolume* volume)
{
    const Transform3f& world_matrix = volume->world_matrix().cast<float>();

    // need to get the ModelVolume pointer
    const ModelObject* mo = wxGetApp().model().objects[volume->composite_id.object_id];
    const ModelVolume* mv = mo->volumes[volume->composite_id.volume_id];
    const TriangleMesh& hull = mv->get_convex_hull();

    float min_z = std::numeric_limits<float>::max();
    for (const stl_facet& facet : hull.stl.facet_start) {
        for (int i = 0; i < 3; ++ i)
            min_z = std::min(min_z, Vec3f::UnitZ().dot(world_matrix * facet.vertex[i]));
    }
    return min_z;
}



static wxBitmapComboBox* create_word_local_combo(wxWindow *parent)
{
    wxSize size(15 * wxGetApp().em_unit(), -1);

    wxBitmapComboBox *temp = nullptr;
#ifdef __WXOSX__
    /* wxBitmapComboBox with wxCB_READONLY style return NULL for GetTextCtrl(),
     * so ToolTip doesn't shown.
     * Next workaround helps to solve this problem
     */
    temp = new wxBitmapComboBox();
    temp->SetTextCtrlStyle(wxTE_READONLY);
	temp->Create(parent, wxID_ANY, wxString(""), wxDefaultPosition, size, 0, nullptr);
#else
	temp = new wxBitmapComboBox(parent, wxID_ANY, wxString(""), wxDefaultPosition, size, 0, nullptr, wxCB_READONLY);
#endif //__WXOSX__

    temp->SetFont(Slic3r::GUI::wxGetApp().normal_font());
    temp->SetBackgroundStyle(wxBG_STYLE_PAINT);

    temp->Append(_(L("World coordinates")));
    temp->Append(_(L("Local coordinates")));
    temp->SetSelection(0);
    temp->SetValue(temp->GetString(0));

#ifndef __WXGTK__
    /* Workaround for a correct rendering of the control without Bitmap (under MSW and OSX):
     * 
     * 1. We should create small Bitmap to fill Bitmaps RefData,
     *    ! in this case wxBitmap.IsOK() return true.
     * 2. But then set width to 0 value for no using of bitmap left and right spacing 
     * 3. Set this empty bitmap to the at list one item and BitmapCombobox will be recreated correct
     * 
     * Note: Set bitmap height to the Font size because of OSX rendering.
     */
    wxBitmap empty_bmp(1, temp->GetFont().GetPixelSize().y + 2);
    empty_bmp.SetWidth(0);
    temp->SetItemBitmap(0, empty_bmp);
#endif

    temp->SetToolTip(_(L("Select coordinate space, in which the transformation will be performed.")));
	return temp;
}

void msw_rescale_word_local_combo(wxBitmapComboBox* combo)
{
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
    
    combo->Append(_(L("World coordinates")));
    combo->Append(_(L("Local coordinates")));

    wxBitmap empty_bmp(1, combo->GetFont().GetPixelSize().y + 2);
    empty_bmp.SetWidth(0);
    combo->SetItemBitmap(0, empty_bmp);

    combo->SetValue(selection);
}

static void set_font_and_background_style(wxWindow* win, const wxFont& font)
{
    win->SetFont(font);
    win->SetBackgroundStyle(wxBG_STYLE_PAINT);
}

ObjectManipulation::ObjectManipulation(wxWindow* parent) :
    OG_Settings(parent, true)
{
    m_manifold_warning_bmp = ScalableBitmap(parent, "exclamation");

    // Load bitmaps to be used for the mirroring buttons:
    m_mirror_bitmap_on     = ScalableBitmap(parent, "mirroring_on");
    m_mirror_bitmap_off    = ScalableBitmap(parent, "mirroring_off");
    m_mirror_bitmap_hidden = ScalableBitmap(parent, "mirroring_transparent.png");

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
                update_warning_icon_state(wxGetApp().obj_list()->get_mesh_errors_list());
            });

    sizer->Add(m_fix_throught_netfab_bitmap);

    auto name_label = new wxStaticText(m_parent, wxID_ANY, _(L("Name"))+":");
    set_font_and_background_style(name_label, wxGetApp().normal_font());
    name_label->SetToolTip(_(L("Object name")));
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
    m_word_local_combo->Bind(wxEVT_COMBOBOX, ([this](wxCommandEvent& evt) {
        this->set_world_coordinates(evt.GetSelection() != 1);
    }), m_word_local_combo->GetId());

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
        set_font_and_background_style(m_move_Label, wxGetApp().normal_font());

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
    add_label(&size_Label,      L("Size"), v_sizer);
    if (wxOSX) set_font_and_background_style(size_Label, wxGetApp().normal_font());

    sizer->Add(v_sizer, 0, wxLEFT, border);
    m_labels_grid_sizer->Add(sizer);
    m_main_grid_sizer->Add(m_labels_grid_sizer, 0, wxEXPAND);


    // Add editors grid sizer
    wxFlexGridSizer* editors_grid_sizer = new wxFlexGridSizer(5, 3, 3); // "Name/label", "String name / Editors"
    editors_grid_sizer->SetFlexibleDirection(wxBOTH);

    // Add Axes labels with icons
    static const char axes[] = { 'X', 'Y', 'Z' };
    for (size_t axis_idx = 0; axis_idx < sizeof(axes); axis_idx++) {
        const char label = axes[axis_idx];

        wxStaticText* axis_name = new wxStaticText(m_parent, wxID_ANY, wxString(label));
        set_font_and_background_style(axis_name, wxGetApp().bold_font());

        sizer = new wxBoxSizer(wxHORIZONTAL);
        // Under OSX we use font, smaller than default font, so
        // there is a next trick for an equivalent layout of coordinates combobox and axes labels in they own sizers
        if (wxOSX) 
            sizer->SetMinSize(-1, m_word_local_combo->GetBestHeight(-1));
        sizer->Add(axis_name, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, border);

        // We will add a button to toggle mirroring to each axis:
        auto btn = new ScalableButton(parent, wxID_ANY, "mirroring_off", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER | wxTRANSPARENT_WINDOW);
        btn->SetToolTip(wxString::Format(_(L("Toggle %c axis mirroring")), (int)label));
        btn->SetBitmapDisabled_(m_mirror_bitmap_hidden);

        m_mirror_buttons[axis_idx].first = btn;
        m_mirror_buttons[axis_idx].second = mbShown;

        sizer->AddStretchSpacer(2);
        sizer->Add(btn, 0, wxALIGN_CENTER_VERTICAL);

        btn->Bind(wxEVT_BUTTON, [this, axis_idx](wxCommandEvent&) {
            Axis axis = (Axis)(axis_idx + X);
            if (m_mirror_buttons[axis_idx].second == mbHidden)
                return;

            GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
            Selection& selection = canvas->get_selection();

            if (selection.is_single_volume() || selection.is_single_modifier()) {
                GLVolume* volume = const_cast<GLVolume*>(selection.get_volume(*selection.get_volume_idxs().begin()));
                volume->set_volume_mirror(axis, -volume->get_volume_mirror(axis));
            }
            else if (selection.is_single_full_instance()) {
                for (unsigned int idx : selection.get_volume_idxs()) {
                    GLVolume* volume = const_cast<GLVolume*>(selection.get_volume(idx));
                    volume->set_instance_mirror(axis, -volume->get_instance_mirror(axis));
                }
            }
            else
                return;

            // Update mirroring at the GLVolumes.
            selection.synchronize_unselected_instances(Selection::SYNC_ROTATION_GENERAL);
            selection.synchronize_unselected_volumes();
            // Copy mirroring values from GLVolumes into Model (ModelInstance / ModelVolume), trigger background processing.
            canvas->do_mirror(L("Set Mirror"));
            UpdateAndShow(true);
        });

        editors_grid_sizer->Add(sizer, 0, wxALIGN_CENTER_HORIZONTAL);
    }

    editors_grid_sizer->AddStretchSpacer(1);
    editors_grid_sizer->AddStretchSpacer(1);

    // add EditBoxes 
    auto add_edit_boxes = [this, editors_grid_sizer](const std::string& opt_key, int axis)
    {
        ManipulationEditor* editor = new ManipulationEditor(this, opt_key, axis);
        m_editors.push_back(editor);

        editors_grid_sizer->Add(editor, 0, wxALIGN_CENTER_VERTICAL);
    };
    
    // add Units 
    auto add_unit_text = [this, parent, editors_grid_sizer, height](std::string unit)
    {
        wxStaticText* unit_text = new wxStaticText(parent, wxID_ANY, _(unit));
        set_font_and_background_style(unit_text, wxGetApp().normal_font()); 

        // Unit text should be the same height as labels      
        wxBoxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->SetMinSize(wxSize(-1, height));
        sizer->Add(unit_text, 0, wxALIGN_CENTER_VERTICAL);

        editors_grid_sizer->Add(sizer);
        m_rescalable_sizers.push_back(sizer);
    };

    for (size_t axis_idx = 0; axis_idx < sizeof(axes); axis_idx++)
        add_edit_boxes("position", axis_idx);
    add_unit_text(L("mm"));

    // Add drop to bed button
    m_drop_to_bed_button = new ScalableButton(parent, wxID_ANY, ScalableBitmap(parent, "drop_to_bed"));
    m_drop_to_bed_button->SetToolTip(_(L("Drop to bed")));
    m_drop_to_bed_button->Bind(wxEVT_BUTTON, [=](wxCommandEvent& e) {
        // ???
        GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
        Selection& selection = canvas->get_selection();

        if (selection.is_single_volume() || selection.is_single_modifier()) {
            const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());

            const Geometry::Transformation& instance_trafo = volume->get_instance_transformation();
            Vec3d diff = m_cache.position - instance_trafo.get_matrix(true).inverse() * Vec3d(0., 0., get_volume_min_z(volume));

            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("Drop to bed")));
            change_position_value(0, diff.x());
            change_position_value(1, diff.y());
            change_position_value(2, diff.z());
        }
        });
    editors_grid_sizer->Add(m_drop_to_bed_button);

    for (size_t axis_idx = 0; axis_idx < sizeof(axes); axis_idx++)
        add_edit_boxes("rotation", axis_idx);
    add_unit_text("°");

    // Add reset rotation button
    m_reset_rotation_button = new ScalableButton(parent, wxID_ANY, ScalableBitmap(parent, "undo"));
    m_reset_rotation_button->SetToolTip(_(L("Reset rotation")));
    m_reset_rotation_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
        GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
        Selection& selection = canvas->get_selection();

        if (selection.is_single_volume() || selection.is_single_modifier()) {
            GLVolume* volume = const_cast<GLVolume*>(selection.get_volume(*selection.get_volume_idxs().begin()));
            volume->set_volume_rotation(Vec3d::Zero());
        }
        else if (selection.is_single_full_instance()) {
            for (unsigned int idx : selection.get_volume_idxs()) {
                GLVolume* volume = const_cast<GLVolume*>(selection.get_volume(idx));
                volume->set_instance_rotation(Vec3d::Zero());
            }
        }
        else
            return;

        // Update rotation at the GLVolumes.
        selection.synchronize_unselected_instances(Selection::SYNC_ROTATION_GENERAL);
        selection.synchronize_unselected_volumes();
        // Copy rotation values from GLVolumes into Model (ModelInstance / ModelVolume), trigger background processing.
        canvas->do_rotate(L("Reset Rotation"));

        UpdateAndShow(true);
    });
    editors_grid_sizer->Add(m_reset_rotation_button);

    for (size_t axis_idx = 0; axis_idx < sizeof(axes); axis_idx++)
        add_edit_boxes("scale", axis_idx);
    add_unit_text("%");

    // Add reset scale button
    m_reset_scale_button = new ScalableButton(parent, wxID_ANY, ScalableBitmap(parent, "undo"));
    m_reset_scale_button->SetToolTip(_(L("Reset scale")));
    m_reset_scale_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("Reset scale")));
        change_scale_value(0, 100.);
        change_scale_value(1, 100.);
        change_scale_value(2, 100.);
    });
    editors_grid_sizer->Add(m_reset_scale_button);

    for (size_t axis_idx = 0; axis_idx < sizeof(axes); axis_idx++)
        add_edit_boxes("size", axis_idx);
    add_unit_text("mm");
    editors_grid_sizer->AddStretchSpacer(1);

    m_main_grid_sizer->Add(editors_grid_sizer, 1, wxEXPAND);

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
		// Show the "World Coordinates" / "Local Coordintes" Combo in Advanced / Expert mode only.
		bool show_world_local_combo = wxGetApp().plater()->canvas3D()->get_selection().is_single_full_instance() && wxGetApp().get_mode() != comSimple;
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

void ObjectManipulation::update_settings_value(const Selection& selection)
{
	m_new_move_label_string   = L("Position");
    m_new_rotate_label_string = L("Rotation");
    m_new_scale_label_string  = L("Scale factors");

    if (wxGetApp().get_mode() == comSimple)
        m_world_coordinates = true;

    ObjectList* obj_list = wxGetApp().obj_list();
    if (selection.is_single_full_instance())
    {
        // all volumes in the selection belongs to the same instance, any of them contains the needed instance data, so we take the first one
        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
        m_new_position = volume->get_instance_offset();

        // Verify whether the instance rotation is multiples of 90 degrees, so that the scaling in world coordinates is possible.
		if (m_world_coordinates && ! m_uniform_scale && 
            ! Geometry::is_rotation_ninety_degrees(volume->get_instance_rotation())) {
			// Manipulating an instance in the world coordinate system, rotation is not multiples of ninety degrees, therefore enforce uniform scaling.
			m_uniform_scale = true;
			m_lock_bnt->SetLock(true);
		}

        if (m_world_coordinates) {
			m_new_rotate_label_string = L("Rotate");
			m_new_rotation = Vec3d::Zero();
			m_new_size     = selection.get_scaled_instance_bounding_box().size();
			m_new_scale    = m_new_size.cwiseProduct(selection.get_unscaled_instance_bounding_box().size().cwiseInverse()) * 100.;
		} else {
			m_new_rotation = volume->get_instance_rotation() * (180. / M_PI);
			m_new_size     = volume->get_instance_transformation().get_scaling_factor().cwiseProduct(wxGetApp().model().objects[volume->object_idx()]->raw_mesh_bounding_box().size());
			m_new_scale    = volume->get_instance_scaling_factor() * 100.;
		}

        m_new_enabled  = true;
    }
    else if (selection.is_single_full_object() && obj_list->is_selected(itObject))
    {
        const BoundingBoxf3& box = selection.get_bounding_box();
        m_new_position = box.center();
        m_new_rotation = Vec3d::Zero();
        m_new_scale    = Vec3d(100., 100., 100.);
        m_new_size     = box.size();
        m_new_rotate_label_string = L("Rotate");
		m_new_scale_label_string  = L("Scale");
        m_new_enabled  = true;
    }
    else if (selection.is_single_modifier() || selection.is_single_volume())
    {
        // the selection contains a single volume
        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
        m_new_position = volume->get_volume_offset();
        m_new_rotation = volume->get_volume_rotation() * (180. / M_PI);
        m_new_scale    = volume->get_volume_scaling_factor() * 100.;
        m_new_size = volume->get_volume_transformation().get_scaling_factor().cwiseProduct(volume->bounding_box().size());
        m_new_enabled = true;
    }
    else if (obj_list->multiple_selection() || obj_list->is_selected(itInstanceRoot))
    {
        reset_settings_value();
		m_new_move_label_string   = L("Translate");
		m_new_rotate_label_string = L("Rotate");
		m_new_scale_label_string  = L("Scale");
        m_new_size = selection.get_bounding_box().size();
        m_new_enabled  = true;
    }
	else {
        // No selection, reset the cache.
//		assert(selection.is_empty());
		reset_settings_value();
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
			wxString new_text = double_to_string(new_value(i), 2);
			double new_rounded;
			new_text.ToDouble(&new_rounded);
			if (std::abs(cached_rounded(i) - new_rounded) > EPSILON) {
				cached_rounded(i) = new_rounded;
                const int id = key_id*3+i;
                if (id >= 0) m_editors[id]->set_value(new_text);
            }
			cached(i) = new_value(i);
		};
        update(m_cache.position, m_cache.position_rounded, mePosition, m_new_position);
        update(m_cache.scale,    m_cache.scale_rounded,    meScale,    m_new_scale);
        update(m_cache.size,     m_cache.size_rounded,     meSize,     m_new_size);
        update(m_cache.rotation, m_cache.rotation_rounded, meRotation, m_new_rotation);
    }


    if (selection.requires_uniform_scale()) {
        m_lock_bnt->SetLock(true);
        m_lock_bnt->SetToolTip(_(L("You cannot use non-uniform scaling mode for multiple objects/parts selection")));
        m_lock_bnt->disable();
    }
    else {
        m_lock_bnt->SetLock(m_uniform_scale);
        m_lock_bnt->SetToolTip(wxEmptyString);
        m_lock_bnt->enable();
    }

    { 
        int new_selection = m_world_coordinates ? 0 : 1; 
        if (m_word_local_combo->GetSelection() != new_selection)
            m_word_local_combo->SetSelection(new_selection);
    }

    if (m_new_enabled)
        m_og->enable();
    else
        m_og->disable();

    update_reset_buttons_visibility();
    update_mirror_buttons_visibility();

    m_dirty = false;
}



void ObjectManipulation::update_reset_buttons_visibility()
{
    GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
    if (!canvas)
        return;
    const Selection& selection = canvas->get_selection();

    bool show_rotation = false;
    bool show_scale = false;
    bool show_drop_to_bed = false;

    if (selection.is_single_full_instance() || selection.is_single_modifier() || selection.is_single_volume()) {
        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
        Vec3d rotation;
        Vec3d scale;
        double min_z = 0.;

        if (selection.is_single_full_instance()) {
            rotation = volume->get_instance_rotation();
            scale = volume->get_instance_scaling_factor();
        }
        else {
            rotation = volume->get_volume_rotation();
            scale = volume->get_volume_scaling_factor();
            min_z = get_volume_min_z(volume);
        }
        show_rotation = !rotation.isApprox(Vec3d::Zero());
        show_scale = !scale.isApprox(Vec3d::Ones());
        show_drop_to_bed = (std::abs(min_z) > EPSILON);
    }

    wxGetApp().CallAfter([this, show_rotation, show_scale, show_drop_to_bed] {
        // There is a case (under OSX), when this function is called after the Manipulation panel is hidden
        // So, let check if Manipulation panel is still shown for this moment
        if (!this->IsShown())
            return;
        m_reset_rotation_button->Show(show_rotation);
        m_reset_scale_button->Show(show_scale);
        m_drop_to_bed_button->Show(show_drop_to_bed);

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
    GLCanvas3D* canvas = wxGetApp().plater()->canvas3D();
    Selection& selection = canvas->get_selection();
    std::array<MirrorButtonState, 3> new_states = {mbHidden, mbHidden, mbHidden};

    if (!m_world_coordinates) {
        if (selection.is_single_full_instance() || selection.is_single_modifier() || selection.is_single_volume()) {
            const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
            Vec3d mirror;

            if (selection.is_single_full_instance())
                mirror = volume->get_instance_mirror();
            else
                mirror = volume->get_volume_mirror();

            for (unsigned char i=0; i<3; ++i)
                new_states[i] = (mirror[i] < 0. ? mbActive : mbShown);
        }
    }
    else {
        // the mirroring buttons should be hidden in world coordinates,
        // unless we make it actually mirror in world coords.
    }

    // Hiding the buttons through Hide() always messed up the sizers. As a workaround, the button
    // is assigned a transparent bitmap. We must of course remember the actual state.
    wxGetApp().CallAfter([this, new_states]{
        for (int i=0; i<3; ++i) {
            if (new_states[i] != m_mirror_buttons[i].second) {
                const ScalableBitmap* bmp = nullptr;
                switch (new_states[i]) {
                    case mbHidden : bmp = &m_mirror_bitmap_hidden; m_mirror_buttons[i].first->Enable(false); break;
                    case mbShown  : bmp = &m_mirror_bitmap_off; m_mirror_buttons[i].first->Enable(true); break;
                    case mbActive : bmp = &m_mirror_bitmap_on; m_mirror_buttons[i].first->Enable(true); break;
                }
                m_mirror_buttons[i].first->SetBitmap_(*bmp);
                m_mirror_buttons[i].second = new_states[i];
            }
        }
    });
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

void ObjectManipulation::update_warning_icon_state(const wxString& tooltip)
{
    m_fix_throught_netfab_bitmap->SetBitmap(tooltip.IsEmpty() ? wxNullBitmap : m_manifold_warning_bmp.bmp());
    m_fix_throught_netfab_bitmap->SetMinSize(tooltip.IsEmpty() ? wxSize(0,0) : m_manifold_warning_bmp.bmp().GetSize());
    m_fix_throught_netfab_bitmap->SetToolTip(tooltip);
}

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
    selection.start_dragging();
    selection.translate(position - m_cache.position, selection.requires_local_axes());
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

    TransformationType transformation_type(TransformationType::World_Relative_Joint);
    if (selection.is_single_full_instance() || selection.requires_local_axes())
		transformation_type.set_independent();
	if (selection.is_single_full_instance() && ! m_world_coordinates) {
        //FIXME Selection::rotate() does not process absoulte rotations correctly: It does not recognize the axis index, which was changed.
		// transformation_type.set_absolute();
		transformation_type.set_local();
	}

    selection.start_dragging();
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
    if (std::abs(m_cache.scale_rounded(axis) - value) < EPSILON)
        return;

    Vec3d scale = m_cache.scale;
	scale(axis) = value;

    this->do_scale(axis, scale);

    m_cache.scale = scale;
	m_cache.scale_rounded(axis) = DBL_MAX;
	this->UpdateAndShow(true);
}


void ObjectManipulation::change_size_value(int axis, double value)
{
    if (std::abs(m_cache.size_rounded(axis) - value) < EPSILON)
        return;

    Vec3d size = m_cache.size;
    size(axis) = value;

    const Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();

    Vec3d ref_size = m_cache.size;
	if (selection.is_single_volume() || selection.is_single_modifier())
        ref_size = selection.get_volume(*selection.get_volume_idxs().begin())->bounding_box().size();
    else if (selection.is_single_full_instance())
		ref_size = m_world_coordinates ? 
            selection.get_unscaled_instance_bounding_box().size() :
            wxGetApp().model().objects[selection.get_volume(*selection.get_volume_idxs().begin())->object_idx()]->raw_mesh_bounding_box().size();

    this->do_scale(axis, 100. * Vec3d(size(0) / ref_size(0), size(1) / ref_size(1), size(2) / ref_size(2)));

    m_cache.size = size;
	m_cache.size_rounded(axis) = DBL_MAX;
	this->UpdateAndShow(true);
}

void ObjectManipulation::do_scale(int axis, const Vec3d &scale) const
{
    Selection& selection = wxGetApp().plater()->canvas3D()->get_selection();
    Vec3d scaling_factor = scale;

    TransformationType transformation_type(TransformationType::World_Relative_Joint);
    if (selection.is_single_full_instance()) {
        transformation_type.set_absolute();
        if (! m_world_coordinates)
            transformation_type.set_local();
    }

    if (m_uniform_scale || selection.requires_uniform_scale())
        scaling_factor = scale(axis) * Vec3d::Ones();

    selection.start_dragging();
    selection.scale(scaling_factor * 0.01, transformation_type);
    wxGetApp().plater()->canvas3D()->do_scale(L("Set Scale"));
}

void ObjectManipulation::on_change(const std::string& opt_key, int axis, double new_value)
{
    if (!m_cache.is_valid())
        return;

    if (opt_key == "position")
        change_position_value(axis, new_value);
    else if (opt_key == "rotation")
        change_rotation_value(axis, new_value);
    else if (opt_key == "scale")
        change_scale_value(axis, new_value);
    else if (opt_key == "size")
        change_size_value(axis, new_value);
}

void ObjectManipulation::set_uniform_scaling(const bool new_value)
{ 
    const Selection &selection = wxGetApp().plater()->canvas3D()->get_selection();
	if (selection.is_single_full_instance() && m_world_coordinates && !new_value) {
        // Verify whether the instance rotation is multiples of 90 degrees, so that the scaling in world coordinates is possible.
        // all volumes in the selection belongs to the same instance, any of them contains the needed instance data, so we take the first one
        const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
        // Is the angle close to a multiple of 90 degrees?
		if (! Geometry::is_rotation_ninety_degrees(volume->get_instance_rotation())) {
            // Cannot apply scaling in the world coordinate system.
			wxMessageDialog dlg(GUI::wxGetApp().mainframe,
                _(L("The currently manipulated object is tilted (rotation angles are not multiples of 90°).\n"
                    "Non-uniform scaling of tilted objects is only possible in the World coordinate system,\n"
                    "once the rotation is embedded into the object coordinates.")) + "\n" +
                _(L("This operation is irreversible.\n"
                    "Do you want to proceed?")),
                SLIC3R_APP_NAME,
				wxYES_NO | wxCANCEL | wxCANCEL_DEFAULT | wxICON_QUESTION);
            if (dlg.ShowModal() != wxID_YES) {
                // Enforce uniform scaling.
                m_lock_bnt->SetLock(true);
                return;
            }
            // Bake the rotation into the meshes of the object.
            wxGetApp().model().objects[volume->composite_id.object_id]->bake_xy_rotation_into_meshes(volume->composite_id.instance_id);
            // Update the 3D scene, selections etc.
            wxGetApp().plater()->update();
            // Recalculate cached values at this panel, refresh the screen.
            this->UpdateAndShow(true);
        }
    }
    m_uniform_scale = new_value;
}

void ObjectManipulation::msw_rescale()
{
    const int em = wxGetApp().em_unit();
    m_item_name->SetMinSize(wxSize(20*em, wxDefaultCoord));
    msw_rescale_word_local_combo(m_word_local_combo);
    m_word_local_combo_sizer->SetMinSize(wxSize(-1, m_word_local_combo->GetBestHeight(-1)));
    m_manifold_warning_bmp.msw_rescale();

    const wxString& tooltip = m_fix_throught_netfab_bitmap->GetToolTipText();
    m_fix_throught_netfab_bitmap->SetBitmap(tooltip.IsEmpty() ? wxNullBitmap : m_manifold_warning_bmp.bmp());
    m_fix_throught_netfab_bitmap->SetMinSize(tooltip.IsEmpty() ? wxSize(0, 0) : m_manifold_warning_bmp.bmp().GetSize());

    m_mirror_bitmap_on.msw_rescale();
    m_mirror_bitmap_off.msw_rescale();
    m_mirror_bitmap_hidden.msw_rescale();
    m_reset_scale_button->msw_rescale();
    m_reset_rotation_button->msw_rescale();
    m_drop_to_bed_button->msw_rescale();
    m_lock_bnt->msw_rescale();

    for (int id = 0; id < 3; ++id)
        m_mirror_buttons[id].first->msw_rescale();

    // rescale label-heights
    // Text trick to grid sizer layout:
    // Height of labels should be equivalent to the edit boxes
    const int height = wxTextCtrl(parent(), wxID_ANY, "Br").GetBestHeight(-1);
    for (wxBoxSizer* sizer : m_rescalable_sizers)
        sizer->SetMinSize(wxSize(-1, height));

    // rescale edit-boxes
    for (ManipulationEditor* editor : m_editors)
        editor->msw_rescale();

    get_og()->msw_rescale();
}

static const char axes[] = { 'x', 'y', 'z' };
ManipulationEditor::ManipulationEditor(ObjectManipulation* parent,
                                       const std::string& opt_key,
                                       int axis) :
    wxTextCtrl(parent->parent(), wxID_ANY, wxEmptyString, wxDefaultPosition,
        wxSize(5*int(wxGetApp().em_unit()), wxDefaultCoord), wxTE_PROCESS_ENTER),
    m_opt_key(opt_key),
    m_axis(axis)
{
    set_font_and_background_style(this, wxGetApp().normal_font());
#ifdef __WXOSX__
    this->OSXDisableAllSmartSubstitutions();
#endif // __WXOSX__

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
}

void ManipulationEditor::msw_rescale()
{
    const int em = wxGetApp().em_unit();
    SetMinSize(wxSize(5 * em, wxDefaultCoord));
}

double ManipulationEditor::get_value()
{
    wxString str = GetValue();

    double value;
    // Replace the first occurence of comma in decimal number.
    str.Replace(",", ".", false);
    if (str == ".")
        value = 0.0;

    if ((str.IsEmpty() || !str.ToCDouble(&value)) && !m_valid_value.IsEmpty()) {
        str = m_valid_value;
        SetValue(str);
        str.ToCDouble(&value);
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
