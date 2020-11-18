#ifndef slic3r_SavePresetDialog_hpp_
#define slic3r_SavePresetDialog_hpp_

//#include <wx/gdicmn.h>

#include "libslic3r/Preset.hpp"
#include "wxExtensions.hpp"
#include "GUI_Utils.hpp"

class wxString;
class wxStaticText;
class wxComboBox;
class wxRadioBox;
class wxStaticBitmap;

namespace Slic3r {

namespace GUI {

class SavePresetDialog : public DPIDialog
{
    enum ActionType
    {
        ChangePreset,
        AddPreset,
        Switch, 
        UndefAction
    };

    struct Item
    {
        enum ValidationType
        {
            Valid,
            NoValid,
            Warning
        };

        Item(Preset::Type type, const std::string& suffix, wxBoxSizer* sizer, SavePresetDialog* parent);

        void            update_valid_bmp();
        void            accept();

        bool            is_valid()      const { return m_valid_type != NoValid; }
        Preset::Type    type()          const { return m_type; }
        std::string     preset_name()   const { return m_preset_name; }

    private:
        Preset::Type    m_type;
        ValidationType  m_valid_type;
        std::string		m_preset_name;

        SavePresetDialog*   m_parent        {nullptr};
        wxStaticBitmap*     m_valid_bmp     {nullptr};
        wxComboBox*         m_combo         {nullptr};
        wxStaticText*       m_valid_label   {nullptr};

        PresetCollection*   m_presets       {nullptr};

        void update();
    };

    std::vector<Item*>   m_items;

    wxBoxSizer*         m_presets_sizer     {nullptr};
    wxStaticText*       m_label             {nullptr};
    wxRadioBox*         m_action_radio_box  {nullptr};
    wxBoxSizer*         m_radio_sizer       {nullptr};  
    ActionType          m_action            {UndefAction};

    std::string         m_ph_printer_name;
    std::string         m_old_preset_name;

public:

    SavePresetDialog(wxWindow* parent, Preset::Type type, std::string suffix = "");
    SavePresetDialog(wxWindow* parent, std::vector<Preset::Type> types, std::string suffix = "");
    ~SavePresetDialog();

    void AddItem(Preset::Type type, const std::string& suffix);

    std::string get_name();
    std::string get_name(Preset::Type type);

    bool enable_ok_btn() const;
    void add_info_for_edit_ph_printer(wxBoxSizer *sizer);
    void update_info_for_edit_ph_printer(const std::string &preset_name);
    void layout();

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override {}

private:
    void build(std::vector<Preset::Type> types, std::string suffix = "");
    void update_physical_printers(const std::string& preset_name);
    void accept();
};

} // namespace GUI
} // namespace Slic3r

#endif
