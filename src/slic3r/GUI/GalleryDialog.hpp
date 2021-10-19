#ifndef slic3r_GalleryDialog_hpp_
#define slic3r_GalleryDialog_hpp_

#include "GUI_Utils.hpp"

class wxListCtrl;
class wxImageList;
class wxListEvent;

namespace Slic3r {

namespace GUI {


//------------------------------------------
//          GalleryDialog
//------------------------------------------

class GalleryDialog : public DPIDialog
{
    wxListCtrl*     m_list_ctrl  { nullptr };
    wxImageList*    m_image_list { nullptr };

    struct Item {
        std::string name;
        bool        is_system;
    };
    std::vector<Item>   m_selected_items;

    int ID_BTN_ADD_CUSTOM_SHAPE;
    int ID_BTN_DEL_CUSTOM_SHAPE;
    int ID_BTN_REPLACE_CUSTOM_PNG;

    void load_label_icon_list();
    void add_custom_shapes(wxEvent& event);
    void del_custom_shapes();
    void del_custom_shapes(wxEvent& event) { del_custom_shapes(); }
    void change_thumbnail();
    void change_thumbnail(wxEvent& event) { change_thumbnail(); }
    void select(wxListEvent& event);
    void deselect(wxListEvent& event);
    void show_context_menu(wxListEvent& event);
    void key_down(wxListEvent& event);

    bool can_delete();
    bool can_change_thumbnail();

    void update();

public:
    GalleryDialog(wxWindow* parent, bool modify_gallery = false);
    ~GalleryDialog();

    void get_input_files(wxArrayString& input_files);
    bool load_files(const wxArrayString& input_files);

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override {};
};


} // namespace GUI
} // namespace Slic3r

#endif //slic3r_GalleryDialog_hpp_
