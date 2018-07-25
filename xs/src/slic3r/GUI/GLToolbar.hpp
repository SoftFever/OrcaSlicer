#ifndef slic3r_GLToolbar_hpp_
#define slic3r_GLToolbar_hpp_

#include "../../slic3r/GUI/GLTexture.hpp"

#include <string>
#include <vector>

namespace Slic3r {

class Pointf;

namespace GUI {

class GLCanvas3D;

class GLToolbarItem
{
public:
    enum EType : unsigned char
    {
        Action,
        Separator,
        Num_Types
    };

    enum EState : unsigned char
    {
        Normal,
        Hover,
        Pressed,
        Disabled,
        Num_States
    };

private:
    // icon textures are assumed to be square and all with the same size in pixels, no internal check is done
    GLTexture m_icon_textures[Num_States];

    EType m_type;
    EState m_state;

    std::string m_name;
    std::string m_tooltip;

public:
    GLToolbarItem(EType type, const std::string& name, const std::string& tooltip);

    bool load_textures(const std::string* filenames);

    EState get_state() const;
    void set_state(EState state);

    const std::string& get_name() const;

    const std::string& get_tooltip() const;

    unsigned int get_icon_texture_id() const;
    int get_icon_textures_size() const;

    bool is_separator() const;
};

class GLToolbar
{
public:
    struct ItemCreationData
    {
        std::string name;
        std::string tooltip;
        std::string textures[GLToolbarItem::Num_States];
    };

private:
    typedef std::vector<GLToolbarItem*> ItemsList;

    bool m_enabled;
    ItemsList m_items;

    float m_textures_scale;
    float m_offset_y;
    float m_gap_x;
    float m_separator_x;

public:
    GLToolbar();

    bool is_enabled() const;
    void set_enabled(bool enable);

    void set_textures_scale(float scale);
    void set_offset_y(float offset);
    void set_gap_x(float gap);
    void set_separator_x(float separator);

    bool add_item(const ItemCreationData& data);
    bool add_separator();

    void enable_item(const std::string& name);
    void disable_item(const std::string& name);

    void update_hover_state(GLCanvas3D& canvas, const Pointf& mouse_pos);

    void render(const GLCanvas3D& canvas, const Pointf& mouse_pos) const;

private:
    float _get_total_width() const;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLToolbar_hpp_
