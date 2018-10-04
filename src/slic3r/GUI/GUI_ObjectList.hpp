#ifndef slic3r_GUI_ObjectList_hpp_
#define slic3r_GUI_ObjectList_hpp_

#include <wx/panel.h>
#include <wx/bitmap.h>

class wxBoxSizer;
class wxDataViewCtrl;
class wxDataViewColumn;
class PrusaObjectDataViewModel;

namespace Slic3r {
namespace GUI {

class ConfigOptionsGroup;

class ObjectList
{
    wxBoxSizer                  *m_sizer {nullptr};
    wxDataViewCtrl				*m_objects_ctrl{ nullptr };
    PrusaObjectDataViewModel	*m_objects_model{ nullptr };
    wxWindow                    *m_parent{ nullptr };

    wxBitmap	m_icon_modifiermesh;
    wxBitmap	m_icon_solidmesh;
    wxBitmap	m_icon_manifold_warning;
    wxBitmap	m_bmp_cog;
    wxBitmap	m_bmp_split;

    int			m_selected_object_id = -1;

public:
    ObjectList(wxWindow* parent);
    ~ObjectList() {}

    void                create_objects_ctrl();
    wxDataViewColumn*   create_objects_list_extruder_column(int extruders_count);
    void                update_objects_list_extruder_column(int extruders_count);

    void                set_tooltip_for_item(const wxPoint& pt);

    wxPoint             get_mouse_position_in_control();
    wxBoxSizer*         get_sizer(){return  m_sizer;}
    int                 get_sel_obj_id() { return m_selected_object_id; }
};


}}

#endif //slic3r_GUI_ObjectList_hpp_
