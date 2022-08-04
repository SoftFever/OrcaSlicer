#include "PrintOptionsDialog.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"

#define DLG_SIZE  (wxSize(FromDIP(360), FromDIP(160)))

static const wxColour STATIC_BOX_LINE_COL = wxColour(238, 238, 238);

namespace Slic3r { namespace GUI {

PrintOptionsDialog::PrintOptionsDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, _L("Print Options"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    this->SetDoubleBuffered(true);
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);
    
    this->SetMinSize(DLG_SIZE);
    this->SetSize(DLG_SIZE);

    auto m_options_sizer = create_settings_group(this);
    this->SetSizer(m_options_sizer);
    this->Layout();
    m_options_sizer->Fit(this);
    this->Fit();

    m_cb_first_layer->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& evt) {
        if (obj) {
            obj->command_xcam_control_first_layer_inspector(m_cb_first_layer->GetValue(), false);
        }
        evt.Skip();
    });

    m_cb_spaghetti->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& evt) {
        update_spaghetti();

        if (obj) {
            obj->command_xcam_control_spaghetti_detector(m_cb_spaghetti->GetValue(), m_cb_spaghetti_print_halt->GetValue());
        }
        evt.Skip();
    });

    m_cb_spaghetti_print_halt->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent&evt) {
        if (obj) {
            obj->command_xcam_control_spaghetti_detector(m_cb_spaghetti->GetValue(), m_cb_spaghetti_print_halt->GetValue());
        }
        evt.Skip();
    });
}

PrintOptionsDialog::~PrintOptionsDialog() {}

void PrintOptionsDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    this->SetMinSize(DLG_SIZE);
    this->SetSize(DLG_SIZE);
    Fit();
}

void PrintOptionsDialog::update_spaghetti()
{
    if (m_cb_spaghetti->GetValue()) {
        m_cb_spaghetti_print_halt->Enable();
        text_spaghetti_print_halt->Enable();
    }
    else {
        m_cb_spaghetti_print_halt->Disable();
        text_spaghetti_print_halt->Disable();
    }
}

void PrintOptionsDialog::update_options(MachineObject *obj_)
{
    if (!obj_) return;
    this->Freeze();
    m_cb_spaghetti->SetValue(obj_->xcam_spaghetti_detector);
    m_cb_spaghetti_print_halt->SetValue(obj_->xcam_spaghetti_print_halt);
    m_cb_first_layer->SetValue(obj_->xcam_first_layer_inspector);
    update_spaghetti();
    this->Thaw();
}

wxBoxSizer* PrintOptionsDialog::create_settings_group(wxWindow* parent)
{
    auto sizer = new wxBoxSizer(wxVERTICAL);
    auto line_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_cb_spaghetti = new CheckBox(parent);
    auto text_spaghetti = new wxStaticText(parent, wxID_ANY, _L("spaghetti Detection"));
    text_spaghetti->SetFont(Label::Body_14);
    line_sizer->Add(FromDIP(5), 0, 0, 0);
    line_sizer->Add(m_cb_spaghetti, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    line_sizer->Add(text_spaghetti, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    sizer->Add(line_sizer, 0, wxEXPAND | wxALL, FromDIP(5));

    line_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_cb_spaghetti_print_halt = new CheckBox(parent);
    text_spaghetti_print_halt = new wxStaticText(parent, wxID_ANY, _L("Stop printing when spaghetti detected"));
    text_spaghetti_print_halt->SetFont(Label::Body_12);
    line_sizer->Add(FromDIP(30), 0, 0, 0);
    line_sizer->Add(m_cb_spaghetti_print_halt, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    line_sizer->Add(text_spaghetti_print_halt, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    
    sizer->Add(line_sizer, 0, wxEXPAND | wxALL , 0);

    sizer->Add(0, FromDIP(10), 0, 0);
    StaticLine* line = new StaticLine(parent, false);
    line->SetLineColour(STATIC_BOX_LINE_COL);
    sizer->Add(line, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));

    line_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_cb_first_layer = new CheckBox(parent);
    auto text_first_layer = new wxStaticText(parent, wxID_ANY, _L("First Layer Inspection"));
    text_first_layer->SetFont(Label::Body_14);
    line_sizer->Add(FromDIP(5), 0, 0, 0);
    line_sizer->Add(m_cb_first_layer, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    line_sizer->Add(text_first_layer, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    sizer->Add(line_sizer, 1, wxEXPAND | wxALL, FromDIP(5));
    line_sizer->Add(FromDIP(5), 0, 0, 0);

    return sizer;
}

void PrintOptionsDialog::update_machine_obj(MachineObject *obj_)
{
    obj = obj_;
}

bool PrintOptionsDialog::Show(bool show)
{
    if (show) { CentreOnParent(); }
    return DPIDialog::Show(show);
}

}} // namespace Slic3r::GUI
