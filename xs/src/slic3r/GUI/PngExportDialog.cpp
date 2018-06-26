///////////////////////////////////////////////////////////////////////////
// C++ code generated with wxFormBuilder (version Jun 17 2015)
// http://www.wxformbuilder.org/
//
// PLEASE DO "NOT" EDIT THIS FILE!
///////////////////////////////////////////////////////////////////////////

#include "PngExportDialog.hpp"

///////////////////////////////////////////////////////////////////////////

PngExportDialog::PngExportDialog( wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style ) : wxDialog( parent, id, title, pos, size, style )
{
    this->SetSizeHints( wxDefaultSize, wxDefaultSize );

    wxBoxSizer* top_layout_;
    top_layout_ = new wxBoxSizer( wxVERTICAL );

    wxBoxSizer* bSizer15;
    bSizer15 = new wxBoxSizer( wxHORIZONTAL );

    wxBoxSizer* bSizer16;
    bSizer16 = new wxBoxSizer( wxVERTICAL );

    wxGridSizer* gSizer2;
    gSizer2 = new wxGridSizer( 4, 1, 0, 0 );

    filepick_text_ = new wxStaticText( this, wxID_ANY, _("Target zip file"), wxDefaultPosition, wxDefaultSize, 0 );
    filepick_text_->Wrap( -1 );
    gSizer2->Add( filepick_text_, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5 );

    resotext_ = new wxStaticText( this, wxID_ANY, _("Resolution (w, h) [px]"), wxDefaultPosition, wxDefaultSize, 0 );
    resotext_->Wrap( -1 );
    gSizer2->Add( resotext_, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5 );

    bed_size_text_ = new wxStaticText( this, wxID_ANY, _("Bed size (w, h) [mm]"), wxDefaultPosition, wxDefaultSize, 0 );
    bed_size_text_->Wrap( -1 );
    gSizer2->Add( bed_size_text_, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5 );

    corr_text_ = new wxStaticText( this, wxID_ANY, _("Size correction"), wxDefaultPosition, wxDefaultSize, 0 );
    corr_text_->Wrap( -1 );
    gSizer2->Add( corr_text_, 0, wxALIGN_CENTER_VERTICAL|wxALL, 5 );


    bSizer16->Add( gSizer2, 1, wxEXPAND, 5 );


    bSizer15->Add( bSizer16, 1, wxEXPAND, 5 );

    wxBoxSizer* bSizer18;
    bSizer18 = new wxBoxSizer( wxVERTICAL );

    wxBoxSizer* filepick_layout_;
    filepick_layout_ = new wxBoxSizer( wxHORIZONTAL );

    filepick_ctl_ = new wxFilePickerCtrl( this, wxID_ANY, wxEmptyString, _("Select a file"), wxT("*.zip"), wxDefaultPosition, wxSize( 308,-1 ), wxFLP_USE_TEXTCTRL | wxFLP_SAVE | wxFLP_OVERWRITE_PROMPT, wxDefaultValidator, wxT("filepick_ctl") );
    filepick_layout_->Add( filepick_ctl_, 0, wxALIGN_CENTER|wxALIGN_LEFT|wxALL, 5 );


    bSizer18->Add( filepick_layout_, 1, wxEXPAND, 5 );

    wxBoxSizer* resolution_layout_;
    resolution_layout_ = new wxBoxSizer( wxHORIZONTAL );

    wxBoxSizer* resolution_spins_layout_;
    resolution_spins_layout_ = new wxBoxSizer( wxHORIZONTAL );

    spin_reso_width_ = new wxSpinCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize( 100,-1 ), wxSP_ARROW_KEYS, 0, 10000, 1440 );
    resolution_spins_layout_->Add( spin_reso_width_, 0, wxALIGN_CENTER|wxALL, 5 );

    spin_reso_height_ = new wxSpinCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize( 100,-1 ), wxSP_ARROW_KEYS, 0, 10000, 2560 );
    resolution_spins_layout_->Add( spin_reso_height_, 0, wxALIGN_CENTER|wxALL, 5 );

    reso_lock_btn_ = new wxToggleButton( this, wxID_ANY, _("Lock"), wxDefaultPosition, wxDefaultSize, 0 );
    reso_lock_btn_->SetValue(true);
    resolution_spins_layout_->Add( reso_lock_btn_, 0, wxALL, 5 );


    resolution_layout_->Add( resolution_spins_layout_, 1, wxEXPAND, 5 );


    bSizer18->Add( resolution_layout_, 1, wxEXPAND, 5 );

    wxBoxSizer* bedsize_layout_;
    bedsize_layout_ = new wxBoxSizer( wxHORIZONTAL );

    wxBoxSizer* bedsize_spins_layout_;
    bedsize_spins_layout_ = new wxBoxSizer( wxHORIZONTAL );

    bed_width_spin_ = new wxSpinCtrlDouble( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize( 100,-1 ), wxSP_ARROW_KEYS, 0, 1e6, 68.0 );
    bedsize_spins_layout_->Add( bed_width_spin_, 0, wxALIGN_CENTER|wxALL, 5 );

    bed_height_spin_ = new wxSpinCtrlDouble( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize( 100,-1 ), wxSP_ARROW_KEYS, 0, 1e6, 120.0 );
    bedsize_spins_layout_->Add( bed_height_spin_, 0, wxALIGN_CENTER|wxALL, 5 );

    bedsize_lock_btn_ = new wxToggleButton( this, wxID_ANY, _("Lock"), wxDefaultPosition, wxDefaultSize, 0 );
    bedsize_lock_btn_->SetValue(true);
    bedsize_spins_layout_->Add( bedsize_lock_btn_, 0, wxALL, 5 );


    bedsize_layout_->Add( bedsize_spins_layout_, 1, wxEXPAND, 5 );


    bSizer18->Add( bedsize_layout_, 1, wxEXPAND, 5 );

    wxBoxSizer* corr_layout_;
    corr_layout_ = new wxBoxSizer( wxHORIZONTAL );

    corr_spin_ = new wxSpinCtrlDouble( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize( 100,-1 ), wxSP_ARROW_KEYS, 0, 100, 1, 0.1 );
    corr_layout_->Add( corr_spin_, 0, wxALIGN_CENTER|wxALL, 5 );


    corr_layout_->Add( 0, 0, 1, wxEXPAND, 5 );

    export_btn_ = new wxButton( this, wxID_ANY, _("Export"), wxDefaultPosition, wxDefaultSize, 0, wxDefaultValidator, wxT("export_btn") );
    corr_layout_->Add( export_btn_, 0, wxALL, 5 );


    bSizer18->Add( corr_layout_, 1, wxEXPAND, 5 );


    bSizer15->Add( bSizer18, 1, wxEXPAND, 5 );


    top_layout_->Add( bSizer15, 1, wxEXPAND, 5 );


    this->SetSizer( top_layout_ );
    this->Layout();

    this->Centre( wxBOTH );

    // Connect Events
    spin_reso_width_->Connect( wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler( PngExportDialog::EvalResoSpin ), NULL, this );
    spin_reso_height_->Connect( wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler( PngExportDialog::EvalResoSpin ), NULL, this );
    reso_lock_btn_->Connect( wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler( PngExportDialog::ResoLock ), NULL, this );
    bed_width_spin_->Connect( wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler( PngExportDialog::EvalBedSpin ), NULL, this );
    bed_height_spin_->Connect( wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler( PngExportDialog::EvalBedSpin ), NULL, this );
    bedsize_lock_btn_->Connect( wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler( PngExportDialog::BedsizeLock ), NULL, this );
    export_btn_->Connect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( PngExportDialog::Close ), NULL, this );
}

PngExportDialog::~PngExportDialog()
{
    // Disconnect Events
    spin_reso_width_->Disconnect( wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler( PngExportDialog::EvalResoSpin ), NULL, this );
    spin_reso_height_->Disconnect( wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler( PngExportDialog::EvalResoSpin ), NULL, this );
    reso_lock_btn_->Disconnect( wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler( PngExportDialog::ResoLock ), NULL, this );
    bed_width_spin_->Disconnect( wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler( PngExportDialog::EvalBedSpin ), NULL, this );
    bed_height_spin_->Disconnect( wxEVT_COMMAND_TEXT_UPDATED, wxCommandEventHandler( PngExportDialog::EvalBedSpin ), NULL, this );
    bedsize_lock_btn_->Disconnect( wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, wxCommandEventHandler( PngExportDialog::BedsizeLock ), NULL, this );
    export_btn_->Disconnect( wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler( PngExportDialog::Close ), NULL, this );

}
