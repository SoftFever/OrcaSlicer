#include "CloneDialog.hpp"

#include "GUI_App.hpp"
#include "MainFrame.hpp"

/*
create new bed if not fits
checkbox shortcuts
*/

namespace Slic3r { namespace GUI {

CloneDialog::CloneDialog(wxWindow *parent)
    : DPIDialog(parent ? parent : static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, _L("Clone"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    SetBackgroundColour(*wxWHITE);
    SetFont(Label::Body_14);

    m_plater = wxGetApp().plater();
    m_parent = parent;
    m_cancel_process = false;
    //m_config = wxGetApp().app_config;

    auto v_sizer = new wxBoxSizer(wxVERTICAL);
    auto f_sizer = new wxFlexGridSizer(2, 2, FromDIP(4) , FromDIP(20));

    auto count_label = new wxStaticText(this, wxID_ANY, _L("Number of copies:"), wxDefaultPosition, wxDefaultSize, 0);
    m_count_spin = new SpinInput(this, wxEmptyString, "", wxDefaultPosition, wxSize(FromDIP(120), -1), wxSP_ARROW_KEYS, 1, 1000, 1);
    m_count_spin->GetTextCtrl()->SetFocus();
    f_sizer->Add(count_label  , 0, wxEXPAND | wxALIGN_CENTER_VERTICAL);
    f_sizer->Add(m_count_spin, 0, wxALIGN_CENTER_VERTICAL);

    auto arrange_label = new wxStaticText(this, wxID_ANY, _L("Auto arrange plate after cloning") + ":", wxDefaultPosition, wxDefaultSize, 0);
    m_arrange_cb = new ::CheckBox(this);
    m_arrange_cb->SetValue(false);
    f_sizer->Add(arrange_label, 0, wxEXPAND | wxALIGN_CENTER_VERTICAL);
    f_sizer->Add(m_arrange_cb , 0, wxALIGN_CENTER_VERTICAL | wxTOP | wxBOTTOM, FromDIP(5));

    v_sizer->Add(f_sizer, 1, wxEXPAND | wxALL, FromDIP(10));

    m_progress = new ProgressBar(this, wxID_ANY, 100);
    m_progress->SetHeight(FromDIP(8));
    m_progress->SetProgressForedColour(StateColor::darkModeColorFor(wxColour("#DFDFDF")));
    m_progress->SetDoubleBuffered(true);
    m_progress->Hide();
    v_sizer->Add(m_progress, 0, wxEXPAND | wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(10));

    // used next button to get automatic left alignment
    auto dlg_btns = new DialogButtons(this, {"Next", "OK", "Cancel"});

    dlg_btns->GetFORWARD()->SetLabel(_L("Fill"));
    dlg_btns->GetFORWARD()->Bind(wxEVT_BUTTON, [this](wxCommandEvent &e) {
        m_plater->fill_bed_with_instances();
        EndModal(wxID_OK);
    });
    dlg_btns->GetFORWARD()->SetToolTip(_L("Fill bed with copies"));

    dlg_btns->GetOK()->Bind(wxEVT_BUTTON, [this, dlg_btns, v_sizer](wxCommandEvent &e) {

        dlg_btns->GetOK()->SetFocus(); // ensures input box value applied with wxEVT_KILL_FOCUS
                                       // used OK for focus if user spammed enter button

        m_count = m_count_spin->GetValue();
        m_count_spin->Disable();
        m_arrange_cb->Disable();

        m_progress->Show();

        dlg_btns->GetOK()->Hide();
        dlg_btns->GetFORWARD()->Hide();

        this->Layout();
        v_sizer->Fit(this);
        Refresh();

        Selection& sel = m_plater->canvas3D()->get_selection();
        m_plater->take_snapshot(std::string("Selection-clone"));
        sel.copy_to_clipboard();

        for (int i = 0; i < m_count; i++) { // same method with Selection::clone()
            m_progress->SetValue(static_cast<int>(static_cast<double>(i) / m_count * 100)); // pass 0 / 100
            sel.paste_from_clipboard();     // Should be a better method instead copy paste object

            if(m_cancel_process){
                m_plater->undo();
                return;
            }
            wxYield();                      // Allow event loop to process updates
        }
        
        if (m_arrange_cb->GetValue()){ //if (m_config->get("auto_arrange") == "true") {
            m_plater->set_prepare_state(Job::PREPARE_STATE_MENU);
            m_plater->arrange();
        }

        EndModal(wxID_OK);
    });

    dlg_btns->GetCANCEL()->Bind(wxEVT_BUTTON, [this](wxCommandEvent &e) {
        m_cancel_process = true;
        EndModal(wxID_CANCEL);
    });

    v_sizer->Add(dlg_btns, 0, wxEXPAND);

    this->SetSizer(v_sizer);
    this->Layout();
    v_sizer->Fit(this);
    wxGetApp().UpdateDlgDarkUI(this);
}

CloneDialog::~CloneDialog() {}

}} // namespace Slic3r::GUI