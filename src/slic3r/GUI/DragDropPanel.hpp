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

    void AddColorBlock(const wxColour &color, int filament_id, bool update_ui = true);
    void RemoveColorBlock(ColorPanel *panel, bool update_ui = true);
    void DoDragDrop(ColorPanel *panel, const wxColour &color, int filament_id);

    std::vector<int> GetAllFilaments() const;

    void set_is_draging(bool is_draging) { m_is_draging = is_draging; }
    bool is_draging() const { return m_is_draging; }

    std::vector<ColorPanel *> get_filament_blocks() const { return m_filament_blocks; }

private:
    wxBoxSizer *m_sizer;
    wxGridSizer *m_grid_item_sizer;
    bool         m_is_auto;

    std::vector<ColorPanel *> m_filament_blocks;
private:
    bool m_is_draging = false;
};

///////////////   ColorPanel  start ////////////////////////
// The UI panel of drag item
class ColorPanel : public wxPanel
{
public:
    ColorPanel(DragDropPanel *parent, const wxColour &color, int filament_id);

    wxColour GetColor() const { return m_color; }
    int      GetFilamentId() const { return m_filament_id; }

private:
    void OnLeftDown(wxMouseEvent &event);
    void OnLeftUp(wxMouseEvent &event);
    void OnPaint(wxPaintEvent &event);

    DragDropPanel *m_parent;
    wxColor        m_color;
    int            m_filament_id;
};
}} // namespace Slic3r::GUI

#endif /* slic3r_DragDropPanel_hpp_ */
