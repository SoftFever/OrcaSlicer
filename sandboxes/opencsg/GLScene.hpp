#ifndef GLSCENE_HPP
#define GLSCENE_HPP

#include <vector>
#include <memory>

#include <libslic3r/Geometry.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/SLA/Hollowing.hpp>
#include <opencsg/opencsg.h>

namespace Slic3r {

class SLAPrint;

namespace GL {

template<class T> using shptr = std::shared_ptr<T>;
template<class T> using uqptr = std::unique_ptr<T>;
template<class T> using wkptr = std::weak_ptr<T>;

template<class T, class A = std::allocator<T>>
using Collection = std::vector<T, A>;

template<class L> void cleanup(Collection<std::weak_ptr<L>> &listeners) {
    auto it = std::remove_if(listeners.begin(), listeners.end(),
                             [](auto &l) { return !l.lock(); });
    listeners.erase(it, listeners.end());
}

template<class F, class L, class...Args>
void call(F &&f, Collection<std::weak_ptr<L>> &listeners, Args&&... args) {
    for (auto &l : listeners)
        if (auto p = l.lock()) ((p.get())->*f)(std::forward<Args>(args)...);
}

class MouseInput
{
public:
    
    enum WheelAxis {
        waVertical, waHorizontal
    };
    
    class Listener {
    public:
        
        virtual ~Listener() = default;
        
        virtual void on_left_click_down() {}
        virtual void on_left_click_up() {}
        virtual void on_right_click_down() {}
        virtual void on_right_click_up() {}
        virtual void on_double_click() {}
        virtual void on_scroll(long /*v*/, long /*delta*/, WheelAxis ) {}
        virtual void on_moved_to(long /*x*/, long /*y*/) {}
    };
    
private:
    Collection<wkptr<Listener>> m_listeners;
        
public:
    virtual ~MouseInput() = default;

    virtual void left_click_down()
    {
        call(&Listener::on_left_click_down, m_listeners);
    }
    virtual void left_click_up()
    {
        call(&Listener::on_left_click_up, m_listeners);
    }
    virtual void right_click_down()
    {
        call(&Listener::on_right_click_down, m_listeners);
    }
    virtual void right_click_up()
    {
        call(&Listener::on_right_click_up, m_listeners);
    }
    virtual void double_click()
    {
        call(&Listener::on_double_click, m_listeners);
    }
    virtual void scroll(long v, long d, WheelAxis wa)
    {
        call(&Listener::on_scroll, m_listeners, v, d, wa);
    }
    virtual void move_to(long x, long y)
    {
        call(&Listener::on_moved_to, m_listeners, x, y);
    }
    
    void add_listener(shptr<Listener> listener)
    {
        m_listeners.emplace_back(listener);
        cleanup(m_listeners);
    }
};

class IndexedVertexArray {
public:
    ~IndexedVertexArray() { release_geometry(); }

    // Vertices and their normals, interleaved to be used by void
    // glInterleavedArrays(GL_N3F_V3F, 0, x)
    Collection<float> vertices_and_normals_interleaved;
    Collection<int>   triangle_indices;
    Collection<int>   quad_indices;

    // When the geometry data is loaded into the graphics card as Vertex
    // Buffer Objects, the above mentioned std::vectors are cleared and the
    // following variables keep their original length.
    size_t vertices_and_normals_interleaved_size{ 0 };
    size_t triangle_indices_size{ 0 };
    size_t quad_indices_size{ 0 };
    
    // IDs of the Vertex Array Objects, into which the geometry has been loaded.
    // Zero if the VBOs are not sent to GPU yet.
    unsigned int       vertices_and_normals_interleaved_VBO_id{ 0 };
    unsigned int       triangle_indices_VBO_id{ 0 };
    unsigned int       quad_indices_VBO_id{ 0 };
    
    
    void push_geometry(float x, float y, float z, float nx, float ny, float nz);

    inline void push_geometry(
        double x, double y, double z, double nx, double ny, double nz)
    {
        push_geometry(float(x), float(y), float(z), float(nx), float(ny), float(nz));
    }

    inline void push_geometry(const Vec3d &p, const Vec3d &n)
    {
        push_geometry(p(0), p(1), p(2), n(0), n(1), n(2));
    }

    void push_triangle(int idx1, int idx2, int idx3);
    
    void load_mesh(const TriangleMesh &mesh);

    inline bool has_VBOs() const
    {
        return vertices_and_normals_interleaved_VBO_id != 0;
    }

    // Finalize the initialization of the geometry & indices,
    // upload the geometry and indices to OpenGL VBO objects
    // and shrink the allocated data, possibly relasing it if it has been
    // loaded into the VBOs.
    void finalize_geometry();
    // Release the geometry data, release OpenGL VBOs.
    void release_geometry();
    
    void render() const;
    
    // Is there any geometry data stored?
    bool empty() const { return vertices_and_normals_interleaved_size == 0; }
    
    void clear();
    
    // Shrink the internal storage to tighly fit the data stored.
    void shrink_to_fit();
};

bool enable_multisampling(bool e = true);
void renderfps();

class Primitive : public OpenCSG::Primitive
{
    IndexedVertexArray m_geom;
    Geometry::Transformation m_trafo;
public:
    
    using OpenCSG::Primitive::Primitive;
    
    Primitive() : OpenCSG::Primitive(OpenCSG::Intersection, 1) {}
    
    void render();
    
    void translation(const Vec3d &offset) { m_trafo.set_offset(offset); }
    void rotation(const Vec3d &rot) { m_trafo.set_rotation(rot); }
    void scale(const Vec3d &scaleing) { m_trafo.set_scaling_factor(scaleing); }
    void scale(double s) { scale({s, s, s}); }
    
    inline void load_mesh(const TriangleMesh &mesh) {
        m_geom.load_mesh(mesh);
        m_geom.finalize_geometry();
    }
};

class Scene;

class Camera {
protected:
    Vec2f m_rot = {0., 0.};
    Vec3d m_referene = {0., 0., 0.};
    double m_zoom = 0.;
    double m_clip_z = 0.;
public:
    
    virtual ~Camera() = default;
    
    virtual void view();
    virtual void set_screen(long width, long height) = 0;
    
    void set_rotation(const Vec2f &rotation) { m_rot = rotation; }    
    void rotate(const Vec2f &rotation) { m_rot += rotation; }
    void set_zoom(double z) { m_zoom = z; }
    void set_reference_point(const Vec3d &p) { m_referene = p; }
    void set_clip_z(double z) { m_clip_z = z; }
};

class PerspectiveCamera: public Camera {
public:
    
    void set_screen(long width, long height) override;
};

class CSGSettings {
    OpenCSG::Algorithm m_csgalg = OpenCSG::Algorithm::Automatic;
public:
    void set_csg_algo(OpenCSG::Algorithm alg);
};
       
class Display : public std::enable_shared_from_this<Display>,
                public MouseInput::Listener
{
protected:
    shptr<Scene> m_scene;
    long m_wheel_pos = 0;
    Vec2i m_mouse_pos, m_mouse_pos_rprev, m_mouse_pos_lprev;
    Vec2i m_size;
    bool m_initialized = false, m_left_btn = false, m_right_btn = false;
    
    CSGSettings m_csgsettings;
    
    shptr<Camera> m_camera;
    
public:
    Display(shptr<Scene> scene = nullptr, shptr<Camera> camera = nullptr)
        : m_scene(scene)
        , m_camera(camera ? camera : std::make_shared<PerspectiveCamera>())
    {}

    virtual void swap_buffers() = 0;
    
    virtual void set_active(long width, long height);
    
    virtual void repaint(long width, long height);    
    void repaint() { repaint(m_size.x(), m_size.y()); }
    
    void set_scene(shptr<Scene> scene);
    shptr<Scene> get_scene() { return m_scene; }
    
    bool is_initialized() const { return m_initialized; }
    
    void on_scroll(long v, long d, MouseInput::WheelAxis wa) override;
    void on_moved_to(long x, long y) override;
    void on_left_click_down() override { m_left_btn = true; }
    void on_left_click_up() override { m_left_btn = false;  }
    void on_right_click_down() override { m_right_btn = true;  }
    void on_right_click_up() override { m_right_btn = false; }
    
    void move_clip_plane(double z) { m_camera->set_clip_z(z); }
    
    const CSGSettings & csgsettings() const { return m_csgsettings; }
    void csgsettings(const CSGSettings &settings) { m_csgsettings = settings; }
    
    virtual void on_scene_updated();
    virtual void clear_screen();
    virtual void render_scene();
};

class Scene: public MouseInput::Listener
{
    Collection<shptr<Primitive>> m_primitives;
    Collection<Primitive *> m_primitives_free;
    Collection<OpenCSG::Primitive *> m_primitives_csg;
    
    uqptr<SLAPrint> m_print;
public:
    
    Scene();
    ~Scene();
    
    const Collection<Primitive*>& free_primitives() const 
    { 
        return m_primitives_free; 
    }
    
    const Collection<OpenCSG::Primitive*>& csg_primitives() const 
    { 
        return m_primitives_csg; 
    }
    
    void add_display(shptr<Display> disp)
    {
        m_displays.emplace_back(disp);
        cleanup(m_displays);
    }
    
    void set_print(uqptr<SLAPrint> &&print);
    
    BoundingBoxf3 get_bounding_box() const;

protected:
    
    shptr<Primitive> add_mesh(const TriangleMesh &mesh);
    shptr<Primitive> add_mesh(const TriangleMesh &mesh,
                                OpenCSG::Operation op,
                                unsigned covexity);

private:
    
    Collection<wkptr<Display>> m_displays;
};

}}     // namespace Slic3r::GL
#endif // GLSCENE_HPP
