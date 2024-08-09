#ifndef slic3r_DragDropPanel_hpp_
#define slic3r_DragDropPanel_hpp_

#include "GUI.hpp"
#include "GUI_Utils.hpp"

#include <wx/simplebook.h>
#include <wx/dialog.h>
#include <wx/timer.h>
#include <vector>


namespace Slic3r { namespace GUI {

class ColorPanel;
class DragDropPanel : public wxPanel
{
public:
    DragDropPanel(wxWindow *parent, const wxString &label, bool is_auto);

    void AddColorBlock(const wxColour &color, int filament_id);
    void RemoveColorBlock(ColorPanel *panel);
    void DoDragDrop(ColorPanel *panel, const wxColour &color, int filament_id);

    std::vector<int> GetAllFilaments() const;

    void set_is_draging(bool is_draging) { m_is_draging = is_draging; }
    bool is_draging() const { return m_is_draging; }

private:
    wxBoxSizer *m_sizer;
    wxGridSizer *m_grid_item_sizer;
    bool         m_is_auto;

private:
    bool m_is_draging = false;
};
}} // namespace Slic3r::GUI

#endif /* slic3r_DragDropPanel_hpp_ */
