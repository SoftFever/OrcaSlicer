#ifndef slic3r_GUI_SingleChoice_hpp_
#define slic3r_GUI_SingleChoice_hpp_

#include "GUI_Utils.hpp"
#include "Plater.hpp"
#include "Selection.hpp"

#include "Widgets/Button.hpp"
#include "Widgets/SpinInput.hpp"
#include "Widgets/DialogButtons.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ProgressBar.hpp"

namespace Slic3r { namespace GUI {

class CloneDialog : public DPIDialog
{
public:
    CloneDialog(wxWindow *parent = nullptr);
    ~CloneDialog();

private:
    SpinInput*   m_count_spin;
    int          m_count;
    CheckBox*    m_arrange_cb;
    Plater*      m_plater;
    ProgressBar* m_progress;
    wxWindow*    m_parent;
    bool         m_cancel_process;
    //AppConfig* m_config;

    void on_dpi_changed(const wxRect &suggested_rect) override {}

};
}} // namespace Slic3r::GUI

#endif