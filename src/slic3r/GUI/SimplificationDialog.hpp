#ifndef slic3r_SimplificationDialog_hpp_
#define slic3r_SimplificationDialog_hpp_

#include "GUI_Utils.hpp"

namespace Slic3r {
namespace GUI {

class SimplificationDialog : public DPIDialog
{
    void OnOK(wxEvent& event);

public:
    SimplificationDialog(wxWindow* parent);
    ~SimplificationDialog();

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
};


} // namespace GUI
} // namespace Slic3r

#endif //slic3r_SimplificationDialog_hpp_
