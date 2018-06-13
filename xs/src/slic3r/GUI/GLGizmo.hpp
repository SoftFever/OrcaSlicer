#ifndef slic3r_GLGizmo_hpp_
#define slic3r_GLGizmo_hpp_

#include "../../slic3r/GUI/GLTexture.hpp"

namespace Slic3r {
namespace GUI {

class GLGizmoBase
{
public:
    enum EState
    {
        Off,
        Hover,
        On,
        Num_States
    };

protected:
    EState m_state;
    // textures are assumed to be all the same size in pixels
    // no internal check is done
    GLTexture m_textures[Num_States];

public:
    GLGizmoBase();
    virtual ~GLGizmoBase();

    bool init();

    EState get_state() const;

    unsigned int get_textures_id() const;
    int get_textures_height() const;
    int get_textures_width() const;

    virtual void render() const = 0;

protected:
    virtual bool on_init() = 0;
};

class GLGizmoRotate : public GLGizmoBase
{
    float m_angle_x;
    float m_angle_y;
    float m_angle_z;

public:
    GLGizmoRotate();

    void render() const;

protected:
    virtual bool on_init();
};

class GLGizmoScale : public GLGizmoBase
{
    float m_scale_x;
    float m_scale_y;
    float m_scale_z;

public:
    GLGizmoScale();

    void render() const;

protected:
    virtual bool on_init();
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmo_hpp_

