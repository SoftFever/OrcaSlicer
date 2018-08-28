#include "PngExportDialog.hpp"

namespace Slic3r {

PngExportDialog::PngExportDialog( wxWindow* parent, wxWindowID id,
                                  const wxString& title, const wxPoint& pos,
                                  const wxSize& size, long style ) :
    wxDialog( parent, id, title, pos, size, style )
{
    this->SetSizeHints( wxDefaultSize, wxDefaultSize );

    auto top_layout = new wxBoxSizer(wxHORIZONTAL);

    // /////////////////////////////////////////////////////////////////////////
    // Labels
    // /////////////////////////////////////////////////////////////////////////

    auto labels_layout = new wxGridSizer(6, 1, 0, 0);

    // Input File picker label
    auto filepick_text = new wxStaticText( this, wxID_ANY,
                                           _("Target zip file"),
                                           wxDefaultPosition,
                                           wxDefaultSize, 0 );
    filepick_text->Wrap( -1 );
    labels_layout->Add( filepick_text, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5 );

    // Config file label
    auto confpick_text = new wxStaticText( this, wxID_ANY,
                                           _("Config file (optional)"),
                                           wxDefaultPosition,
                                           wxDefaultSize, 0 );
    confpick_text->Wrap( -1 );
    labels_layout->Add( confpick_text, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5 );
    confpick_text->Disable();

    // Resolution layout
    auto resotext = new wxStaticText( this, wxID_ANY,
                                      _("Resolution (w, h) [px]"),
                                      wxDefaultPosition, wxDefaultSize, 0 );
    resotext->Wrap( -1 );
    labels_layout->Add( resotext, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5 );

    // Bed size label
    auto bed_size_text = new wxStaticText( this, wxID_ANY,
                                           _("Bed size (w, h) [mm]"),
                                           wxDefaultPosition,
                                           wxDefaultSize, 0 );
    bed_size_text->Wrap( -1 );
    labels_layout->Add( bed_size_text, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5 );

    // Correction label
    auto corr_text = new wxStaticText( this, wxID_ANY, _("Scale (x, y, z)"),
                                       wxDefaultPosition, wxDefaultSize, 0 );
    corr_text->Wrap( -1 );
    labels_layout->Add( corr_text, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5 );

    // Exp time label
    auto exp_text = new wxStaticText( this, wxID_ANY,
                                      _("Exposure time [s]"),
                                      wxDefaultPosition, wxDefaultSize, 0 );
    exp_text->Wrap( -1 );
    labels_layout->Add( exp_text, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5 );

    top_layout->Add( labels_layout, 0, wxEXPAND, 5 );

    // /////////////////////////////////////////////////////////////////////////


    // /////////////////////////////////////////////////////////////////////////
    // Body
    // /////////////////////////////////////////////////////////////////////////

    auto body_layout = new wxBoxSizer( wxVERTICAL );

    // Input file picker
    auto fpicklayout = new wxBoxSizer(wxHORIZONTAL);
    filepick_ctl_ = new wxFilePickerCtrl( this, wxID_ANY, wxEmptyString,
                                          _("Select a file"), wxT("*.zip"),
                                          wxDefaultPosition, wxDefaultSize,
                                          wxFLP_USE_TEXTCTRL | wxFLP_SAVE,
                                          wxDefaultValidator,
                                          wxT("filepick_ctl") );
    fpicklayout->Add( filepick_ctl_, 1, wxALL | wxALIGN_CENTER, 5);
    body_layout->Add( fpicklayout, 1, wxEXPAND, 5 );

    auto ctlpicklayout = new wxBoxSizer(wxHORIZONTAL);
    confpick_ctl_ = new wxFilePickerCtrl(
                this, wxID_ANY, wxEmptyString, _("Select a file"),
                wxT("*.json"), wxDefaultPosition, wxDefaultSize,
                wxFLP_USE_TEXTCTRL | wxFLP_DEFAULT_STYLE, wxDefaultValidator,
                wxT("filepick_ctl") );
    confpick_ctl_->Disable();
    ctlpicklayout->Add( confpick_ctl_, 1, wxALL | wxALIGN_CENTER, 5);
    body_layout->Add( ctlpicklayout, 1, wxEXPAND, 5 );


    // Resolution controls /////////////////////////////////////////////////////

    auto res_spins_layout = new wxBoxSizer( wxHORIZONTAL );
    spin_reso_width_ = new wxSpinCtrl( this, wxID_ANY, wxEmptyString,
                                       wxDefaultPosition, wxDefaultSize,
                                       wxSP_ARROW_KEYS, 0, 10000, 1440 );
    res_spins_layout->Add( spin_reso_width_, 1, wxALIGN_CENTER|wxALL, 5 );
    spin_reso_height_ = new wxSpinCtrl( this, wxID_ANY, wxEmptyString,
                                        wxDefaultPosition, wxDefaultSize,
                                        wxSP_ARROW_KEYS, 0, 10000, 2560 );
    res_spins_layout->Add( spin_reso_height_, 1, wxALIGN_CENTER|wxALL, 5 );

    reso_lock_btn_ = new wxToggleButton( this, wxID_ANY, _("Lock"),
                                         wxDefaultPosition, wxDefaultSize, 0 );
    reso_lock_btn_->SetValue(true);
    res_spins_layout->Add( reso_lock_btn_, 0, wxALIGN_CENTER|wxALL, 5 );

    body_layout->Add( res_spins_layout, 1, wxEXPAND, 5 );


    // Bed size controls ///////////////////////////////////////////////////////

    auto bed_spins_layout = new wxBoxSizer( wxHORIZONTAL );
    bed_width_spin_ = new wxSpinCtrlDouble( this, wxID_ANY, wxEmptyString,
                                            wxDefaultPosition, wxDefaultSize,
                                            wxSP_ARROW_KEYS, 0, 1e6, 68.0 );

    bed_spins_layout->Add( bed_width_spin_, 1, wxALIGN_CENTER|wxALL, 5 );

    bed_height_spin_ = new wxSpinCtrlDouble( this, wxID_ANY, wxEmptyString,
                                             wxDefaultPosition, wxDefaultSize,
                                             wxSP_ARROW_KEYS, 0, 1e6, 120.0 );
    bed_spins_layout->Add( bed_height_spin_, 1, wxALIGN_CENTER|wxALL, 5 );

    bedsize_lock_btn_ = new wxToggleButton( this, wxID_ANY, _("Lock"),
                                            wxDefaultPosition,
                                            wxDefaultSize, 0 );
    bedsize_lock_btn_->SetValue(true);
    bed_spins_layout->Add( bedsize_lock_btn_, 0, wxALIGN_CENTER|wxALL, 5 );

    body_layout->Add( bed_spins_layout, 1, wxEXPAND, 5 );


    // Scale correction controls ///////////////////////////////////////////////

    auto corr_layout = new wxBoxSizer( wxHORIZONTAL );
    corr_spin_x_ = new wxSpinCtrlDouble( this, wxID_ANY, wxEmptyString,
                                         wxDefaultPosition, wxDefaultSize,
                                         wxSP_ARROW_KEYS, 0, 100, 1, 0.01 );
    corr_spin_x_->SetDigits(3);
    corr_spin_x_->SetMaxSize(wxSize(100, -1));
    corr_layout->Add( corr_spin_x_, 0, wxALIGN_CENTER|wxALL, 5 );

    corr_spin_y_ = new wxSpinCtrlDouble( this, wxID_ANY, wxEmptyString,
                                         wxDefaultPosition, wxDefaultSize,
                                         wxSP_ARROW_KEYS, 0, 100, 1, 0.01 );
    corr_spin_y_->SetDigits(3);
    corr_spin_y_->SetMaxSize(wxSize(100, -1));
    corr_layout->Add( corr_spin_y_, 0, wxALIGN_CENTER|wxALL, 5 );

    corr_spin_z_ = new wxSpinCtrlDouble( this, wxID_ANY, wxEmptyString,
                                         wxDefaultPosition, wxDefaultSize,
                                         wxSP_ARROW_KEYS, 0, 100, 1, 0.01 );
    corr_spin_z_->SetDigits(3);
    corr_spin_z_->SetMaxSize(wxSize(100, -1));
    corr_layout->Add( corr_spin_z_, 0, wxALIGN_CENTER|wxALL, 5 );

    corr_layout->Add( bedsize_lock_btn_->GetSize().GetWidth(), 0, 1, wxEXPAND, 5 );

    body_layout->Add( corr_layout, 1, wxEXPAND, 5 );

    // Exposure time controls /////////////////////////////////////////////////

    auto exp_layout = new wxBoxSizer( wxHORIZONTAL );
    exptime_spin_ = new wxSpinCtrlDouble( this, wxID_ANY, wxEmptyString,
                                          wxDefaultPosition, wxDefaultSize,
                                          wxSP_ARROW_KEYS, 0, 100, 1, 0.01 );
    exptime_spin_->SetDigits(3);
    exptime_spin_->SetMaxSize(wxSize(100, -1));

    auto first_txt = new wxStaticText( this, wxID_ANY,
                                       _("First exp. time"),
                                       wxDefaultPosition,
                                       wxDefaultSize, wxALIGN_RIGHT );

    exptime_first_spin_ = new wxSpinCtrlDouble( this, wxID_ANY, wxEmptyString,
                                                wxDefaultPosition,
                                                wxDefaultSize, wxSP_ARROW_KEYS,
                                                0, 100, 1, 0.01 );
    exptime_first_spin_->SetDigits(3);
    exptime_first_spin_->SetMaxSize(wxSize(100, -1));

    exp_layout->Add( exptime_spin_, 1, wxALIGN_CENTER|wxALL, 5 );
    exp_layout->Add( first_txt, 1, wxALIGN_CENTER|wxALL, 5);
    exp_layout->Add( exptime_first_spin_, 1, wxALIGN_CENTER|wxALL, 5 );

    export_btn_ = new wxButton( this, wxID_ANY, _("Export"), wxDefaultPosition,
                                wxDefaultSize, 0, wxDefaultValidator,
                                wxT("export_btn") );

    exp_layout->Add( export_btn_, 0, wxALIGN_CENTER|wxALL, 5 );

    body_layout->Add( exp_layout, 1, wxEXPAND, 5 );

    top_layout->Add( body_layout, 0, wxEXPAND, 5 );

    // /////////////////////////////////////////////////////////////////////////
    // Finalize
    // /////////////////////////////////////////////////////////////////////////

    this->SetSizer(top_layout);
    this->Layout();

    this->Fit();
    this->SetMinSize(this->GetSize());
    this->Centre( wxBOTH );

    // Connect Events
    filepick_ctl_->Connect(
                wxEVT_COMMAND_FILEPICKER_CHANGED,
                wxFileDirPickerEventHandler( PngExportDialog::onFileChanged ),
                NULL, this );
    spin_reso_width_->Connect(
                wxEVT_COMMAND_TEXT_UPDATED,
                wxCommandEventHandler( PngExportDialog::EvalResoSpin ),
                NULL, this );
    spin_reso_height_->Connect(
                wxEVT_COMMAND_TEXT_UPDATED,
                wxCommandEventHandler( PngExportDialog::EvalResoSpin ),
                NULL, this );
    reso_lock_btn_->Connect(
                wxEVT_COMMAND_TOGGLEBUTTON_CLICKED,
                wxCommandEventHandler( PngExportDialog::ResoLock ),
                NULL, this );
    bed_width_spin_->Connect(
                wxEVT_COMMAND_TEXT_UPDATED,
                wxCommandEventHandler( PngExportDialog::EvalBedSpin ),
                NULL, this );
    bed_height_spin_->Connect(
                wxEVT_COMMAND_TEXT_UPDATED,
                wxCommandEventHandler( PngExportDialog::EvalBedSpin ),
                NULL, this );
    bedsize_lock_btn_->Connect(
                wxEVT_COMMAND_TOGGLEBUTTON_CLICKED,
                wxCommandEventHandler( PngExportDialog::BedsizeLock ),
                NULL, this );
    export_btn_->Connect(
                wxEVT_COMMAND_BUTTON_CLICKED,
                wxCommandEventHandler( PngExportDialog::Close ), NULL, this );
}

PngExportDialog::~PngExportDialog()
{
    // Disconnect Events
    filepick_ctl_->Disconnect(
                wxEVT_COMMAND_FILEPICKER_CHANGED,
                wxFileDirPickerEventHandler( PngExportDialog::onFileChanged ),
                NULL, this );
    spin_reso_width_->Disconnect(
                wxEVT_COMMAND_TEXT_UPDATED,
                wxCommandEventHandler( PngExportDialog::EvalResoSpin ),
                NULL, this );
    spin_reso_height_->Disconnect(
                wxEVT_COMMAND_TEXT_UPDATED,
                wxCommandEventHandler( PngExportDialog::EvalResoSpin ),
                NULL, this );
    reso_lock_btn_->Disconnect(
                wxEVT_COMMAND_TOGGLEBUTTON_CLICKED,
                wxCommandEventHandler( PngExportDialog::ResoLock ),
                NULL, this );
    bed_width_spin_->Disconnect(
                wxEVT_COMMAND_TEXT_UPDATED,
                wxCommandEventHandler( PngExportDialog::EvalBedSpin ),
                NULL, this );
    bed_height_spin_->Disconnect(
                wxEVT_COMMAND_TEXT_UPDATED,
                wxCommandEventHandler( PngExportDialog::EvalBedSpin ),
                NULL, this );
    bedsize_lock_btn_->Disconnect(
                wxEVT_COMMAND_TOGGLEBUTTON_CLICKED,
                wxCommandEventHandler( PngExportDialog::BedsizeLock ),
                NULL, this );
    export_btn_->Disconnect(
                wxEVT_COMMAND_BUTTON_CLICKED,
                wxCommandEventHandler( PngExportDialog::Close ), NULL, this );

}

}
