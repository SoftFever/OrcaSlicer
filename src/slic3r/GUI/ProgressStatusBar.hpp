#ifndef PROGRESSSTATUSBAR_HPP
#define PROGRESSSTATUSBAR_HPP

#include <memory>
#include <string>
#include <functional>
#include <string>

class wxTimer;
class wxGauge;
class wxButton;
class wxTimerEvent;
class wxStatusBar;
class wxWindow;
class wxFrame;
class wxString;

namespace Slic3r {

/**
 * @brief The ProgressStatusBar class is the widgets occupying the lower area
 * of the Slicer main window. It consists of a message area to the left and a
 * progress indication area to the right with an optional cancel button.
 */
class ProgressStatusBar 
{
    wxStatusBar *self;      // we cheat! It should be the base class but: perl!
    wxGauge *m_prog;
    wxButton *m_cancelbutton;
    std::unique_ptr<wxTimer> m_timer;
public:

    /// Cancel callback function type
    using CancelFn = std::function<void()>;

    ProgressStatusBar(wxWindow *parent = nullptr, int id = -1);
    ~ProgressStatusBar();

    int         get_progress() const;
    // if the argument is less than 0 it shows the last state or
    // pulses if no state was set before.
    void        set_progress(int);
    int         get_range() const;
    void        set_range(int = 100);
    void        show_progress(bool);
    void        start_busy(int = 100);
    void        stop_busy();
    inline bool is_busy() const { return m_busy; }
    void        set_cancel_callback(CancelFn = CancelFn());
    inline void reset_cancel_callback() { set_cancel_callback(); }
    void        run(int rate);
    void        embed(wxFrame *frame = nullptr);
    void        set_status_text(const wxString& txt);
    void        set_status_text(const std::string& txt);
    void        set_status_text(const char *txt);
    wxString    get_status_text() const;

    // Temporary methods to satisfy Perl side
    void        show_cancel_button();
    void        hide_cancel_button();

private:
    bool m_busy = false;
    CancelFn m_cancel_cb;
};

namespace GUI {
    using Slic3r::ProgressStatusBar;
}

}

#endif // PROGRESSSTATUSBAR_HPP
