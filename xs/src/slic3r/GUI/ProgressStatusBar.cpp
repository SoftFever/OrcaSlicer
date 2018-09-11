#include "ProgressStatusBar.hpp"

#include <wx/timer.h>
#include <wx/gauge.h>
#include <wx/button.h>
#include <wx/statusbr.h>
#include <wx/frame.h>
#include "GUI.hpp"

#include <iostream>

namespace Slic3r {

ProgressStatusBar::ProgressStatusBar(wxWindow *parent, int id):
    self(new wxStatusBar(parent ? parent : GUI::get_main_frame(),
                         id == -1? wxID_ANY : id)),
    timer_(new wxTimer(self)),
    prog_ (new wxGauge(self,
                       wxGA_HORIZONTAL,
                       100,
                       wxDefaultPosition,
                       wxDefaultSize)),
    cancelbutton_(new wxButton(self,
                               -1,
                               "Cancel",
                               wxDefaultPosition,
                               wxDefaultSize))
{
    prog_->Hide();
    cancelbutton_->Hide();

    self->SetFieldsCount(3);
    int w[] = {-1, 150, 155};
    self->SetStatusWidths(3, w);

    self->Bind(wxEVT_TIMER, [this](const wxTimerEvent&) {
        if (prog_->IsShown()) timer_->Stop();
        if(is_busy()) prog_->Pulse();
    });

    self->Bind(wxEVT_SIZE, [this](wxSizeEvent& event){
        wxRect rect;
        self->GetFieldRect(1, rect);
        auto offset = 0;
        cancelbutton_->Move(rect.GetX() + offset, rect.GetY() + offset);
        cancelbutton_->SetSize(rect.GetWidth() - offset, rect.GetHeight());

        self->GetFieldRect(2, rect);
        prog_->Move(rect.GetX() + offset, rect.GetY() + offset);
        prog_->SetSize(rect.GetWidth() - offset, rect.GetHeight());

        event.Skip();
    });

    cancelbutton_->Bind(wxEVT_BUTTON, [this](const wxCommandEvent&) {
        if(cancel_cb_) cancel_cb_();
        cancelbutton_->Hide();
    });
}

ProgressStatusBar::~ProgressStatusBar() {
    if(timer_->IsRunning()) timer_->Stop();
}

int ProgressStatusBar::get_progress() const
{
    return prog_->GetValue();
}

void ProgressStatusBar::set_progress(int val)
{
    if(!prog_->IsShown()) show_progress(true);

    if(val == prog_->GetRange()) {
        prog_->SetValue(0);
        show_progress(false);
    } else {
        prog_->SetValue(val);
    }
}

int ProgressStatusBar::get_range() const
{
    return prog_->GetRange();
}

void ProgressStatusBar::set_range(int val)
{
    if(val != prog_->GetRange()) {
        prog_->SetRange(val);
    }
}

void ProgressStatusBar::show_progress(bool show)
{
    prog_->Show(show);
    prog_->Pulse();
}

void ProgressStatusBar::start_busy(int rate)
{
    busy_ = true;
    show_progress(true);
    if (!timer_->IsRunning()) {
        timer_->Start(rate);
    }
}

void ProgressStatusBar::stop_busy()
{
    timer_->Stop();
    show_progress(false);
    prog_->SetValue(0);
    busy_ = false;
}

void ProgressStatusBar::set_cancel_callback(ProgressStatusBar::CancelFn ccb) {
    cancel_cb_ = ccb;
    if(ccb) cancelbutton_->Show();
    else cancelbutton_->Hide();
}

void ProgressStatusBar::run(int rate)
{
    if(!timer_->IsRunning()) {
        timer_->Start(rate);
    }
}

void ProgressStatusBar::embed(wxFrame *frame)
{
    wxFrame* mf = frame? frame : GUI::get_main_frame();
    mf->SetStatusBar(self);
}

void ProgressStatusBar::set_status_text(const std::string& txt)
{
    self->SetStatusText(txt);
}

void ProgressStatusBar::show_cancel_button()
{
    cancelbutton_->Show();
}

void ProgressStatusBar::hide_cancel_button()
{
    cancelbutton_->Hide();
}

}
