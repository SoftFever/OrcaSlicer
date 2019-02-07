#include "GUI_Utils.hpp"

#include <algorithm>
#include <boost/lexical_cast.hpp>
#include <boost/format.hpp>

#include <wx/toplevel.h>
#include <wx/sizer.h>
#include <wx/checkbox.h>

#include "libslic3r/Config.hpp"


namespace Slic3r {
namespace GUI {


wxTopLevelWindow* find_toplevel_parent(wxWindow *window)
{
    for (; window != nullptr; window = window->GetParent()) {
        if (window->IsTopLevel()) {
            return dynamic_cast<wxTopLevelWindow*>(window);
        }
    }

    return nullptr;
}

void on_window_geometry(wxTopLevelWindow *tlw, std::function<void()> callback)
{
    tlw->Bind(wxEVT_CREATE, [=](wxWindowCreateEvent &event) {
#ifdef __linux__
        // On Linux, the geometry is only available after wxEVT_CREATE + CallAfter
        // cf. https://groups.google.com/forum/?pli=1#!topic/wx-users/fERSXdpVwAI
        tlw->CallAfter([=]() {
#endif
            callback();
#ifdef __linux__
        });
#endif
        event.Skip();
    });
}


CheckboxFileDialog::ExtraPanel::ExtraPanel(wxWindow *parent)
    : wxPanel(parent, wxID_ANY)
{
    // WARN: wxMSW does some extra shenanigans to calc the extra control size.
    // It first calls the create function with a dummy empty wxDialog parent and saves its size.
    // Afterwards, the create function is called again with the real parent.
    // Additionally there's no way to pass any extra data to the create function (no closure),
    // which is why we have to this stuff here. Grrr!
    auto *dlg = dynamic_cast<CheckboxFileDialog*>(parent);
    const wxString checkbox_label(dlg != nullptr ? dlg->checkbox_label : wxString("String long enough to contain dlg->checkbox_label"));

    auto* sizer = new wxBoxSizer(wxHORIZONTAL);
    cbox = new wxCheckBox(this, wxID_ANY, checkbox_label);
    cbox->SetValue(true);
    sizer->AddSpacer(5);
    sizer->Add(this->cbox, 0, wxEXPAND | wxALL, 5);
    SetSizer(sizer);
    sizer->SetSizeHints(this);
}

wxWindow* CheckboxFileDialog::ExtraPanel::ctor(wxWindow *parent) {
    return new ExtraPanel(parent);
}

CheckboxFileDialog::CheckboxFileDialog(wxWindow *parent,
    const wxString &checkbox_label,
    bool checkbox_value,
    const wxString &message,
    const wxString &default_dir,
    const wxString &default_file,
    const wxString &wildcard,
    long style,
    const wxPoint &pos,
    const wxSize &size,
    const wxString &name
)
    : wxFileDialog(parent, message, default_dir, default_file, wildcard, style, pos, size, name)
    , checkbox_label(checkbox_label)
{
    if (checkbox_label.IsEmpty()) {
        return;
    }

    SetExtraControlCreator(ExtraPanel::ctor);
}

bool CheckboxFileDialog::get_checkbox_value() const
{
    auto *extra_panel = dynamic_cast<ExtraPanel*>(GetExtraControl());
    return extra_panel != nullptr ? extra_panel->cbox->GetValue() : false;
}


WindowMetrics WindowMetrics::from_window(wxTopLevelWindow *window)
{
    WindowMetrics res;
    res.rect = window->GetScreenRect();
    res.maximized = window->IsMaximized();
    return res;
}

boost::optional<WindowMetrics> WindowMetrics::deserialize(const std::string &str)
{
    std::vector<std::string> metrics_str;
    metrics_str.reserve(5);

    if (!unescape_strings_cstyle(str, metrics_str) || metrics_str.size() != 5) {
        return boost::none;
    }

    int metrics[5];
    try {
        for (size_t i = 0; i < 5; i++) {
            metrics[i] = boost::lexical_cast<int>(metrics_str[i]);
        }
    } catch(const boost::bad_lexical_cast &) {
        return boost::none;
    }

    if ((metrics[4] & ~1) != 0) {    // Checks if the maximized flag is 1 or 0
        metrics[4] = 0;
    }

    WindowMetrics res;
    res.rect = wxRect(metrics[0], metrics[1], metrics[2], metrics[3]);
    res.maximized = metrics[4] != 0;

    return res;
}

void WindowMetrics::sanitize_for_display(const wxRect &screen_rect)
{
    rect = rect.Intersect(screen_rect);

    // Prevent the window from going too far towards the right and/or bottom edge
    // It's hardcoded here that the threshold is 80% of the screen size
    rect.x = std::min(rect.x, screen_rect.x + 4*screen_rect.width/5);
    rect.y = std::min(rect.y, screen_rect.y + 4*screen_rect.height/5);
}

std::string WindowMetrics::serialize() const
{
    return (boost::format("%1%; %2%; %3%; %4%; %5%")
        % rect.x
        % rect.y
        % rect.width
        % rect.height
        % static_cast<int>(maximized)
    ).str();
}

std::ostream& operator<<(std::ostream &os, const WindowMetrics& metrics)
{
    return os << '(' << metrics.serialize() << ')';
}


}
}
