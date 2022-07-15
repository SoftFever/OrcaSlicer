#include "StateHandler.hpp"

wxDEFINE_EVENT(EVT_ENABLE_CHANGED, wxCommandEvent);

StateHandler::StateHandler(wxWindow * owner)
    : owner_(owner)
{
    if (owner->IsEnabled())
        states_ |= Enabled;
    if (owner->HasFocus())
        states_ |= Focused;
}

void StateHandler::attach(StateColor const &color)
{
    colors_.push_back(&color);
}
void StateHandler::attach(std::vector<StateColor const *> const & colors)
{
    colors_.insert(colors_.end(), colors.begin(), colors.end());
}

void StateHandler::update_binds()
{
    int bind_states = 0;
    for (auto c : colors_) {
        bind_states |= c->states();
    }
    bind_states = bind_states | (bind_states >> 16);
    int diff = bind_states ^ bind_states_;
    State       states[] = {Enabled, Checked, Focused, Hovered, Pressed};
    wxEventType events[] = {EVT_ENABLE_CHANGED, wxEVT_CHECKBOX, wxEVT_SET_FOCUS, wxEVT_ENTER_WINDOW, wxEVT_LEFT_DOWN};
    wxEventType events2[] = {{0}, {0}, wxEVT_KILL_FOCUS, wxEVT_LEAVE_WINDOW, wxEVT_LEFT_UP};
    for (int i = 0; i < 5; ++i) {
        int s = states[i];
        if (diff & s) {
            if (bind_states & s) {
                owner_->Bind(events[i], &StateHandler::changed, this);
                if (events2[i])
                    owner_->Bind(events2[i], &StateHandler::changed, this);
            } else {
                owner_->Unbind(events[i], &StateHandler::changed, this);
                if (events2[i])
                    owner_->Unbind(events2[i], &StateHandler::changed, this);
            }
        }
    }
    bind_states_ = bind_states;
    owner_->Refresh();
}

void StateHandler::changed(wxEvent & event)
{
    event.Skip();
    wxEventType events[] = {EVT_ENABLE_CHANGED, wxEVT_CHECKBOX, wxEVT_SET_FOCUS, wxEVT_ENTER_WINDOW, wxEVT_LEFT_DOWN};
    wxEventType events2[] = {{0}, {0}, wxEVT_KILL_FOCUS, wxEVT_LEAVE_WINDOW, wxEVT_LEFT_UP};
    int old = states2_ | states_;
    // some events are from another window (ex: text_ctrl of TextInput), save state in states2_ to avoid conflicts
    int & states = event.GetEventObject() == owner_ ? states_ : states2_;
    for (int i = 0; i < 5; ++i) {
        if (events2[i]) {
            if (event.GetEventType() == events[i]) {
                states |= 1 << i;
                break;
            } else if (event.GetEventType() == events2[i]) {
                states &= ~(1 << i);
                break;
            }
        }
        else {
            if (event.GetEventType() == events[i]) {
                states ^= (1 << i);
                break;
            }
        }
    }
    if (old != (states2_ | states_))
        owner_->Refresh();
}
