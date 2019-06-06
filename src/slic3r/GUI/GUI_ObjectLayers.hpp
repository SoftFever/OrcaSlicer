#ifndef slic3r_GUI_ObjectLayers_hpp_
#define slic3r_GUI_ObjectLayers_hpp_

#include "GUI_ObjectSettings.hpp"
#include "wxExtensions.hpp"

#ifdef __WXOSX__
#include "../libslic3r/PrintConfig.hpp"
#endif

class wxBoxSizer;

namespace Slic3r {
class ModelObject;

namespace GUI {
class ConfigOptionsGroup;

typedef double                          coordf_t;
typedef std::pair<coordf_t, coordf_t>   t_layer_height_range;

class LayerRangeEditor : public wxTextCtrl
{
    bool                m_enter_pressed     { false };
    bool                m_call_kill_focus   { false };

public:
    LayerRangeEditor(   wxWindow* parent,
                        const wxString& value = wxEmptyString,
                        std::function<bool(coordf_t val)> edit_fn = [](coordf_t) {return false; }
                        );
    ~LayerRangeEditor() {}

private:
    coordf_t            get_value();
};

class ObjectLayers : public OG_Settings
{
    ScalableBitmap  m_bmp_delete;
    ScalableBitmap  m_bmp_add;
    ModelObject*    m_object {nullptr};

    wxFlexGridSizer*       m_grid_sizer;
    t_layer_height_range   m_last_edited_range;

    enum SelectedItemType
    {
        sitUndef,
        sitMinZ,
        sitMaxZ,
        sitLayerHeight,
    } m_selection_type {sitUndef};

public:
    ObjectLayers(wxWindow* parent);
    ~ObjectLayers() {}

    wxSizer*    create_layer(const t_layer_height_range& range);    // without_buttons
    void        create_layers_list();
    void        update_layers_list();

    void        UpdateAndShow(const bool show) override;
    void        msw_rescale();
};

}}

#endif // slic3r_GUI_ObjectLayers_hpp_
