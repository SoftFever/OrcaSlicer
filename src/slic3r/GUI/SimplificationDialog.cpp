#include "SimplificationDialog.hpp"

#include <cstddef>
#include <vector>
#include <string>
#include <boost/algorithm/string.hpp>

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/statbox.h>
#include <wx/wupdlock.h>

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "format.hpp"
#include "wxExtensions.hpp"
#include "I18N.hpp"
#include "libslic3r/Utils.hpp"


namespace Slic3r {
namespace GUI {

#define BORDER_W 10

SimplificationDialog::SimplificationDialog(wxWindow* parent) :
    DPIDialog(parent, wxID_ANY, _L("Name of Dialog"), wxDefaultPosition, wxSize(45 * wxGetApp().em_unit(), -1), wxDEFAULT_DIALOG_STYLE/* | wxRESIZE_BORDER*/)
{
    SetFont(wxGetApp().normal_font());

    wxStaticText* label_top = new wxStaticText(this, wxID_ANY, _L("Some text") + ":");

    wxFlexGridSizer* grid_sizer = new wxFlexGridSizer(3, 2, 1, 2);
    grid_sizer->AddGrowableCol(1, 1);
    grid_sizer->SetFlexibleDirection(wxBOTH);

    for (int i = 0; i < 3; i++) {
        auto* text = new wxStaticText(this, wxID_ANY, _L("Text") + " " + std::to_string(i) + " :");

#ifdef _WIN32
        long style = wxBORDER_SIMPLE;
#else
        long style = 0
#endif
        auto value = new wxTextCtrl(this, wxID_ANY, "Some Value", wxDefaultPosition, wxDefaultSize, style);

        grid_sizer->Add(text, 0, wxALIGN_LEFT | wxALIGN_CENTER_VERTICAL | wxRIGHT, 4);
        grid_sizer->Add(value, 1, wxEXPAND | wxBOTTOM, 1);
    }

    wxStdDialogButtonSizer* btns = this->CreateStdDialogButtonSizer(wxOK | wxCANCEL);
    this->Bind(wxEVT_BUTTON, &SimplificationDialog::OnOK, this, wxID_OK);

    wxBoxSizer* topSizer = new wxBoxSizer(wxVERTICAL);

    topSizer->Add(label_top , 0, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, BORDER_W);
    topSizer->Add(grid_sizer, 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, BORDER_W);
    topSizer->Add(btns      , 0, wxEXPAND | wxALL, BORDER_W); 

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);

    wxGetApp().UpdateDlgDarkUI(this);

    this->CenterOnScreen();
}

SimplificationDialog::~SimplificationDialog()
{
}

void SimplificationDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    const int& em = em_unit();
    msw_buttons_rescale(this, em, { wxID_OK, wxID_CANCEL });

    const wxSize& size = wxSize(45 * em, 35 * em);
    SetMinSize(size);

    Fit();
    Refresh();
}

void SimplificationDialog::OnOK(wxEvent& event)
{
    event.Skip();
}

}}    // namespace Slic3r::GUI
