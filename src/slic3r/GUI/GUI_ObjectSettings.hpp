#ifndef slic3r_GUI_ObjectSettings_hpp_
#define slic3r_GUI_ObjectSettings_hpp_

#include <memory>
#include <vector>
#include <wx/panel.h>
#include "wxExtensions.hpp"

class wxBoxSizer;

namespace Slic3r {
namespace GUI {
class ConfigOptionsGroup;

class OG_Settings
{
protected:
    std::shared_ptr<ConfigOptionsGroup> m_og;
    wxWindow* m_parent;
public:
    OG_Settings(wxWindow* parent, const bool staticbox);
    virtual ~OG_Settings() {}

    virtual bool        IsShown();
    virtual void        Show(const bool show);
    virtual void        Hide();
    virtual void        UpdateAndShow(const bool show);

    virtual wxSizer*    get_sizer();
    ConfigOptionsGroup* get_og() { return m_og.get(); }
};


class ObjectSettings : public OG_Settings
{
    // sizer for extra Object/Part's settings
    wxBoxSizer* m_settings_list_sizer{ nullptr };  
    // option groups for settings
    std::vector <std::shared_ptr<ConfigOptionsGroup>> m_og_settings;

    ScalableBitmap m_bmp_delete;
    ScalableBitmap m_bmp_delete_focus;

public:
    ObjectSettings(wxWindow* parent);
    ~ObjectSettings() {}

    bool        update_settings_list();
    void        UpdateAndShow(const bool show) override;
    void        msw_rescale();
};

}}

#endif // slic3r_GUI_ObjectSettings_hpp_
