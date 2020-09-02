#include "PresetComboBoxes.hpp"

#include <cstddef>
#include <vector>
#include <string>
#include <boost/algorithm/string.hpp>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/statbox.h>
#include <wx/colordlg.h>
#include <wx/wupdlock.h>
#include <wx/menu.h>

#include "libslic3r/libslic3r.h"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "Tab.hpp"
#include "ConfigWizard.hpp"
#include "../Utils/ASCIIFolding.hpp"
#include "../Utils/FixModelByWin10.hpp"
#include "../Utils/UndoRedo.hpp"
#include "BitmapCache.hpp"
#include "PhysicalPrinterDialog.hpp"

using Slic3r::GUI::format_wxstr;

namespace Slic3r {
namespace GUI {

#define BORDER_W 10

// ---------------------------------
// ***  PresetComboBox  ***
// ---------------------------------

/* For PresetComboBox we use bitmaps that are created from images that are already scaled appropriately for Retina
 * (Contrary to the intuition, the `scale` argument for Bitmap's constructor doesn't mean
 * "please scale this to such and such" but rather
 * "the wxImage is already sized for backing scale such and such". )
 * Unfortunately, the constructor changes the size of wxBitmap too.
 * Thus We need to use unscaled size value for bitmaps that we use
 * to avoid scaled size of control items.
 * For this purpose control drawing methods and
 * control size calculation methods (virtual) are overridden.
 **/

PresetComboBox::PresetComboBox(wxWindow* parent, Preset::Type preset_type, const wxSize& size) :
    wxBitmapComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, size, 0, nullptr, wxCB_READONLY),
    m_type(preset_type),
    m_last_selected(wxNOT_FOUND),
    m_em_unit(em_unit(this)),
    m_preset_bundle(wxGetApp().preset_bundle)
{
    SetFont(wxGetApp().normal_font());
#ifdef _WIN32
    // Workaround for ignoring CBN_EDITCHANGE events, which are processed after the content of the combo box changes, so that
    // the index of the item inside CBN_EDITCHANGE may no more be valid.
    EnableTextChangedEvents(false);
#endif /* _WIN32 */

    switch (m_type)
    {
    case Preset::TYPE_PRINT: {
        m_collection = &m_preset_bundle->prints;
        m_main_bitmap_name = "cog";
        break;
    }
    case Preset::TYPE_FILAMENT: {
        m_collection = &m_preset_bundle->filaments;
        m_main_bitmap_name = "spool";
        break;
    }
    case Preset::TYPE_SLA_PRINT: {
        m_collection = &m_preset_bundle->sla_prints;
        m_main_bitmap_name = "cog";
        break;
    }
    case Preset::TYPE_SLA_MATERIAL: {
        m_collection = &m_preset_bundle->sla_materials;
        m_main_bitmap_name = "resin";
        break;
    }
    case Preset::TYPE_PRINTER: {
        m_collection = &m_preset_bundle->printers;
        m_main_bitmap_name = "printer";
        break;
    }
    default: break;
    }

    m_bitmapCompatible   = ScalableBitmap(this, "flag_green");
    m_bitmapIncompatible = ScalableBitmap(this, "flag_red");

    // parameters for an icon's drawing
    fill_width_height();

    Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& evt) {
        // see https://github.com/prusa3d/PrusaSlicer/issues/3889
        // Under OSX: in case of use of a same names written in different case (like "ENDER" and "Ender")
        // m_presets_choice->GetSelection() will return first item, because search in PopupListCtrl is case-insensitive.
        // So, use GetSelection() from event parameter 
        auto selected_item = evt.GetSelection();

        auto marker = reinterpret_cast<Marker>(this->GetClientData(selected_item));
        if (marker >= LABEL_ITEM_DISABLED && marker < LABEL_ITEM_MAX)
            this->SetSelection(this->m_last_selected);
        else if (on_selection_changed && (m_last_selected != selected_item || m_collection->current_is_dirty())) {
            m_last_selected = selected_item;
            on_selection_changed(selected_item);
            evt.StopPropagation();
        }
        evt.Skip();
    });
}

PresetComboBox::~PresetComboBox()
{
}

BitmapCache& PresetComboBox::bitmap_cache()
{
    static BitmapCache bmps;
    return bmps;
}

void PresetComboBox::set_label_marker(int item, LabelItemType label_item_type)
{
    this->SetClientData(item, (void*)label_item_type);
}

bool PresetComboBox::set_printer_technology(PrinterTechnology pt)
{
    if (printer_technology != pt) {
        printer_technology = pt;
        return true;
    }
    return false;
}

void PresetComboBox::invalidate_selection()
{
    m_last_selected = INT_MAX; // this value means that no one item is selected
}

void PresetComboBox::validate_selection(bool predicate/*=false*/)
{
    if (predicate ||
        // just in case: mark m_last_selected as a first added element
        m_last_selected == INT_MAX)
        m_last_selected = GetCount() - 1;
}

void PresetComboBox::update_selection()
{
    /* If selected_preset_item is still equal to INT_MAX, it means that
     * there is no presets added to the list.
     * So, select last combobox item ("Add/Remove preset")
     */
    validate_selection();

    SetSelection(m_last_selected);
    SetToolTip(GetString(m_last_selected));
}

void PresetComboBox::update(std::string select_preset_name)
{
    Freeze();
    Clear();
    invalidate_selection();

    const std::deque<Preset>& presets = m_collection->get_presets();

    std::map<wxString, std::pair<wxBitmap*, bool>>  nonsys_presets;
    std::map<wxString, wxBitmap*>                   incomp_presets;

    wxString selected = "";
    if (!presets.front().is_visible)
        set_label_marker(Append(separator(L("System presets")), wxNullBitmap));

    for (size_t i = presets.front().is_visible ? 0 : m_collection->num_default_presets(); i < presets.size(); ++i)
    {
        const Preset& preset = presets[i];
        if (!preset.is_visible || !preset.is_compatible)
            continue;

        // marker used for disable incompatible printer models for the selected physical printer
        bool is_enabled = m_type == Preset::TYPE_PRINTER && printer_technology != ptAny ? preset.printer_technology() == printer_technology : true;
        if (select_preset_name.empty() && is_enabled)
            select_preset_name = preset.name;

        std::string   bitmap_key = "cb";
        if (m_type == Preset::TYPE_PRINTER) {
            bitmap_key += "_printer";
            if (preset.printer_technology() == ptSLA)
                bitmap_key += "_sla";
        }
        std::string main_icon_name = m_type == Preset::TYPE_PRINTER && preset.printer_technology() == ptSLA ? "sla_printer" : m_main_bitmap_name;

        wxBitmap* bmp = get_bmp(bitmap_key, main_icon_name, "lock_closed", is_enabled, preset.is_compatible, preset.is_system || preset.is_default);
        assert(bmp);

        if (!is_enabled)
            incomp_presets.emplace(wxString::FromUTF8((preset.name + (preset.is_dirty ? Preset::suffix_modified() : "")).c_str()), bmp);
        else if (preset.is_default || preset.is_system)
        {
            Append(wxString::FromUTF8((preset.name + (preset.is_dirty ? Preset::suffix_modified() : "")).c_str()), *bmp);
            validate_selection(preset.name == select_preset_name);
        }
        else
        {
            nonsys_presets.emplace(wxString::FromUTF8((preset.name + (preset.is_dirty ? Preset::suffix_modified() : "")).c_str()), std::pair<wxBitmap*, bool>(bmp, is_enabled));
            if (preset.name == select_preset_name || (select_preset_name.empty() && is_enabled))
                selected = wxString::FromUTF8((preset.name + (preset.is_dirty ? Preset::suffix_modified() : "")).c_str());
        }
        if (i + 1 == m_collection->num_default_presets())
            set_label_marker(Append(separator(L("System presets")), wxNullBitmap));
    }
    if (!nonsys_presets.empty())
    {
        set_label_marker(Append(separator(L("User presets")), wxNullBitmap));
        for (std::map<wxString, std::pair<wxBitmap*, bool>>::iterator it = nonsys_presets.begin(); it != nonsys_presets.end(); ++it) {
            int item_id = Append(it->first, *it->second.first);
            bool is_enabled = it->second.second;
            if (!is_enabled)
                set_label_marker(item_id, LABEL_ITEM_DISABLED);
            validate_selection(it->first == selected);
        }
    }
    if (!incomp_presets.empty())
    {
        set_label_marker(Append(separator(L("Incompatible presets")), wxNullBitmap));
        for (std::map<wxString, wxBitmap*>::iterator it = incomp_presets.begin(); it != incomp_presets.end(); ++it) {
            set_label_marker(Append(it->first, *it->second), LABEL_ITEM_DISABLED);
        }
    }

    update_selection();
    Thaw();
}

void PresetComboBox::update()
{
    this->update(into_u8(this->GetString(this->GetSelection())));
}

void PresetComboBox::msw_rescale()
{
    m_em_unit = em_unit(this);

    m_bitmapIncompatible.msw_rescale();
    m_bitmapCompatible.msw_rescale();

    // parameters for an icon's drawing
    fill_width_height();

    // update the control to redraw the icons
    update();
}

void PresetComboBox::fill_width_height()
{
    // To avoid asserts, each added bitmap to wxBitmapCombobox should be the same size, so
    // set a bitmap's height to m_bitmapCompatible->GetHeight() and norm_icon_width to m_bitmapCompatible->GetWidth()
    icon_height     = m_bitmapCompatible.GetBmpHeight();
    norm_icon_width = m_bitmapCompatible.GetBmpWidth();

    /* It's supposed that standard size of an icon is 16px*16px for 100% scaled display.
    * So set sizes for solid_colored icons used for filament preset
    * and scale them in respect to em_unit value
    */
    const float scale_f = (float)m_em_unit * 0.1f;

    thin_icon_width = lroundf(8 * scale_f);          // analogue to 8px;
    wide_icon_width = norm_icon_width + thin_icon_width;

    space_icon_width = lroundf(2 * scale_f);
    thin_space_icon_width = 2 * space_icon_width;
    wide_space_icon_width = 3 * space_icon_width;
}

wxString PresetComboBox::separator(const std::string& label)
{
    return wxString::FromUTF8(separator_head()) + _(label) + wxString::FromUTF8(separator_tail());
}

wxBitmap* PresetComboBox::get_bmp(  std::string bitmap_key, bool wide_icons, const std::string& main_icon_name, 
                                    bool is_compatible/* = true*/, bool is_system/* = false*/, bool is_single_bar/* = false*/,
                                    std::string filament_rgb/* = ""*/, std::string extruder_rgb/* = ""*/)
{
    // If the filament preset is not compatible and there is a "red flag" icon loaded, show it left
    // to the filament color image.
    if (wide_icons)
        bitmap_key += is_compatible ? ",cmpt" : ",ncmpt";

    bitmap_key += is_system ? ",syst" : ",nsyst";
    bitmap_key += ",h" + std::to_string(icon_height);

    wxBitmap* bmp = bitmap_cache().find(bitmap_key);
    if (bmp == nullptr) {
        // Create the bitmap with color bars.
        std::vector<wxBitmap> bmps;
        if (wide_icons)
            // Paint a red flag for incompatible presets.
            bmps.emplace_back(is_compatible ? bitmap_cache().mkclear(norm_icon_width, icon_height) : m_bitmapIncompatible.bmp());

        if (m_type == Preset::TYPE_FILAMENT)
        {
            unsigned char rgb[3];
            // Paint the color bars.
            bitmap_cache().parse_color(filament_rgb, rgb);
            bmps.emplace_back(bitmap_cache().mksolid(is_single_bar ? wide_icon_width : norm_icon_width, icon_height, rgb));
            if (!is_single_bar) {
                bitmap_cache().parse_color(extruder_rgb, rgb);
                bmps.emplace_back(bitmap_cache().mksolid(thin_icon_width, icon_height, rgb));
            }
            // Paint a lock at the system presets.
            bmps.emplace_back(bitmap_cache().mkclear(space_icon_width, icon_height));
        }
        else
        {
            // Paint the color bars.
            bmps.emplace_back(bitmap_cache().mkclear(thin_space_icon_width, icon_height));
            bmps.emplace_back(create_scaled_bitmap(main_icon_name));
            // Paint a lock at the system presets.
            bmps.emplace_back(bitmap_cache().mkclear(wide_space_icon_width, icon_height));
        }
        bmps.emplace_back(is_system ? create_scaled_bitmap("lock_closed") : bitmap_cache().mkclear(norm_icon_width, icon_height));
        bmp = bitmap_cache().insert(bitmap_key, bmps);
    }

    return bmp;
}

wxBitmap* PresetComboBox::get_bmp(  std::string bitmap_key, const std::string& main_icon_name, const std::string& next_icon_name,
                                    bool is_enabled/* = true*/, bool is_compatible/* = true*/, bool is_system/* = false*/)
{
    bitmap_key += !is_enabled ? "_disabled" : "";
    bitmap_key += is_compatible ? ",cmpt" : ",ncmpt";
    bitmap_key += is_system ? ",syst" : ",nsyst";
    bitmap_key += ",h" + std::to_string(icon_height);

    wxBitmap* bmp = bitmap_cache().find(bitmap_key);
    if (bmp == nullptr) {
        // Create the bitmap with color bars.
        std::vector<wxBitmap> bmps;
        bmps.emplace_back(m_type == Preset::TYPE_PRINTER ? create_scaled_bitmap(main_icon_name, this, 16, !is_enabled) : 
                          is_compatible ? m_bitmapCompatible.bmp() : m_bitmapIncompatible.bmp());
        // Paint a lock at the system presets.
        bmps.emplace_back(is_system ? create_scaled_bitmap(next_icon_name, this, 16, !is_enabled) : bitmap_cache().mkclear(norm_icon_width, icon_height));
        bmp = bitmap_cache().insert(bitmap_key, bmps);
    }

    return bmp;
}

bool PresetComboBox::is_selected_physical_printer()
{
    auto selected_item = this->GetSelection();
    auto marker = reinterpret_cast<Marker>(this->GetClientData(selected_item));
    return marker == LABEL_ITEM_PHYSICAL_PRINTER;
}

bool PresetComboBox::selection_is_changed_according_to_physical_printers()
{
    if (m_type != Preset::TYPE_PRINTER || !is_selected_physical_printer())
        return false;

    PhysicalPrinterCollection& physical_printers = m_preset_bundle->physical_printers;

    std::string selected_string = this->GetString(this->GetSelection()).ToUTF8().data();

    std::string old_printer_full_name, old_printer_preset;
    if (physical_printers.has_selection()) {
        old_printer_full_name = physical_printers.get_selected_full_printer_name();
        old_printer_preset = physical_printers.get_selected_printer_preset_name();
    }
    else
        old_printer_preset = m_collection->get_edited_preset().name;
    // Select related printer preset on the Printer Settings Tab 
    physical_printers.select_printer(selected_string);
    std::string preset_name = physical_printers.get_selected_printer_preset_name();

    // if new preset wasn't selected, there is no need to call update preset selection
    if (old_printer_preset == preset_name) {
        // we need just to update according Plater<->Tab PresetComboBox 
        if (dynamic_cast<PlaterPresetComboBox*>(this)!=nullptr) {
            wxGetApp().get_tab(m_type)->update_preset_choice();
            // Synchronize config.ini with the current selections.
            m_preset_bundle->export_selections(*wxGetApp().app_config);
        }
        else if (dynamic_cast<TabPresetComboBox*>(this)!=nullptr)
            wxGetApp().sidebar().update_presets(m_type);

        this->update();
        return true;
    }

    Tab* tab = wxGetApp().get_tab(Preset::TYPE_PRINTER);
    if (tab)
        tab->select_preset(preset_name, false, old_printer_full_name);
    return true;
}

#ifdef __APPLE__
bool PresetComboBox::OnAddBitmap(const wxBitmap& bitmap)
{
    if (bitmap.IsOk())
    {
        // we should use scaled! size values of bitmap
        int width = (int)bitmap.GetScaledWidth();
        int height = (int)bitmap.GetScaledHeight();

        if (m_usedImgSize.x < 0)
        {
            // If size not yet determined, get it from this image.
            m_usedImgSize.x = width;
            m_usedImgSize.y = height;

            // Adjust control size to vertically fit the bitmap
            wxWindow* ctrl = GetControl();
            ctrl->InvalidateBestSize();
            wxSize newSz = ctrl->GetBestSize();
            wxSize sz = ctrl->GetSize();
            if (newSz.y > sz.y)
                ctrl->SetSize(sz.x, newSz.y);
            else
                DetermineIndent();
        }

        wxCHECK_MSG(width == m_usedImgSize.x && height == m_usedImgSize.y,
            false,
            "you can only add images of same size");

        return true;
    }

    return false;
}

void PresetComboBox::OnDrawItem(wxDC& dc,
    const wxRect& rect,
    int item,
    int flags) const
{
    const wxBitmap& bmp = *(wxBitmap*)m_bitmaps[item];
    if (bmp.IsOk())
    {
        // we should use scaled! size values of bitmap
        wxCoord w = bmp.GetScaledWidth();
        wxCoord h = bmp.GetScaledHeight();

        const int imgSpacingLeft = 4;

        // Draw the image centered
        dc.DrawBitmap(bmp,
            rect.x + (m_usedImgSize.x - w) / 2 + imgSpacingLeft,
            rect.y + (rect.height - h) / 2,
            true);
    }

    wxString text = GetString(item);
    if (!text.empty())
        dc.DrawText(text,
            rect.x + m_imgAreaWidth + 1,
            rect.y + (rect.height - dc.GetCharHeight()) / 2);
}
#endif


// ---------------------------------
// ***  PlaterPresetComboBox  ***
// ---------------------------------

PlaterPresetComboBox::PlaterPresetComboBox(wxWindow *parent, Preset::Type preset_type) :
    PresetComboBox(parent, preset_type, wxSize(15 * wxGetApp().em_unit(), -1))
{
    Bind(wxEVT_COMBOBOX, [this](wxCommandEvent &evt) {
        auto selected_item = evt.GetSelection();

        auto marker = reinterpret_cast<Marker>(this->GetClientData(selected_item));
        if (marker >= LABEL_ITEM_MARKER && marker < LABEL_ITEM_MAX) {
            this->SetSelection(this->m_last_selected);
            evt.StopPropagation();
            if (marker == LABEL_ITEM_WIZARD_PRINTERS)
                show_add_menu();
            else
            {
                ConfigWizard::StartPage sp = ConfigWizard::SP_WELCOME;
                switch (marker) {
                case LABEL_ITEM_WIZARD_FILAMENTS: sp = ConfigWizard::SP_FILAMENTS; break;
                case LABEL_ITEM_WIZARD_MATERIALS: sp = ConfigWizard::SP_MATERIALS; break;
                default: break;
                }
                wxTheApp->CallAfter([sp]() { wxGetApp().run_wizard(ConfigWizard::RR_USER, sp); });
            }
        } else if (marker == LABEL_ITEM_PHYSICAL_PRINTER || this->m_last_selected != selected_item || m_collection->current_is_dirty() ) {
            this->m_last_selected = selected_item;
            evt.SetInt(this->m_type);
            evt.Skip();
        } else {
            evt.StopPropagation();
        }
    });

    if (m_type == Preset::TYPE_FILAMENT)
    {
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &event) {
            const Preset* selected_preset = m_collection->find_preset(m_preset_bundle->filament_presets[m_extruder_idx]);
            // Wide icons are shown if the currently selected preset is not compatible with the current printer,
            // and red flag is drown in front of the selected preset.
            bool          wide_icons = selected_preset && !selected_preset->is_compatible;
            float scale = m_em_unit*0.1f;

            int shifl_Left = wide_icons ? int(scale * 16 + 0.5) : 0;
#if defined(wxBITMAPCOMBOBOX_OWNERDRAWN_BASED)
            shifl_Left  += int(scale * 4 + 0.5f); // IMAGE_SPACING_RIGHT = 4 for wxBitmapComboBox -> Space left of image
#endif
            int icon_right_pos = shifl_Left + int(scale * (24+4) + 0.5);
            int mouse_pos = event.GetLogicalPosition(wxClientDC(this)).x;
            if (mouse_pos < shifl_Left || mouse_pos > icon_right_pos ) {
                // Let the combo box process the mouse click.
                event.Skip();
                return;
            }

            // Swallow the mouse click and open the color picker.

            // get current color
            DynamicPrintConfig* cfg = wxGetApp().get_tab(Preset::TYPE_PRINTER)->get_config();
            auto colors = static_cast<ConfigOptionStrings*>(cfg->option("extruder_colour")->clone());
            wxColour clr(colors->values[m_extruder_idx]);
            if (!clr.IsOk())
                clr = wxColour(0,0,0); // Don't set alfa to transparence

            auto data = new wxColourData();
            data->SetChooseFull(1);
            data->SetColour(clr);

            wxColourDialog dialog(this, data);
            dialog.CenterOnParent();
            if (dialog.ShowModal() == wxID_OK)
            {
                colors->values[m_extruder_idx] = dialog.GetColourData().GetColour().GetAsString(wxC2S_HTML_SYNTAX).ToStdString();

                DynamicPrintConfig cfg_new = *cfg;
                cfg_new.set_key_value("extruder_colour", colors);

                wxGetApp().get_tab(Preset::TYPE_PRINTER)->load_config(cfg_new);
                this->update();
                wxGetApp().plater()->on_config_change(cfg_new);
            }
        });
    }

    edit_btn = new ScalableButton(parent, wxID_ANY, "cog");
    edit_btn->SetToolTip(_L("Click to edit preset"));

    edit_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent)
    {
        // In a case of a physical printer, for its editing open PhysicalPrinterDialog
        if (m_type == Preset::TYPE_PRINTER/* && this->is_selected_physical_printer()*/) {
            this->show_edit_menu();
            return;
        }

        if (!switch_to_tab())
            return;

        /* In a case of a multi-material printing, for editing another Filament Preset
         * it's needed to select this preset for the "Filament settings" Tab
         */
        if (m_type == Preset::TYPE_FILAMENT && wxGetApp().extruders_edited_cnt() > 1)
        {
            const std::string& selected_preset = GetString(GetSelection()).ToUTF8().data();

            // Call select_preset() only if there is new preset and not just modified
            if ( !boost::algorithm::ends_with(selected_preset, Preset::suffix_modified()) )
            {
                const std::string& preset_name = wxGetApp().preset_bundle->filaments.get_preset_name_by_alias(selected_preset);
                wxGetApp().get_tab(m_type)->select_preset(preset_name);
            }
        }
    });
}

PlaterPresetComboBox::~PlaterPresetComboBox()
{
    if (edit_btn)
        edit_btn->Destroy();
}

bool PlaterPresetComboBox::switch_to_tab()
{
    Tab* tab = wxGetApp().get_tab(m_type);
    if (!tab)
        return false;

    int page_id = wxGetApp().tab_panel()->FindPage(tab);
    if (page_id == wxNOT_FOUND)
        return false;

    wxGetApp().tab_panel()->SetSelection(page_id);
    // Switch to Settings NotePad
    wxGetApp().mainframe->select_tab();
    return true;
}

void PlaterPresetComboBox::show_add_menu()
{
    wxMenu* menu = new wxMenu();

    append_menu_item(menu, wxID_ANY, _L("Add/Remove presets"), "",
        [this](wxCommandEvent&) { 
            wxTheApp->CallAfter([]() { wxGetApp().run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_PRINTERS); });
        }, "edit_uni", menu, []() { return true; }, wxGetApp().plater());

    append_menu_item(menu, wxID_ANY, _L("Add physical printer"), "",
        [this](wxCommandEvent&) {
            PhysicalPrinterDialog dlg(wxEmptyString);
            if (dlg.ShowModal() == wxID_OK)
                update();
        }, "edit_uni", menu, []() { return true; }, wxGetApp().plater());

    wxGetApp().plater()->PopupMenu(menu);
}

void PlaterPresetComboBox::show_edit_menu()
{
    wxMenu* menu = new wxMenu();

    append_menu_item(menu, wxID_ANY, _L("Edit preset"), "",
        [this](wxCommandEvent&) { this->switch_to_tab(); }, "cog", menu, []() { return true; }, wxGetApp().plater());

    if (this->is_selected_physical_printer()) {
    append_menu_item(menu, wxID_ANY, _L("Edit physical printer"), "",
        [this](wxCommandEvent&) {
            PhysicalPrinterDialog dlg(this->GetString(this->GetSelection()));
            if (dlg.ShowModal() == wxID_OK)
                update();
        }, "cog", menu, []() { return true; }, wxGetApp().plater());

    append_menu_item(menu, wxID_ANY, _L("Delete physical printer"), "",
        [this](wxCommandEvent&) {
            const std::string& printer_name = m_preset_bundle->physical_printers.get_selected_full_printer_name();
            if (printer_name.empty())
                return;

            const wxString msg = from_u8((boost::format(_u8L("Are you sure you want to delete \"%1%\" printer?")) % printer_name).str());
            if (wxMessageDialog(this, msg, _L("Delete Physical Printer"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION).ShowModal() != wxID_YES)
                return;

            m_preset_bundle->physical_printers.delete_selected_printer();

            wxGetApp().get_tab(m_type)->update_preset_choice();
            update();
        }, "cross", menu, []() { return true; }, wxGetApp().plater());
    }
    else
        append_menu_item(menu, wxID_ANY, _L("Add/Remove presets"), "",
            [this](wxCommandEvent&) {
                wxTheApp->CallAfter([]() { wxGetApp().run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_PRINTERS); });
            }, "edit_uni", menu, []() { return true; }, wxGetApp().plater());

    append_menu_item(menu, wxID_ANY, _L("Add physical printer"), "",
        [this](wxCommandEvent&) {
            PhysicalPrinterDialog dlg(wxEmptyString);
            if (dlg.ShowModal() == wxID_OK)
                update();
        }, "edit_uni", menu, []() { return true; }, wxGetApp().plater());

    wxGetApp().plater()->PopupMenu(menu);
}

// Only the compatible presets are shown.
// If an incompatible preset is selected, it is shown as well.
void PlaterPresetComboBox::update()
{
    if (m_type == Preset::TYPE_FILAMENT &&
        (m_preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA ||
        m_preset_bundle->filament_presets.size() <= m_extruder_idx) )
        return;

    // Otherwise fill in the list from scratch.
    this->Freeze();
    this->Clear();
    invalidate_selection();

    const Preset* selected_filament_preset;
    std::string extruder_color;
    if (m_type == Preset::TYPE_FILAMENT)
    {
        unsigned char rgb[3];
        extruder_color = m_preset_bundle->printers.get_edited_preset().config.opt_string("extruder_colour", (unsigned int)m_extruder_idx);
        if (!bitmap_cache().parse_color(extruder_color, rgb))
            // Extruder color is not defined.
            extruder_color.clear();
        selected_filament_preset = m_collection->find_preset(m_preset_bundle->filament_presets[m_extruder_idx]);
        assert(selected_filament_preset);
    }

    bool has_selection = m_collection->get_selected_idx() != size_t(-1);
    const Preset* selected_preset = m_type == Preset::TYPE_FILAMENT ? selected_filament_preset : has_selection ? &m_collection->get_selected_preset() : nullptr;
    // Show wide icons if the currently selected preset is not compatible with the current printer,
    // and draw a red flag in front of the selected preset.
    bool wide_icons = selected_preset && !selected_preset->is_compatible;

    std::map<wxString, wxBitmap*> nonsys_presets;

    wxString selected_user_preset = "";
    wxString tooltip = "";
    const std::deque<Preset>& presets = m_collection->get_presets();

    if (!presets.front().is_visible)
        this->set_label_marker(this->Append(separator(L("System presets")), wxNullBitmap));

    for (size_t i = presets.front().is_visible ? 0 : m_collection->num_default_presets(); i < presets.size(); ++i) 
    {
        const Preset& preset = presets[i];
        bool is_selected =  m_type == Preset::TYPE_FILAMENT ?
                            m_preset_bundle->filament_presets[m_extruder_idx] == preset.name :
                            // The case, when some physical printer is selected
                            m_type == Preset::TYPE_PRINTER && m_preset_bundle->physical_printers.has_selection() ? false :
                            i == m_collection->get_selected_idx();

        if (!preset.is_visible || (!preset.is_compatible && !is_selected))
            continue;

        std::string bitmap_key, filament_rgb, extruder_rgb;
        std::string bitmap_type_name = bitmap_key = m_type == Preset::TYPE_PRINTER && preset.printer_technology() == ptSLA ? "sla_printer" : m_main_bitmap_name;

        bool single_bar = false;
        if (m_type == Preset::TYPE_FILAMENT)
        {
            // Assign an extruder color to the selected item if the extruder color is defined.
            filament_rgb = preset.config.opt_string("filament_colour", 0);
            extruder_rgb = (is_selected && !extruder_color.empty()) ? extruder_color : filament_rgb;
            single_bar = filament_rgb == extruder_rgb;

            bitmap_key += single_bar ? filament_rgb : filament_rgb + extruder_rgb;
        }

        wxBitmap* bmp = get_bmp(bitmap_key, wide_icons, bitmap_type_name, 
                                preset.is_compatible, preset.is_system || preset.is_default, 
                                single_bar, filament_rgb, extruder_rgb);
        assert(bmp);

        const std::string name = preset.alias.empty() ? preset.name : preset.alias;
        if (preset.is_default || preset.is_system) {
            Append(wxString::FromUTF8((name + (preset.is_dirty ? Preset::suffix_modified() : "")).c_str()), *bmp);
            validate_selection(is_selected);
            if (is_selected)
                tooltip = wxString::FromUTF8(preset.name.c_str());
        }
        else
        {
            nonsys_presets.emplace(wxString::FromUTF8((name + (preset.is_dirty ? Preset::suffix_modified() : "")).c_str()), bmp);
            if (is_selected) {
                selected_user_preset = wxString::FromUTF8((name + (preset.is_dirty ? Preset::suffix_modified() : "")).c_str());
                tooltip = wxString::FromUTF8(preset.name.c_str());
            }
        }
        if (i + 1 == m_collection->num_default_presets())
            set_label_marker(Append(separator(L("System presets")), wxNullBitmap));
    }
    if (!nonsys_presets.empty())
    {
        set_label_marker(Append(separator(L("User presets")), wxNullBitmap));
        for (std::map<wxString, wxBitmap*>::iterator it = nonsys_presets.begin(); it != nonsys_presets.end(); ++it) {
            Append(it->first, *it->second);
            validate_selection(it->first == selected_user_preset);
        }
    }

    if (m_type == Preset::TYPE_PRINTER)
    {
        // add Physical printers, if any exists
        if (!m_preset_bundle->physical_printers.empty()) {
            set_label_marker(Append(separator(L("Physical printers")), wxNullBitmap));
            const PhysicalPrinterCollection& ph_printers = m_preset_bundle->physical_printers;

            for (PhysicalPrinterCollection::ConstIterator it = ph_printers.begin(); it != ph_printers.end(); ++it) {
                for (const std::string preset_name : it->get_preset_names()) {
                    Preset* preset = m_collection->find_preset(preset_name);
                    if (!preset)
                        continue;
                    std::string main_icon_name, bitmap_key = main_icon_name = preset->printer_technology() == ptSLA ? "sla_printer" : m_main_bitmap_name;
                    wxBitmap* bmp = get_bmp(main_icon_name, wide_icons, main_icon_name);
                    assert(bmp);

                    set_label_marker(Append(wxString::FromUTF8((it->get_full_name(preset_name) + (preset->is_dirty ? Preset::suffix_modified() : "")).c_str()), *bmp), LABEL_ITEM_PHYSICAL_PRINTER);
                    validate_selection(ph_printers.is_selected(it, preset_name));
                }
            }
        }
    }

    if (m_type == Preset::TYPE_PRINTER || m_type == Preset::TYPE_SLA_MATERIAL) {
        wxBitmap* bmp = get_bmp("edit_preset_list", wide_icons, "edit_uni");
        assert(bmp);

        if (m_type == Preset::TYPE_SLA_MATERIAL)
            set_label_marker(Append(separator(L("Add/Remove materials")), *bmp), LABEL_ITEM_WIZARD_MATERIALS);
        else
            set_label_marker(Append(separator(L("Add/Remove printers")), *bmp), LABEL_ITEM_WIZARD_PRINTERS);
    }

    update_selection();
    Thaw();

    if (!tooltip.IsEmpty())
        SetToolTip(tooltip);

    // Update control min size after rescale (changed Display DPI under MSW)
    if (GetMinWidth() != 20 * m_em_unit)
        SetMinSize(wxSize(20 * m_em_unit, GetSize().GetHeight()));
}

void PlaterPresetComboBox::msw_rescale()
{
    PresetComboBox::msw_rescale();
    edit_btn->msw_rescale();
}


// ---------------------------------
// ***  TabPresetComboBox  ***
// ---------------------------------

TabPresetComboBox::TabPresetComboBox(wxWindow* parent, Preset::Type preset_type) :
    PresetComboBox(parent, preset_type, wxSize(35 * wxGetApp().em_unit(), -1))
{
    Bind(wxEVT_COMBOBOX, [this](wxCommandEvent& evt) {
        // see https://github.com/prusa3d/PrusaSlicer/issues/3889
        // Under OSX: in case of use of a same names written in different case (like "ENDER" and "Ender")
        // m_presets_choice->GetSelection() will return first item, because search in PopupListCtrl is case-insensitive.
        // So, use GetSelection() from event parameter 
        auto selected_item = evt.GetSelection();

        auto marker = reinterpret_cast<Marker>(this->GetClientData(selected_item));
        if (marker >= LABEL_ITEM_DISABLED && marker < LABEL_ITEM_MAX) {
            this->SetSelection(this->m_last_selected);
            if (marker == LABEL_ITEM_WIZARD_PRINTERS)
                wxTheApp->CallAfter([this]() {
                wxGetApp().run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_PRINTERS);

                // update combobox if its parent is a PhysicalPrinterDialog
                PhysicalPrinterDialog* parent = dynamic_cast<PhysicalPrinterDialog*>(this->GetParent());
                if (parent != nullptr)
                    update();
            });
        }
        else if (on_selection_changed && (m_last_selected != selected_item || m_collection->current_is_dirty()) ) {
            m_last_selected = selected_item;
            on_selection_changed(selected_item);
        }

        evt.StopPropagation();
    });
}

// Update the choice UI from the list of presets.
// If show_incompatible, all presets are shown, otherwise only the compatible presets are shown.
// If an incompatible preset is selected, it is shown as well.
void TabPresetComboBox::update()
{
    Freeze();
    Clear();
    invalidate_selection();

    const std::deque<Preset>& presets = m_collection->get_presets();

    std::map<wxString, std::pair<wxBitmap*, bool>> nonsys_presets;
    wxString selected = "";
    if (!presets.front().is_visible)
        set_label_marker(Append(separator(L("System presets")), wxNullBitmap));
    int idx_selected = m_collection->get_selected_idx();

    PrinterTechnology proper_pt = ptAny;
    if (m_type == Preset::TYPE_PRINTER && m_preset_bundle->physical_printers.has_selection()) {
        std::string sel_preset_name = m_preset_bundle->physical_printers.get_selected_printer_preset_name();
        Preset* preset = m_collection->find_preset(sel_preset_name);
        if (preset)
            proper_pt = preset->printer_technology();
        else
            m_preset_bundle->physical_printers.unselect_printer();
    }

    for (size_t i = presets.front().is_visible ? 0 : m_collection->num_default_presets(); i < presets.size(); ++i)
    {
        const Preset& preset = presets[i];
        if (!preset.is_visible || (!show_incompatible && !preset.is_compatible && i != idx_selected))
            continue;
        
        // marker used for disable incompatible printer models for the selected physical printer
        bool is_enabled = true;

        std::string bitmap_key = "tab";
        if (m_type == Preset::TYPE_PRINTER) {
            bitmap_key += "_printer";
            if (preset.printer_technology() == ptSLA)
                bitmap_key += "_sla";
        }
        std::string main_icon_name = m_type == Preset::TYPE_PRINTER && preset.printer_technology() == ptSLA ? "sla_printer" : m_main_bitmap_name;

        wxBitmap* bmp = get_bmp(bitmap_key, main_icon_name, "lock_closed", is_enabled, preset.is_compatible, preset.is_system || preset.is_default);
        assert(bmp);

        if (preset.is_default || preset.is_system) {
            int item_id = Append(wxString::FromUTF8((preset.name + (preset.is_dirty ? Preset::suffix_modified() : "")).c_str()), *bmp);
            if (!is_enabled)
                set_label_marker(item_id, LABEL_ITEM_DISABLED);
            validate_selection(i == idx_selected);
        }
        else
        {
            std::pair<wxBitmap*, bool> pair(bmp, is_enabled);
            nonsys_presets.emplace(wxString::FromUTF8((preset.name + (preset.is_dirty ? Preset::suffix_modified() : "")).c_str()), std::pair<wxBitmap*, bool>(bmp, is_enabled));
            if (i == idx_selected)
                selected = wxString::FromUTF8((preset.name + (preset.is_dirty ? Preset::suffix_modified() : "")).c_str());
        }
        if (i + 1 == m_collection->num_default_presets())
            set_label_marker(Append(separator(L("System presets")), wxNullBitmap));
    }
    if (!nonsys_presets.empty())
    {
        set_label_marker(Append(separator(L("User presets")), wxNullBitmap));
        for (std::map<wxString, std::pair<wxBitmap*, bool>>::iterator it = nonsys_presets.begin(); it != nonsys_presets.end(); ++it) {
            int item_id = Append(it->first, *it->second.first);
            bool is_enabled = it->second.second;
            if (!is_enabled)
                set_label_marker(item_id, LABEL_ITEM_DISABLED);
            validate_selection(it->first == selected);
        }
    }

    if (m_type == Preset::TYPE_PRINTER)
    {
        // add Physical printers, if any exists
        if (!m_preset_bundle->physical_printers.empty()) {
            set_label_marker(Append(separator(L("Physical printers")), wxNullBitmap));
            const PhysicalPrinterCollection& ph_printers = m_preset_bundle->physical_printers;

            for (PhysicalPrinterCollection::ConstIterator it = ph_printers.begin(); it != ph_printers.end(); ++it) {
                for (const std::string preset_name : it->get_preset_names()) {
                    Preset* preset = m_collection->find_preset(preset_name);
                    if (!preset)
                        continue;
                    std::string main_icon_name = preset->printer_technology() == ptSLA ? "sla_printer" : m_main_bitmap_name;

                    wxBitmap* bmp = get_bmp(main_icon_name, main_icon_name, "", true, true, false);
                    assert(bmp);

                    set_label_marker(Append(wxString::FromUTF8((it->get_full_name(preset_name) + (preset->is_dirty ? Preset::suffix_modified() : "")).c_str()), *bmp), LABEL_ITEM_PHYSICAL_PRINTER);
                    validate_selection(ph_printers.is_selected(it, preset_name));
                }
            }
        }

        // add "Add/Remove printers" item
        std::string icon_name = "edit_uni";
        wxBitmap* bmp = get_bmp("edit_preset_list, tab,", icon_name, "");
        assert(bmp);

        set_label_marker(Append(separator(L("Add/Remove printers")), *bmp), LABEL_ITEM_WIZARD_PRINTERS);
    }

    update_selection();
    Thaw();
}

void TabPresetComboBox::msw_rescale()
{
    PresetComboBox::msw_rescale();
    wxSize sz = wxSize(35 * m_em_unit, -1);
    SetMinSize(sz);
    SetSize(sz);
}

void TabPresetComboBox::update_dirty()
{
    // 1) Update the dirty flag of the current preset.
    m_collection->update_dirty();

    // 2) Update the labels.
    wxWindowUpdateLocker noUpdates(this);
    for (unsigned int ui_id = 0; ui_id < GetCount(); ++ui_id) {
        auto marker = reinterpret_cast<Marker>(this->GetClientData(ui_id));
        if (marker >= LABEL_ITEM_MARKER)
            continue;

        std::string   old_label = GetString(ui_id).utf8_str().data();
        std::string   preset_name = Preset::remove_suffix_modified(old_label);
        std::string   ph_printer_name;

        if (marker == LABEL_ITEM_PHYSICAL_PRINTER) {
            ph_printer_name = PhysicalPrinter::get_short_name(preset_name);
            preset_name = PhysicalPrinter::get_preset_name(preset_name);
        }
            
        const Preset* preset = m_collection->find_preset(preset_name, false);
        if (preset) {
            std::string new_label = preset->is_dirty ? preset->name + Preset::suffix_modified() : preset->name;

            if (marker == LABEL_ITEM_PHYSICAL_PRINTER)
                new_label = ph_printer_name + PhysicalPrinter::separator() + new_label;

            if (old_label != new_label)
                SetString(ui_id, wxString::FromUTF8(new_label.c_str()));
        }
    }
#ifdef __APPLE__
    // wxWidgets on OSX do not upload the text of the combo box line automatically.
    // Force it to update by re-selecting.
    SetSelection(GetSelection());
#endif /* __APPLE __ */
}


//-----------------------------------------------
//          SavePresetDialog::Item
//-----------------------------------------------

SavePresetDialog::Item::Item(Preset::Type type, const std::string& suffix, wxBoxSizer* sizer, SavePresetDialog* parent):
    m_type(type),
    m_parent(parent)
{
    Tab* tab = wxGetApp().get_tab(m_type);
    assert(tab);
    m_presets = tab->get_presets();

    const Preset& sel_preset = m_presets->get_selected_preset();
    std::string preset_name =   sel_preset.is_default ? "Untitled" :
                                sel_preset.is_system ? (boost::format(("%1% - %2%")) % sel_preset.name % suffix).str() :
                                sel_preset.name;

    // if name contains extension
    if (boost::iends_with(preset_name, ".ini")) {
        size_t len = preset_name.length() - 4;
        preset_name.resize(len);
    }

    std::vector<std::string> values;
    for (const Preset& preset : *m_presets) {
        if (preset.is_default || preset.is_system || preset.is_external)
            continue;
        values.push_back(preset.name);
    }

    wxStaticText* label_top = new wxStaticText(m_parent, wxID_ANY, from_u8((boost::format(_utf8(L("Save %s as:"))) % into_u8(tab->title())).str()));

    m_valid_bmp = new wxStaticBitmap(m_parent, wxID_ANY, create_scaled_bitmap("tick_mark", m_parent));

    m_combo = new wxComboBox(m_parent, wxID_ANY, from_u8(preset_name), wxDefaultPosition, wxSize(35 * wxGetApp().em_unit(), -1));
    for (const std::string& value : values)
        m_combo->Append(from_u8(value));

    m_combo->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { update(); });
#ifdef __WXOSX__
    // Under OSX wxEVT_TEXT wasn't invoked after change selection in combobox,
    // So process wxEVT_COMBOBOX too
    m_combo->Bind(wxEVT_COMBOBOX, [this](wxCommandEvent&) { update(); });
#endif //__WXOSX__

    m_valid_label = new wxStaticText(m_parent, wxID_ANY, "");
    m_valid_label->SetFont(wxGetApp().bold_font());

    wxBoxSizer* combo_sizer = new wxBoxSizer(wxHORIZONTAL);
    combo_sizer->Add(m_valid_bmp,   0, wxALIGN_CENTER_VERTICAL | wxRIGHT, BORDER_W);
    combo_sizer->Add(m_combo,       1, wxEXPAND, BORDER_W);

    sizer->Add(label_top,       0, wxEXPAND | wxTOP| wxBOTTOM, BORDER_W);
    sizer->Add(combo_sizer,     0, wxEXPAND | wxBOTTOM, BORDER_W);
    sizer->Add(m_valid_label,   0, wxEXPAND | wxLEFT,   3*BORDER_W);

    if (m_type == Preset::TYPE_PRINTER)
        m_parent->add_info_for_edit_ph_printer(sizer);

    update();
}

void SavePresetDialog::Item::update()
{
    m_preset_name = into_u8(m_combo->GetValue());

    m_valid_type = Valid;
    wxString info_line;

    const char* unusable_symbols = "<>[]:/\\|?*\"";

    const std::string unusable_suffix = PresetCollection::get_suffix_modified();//"(modified)";
    for (size_t i = 0; i < std::strlen(unusable_symbols); i++) {
        if (m_preset_name.find_first_of(unusable_symbols[i]) != std::string::npos) {
            info_line = _L("The supplied name is not valid;") + "\n" +
                        _L("the following characters are not allowed:") + " " + unusable_symbols;
            m_valid_type = NoValid;
            break;
        }
    }

    if (m_valid_type == Valid && m_preset_name.find(unusable_suffix) != std::string::npos) {
        info_line = _L("The supplied name is not valid;") + "\n" +
                    _L("the following suffix is not allowed:") + "\n\t" +
                    from_u8(PresetCollection::get_suffix_modified());
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && m_preset_name == "- default -") {
        info_line = _L("The supplied name is not available.");
        m_valid_type = NoValid;
    }

    const Preset* existing = m_presets->find_preset(m_preset_name, false);
    if (m_valid_type == Valid && existing && (existing->is_default || existing->is_system)) {
        info_line = _L("Cannot overwrite a system profile.");
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && existing && (existing->is_external)) {
        info_line = _L("Cannot overwrite an external profile.");
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && existing && m_preset_name != m_presets->get_selected_preset_name())
    {
        info_line = from_u8((boost::format(_u8L("Preset with name \"%1%\" already exists.")) % m_preset_name).str());
        if (!existing->is_compatible)
            info_line += "\n" + _L("And selected preset is imcopatible with selected printer.");
        info_line += "\n" + _L("Note: This preset will be replaced after saving");
        m_valid_type = Warning;
    }

    if (m_valid_type == Valid && m_preset_name.empty()) {
        info_line = _L("The empty name is not available.");
        m_valid_type = NoValid;
    }

    m_valid_label->SetLabel(info_line);
    m_valid_label->Show(!info_line.IsEmpty());

    update_valid_bmp();

    if (m_type == Preset::TYPE_PRINTER)
        m_parent->update_info_for_edit_ph_printer(m_preset_name);

    m_parent->layout();
}

void SavePresetDialog::Item::update_valid_bmp()
{
    std::string bmp_name =  m_valid_type == Warning ? "exclamation" :
                            m_valid_type == NoValid ? "cross"       : "tick_mark" ;
    m_valid_bmp->SetBitmap(create_scaled_bitmap(bmp_name, m_parent));
}

void SavePresetDialog::Item::accept()
{
    if (m_valid_type == Warning)
        m_presets->delete_preset(m_preset_name);
}


//-----------------------------------------------
//          SavePresetDialog
//-----------------------------------------------

SavePresetDialog::SavePresetDialog(Preset::Type type, std::string suffix)
    : DPIDialog(nullptr, wxID_ANY, _L("Save preset"), wxDefaultPosition, wxSize(45 * wxGetApp().em_unit(), 5 * wxGetApp().em_unit()), wxDEFAULT_DIALOG_STYLE | wxICON_WARNING | wxRESIZE_BORDER)
{
    build(std::vector<Preset::Type>{type}, suffix);
}

SavePresetDialog::SavePresetDialog(std::vector<Preset::Type> types, std::string suffix)
    : DPIDialog(nullptr, wxID_ANY, _L("Save preset"), wxDefaultPosition, wxSize(45 * wxGetApp().em_unit(), 5 * wxGetApp().em_unit()), wxDEFAULT_DIALOG_STYLE | wxICON_WARNING | wxRESIZE_BORDER)
{
    build(types, suffix);
}

SavePresetDialog::~SavePresetDialog()
{
    for (auto  item : m_items) {
        delete item;
    }
}

void SavePresetDialog::build(std::vector<Preset::Type> types, std::string suffix)
{
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#if ENABLE_WX_3_1_3_DPI_CHANGED_EVENT
    // ys_FIXME! temporary workaround for correct font scaling
    // Because of from wxWidgets 3.1.3 auto rescaling is implemented for the Fonts,
    // From the very beginning set dialog font to the wxSYS_DEFAULT_GUI_FONT
    this->SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));
#endif // ENABLE_WX_3_1_3_DPI_CHANGED_EVENT

    if (suffix.empty())
        suffix = _CTX_utf8(L_CONTEXT("Copy", "PresetName"), "PresetName");

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    m_presets_sizer = new wxBoxSizer(wxVERTICAL);

    // Add first item
    for (Preset::Type type : types)
        AddItem(type, suffix);

    // Add dialog's buttons
    wxStdDialogButtonSizer* btns = this->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    wxButton* btnOK = static_cast<wxButton*>(this->FindWindowById(wxID_OK, this));
    btnOK->Bind(wxEVT_BUTTON,    [this](wxCommandEvent&)        { accept(); });
    btnOK->Bind(wxEVT_UPDATE_UI, [this](wxUpdateUIEvent& evt)   { evt.Enable(enable_ok_btn()); });

    topSizer->Add(m_presets_sizer,  0, wxEXPAND | wxALL, BORDER_W);
    topSizer->Add(btns,             0, wxEXPAND | wxALL, BORDER_W);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);
}

void SavePresetDialog::AddItem(Preset::Type type, const std::string& suffix)
{
    m_items.emplace_back(new Item{type, suffix, m_presets_sizer, this});
}

std::string SavePresetDialog::get_name()
{
    return m_items.front()->preset_name();
}

std::string SavePresetDialog::get_name(Preset::Type type)
{
    for (const Item* item : m_items)
        if (item->type() == type)
            return item->preset_name();
    return "";
}

bool SavePresetDialog::enable_ok_btn() const
{
    for (const Item* item : m_items)
        if (!item->is_valid())
            return false;

    return true;
}

void SavePresetDialog::add_info_for_edit_ph_printer(wxBoxSizer* sizer)
{
    PhysicalPrinterCollection& printers = wxGetApp().preset_bundle->physical_printers;
    m_ph_printer_name = printers.get_selected_printer_name();
    m_old_preset_name = printers.get_selected_printer_preset_name();

    wxString msg_text = from_u8((boost::format(_u8L("You have selected physical printer \"%1%\" \n"
                                                    "with related printer preset \"%2%\"")) %
                                                    m_ph_printer_name % m_old_preset_name).str());
    m_label = new wxStaticText(this, wxID_ANY, msg_text);
    m_label->SetFont(wxGetApp().bold_font());

    wxString choices[] = {"","",""};

    m_action_radio_box = new wxRadioBox(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize,
                                        WXSIZEOF(choices), choices, 3, wxRA_SPECIFY_ROWS);
    m_action_radio_box->SetSelection(0);
    m_action_radio_box->Bind(wxEVT_RADIOBOX, [this](wxCommandEvent& e) {
        m_action = (ActionType)e.GetSelection(); });
    m_action = ChangePreset;

    m_radio_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_radio_sizer->Add(m_action_radio_box, 1, wxEXPAND | wxTOP, 2*BORDER_W);

    sizer->Add(m_label,         0, wxEXPAND | wxLEFT | wxTOP,   3*BORDER_W);
    sizer->Add(m_radio_sizer,   1, wxEXPAND | wxLEFT,           3*BORDER_W);
}

void SavePresetDialog::update_info_for_edit_ph_printer(const std::string& preset_name)
{
    bool show = wxGetApp().preset_bundle->physical_printers.has_selection() && m_old_preset_name != preset_name;

    m_label->Show(show);
    m_radio_sizer->ShowItems(show);
    if (!show) {
        this->SetMinSize(wxSize(100,50));
        return;
    }

    wxString msg_text = from_u8((boost::format(_u8L("What would you like to do with \"%1%\" preset after saving?")) % preset_name).str());
    m_action_radio_box->SetLabel(msg_text);

    wxString choices[] = { from_u8((boost::format(_u8L("Change \"%1%\" to \"%2%\" for this physical printer \"%3%\"")) % m_old_preset_name % preset_name % m_ph_printer_name).str()),
                           from_u8((boost::format(_u8L("Add \"%1%\" as a next preset for the the physical printer \"%2%\"")) % preset_name % m_ph_printer_name).str()),
                           from_u8((boost::format(_u8L("Just switch to \"%1%\" preset")) % preset_name).str()) };

    int n = 0;
    for(const wxString& label: choices)
        m_action_radio_box->SetString(n++, label);
}

void SavePresetDialog::layout()
{
    this->Layout();
    this->Fit();
}

void SavePresetDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    const int& em = em_unit();

    msw_buttons_rescale(this, em, { wxID_OK, wxID_CANCEL });

    for (Item* item : m_items)
        item->update_valid_bmp();

    //const wxSize& size = wxSize(45 * em, 35 * em);
    SetMinSize(/*size*/wxSize(100, 50));

    Fit();
    Refresh();
}

void SavePresetDialog::update_physical_printers(const std::string& preset_name)
{
    if (m_action == UndefAction)
        return;

    PhysicalPrinterCollection& physical_printers = wxGetApp().preset_bundle->physical_printers;
    if (!physical_printers.has_selection())
        return;

    std::string printer_preset_name = physical_printers.get_selected_printer_preset_name();

    if (m_action == Switch)
        // unselect physical printer, if it was selected
        physical_printers.unselect_printer();
    else
    {
        PhysicalPrinter printer = physical_printers.get_selected_printer();

        if (m_action == ChangePreset)
            printer.delete_preset(printer_preset_name);

        if (printer.add_preset(preset_name))
            physical_printers.save_printer(printer);

        physical_printers.select_printer(printer.get_full_name(preset_name));
    }    
}

void SavePresetDialog::accept()
{
    for (Item* item : m_items) {
        item->accept();
        if (item->type() == Preset::TYPE_PRINTER)
            update_physical_printers(item->preset_name());
    }

    EndModal(wxID_OK);
}



}}    // namespace Slic3r::GUI
