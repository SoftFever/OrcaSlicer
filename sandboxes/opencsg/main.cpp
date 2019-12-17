#include <iostream>
#include <utility>
#include <memory>

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

#include "GLScene.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/Format/3mf.hpp"
#include "libslic3r/SLAPrint.hpp"

#include "slic3r/GUI/Job.hpp"
#include "slic3r/GUI/ProgressStatusBar.hpp"

using namespace Slic3r::GL;

class Canvas: public wxGLCanvas, public Slic3r::GL::Display
{
    shptr<wxGLContext> m_context;
public:
    
    void set_active(long w, long h) override
    {
        SetCurrent(*m_context);
        Slic3r::GL::Display::set_active(w, h);
    }
    
    void swap_buffers() override { SwapBuffers(); }
    
    template<class...Args>
    Canvas(Args &&...args): wxGLCanvas(std::forward<Args>(args)...)
    {
        auto ctx = new wxGLContext(this);
        if (!ctx || !ctx->IsOK()) {
            wxMessageBox("Could not create OpenGL context.", "Error",
                         wxOK | wxICON_ERROR);
            return;
        }
        
        m_context.reset(ctx);
    }

    ~Canvas() override
    {
        m_scene_cache.clear();
        m_context.reset();
    }
};

class MyFrame: public wxFrame
{
    shptr<Scene>      m_scene;    // Model
    shptr<Canvas>     m_canvas;   // View
    shptr<Controller> m_ctl;      // Controller

    shptr<Slic3r::GUI::ProgressStatusBar> m_stbar;
    uqptr<Slic3r::GUI::Job> m_ui_job;
    
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

        void process() override;
        
    protected:
        
        void finalize() override 
        {
            m_parent->m_scene->set_print(std::move(m_print));
            m_parent->m_stbar->set_status_text(
                        wxString::Format("Model %s loaded.", m_fname));
        }
    };
    
public:
    MyFrame(const wxString& title, const wxPoint& pos, const wxSize& size);
    
private:
    
    void bind_canvas_events_to_controller();
    
    void OnClose(wxCloseEvent& /*event*/) 
    {
        RemoveChild(m_canvas.get());
        m_canvas.reset();
        Destroy();
    }
    
    void OnOpen(wxCommandEvent &/*evt*/)
    {
        wxFileDialog dlg(this, "Select project file", 
                         wxEmptyString, wxEmptyString, "*.3mf");
        
        if (dlg.ShowModal() == wxID_OK)
        {
            m_ui_job = std::make_unique<SLAJob>(this, dlg.GetPath().ToStdString());
            m_ui_job->start();
        }
    }
    
    void OnShown(wxShowEvent&)
    {
        const wxSize ClientSize = GetClientSize();
        m_canvas->set_active(ClientSize.x, ClientSize.y);
        
        // Do the repaint continuously
        Bind(wxEVT_IDLE, [this](wxIdleEvent &evt) {
            m_canvas->repaint();
            evt.RequestMore();
        });
    }
};

class App : public wxApp {
    MyFrame *m_frame;
public:
    bool OnInit() override {
        m_frame = new MyFrame("PrusaSlicer OpenCSG Demo", wxDefaultPosition, wxSize(1024, 768));
        
        m_frame->Show( true );
        
        return true;
    }
};

wxIMPLEMENT_APP(App);

MyFrame::MyFrame(const wxString &title, const wxPoint &pos, const wxSize &size):
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
     WX_GL_SAMPLE_BUFFERS, GL_TRUE, WX_GL_SAMPLES, 4, 0};
    
    m_scene = std::make_shared<Scene>();
    m_ctl = std::make_shared<Controller>();
    m_ctl->set_scene(m_scene);
    
    m_canvas = std::make_shared<Canvas>(this, wxID_ANY, attribList,
                                        wxDefaultPosition, wxDefaultSize,
                                        wxWANTS_CHARS | wxFULL_REPAINT_ON_RESIZE);
    m_ctl->add_display(m_canvas);

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
            (const wxString &label, std::vector<wxString> &&list)
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
    
    auto alg_select = add_combobox("Algorithm", {"Auto", "Goldfeather", "SCS"});
    auto depth_select = add_combobox("Depth Complexity", {"Off", "OcclusionQuery", "On"});
    auto optimization_select = add_combobox("Optimization", { "Default", "ForceOn", "On", "Off" });
    depth_select->Disable();
    
    controlsizer->Add(slider_sizer, 0, wxEXPAND);
    controlsizer->Add(console_sizer, 1, wxEXPAND);
    
    control_panel->SetSizer(controlsizer);
    
    auto sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_canvas.get(), 1, wxEXPAND);
    sizer->Add(control_panel, 0, wxEXPAND);
    SetSizer(sizer);
    
    Bind(wxEVT_MENU, &MyFrame::OnOpen, this, wxID_OPEN);
    Bind(wxEVT_CLOSE_WINDOW, &MyFrame::OnClose, this);
    Bind(wxEVT_MENU, [this](wxCommandEvent &) { Close(true); }, wxID_EXIT);
    
    Bind(wxEVT_SHOW, &MyFrame::OnShown, this, GetId());
    
    Bind(wxEVT_SLIDER, [this, slider](wxCommandEvent &) {
        m_ctl->move_clip_plane(double(slider->GetValue()));
    });
    
    ms_toggle->Bind(wxEVT_TOGGLEBUTTON, [this, ms_toggle](wxCommandEvent &){
        enable_multisampling(ms_toggle->GetValue());
        m_canvas->repaint();
    });
    
    csg_toggle->Bind(wxEVT_TOGGLEBUTTON, [this, csg_toggle](wxCommandEvent &){
        CSGSettings settings = m_canvas->get_csgsettings();
        settings.enable_csg(csg_toggle->GetValue());
        m_canvas->apply_csgsettings(settings);
    });
    
    alg_select->Bind(wxEVT_COMBOBOX,
                     [this, alg_select, depth_select](wxCommandEvent &)
    {
        int sel = alg_select->GetSelection();
        depth_select->Enable(sel > 0);
        CSGSettings settings = m_canvas->get_csgsettings();
        settings.set_algo(OpenCSG::Algorithm(sel));
        m_canvas->apply_csgsettings(settings);
    });
    
    depth_select->Bind(wxEVT_COMBOBOX, [this, depth_select](wxCommandEvent &) {
        int sel = depth_select->GetSelection();
        CSGSettings settings = m_canvas->get_csgsettings();
        settings.set_depth_algo(OpenCSG::DepthComplexityAlgorithm(sel));
        m_canvas->apply_csgsettings(settings);
    });
    
    optimization_select->Bind(wxEVT_COMBOBOX,
                              [this, optimization_select](wxCommandEvent &) {
        int sel = optimization_select->GetSelection();
        CSGSettings settings = m_canvas->get_csgsettings();
        settings.set_optimization(OpenCSG::Optimization(sel));
        m_canvas->apply_csgsettings(settings);
    });
    
    convexity_spin->Bind(wxEVT_SPINCTRL, [this, convexity_spin](wxSpinEvent &) {
        CSGSettings settings = m_canvas->get_csgsettings();
        int c = convexity_spin->GetValue();
        
        if (c > 0) {
            settings.set_convexity(unsigned(c));
            m_canvas->apply_csgsettings(settings);
        }
    });
    
    bind_canvas_events_to_controller();
}

void MyFrame::bind_canvas_events_to_controller()
{
    m_canvas->Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent &evt) {
        m_ctl->on_scroll(evt.GetWheelRotation(), evt.GetWheelDelta(),
                         evt.GetWheelAxis() == wxMOUSE_WHEEL_VERTICAL ?
                             Slic3r::GL::MouseInput::waVertical :
                             Slic3r::GL::MouseInput::waHorizontal);
    });

    m_canvas->Bind(wxEVT_MOTION, [this](wxMouseEvent &evt) {
        m_ctl->on_moved_to(evt.GetPosition().x, evt.GetPosition().y);
    });

    m_canvas->Bind(wxEVT_RIGHT_DOWN, [this](wxMouseEvent & /*evt*/) {
        m_ctl->on_right_click_down();
    });

    m_canvas->Bind(wxEVT_RIGHT_UP, [this](wxMouseEvent & /*evt*/) {
        m_ctl->on_right_click_up();
    });

    m_canvas->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent & /*evt*/) {
        m_ctl->on_left_click_down();
    });

    m_canvas->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent & /*evt*/) {
        m_ctl->on_left_click_up();
    });

    m_canvas->Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
        // This is required even though dc is not used otherwise.
        wxPaintDC dc(m_canvas.get());
        
        // Set the OpenGL viewport according to the client size of this
        // canvas. This is done here rather than in a wxSizeEvent handler
        // because our OpenGL rendering context (and thus viewport setting) is
        // used with multiple canvases: If we updated the viewport in the
        // wxSizeEvent handler, changing the size of one canvas causes a
        // viewport setting that is wrong when next another canvas is
        // repainted.
        const wxSize ClientSize = m_canvas->GetClientSize();
        
        m_canvas->set_screen_size(ClientSize.x, ClientSize.y);
        m_canvas->repaint();
    }, m_canvas->GetId());
}

void MyFrame::SLAJob::process() 
{
    using Status = Slic3r::PrintBase::SlicingStatus;
    
    Slic3r::DynamicPrintConfig cfg;
    auto model = Slic3r::Model::read_from_file(m_fname, &cfg);
    
    m_print = std::make_unique<Slic3r::SLAPrint>();
    m_print->apply(model, cfg);
    
    Slic3r::PrintBase::TaskParams params;
    params.to_object_step = Slic3r::slaposHollowing;
    m_print->set_task(params);
    
    m_print->set_status_callback([this](const Status &status) {
        update_status(status.percent, status.text);
    });
    
    m_print->process();
}

//int main() {}
