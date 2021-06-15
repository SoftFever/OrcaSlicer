#ifndef slic3r_GUI_ExtruderSequenceDialog_hpp_
#define slic3r_GUI_ExtruderSequenceDialog_hpp_

#include "GUI_Utils.hpp"
#include "DoubleSlider.hpp"

class wxTextCtrl;
class wxFlexGridSizer;
class wxCheckBox;

namespace Slic3r {
namespace GUI {

// ----------------------------------------------------------------------------
// ExtruderSequenceDialog: a node inside ObjectDataViewModel
// ----------------------------------------------------------------------------

class ExtruderSequenceDialog: public DPIDialog
{
    ScalableBitmap  m_bmp_del;
    ScalableBitmap  m_bmp_add;
    DoubleSlider::ExtrudersSequence m_sequence;

    wxTextCtrl* m_interval_by_layers {nullptr}; 
    wxTextCtrl* m_interval_by_mm {nullptr};
    wxCheckBox* m_random_sequence {nullptr};
    wxCheckBox* m_color_repetition{nullptr};

    wxFlexGridSizer* m_intervals_grid_sizer {nullptr};
    wxFlexGridSizer* m_extruders_grid_sizer {nullptr};
public:
    ExtruderSequenceDialog(const DoubleSlider::ExtrudersSequence& sequence);

    ~ExtruderSequenceDialog() {}

    DoubleSlider::ExtrudersSequence GetValue() { return m_sequence; }

protected:
    void apply_extruder_sequence();
    void on_dpi_changed(const wxRect& suggested_rect) override;
    
};

}
}


#endif // slic3r_GUI_ExtruderSequenceDialog_hpp_
