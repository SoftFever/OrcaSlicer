#ifndef slic3r_CoordAxes_hpp_
#define slic3r_CoordAxes_hpp_

#include "GLModel.hpp"

namespace Slic3r {
namespace GUI {

class CoordAxes
{
public:
    static const float DefaultStemRadius;
    static const float DefaultStemLength;
    static const float DefaultTipRadius;
    static const float DefaultTipLength;

private:
    Vec3d m_origin{ Vec3d::Zero() };
    float m_stem_radius{ DefaultStemRadius };
    float m_stem_length{ DefaultStemLength };
    float m_tip_radius{ DefaultTipRadius };
    float m_tip_length{ DefaultTipLength };
    GLModel m_arrow;

public:
    const Vec3d& get_origin() const { return m_origin; }
    void set_origin(const Vec3d& origin) { m_origin = origin; }
    void set_stem_radius(float radius) {
        m_stem_radius = radius;
        m_arrow.reset();
    }
    void set_stem_length(float length) {
        m_stem_length = length;
        m_arrow.reset();
    }
    void set_tip_radius(float radius) {
        m_tip_radius = radius;
        m_arrow.reset();
    }
    void set_tip_length(float length) {
        m_tip_length = length;
        m_arrow.reset();
    }

    float get_stem_radius() const { return m_stem_radius; }
    float get_stem_length() const { return m_stem_length; }
    float get_tip_radius() const { return m_tip_radius; }
    float get_tip_length() const { return m_tip_length; }
    float get_total_length() const { return m_stem_length + m_tip_length; }

    void render(const Transform3d& trafo, float emission_factor = 0.0f);
};

} // GUI
} // Slic3r

#endif // slic3r_CoordAxes_hpp_
