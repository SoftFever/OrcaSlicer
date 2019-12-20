#include <iostream>
#include <utility>
#include <memory>

#include "Engine.hpp"
#include "ShaderCSGDisplay.hpp"

#include <GL/glew.h>

#include <opencsg/opencsg.h>
// For compilers that support precompilation, includes "wx/wx.h".
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif

#include <wx/slider.h>
#include <wx/tglbtn.h>
#include <wx/combobox.h>
#include <wx/spinctrl.h>
#include <wx/msgdlg.h>
#include <wx/glcanvas.h>
#include <wx/cmdline.h>

#include "libslic3r/Model.hpp"
#include "libslic3r/Format/3mf.hpp"
#include "libslic3r/SLAPrint.hpp"

#include "slic3r/GUI/Job.hpp"
#include "slic3r/GUI/ProgressStatusBar.hpp"

using namespace Slic3r::GL;

class Renderer {
protected:
    wxGLCanvas *m_canvas;
    shptr<wxGLContext> m_context;
public:
    
    Renderer(wxGLCanvas *c): m_canvas{c} {
        auto ctx = new wxGLContext(m_canvas);
        if (!ctx || !ctx->IsOK()) {
            wxMessageBox("Could not create OpenGL context.", "Error",
                         wxOK | wxICON_ERROR);
            return;
        }
        
        m_context.reset(ctx);
    }
    
    wxGLContext * context() { return m_context.get(); }
    const wxGLContext * context() const { return m_context.get(); }
};

// Tell the CSGDisplay how to swap buffers and set the gl context.
class OCSGRenderer: public Renderer, public Slic3r::GL::CSGDisplay {
public:
    
    OCSGRenderer(wxGLCanvas *c): Renderer{c} {}
    
    void set_active(long w, long h) override
    {
        m_canvas->SetCurrent(*m_context);
        Slic3r::GL::Display::set_active(w, h);
    }
    
    void swap_buffers() override { m_canvas->SwapBuffers(); }
    
    ~OCSGRenderer() override { m_scene_cache.clear(); }
};

// Tell the CSGDisplay how to swap buffers and set the gl context.
class ShaderCSGRenderer : public Renderer,
                          public Slic3r::GL::ShaderCSGDisplay
{
public:
    
    ShaderCSGRenderer(wxGLCanvas *c): Renderer{c} {}
    
    void set_active(long w, long h) override
    {
        m_canvas->SetCurrent(*m_context);
        Slic3r::GL::Display::set_active(w, h);
    }
    
    void swap_buffers() override { m_canvas->SwapBuffers(); }
};

// The opengl rendering facility. Here we implement the rendering objects.
class Canvas: public wxGLCanvas
{
    // Create the OCSGDisplay for rendering with OpenCSG algorithms
    shptr<OCSGRenderer> m_ocsgdisplay = std::make_shared<OCSGRenderer>(this);
    
    // One display is active at a time, the OCSGRenderer by default.
    shptr<Slic3r::GL::Display> m_display = m_ocsgdisplay;
    
public:
    
    template<class...Args>
    Canvas(Args &&...args): wxGLCanvas(std::forward<Args>(args)...)
    {
        Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
            // This is required even though dc is not used otherwise.
            wxPaintDC dc(this);

            // Set the OpenGL viewport according to the client size of this
            // canvas. This is done here rather than in a wxSizeEvent handler
            // because our OpenGL rendering context (and thus viewport
            // setting) is used with multiple canvases: If we updated the
            // viewport in the wxSizeEvent handler, changing the size of one
            // canvas causes a viewport setting that is wrong when next
            // another canvas is repainted.
            const wxSize ClientSize = GetClientSize();
            
            m_display->set_screen_size(ClientSize.x, ClientSize.y);
            m_display->repaint();
        });
    }
    
    shptr<Slic3r::GL::Display> get_display() const { return m_display; }

    void set_display(shptr<Slic3r::GL::Display> d) { m_display = d; }

    shptr<Slic3r::GL::CSGDisplay> get_ocsg_display() const { return m_ocsgdisplay; }
};

// Enumerate possible mouse events, we will record them.
enum EEvents { LCLK_U, RCLK_U, LCLK_D, RCLK_D, DDCLK, SCRL, MV };
struct Event
{
    EEvents type;
    long    a, b;
    Event(EEvents t, long x = 0, long y = 0) : type{t}, a{x}, b{y} {}
};

// Create a special mouse input adapter, which can store (record) the received
// mouse signals into a file and play back the stored events later.
class RecorderMouseInput: public MouseInput {
    std::vector<Event> m_events;
    bool m_recording = false, m_playing = false;
    
public:
    void left_click_down() override
    {
        if (m_recording) m_events.emplace_back(LCLK_D);
        if (!m_playing) MouseInput::left_click_down();
    }
    void left_click_up() override
    {
        if (m_recording) m_events.emplace_back(LCLK_U);
        if (!m_playing) MouseInput::left_click_up();
    }
    void right_click_down() override
    {
        if (m_recording) m_events.emplace_back(RCLK_D);
        if (!m_playing) MouseInput::right_click_down();
    }
    void right_click_up() override
    {
        if (m_recording) m_events.emplace_back(RCLK_U);
        if (!m_playing) MouseInput::right_click_up();
    }
    void double_click() override
    {
        if (m_recording) m_events.emplace_back(DDCLK);
        if (!m_playing) MouseInput::double_click();
    }
    void scroll(long v, long d, WheelAxis wa) override
    {
        if (m_recording) m_events.emplace_back(SCRL, v, d);
        if (!m_playing) MouseInput::scroll(v, d, wa);
    }
    void move_to(long x, long y) override
    {
        if (m_recording) m_events.emplace_back(MV, x, y);
        if (!m_playing) MouseInput::move_to(x, y);
    }
    
    void save(std::ostream &stream)
    {
        for (const Event &evt : m_events)
            stream << evt.type << " " << evt.a << " " << evt.b << std::endl;
    }
    
    void load(std::istream &stream)
    {
        m_events.clear();
        while (stream.good()) {
            int type; long a, b;
            stream >> type >> a >> b;
            m_events.emplace_back(EEvents(type), a, b);
        }
    }    
    
    void record(bool r) { m_recording = r; if (r) m_events.clear(); }
    
    void play()
    {
        m_playing = true;
        std::cout << "Mouse is playing back" << std::endl;
        for (const Event &evt : m_events) {
            switch (evt.type) {
            case LCLK_U: MouseInput::left_click_up(); break;
            case LCLK_D: MouseInput::left_click_down(); break;
            case RCLK_U: MouseInput::right_click_up(); break;
            case RCLK_D: MouseInput::right_click_down(); break;
            case DDCLK:  MouseInput::double_click(); break;
            case SCRL:   MouseInput::scroll(evt.a, evt.b, WheelAxis::waVertical); break;
            case MV:     MouseInput::move_to(evt.a, evt.b); break;
            }
            
            wxYield();
            if (!m_playing)
                break;
        }
        m_playing = false;
    }
    
    void stop() { m_playing = false; }
    bool is_playing() const { return m_playing; }
};

// The top level frame of the application.
class MyFrame: public wxFrame
{
    // Instantiate the 3D engine.
    shptr<Scene>      m_scene;    // Model
    shptr<Canvas>     m_canvas;   // View
    shptr<Controller> m_ctl;      // Controller
    
    // Add a status bar with progress indication.
    shptr<Slic3r::GUI::ProgressStatusBar> m_stbar;
    
    RecorderMouseInput m_mouse;
    
    // When loading a Model from 3mf and preparing it, we use a separate thread.
    class SLAJob: public Slic3r::GUI::Job {
        MyFrame *m_parent;
        std::unique_ptr<Slic3r::SLAPrint> m_print;
        std::string m_fname;
        
    public:
        SLAJob(MyFrame *frame, const std::string &fname)
            : Slic3r::GUI::Job{frame->m_stbar}
            , m_parent{frame}
            , m_fname{fname}
        {}
        
        // Runs in separate thread
        void process() override;
        
        const std::string & get_project_fname() const { return m_fname; }
        
    protected:
        
        // Runs in the UI thread.
        void finalize() override 
        {
            m_parent->m_scene->set_print(std::move(m_print));
            m_parent->m_stbar->set_status_text(
                        wxString::Format("Model %s loaded.", m_fname));
            std::cout << "Model loaded" << std::endl;
        }
    };
    
    uqptr<SLAJob> m_ui_job;
    
    // To keep track of the running average of measured fps values.
    double m_fps_avg = 0.;
    
    wxToggleButton *m_record_btn;
    
public:
    MyFrame(const wxString &               title,
            const wxPoint &                pos,
            const wxSize &                 size,
            const wxCmdLineParser &settings);
    
    // Grab a 3mf and load (hollow it out) within the UI job.
    void load_model(const std::string &fname) {
        m_ui_job = std::make_unique<SLAJob>(this, fname);
        m_ui_job->start();
    }
    
    // Load a previously stored mouse event log and play it back.
    void play_back_mouse(const std::string &events_fname)
    {
        std::fstream stream(events_fname, std::fstream::in);

        if (stream.good()) {
            std::string model_name;
            std::getline(stream, model_name);
            load_model(model_name);
            while (!m_ui_job->is_finalized()) {
                wxYield();
            }
            
            int w, h;
            stream >> w >> h;
            SetSize(w, h);
            
            m_mouse.load(stream);
            if (m_record_btn) m_record_btn->Disable();
            m_mouse.play();
        }
    }
    
    Canvas * canvas() { return m_canvas.get(); }
    const Canvas * canvas() const { return m_canvas.get(); }
    
    // Bind the canvas mouse events to a class implementing MouseInput interface
    void bind_canvas_events(MouseInput &msinput);
    
    double get_fps_average() const { return m_fps_avg; }
};

// Possible OpenCSG configuration values. Will be used on the command line and
// on the UI widgets.
static const std::vector<wxString> CSG_ALGS  = {"Auto", "Goldfeather", "SCS", "EnricoShader"};
static const std::vector<wxString> CSG_DEPTH = {"Off", "OcclusionQuery", "On"};
static const std::vector<wxString> CSG_OPT   = { "Default", "ForceOn", "On", "Off" };

class App : public wxApp {
    MyFrame *m_frame = nullptr;
public:
    bool OnInit() override {
        
        wxCmdLineParser parser(argc, argv);
        
        parser.AddOption("p", "play", "play back file", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
        parser.AddOption("a", "algorithm", "OpenCSG algorithm [Auto|Goldfeather|SCS]", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
        parser.AddOption("d", "depth", "OpenCSG depth strategy [Off|OcclusionQuery|On]", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
        parser.AddOption("o", "optimization", "OpenCSG optimization strategy [Default|ForceOn|On|Off]", wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL);
        parser.AddOption("c", "convexity", "OpenCSG convexity parameter for generic meshes", wxCMD_LINE_VAL_NUMBER, wxCMD_LINE_PARAM_OPTIONAL);
        parser.AddSwitch("", "disable-csg", "Disable csg rendering", wxCMD_LINE_PARAM_OPTIONAL);
        
        parser.Parse();
        
        wxString fname;
        bool is_play = parser.Found("play", &fname);
        
        m_frame = new MyFrame("PrusaSlicer OpenCSG Demo", wxDefaultPosition, wxSize(1024, 768), parser);

        if (is_play) {
            m_frame->Show( true );
            m_frame->play_back_mouse(fname.ToStdString());
            
            std::cout << m_frame->get_fps_average() << std::endl;
            
            m_frame->Destroy();
            
        } else m_frame->Show( true );
        
        return true;
    }
};

wxIMPLEMENT_APP(App);

MyFrame::MyFrame(const wxString &title, const wxPoint &pos, const wxSize &size, 
                 const wxCmdLineParser &parser):
    wxFrame(nullptr, wxID_ANY, title, pos, size)
{
    wxMenu *menuFile = new wxMenu;
    menuFile->Append(wxID_OPEN);
    menuFile->Append(wxID_EXIT);
    wxMenuBar *menuBar = new wxMenuBar;
    menuBar->Append( menuFile, "&File" );
    SetMenuBar( menuBar );
    
    m_stbar = std::make_shared<Slic3r::GUI::ProgressStatusBar>(this);
    m_stbar->embed(this);
    
    SetStatusText( "Welcome to wxWidgets!" );
    
    int attribList[] =
    {WX_GL_RGBA, WX_GL_DOUBLEBUFFER,
     // RGB channels each should be allocated with 8 bit depth. One
     // should almost certainly get these bit depths by default.
     WX_GL_MIN_RED, 8, WX_GL_MIN_GREEN, 8, WX_GL_MIN_BLUE, 8,
     // Requesting an 8 bit alpha channel. Interestingly, the NVIDIA
     // drivers would most likely work with some alpha plane, but
     // glReadPixels would not return the alpha channel on NVIDIA if
     // not requested when the GL context is created.
     WX_GL_MIN_ALPHA, 8, WX_GL_DEPTH_SIZE, 8, WX_GL_STENCIL_SIZE, 8,
     /*WX_GL_SAMPLE_BUFFERS, GL_TRUE, WX_GL_SAMPLES, 4,*/ 0};
    
    m_scene = std::make_shared<Scene>();
    m_ctl = std::make_shared<Controller>();
    m_ctl->set_scene(m_scene);
    
    m_canvas = std::make_shared<Canvas>(this, wxID_ANY, attribList,
                                        wxDefaultPosition, wxDefaultSize,
                                        wxWANTS_CHARS | wxFULL_REPAINT_ON_RESIZE);
    
    wxString alg;
    parser.Found("algorithm", &alg);
    
    wxString depth;
    parser.Found("depth", &depth);
    
    wxString opt;
    parser.Found("optimization", &opt);
    
    long convexity = 1;
    parser.Found("convexity", &convexity);
    
    bool csg_off = parser.Found("disable-csg");
    
    auto get_idx = [](const wxString &a, const std::vector<wxString> &v) {
        auto it = std::find(v.begin(), v.end(), a.ToStdString());
        return it - v.begin();
    };
    
    Slic3r::GL::CSGSettings settings;
    
    if (auto a = get_idx(alg, CSG_ALGS) < OpenCSG::AlgorithmUnused) 
            settings.set_algo(OpenCSG::Algorithm(a));
    
    if (auto a = get_idx(depth, CSG_DEPTH) < OpenCSG::DepthComplexityAlgorithmUnused) 
            settings.set_depth_algo(OpenCSG::DepthComplexityAlgorithm(a));
    
    if (auto a = get_idx(opt, CSG_OPT) < OpenCSG::OptimizationUnused) 
            settings.set_optimization(OpenCSG::Optimization(a));
    
    settings.set_convexity(unsigned(convexity));
    settings.enable_csg(!csg_off);
    
    m_canvas->get_ocsg_display()->apply_csgsettings(settings);

    wxPanel *control_panel = new wxPanel(this);

    auto controlsizer = new wxBoxSizer(wxHORIZONTAL);
    auto slider_sizer = new wxBoxSizer(wxVERTICAL);
    auto console_sizer = new wxBoxSizer(wxVERTICAL);
    
    auto slider = new wxSlider(control_panel, wxID_ANY, 0, 0, 100,
                               wxDefaultPosition, wxDefaultSize,
                               wxSL_VERTICAL);
    slider_sizer->Add(slider, 1, wxEXPAND);
    
    auto ms_toggle = new wxToggleButton(control_panel, wxID_ANY, "Multisampling");
    console_sizer->Add(ms_toggle, 0, wxALL | wxEXPAND, 5);
    
    auto csg_toggle = new wxToggleButton(control_panel, wxID_ANY, "CSG");
    csg_toggle->SetValue(true);
    console_sizer->Add(csg_toggle, 0, wxALL | wxEXPAND, 5);
    
    auto add_combobox = [control_panel, console_sizer]
            (const wxString &label, const std::vector<wxString> &list)
    {
        auto widget = new wxComboBox(control_panel, wxID_ANY, list[0],
                wxDefaultPosition, wxDefaultSize,
                int(list.size()), list.data());
        
        auto sz = new wxBoxSizer(wxHORIZONTAL);
        sz->Add(new wxStaticText(control_panel, wxID_ANY, label), 0,
                wxALL | wxALIGN_CENTER, 5);
        sz->Add(widget, 1, wxALL | wxEXPAND, 5);
        console_sizer->Add(sz, 0, wxEXPAND);
        return widget;
    };
    
    auto add_spinctl = [control_panel, console_sizer]
            (const wxString &label, int initial, int min, int max)
    {    
        auto widget = new wxSpinCtrl(
                    control_panel, wxID_ANY,
                    wxString::Format("%d", initial),
                    wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, min, max,
                    initial);
        
        auto sz = new wxBoxSizer(wxHORIZONTAL);
        sz->Add(new wxStaticText(control_panel, wxID_ANY, label), 0,
                wxALL | wxALIGN_CENTER, 5);
        sz->Add(widget, 1, wxALL | wxEXPAND, 5);
        console_sizer->Add(sz, 0, wxEXPAND);
        return widget;
    };
    
    auto convexity_spin = add_spinctl("Convexity", CSGSettings::DEFAULT_CONVEXITY, 0, 100);
    
    auto alg_select = add_combobox("Algorithm", CSG_ALGS);
    auto depth_select = add_combobox("Depth Complexity", CSG_DEPTH);
    auto optimization_select = add_combobox("Optimization", CSG_OPT);
    depth_select->Disable();
    
    auto fpstext = new wxStaticText(control_panel, wxID_ANY, "");
    console_sizer->Add(fpstext, 0, wxALL, 5);
    
    m_record_btn = new wxToggleButton(control_panel, wxID_ANY, "Record");
    console_sizer->Add(m_record_btn, 0, wxALL | wxEXPAND, 5);
    
    controlsizer->Add(slider_sizer, 0, wxEXPAND);
    controlsizer->Add(console_sizer, 1, wxEXPAND);
    
    control_panel->SetSizer(controlsizer);
    
    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_canvas.get(), 1, wxEXPAND);
    sizer->Add(control_panel, 0, wxEXPAND);
    SetSizer(sizer);
    
    if (alg != "EnricoShader") {
        if (settings.get_algo() > 0) depth_select->Enable(true);
        alg_select->SetSelection(settings.get_algo());
        depth_select->SetSelection(settings.get_depth_algo());
        optimization_select->SetSelection(settings.get_optimization());
        convexity_spin->SetValue(int(settings.get_convexity()));
        csg_toggle->SetValue(settings.is_enabled());
    } else {
        alg_select->SetSelection(int(get_idx("EnricoShader", CSG_ALGS)));
        depth_select->Disable();
        optimization_select->Disable();
        convexity_spin->Disable();
        csg_toggle->Disable();
        auto renderer = std::make_shared<ShaderCSGRenderer>(canvas());
        canvas()->set_display(renderer);
    }
    
    m_ctl->add_display(m_canvas->get_display());
    m_canvas->get_display()->get_fps_counter().add_listener([this, fpstext](double fps) {
        fpstext->SetLabel(wxString::Format("fps: %.2f", fps) );
        m_fps_avg = 0.9 * m_fps_avg + 0.1 * fps;
    });
    
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &evt){
        if (m_canvas) RemoveChild(m_canvas.get());
        m_canvas.reset();
        if (!m_mouse.is_playing()) evt.Skip();
        else m_mouse.stop();
    });    
    
    Bind(wxEVT_MENU, [this](wxCommandEvent &) {
        wxFileDialog dlg(this, "Select project file",  wxEmptyString,
                         wxEmptyString, "*.3mf", wxFD_OPEN|wxFD_FILE_MUST_EXIST);

        if (dlg.ShowModal() == wxID_OK) load_model(dlg.GetPath().ToStdString());
    }, wxID_OPEN);
    
    Bind(wxEVT_MENU, [this](wxCommandEvent &) { Close(true); }, wxID_EXIT);
    
    Bind(wxEVT_SHOW, [this, ms_toggle](wxShowEvent &) {
        const wxSize ClientSize = GetClientSize();
        m_canvas->get_display()->set_active(ClientSize.x, ClientSize.y);
        enable_multisampling(ms_toggle->GetValue());
        
        // Do the repaint continuously
        m_canvas->Bind(wxEVT_IDLE, [this](wxIdleEvent &evt) {
            if (IsShown() && m_canvas->IsShown())
                m_canvas->get_display()->repaint();
            
            evt.RequestMore();
        });
        
        bind_canvas_events(m_mouse);
    });
    
    Bind(wxEVT_SLIDER, [this, slider](wxCommandEvent &) {
        m_ctl->move_clip_plane(double(slider->GetValue()));
    });
    
    ms_toggle->Bind(wxEVT_TOGGLEBUTTON, [this, ms_toggle](wxCommandEvent &){
        enable_multisampling(ms_toggle->GetValue());
        m_canvas->get_display()->repaint();
    });
    
    csg_toggle->Bind(wxEVT_TOGGLEBUTTON, [this, csg_toggle](wxCommandEvent &){
        CSGSettings stt = m_canvas->get_ocsg_display()->get_csgsettings();
        stt.enable_csg(csg_toggle->GetValue());
        m_canvas->get_ocsg_display()->apply_csgsettings(stt);
    });
    
    alg_select->Bind(wxEVT_COMBOBOX,
                     [this, alg_select, depth_select](wxCommandEvent &)
    {
        int sel = alg_select->GetSelection();
        depth_select->Enable(sel > 0);
        CSGSettings stt = m_canvas->get_ocsg_display()->get_csgsettings();
        stt.set_algo(OpenCSG::Algorithm(sel));
        m_canvas->get_ocsg_display()->apply_csgsettings(stt);
    });
    
    depth_select->Bind(wxEVT_COMBOBOX, [this, depth_select](wxCommandEvent &) {
        int sel = depth_select->GetSelection();
        CSGSettings stt = m_canvas->get_ocsg_display()->get_csgsettings();
        stt.set_depth_algo(OpenCSG::DepthComplexityAlgorithm(sel));
        m_canvas->get_ocsg_display()->apply_csgsettings(stt);
    });
    
    optimization_select->Bind(wxEVT_COMBOBOX,
                              [this, optimization_select](wxCommandEvent &) {
        int sel = optimization_select->GetSelection();
        CSGSettings stt = m_canvas->get_ocsg_display()->get_csgsettings();
        stt.set_optimization(OpenCSG::Optimization(sel));
        m_canvas->get_ocsg_display()->apply_csgsettings(stt);
    });
    
    convexity_spin->Bind(wxEVT_SPINCTRL, [this, convexity_spin](wxSpinEvent &) {
        CSGSettings stt = m_canvas->get_ocsg_display()->get_csgsettings();
        int c = convexity_spin->GetValue();
        
        if (c > 0) {
            stt.set_convexity(unsigned(c));
            m_canvas->get_ocsg_display()->apply_csgsettings(stt);
        }
    });
    
    m_record_btn->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &) {
        if (!m_ui_job) {
            m_stbar->set_status_text("No project loaded!");
            return;
        }
        
        if (m_record_btn->GetValue()) {
            if (auto c = m_canvas->get_display()->camera()) reset(*c);
            m_ctl->on_scene_updated(*m_scene);
            m_mouse.record(true);
        } else {
            m_mouse.record(false);
            wxFileDialog dlg(this, "Select output file",
                             wxEmptyString, wxEmptyString, "*.events",
                             wxFD_SAVE|wxFD_OVERWRITE_PROMPT);
            
            if (dlg.ShowModal() == wxID_OK) {
                std::fstream stream(dlg.GetPath().ToStdString(),
                                    std::fstream::out);
                
                if (stream.good()) {
                    stream << m_ui_job->get_project_fname() << "\n";
                    wxSize winsize = GetSize();
                    stream << winsize.x << " " << winsize.y << "\n";
                    m_mouse.save(stream);
                }
            }
        }
    });
}

void MyFrame::bind_canvas_events(MouseInput &ms)
{
    m_canvas->Bind(wxEVT_MOUSEWHEEL, [&ms](wxMouseEvent &evt) {
        ms.scroll(evt.GetWheelRotation(), evt.GetWheelDelta(),
                  evt.GetWheelAxis() == wxMOUSE_WHEEL_VERTICAL ?
                      Slic3r::GL::MouseInput::waVertical :
                      Slic3r::GL::MouseInput::waHorizontal);
    });

    m_canvas->Bind(wxEVT_MOTION, [&ms](wxMouseEvent &evt) {
        ms.move_to(evt.GetPosition().x, evt.GetPosition().y);
    });

    m_canvas->Bind(wxEVT_RIGHT_DOWN, [&ms](wxMouseEvent & /*evt*/) {
        ms.right_click_down();
    });

    m_canvas->Bind(wxEVT_RIGHT_UP, [&ms](wxMouseEvent & /*evt*/) {
        ms.right_click_up();
    });

    m_canvas->Bind(wxEVT_LEFT_DOWN, [&ms](wxMouseEvent & /*evt*/) {
        ms.left_click_down();
    });

    m_canvas->Bind(wxEVT_LEFT_UP, [&ms](wxMouseEvent & /*evt*/) {
        ms.left_click_up();
    });
    
    ms.add_listener(m_ctl);
}

void MyFrame::SLAJob::process() 
{
    using SlStatus = Slic3r::PrintBase::SlicingStatus;
    
    Slic3r::DynamicPrintConfig cfg;
    auto model = Slic3r::Model::read_from_file(m_fname, &cfg);
    
    m_print = std::make_unique<Slic3r::SLAPrint>();
    m_print->apply(model, cfg);
    
    Slic3r::PrintBase::TaskParams params;
    params.to_object_step = Slic3r::slaposHollowing;
    m_print->set_task(params);
    
    m_print->set_status_callback([this](const SlStatus &status) {
        update_status(status.percent, status.text);
    });
    
    try {
        m_print->process();
    } catch(std::exception &e) {
        update_status(0, wxString("Exception during processing: ") + e.what());
    }
}

//int main() {}
