//**********************************************************/
/* File: uiAmsHumidityPopup.h
*  Description: The popup with Ams Humidity
*
*  \n class uiAmsHumidityPopup
//**********************************************************/

#pragma once
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Widgets/PopupWindow.hpp"

#include "slic3r/GUI/wxExtensions.hpp"

//Previous defintions
class wxGrid;

namespace Slic3r { namespace GUI {

struct uiAmsHumidityInfo
{
    std::string ams_id;
    int humidity_level = -1;
    int humidity_percent = -1;
    float current_temperature;
    int left_dry_time = -1;
};

/// </summary>
/// Note: The popup of Ams Humidity with percentage and dry time
/// Author: xin.zhang
/// </summary>
class uiAmsPercentHumidityDryPopup : public PopupWindow
{
public:
    uiAmsPercentHumidityDryPopup(wxWindow *parent);
    ~uiAmsPercentHumidityDryPopup() = default;

public:
    void Update(uiAmsHumidityInfo *info) { m_ams_id = info->ams_id; Update(info->humidity_level, info->humidity_percent, info->left_dry_time, info->current_temperature); };

    std::string get_owner_ams_id() const { return m_ams_id; }

    virtual void OnDismiss() wxOVERRIDE {};
    virtual bool ProcessLeftDown(wxMouseEvent &event) wxOVERRIDE { return true;};

    void msw_rescale();

private:
    void Update(int humidiy_level, int humidity_percent, int left_dry_time, float current_temperature);

    void paintEvent(wxPaintEvent &evt);
    void render(wxDC &dc);
    void doRender(wxDC &dc);

    void DrawGridArea(wxDC &dc, wxPoint start_p);

private:
    /*owner ams id*/
    std::string m_ams_id;

    int m_humidity_level   = 0;
    int m_humidity_percent = 0;
    int m_left_dry_time    = 0;
    float m_current_temperature = 0;

    // Bitmap
    ScalableBitmap close_img;
    ScalableBitmap drying_img;
    ScalableBitmap idle_img;

    // Widgets
    wxStaticBitmap* m_humidity_img;
    wxGrid*         m_grid_area;
};

}} // namespace Slic3r::GUI