#include "GUI_Utils.hpp"

#include <algorithm>
#include <boost/lexical_cast.hpp>
#include <boost/format.hpp>

#include <wx/toplevel.h>
#include <wx/sizer.h>
#include <wx/panel.h>
#include <wx/checkbox.h>

#include "libslic3r/Config.hpp"


namespace Slic3r {
namespace GUI {


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
    , cbox(nullptr)
{
    if (checkbox_label.IsEmpty()) {
        return;
    }

    extra_control_creator = [this, checkbox_label](wxWindow *parent) -> wxWindow* {
        wxPanel* panel = new wxPanel(parent, -1);
        wxSizer* sizer = new wxBoxSizer(wxHORIZONTAL);
        this->cbox = new wxCheckBox(panel, wxID_HIGHEST + 1, checkbox_label);
        this->cbox->SetValue(true);
        sizer->AddSpacer(5);
        sizer->Add(this->cbox, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, 5);
        panel->SetSizer(sizer);
        sizer->SetSizeHints(panel);

        return panel;
    };

    SetExtraControlCreator(*extra_control_creator.target<ExtraControlCreatorFunction>());
}

bool CheckboxFileDialog::get_checkbox_value() const
{
    return this->cbox != nullptr ? cbox->IsChecked() : false;
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
    res.maximized = metrics[4];

    return res;
}

void WindowMetrics::sanitize_for_display(const wxRect &screen_rect)
{
    rect = rect.Intersect(screen_rect);
}

std::string WindowMetrics::serialize()
{
    return (boost::format("%1%; %2%; %3%; %4%; %5%")
        % rect.GetX()
        % rect.GetY()
        % rect.GetWidth()
        % rect.GetHeight()
        % static_cast<int>(maximized)
    ).str();
}



}
}
