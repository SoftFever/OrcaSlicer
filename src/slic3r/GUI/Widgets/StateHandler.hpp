#ifndef slic3r_GUI_StateHandler_hpp_
#define slic3r_GUI_StateHandler_hpp_

#include <wx/event.h>

#include "StateColor.hpp"

wxDECLARE_EVENT(EVT_ENABLE_CHANGED, wxCommandEvent);

class StateHandler : public wxEvtHandler
{
public:
    enum State {
        Enabled = 1,
        Checked = 2,
        Focused = 4,
        Hovered = 8,
        Pressed = 16,
        Disabled = 1 << 16,
        NotChecked = 2 << 16,
        NotFocused = 4 << 16,
        NotHovered = 8 << 16,
        NotPressed = 16 << 16,
    };

public:
    StateHandler(wxWindow * owner);

public:
    void attach(StateColor const & color);

    void attach(std::vector<StateColor const *> const & colors);

    void update_binds();

    int states() const { return states_ | states2_; }

private:
    void changed(wxEvent & event);

private:
    wxWindow * owner_;
    std::vector<StateColor const *> colors_;
    int bind_states_ = 0;
    int states_ = 0;
    int states2_ = 0;
};

#endif // !slic3r_GUI_StateHandler_hpp_
