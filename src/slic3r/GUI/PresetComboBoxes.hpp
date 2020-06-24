#ifndef slic3r_PresetComboBoxes_hpp_
#define slic3r_PresetComboBoxes_hpp_

#include <vector>

#include <wx/panel.h>
#include <wx/bmpcbox.h>
#include <wx/gdicmn.h>

#include "libslic3r/Preset.hpp"
#include "wxExtensions.hpp"
#include "GUI_Utils.hpp"

class wxString;
class wxTextCtrl;
class ScalableButton;

namespace Slic3r {

namespace GUI {

class BitmapCache;


// ---------------------------------
// ***  PresetComboBox  ***
// ---------------------------------

// BitmapComboBox used to presets list on Sidebar and Tabs
class PresetComboBox : public wxBitmapComboBox
{
public:
    PresetComboBox(wxWindow* parent, Preset::Type preset_type, const wxSize& size = wxDefaultSize);
    ~PresetComboBox();

	enum LabelItemType {
		LABEL_ITEM_PHYSICAL_PRINTER = 0xffffff01,
		LABEL_ITEM_MARKER,
		LABEL_ITEM_PHYSICAL_PRINTERS,
		LABEL_ITEM_WIZARD_PRINTERS,
        LABEL_ITEM_WIZARD_FILAMENTS,
        LABEL_ITEM_WIZARD_MATERIALS,

        LABEL_ITEM_MAX,
	};

    void set_label_marker(int item, LabelItemType label_item_type = LABEL_ITEM_MARKER);

    virtual void update() {};
    virtual void msw_rescale();

protected:
    typedef std::size_t Marker;

    Preset::Type        m_type;
    std::string         m_main_bitmap_name;

    PresetBundle*       m_preset_bundle {nullptr};
    PresetCollection*   m_collection {nullptr};

    // Caching color bitmaps for the filament combo box.
    BitmapCache*        m_bitmap_cache {nullptr};
    // Indicator, that the preset is compatible with the selected printer.
    ScalableBitmap      m_bitmapCompatible;
    // Indicator, that the preset is NOT compatible with the selected printer.
    ScalableBitmap      m_bitmapIncompatible;
    // Indicator, that the preset is system and not modified.
    ScalableBitmap      m_bitmapLock;

    int m_last_selected;
    int m_em_unit;

    // parameters for an icon's drawing
    int icon_height;
    int norm_icon_width;
    int thin_icon_width;
    int wide_icon_width;
    int space_icon_width;
    int thin_space_icon_width;
    int wide_space_icon_width;

#ifdef __linux__
    static const char* separator_head() { return "------- "; }
    static const char* separator_tail() { return " -------"; }
#else // __linux__ 
    static const char* separator_head() { return "————— "; }
    static const char* separator_tail() { return " —————"; }
#endif // __linux__
    static wxString    separator(const std::string& label);

#ifdef __APPLE__
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
    virtual bool OnAddBitmap(const wxBitmap& bitmap) override;
    virtual void OnDrawItem(wxDC& dc, const wxRect& rect, int item, int flags) const override;
#endif

private:
    void fill_width_height();
};


// ---------------------------------
// ***  PlaterPresetComboBox  ***
// ---------------------------------

class PlaterPresetComboBox : public PresetComboBox
{
public:
    PlaterPresetComboBox(wxWindow *parent, Preset::Type preset_type);
    ~PlaterPresetComboBox();

    ScalableButton* edit_btn { nullptr };

    void set_extruder_idx(const int extr_idx)   { m_extruder_idx = extr_idx; }
    int  get_extruder_idx() const               { return m_extruder_idx; }

    bool is_selected_physical_printer();
    bool switch_to_tab();
    void show_edit_menu();

    void update() override;
    void msw_rescale() override;

private:
    int     m_extruder_idx = -1;
};


// ---------------------------------
// ***  TabPresetComboBox  ***
// ---------------------------------

class TabPresetComboBox : public PresetComboBox
{
    bool show_incompatible {false};
    std::function<void(int)> on_selection_changed { nullptr };

public:
    TabPresetComboBox(wxWindow *parent, Preset::Type preset_type);
    ~TabPresetComboBox() {}
    void set_show_incompatible_presets(bool show_incompatible_presets) {
        show_incompatible = show_incompatible_presets;
    }

    void update() override;
    void update_dirty();
    void msw_rescale() override;

    void set_selection_changed_function(std::function<void(int)> sel_changed) { on_selection_changed = sel_changed; }
};


//------------------------------------------
//          PhysicalPrinterDialog
//------------------------------------------
class ConfigOptionsGroup;
class PhysicalPrinterDialog : public DPIDialog
{
    PhysicalPrinter     m_printer;
    DynamicPrintConfig* m_config            { nullptr };

    wxTextCtrl*         m_printer_name      { nullptr };
    TabPresetComboBox*  m_printer_presets   { nullptr };
    ConfigOptionsGroup* m_optgroup          { nullptr };

    ScalableButton*     m_printhost_browse_btn;
    ScalableButton*     m_printhost_test_btn;
    ScalableButton*     m_printhost_cafile_browse_btn {nullptr};

    void build_printhost_settings(ConfigOptionsGroup* optgroup);
    void update_octoprint_visible();
    void OnOK(wxEvent& event);

public:
    PhysicalPrinterDialog(wxString printer_name);
    ~PhysicalPrinterDialog() {}

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override {};
};

} // namespace GUI
} // namespace Slic3r

#endif
