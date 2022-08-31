#ifndef slic3r_BindDialog_hpp_
#define slic3r_BindDialog_hpp_

#include "I18N.hpp"

#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/gauge.h>
#include <wx/button.h>
#include <wx/bitmap.h>
#include <wx/image.h>
#include <wx/icon.h>
#include <wx/dialog.h>
#include <curl/curl.h>
#include "wxExtensions.hpp"
#include "Plater.hpp"
#include "Widgets/StepCtrl.hpp"
#include "Widgets/ProgressDialog.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/ProgressBar.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Jobs/BindJob.hpp"
#include "BBLStatusBar.hpp"
#include "BBLStatusBarBind.hpp"

#define BIND_DIALOG_GREY200 wxColour(248, 248, 248)
#define BIND_DIALOG_GREY800 wxColour(50, 58, 61)
#define BIND_DIALOG_GREY900 wxColour(38, 46, 48)
#define BIND_DIALOG_BUTTON_SIZE wxSize(FromDIP(68), FromDIP(24))
#define BIND_DIALOG_BUTTON_PANEL_SIZE wxSize(FromDIP(450), FromDIP(30))

namespace Slic3r { namespace GUI {

struct MemoryStruct
{
    char * memory;
    size_t read_pos;
    size_t size;
};

class BindMachineDialog : public DPIDialog
{
private:
    wxStaticText * m_printer_name;
    wxStaticText * m_user_name;
    StaticBox *   m_panel_left;
    StaticBox *   m_panel_right;
    wxStaticText *m_status_text;
    Button *      m_button_bind;
    Button *      m_button_cancel;
    wxSimplebook *m_simplebook;
    wxStaticBitmap *m_avatar;

    MachineObject *                   m_machine_info{nullptr};
    std::shared_ptr<BindJob>          m_bind_job;
    std::shared_ptr<BBLStatusBarBind> m_status_bar;

public:
    BindMachineDialog(Plater *plater = nullptr);
    ~BindMachineDialog();
    void     on_cancel(wxCommandEvent &event);
    void     on_bind_fail(wxCommandEvent &event);
    void     on_update_message(wxCommandEvent &event);
    void     on_bind_success(wxCommandEvent &event);
    void     on_bind_printer(wxCommandEvent &event);
    void     on_dpi_changed(const wxRect &suggested_rect) override;
    void     update_machine_info(MachineObject *info) { m_machine_info = info; };
    void     on_show(wxShowEvent &event);
    void     on_close(wxCloseEvent& event);
};

class UnBindMachineDialog : public DPIDialog
{
protected:
    wxStaticText *  m_printer_name;
    wxStaticText *  m_user_name;
    wxStaticText *m_status_text;
    Button *      m_button_unbind;
    Button *      m_button_cancel;
    MachineObject *m_machine_info{nullptr};
    wxStaticBitmap *m_avatar;

public:
    UnBindMachineDialog(Plater *plater = nullptr);
    ~UnBindMachineDialog();

    void on_cancel(wxCommandEvent &event);
    void on_unbind_printer(wxCommandEvent &event);
    void on_dpi_changed(const wxRect &suggested_rect) override;
    void update_machine_info(MachineObject *info) { m_machine_info = info; };
    void on_show(wxShowEvent &event);
};

}} // namespace Slic3r::GUI

#endif /* slic3r_BindDialog_hpp_ */
