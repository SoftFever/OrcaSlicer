#include "Field.hpp"

namespace Slic3r { namespace GUI {

void TextCtrl::BUILD() {
    wxString default_value = "";
    // Empty wxString object fails cast, default to "" if 
    // the recast fails from boost::any.
    try {
        if (opt.default_value != nullptr) {
            default_value = boost::any_cast<wxString>(*(opt.default_value));
        }
    } catch (const boost::bad_any_cast& e) {
        //TODO log error
    }
    auto size = wxSize(opt.height,opt.width);
    if (opt.height == 0 || opt.width == 0) { size = wxDefaultSize; }

    wxTextCtrl* temp = new wxTextCtrl(_parent, wxID_ANY, default_value, wxDefaultPosition, size, (opt.multiline ? wxTE_MULTILINE : 0));

    _on_change = [=](wxCommandEvent& a) { this->__on_change(a);};

    // This replaces the generic EVT_TEXT call to set the table up, it works with function objects.
    temp->Bind(wxEVT_TEXT, _on_change, temp->GetId());
    if (opt.tooltip.length() > 0) { temp->SetToolTip(opt.tooltip); }

    // recast as a wxWindow to fit the calling convention
    _window = dynamic_cast<wxWindow*>(temp);
}

// Fixed (temporary) function. We can (and probably should) use lambdas instead.
void TextCtrl::__on_change(wxCommandEvent& a) {
    printf("Calling _on_change for %d.\n", opt.label);
}

} }
