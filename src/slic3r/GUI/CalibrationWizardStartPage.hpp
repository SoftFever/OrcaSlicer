#ifndef slic3r_GUI_CalibrationWizardStartPage_hpp_
#define slic3r_GUI_CalibrationWizardStartPage_hpp_

#include "CalibrationWizardPage.hpp"

namespace Slic3r { namespace GUI {



class CalibrationStartPage : public CalibrationWizardPage
{
public:
    CalibrationStartPage(wxWindow* parent,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

protected:
    CalibMode m_cali_mode;

    wxBoxSizer*   m_top_sizer;
    wxBoxSizer*   m_images_sizer;
    Label*        m_when_title;
    Label*        m_when_content;
    Label*        m_about_title;
    Label*        m_about_content;
    wxStaticBitmap* m_before_bmp{ nullptr };
    wxStaticBitmap* m_after_bmp{ nullptr };
    wxStaticBitmap* m_bmp_intro{ nullptr };

    void create_when(wxWindow* parent, wxString title, wxString content);
    void create_about(wxWindow* parent, wxString title, wxString content);
    void create_bitmap(wxWindow* parent, const wxBitmap& before_img, const wxBitmap& after_img);
    void create_bitmap(wxWindow* parent, std::string before_img, std::string after_img);
    void create_bitmap(wxWindow* parent, std::string img);
};

class CalibrationPAStartPage : public CalibrationStartPage
{
public:
    CalibrationPAStartPage(wxWindow* parent,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    void create_page(wxWindow* parent);

    void on_reset_page();
    void on_device_connected(MachineObject* obj);
};

class CalibrationFlowRateStartPage : public CalibrationStartPage
{
public:
    CalibrationFlowRateStartPage(wxWindow* parent,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    void create_page(wxWindow* parent);
    void on_reset_page();
    void on_device_connected(MachineObject* obj);
};

class CalibrationMaxVolumetricSpeedStartPage : public CalibrationStartPage
{
public:
    CalibrationMaxVolumetricSpeedStartPage(wxWindow* parent,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    void create_page(wxWindow* parent);
};

}} // namespace Slic3r::GUI

#endif