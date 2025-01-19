#ifndef _BaseTransparentDPIFrame_H_
#define _BaseTransparentDPIFrame_H_

#include <future>
#include <thread>
#include "GUI_App.hpp"
#include "GUI_Utils.hpp"

class Button;
class CheckBox;
namespace Slic3r { namespace GUI {
class CapsuleButton;

class BaseTransparentDPIFrame : public Slic3r::GUI::DPIFrame
{
public:
    enum DisappearanceMode {
        None,
        TimedDisappearance,//defalut 7 second
        QuickDisappearance
    };

    BaseTransparentDPIFrame(wxWindow *        parent,
                            int               win_width,
                            wxPoint           dialog_pos,
                            int               ok_button_width,
                            wxString          win_text,
                            wxString          ok_text,
                            wxString          cancel_text         = "",
                            DisappearanceMode disappearance_mode = DisappearanceMode::None);
    ~BaseTransparentDPIFrame() override;
    void on_dpi_changed(const wxRect &suggested_rect) override;
    void on_show();
    void on_hide();
    void clear_timer_count();
    bool Show(bool show =true) override;

    virtual void  deal_ok();
    virtual void  deal_cancel();
    virtual void  on_timer(wxTimerEvent &event);

protected:
    Button *    m_button_ok     = nullptr;
    Button *    m_button_cancel = nullptr;

    DisappearanceMode m_timed_disappearance_mode;
    float m_timer_count = 0;
    wxTimer *m_refresh_timer{nullptr};
    int  m_disappearance_second = 2500;//unit ms: mean 5s

private:
    wxBoxSizer *m_sizer_main{nullptr};

private:
    void  init_timer();
};
}}     // namespace Slic3r::GUI
#endif  // _STEP_MESH_DIALOG_H_
