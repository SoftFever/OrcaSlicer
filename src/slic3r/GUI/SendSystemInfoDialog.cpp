#include "SendSystemInfoDialog.hpp"

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Platform.hpp"
#include "libslic3r/Utils.hpp"

#include "slic3r/GUI/format.hpp"
#include "slic3r/Utils/Http.hpp"

#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"
#include "OpenGLManager.hpp"

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim_all.hpp>
#include <boost/property_tree/json_parser.hpp>

#include "GL/glew.h"

#include <wx/display.h>
#include <wx/htmllbox.h>
#include <wx/stattext.h>
#include <wx/timer.h>
#include <wx/utils.h>

#include <atomic>
#include <thread>

namespace Slic3r {
namespace GUI {



// Declaration of a free function defined in OpenGLManager.cpp:
std::string gl_get_string_safe(GLenum param, const std::string& default_value);


// A dialog with the information text and buttons send/dont send/ask later.
class SendSystemInfoDialog : public DPIDialog
{
    enum {
        MIN_WIDTH = 60,
        MIN_HEIGHT = 40
    };

public:
    SendSystemInfoDialog(wxWindow* parent);

private:
    bool send_info();
    /*const */std::string m_system_info_json; // will be const, this is FOR DEBUGGING
    wxButton* m_btn_send;
    wxButton* m_btn_dont_send;
    wxButton* m_btn_ask_later;

    void on_dpi_changed(const wxRect&) override;
};



// A dialog to show when the upload is in progress (with a Cancel button).
class SendSystemInfoProgressDialog : public wxDialog
{
public:
    SendSystemInfoProgressDialog(wxWindow* parent, const wxString& message)
        : wxDialog(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxCAPTION)
    {
        auto* text = new wxStaticText(this, wxID_ANY, message, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
        auto* btn = new wxButton(this, wxID_CANCEL, _L("Cancel"));
        auto* vsizer = new wxBoxSizer(wxVERTICAL);
        auto *top_sizer = new wxBoxSizer(wxVERTICAL);
        vsizer->Add(text, 1, wxEXPAND|wxALIGN_CENTER_HORIZONTAL);
        vsizer->AddSpacer(5);
        vsizer->Add(btn, 0, wxALIGN_CENTER_HORIZONTAL);
        top_sizer->Add(vsizer, 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT | wxBOTTOM, 10);
        SetSizer(top_sizer);
        #ifdef _WIN32
            wxGetApp().UpdateDlgDarkUI(this);
        #endif
    }
};



// A dialog with multiline read-only text control to show the JSON.
class ShowJsonDialog : public wxDialog
{
public:
    ShowJsonDialog(wxWindow* parent, const wxString& json, const wxSize& size)
        : wxDialog(parent, wxID_ANY, _L("Data to send"), wxDefaultPosition, size, wxCAPTION|wxRESIZE_BORDER)
    {
        auto* text = new wxTextCtrl(this, wxID_ANY, json,
                                    wxDefaultPosition, wxDefaultSize,
                                    wxTE_MULTILINE
                                    // | wxTE_READONLY // commented out for DEBUGGING ONLY
                                    | wxTE_DONTWRAP);
        text->SetFont(wxGetApp().code_font());
        text->ShowPosition(0);

        auto* btn = new wxButton(this, wxID_CANCEL, _L("Close"));
        auto* vsizer = new wxBoxSizer(wxVERTICAL);
        auto *top_sizer = new wxBoxSizer(wxVERTICAL);
        vsizer->Add(text, 1, wxEXPAND);
        vsizer->AddSpacer(5);
        vsizer->Add(btn, 0, wxALIGN_CENTER_HORIZONTAL);
        top_sizer->Add(vsizer, 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT | wxBOTTOM, 10);
        SetSizer(top_sizer);
        #ifdef _WIN32
            wxGetApp().UpdateDlgDarkUI(this);
        #endif

        m_text = text; // DEBUGGING ONLY
    }

    // DEBUGGING ONLY:
    wxTextCtrl* m_text;
    std::string get_data() const // debugging only
    {
        return m_text->GetValue().ToUTF8().data();
    }
};



// Last version where the info was sent / dialog dismissed is saved in appconfig.
// Only show the dialog when this info is not found (e.g. fresh install) or when
// current version is newer. Only major and minor versions are compared.
static bool should_dialog_be_shown()
{
    return true; // DEBUGGING ONLY

    std::string last_sent_version = wxGetApp().app_config->get("version_system_info_sent");
    Semver semver_current(SLIC3R_VERSION);
    Semver semver_last_sent;
    if (! last_sent_version.empty())
        semver_last_sent = Semver(last_sent_version);

    if (semver_current.prerelease() && std::string(semver_current.prerelease()) != "rc")
        return false; // Only show in rcs / finals.

    // Show the dialog if current > last, but they differ in more than just patch.
    return ((semver_current.maj() > semver_last_sent.maj())
        || (semver_current.maj() == semver_last_sent.maj() && semver_current.min() > semver_last_sent.min() ));
}



// Following function saves current PrusaSlicer version into app config.
// It will be later used to decide whether to open the dialog or not.
static void save_version()
{
    wxGetApp().app_config->set("version_system_info_sent", std::string(SLIC3R_VERSION));
}



static std::map<std::string, std::string> parse_lscpu_etc(const std::string& name, char delimiter)
{
    std::map<std::string, std::string> out;
    constexpr size_t max_len = 100;
    char cline[max_len] = "";
    FILE* fp = popen(name.data(), "r");
    if (fp != NULL) {
        while (fgets(cline, max_len, fp) != NULL) {
            std::string line(cline);
            line.erase(std::remove_if(line.begin(), line.end(),
                           [](char c) { return c=='\"' || c=='\r' || c=='\n'; }),
                       line.end());
            size_t pos = line.find(delimiter);
            if (pos < line.size() - 1) {
                std::string key = line.substr(0, pos);
                std::string value = line.substr(pos+1);
                boost::trim_all(key); // remove leading and trailing spaces
                boost::trim_all(value);
                out[key] = value;
            }
        }
        pclose(fp);
    }
    return out;
}



// Following function generates one string that will be shown in the preview
// and later sent if confirmed by the user.
static std::string generate_system_info_json()
{
    // Calculate hash of username so it is possible to identify duplicates.
    // The result is mod 10000 so most of the information is lost and it is
    // not possible to unhash the username. It is more than enough to help
    // identify duplicate entries.
    size_t datadir_hash = std::hash<std::string>{}(std::string(wxGetUserId().ToUTF8().data())) % 10000;

    // Get system language.
    std::string sys_language = "Unknown";
    const wxLanguage lang_system = wxLanguage(wxLocale::GetSystemLanguage());
    if (lang_system != wxLANGUAGE_UNKNOWN)
        sys_language = wxLocale::GetLanguageInfo(lang_system)->CanonicalName.ToUTF8().data();

    // Build a property tree with all the information.
    namespace pt = boost::property_tree;

    pt::ptree data_node;
    data_node.put("PrusaSlicerVersion", SLIC3R_VERSION);
    data_node.put("BuildID", SLIC3R_BUILD_ID);
    data_node.put("UsernameHash", datadir_hash);
    data_node.put("Platform", platform_to_string(platform()));
    data_node.put("PlatformFlavor", platform_flavor_to_string(platform_flavor()));
    data_node.put("OSDescription", wxPlatformInfo::Get().GetOperatingSystemDescription().ToUTF8().data());
#ifdef __linux__
    std::string distro_id = wxGetLinuxDistributionInfo().Id.ToUTF8().data(); // uses lsb-release
    std::string distro_ver = wxGetLinuxDistributionInfo().Release.ToUTF8().data();
    if (distro_id.empty()) { // lsb-release probably not available
        std::map<std::string, std::string> dist_info = parse_lscpu_etc("cat /etc/*release", '=');
        distro_id = dist_info["ID"];
        distro_ver = dist_info["VERSION_ID"];
    }
    data_node.put("Linux_DistroID", distro_id);
    data_node.put("Linux_DistroVer", distro_ver);
    data_node.put("Linux_Wayland", wxGetEnv("WAYLAND_DISPLAY", nullptr));
#endif
    data_node.put("wxWidgets", wxVERSION_NUM_DOT_STRING);
#ifdef __WXGTK__
    data_node.put("GTK",
    #if defined(__WXGTK2__)
        2
    #elif defined(__WXGTK3__)
        3
    #elif defined(__WXGTK4__)
        4
    #elif defined(__WXGTK5__)
        5
    #else
        "Unknown"
    #endif
    );
#endif // __WXGTK__
    data_node.put("SystemLanguage", sys_language);
    data_node.put("TranslationLanguage: ", wxGetApp().app_config->get("translation_language"));

    pt::ptree hw_node;
    hw_node.put("ArchName", wxPlatformInfo::Get().GetArchName());
    hw_node.put("RAM_MB", size_t(Slic3r::total_physical_memory()/1000000));

    // Now get some CPU info:
    pt::ptree cpu_node;
#ifdef _WIN32

#elif __APPLE__
     std::map<std::string, std::string> sysctl = parse_lscpu_etc("sysctl -a", ':');
     cpu_node.put("CPU(s)",     sysctl["machdep.cpu.core_count"]);
     cpu_node.put("CPU_Model",  sysctl["machdep.cpu.brand_string"]);
     cpu_node.put("CPU_Vendor", sysctl["machdep.cpu.vendor"]);
#else
    std::map<std::string, std::string> lscpu = parse_lscpu_etc("lscpu", ':');
    cpu_node.put("Arch",   lscpu["Architecture"]);
    cpu_node.put("Cores",  lscpu["CPU(s)"]);
    cpu_node.put("Model",  lscpu["Model name"]);
    cpu_node.put("Vendor", lscpu["Vendor ID"]);
#endif
    hw_node.add_child("CPU", cpu_node);

    pt::ptree monitors_node;
    for (int i=0; i<int(wxDisplay::GetCount()); ++i) {
        wxDisplay display(i);
        double scaling = -1.;
        #if wxCHECK_VERSION(3, 1, 2) // we have wxDisplag::GetPPI
            int std_ppi = 96;
            #ifdef __WXOSX__ // see impl of wxDisplay::GetStdPPIValue from 3.1.5
                std_ppi = 72;
            #endif
            scaling = double(display.GetPPI().GetWidth()) / std_ppi;
        #endif
        pt::ptree monitor_node; // Create an unnamed node containing the value
        monitor_node.put("width", display.GetGeometry().GetWidth());
        monitor_node.put("height", display.GetGeometry().GetHeight());
        std::stringstream ss;
        ss << std::setprecision(3) << scaling;
        monitor_node.put("scaling", ss.str() );
        monitors_node.push_back(std::make_pair("", monitor_node));
    }
    hw_node.add_child("Monitors", monitors_node);
    data_node.add_child("Hardware", hw_node);

    pt::ptree opengl_node;
    opengl_node.put("Version", OpenGLManager::get_gl_info().get_version());
    opengl_node.put("GLSLVersion", OpenGLManager::get_gl_info().get_glsl_version());
    opengl_node.put("Vendor", OpenGLManager::get_gl_info().get_vendor());
    opengl_node.put("Renderer", OpenGLManager::get_gl_info().get_renderer());
    // Generate list of OpenGL extensions:
    std::string extensions_str = gl_get_string_safe(GL_EXTENSIONS, "");
    std::vector<std::string> extensions_list;
    boost::split(extensions_list, extensions_str, boost::is_any_of(" "), boost::token_compress_off);
    std::sort(extensions_list.begin(), extensions_list.end());
    pt::ptree extensions_node;
    for (const std::string& s : extensions_list) {
        if (s.empty())
            continue;
        pt::ptree ext_node; // Create an unnamed node containing the value
        ext_node.put("", s);
        extensions_node.push_back(std::make_pair("", ext_node)); // Add this node to the list.
    }
    opengl_node.add_child("Extensions", extensions_node);
    data_node.add_child("OpenGL", opengl_node);

    pt::ptree root;
    root.add_child("data", data_node);

    // Serialize the tree into JSON and return it.
    std::stringstream ss;
    pt::write_json(ss, root);
    return ss.str();

    // FURTHER THINGS TO CONSIDER:
    //std::cout << wxPlatformInfo::Get().GetOperatingSystemFamilyName() << std::endl;          // Unix
    // ? CPU, GPU, UNKNOWN ?
    // printers? will they be installed already?
}



SendSystemInfoDialog::SendSystemInfoDialog(wxWindow* parent)
    : m_system_info_json{generate_system_info_json()},
    GUI::DPIDialog(parent, wxID_ANY, _L("Send system info"), wxDefaultPosition, wxDefaultSize,
           wxDEFAULT_DIALOG_STYLE)
{
    // Get current PrusaSliver version info.
    std::string app_name;
    {
        Semver semver(SLIC3R_VERSION);
        bool is_alpha = std::string{semver.prerelease()}.find("alpha") != std::string::npos;
        bool is_beta = std::string{semver.prerelease()}.find("beta") != std::string::npos;
        app_name = std::string(SLIC3R_APP_NAME) + " " + std::to_string(semver.maj())
                               + "." + std::to_string(semver.min()) + " "
                               + (is_alpha ? "Alpha" : is_beta ? "Beta" : "");
    }

    // Get current source file name.
    std::string filename(__FILE__);
    size_t last_slash_idx = filename.find_last_of("/\\");
    if (last_slash_idx != std::string::npos)
        filename = filename.substr(last_slash_idx+1);

    // Set dialog background color, fonts, etc.
    SetFont(wxGetApp().normal_font());
    wxColour bgr_clr = wxGetApp().get_window_default_clr();
    SetBackgroundColour(bgr_clr);
    const auto text_clr = wxGetApp().get_label_clr_default();
    auto text_clr_str = wxString::Format(wxT("#%02X%02X%02X"), text_clr.Red(), text_clr.Green(), text_clr.Blue());
    auto bgr_clr_str = wxString::Format(wxT("#%02X%02X%02X"), bgr_clr.Red(), bgr_clr.Green(), bgr_clr.Blue());


    auto *topSizer = new wxBoxSizer(wxVERTICAL);
    auto *vsizer = new wxBoxSizer(wxVERTICAL);

    wxString text0 = GUI::format_wxstr(_L("This is the first time you are running %1%. We would like to "
           "ask you to send some of your system information to us. This will only "
           "happen once and we will not ask you to do this again (only after you "
           "upgrade to the next version)."), app_name );
    wxString label1 = _L("Why is it needed");
    wxString text1 = _L("If we know your hardware, operating system, etc., it will greatly help us "
        "in development, prioritization and possible deprecation of features that "
        "are no more needed (for example legacy OpenGL support). This will help "
        "us to focus our effort more efficiently and spend time on features that "
        "are needed the most.");
    wxString label2 = _L("Is it safe?");
    wxString text2 = GUI::format_wxstr(
        _L("We do not send any personal information nor anything that would allow us "
           "to identify you later. To detect duplicate entries, a number derived "
           "from your username is sent, but it cannot be used to recover the username. "
           "Apart from that, only general data about your OS, hardware and OpenGL "
           "installation are sent. PrusaSlicer is open source, if you want to "
           "inspect the code actually performing the communication, see %1%."),
           std::string("<i>") + filename + "</i>");
    wxString label3 = _L("Show verbatim data that will be sent");

    auto* html_window = new wxHtmlWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_NEVER);
    wxString html = GUI::format_wxstr(
            "<html><body bgcolor=%1%><font color=%2%>"
            + text0 + "<br /><br />"
            + "<b>" + label1 + "</b><br />"
            + text1 + "<br /><br />"
            + "<b>" + label2 + "</b><br />"
            + text2 + "<br /><br />"
            + "<b><a href=\"show\">" + label3 + "</a></b><br />"
            + "</font></body></html>", bgr_clr_str, text_clr_str);
    html_window->SetPage(html);
    html_window->Bind(wxEVT_HTML_LINK_CLICKED, [this](wxHtmlLinkEvent &evt) {
                                                   ShowJsonDialog dlg(this, m_system_info_json, GetSize().Scale(0.9, 0.7));
                                                   dlg.ShowModal();
                                                   m_system_info_json = dlg.get_data(); // DEBUGGING ONLY
    });

    vsizer->Add(html_window, 1, wxEXPAND);

    m_btn_ask_later = new wxButton(this, wxID_ANY, _L("Ask me next time"));
    m_btn_dont_send = new wxButton(this, wxID_ANY, _L("Do not send anything"));
    m_btn_send = new wxButton(this, wxID_ANY, _L("Send system info"));

    auto* hsizer = new wxBoxSizer(wxHORIZONTAL);
    const int em = GUI::wxGetApp().em_unit();
    hsizer->Add(m_btn_ask_later);
    hsizer->AddSpacer(em);
    hsizer->Add(m_btn_dont_send);
    hsizer->AddSpacer(em);
    hsizer->Add(m_btn_send);

    vsizer->Add(hsizer, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, 10);
    topSizer->Add(vsizer, 1, wxEXPAND | wxLEFT | wxTOP | wxRIGHT, 10);

    SetSizer(topSizer);
    topSizer->SetSizeHints(this);


#ifdef _WIN32
    wxGetApp().UpdateDlgDarkUI(this);
#endif

    const auto size = GetSize();
    SetSize(std::max(size.GetWidth(), MIN_WIDTH * em),
            std::max(size.GetHeight(), MIN_HEIGHT * em));

    m_btn_send->Bind(wxEVT_BUTTON, [this](const wxEvent&)
                                    {
                                        if (send_info()) {
                                            save_version();
                                            EndModal(0);
                                        }
                                    });
    m_btn_dont_send->Bind(wxEVT_BUTTON, [this](const wxEvent&)
                                         {
                                             save_version();
                                             EndModal(0);
                                         });
    m_btn_ask_later->Bind(wxEVT_BUTTON, [this](const wxEvent&) { EndModal(0); });
}



void SendSystemInfoDialog::on_dpi_changed(const wxRect&)
{
    const int& em = em_unit();
    msw_buttons_rescale(this, em, { m_btn_send->GetId(),
                                    m_btn_dont_send->GetId(),
                                    m_btn_ask_later->GetId() });
    SetMinSize(wxSize(MIN_WIDTH * em, MIN_HEIGHT * em));
    Fit();
    Refresh();
}



// This actually sends the info.
bool SendSystemInfoDialog::send_info()
{
    std::atomic<int> job_done = false; // Flag to communicate between threads.
    struct Result {
        enum {
            Success,
            Cancelled,
            Error
        } value;
        wxString str;
    } result; // No synchronization needed, UI thread reads only after worker is joined.

    auto send = [&job_done, &result](const std::string& data) {
        const std::string url = "https://files.prusa3d.com/wp-json/v1/ps";
        Http http = Http::post(url);
        http.header("Content-Type", "application/json")
            .set_post_body(data)
            .on_complete([&result](std::string body, unsigned status) {
                result = { Result::Success, _L("System info sent successfully. Thank you.") };

                // DEBUGGING ONLY:
                std::cout << "Response: " << std::endl << body << std::endl << "-----------------" << std::endl;
            })
            .on_error([&result](std::string body, std::string error, unsigned status) {
                result = { Result::Error, GUI::format_wxstr(_L("Sending system info failed! Status: %1%"), status) };
            })
            .on_progress([&job_done, &result](Http::Progress, bool &cancel) {
                if (job_done) // UI thread wants us to cancel.
                    cancel = true;
                if (cancel)
                    result = { Result::Cancelled, _L("Sending system info was cancelled.") };
            })
            .perform_sync();
        job_done = true; // So that the dialog knows we are done.
    };

    std::thread sending_thread(send, m_system_info_json);
    SendSystemInfoProgressDialog dlg(this, _L("Sending system info..."));
    wxTimer timer(&dlg); // Periodically check the status of the other thread, close dialog when done.
    dlg.Bind(wxEVT_TIMER, [&dlg, &job_done](wxTimerEvent&){ if (job_done) dlg.EndModal(0); });
    timer.Start(50);
    dlg.ShowModal();
    // The dialog is closed, either by user, or by the now terminated worker thread.
    job_done = true;       // In case the user closed the dialog, let the other thread know
    sending_thread.join(); // and wait until it terminates.

    InfoDialog info_dlg(wxGetApp().mainframe, wxEmptyString, result.str);
    info_dlg.ShowModal();
    return result.value == Result::Success;
}



// The only function callable from outside this unit.
void show_send_system_info_dialog_if_needed()
{
    if (wxGetApp().is_gcode_viewer() || ! should_dialog_be_shown())
        return;

    SendSystemInfoDialog dlg(wxGetApp().mainframe);
    dlg.ShowModal();
}





} // namespace GUI
} // namespace Slic3r
