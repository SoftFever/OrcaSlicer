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
#include "PrintHostDialogs.hpp"
#include "ConfigWizard.hpp"
#include "../Utils/ASCIIFolding.hpp"
#include "../Utils/PrintHost.hpp"
#include "../Utils/FixModelByWin10.hpp"
#include "../Utils/UndoRedo.hpp"
#include "RemovableDriveManager.hpp"
#include "BitmapCache.hpp"
#include "BonjourDialog.hpp"

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
                PhysicalPrinterDialog dlg(wxEmptyString);
                if (dlg.ShowModal() == wxID_OK)
                    this->update();
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
        // In a case of a physical printer, for its editing open PhysicalPrinterDialog
        if (m_type == Preset::TYPE_PRINTER && this->is_selected_physical_printer()) {
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

bool PlaterPresetComboBox::is_selected_physical_printer()
{
    auto selected_item = this->GetSelection();
    auto marker = reinterpret_cast<Marker>(this->GetClientData(selected_item));
    return marker == LABEL_ITEM_PHYSICAL_PRINTER;
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

void PlaterPresetComboBox::show_edit_menu()
{
    wxMenu* menu = new wxMenu();

    append_menu_item(menu, wxID_ANY, _L("Edit related printer profile"), "",
        [this](wxCommandEvent&) { this->switch_to_tab(); }, "cog", menu, []() { return true; }, wxGetApp().plater());

    append_menu_item(menu, wxID_ANY, _L("Edit physical printer"), "",
        [this](wxCommandEvent&) {
            PhysicalPrinterDialog dlg(this->GetString(this->GetSelection()));
            if (dlg.ShowModal() == wxID_OK)
                update();
        }, "cog", menu, []() { return true; }, wxGetApp().plater());

    append_menu_item(menu, wxID_ANY, _L("Delete physical printer"), "",
        [this](wxCommandEvent&) {
            const std::string& printer_name = m_preset_bundle->physical_printers.get_selected_printer_name();
            if (printer_name.empty())
                return;

            const wxString msg = from_u8((boost::format(_u8L("Are you sure you want to delete \"%1%\" printer?")) % printer_name).str());
            if (wxMessageDialog(this, msg, _L("Delete Physical Printer"), wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION).ShowModal() != wxID_YES)
                return;

            m_preset_bundle->physical_printers.delete_selected_printer();
            update();
        }, "cross", menu, []() { return true; }, wxGetApp().plater());

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

    wxString selected = "";
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

    if (m_type == Preset::TYPE_PRINTER)
    {
        // add Physical printers, if any exists
        if (!m_preset_bundle->physical_printers.empty()) {
            set_label_marker(Append(separator(L("Physical printers")), wxNullBitmap));
            const PhysicalPrinterCollection& ph_printers = m_preset_bundle->physical_printers;

            for (PhysicalPrinterCollection::ConstIterator it = ph_printers.begin(); it != ph_printers.end(); ++it) {
                std::string   bitmap_key = it->printer_technology() == ptSLA ? "sla_printer" : m_main_bitmap_name;
                if (wide_icons)
                    bitmap_key += "wide,";
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
                    bmps.emplace_back(create_scaled_bitmap(it->printer_technology() == ptSLA ? "sla_printer" : m_main_bitmap_name));
                    // Paint a lock at the system presets.
                    bmps.emplace_back(m_bitmap_cache->mkclear(wide_space_icon_width+norm_icon_width, icon_height));
                    bmp = m_bitmap_cache->insert(bitmap_key, bmps);
                }

                set_label_marker(Append(wxString::FromUTF8((it->name).c_str()), *bmp), LABEL_ITEM_PHYSICAL_PRINTER);
                if (ph_printers.has_selection() && it->name == ph_printers.get_selected_printer_name() ||
                    // just in case: mark selected_preset_item as a first added element
                    selected_preset_item == INT_MAX)
                    selected_preset_item = GetCount() - 1;
            }
        }

        // add LABEL_ITEM_PHYSICAL_PRINTERS
        std::string   bitmap_key;
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
        if (marker >= LABEL_ITEM_MARKER && marker < LABEL_ITEM_MAX) {
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
        else if (on_selection_changed && (m_last_selected != selected_item || m_collection->current_is_dirty()) )
            on_selection_changed(selected_item);

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


PhysicalPrinterDialog::PhysicalPrinterDialog(wxString printer_name)
    : DPIDialog(NULL, wxID_ANY, _L("Physical Printer"), wxDefaultPosition, wxSize(45 * wxGetApp().em_unit(), -1), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    SetFont(wxGetApp().normal_font());
    SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));

    int border  = 10;

    m_printer_presets = new TabPresetComboBox(this, Preset::TYPE_PRINTER);
    m_printer_presets->set_selection_changed_function([this](int selection) {
        std::string selected_string = Preset::remove_suffix_modified(m_printer_presets->GetString(selection).ToUTF8().data());
        Preset* preset = wxGetApp().preset_bundle->printers.find_preset(selected_string);
        assert(preset);
        Preset& edited_preset = wxGetApp().preset_bundle->printers.get_edited_preset();
        if (preset->name == edited_preset.name)
            preset = &edited_preset;
        m_printer.update_from_preset(*preset);

        // update values
        m_optgroup->reload_config();
        update();
    });
    m_printer_presets->update();

    wxString preset_name = m_printer_presets->GetString(m_printer_presets->GetSelection());

    if (printer_name.IsEmpty())
        printer_name = preset_name + " - "+_L("Physical Printer");
    m_printer_name    = new wxTextCtrl(this, wxID_ANY, printer_name, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);

    PhysicalPrinterCollection& printers = wxGetApp().preset_bundle->physical_printers;
    PhysicalPrinter* printer = printers.find_printer(into_u8(printer_name));
    if (!printer) {
        const Preset& preset = wxGetApp().preset_bundle->printers.get_edited_preset();
        printer = new PhysicalPrinter(into_u8(printer_name), preset);
    }
    assert(printer);
    m_printer = *printer;

    m_config = &m_printer.config;

    m_optgroup = new ConfigOptionsGroup(this, _L("Print Host upload"), m_config);
    build_printhost_settings(m_optgroup);
    m_optgroup->reload_config();

    wxStdDialogButtonSizer* btns = this->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    wxButton* btnOK = static_cast<wxButton*>(this->FindWindowById(wxID_OK, this));
    btnOK->Bind(wxEVT_BUTTON, &PhysicalPrinterDialog::OnOK, this);

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    topSizer->Add(m_printer_name      , 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(m_printer_presets   , 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(m_optgroup->sizer   , 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, border);
    topSizer->Add(btns                , 0, wxEXPAND | wxALL, border); 

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);
}

void PhysicalPrinterDialog::build_printhost_settings(ConfigOptionsGroup* m_optgroup)
{
    m_optgroup->m_on_change = [this](t_config_option_key opt_key, boost::any value) {
        if (opt_key == "authorization_type")
            this->update();
    };

    m_optgroup->append_single_option_line("host_type");

    auto create_sizer_with_btn = [this](wxWindow* parent, ScalableButton** btn, const std::string& icon_name, const wxString& label) {
        *btn = new ScalableButton(parent, wxID_ANY, icon_name, label, wxDefaultSize, wxDefaultPosition, wxBU_LEFT | wxBU_EXACTFIT);
        (*btn)->SetFont(wxGetApp().normal_font());

        auto sizer = new wxBoxSizer(wxHORIZONTAL);
        sizer->Add(*btn);
        return sizer;
    };

    auto printhost_browse = [=](wxWindow* parent) 
    {
        auto sizer = create_sizer_with_btn(parent, &m_printhost_browse_btn, "browse", _L("Browse") + " " + dots);
        m_printhost_browse_btn->Bind(wxEVT_BUTTON, [=](wxCommandEvent& e) {
            BonjourDialog dialog(this, Preset::printer_technology(m_printer.config));
            if (dialog.show_and_lookup()) {
                m_optgroup->set_value("print_host", std::move(dialog.get_selected()), true);
                m_optgroup->get_field("print_host")->field_changed();
            }
        });

        return sizer;
    };

    auto print_host_test = [=](wxWindow* parent) {
        auto sizer = create_sizer_with_btn(parent, &m_printhost_test_btn, "test", _L("Test"));

        m_printhost_test_btn->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
            std::unique_ptr<PrintHost> host(PrintHost::get_print_host(m_config));
            if (!host) {
                const wxString text = _L("Could not get a valid Printer Host reference");
                show_error(this, text);
                return;
            }
            wxString msg;
            if (host->test(msg)) {
                show_info(this, host->get_test_ok_msg(), _L("Success!"));
            }
            else {
                show_error(this, host->get_test_failed_msg(msg));
            }
            });

        return sizer;
    };

    // Set a wider width for a better alignment
    Option option = m_optgroup->get_option("print_host");
    option.opt.width = Field::def_width_wider();
    Line host_line = m_optgroup->create_single_option_line(option);
    host_line.append_widget(printhost_browse);
    host_line.append_widget(print_host_test);
    m_optgroup->append_line(host_line);

    m_optgroup->append_single_option_line("authorization_type");

    option = m_optgroup->get_option("printhost_apikey");
    option.opt.width = Field::def_width_wider();
    m_optgroup->append_single_option_line(option);

    const auto ca_file_hint = _u8L("HTTPS CA file is optional. It is only needed if you use HTTPS with a self-signed certificate.");

    if (Http::ca_file_supported()) {
        option = m_optgroup->get_option("printhost_cafile");
        option.opt.width = Field::def_width_wider();
        Line cafile_line = m_optgroup->create_single_option_line(option);

        auto printhost_cafile_browse = [=](wxWindow* parent) {
            auto sizer = create_sizer_with_btn(parent, &m_printhost_cafile_browse_btn, "browse", _L("Browse") + " " + dots);
            m_printhost_cafile_browse_btn->Bind(wxEVT_BUTTON, [this, m_optgroup](wxCommandEvent e) {
                static const auto filemasks = _L("Certificate files (*.crt, *.pem)|*.crt;*.pem|All files|*.*");
                wxFileDialog openFileDialog(this, _L("Open CA certificate file"), "", "", filemasks, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
                if (openFileDialog.ShowModal() != wxID_CANCEL) {
                    m_optgroup->set_value("printhost_cafile", std::move(openFileDialog.GetPath()), true);
                    m_optgroup->get_field("printhost_cafile")->field_changed();
                }
                });

            return sizer;
        };

        cafile_line.append_widget(printhost_cafile_browse);
        m_optgroup->append_line(cafile_line);

        Line cafile_hint{ "", "" };
        cafile_hint.full_width = 1;
        cafile_hint.widget = [this, ca_file_hint](wxWindow* parent) {
            auto txt = new wxStaticText(parent, wxID_ANY, ca_file_hint);
            auto sizer = new wxBoxSizer(wxHORIZONTAL);
            sizer->Add(txt);
            return sizer;
        };
        m_optgroup->append_line(cafile_hint);
    }
    else {
        Line line{ "", "" };
        line.full_width = 1;

        line.widget = [ca_file_hint](wxWindow* parent) {
            std::string info = _u8L("HTTPS CA File") + ":\n\t" +
                (boost::format(_u8L("On this system, %s uses HTTPS certificates from the system Certificate Store or Keychain.")) % SLIC3R_APP_NAME).str() +
                "\n\t" + _u8L("To use a custom CA file, please import your CA file into Certificate Store / Keychain.");

            //auto txt = new wxStaticText(parent, wxID_ANY, from_u8((boost::format("%1%\n\n\t%2%") % info % ca_file_hint).str()));
            auto txt = new wxStaticText(parent, wxID_ANY, from_u8((boost::format("%1%\n\t%2%") % info % ca_file_hint).str()));
            txt->SetFont(wxGetApp().normal_font());
            auto sizer = new wxBoxSizer(wxHORIZONTAL);
            sizer->Add(txt, 1, wxEXPAND);
            return sizer;
        };

        m_optgroup->append_line(line);
    }

    for (const std::string& opt_key : std::vector<std::string>{ "login", "password" }) {        
        option = m_optgroup->get_option(opt_key);
        option.opt.width = Field::def_width_wider();
        m_optgroup->append_single_option_line(option);
    }

    update();
}

void PhysicalPrinterDialog::update()
{
    const PrinterTechnology tech = Preset::printer_technology(m_printer.config);
    // Only offer the host type selection for FFF, for SLA it's always the SL1 printer (at the moment)
    if (tech == ptFFF) {
        m_optgroup->show_field("host_type");
        m_optgroup->hide_field("authorization_type");
        for (const std::string& opt_key : std::vector<std::string>{ "login", "password" })
            m_optgroup->hide_field(opt_key);
    }
    else {
        m_optgroup->set_value("host_type", int(PrintHostType::htOctoPrint), false);
        m_optgroup->hide_field("host_type");

        m_optgroup->show_field("authorization_type");

        AuthorizationType auth_type = m_config->option<ConfigOptionEnum<AuthorizationType>>("authorization_type")->value;
        m_optgroup->show_field("printhost_apikey", auth_type == AuthorizationType::atKeyPassword);

        for (const std::string& opt_key : std::vector<std::string>{ "login", "password" })
            m_optgroup->show_field(opt_key, auth_type == AuthorizationType::atUserPassword);
    }

    this->Layout();
}

void PhysicalPrinterDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    const int& em = em_unit();

    m_printhost_browse_btn->msw_rescale();
    m_printhost_test_btn->msw_rescale();
    if (m_printhost_cafile_browse_btn)
        m_printhost_cafile_browse_btn->msw_rescale();

    m_optgroup->msw_rescale();

    msw_buttons_rescale(this, em, { wxID_OK, wxID_CANCEL });

    const wxSize& size = wxSize(45 * em, 35 * em);
    SetMinSize(size);

    Fit();
    Refresh();
}

void PhysicalPrinterDialog::OnOK(wxEvent& event)
{
    wxString printer_name = m_printer_name->GetValue();
    if (printer_name.IsEmpty()) {
        show_error(this, _L("The supplied name is empty. It can't be saved."));
        return;
    }

    PhysicalPrinterCollection& printers = wxGetApp().preset_bundle->physical_printers;
    const PhysicalPrinter* existing = printers.find_printer(into_u8(printer_name));
    if (existing && into_u8(printer_name) != printers.get_selected_printer_name())
    {
        wxString msg_text = from_u8((boost::format(_u8L("Printer with name \"%1%\" already exists.")) % printer_name).str());
        msg_text += "\n" + _L("Replace?");
        wxMessageDialog dialog(nullptr, msg_text, _L("Warning"), wxICON_WARNING | wxYES | wxNO);

        if (dialog.ShowModal() == wxID_NO)
            return;

        // Remove the printer from the list.
        printers.delete_printer(into_u8(printer_name));
    }

    //upadte printer name, if it was changed
    m_printer.name = into_u8(printer_name);

    // save new physical printer
    printers.save_printer(m_printer);

    // update selection on the tab only when it was changed
    if (m_printer.get_preset_name() != wxGetApp().preset_bundle->printers.get_selected_preset_name()) {
        Tab* tab = wxGetApp().get_tab(Preset::TYPE_PRINTER);
        if (tab) {
            wxString preset_name = m_printer_presets->GetString(m_printer_presets->GetSelection());
            tab->select_preset(into_u8(preset_name));
        }
    }

    event.Skip();
}


}}    // namespace Slic3r::GUI
