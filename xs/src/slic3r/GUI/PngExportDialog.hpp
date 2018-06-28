#ifndef PNG_EXPORT_DIALOG_HPP
#define PNG_EXPORT_DIALOG_HPP

#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/intl.h>
#include <wx/string.h>
#include <wx/stattext.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/filepicker.h>
#include <wx/spinctrl.h>
#include <wx/tglbtn.h>
#include <wx/button.h>
#include <wx/dialog.h>

#include "GUI.hpp"

namespace Slic3r {

///////////////////////////////////////////////////////////////////////////////
/// Class PngExportDialog
///////////////////////////////////////////////////////////////////////////////
class PngExportDialog : public wxDialog
{
    private:

    protected:
        wxFilePickerCtrl* filepick_ctl_;
        wxFilePickerCtrl* confpick_ctl_;
        wxSpinCtrl* spin_reso_width_;
        wxSpinCtrl* spin_reso_height_;
        wxToggleButton* reso_lock_btn_;
        wxSpinCtrlDouble* bed_width_spin_;
        wxSpinCtrlDouble* bed_height_spin_;
        wxToggleButton* bedsize_lock_btn_;
        wxSpinCtrlDouble* exptime_spin_;
        wxSpinCtrlDouble* exptime_first_spin_;
        wxSpinCtrlDouble* corr_spin_x_;
        wxSpinCtrlDouble* corr_spin_y_;
        wxSpinCtrlDouble* corr_spin_z_;
        wxButton* export_btn_;

        // Virtual event handlers, overide them in your derived class
        virtual void onFileChanged( wxFileDirPickerEvent& event ) { event.Skip(); }
        virtual void EvalResoSpin( wxCommandEvent& event ) { event.Skip(); }
        virtual void ResoLock( wxCommandEvent& event ) { event.Skip(); }
        virtual void EvalBedSpin( wxCommandEvent& event ) { event.Skip(); }
        virtual void BedsizeLock( wxCommandEvent& event ) { event.Skip(); }
        virtual void Close( wxCommandEvent& /*event*/ ) { EndModal(wxID_OK); }

    public:

        PngExportDialog( wxWindow* parent, wxWindowID id = wxID_ANY, const wxString& title = _("Slice to zipped PNG files"), const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxSize( 452,170 ), long style = wxDEFAULT_DIALOG_STYLE );
        ~PngExportDialog();

};

}
#endif //PNG_EXPORT_DIALOG_HPP
