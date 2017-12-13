#include "GUI.hpp"//"slic3r_gui.hpp"
#include "Field.hpp"

namespace Slic3r { namespace GUI {

    void Field::_on_kill_focus(wxFocusEvent& event) {
        // Without this, there will be nasty focus bugs on Windows.
        // Also, docs for wxEvent::Skip() say "In general, it is recommended to skip all 
        // non-command events to allow the default handling to take place."
        event.Skip(1);

        // call the registered function if it is available
//!        if (on_kill_focus) 
//!            on_kill_focus(opt_id);
    }
    void Field::_on_change(wxCommandEvent& event) {
        std::cerr << "calling Field::_on_change \n";
//!        if (on_change != nullptr  && !disable_change_event)
//!            on_change(opt_id, "A");
    }
    void TextCtrl::BUILD() {
        auto size = wxSize(wxDefaultSize);
        if (opt.height >= 0) size.SetHeight(opt.height);
        if (opt.width >= 0) size.SetWidth(opt.width);

		auto temp = new wxTextCtrl(parent, wxID_ANY, wxString(""), wxDefaultPosition, size, (opt.multiline ? wxTE_MULTILINE : 0)); //! new wxTextCtrl(parent, wxID_ANY, wxString(opt.default_value->getString()), wxDefaultPosition, size, (opt.multiline ? wxTE_MULTILINE : 0));

        if (opt.tooltip.length() > 0) { temp->SetToolTip(opt.tooltip); }
        
        temp->Bind(wxEVT_TEXT, ([=](wxCommandEvent e) { _on_change(e); }), temp->GetId());
        temp->Bind(wxEVT_KILL_FOCUS, ([this](wxFocusEvent e) { _on_kill_focus(e); }), temp->GetId());

        // recast as a wxWindow to fit the calling convention
        window = dynamic_cast<wxWindow*>(temp);

    }

    void TextCtrl::enable() { (dynamic_cast<wxTextCtrl*>(window))->Enable(); (dynamic_cast<wxTextCtrl*>(window))->SetEditable(1); }
    void TextCtrl::disable() { dynamic_cast<wxTextCtrl*>(window)->Disable(); dynamic_cast<wxTextCtrl*>(window)->SetEditable(0); }
    void TextCtrl::set_tooltip(const wxString& tip) { }
}}

