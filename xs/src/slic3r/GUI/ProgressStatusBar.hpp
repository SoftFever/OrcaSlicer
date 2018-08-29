#ifndef PROGRESSSTATUSBAR_HPP
#define PROGRESSSTATUSBAR_HPP

#include <memory>
#include <functional>

class wxTimer;
class wxGauge;
class wxButton;
class wxTimerEvent;
class wxStatusBar;
class wxWindow;

namespace Slic3r {

/**
 * @brief The ProgressStatusBar class is the widgets occupying the lower area
 * of the Slicer main window. It consists of a message area to the left and a
 * progress indication area to the right with an optional cancel button.
 */
class ProgressStatusBar {
    wxStatusBar *self;      // we cheat! It should be the base class but: perl!
    wxTimer *timer_;
    wxGauge *prog_;
    wxButton *cancelbutton_;
public:

    /// Cancel callback function type
    using CancelFn = std::function<void()>;

    ProgressStatusBar(wxWindow *parent = nullptr, int id = -1);
    ~ProgressStatusBar();

    int  get_progress() const;
    void set_progress(int);
    void set_range(int = 100);
    void show_progress(bool);
    void start_busy(int = 100);
    void stop_busy();
    inline bool is_busy() const { return busy_; }
    void set_cancel_callback(CancelFn);
    void run(int rate);

    // Temporary methods to satisfy Perl side
    void Embed();
    void SetStatusText(std::string txt);
    int  GetId();
    int  GetProgId();

private:
    bool busy_ = false;
    CancelFn cancel_cb_;
};

namespace GUI {
    using Slic3r::ProgressStatusBar;
}

}

#endif // PROGRESSSTATUSBAR_HPP
