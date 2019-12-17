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
using vector = std::vector<T, A>;

template<class L> void cleanup(vector<std::weak_ptr<L>> &listeners) {
    auto it = std::remove_if(listeners.begin(), listeners.end(),
                             [](auto &l) { return !l.lock(); });
    listeners.erase(it, listeners.end());
}

template<class F, class L, class...Args>
void call(F &&f, vector<std::weak_ptr<L>> &listeners, Args&&... args) {
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
        virtual ~Listener();
        
        virtual void on_left_click_down() {}
        virtual void on_left_click_up() {}
        virtual void on_right_click_down() {}
        virtual void on_right_click_up() {}
        virtual void on_double_click() {}
        virtual void on_scroll(long /*v*/, long /*delta*/, WheelAxis ) {}
        virtual void on_moved_to(long /*x*/, long /*y*/) {}
    };
    
private:
    vector<wkptr<Listener>> m_listeners;
        
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
    vector<float> vertices_and_normals_interleaved;
    vector<int>   triangle_indices;
    vector<int>   quad_indices;

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
public:
    static const constexpr unsigned DEFAULT_CONVEXITY = 10;
    
private:
    OpenCSG::Algorithm m_csgalg = OpenCSG::Algorithm::Automatic;
    OpenCSG::DepthComplexityAlgorithm m_depth_algo = OpenCSG::NoDepthComplexitySampling;
    OpenCSG::Optimization m_optim = OpenCSG::OptimizationDefault;
    bool m_enable = true;
    unsigned int m_convexity = DEFAULT_CONVEXITY;
    
public:
    int get_algo() const { return int(m_csgalg); }
    void set_algo(OpenCSG::Algorithm alg) { m_csgalg = alg; }
    
    int get_depth_algo() const { return int(m_depth_algo); }
    void set_depth_algo(OpenCSG::DepthComplexityAlgorithm alg) { m_depth_algo = alg; }
    
    int  get_optimization() const { return int(m_optim); }
    void set_optimization(OpenCSG::Optimization o) { m_optim = o; }
    
    void enable_csg(bool en = true) { m_enable = en; }
    bool is_enabled() const { return m_enable; }
    
    unsigned get_convexity() const { return m_convexity; }
    void set_convexity(unsigned c) { m_convexity = c; }
};
      
class Scene
{
    uqptr<SLAPrint> m_print;
public:
    
    class Listener {
    public:
        virtual ~Listener() = default;
        virtual void on_scene_updated(const Scene &scene) = 0;
    };
    
    Scene();
    ~Scene();
    
    void set_print(uqptr<SLAPrint> &&print);
    const SLAPrint * get_print() const { return m_print.get(); }
    
    BoundingBoxf3 get_bounding_box() const;
    
    void add_listener(shptr<Listener> listener)
    {
        m_listeners.emplace_back(listener);
        cleanup(m_listeners);
    }
    
private:
    vector<wkptr<Listener>> m_listeners;
};

class Display : public Scene::Listener
{
protected:
    Vec2i m_size;
    bool m_initialized = false;
    
    CSGSettings m_csgsettings;
    
    struct SceneCache {
        vector<shptr<Primitive>> primitives;
        vector<Primitive *> primitives_free;
        vector<OpenCSG::Primitive *> primitives_csg;
        
        void clear();
        
        shptr<Primitive> add_mesh(const TriangleMesh &mesh);
        shptr<Primitive> add_mesh(const TriangleMesh &mesh,
                                  OpenCSG::Operation  op,
                                  unsigned            covexity);
    } m_scene_cache;
    
    shptr<Camera>  m_camera;
    
public:
    
    explicit Display(shptr<Camera> camera = nullptr)
        : m_camera(camera ? camera : std::make_shared<PerspectiveCamera>())
    {}
    
    ~Display() override;
    
    Camera * camera() { return m_camera.get(); }
    
    virtual void swap_buffers() = 0;
    virtual void set_active(long width, long height);
    virtual void set_screen_size(long width, long height);
    Vec2i get_screen_size() const { return m_size; }
    
    virtual void repaint();
    
    bool is_initialized() const { return m_initialized; }
    
    const CSGSettings & get_csgsettings() const { return m_csgsettings; }
    void apply_csgsettings(const CSGSettings &settings);
    
    void on_scene_updated(const Scene &scene) override;
    
    virtual void clear_screen();
    virtual void render_scene();
};

class Controller : public std::enable_shared_from_this<Controller>,
                   public MouseInput::Listener,
                   public Scene::Listener
{
    long m_wheel_pos = 0;
    Vec2i m_mouse_pos, m_mouse_pos_rprev, m_mouse_pos_lprev;
    bool m_left_btn = false, m_right_btn = false;

    shptr<Scene>               m_scene;
    vector<wkptr<Display>> m_displays;
    
    // Call a method of Camera on all the cameras of the attached displays
    template<class F, class...Args>
    void call_cameras(F &&f, Args&&... args) {
        for (wkptr<Display> &l : m_displays)
            if (auto disp = l.lock()) if (disp->camera())
                (disp->camera()->*f)(std::forward<Args>(args)...);
    }
    
public:
    
    void set_scene(shptr<Scene> scene)
    {
        m_scene = scene;
        m_scene->add_listener(shared_from_this());
    }
    
    const Scene * get_scene() const { return m_scene.get(); }

    void add_display(shptr<Display> disp)
    {
        m_displays.emplace_back(disp);
        cleanup(m_displays);
    }
    
    void on_scene_updated(const Scene &scene) override;
    
    void on_left_click_down() override { m_left_btn = true; }
    void on_left_click_up() override { m_left_btn = false;  }
    void on_right_click_down() override { m_right_btn = true;  }
    void on_right_click_up() override { m_right_btn = false; }
    
    void on_scroll(long v, long d, MouseInput::WheelAxis wa) override;
    void on_moved_to(long x, long y) override;

    void move_clip_plane(double z) { call_cameras(&Camera::set_clip_z, z); }
};

}}     // namespace Slic3r::GL
#endif // GLSCENE_HPP
