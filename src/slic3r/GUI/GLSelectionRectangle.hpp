#ifndef slic3r_GLSelectionRectangle_hpp_
#define slic3r_GLSelectionRectangle_hpp_

#include "libslic3r/Point.hpp"

namespace Slic3r {
namespace GUI {
    
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
struct Camera;
//class Camera;
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

class GLSelectionRectangle {
public:
    enum EState {
            Off,
            SlaSelect,
            SlaDeselect
        };

    // Initiates the rectangle. Width and height describe canvas size.
    void start_dragging(const Vec2d& mouse_position, float width, float height, EState status);

    // To be called on mouse move.
    void dragging(const Vec2d& mouse_position);

    // Given a vector of points in world coordinates, the function returns indices of those
    // that are in the rectangle. It then disables the rectangle.
    std::vector<unsigned int> end_dragging(const Camera& camera, const std::vector<Vec3d>& points);

    void render() const;
        
    bool is_active() const { return m_status != Off; }
    EState get_status() const { return m_status; }
    
    

private:
    EState m_status = Off;
    Vec2d m_start_corner;
    Vec2d m_end_corner;
    float m_width;
    float m_height;
};

    
} // namespace GUI
} // namespace Slic3r


#endif // slic3r_GLGizmoSlaSupports_hpp_
