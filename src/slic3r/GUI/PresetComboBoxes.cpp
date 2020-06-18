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

#include "libslic3r/libslic3r.h"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "Tab.hpp"
#include "PrintHostDialogs.hpp"
#include "ConfigWizard.hpp"
#include "../Utils/ASCIIFolding.hpp"
#include "../Utils/PrintHost.hpp"
#include "../Utils/FixModelByWin10.hpp"
#include "../Utils/UndoRedo.hpp"
#include "RemovableDriveManager.hpp"
#include "BitmapCache.hpp"

using Slic3r::GUI::format_wxstr;

static const std::pair<unsigned int, unsigned int> THUMBNAIL_SIZE_3MF = { 256, 256 };

namespace Slic3r {
namespace GUI {

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
    m_preset_bundle(wxGetApp().preset_bundle),
    m_bitmap_cache(new BitmapCache)
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
    m_bitmapLock         = ScalableBitmap(this, "lock_closed");

    // parameters for an icon's drawing
    fill_width_height();
}

PresetComboBox::~PresetComboBox()
{
    delete m_bitmap_cache;
    m_bitmap_cache = nullptr;
}

void PresetComboBox::set_label_marker(int item, LabelItemType label_item_type)
{
    this->SetClientData(item, (void*)label_item_type);
}

void PresetComboBox::msw_rescale()
{
    m_em_unit = em_unit(this);

    m_bitmapLock.msw_rescale();
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
    // set a bitmap's height to m_bitmapLock->GetHeight() and norm_icon_width to m_bitmapLock->GetWidth()
    icon_height = m_bitmapLock.GetBmpHeight();
    norm_icon_width = m_bitmapLock.GetBmpWidth();

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
            if (marker == LABEL_ITEM_PHYSICAL_PRINTERS)
            {
                PhysicalPrinterDialog dlg(_L("New Physical Printer"), this->m_last_selected);
                dlg.ShowModal();
                return;
            }
            if (marker >= LABEL_ITEM_WIZARD_PRINTERS) {
                ConfigWizard::StartPage sp = ConfigWizard::SP_WELCOME;
                switch (marker) {
                    case LABEL_ITEM_WIZARD_PRINTERS: sp = ConfigWizard::SP_PRINTERS; break;
                    case LABEL_ITEM_WIZARD_FILAMENTS: sp = ConfigWizard::SP_FILAMENTS; break;
                    case LABEL_ITEM_WIZARD_MATERIALS: sp = ConfigWizard::SP_MATERIALS; break;
                    default: break;
                }
                wxTheApp->CallAfter([sp]() { wxGetApp().run_wizard(ConfigWizard::RR_USER, sp); });
            }
        } else if ( this->m_last_selected != selected_item || m_collection->current_is_dirty() ) {
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
            bool          wide_icons = selected_preset != nullptr && !selected_preset->is_compatible;
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
        Tab* tab = wxGetApp().get_tab(m_type);
        if (!tab)
            return;

        int page_id = wxGetApp().tab_panel()->FindPage(tab);
        if (page_id == wxNOT_FOUND)
            return;

        wxGetApp().tab_panel()->SetSelection(page_id);

        // Switch to Settings NotePad
        wxGetApp().mainframe->select_tab();

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
                tab->select_preset(preset_name);
            }
        }
    });
}

PlaterPresetComboBox::~PlaterPresetComboBox()
{
    if (edit_btn)
        edit_btn->Destroy();
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
    size_t selected_preset_item = INT_MAX; // some value meaning that no one item is selected

    const Preset* selected_filament_preset;
    std::string extruder_color;
    if (m_type == Preset::TYPE_FILAMENT)
    {
        unsigned char rgb[3];
        extruder_color = m_preset_bundle->printers.get_edited_preset().config.opt_string("extruder_colour", (unsigned int)m_extruder_idx);
        if (!m_bitmap_cache->parse_color(extruder_color, rgb))
            // Extruder color is not defined.
            extruder_color.clear();
        selected_filament_preset = m_collection->find_preset(m_preset_bundle->filament_presets[m_extruder_idx]);
        assert(selected_filament_preset);
    }

    const Preset& selected_preset = m_type == Preset::TYPE_FILAMENT ? *selected_filament_preset : m_collection->get_selected_preset();
    // Show wide icons if the currently selected preset is not compatible with the current printer,
    // and draw a red flag in front of the selected preset.
    bool wide_icons = !selected_preset.is_compatible;

    std::map<wxString, wxBitmap*> nonsys_presets;
    std::map<wxString, wxBitmap*> physical_printers;

    wxString selected = "";
    wxString tooltip = "";
    const std::deque<Preset>& presets = m_collection->get_presets();

    if (!presets.front().is_visible)
        this->set_label_marker(this->Append(separator(L("System presets")), wxNullBitmap));

    for (size_t i = presets.front().is_visible ? 0 : m_collection->num_default_presets(); i < presets.size(); ++i) 
    {
        const Preset& preset = presets[i];
        bool is_selected = m_type == Preset::TYPE_FILAMENT ?
                        m_preset_bundle->filament_presets[m_extruder_idx] == preset.name :
                        i == m_collection->get_selected_idx();

        if (!preset.is_visible || (!preset.is_compatible && !is_selected))
            continue;

        std::string bitmap_key, filament_rgb, extruder_rgb;
        bool single_bar = false;
        if (m_type == Preset::TYPE_PRINTER && preset.printer_technology() == ptSLA)
            bitmap_key = "sla_printer";
        else if (m_type == Preset::TYPE_FILAMENT)
        {
            // Assign an extruder color to the selected item if the extruder color is defined.
            filament_rgb = preset.config.opt_string("filament_colour", 0);
            extruder_rgb = (selected && !extruder_color.empty()) ? extruder_color : filament_rgb;
            single_bar = filament_rgb == extruder_rgb;

            bitmap_key = single_bar ? filament_rgb : filament_rgb + extruder_rgb;
        }
        wxBitmap    main_bmp   = create_scaled_bitmap(m_type == Preset::TYPE_PRINTER && preset.printer_technology() == ptSLA ? "sla_printer" : m_main_bitmap_name);

        // If the filament preset is not compatible and there is a "red flag" icon loaded, show it left
        // to the filament color image.
        if (wide_icons)
            bitmap_key += preset.is_compatible ? ",cmpt" : ",ncmpt";
        bitmap_key += (preset.is_system || preset.is_default) ? ",syst" : ",nsyst";
        bitmap_key += "-h" + std::to_string(icon_height);

        wxBitmap* bmp = m_bitmap_cache->find(bitmap_key);
        if (bmp == nullptr) {
            // Create the bitmap with color bars.
            std::vector<wxBitmap> bmps;
            if (wide_icons)
                // Paint a red flag for incompatible presets.
                bmps.emplace_back(preset.is_compatible ? m_bitmap_cache->mkclear(norm_icon_width, icon_height) : m_bitmapIncompatible.bmp());

            if (m_type == Preset::TYPE_FILAMENT)
            {
                unsigned char rgb[3];
                // Paint the color bars.
                m_bitmap_cache->parse_color(filament_rgb, rgb);
                bmps.emplace_back(m_bitmap_cache->mksolid(single_bar ? wide_icon_width : norm_icon_width, icon_height, rgb));
                if (!single_bar) {
                    m_bitmap_cache->parse_color(extruder_rgb, rgb);
                    bmps.emplace_back(m_bitmap_cache->mksolid(thin_icon_width, icon_height, rgb));
                }
                // Paint a lock at the system presets.
                bmps.emplace_back(m_bitmap_cache->mkclear(space_icon_width, icon_height));
            }
            else
            {
                // Paint the color bars.
                bmps.emplace_back(m_bitmap_cache->mkclear(thin_space_icon_width, icon_height));
                bmps.emplace_back(main_bmp);
                // Paint a lock at the system presets.
                bmps.emplace_back(m_bitmap_cache->mkclear(wide_space_icon_width, icon_height));
            }
            bmps.emplace_back((preset.is_system || preset.is_default) ? m_bitmapLock.bmp() : m_bitmap_cache->mkclear(norm_icon_width, icon_height));
            bmp = m_bitmap_cache->insert(bitmap_key, bmps);
        }

        const std::string name = preset.alias.empty() ? preset.name : preset.alias;
        if (preset.is_default || preset.is_system) {
            Append(wxString::FromUTF8((name + (preset.is_dirty ? Preset::suffix_modified() : "")).c_str()),
                !bmp ? main_bmp : *bmp);
            if (is_selected ||
                // just in case: mark selected_preset_item as a first added element
                selected_preset_item == INT_MAX) {
                selected_preset_item = GetCount() - 1;
                tooltip = wxString::FromUTF8(preset.name.c_str());
            }
        }
        else
        {
            nonsys_presets.emplace(wxString::FromUTF8((name + (preset.is_dirty ? Preset::suffix_modified() : "")).c_str()), bmp);
            if (is_selected) {
                selected = wxString::FromUTF8((name + (preset.is_dirty ? Preset::suffix_modified() : "")).c_str());
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
            if (it->first == selected ||
                // just in case: mark selected_preset_item as a first added element
                selected_preset_item == INT_MAX)
                selected_preset_item = GetCount() - 1;
        }
    }
    if (!physical_printers.empty())
    {
        set_label_marker(Append(separator(L("Physical printers")), wxNullBitmap));
        for (std::map<wxString, wxBitmap*>::iterator it = physical_printers.begin(); it != physical_printers.end(); ++it) {
            Append(it->first, *it->second);
            if (it->first == selected ||
                // just in case: mark selected_preset_item as a first added element
                selected_preset_item == INT_MAX)
                selected_preset_item = GetCount() - 1;
        }
    }

    if (m_type == Preset::TYPE_PRINTER || m_type == Preset::TYPE_SLA_MATERIAL) {
        std::string   bitmap_key = "";
        // If the filament preset is not compatible and there is a "red flag" icon loaded, show it left
        // to the filament color image.
        if (wide_icons)
            bitmap_key += "wide,";
        bitmap_key += "edit_preset_list";
        bitmap_key += "-h" + std::to_string(icon_height);

        wxBitmap* bmp = m_bitmap_cache->find(bitmap_key);
        if (bmp == nullptr) {
            // Create the bitmap with color bars.update_plater_ui
            std::vector<wxBitmap> bmps;
            if (wide_icons)
                // Paint a red flag for incompatible presets.
                bmps.emplace_back(m_bitmap_cache->mkclear(norm_icon_width, icon_height));
            // Paint the color bars.
            bmps.emplace_back(m_bitmap_cache->mkclear(thin_space_icon_width, icon_height));
            bmps.emplace_back(create_scaled_bitmap(m_main_bitmap_name));
            // Paint a lock at the system presets.
            bmps.emplace_back(m_bitmap_cache->mkclear(wide_space_icon_width, icon_height));
            bmps.emplace_back(create_scaled_bitmap("edit_uni"));
            bmp = m_bitmap_cache->insert(bitmap_key, bmps);
        }
        if (m_type == Preset::TYPE_SLA_MATERIAL)
            set_label_marker(Append(separator(L("Add/Remove materials")), *bmp), LABEL_ITEM_WIZARD_MATERIALS);
        else
            set_label_marker(Append(separator(L("Add/Remove printers")), *bmp), LABEL_ITEM_WIZARD_PRINTERS);
    }
    if (m_type == Preset::TYPE_PRINTER) {
        std::string   bitmap_key = "";
        if (wide_icons)
            bitmap_key += "wide,";
        bitmap_key += "edit_preset_list";
        bitmap_key += "-h" + std::to_string(icon_height);

        wxBitmap* bmp = m_bitmap_cache->find(bitmap_key);
        if (bmp == nullptr) {
            // Create the bitmap with color bars.
            std::vector<wxBitmap> bmps;
            if (wide_icons)
                // Paint a red flag for incompatible presets.
                bmps.emplace_back(m_bitmap_cache->mkclear(norm_icon_width, icon_height));
            // Paint the color bars.
            bmps.emplace_back(m_bitmap_cache->mkclear(thin_space_icon_width, icon_height));
            bmps.emplace_back(create_scaled_bitmap("printer"));
            // Paint a lock at the system presets.
            bmps.emplace_back(m_bitmap_cache->mkclear(wide_space_icon_width, icon_height));
            bmps.emplace_back(create_scaled_bitmap("edit_uni"));
            bmp = m_bitmap_cache->insert(bitmap_key, bmps);
        }
        set_label_marker(Append(separator(L("Add physical printer")), *bmp), LABEL_ITEM_PHYSICAL_PRINTERS);
    }

    /* But, if selected_preset_item is still equal to INT_MAX, it means that
     * there is no presets added to the list.
     * So, select last combobox item ("Add/Remove preset")
     */
    if (selected_preset_item == INT_MAX)
        selected_preset_item = GetCount() - 1;

    SetSelection(selected_preset_item);
    SetToolTip(tooltip.IsEmpty() ? GetString(selected_preset_item) : tooltip);
    m_last_selected = selected_preset_item;
    Thaw();

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

TabPresetComboBox::TabPresetComboBox(wxWindow* parent, Preset::Type preset_type, bool is_from_physical_printer/* = false*/) :
    PresetComboBox(parent, preset_type, wxSize(35 * wxGetApp().em_unit(), -1))
{
    Bind(wxEVT_COMBOBOX, [this, is_from_physical_printer](wxCommandEvent& evt) {
        // see https://github.com/prusa3d/PrusaSlicer/issues/3889
        // Under OSX: in case of use of a same names written in different case (like "ENDER" and "Ender")
        // m_presets_choice->GetSelection() will return first item, because search in PopupListCtrl is case-insensitive.
        // So, use GetSelection() from event parameter 
        auto selected_item = evt.GetSelection();

        auto marker = reinterpret_cast<Marker>(this->GetClientData(selected_item));
        if (marker >= LABEL_ITEM_MARKER && marker < LABEL_ITEM_MAX) {
            this->SetSelection(this->m_last_selected);
            if (marker == LABEL_ITEM_WIZARD_PRINTERS)
                wxTheApp->CallAfter([this, is_from_physical_printer]() {
                wxGetApp().run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_PRINTERS);
                if (is_from_physical_printer)
                    update();
            });
        }
        else if ( is_from_physical_printer) {
            // do nothing
        }
        else if (m_last_selected != selected_item || m_collection->current_is_dirty() ) {
            std::string selected_string = this->GetString(selected_item).ToUTF8().data();
            Tab* tab = wxGetApp().get_tab(this->m_type);
            assert (tab);
            tab->select_preset(selected_string);
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
    size_t selected_preset_item = INT_MAX; // some value meaning that no one item is selected

    const std::deque<Preset>& presets = m_collection->get_presets();

    std::map<wxString, wxBitmap*> nonsys_presets;
    wxString selected = "";
    if (!presets.front().is_visible)
        set_label_marker(Append(separator(L("System presets")), wxNullBitmap));
    int idx_selected = m_collection->get_selected_idx();
    for (size_t i = presets.front().is_visible ? 0 : m_collection->num_default_presets(); i < presets.size(); ++i) {
        const Preset& preset = presets[i];
        if (!preset.is_visible || (!show_incompatible && !preset.is_compatible && i != idx_selected))
            continue;

        std::string   bitmap_key = "tab";
        wxBitmap main_bmp = create_scaled_bitmap(m_type == Preset::TYPE_PRINTER && preset.printer_technology() == ptSLA ? "sla_printer" : m_main_bitmap_name, this);
        if (m_type == Preset::TYPE_PRINTER) {
            bitmap_key += "_printer";
            if (preset.printer_technology() == ptSLA)
                bitmap_key += "_sla";
        }
        bitmap_key += preset.is_compatible ? ",cmpt" : ",ncmpt";
        bitmap_key += (preset.is_system || preset.is_default) ? ",syst" : ",nsyst";
        bitmap_key += "-h" + std::to_string(icon_height);

        wxBitmap* bmp = m_bitmap_cache->find(bitmap_key);
        if (bmp == nullptr) {
            // Create the bitmap with color bars.
            std::vector<wxBitmap> bmps;
            bmps.emplace_back(m_type == Preset::TYPE_PRINTER ? main_bmp : preset.is_compatible ? m_bitmapCompatible.bmp() : m_bitmapIncompatible.bmp());
            // Paint a lock at the system presets.
            bmps.emplace_back((preset.is_system || preset.is_default) ? m_bitmapLock.bmp() : m_bitmap_cache->mkclear(norm_icon_width, icon_height));
            bmp = m_bitmap_cache->insert(bitmap_key, bmps);
        }

        if (preset.is_default || preset.is_system) {
            Append(wxString::FromUTF8((preset.name + (preset.is_dirty ? Preset::suffix_modified() : "")).c_str()),
                (bmp == 0) ? main_bmp : *bmp);
            if (i == idx_selected ||
                // just in case: mark selected_preset_item as a first added element
                selected_preset_item == INT_MAX)
                selected_preset_item = GetCount() - 1;
        }
        else
        {
            nonsys_presets.emplace(wxString::FromUTF8((preset.name + (preset.is_dirty ? Preset::suffix_modified() : "")).c_str()), bmp);
            if (i == idx_selected)
                selected = wxString::FromUTF8((preset.name + (preset.is_dirty ? Preset::suffix_modified() : "")).c_str());
        }
        if (i + 1 == m_collection->num_default_presets())
            set_label_marker(Append(separator(L("System presets")), wxNullBitmap));
    }
    if (!nonsys_presets.empty())
    {
        set_label_marker(Append(separator(L("User presets")), wxNullBitmap));
        for (std::map<wxString, wxBitmap*>::iterator it = nonsys_presets.begin(); it != nonsys_presets.end(); ++it) {
            Append(it->first, *it->second);
            if (it->first == selected ||
                // just in case: mark selected_preset_item as a first added element
                selected_preset_item == INT_MAX)
                selected_preset_item = GetCount() - 1;
        }
    }
    if (m_type == Preset::TYPE_PRINTER) {
        std::string   bitmap_key = "edit_preset_list";
        bitmap_key += "-h" + std::to_string(icon_height);

        wxBitmap* bmp = m_bitmap_cache->find(bitmap_key);
        if (bmp == nullptr) {
            // Create the bitmap with color bars.
            std::vector<wxBitmap> bmps;
            bmps.emplace_back(create_scaled_bitmap(m_main_bitmap_name, this));
            bmps.emplace_back(create_scaled_bitmap("edit_uni", this));
            bmp = m_bitmap_cache->insert(bitmap_key, bmps);
        }
        set_label_marker(Append(separator(L("Add/Remove printers")), *bmp), LABEL_ITEM_WIZARD_PRINTERS);
    }

    /* But, if selected_preset_item is still equal to INT_MAX, it means that
     * there is no presets added to the list.
     * So, select last combobox item ("Add/Remove preset")
     */
    if (selected_preset_item == INT_MAX)
        selected_preset_item = GetCount() - 1;

    SetSelection(selected_preset_item);
    SetToolTip(GetString(selected_preset_item));
    Thaw();

    m_last_selected = selected_preset_item;
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
        std::string   old_label = GetString(ui_id).utf8_str().data();
        std::string   preset_name = Preset::remove_suffix_modified(old_label);
        const Preset* preset = m_collection->find_preset(preset_name, false);
        if (preset) {
            std::string new_label = preset->is_dirty ? preset->name + Preset::suffix_modified() : preset->name;
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


//------------------------------------------
//          PhysicalPrinterDialog
//------------------------------------------


PhysicalPrinterDialog::PhysicalPrinterDialog(const wxString& printer_name, int last_selected_preset)
    : DPIDialog(NULL, wxID_ANY, _L("PhysicalPrinter"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    SetFont(wxGetApp().normal_font());
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    int border  = 10;
    int em      = em_unit();

    printer_text    = new wxTextCtrl(this, wxID_ANY, printer_name, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    printer_presets = new TabPresetComboBox(this, Preset::TYPE_PRINTER, true);
    printer_presets->update();

    wxStdDialogButtonSizer* btns = this->CreateStdDialogButtonSizer(wxOK | wxCANCEL);

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    topSizer->Add(printer_text      , 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(printer_presets   , 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(btns              , 0, wxEXPAND | wxALL, border); 

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);
}

void PhysicalPrinterDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    const int& em = em_unit();

    msw_buttons_rescale(this, em, { wxID_OK, wxID_CANCEL });

    const wxSize& size = wxSize(40 * em, 30 * em);
    SetMinSize(size);

    Fit();
    Refresh();
}


}}    // namespace Slic3r::GUI
