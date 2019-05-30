#ifndef slic3r_GUI_ObjectLayers_hpp_
#define slic3r_GUI_ObjectLayers_hpp_

#include "GUI_ObjectSettings.hpp"
#include "wxExtensions.hpp"

class wxBoxSizer;

namespace Slic3r {
class ModelObject;

namespace GUI {
class ConfigOptionsGroup;

class ObjectLayers : public OG_Settings
{
    ScalableBitmap  m_bmp_delete;
    ScalableBitmap  m_bmp_add;
    ModelObject*    m_object {nullptr};

    std::vector<std::string> m_legends;

public:
    ObjectLayers(wxWindow* parent);
    ~ObjectLayers() {}

    void        create_layers_list();
    void        create_layer();
    void        update_layers_list();
    void        add_layer() {};
    void        del_layer() {};

    void        UpdateAndShow(const bool show) override;
    void        msw_rescale();
};

}}

#endif // slic3r_GUI_ObjectLayers_hpp_
